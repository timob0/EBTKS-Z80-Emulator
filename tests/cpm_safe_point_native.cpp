// Allocation-free host regression for the CP/M CCP safe-point gate and
// injected, non-BDOS Z80 service. Uses the real pinned redcode/Z80 core.
#include "../ESP32_Z80_Emulator/cpm_safe_point.h"
#include "../ESP32_Z80_Emulator/cpm_service_probe.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static bool fetchGate(void *ctx,uint16_t pc,const uint8_t *memory) {
  return static_cast<CpmSafePoint::Gate *>(ctx)->fetch(pc,memory);
}
struct Fixture {
  uint8_t mem[65536]={};
  Z80Bridge cpu{};
  volatile bool run=true;
  CpmSafePoint::Gate gate;
  Fixture() {
    // Native 2.2 CCP byte signatures, taken from checked-in cpm22.lst.
    const uint8_t ccpPrompt[]={0x31,0xAB,0xD7};
    const uint8_t ccpCall[]={0xCD,0x39,0xD1};
    const uint8_t command[]={0x11,0x80,0x00};
    const uint8_t console[]={0x0E,0x0A};
    const uint8_t bdosCall[]={0xCD,0x05,0x00};
    const uint8_t returnToCcp[]={0xCD,0x29,0xD1};
    memcpy(mem+0xD382,ccpPrompt,sizeof(ccpPrompt));
    memcpy(mem+0xD395,ccpCall,sizeof(ccpCall));
    memcpy(mem+0xD398,command,sizeof(command));
    memcpy(mem+0xD19C,console,sizeof(console));
    memcpy(mem+0xD1A1,bdosCall,sizeof(bdosCall));
    memcpy(mem+0xD1A4,returnToCcp,sizeof(returnToCcp));
    z80BridgeInit(&cpu,mem,nullptr,nullptr,nullptr,&run);
    z80BridgeColdReset(&cpu);
    cpu.service_fetch=fetchGate;
    cpu.device=&gate;
    gate.reset(mem,true);
    assert(gate.compatible.load());
  }
};

static void testRejectAbsentAndBusy() {
  Fixture f;
  assert(!f.gate.request());
  assert(f.gate.mode.load()==CpmSafePoint::UNKNOWN);
  // A direct jump to D19C is NOT sufficient; must pass real CCP D382 first.
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(!f.gate.request());
  f.gate.fetch(CpmSafePoint::kCcpPrompt,f.mem);
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(f.gate.request());
  assert(!f.gate.request()); // exactly one outstanding
  f.mem[CpmSafePoint::kCcpInputLength]=3;
  f.gate.fetch(CpmSafePoint::kCcpInputReturn,f.mem);
  assert(f.gate.result.load()==CpmSafePoint::INPUT_NOT_EMPTY);
  assert(!f.gate.fetch(CpmSafePoint::kCcpCommand,f.mem));
  assert(!f.gate.fetch(CpmSafePoint::kCcpPrompt,f.mem));
  assert(!f.gate.request()); // normal command executing
  f.gate.fetch(CpmSafePoint::kCcpPrompt,f.mem);
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(f.gate.request()); // next verified CCP prompt
  f.gate.onWarmBoot();
  assert(f.gate.result.load()==CpmSafePoint::INVALIDATED);
  assert(!f.gate.request());
}

static void testRejectWrongCcpOrMissingNative() {
  Fixture f;
  f.gate.reset(f.mem,false);
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(!f.gate.request());
  f.mem[0xD395]=0;
  f.gate.reset(f.mem,true);
  assert(!f.gate.compatible.load());
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(!f.gate.request());
}

