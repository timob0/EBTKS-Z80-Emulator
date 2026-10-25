#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "cpm_file_read_service.h"

// B2c: creates a NEW CP/M file using the native CCP D382h safe point.
// Never uses raw mounted-disk writes. A failed/partial MAKE is NOT silently
// deleted: report its destination and require an operator decision.
// All guest memory and CPU access remains on the pinned Z80 worker.
namespace CpmFileWriteService {
static constexpr uint16_t kDma=CpmDirectoryService::kDma;
static constexpr uint16_t kFcb=CpmDirectoryService::kFcb;
static constexpr uint16_t kProgram=CpmDirectoryService::kProgram;
static constexpr uint16_t kDmaVariable=CpmDirectoryService::kBdosDmaVariable;
static constexpr uint16_t kMaxRecords=63;

struct Outcome {
  uint8_t error=CpmSafePoint::FILE_OK;
  uint16_t written=0;
  uint16_t crc=0xFFFF;
  bool partialPossible=false; // true if MAKE may have changed the directory
};
typedef bool (*ReadSource)(void *context,uint16_t record,uint8_t bytes[128],
                           uint16_t &recordCrc);
inline uint16_t extendCrc(uint16_t crc,const uint8_t *bytes,size_t length) {
  for(size_t i=0;i<length;++i) {
    crc^=uint16_t(bytes[i])<<8;
    for(unsigned b=0;b<8;++b)
      crc=(crc&0x8000)?uint16_t((crc<<1)^0x1021):uint16_t(crc<<1);
  }
  return crc;
}
inline bool execute(Z80Bridge *bridge,const uint8_t fcbName[11],
                    uint8_t drive,uint16_t totalRecords,uint16_t expectedCrc,
                    ReadSource readSource,void *sourceContext,
                    Outcome &out,bool &needReset,
                    void (*yieldCpu)()=nullptr,
                    CpmSafePoint::Gate *progressGate=nullptr) {
  out=Outcome{};
  needReset=false;
  if(!bridge || !bridge->memory ||
     (bridge->running && !*bridge->running)) {
    out.error=CpmSafePoint::FILE_NOT_RUNNING;return false;
  }
  if(!fcbName || !readSource || drive<1 || drive>16 ||
     totalRecords==0 || totalRecords>kMaxRecords) {
    out.error=CpmSafePoint::FILE_BAD_REQUEST;return false;
  }
  uint8_t *const ram=bridge->memory;
  if(ram[5]!=0xC3 || ram[6]!=0x06 || ram[7]!=0xD8) {
    out.error=CpmSafePoint::FILE_BAD_VECTOR;return false;
  }
  if(ram[kDmaVariable]!=0x80 || ram[kDmaVariable+1]!=0) {
    out.error=CpmSafePoint::FILE_BAD_DMA;return false;
  }

  uint8_t savedScratch[256],savedDma[128];
  memcpy(savedScratch,ram+kProgram,sizeof(savedScratch));
  memcpy(savedDma,ram+kDma,sizeof(savedDma));
  const Z80 savedCpu=bridge->cpu;
  const bool oldTrap=bridge->trap_enabled;
  const bool oldBypass=bridge->trap_bypass_once;
  const uint16_t oldTrapAddr=bridge->trap_address;
  const auto oldFetch=bridge->service_fetch;
  const Z80BridgeStop oldStop=bridge->stop_reason;
  const uint16_t oldStoppedAt=bridge->stopped_at;
  bool unsafe=false,created=false,ok=false;
  uint8_t bytes[128];

  // Exact directory search, under the same paused native CCP context.
  // A==FF is the *only* condition permitting MAKE. All other returns,
  // including unexpected search slots, refuse to create anything.
  memset(ram+kFcb,0,36);
  ram[kFcb]=drive;
  memcpy(ram+kFcb+1,fcbName,11);
  if(!CpmDirectoryService::invoke(bridge,17,yieldCpu)) {
    out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;unsafe=true;
  } else if(Z80_A(bridge->cpu)!=0xFF) {
    out.error=CpmSafePoint::FILE_TARGET_EXISTS;
  } else {
    memset(ram+kFcb,0,36);
    ram[kFcb]=drive;
    memcpy(ram+kFcb+1,fcbName,11);
    // An interrupted MAKE may already have changed the directory.
    out.partialPossible=true;
    if(!CpmDirectoryService::invoke(bridge,22,yieldCpu)) {
      out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;unsafe=true;
    } else if(Z80_A(bridge->cpu)==0xFF) {
      out.error=CpmSafePoint::FILE_MAKE_FAILED;
    } else {
      created=true;
      bool allGood=true;
      for(uint16_t index=0;index<totalRecords;++index) {
        uint16_t receivedCrc=0;
        if(!readSource(sourceContext,index,bytes,receivedCrc)) {
          out.error=CpmSafePoint::FILE_SD_SOURCE_FAILED;
          allGood=false;break;
        }
        if(CpmSafePoint::fileRecordCrc(bytes,sizeof(bytes))!=receivedCrc) {
          out.error=CpmSafePoint::FILE_IMPORT_CRC_MISMATCH;
          allGood=false;break;
        }
        memcpy(ram+kDma,bytes,sizeof(bytes));
        if(!CpmDirectoryService::invoke(bridge,21,yieldCpu)) {
          out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
          unsafe=true;allGood=false;break;
        }
        if(Z80_A(bridge->cpu)!=0) {
          out.error=CpmSafePoint::FILE_WRITE_RECORD_FAILED;
          allGood=false;break;
        }
        out.crc=extendCrc(out.crc,bytes,sizeof(bytes));
        ++out.written;
        if(progressGate) {
          progressGate->exportRecords.store(out.written,std::memory_order_release);
          progressGate->exportCrc.store(out.crc,std::memory_order_release);
        }
      }
      if(allGood && out.crc!=expectedCrc) {
        out.error=CpmSafePoint::FILE_IMPORT_CRC_MISMATCH;
        allGood=false;
      }
      // CLOSE the newly created FCB even after an ordinary disk-full or
      // source-read error. Never re-enter BDOS after an interrupted call.
      if(!unsafe) {
        if(!CpmDirectoryService::invoke(bridge,16,yieldCpu)) {
          out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;unsafe=true;
          allGood=false;
        } else if(Z80_A(bridge->cpu)==0xFF) {
          if(allGood)out.error=CpmSafePoint::FILE_CLOSE_ERROR;
          allGood=false;
        }
      }
      if(allGood) {
        // Independent CP/M BDOS readback. FCB must be freshly reopened
        // after closing the new file, and sequential record count checked.
        memset(ram+kFcb,0,36);
        ram[kFcb]=drive;
        memcpy(ram+kFcb+1,fcbName,11);
        bool opened=false;
        if(!CpmDirectoryService::invoke(bridge,15,yieldCpu)) {
          out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;unsafe=true;
        } else if(Z80_A(bridge->cpu)==0xFF) {
          out.error=CpmSafePoint::FILE_IMPORT_VERIFY_FAILED;
        } else {
          opened=true;
          uint16_t readbackCrc=0xFFFF;
          for(uint16_t index=0;index<totalRecords;++index) {
            if(!CpmDirectoryService::invoke(bridge,20,yieldCpu)) {
              out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
              unsafe=true;break;
            }
            if(Z80_A(bridge->cpu)!=0) {
              out.error=CpmSafePoint::FILE_IMPORT_VERIFY_FAILED;
              break;
            }
            readbackCrc=extendCrc(readbackCrc,ram+kDma,128);
          }
          if(!unsafe && out.error==CpmSafePoint::FILE_OK) {
            if(!CpmDirectoryService::invoke(bridge,20,yieldCpu)) {
              out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
              unsafe=true;
            } else if(Z80_A(bridge->cpu)!=1 ||
                      readbackCrc!=expectedCrc) {
              out.error=CpmSafePoint::FILE_IMPORT_VERIFY_FAILED;
            }
          }
        }
        if(opened && !unsafe) {
          if(!CpmDirectoryService::invoke(bridge,16,yieldCpu)) {
            out.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;unsafe=true;
          } else if(Z80_A(bridge->cpu)==0xFF &&
                    out.error==CpmSafePoint::FILE_OK) {
            out.error=CpmSafePoint::FILE_CLOSE_ERROR;
          }
        }
        ok=!unsafe && out.error==CpmSafePoint::FILE_OK;
      }
    }
  }

  const bool dmaChanged=ram[kDmaVariable]!=0x80 || ram[kDmaVariable+1]!=0;
  if(dmaChanged && !unsafe) {
    out.error=CpmSafePoint::FILE_DMA_CHANGED;
    ok=false;
  }
  memcpy(ram+kDma,savedDma,sizeof(savedDma));
  memcpy(ram+kProgram,savedScratch,sizeof(savedScratch));
  bridge->cpu=savedCpu;
  bridge->trap_enabled=oldTrap;
  bridge->trap_bypass_once=oldBypass;
  bridge->trap_address=oldTrapAddr;
  bridge->service_fetch=oldFetch;
  bridge->stop_reason=oldStop;
  bridge->stopped_at=oldStoppedAt;
  needReset=unsafe||dmaChanged;
  // Once the native CP/M file and close/readback have been verified, it
  // is no longer "partial"; errors conservatively retain this warning.
  if(ok)out.partialPossible=false;
  (void)created;
  return ok;
}
} // namespace CpmFileWriteService
