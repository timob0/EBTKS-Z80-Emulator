#pragma once
#include <stdint.h>
#include <string.h>
#include "cpm_safe_point.h"
#include "z80_bridge.h"

// Stage B1: CP/M 2.2 read-only directory search. Only the Z80 CPU task calls
// this at the verified CCP D382 boundary, never an HTTP or Teensy callback.
// The native BDOS 17/18 API selects the current drive + user area. Results
// contain DIRECTORY EXTENTS, not yet deduplicated logical files or sizes.
namespace CpmDirectoryService {
static constexpr uint16_t kProgram=0xC000;
static constexpr uint16_t kFcb=0xC020;
static constexpr uint16_t kStack=0xC100;
static constexpr uint16_t kDma=0x0080;
static constexpr uint16_t kBdosDmaVariable=0xE5B1; // cpm22.lst USERDMA
static constexpr unsigned kScratchBytes=0x100;
static constexpr unsigned kMaxSlices=256;
static constexpr size_t kCyclesPerSlice=12000;

// Simple CP/M trampoline: LD DE,C020 / LD C,17 or 18 / CALL 0005 / NOP.
// Trap the instruction at C008 BEFORE it executes, so the redcode core
// can be resumed for Search Next without relying on HALT release behavior.
inline bool invoke(Z80Bridge *bridge,uint8_t function,void (*yieldCpu)()) {
  uint8_t *ram=bridge->memory;
  const uint8_t code[9]={0x11,0x20,0xC0,0x0E,function,0xCD,0x05,0x00,0x00};
  memcpy(ram+kProgram,code,sizeof(code));
  Z80_PC(bridge->cpu)=kProgram;
  Z80_SP(bridge->cpu)=kStack;
  bridge->trap_enabled=true;
  bridge->trap_address=kProgram+8;
  bridge->trap_bypass_once=false;
  bridge->service_fetch=nullptr;

  for(unsigned slice=0;slice<kMaxSlices;++slice) {
    if(bridge->running && !*bridge->running)return false;
    const Z80BridgeStop stop=z80BridgeRun(bridge,kCyclesPerSlice);
    if(stop==Z80_BRIDGE_TRAP)
      return bridge->stopped_at==kProgram+8;
    if(stop!=Z80_BRIDGE_CONTINUE)return false;
    if(yieldCpu)yieldCpu();
  }
  return false;
}

// needReset is true only when BDOS was entered and did not finish safely.
// In that case the caller must request Z80-only reset: do not resume a
// partially interrupted, potentially non-reentrant BDOS activation.
inline bool execute(Z80Bridge *bridge,CpmSafePoint::Directory &result,
                    bool &needReset,uint8_t drive=0,void (*yieldCpu)()=nullptr) {
  needReset=false;
  result=CpmSafePoint::Directory{};
  if(!bridge || !bridge->memory || drive>16 ||
     (bridge->running && !*bridge->running)) {
    result.error=CpmSafePoint::DIR_NOT_RUNNING;
    return false;
  }
  uint8_t *ram=bridge->memory;
  // Fail closed on other CP/M variants, corrupted page-zero vector or
  // unexpected DMA. CCP is expected to have installed standard DMA 0080.
  if(ram[5]!=0xC3 || ram[6]!=0x06 || ram[7]!=0xD8) {
    result.error=CpmSafePoint::DIR_BAD_VECTOR;
    return false;
  }
  if(ram[kBdosDmaVariable]!=0x80 || ram[kBdosDmaVariable+1]!=0x00) {
    result.error=CpmSafePoint::DIR_BAD_DMA;
    return false;
  }

  // C000-C0FF holds the trampoline, search FCB and a short temporary stack.
  // A CP/M search deposits a 128-byte directory record at default DMA 0080.
  // Neither region remains changed after the operation.
  uint8_t scratch[kScratchBytes];
  uint8_t dma[128];
  memcpy(scratch,ram+kProgram,sizeof(scratch));
  memcpy(dma,ram+kDma,sizeof(dma));
  const Z80 savedCpu=bridge->cpu;
  const bool oldTrap=bridge->trap_enabled;
  const bool oldBypass=bridge->trap_bypass_once;
  const uint16_t oldTrapAddress=bridge->trap_address;
  const auto oldServiceFetch=bridge->service_fetch;
  const Z80BridgeStop oldStop=bridge->stop_reason;
  const uint16_t oldStoppedAt=bridge->stopped_at;

  memset(ram+kFcb,0,36);
  ram[kFcb]=drive; // 0=current, 1..16=explicit A:..P:
  result.drive=char('A'+(drive?drive-1:(ram[4]&0x0F)));
  memset(ram+kFcb+1,'?',11); // 8.3 wildcard, current user area
  bool ok=true;
  bool completed=false;
  bool bdosUnsafe=false;
  for(unsigned index=0;index<=CpmSafePoint::kDirectoryMax;++index) {
    // First/next are BDOS calls 17 and 18; NO BDOS filesystem writes.
    // The BDOS internal directory search state persists between calls.
    if(!invoke(bridge,index==0?17:18,yieldCpu)) {
      // A BDOS invocation that never returned to the trampoline could have
      // left the guest inside its non-reentrant BDOS; only this failure
      // requires a Z80-only reset. Malformed disk metadata does NOT.
      bdosUnsafe=true;
      result.error=CpmSafePoint::DIR_BDOS_INTERRUPTED;
      ok=false;break;
    }
    const uint8_t found=Z80_A(bridge->cpu);
    if(found==0xFF) {completed=true;break;}
    if(found>3) {result.error=CpmSafePoint::DIR_INVALID_SLOT;ok=false;break;}
    if(index==CpmSafePoint::kDirectoryMax) {
      result.truncated=true;
      completed=true;
      break;
    }
    const uint8_t *entry=ram+kDma+(uint16_t)found*32;
    if(entry[0]>15) {result.error=CpmSafePoint::DIR_INVALID_USER;ok=false;break;}

    // Directory filesystems can contain non-canonical raw 8.3 entries.
    // A bad filename byte is NOT proof of an interrupted BDOS execution.
    // Never fabricate a name: skip that one entry, finish the search and
    // report the exact first offending on-disk byte for diagnosis.
    CpmSafePoint::DirectoryEntry &dst=result.entries[result.count];
    dst.user=entry[0];
    dst.extent=entry[12];
    dst.records=entry[15];
    unsigned pos=0;
    unsigned badOffset=0xFF;
    for(unsigned j=0;j<8;++j) {
      const uint8_t ch=entry[1+j]&0x7F; // CP/M high-bit attributes
      if(ch!=' ') {
        if(ch<33 || ch>126) {badOffset=1+j;break;}
        dst.name[pos++]=char(ch);
      }
    }
    if(badOffset==0xFF && pos==0)badOffset=1; // missing basename
    if(badOffset==0xFF) {
      unsigned suffixEnd=3;
      while(suffixEnd && (entry[9+suffixEnd-1]&0x7F)==' ')--suffixEnd;
      if(suffixEnd)dst.name[pos++]='.';
      for(unsigned j=0;j<suffixEnd;++j) {
        const uint8_t ch=entry[9+j]&0x7F;
        if(ch<33 || ch>126) {badOffset=9+j;break;}
        dst.name[pos++]=char(ch);
      }
    }
    if(badOffset!=0xFF) {
      if(result.skipped==0) {
        result.firstBadIndex=uint8_t(index);
        result.firstBadOffset=uint8_t(badOffset);
        result.firstBadByte=entry[badOffset]; // raw byte, before attribute mask
      }
      ++result.skipped;
      // Successful completion with DIR_INVALID_NAME means the listing is
      // usable but contains a non-fatal, explicitly disclosed omission.
      result.error=CpmSafePoint::DIR_INVALID_NAME;
      dst=CpmSafePoint::DirectoryEntry{};
      continue;
    }
    dst.name[pos]=0;
    ++result.count;
  }

  // A successful read must leave the BDOS DMA pointer unchanged.
  const bool dmaChanged=ram[kBdosDmaVariable]!=0x80 ||
                        ram[kBdosDmaVariable+1]!=0x00;
  if(dmaChanged) {
    result.error=CpmSafePoint::DIR_DMA_CHANGED;
    ok=false;
  }
  memcpy(ram+kDma,dma,sizeof(dma));
  memcpy(ram+kProgram,scratch,sizeof(scratch));
  bridge->cpu=savedCpu;
  bridge->trap_enabled=oldTrap;
  bridge->trap_bypass_once=oldBypass;
  bridge->trap_address=oldTrapAddress;
  bridge->service_fetch=oldServiceFetch;
  bridge->stop_reason=oldStop;
  bridge->stopped_at=oldStoppedAt;
  // Only genuinely unsafe guest execution (or an unexpectedly modified
  // BDOS DMA pointer) justifies a forced restart. Never reboot the Z80
  // because an on-disk filename failed host-side formatting validation.
  needReset=bdosUnsafe || dmaChanged;
  return ok && completed;
}
} // namespace CpmDirectoryService
