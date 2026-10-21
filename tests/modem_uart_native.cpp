// Host-native 8250 virtual modem UART test.
#include "../ESP32_Z80_Emulator/ebtks_modem_uart.h"
#include <cassert>
#include <cstdio>
using EbtksModem::Uart8250;
int main() {
  Uart8250 u;
  assert(u.read(5)==0x60);
  assert((u.read(6)&0xb0)==0x30);
  assert(u.read(2)==1); // no interrupts asserted by polling-only model
  u.write(3,0x83); // set DLAB
  u.write(0,0x34);u.write(1,0x12);
  assert(u.read(0)==0x34 && u.read(1)==0x12);
  u.write(3,0x03); // return to data
  assert(u.read(3)==3);
  u.write(1,0x05);assert(u.read(1)==0x05);
  u.write(4,0x0b);assert(u.read(4)==0x0b);
  u.write(7,0xa5);assert(u.read(7)==0xa5);
  u.write(0,'A');u.write(0,'T');
  assert(u.read(5)&0x20);
  unsigned char c=0;
  assert(u.popTx(c)&&c=='A');
  assert(u.popTx(c)&&c=='T');
  assert(!u.popTx(c));
  assert(u.read(5)==0x60);
  assert(u.pushRx('O')&&u.pushRx('K'));
  assert((u.read(5)&1)==1);
  assert(u.read(0)=='O'&&u.read(0)=='K');
  assert(!(u.read(5)&1));
  u.setCarrier(true);
  assert(u.carrier());
  assert(u.read(6)==0xb8);
  assert(u.read(6)==0xb0);
  u.setCarrier(false);
  assert(u.read(6)==0x38);
  for(unsigned i=0;i<Uart8250::kRxSize-1;++i)assert(u.pushRx('z'));
  assert(!u.pushRx('x') && u.droppedRx()==1);
  assert(u.read(5)&0x02);
  assert(!(u.read(5)&0x02)); // reading LSR clears overrun
  u.write(2,0x03); // enable FIFO and clear RX
  assert(!(u.read(5)&1));
  assert(u.read(2)==0xc1);
  for(unsigned i=0;i<Uart8250::kTxSize-1;++i)u.write(0,'x');
  assert(!(u.read(5)&0x20)); // TX FIFO full
  u.write(0,'x');assert(u.droppedTx()==1);
  assert(u.popTx(c));assert(u.read(5)&0x20);
  u.write(2,0x05); // clear TX
  assert(!u.popTx(c));
  u.reset();assert(u.read(5)==0x60 && u.read(7)==0);
  std::puts("PASS: virtual 8250 registers, DLAB, FIFO, carrier, backpressure");
}
