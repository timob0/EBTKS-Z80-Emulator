#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ebtks_modem_uart.h"
#include "ebtks_modem_hayes.h"

// 8250-compatible, polling-only virtual modem at Z80 ports 88h..8Fh.
// CPU port I/O NEVER calls Wi-Fi, malloc, DNS, delays or network functions.
// The dedicated core-0 worker owns the WiFiClient and Hayes parser. The two
// FIFO directions and UART register file are guarded by tiny critical sections.
// This device is independent of the console at 80h/85h and Teensy SD/UART.
namespace CpmWifiModem {
static constexpr uint8_t kBase=0x88;
static portMUX_TYPE uartMux=portMUX_INITIALIZER_UNLOCKED;
static EbtksModem::Uart8250 uart;
static TaskHandle_t workerHandle=nullptr;
static WiFiClient socketClient; // never touched by the Z80 CPU task
static EbtksModem::Hayes hayes;
static bool online=false,commandMode=true;
static uint8_t telState=0,telCommand=0,sbOption=0,sbValue=0;
static bool txBinary=false;
static uint8_t response[256]={};
static uint16_t responseIn=0,responseOut=0;
static uint8_t netTx[512]={};
static uint16_t netIn=0,netOut=0;
static uint32_t lastDataMs=0,plusStartMs=0;
static uint8_t pendingPluses=0;

inline bool isPort(uint8_t p){return (p&0xf8)==kBase;}
inline uint8_t readPort(uint8_t p) {
  portENTER_CRITICAL(&uartMux);
  uint8_t v=uart.read(static_cast<uint8_t>(p-kBase));
  portEXIT_CRITICAL(&uartMux);
  return v;
}
inline void writePort(uint8_t p,uint8_t v) {
  portENTER_CRITICAL(&uartMux);
  uart.write(static_cast<uint8_t>(p-kBase),v);
  portEXIT_CRITICAL(&uartMux);
}
inline bool takeTx(uint8_t& c) {
  portENTER_CRITICAL(&uartMux);
  bool ok=uart.popTx(c);
  portEXIT_CRITICAL(&uartMux);
  return ok;
}
inline uint16_t rxFree() {
  portENTER_CRITICAL(&uartMux);
  uint16_t n=uart.rxFree();
  portEXIT_CRITICAL(&uartMux);
  return n;
}
inline void pushRx(uint8_t c) {
  portENTER_CRITICAL(&uartMux);
  uart.pushRx(c);
  portEXIT_CRITICAL(&uartMux);
}
inline void setCarrier(bool on) {
  portENTER_CRITICAL(&uartMux);
  uart.setCarrier(on);
  portEXIT_CRITICAL(&uartMux);
}
static uint16_t next(uint16_t p,uint16_t size){return (p+1==size)?0:static_cast<uint16_t>(p+1);}
static uint16_t netFree(){return static_cast<uint16_t>((netOut+sizeof(netTx)-netIn-1)%sizeof(netTx));}
static bool queueNet(uint8_t c) {
  uint16_t n=next(netIn,sizeof(netTx));
  if(n==netOut)return false;
  netTx[netIn]=c;netIn=n;return true;
}
static void queueWire(uint8_t c) {
  // Once Telnet BINARY has been negotiated by the remote endpoint,
  // raw 0xff is still escaped as IAC IAC but CR NUL is no longer needed.
  if(c==255) {queueNet(255);queueNet(255);}
  else if(c==13 && !txBinary){queueNet(13);queueNet(0);}
  else queueNet(c);
}
static void queueResponseByte(uint8_t c) {
  const uint16_t n=next(responseIn,sizeof(response));
  if(n!=responseOut){response[responseIn]=c;responseIn=n;}
}
static void queueText(const char* s) {
  while(*s)queueResponseByte(static_cast<uint8_t>(*s++));
}
static void flushResponse() {
  for(int i=0;i<64 && responseOut!=responseIn && rxFree();++i) {
    pushRx(response[responseOut]);
    responseOut=next(responseOut,sizeof(response));
  }
}
static void result(uint8_t code) {
  if(hayes.quiet())return;
  if(!hayes.verbose()) {
    // Hayes numeric result codes: OK=0 CONNECT=1 NO CARRIER=3 ERROR=4.
    queueResponseByte(static_cast<uint8_t>('0'+code));
    queueResponseByte(13);
    return;
  }
  switch(code) {
    case 0:queueText("\r\nOK\r\n");break;
    case 1:queueText("\r\nCONNECT 9600\r\n");break;
    case 3:queueText("\r\nNO CARRIER\r\n");break;
    default:queueText("\r\nERROR\r\n");break;
  }
}
static void hangup(bool respond) {
  socketClient.stop();
  online=false;commandMode=true;
  netIn=netOut=0;pendingPluses=0;txBinary=false;
  telState=0;
  setCarrier(false);
  if(respond)result(0);
}
static void handleHayes(const EbtksModem::HayesEvent& e) {
  using EbtksModem::HayesEvent;
  switch(e.kind) {
    case HayesEvent::NONE:break;
    case HayesEvent::OK:result(0);break;
    case HayesEvent::ERROR:result(4);break;
    case HayesEvent::INFO:
      queueText("\r\nEBTKS Wi-Fi modem 0.1\r\n");
      result(0);break;
    case HayesEvent::HANGUP:hangup(true);break;
    case HayesEvent::ONLINE:
      if(online&&socketClient.connected()) {
        commandMode=false;lastDataMs=millis();result(1);
      } else result(3);
      break;
    case HayesEvent::DIAL: {
      hangup(false);
      bool ok=(WiFi.status()==WL_CONNECTED) &&
              socketClient.connect(e.host,e.port,3000);
      if(ok) {
        socketClient.setNoDelay(true);
        online=true;commandMode=false;lastDataMs=millis();
        setCarrier(true);
        result(1);
      } else {socketClient.stop();result(3);}
      break;
    }
  }
}
// Telnet network virtual terminal adapter. Negotiation bytes must not enter
// the CP/M serial RX FIFO. Accept echo, suppress-go-ahead and binary;
// refuse unsupported options. Announce ANSI terminal type and 80x24 size.
static void telReply(uint8_t command,uint8_t option) {
  queueNet(255);queueNet(command);queueNet(option);
}
static void telNetIncoming(uint8_t c) {
  switch(telState) {
    case 0: // DATA
      if(c==255)telState=1;
      else pushRx(c);
      break;
    case 1: // after IAC
      if(c==255){pushRx(255);telState=0;}
      else if(c>=251&&c<=254){telCommand=c;telState=2;}
      else if(c==250){sbOption=sbValue=0;telState=3;}
      else telState=0;
      break;
    case 2: { // WILL/WONT/DO/DONT with option
      const bool accept=(c==0||c==1||c==3||c==24||c==31);
      if(telCommand==251)telReply((c==0||c==1||c==3)?253:254,c); // WILL
      else if(telCommand==253) {
        telReply(accept?251:252,c); // DO
        if(c==0 && accept)txBinary=true;
        if(c==31) {
          const uint8_t naws[]={255,250,31,0,80,0,24,255,240};
          for(uint8_t v:naws)queueNet(v);
        }
      }
      else if(telCommand==252 && c==0)txBinary=false;
      else if(telCommand==254 && c==0)txBinary=false;
      telState=0;break;
    }
    case 3: // subnegotiation: record first two data bytes only
      if(c==255)telState=4;
      else if(!sbOption)sbOption=c;
      else if(!sbValue)sbValue=c;
      break;
    case 4:
      if(c==240) { // IAC SE
        if(sbOption==24&&sbValue==1) {
          const uint8_t ttype[]={255,250,24,0,'A','N','S','I',255,240};
          for(uint8_t v:ttype)queueNet(v);
        }
        telState=0;
      } else if(c!=255)telState=3;
      break;
  }
}
static void flushNet() {
  for(int loops=0;loops<4 && netOut!=netIn && socketClient.connected();++loops) {
    // Up to 96 bytes in one socket write; avoid one TCP send per character.
    uint8_t buf[96];size_t n=0;
    uint16_t p=netOut;
    while(p!=netIn && n<sizeof(buf)){buf[n++]=netTx[p];p=next(p,sizeof(netTx));}
    const size_t sent=socketClient.write(buf,n);
    if(!sent)break; // don't discard unaccepted tail
    for(size_t i=0;i<sent;++i)netOut=next(netOut,sizeof(netTx));
    if(sent<n)break;
  }
}
static void flushPluses() {
  while(pendingPluses){queueWire('+');--pendingPluses;}
  lastDataMs=millis();
}
static void outgoingData(uint8_t c,uint32_t now) {
  // Guard-time Hayes escape: silence >=1s, then +++, then silence >=1s.
  if(pendingPluses) {
    if(c=='+' && pendingPluses<3 && now-plusStartMs<1000) {
      ++pendingPluses;return;
    }
    flushPluses(); // interrupted or more than 3 pluses = literal traffic
  }
  if(c=='+' && now-lastDataMs>=1000) {
    pendingPluses=1;plusStartMs=now;return;
  }
  queueWire(c);lastDataMs=now;
}
static void pollEscapes(uint32_t now) {
  if(!pendingPluses)return;
  if(now-plusStartMs<1000)return;
  if(pendingPluses==3) {
    pendingPluses=0;commandMode=true;result(0);
  } else flushPluses();
}
static void worker(void*) {
  // Bounded memory and only one socket; keep CP/M and Telnet UI responsive.
  for(;;) {
    const uint32_t now=millis();
    if(online && !commandMode)pollEscapes(now);
    // Consume no more Z80 bytes if TCP output buffering is congested.
    for(int n=0;n<48 && (!online||commandMode||netFree()>8);++n) {
      uint8_t c;
      if(!takeTx(c))break;
      if(!online||commandMode) {
        if(hayes.echo())queueResponseByte(c);
        handleHayes(hayes.feed(c));
      } else outgoingData(c,millis());
    }
    if(online) {
      flushNet();
      // Reserve RX capacity before reading network bytes, so slow CP/M
      // software applies backpressure to TCP instead of losing BBS data.
      for(int i=0;i<64 && rxFree()>16 && socketClient.available()>0;++i) {
        int c=socketClient.read();
        if(c<0)break;
        telNetIncoming(static_cast<uint8_t>(c));
      }
      // Retain data already received by WiFiClient before reporting close.
      if(!socketClient.connected() && socketClient.available()==0) {
        hangup(false);
        result(3);
      }
    }
    flushResponse();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
inline void start() {
  if(workerHandle)return;
  BaseType_t ok=xTaskCreatePinnedToCore(worker,"CPMWifiModem",5120,nullptr,
                                        1,&workerHandle,0);
  if(ok!=pdPASS) {
    workerHandle=nullptr;
    EbtksDebugSerial.println("CP/M modem: worker allocation failed");
  } else EbtksDebugSerial.println("CP/M modem: virtual UART 88h-8Fh online");
}
} // namespace CpmWifiModem
