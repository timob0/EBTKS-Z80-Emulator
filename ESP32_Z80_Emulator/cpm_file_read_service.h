#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "cpm_directory_service.h"

// Stage B2a: a SINGLE exact CP/M 128-byte record from a mounted live
// drive. Never edit the image or invoke BDOS outside the verified CCP D382
// safe point. All functions touching bridge->memory run on pinned CPU task.
namespace CpmFileReadService {
static constexpr uint16_t kProgram=CpmDirectoryService::kProgram;
static constexpr uint16_t kFcb=CpmDirectoryService::kFcb;
static constexpr uint16_t kDma=CpmDirectoryService::kDma;
static constexpr uint16_t kDmaVariable=CpmDirectoryService::kBdosDmaVariable;
static constexpr uint16_t kMaxRecord=63; // diagnostic bound, NOT full-file export

inline bool validFcbChar(char c) {
  return (c>='A' && c<='Z') || (c>='0' && c<='9') ||
         c=='_' || c=='$' || c=='#' || c=='@' || c=='!' ||
         c=='%' || c=='&' || c==39 || c=='(' || c==')' ||
         c=='-' || c=='^';
}

// Require exact drive and 8.3 filename; no wildcard, user change,
// arbitrary SD paths, controls, spaces or ambiguous default drive.
inline bool parseName(const char *name,uint8_t &drive,uint8_t fcb[11]) {
  if(!name || !fcb)return false;
  const size_t n=strnlen(name,17);
  if(n<4 || n>14 || name[1]!=':')return false;
  char d=name[0];
  if(d>='a' && d<='z')d-=32;
  if(d<'A'||d>'P')return false;
  drive=uint8_t(d-'A'+1);
  memset(fcb,' ',11);
  unsigned pos=0, ext=0;
  bool dot=false;
  for(size_t i=2;i<n;++i) {
    char c=name[i];
    if(c>='a' && c<='z')c-=32;
    if(c=='.') {
      if(dot || pos==0 || i+1==n)return false;
      dot=true;
      continue;
    }
    if(!validFcbChar(c))return false;
    if(!dot) {
      if(pos>=8)return false;
      fcb[pos++]=uint8_t(c);
    } else {
      if(ext>=3)return false;
      fcb[8+ext++]=uint8_t(c);
    }
  }
  return pos>0;
}

inline bool execute(Z80Bridge *bridge,const uint8_t fcbName[11],
                    uint8_t drive,uint16_t target,
                    CpmSafePoint::FileRecord &out,bool &needReset,
                    void (*yieldCpu)()=nullptr) {
  needReset=false;
  out=CpmSafePoint::FileRecord{};
  out.record=target;
  if(!bridge || !bridge->memory ||
     (bridge->running && !*bridge->running)) {
    out.error=CpmSafePoint::FILE_NOT_RUNNING;
    return false;
  }
  if(!fcbName || drive<1 || drive>16 || target>kMaxRecord) {
    out.error=CpmSafePoint::FILE_BAD_REQUEST;
    return false;
  }
  uint8_t *const ram=bridge->memory;
  if(ram[5]!=0xC3 || ram[6]!=0x06 || ram[7]!=0xD8) {
    out.error=CpmSafePoint::FILE_BAD_VECTOR;
    return false;
  }
  if(ram[kDmaVariable]!=0x80 || ram[kDmaVariable+1]!=0x00) {
    out.error=CpmSafePoint::FILE_BAD_DMA;
    return false;
  }

  // Save precisely the scratch/FCB/stack and default DMA changed by BDOS.
  uint8_t scratch[256],dma[128];
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
  ram[kFcb]=drive;
  memcpy(ram+kFcb+1,fcbName,11);
  bool ok=true;
  bool unsafe=false;
  bool opened=false;

  // BDOS 15 OPEN. FF = not found. A successful OPEN initializes the
  // FCB allocation/extent fields required by sequential reads.
  if(!CpmDirectoryService::invoke(bridge,15,yieldCpu)) {
    unsafe=true;out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;ok=false;
  } else if(Z80_A(bridge->cpu)==0xFF) {
    out.error=CpmSafePoint::FILE_OPEN_NOT_FOUND;ok=false;
  } else opened=true;

  if(opened) {
    // FCB at C020 must remain live across all BDOS20 calls; we only
    // restore guest C000-C0FF AFTER CLOSE, never between records.
    for(uint16_t i=0;i<=target;++i) {
      if(!CpmDirectoryService::invoke(bridge,20,yieldCpu)) {
        unsafe=true;out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
        ok=false;break;
      }
      const uint8_t rc=Z80_A(bridge->cpu);
      if(rc==1) {out.eof=true;break;}
      if(rc!=0) {out.error=CpmSafePoint::READ_RECORD_ERROR;ok=false;break;}
      if(i==target) {
        memcpy(out.data,ram+kDma,sizeof(out.data));
        out.crc16=CpmSafePoint::fileRecordCrc(out.data,sizeof(out.data));
        out.hasData=true;
      }
    }

    // Even at EOF, close the FCB. Do not attempt close after a trapped
    // /unfinished BDOS call; guest must be reset in that case.
    if(!unsafe) {
      if(!CpmDirectoryService::invoke(bridge,16,yieldCpu)) {
        unsafe=true;out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
        ok=false;
      } else if(Z80_A(bridge->cpu)==0xFF) {
        out.error=CpmSafePoint::FILE_CLOSE_ERROR;ok=false;
      }
    }
  }

  const bool dmaChanged=ram[kDmaVariable]!=0x80 ||
                         ram[kDmaVariable+1]!=0;
  if(dmaChanged && !unsafe) {
    out.error=CpmSafePoint::FILE_DMA_CHANGED;
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
  needReset=unsafe || dmaChanged;
  if(!ok)out.hasData=false;
  return ok;
}
} // namespace CpmFileReadService
