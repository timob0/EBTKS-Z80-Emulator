// Native host regressions for the pinned upstream redcode/Z80 core and
// the portable EBTKS callback/breakpoint adapter. No Arduino or Teensy needed.
#include "../ESP32_Z80_Emulator/z80_bridge.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,   "FAIL %s:%d: %s\\n", __FILE__,__LINE__,#x); std::exit(1); } } while(0)

struct Fixture {
  uint8_t ram[65536]={};
  Z80Bridge bridge{};
  volatile bool running=true;
  Fixture() { z80BridgeInit(&bridge,ram,nullptr,nullptr,nullptr,&running); }
  Z80BridgeStop run() { return z80BridgeRun(&bridge,30000); }
};
static void testSbcHLDE() {
  Fixture f;
  // LD HL,1234 / LD DE,0102 / SCF / SBC HL,DE / HALT
  uint8_t code[]={0x21,0x34,0x12,0x11,0x02,0x01,0x37,0xED,0x52,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(Z80_HL(f.bridge.cpu)==0x1131);
  CHECK((Z80_F(f.bridge.cpu)&Z80_NF)!=0);
}
static void testSbcHLBC() {
  Fixture f;
  uint8_t code[]={0x21,0x00,0x10,0x01,0x10,0x00,0xAF,0xED,0x42,0x76}; // XOR A clears carry before SBC
  std::memcpy(f.ram,code,sizeof(code));
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(Z80_HL(f.bridge.cpu)==0x0FF0);
}
static void testIxIndirectAndJump() {
  Fixture f;
  // LD IX,(2000h); JP (IX); at 0010h HALT
  uint8_t code[]={0xDD,0x2A,0x00,0x20,0xDD,0xE9,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  f.ram[0x2000]=0x10;
  f.ram[0x2001]=0x00;
  f.ram[0x0010]=0x76;
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(Z80_IX(f.bridge.cpu)==0x0010);
  CHECK(Z80_PC(f.bridge.cpu)>=0x0010 && Z80_PC(f.bridge.cpu)<=0x0012);
}
static void testIxIYDisplacementAndBit() {
  Fixture f;
  // LD IX,2010h / LD (IX-1),A / BIT 7,(IX-1) / HALT
  uint8_t code[]={0xDD,0x21,0x10,0x20,0x3E,0x80,
                  0xDD,0x77,0xFF,0xDD,0xCB,0xFF,0x7E,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(f.ram[0x200F]==0x80);
  CHECK((Z80_F(f.bridge.cpu)&Z80_ZF)==0);
}
static void testLdir() {
  Fixture f;
  uint8_t code[]={0x21,0x00,0x20,0x11,0x00,0x30,
                  0x01,0x03,0x00,0xED,0xB0,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  f.ram[0x2000]=11;f.ram[0x2001]=22;f.ram[0x2002]=33;
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(f.ram[0x3000]==11&&f.ram[0x3001]==22&&f.ram[0x3002]==33);
  CHECK(Z80_HL(f.bridge.cpu)==0x2003 && Z80_DE(f.bridge.cpu)==0x3003);
  CHECK(Z80_BC(f.bridge.cpu)==0);
  CHECK((Z80_F(f.bridge.cpu)&Z80_PF)==0);
}
static void testCpir() {
  Fixture f;
  uint8_t code[]={0x21,0x00,0x20,0x01,0x03,0x00,
                  0x3E,0x42,0xED,0xB1,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  f.ram[0x2000]=0x11;f.ram[0x2001]=0x42;f.ram[0x2002]=0x33;
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(Z80_HL(f.bridge.cpu)==0x2002);
  CHECK(Z80_BC(f.bridge.cpu)==1);
  CHECK((Z80_F(f.bridge.cpu)&Z80_ZF)!=0);
}
static void testCBSetRes() {
  Fixture f;
  uint8_t code[]={0x06,0x00,0xCB,0xF8,0xCB,0xB8,0x76}; // SET 7,B / RES 7,B
  std::memcpy(f.ram,code,sizeof(code));
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(Z80_B(f.bridge.cpu)==0);
}
static void testPreInstructionWarmbootTrap() {
  Fixture f;
  constexpr uint16_t WBOOT=0xE603;
  f.ram[WBOOT]=0x3E;f.ram[WBOOT+1]=0x55;f.ram[WBOOT+2]=0x76;
  Z80_PC(f.bridge.cpu)=WBOOT;
  f.bridge.trap_enabled=true;
  f.bridge.trap_address=WBOOT;
  CHECK(f.run()==Z80_BRIDGE_TRAP);
  CHECK(Z80_PC(f.bridge.cpu)==WBOOT);
  CHECK(Z80_A(f.bridge.cpu)!=0x55);
  // On the resume after copying CCP/BDOS, execute the actual WBOOT opcode once.
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(Z80_A(f.bridge.cpu)==0x55);
}
struct Ports {uint16_t last=0;uint8_t value=0;};
static uint8_t ioIn(void *p,uint16_t address){
  Ports *io=static_cast<Ports*>(p);
  io->last=address;
  return 0x7E;
}
static void ioOut(void *p,uint16_t address,uint8_t value){
  Ports *io=static_cast<Ports*>(p);
  io->last=address;io->value=value;
}
static void testIoCallbacks() {
  Fixture f;Ports ports;
  f.bridge.device=&ports;f.bridge.input=ioIn;f.bridge.output=ioOut;
  uint8_t code[]={0x3E,0x42,0xD3,0x20,0xDB,0x20,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  CHECK(f.run()==Z80_BRIDGE_HALT);
  CHECK(ports.value==0x42 && (ports.last&0xFF)==0x20);
  CHECK(Z80_A(f.bridge.cpu)==0x7E);
}
static void testStopBeforeSideEffect() {
  Fixture f;
  uint8_t code[]={0x3E,0x33,0x32,0x00,0x20,0x76};
  std::memcpy(f.ram,code,sizeof(code));
  f.running=false;
  CHECK(f.run()==Z80_BRIDGE_EXTERNAL_STOP);
  CHECK(Z80_PC(f.bridge.cpu)==0);
  CHECK(f.ram[0x2000]==0);
}
int main() {
  testSbcHLDE();testSbcHLBC();testIxIndirectAndJump();
  testIxIYDisplacementAndBit();testLdir();testCpir();testCBSetRes();
  testPreInstructionWarmbootTrap();testIoCallbacks();testStopBeforeSideEffect();
  std::puts("PASS: 10 native redcode/Z80 instruction + EBTKS adapter tests");
  return 0;
}
