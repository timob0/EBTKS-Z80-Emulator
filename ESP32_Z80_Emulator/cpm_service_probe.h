#pragma once
#include <stdint.h>
#include <string.h>
#include "z80_bridge.h"

// Proof-of-control Z80 service, NO BDOS, file, disk, UART or console calls.
// LD A,42h; CP 42h; HALT.  Run from a temporarily saved 3-byte TPA slot.
// HALT is intercepted by the emulator, never allowed to halt normal CP/M.
// All Z80 registers/flags/PC/SP/callback pointers and the touched TPA bytes
// are restored before the original CCP D382 opcode resumes.
namespace CpmServiceProbe {
static constexpr uint16_t kAddress=0xC000;
static constexpr uint8_t kProgram[5]={0x3E,0x42,0xFE,0x42,0x76};
inline bool execute(Z80Bridge *bridge) {
  if(!bridge || !bridge->memory)return false;
  uint8_t original[sizeof(kProgram)];
  memcpy(original,bridge->memory+kAddress,sizeof(original));
  const Z80 savedCpu=bridge->cpu;
  const bool trapBefore=bridge->trap_enabled;
  const bool bypassBefore=bridge->trap_bypass_once;
  const bool serviceBefore=bridge->service_trap_enabled;
  const Z80BridgeStop priorStop=bridge->stop_reason;
  const uint16_t priorStoppedAt=bridge->stopped_at;
  // No BDOS use or data allocation. CPU worker exclusively owns this state.
  memcpy(bridge->memory+kAddress,kProgram,sizeof(kProgram));
  bridge->trap_enabled=false;
  bridge->trap_bypass_once=false;
  bridge->service_trap_enabled=false;
  Z80_PC(bridge->cpu)=kAddress;
  const Z80BridgeStop stop=z80BridgeRun(bridge,200);
  const bool passed=(stop==Z80_BRIDGE_HALT &&
                     bridge->stopped_at==kAddress+4 &&
                     Z80_A(bridge->cpu)==0x42);
  memcpy(bridge->memory+kAddress,original,sizeof(original));
  bridge->cpu=savedCpu;
  bridge->trap_enabled=trapBefore;
  bridge->trap_bypass_once=bypassBefore;
  bridge->service_trap_enabled=serviceBefore;
  bridge->stop_reason=priorStop;
  bridge->stopped_at=priorStoppedAt;
  return passed;
}
} // namespace CpmServiceProbe
