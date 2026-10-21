#pragma once
#include <stdint.h>
#include <stddef.h>

// Portable bounded Hayes parser. The ESP32 network worker owns this object;
// no socket calls, heap strings, or RTOS code occur in the Z80 port handlers.
// This deliberately implements a subset, not a full Hayes chipset.
namespace EbtksModem {
struct HayesEvent {
  enum Kind : uint8_t {NONE,OK,ERROR,DIAL,HANGUP,ONLINE,INFO} kind;
  char host[96];
  uint16_t port;
  bool echo;
};
class Hayes {
  char line_[120]={};
  uint8_t len_=0;
  bool echo_=true,quiet_=false,verbose_=true;
  static char upper(char c) {
    return (c>='a'&&c<='z')?static_cast<char>(c-'a'+'A'):c;
  }
  static bool digit(char c) {return c>='0'&&c<='9';}
  HayesEvent dial(const char* s) const {
    HayesEvent e={HayesEvent::ERROR,{0},23,echo_};
    if(!*s)return e;
    size_t n=0;
    // Accept ATDhost:port, ATDThost:port or ATDPhost:port.
    if(upper(*s)=='T'||upper(*s)=='P')++s;
    const char* q=s;
    while(*q && *q!=':') {
      const char c=*q++;
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
           digit(c)||c=='.'||c=='-'))return e;
      if(++n>=sizeof(e.host))return e;
    }
    if(!n)return e;
    for(size_t i=0;i<n;++i)e.host[i]=s[i];
    e.host[n]=0;
    if(*q==':') {
      unsigned port=0;++q;
      if(!digit(*q))return e;
      while(digit(*q)) {
        port=port*10+static_cast<unsigned>(*q++-'0');
        if(port>65535)return e;
      }
      if(*q||port==0)return e;
      e.port=static_cast<uint16_t>(port);
    }
    e.kind=HayesEvent::DIAL;return e;
  }
  HayesEvent execute() {
    HayesEvent e={HayesEvent::ERROR,{0},23,echo_};
    line_[len_]=0;len_=0;
    if(upper(line_[0])!='A'||upper(line_[1])!='T')return e;
    e.kind=HayesEvent::OK;
    const char* p=line_+2;
    while(*p) {
      const char cmd=upper(*p++);
      switch(cmd) {
        case 'E':
          if(*p=='0'||*p=='1'){echo_=(*p++=='1');break;}
          return {HayesEvent::ERROR,{0},23,echo_};
        case 'Q':
          if(*p=='0'||*p=='1'){quiet_=(*p++=='1');break;}
          return {HayesEvent::ERROR,{0},23,echo_};
        case 'V':
          if(*p=='0'||*p=='1'){verbose_=(*p++=='1');break;}
          return {HayesEvent::ERROR,{0},23,echo_};
        case 'Z':
          if(*p=='0')++p;
          echo_=true;quiet_=false;verbose_=true;
          if(*p)return {HayesEvent::ERROR,{0},23,echo_};
          return {HayesEvent::HANGUP,{0},23,echo_};
        case '&':
          if(upper(*p++)!='F')return {HayesEvent::ERROR,{0},23,echo_};
          echo_=true;quiet_=false;verbose_=true;break;
        case 'D':return dial(p);
        case 'H':
          if(*p=='0')++p;
          return *p?HayesEvent{HayesEvent::ERROR,{0},23,echo_}
                   :HayesEvent{HayesEvent::HANGUP,{0},23,echo_};
        case 'O':
          if(*p=='0')++p;
          return *p?HayesEvent{HayesEvent::ERROR,{0},23,echo_}
                   :HayesEvent{HayesEvent::ONLINE,{0},23,echo_};
        case 'I':
          if(*p=='0')++p;
          return *p?HayesEvent{HayesEvent::ERROR,{0},23,echo_}
                   :HayesEvent{HayesEvent::INFO,{0},23,echo_};
        default:return {HayesEvent::ERROR,{0},23,echo_};
      }
    }
    e.echo=echo_;return e;
  }
public:
  bool echo()const{return echo_;}
  bool quiet()const{return quiet_;}
  bool verbose()const{return verbose_;}
  void reset(){len_=0;echo_=true;quiet_=false;verbose_=true;}
  HayesEvent feed(uint8_t byte) {
    HayesEvent none={HayesEvent::NONE,{0},23,echo_};
    if(byte=='\n')return none;
    if(byte=='\r') {
      if(len_==0)return none;
      return execute();
    }
    if(byte==8 || byte==127) {
      if(len_)--len_;
      return none;
    }
    if(byte<32||byte>126)return none;
    if(static_cast<size_t>(len_)+1>=sizeof(line_)) {
      len_=0;
      return {HayesEvent::ERROR,{0},23,echo_};
    }
    line_[len_++]=static_cast<char>(byte);
    return none;
  }
};
} // namespace EbtksModem
