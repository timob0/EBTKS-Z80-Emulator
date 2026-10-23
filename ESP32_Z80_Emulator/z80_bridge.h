// Portable memory and I/O adapter; no Arduino or Teensy dependency.
#pragma once
#include <stdint.h>
#include <stddef.h>
#define Z80_STATIC
#define Z80_EXTERNAL_HEADER "ebtks_zeta_compat.h"
#include "vendor/redcode_z80/Z80.h"

enum Z80BridgeStop { Z80_BRIDGE_CONTINUE=0, Z80_BRIDGE_TRAP=1,
                     Z80_BRIDGE_HALT=2, Z80_BRIDGE_EXTERNAL_STOP=3,
                     Z80_BRIDGE_SERVICE=4 };
struct Z80Bridge {
  Z80 cpu;
  uint8_t *memory;
  void *device;
  uint8_t (*input)(void*,uint16_t);
  void (*output)(void*,uint16_t,uint8_t);
  volatile bool *running;
  bool trap_enabled;
  bool trap_bypass_once;
  uint16_t trap_address;
  uint16_t stopped_at;
  Z80BridgeStop stop_reason;
  // Optional exact-PC service hook. No network/SD/file access in opcode fetch.
  // Reuse the existing device context for the optional service fetch.
  // EBTKS I/O callbacks ignore device; native bridge clients may set it.
  // Null callback disables the service with no extra bool/context in DRAM.
  bool (*service_fetch)(void *,uint16_t,const uint8_t *);
};
static uint8_t z80BridgeRead(void *ctx,uint16_t address) {
  return static_cast<Z80Bridge*>(ctx)->memory[address];
}
static void z80BridgeWrite(void *ctx,uint16_t address,uint8_t value) {
  static_cast<Z80Bridge*>(ctx)->memory[address]=value;
}
static uint8_t z80BridgeOpcode(void *ctx,uint16_t address) {
  Z80Bridge *b=static_cast<Z80Bridge*>(ctx);
  if (b->running && !*b->running) {
    b->stop_reason=Z80_BRIDGE_EXTERNAL_STOP;
    b->stopped_at=address;
    z80_break(&b->cpu);
    return Z80_NOP; // no side effect; rewind PC after the current step
  }
  // The four original CCP addresses establish the safe-point boundary.
  // Directory auto-Return additionally needs to observe the native BDOS
  // function-10 input count and the two BIOS CONIN status-poll opcodes.
  // Keep the filter exact-PC: never inspect arbitrary guest instructions or
  // mistake console OUTPUT polling for input. No hook does UART/SD I/O here.
  if (b->service_fetch &&
      (address==0xD19C || address==0xD1A4 ||
       address==0xD382 || address==0xD398 ||
       address==0xD9EF || address==0xE826 || address==0xE831) &&
      b->service_fetch(b->device,address,b->memory)) {
    b->stop_reason=Z80_BRIDGE_SERVICE;
    b->stopped_at=address;
    z80_break(&b->cpu);
    return Z80_NOP;
  }
  if (b->trap_enabled && address==b->trap_address) {
    if (b->trap_bypass_once) b->trap_bypass_once=false;
    else {
      b->trap_bypass_once=true;
      b->stop_reason=Z80_BRIDGE_TRAP;
      b->stopped_at=address;
      z80_break(&b->cpu);
      return Z80_NOP;
    }
  }
  return b->memory[address];
}
static uint8_t z80BridgeIn(void *ctx,uint16_t port) {
  Z80Bridge *b=static_cast<Z80Bridge*>(ctx);
  return b->input?b->input(b->device,port):0xFF;
}
static void z80BridgeOut(void *ctx,uint16_t port,uint8_t value) {
  Z80Bridge *b=static_cast<Z80Bridge*>(ctx);
  if(b->output) b->output(b->device,port,value);
  if(b->running && !*b->running) z80_break(&b->cpu);
}
static void z80BridgeHalt(void *ctx,uint8_t signal) {
  Z80Bridge *b=static_cast<Z80Bridge*>(ctx);
  if(signal==1) {
    b->stop_reason=Z80_BRIDGE_HALT;
    b->stopped_at=(uint16_t)(Z80_PC(b->cpu)-1);
    z80_break(&b->cpu);
  }
}
static void z80BridgeInit(Z80Bridge *b,uint8_t *memory,
                          uint8_t (*input)(void*,uint16_t),
                          void (*output)(void*,uint16_t,uint8_t),
                          void *device,volatile bool *running=nullptr) {
  *b={};
  b->memory=memory; b->device=device;
  b->input=input; b->output=output; b->running=running;
  b->cpu.context=b;
  b->cpu.fetch_opcode=z80BridgeOpcode;
  b->cpu.fetch=z80BridgeRead;
  b->cpu.read=z80BridgeRead;
  b->cpu.write=z80BridgeWrite;
  b->cpu.in=z80BridgeIn;
  b->cpu.out=z80BridgeOut;
  b->cpu.halt=z80BridgeHalt;
  b->cpu.options=Z80_MODEL_ZILOG_NMOS;
  z80_power(&b->cpu,Z_TRUE);
}
static void z80BridgeColdReset(Z80Bridge *b) {
  z80_power(&b->cpu,Z_TRUE);
  b->cpu.options=Z80_MODEL_ZILOG_NMOS;
  b->trap_bypass_once=false;
  b->trap_enabled=false;
  b->stop_reason=Z80_BRIDGE_CONTINUE;
  b->stopped_at=0;
}
static Z80BridgeStop z80BridgeRun(Z80Bridge *b,size_t cycleBudget) {
  b->stop_reason=Z80_BRIDGE_CONTINUE;
  (void)z80_run(&b->cpu,cycleBudget);
  if (b->stop_reason==Z80_BRIDGE_TRAP ||
      b->stop_reason==Z80_BRIDGE_EXTERNAL_STOP ||
      b->stop_reason==Z80_BRIDGE_SERVICE) {
    Z80_PC(b->cpu)=b->stopped_at;
  }
  return b->stop_reason;
}
