#include "globals.h"
#include "console_ansi_probe.h"
#include <lwip/sockets.h>
#include <errno.h>
#include <stdlib.h>
#pragma once

void serialTask(void *parameter) {
  Serial.println("Console router task started");
  serial_t = true;
  // Fewer, larger WebSocket frames reduce queue pressure during screen redraws.
  uint8_t chunk[256];
  // A modest heap buffer is allocated ONLY while Telnet is connected.
  // Never allocate another permanent worker stack or add static DRAM/BSS.
  // Socket output uses lwIP MSG_DONTWAIT; Arduino-ESP32 2.0.17's
  // WiFiClient::write() internally performs up to ten 1-second select()
  // retries, which can lock the shared ConsoleTask for much longer than
  // our previous outer 2-second guard.
  static constexpr size_t kTelnetTxCapacity=1024;
  uint8_t *telnetTx=nullptr;
  size_t telnetEnd=0,telnetStart=0;
  TickType_t telnetLastProgress=0;
  auto dropSlowTelnet = [&](const char *reason) {
    EbtksDebugSerial.printf("Telnet TX %s; dropping client (queued=%u, free8=%lu, largest8=%lu)\n",
                           reason,(unsigned)(telnetEnd-telnetStart),
                           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    serverClient.stop();
    free(telnetTx);telnetTx=nullptr;
    telnetEnd=telnetStart=0;
  };

  // One allocation-free virtual terminal probe shared by TFT and Web.
  // Process bytes in console order, even across 256-byte output chunks.
  EbtksAnsiProbe::Terminal ansiProbe;
  auto replyToGuest = [](const char *reply, size_t len) {
    // An external Telnet terminal normally supplies its own DA/DSR replies.
    // Do not inject a competing answer when one is connected.
    if (serverClient.connected()) return;
    // The 1024-byte input ring is shared with real keyboard input.
    // Never inject a partial ANSI response when the ring is full.
    const size_t cap = sizeof(rxBuf);
    const size_t freeSlots = (rxOutPtr + cap - rxInPtr - 1) % cap;
    if (len > freeSlots) return;
    for (size_t i = 0; i < len; ++i)
      consoleRxEnqueue((uint8_t)reply[i]);
  };

  // Minimal Telnet input parser.  Telnet option negotiation arrives on the
  // same TCP stream as terminal characters; those IAC command bytes must not
  // be passed to the emulated Z80 UART.
  enum TelnetRxState : uint8_t { TEL_DATA, TEL_IAC, TEL_IAC_OPTION, TEL_SB, TEL_SB_IAC };
  TelnetRxState telState = TEL_DATA;

  // In Telnet NVT mode, a carriage return is normally sent as CR NUL
  // (literal carriage return) or CR LF (new line).  CP/M wants a single CR
  // for Enter, so remember a received CR and discard a following NUL/LF.
  bool telnetAfterCr = false;

  for (;;) {
    size_t n = 0;
    while (txOutPtr != txInPtr && n < sizeof(chunk)) {
      chunk[n++] = txBuf[txOutPtr];
      uint16_t next = txOutPtr + 1;
      if (next >= sizeof(txBuf)) next = 0;
      txOutPtr = next;
    }

    // Reclaim buffered Telnet data when its peer disconnects. Do NOT carry
    // unsent CP/M bytes into a subsequent client session.
    if (!serverClient.connected() && telnetTx) {
      free(telnetTx);telnetTx=nullptr;
      telnetStart=telnetEnd=0;
    }

    if (n) {
      // Process TFT and console input probes before touching the TCP socket.
      // Even if Telnet is backed up, physical terminal output never waits
      // for Wi-Fi write retries.
      Cpm85Input::observeConsoleOutput(chunk,n);
      EbtksTft::mirrorFeed(chunk,n);
      ansiProbe.feed(chunk,n,replyToGuest);
      webTerminalWrite(chunk,n);

      if (serverClient.connected()) {
        if (!telnetTx) {
          telnetTx=static_cast<uint8_t*>(malloc(kTelnetTxCapacity));
          if (!telnetTx) dropSlowTelnet("buffer allocation failed");
          else telnetLastProgress=xTaskGetTickCount();
        }
        if (telnetTx) {
          // Compact only on demand. Unsent bytes stay in FIFO order.
          if (telnetStart && telnetEnd+n>kTelnetTxCapacity) {
            const size_t left=telnetEnd-telnetStart;
            memmove(telnetTx,telnetTx+telnetStart,left);
            telnetEnd=left;telnetStart=0;
          }
          if (telnetEnd+n>kTelnetTxCapacity) {
            // Shedding one slow client is safer than blocking CP/M, TFT,
            // keyboard, HTTP and SPI behind a full network send queue.
            dropSlowTelnet("buffer full");
          } else {
            memcpy(telnetTx+telnetEnd,chunk,n);
            telnetEnd+=n;
          }
        }
      }
    }

    // At most one nonblocking socket send per console iteration, even if the
    // peer TCP window is closed. This cannot wait ten seconds in
    // WiFiClient::write(), and preserves partial writes for the next pass.
    if (telnetTx && telnetEnd>telnetStart && serverClient.connected()) {
      const int fd=serverClient.fd();
      const ssize_t wrote=(fd>=0)?
          ::send(fd,telnetTx+telnetStart,telnetEnd-telnetStart,MSG_DONTWAIT)
          : -1;
      if (wrote>0) {
        telnetStart+=(size_t)wrote;
        telnetLastProgress=xTaskGetTickCount();
        if (telnetStart==telnetEnd)telnetStart=telnetEnd=0;
      } else if(wrote==0) {
        dropSlowTelnet("socket closed");
      } else if(fd<0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) {
        dropSlowTelnet("socket error");
      } else if((TickType_t)(xTaskGetTickCount()-telnetLastProgress)
                >=pdMS_TO_TICKS(2000)) {
        dropSlowTelnet("no progress for 2 s");
      }
    }

    while (serverClient.connected() && serverClient.available()) {
      uint8_t c = (uint8_t)serverClient.read();

      switch (telState) {
        case TEL_DATA:
          // Normalize Telnet's CR NUL / CR LF convention to one CP/M CR.
          // The CR itself was already queued on the previous byte.
          if (telnetAfterCr) {
            telnetAfterCr = false;
            if (c == 0 || c == 10) break;
          }

          if (c == 255) {                    // IAC
            telState = TEL_IAC;
          } else {
            if (c == 127) c = 8;
            consoleRxEnqueue(c);
            if (c == 13) telnetAfterCr = true;
          }
          break;

        case TEL_IAC:
          if (c == 255) {                    // Escaped literal 0xFF
            consoleRxEnqueue(255);
            telState = TEL_DATA;
          } else if (c == 250) {             // SB - subnegotiation
            telState = TEL_SB;
          } else if (c >= 251 && c <= 254) { // WILL/WONT/DO/DONT + option byte
            telState = TEL_IAC_OPTION;
          } else {
            telState = TEL_DATA;             // Other 2-byte Telnet command
          }
          break;

        case TEL_IAC_OPTION:
          // Consume option byte following WILL/WONT/DO/DONT.
          telState = TEL_DATA;
          break;

        case TEL_SB:
          if (c == 255) telState = TEL_SB_IAC;
          break;

        case TEL_SB_IAC:
          if (c == 240) telState = TEL_DATA; // IAC SE
          else if (c != 255) telState = TEL_SB;
          break;
      }
    }

    // Keys from the physical HP-85 are staged by the low-priority
    // Teensy UART polling worker; only this console task feeds the
    // virtual Z80 UART so it retains Web/Telnet ordering semantics.
    Cpm85Input::drain();
    if (rxOutPtr != rxInPtr && bitRead(pIn[UART_LSR], 0) == 0) {
      pIn[UART_PORT] = rxBuf[rxOutPtr];
      uint16_t next = rxOutPtr + 1;
      if (next >= sizeof(rxBuf)) next = 0;
      rxOutPtr = next;
      bitWrite(pIn[UART_LSR], 0, 1);
    }
    vTaskDelay(1);
  }
}

void outString(char buf[]) {
  int i = 0;
  while (buf[i] > 0) {
    uint16_t next = txInPtr + 1;
    if (next >= sizeof(txBuf)) next = 0;
    // outString shares the console ring with Z80 OUT (80h); don't silently
    // truncate diagnostic/utility strings when the output queue is full.
    if (next == txOutPtr) {
      if (!RUN) break;  // Z80 is being reset/stopped.
      vTaskDelay(1);
      continue;
    }
    txBuf[txInPtr] = (uint8_t)buf[i++];
    txInPtr = next;
  }
}
