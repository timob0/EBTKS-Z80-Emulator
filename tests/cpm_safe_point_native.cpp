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
  assert(f.gate.checkpoint.load()==CpmSafePoint::ARMED);
  assert(!f.gate.takeAutomaticReturn());
  f.gate.observeLineCount(0);  // Z80 B=0 at BDOS D9EF
  f.gate.observeFetchedOpcode(0xE863); // BIOS console OUTPUT status read
  assert(!f.gate.takeAutomaticReturn());
  f.gate.observeFetchedOpcode(CpmSafePoint::kBiosConinPollA);
  assert(f.gate.takeAutomaticReturn());
  assert(f.gate.checkpoint.load()==CpmSafePoint::AUTO_CR);
  assert(!f.gate.takeAutomaticReturn()); // one and only one virtual CR
  f.gate.onWarmBoot();

  Fixture typed;
  typed.gate.fetch(CpmSafePoint::kCcpPrompt,typed.mem);
  typed.gate.fetch(CpmSafePoint::kCcpConsoleInput,typed.mem);
  typed.gate.observeLineCount(3);
  typed.gate.observeFetchedOpcode(CpmSafePoint::kBiosConinPollB);
  assert(!typed.gate.request(CpmSafePoint::DIRECTORY,1));
  assert(!typed.gate.takeAutomaticReturn());
  typed.gate.observeLineCount(0); // operator cleared the line
  typed.gate.observeFetchedOpcode(CpmSafePoint::kBiosConinPollB);
  assert(typed.gate.request(CpmSafePoint::DIRECTORY,2));
  assert(typed.gate.requestedDrive.load()==2);
  assert(typed.gate.takeAutomaticReturn());
  typed.gate.fetch(CpmSafePoint::kCcpInputReturn,typed.mem);
  typed.gate.fetch(CpmSafePoint::kCcpCommand,typed.mem);
  assert(typed.gate.fetch(CpmSafePoint::kCcpPrompt,typed.mem));
  assert(typed.gate.operation.load()==CpmSafePoint::DIRECTORY);
  typed.gate.complete(true);
  assert(typed.gate.checkpoint.load()==CpmSafePoint::BDOS_OK);
  assert(!typed.gate.takeAutomaticReturn());

  Fixture cancelled;
  cancelled.gate.fetch(CpmSafePoint::kCcpPrompt,cancelled.mem);
  cancelled.gate.fetch(CpmSafePoint::kCcpConsoleInput,cancelled.mem);
  cancelled.gate.observeLineCount(0);
  assert(cancelled.gate.request(CpmSafePoint::DIRECTORY,1));
  cancelled.gate.observeLineCount(1);  // user typed before automatic CR
  assert(cancelled.gate.result.load()==CpmSafePoint::INPUT_NOT_EMPTY);
  assert(cancelled.gate.checkpoint.load()==CpmSafePoint::NONEMPTY_LINE);
  cancelled.gate.observeFetchedOpcode(CpmSafePoint::kBiosConinPollA);
  assert(!cancelled.gate.takeAutomaticReturn());
  assert(!cancelled.gate.cancelPending());

  Fixture timedOut;
  timedOut.gate.fetch(CpmSafePoint::kCcpPrompt,timedOut.mem);
  timedOut.gate.fetch(CpmSafePoint::kCcpConsoleInput,timedOut.mem);
  assert(timedOut.gate.request(CpmSafePoint::DIRECTORY,1));
  assert(timedOut.gate.cancelPending());
  assert(timedOut.gate.checkpoint.load()==CpmSafePoint::CANCEL_ON_TIMEOUT);
  timedOut.gate.observeLineCount(0);
  timedOut.gate.observeFetchedOpcode(CpmSafePoint::kBiosConinPollA);
  assert(!timedOut.gate.takeAutomaticReturn());

  Fixture app;
  assert(!app.gate.request(CpmSafePoint::DIRECTORY,1));
  app.gate.observeLineCount(0);
  assert(!app.gate.takeAutomaticReturn());
}