static void testRealZ80TrapExecuteAndRestore() {
  Fixture f;
  constexpr uint16_t addr=CpmServiceProbe::kAddress;
  uint8_t original[sizeof(CpmServiceProbe::kProgram)];
  for(unsigned i=0;i<sizeof(original);++i)
    f.mem[addr+i]=uint8_t(i+0x90);
  memcpy(original,f.mem+addr,sizeof(original));
  f.cpu.trap_enabled=true;
  f.cpu.trap_address=0xE603;
  Z80_PC(f.cpu.cpu)=CpmSafePoint::kCcpPrompt;
  Z80_SP(f.cpu.cpu)=0xBD00;
  Z80_A(f.cpu.cpu)=0xA5;
  Z80_BC(f.cpu.cpu)=0xBEEF;
  const uint16_t oldPC=Z80_PC(f.cpu.cpu);
  const uint16_t oldSP=Z80_SP(f.cpu.cpu);
  const uint16_t oldBC=Z80_BC(f.cpu.cpu);
  assert(!f.gate.request());
  assert(!f.gate.fetch(CpmSafePoint::kCcpPrompt,f.mem));
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(f.gate.request());
  f.mem[CpmSafePoint::kCcpInputLength]=0;
  f.gate.fetch(CpmSafePoint::kCcpInputReturn,f.mem);
  // The CCP does pass through CMMND2 even after a blank line.
  f.gate.fetch(CpmSafePoint::kCcpCommand,f.mem);
  assert(f.gate.result.load()==CpmSafePoint::WAIT_EMPTY_LINE);
  assert(z80BridgeRun(&f.cpu,150)==Z80_BRIDGE_SERVICE);
  assert(Z80_PC(f.cpu.cpu)==oldPC);
  assert(f.gate.result.load()==CpmSafePoint::RUNNING);
  assert(CpmServiceProbe::execute(&f.cpu));
  f.gate.complete(true);
  assert(f.gate.result.load()==CpmSafePoint::PASSED);
  assert(Z80_PC(f.cpu.cpu)==oldPC && Z80_SP(f.cpu.cpu)==oldSP);
  assert(Z80_BC(f.cpu.cpu)==oldBC && Z80_A(f.cpu.cpu)==0xA5);
  assert(memcmp(original,f.mem+addr,sizeof(original))==0);
  assert(f.cpu.trap_enabled && f.cpu.trap_address==0xE603);
  assert(f.cpu.service_fetch==fetchGate);
  assert(f.cpu.device==&f.gate);
  assert(!f.gate.request()); // CCP must reach next input before rearming
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(f.gate.request());
  // A running task never enters CCP input; no changes to current CPU state.
}

static void testDirectoryAutoReturnOnlyAtEmptyNativeLine() {
  Fixture f;
  f.gate.fetch(CpmSafePoint::kCcpPrompt,f.mem);
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  // No BDOS RDBUF1 observation yet: never auto-inject blindly.
  assert(f.gate.request(CpmSafePoint::DIRECTORY,1));
  assert(!f.gate.takeAutomaticReturn());
  f.gate.observeLineCount(0);  // Z80 B=0 at BDOS D9EF
  assert(f.gate.takeAutomaticReturn());
  assert(!f.gate.takeAutomaticReturn()); // one and only one virtual CR
  f.gate.onWarmBoot();

  Fixture typed;
  typed.gate.fetch(CpmSafePoint::kCcpPrompt,typed.mem);
  typed.gate.fetch(CpmSafePoint::kCcpConsoleInput,typed.mem);
  typed.gate.observeLineCount(3);
  assert(!typed.gate.request(CpmSafePoint::DIRECTORY,1));
  assert(!typed.gate.takeAutomaticReturn());
  typed.gate.observeLineCount(0); // operator cleared the line
  assert(typed.gate.request(CpmSafePoint::DIRECTORY,2));
  assert(typed.gate.requestedDrive.load()==2);
  assert(typed.gate.takeAutomaticReturn());
  typed.gate.fetch(CpmSafePoint::kCcpInputReturn,typed.mem);
  typed.gate.fetch(CpmSafePoint::kCcpCommand,typed.mem);
  assert(typed.gate.fetch(CpmSafePoint::kCcpPrompt,typed.mem));
  assert(typed.gate.operation.load()==CpmSafePoint::DIRECTORY);
  typed.gate.complete(true);
  assert(!typed.gate.takeAutomaticReturn());

  Fixture app;
  assert(!app.gate.request(CpmSafePoint::DIRECTORY,1));
  app.gate.observeLineCount(0);
  assert(!app.gate.takeAutomaticReturn());
}

int main(){
  testRejectAbsentAndBusy();
  testRejectWrongCcpOrMissingNative();
  testRealZ80TrapExecuteAndRestore();
  testDirectoryAutoReturnOnlyAtEmptyNativeLine();
  std::puts("PASS: CCP-only gate, nonempty abort, WBOOT abort, real Z80 probe and exact state restoration");
}
