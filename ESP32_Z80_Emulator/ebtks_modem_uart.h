#pragma once
#include <stddef.h>
#include <stdint.h>

// Small, heap-free 8250-compatible Z80 UART core. No Arduino dependency.
// The caller MUST serialize CPU port accesses with worker pushRx/popTx
// (short FreeRTOS critical sections, never socket I/O under the lock).
// Base Z80 I/O address is 88h; these are register offsets 0..7.
namespace EbtksModem {
class Uart8250 {
public:
  static const uint16_t kRxSize=512, kTxSize=512;
private:
  uint8_t rx_[kRxSize]={},tx_[kTxSize]={};
  uint16_t rh_=0,rt_=0,th_=0,tt_=0;
  uint8_t ier_=0,lcr_=3,mcr_=0,scr_=0,dll_=12,dlm_=0,fcr_=0;
  bool carrier_=false,overrun_=false,deltaCarrier_=false;
  uint32_t droppedRx_=0,droppedTx_=0;
  static uint16_t next(uint16_t n,uint16_t size) {
    return (n+1==size)?0:static_cast<uint16_t>(n+1);
  }
public:
  void reset() {
    rh_=rt_=th_=tt_=0;
    ier_=0;lcr_=3;mcr_=0;scr_=0;dll_=12;dlm_=0;fcr_=0;
    carrier_=false;overrun_=false;deltaCarrier_=false;
    droppedRx_=droppedTx_=0;
  }
  uint16_t rxFree() const {
    return static_cast<uint16_t>((rt_+kRxSize-rh_-1)%kRxSize);
  }
  uint16_t txFree() const {
    return static_cast<uint16_t>((tt_+kTxSize-th_-1)%kTxSize);
  }
  uint16_t rxAvailable()const{return static_cast<uint16_t>((rh_+kRxSize-rt_)%kRxSize);}
  uint16_t txAvailable()const{return static_cast<uint16_t>((th_+kTxSize-tt_)%kTxSize);}
  uint32_t droppedRx()const{return droppedRx_;}
  uint32_t droppedTx()const{return droppedTx_;}
  bool pushRx(uint8_t v) {
    const uint16_t n=next(rh_,kRxSize);
    if(n==rt_){overrun_=true;++droppedRx_;return false;}
    rx_[rh_]=v;rh_=n;return true;
  }
  bool popTx(uint8_t& v) {
    if(tt_==th_)return false;
    v=tx_[tt_];tt_=next(tt_,kTxSize);return true;
  }
  void setCarrier(bool on) {
    if(carrier_!=on){carrier_=on;deltaCarrier_=true;}
  }
  bool carrier()const{return carrier_;}
  uint8_t read(uint8_t off) {
    off &= 7;
    if(lcr_ & 0x80) {
      if(off==0)return dll_;
      if(off==1)return dlm_;
    }
    switch(off) {
      case 0: {
        if(rt_==rh_)return 0;
        const uint8_t v=rx_[rt_];rt_=next(rt_,kRxSize);return v;
      }
      case 1:return ier_;
      case 2:return static_cast<uint8_t>(0x01|((fcr_&1)?0xc0:0));
      case 3:return lcr_;
      case 4:return mcr_;
      case 5: {
        uint8_t s=static_cast<uint8_t>((rt_!=rh_?1:0)
                     |(overrun_?2:0)|(txFree()?0x20:0)
                     |(th_==tt_?0x40:0));
        overrun_=false;return s;
      }
      case 6: {
        const uint8_t s=static_cast<uint8_t>(0x30|(carrier_?0x80:0)
                           |(deltaCarrier_?0x08:0));
        deltaCarrier_=false;return s;
      }
      case 7:return scr_;
    }
    return 0;
  }
  void write(uint8_t off,uint8_t v) {
    off &= 7;
    if(lcr_&0x80) {
      if(off==0){dll_=v;return;}
      if(off==1){dlm_=v;return;}
    }
    switch(off) {
      case 0: {
        const uint16_t n=next(th_,kTxSize);
        if(n==tt_)++droppedTx_;
        else {tx_[th_]=v;th_=n;}
        break;
      }
      case 1:ier_=v&0x0f;break; // registers only: IRQ not yet wired
      case 2:
        fcr_=v&1;
        if(v&2)rh_=rt_=0;
        if(v&4)th_=tt_=0;
        break;
      case 3:lcr_=v;break;
      case 4:mcr_=v;break;
      case 7:scr_=v;break;
      default:break;
    }
  }
};
} // namespace EbtksModem