// Exercise the ACTUAL redcode bridge opcode-fetch filter. The first CPDCAT
// revision failed here: it watched D9EF/E826 in the gate, but z80_bridge.h
// only forwarded the four pre-existing CCP addresses.
static Z80Bridge *autoFetchCpu=nullptr;
static int autoReadCount=0;
static int inputFetches=0;
static bool fetchGateAuto(void *ctx,uint16_t pc,const uint8_t *memory) {
  auto *gate=static_cast<CpmSafePoint::Gate *>(ctx);
  gate->observeFetchedOpcode(pc);
  if(pc==CpmSafePoint::kBdosLineLoop) {
    assert(autoFetchCpu);
    gate->observeLineCount(Z80_B(autoFetchCpu->cpu));
  }
  if(pc==CpmSafePoint::kBiosConinPollA ||
     pc==CpmSafePoint::kBiosConinPollB)++inputFetches;
  return gate->fetch(pc,memory);
}
static uint8_t readAutoPort(void *ctx,uint16_t port) {
  if((port&0xFF)!=0x85)return 0xFF;
  auto *gate=static_cast<CpmSafePoint::Gate *>(ctx);
  if(gate->takeAutomaticReturn()) {
    ++autoReadCount;
    return 0x01; // virtual UART receive latch ready with synthetic CR
  }
  return 0;
}
static void testRealBridgeForwardsBdosAndBiosInputFetches() {
  Fixture f;
  autoFetchCpu=&f.cpu;
  autoReadCount=inputFetches=0;
  f.cpu.service_fetch=fetchGateAuto;
  f.cpu.input=readAutoPort;
  f.gate.fetch(CpmSafePoint::kCcpPrompt,f.mem);
  f.gate.fetch(CpmSafePoint::kCcpConsoleInput,f.mem);
  assert(f.gate.request(CpmSafePoint::DIRECTORY,1));

  // Native BDOS buffered-console loop begins with PUSH BC, where B==0.
  f.mem[CpmSafePoint::kBdosLineLoop]=0xC5;   // PUSH BC
  f.mem[CpmSafePoint::kBdosLineLoop+1]=0x76; // HALT, test-only sentinel
  Z80_PC(f.cpu.cpu)=CpmSafePoint::kBdosLineLoop;
  Z80_SP(f.cpu.cpu)=0xBD00;
  Z80_B(f.cpu.cpu)=0;
  assert(z80BridgeRun(&f.cpu,100)==Z80_BRIDGE_HALT);
  assert(f.gate.ccpLineKnown.load() && f.gate.ccpLineEmpty.load());

  // CPU returns to the exact native CBIOS console INPUT polling instruction.
  z80BridgeColdReset(&f.cpu);
  f.mem[CpmSafePoint::kBiosConinPollA]=0xDB;   // IN A,(85h)
  f.mem[CpmSafePoint::kBiosConinPollA+1]=0x85;
  f.mem[CpmSafePoint::kBiosConinPollA+2]=0x76; // HALT
  Z80_PC(f.cpu.cpu)=CpmSafePoint::kBiosConinPollA;
  assert(z80BridgeRun(&f.cpu,100)==Z80_BRIDGE_HALT);
  assert(inputFetches==1 && autoReadCount==1);
  assert(f.gate.autoReturnSent);

  // An ordinary BIOS CONOUT poll at E863 must NEVER trigger a new CR.
  Fixture out;
  autoFetchCpu=&out.cpu;
  autoReadCount=inputFetches=0;
  out.cpu.service_fetch=fetchGateAuto;
  out.cpu.input=readAutoPort;
  out.gate.fetch(CpmSafePoint::kCcpPrompt,out.mem);
  out.gate.fetch(CpmSafePoint::kCcpConsoleInput,out.mem);
  assert(out.gate.request(CpmSafePoint::DIRECTORY,1));
  out.gate.observeLineCount(0);
  out.mem[0xE863]=0xDB; out.mem[0xE864]=0x85; out.mem[0xE865]=0x76;
  Z80_PC(out.cpu.cpu)=0xE863;
  assert(z80BridgeRun(&out.cpu,100)==Z80_BRIDGE_HALT);
  assert(autoReadCount==0 && inputFetches==0);
  autoFetchCpu=nullptr;
}

static void testBdosFailureForensicsSurviveZ80ColdRestart() {
  Fixture f;
  f.gate.operation.store(CpmSafePoint::DIRECTORY);
  f.gate.directoryError.store(CpmSafePoint::DIR_BDOS_INTERRUPTED);
  f.gate.checkpoint.store(CpmSafePoint::BDOS_ENTER);
  f.gate.complete(false);
  assert(f.gate.result.load()==CpmSafePoint::FAILED);
  assert(f.gate.lastDirectoryResult.load()==CpmSafePoint::FAILED);
  assert(f.gate.lastDirectoryError.load()==CpmSafePoint::DIR_BDOS_INTERRUPTED);
  assert(f.gate.lastDirectoryCheckpoint.load()==CpmSafePoint::BDOS_FAIL);
  // The CPU task sets this bit when it forces a Z80-only reset.
  f.gate.lastDirectoryReset.store(true);
  // Outer loop re-bootstrap must reset live state, NOT discard the failure.
  f.gate.reset(f.mem,true);
  assert(f.gate.result.load()==CpmSafePoint::NEVER);
  assert(f.gate.directoryError.load()==CpmSafePoint::DIR_OK);
  assert(f.gate.checkpoint.load()==CpmSafePoint::IDLE);
  assert(f.gate.compatible.load());
  assert(f.gate.lastDirectoryResult.load()==CpmSafePoint::FAILED);
  assert(f.gate.lastDirectoryError.load()==CpmSafePoint::DIR_BDOS_INTERRUPTED);
  assert(f.gate.lastDirectoryCheckpoint.load()==CpmSafePoint::BDOS_FAIL);
  assert(f.gate.lastDirectoryReset.load());
}

int main(){
  testRejectAbsentAndBusy();
  testRejectWrongCcpOrMissingNative();
  testRealZ80TrapExecuteAndRestore();
  testDirectoryAutoReturnOnlyAtEmptyNativeLine();
  testRealBridgeForwardsBdosAndBiosInputFetches();
  testBdosFailureForensicsSurviveZ80ColdRestart();
  std::puts("PASS: CCP gate, native BDOS/CBIOS opcode fetch, one-shot auto CR, Z80 probe and restoration");
}
