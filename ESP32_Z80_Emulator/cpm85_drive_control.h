#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "cpm_catalog_format.h"
#include <ctype.h>
#include <stdio.h>
#include <new>
#include <string.h>

// HP/CPM management over the already-running cpm_keys poll. Only this
// control worker touches the mailbox; the established UART mutex serializes
// its SD/NVS transactions against CP/M disk I/O. Never send unsolicited UART.
namespace Cpm85DriveControl {
static bool pendingAck=false, haveLast=false;
static uint16_t lastSequence=0;
static uint8_t lastOperation=0;
static char lastTransferPhase='E'; // W=CCP wait, P=armed, R=BDOS, S/F/X terminal, E=invalid
static bool lastOk=false;
static constexpr size_t kTextMax=680;
static char *lastText=nullptr; // lazily allocated, outside static TFT DRAM

inline const char *basename(const char *path) {
  const char *p=path ? strrchr(path,'/') : nullptr;
  return p ? p+1 : (path ? path : "");
}
inline bool driveIndex(const char *s,uint8_t &drive) {
  if(!s)return false;
  const unsigned char c=(unsigned char)s[0];
  if(!isalpha(c))return false;
  const int n=toupper(c)-'A';
  if(n<0||n>=CPM_DRIVE_COUNT)return false;
  if(s[1] && !(s[1]==':' && s[2]==0))return false;
  drive=(uint8_t)n;
  return true;
}
inline bool safeBasename(const char *p) {
  if(!p||!*p)return false;
  const size_t n=strlen(p);
  if(n>=CPM_MEDIA_PATH_MAX-strlen(CPM_DISK_ROOT)-2)return false;
  if(p[0]=='.')return false;
  for(size_t i=0;i<n;++i) {
    const unsigned char c=(unsigned char)p[i];
    if(!(isalnum(c)||c=='-'||c=='_'||c=='.'))return false;
  }
  return true;
}
inline void setResult(bool ok,const char *msg) {
  lastOk=ok;
  if(lastText) strlcpy(lastText,msg?msg:"",kTextMax);
}
inline void status() {
  if(!lastText)return;
  if(!cpmStoreLoaded&&!cpmStoreLoadDesired()) {
    setResult(false,"CP/M MOUNTS UNAVAILABLE");
    return;
  }
  size_t used=0;
  used+=snprintf(lastText,kTextMax,"CP/M DRIVE STATUS\n");
  for(uint8_t i=0;i<CPM_DRIVE_COUNT && used<kTextMax-44;++i) {
    const CpmMountEntry &a=cpmActiveMounts[i];
    const CpmMountEntry &d=cpmDesiredMounts[i];
    if(!a.mounted && !d.mounted)continue;
    const char *active=a.mounted?basename(a.path):"-";
    const char *desired=d.mounted?basename(d.path):"-";
    const bool changed=!cpmMountEntryEqual(a,d);
    int n=snprintf(lastText+used,kTextMax-used,
                   "%c: %.14s%s",char('A'+i),active,changed?" -> ":"\n");
    if(n<0 || (size_t)n>=kTextMax-used)break;
    used+=(size_t)n;
    if(changed) {
      n=snprintf(lastText+used,kTextMax-used,"%.21s\n",desired);
      if(n<0 || (size_t)n>=kTextMax-used)break;
      used+=(size_t)n;
    }
  }
  if(cpmMountTablesDiffer() && used+17<kTextMax)
    strlcat(lastText,"CPRESET REQUIRED\n",kTextMax);
  lastOk=true;
}

// HP's CRT has a 768-byte mailbox bound, so enumerate with a hard
// truncation notice rather than sending the unbounded SD directory listing.
inline void images() {
  if (!lastText) return;
  JsonDocument listing;
  JsonArray files;
  if (!cpmStoreFetchDiskDirectory(listing, files)) {
    setResult(false, "CP/M IMAGE DIRECTORY UNAVAILABLE"); return;
  }
  size_t used = 0;
  int n = snprintf(lastText, kTextMax, "CP/M DISK IMAGES (.DSK)\n");
  if (n < 0 || (size_t)n >= kTextMax) {setResult(false, "LIST ERROR"); return;}
  used = (size_t)n;
  unsigned count = 0, shown = 0;
  for (JsonObjectConst item : files) {
    const char *name = item["name"] | "";
    const char *kind = item["type"] | "";
    // Exclude hidden/metadata files such as macOS AppleDouble ._WORK.DSK.
    if (name[0] == '.') continue;
    const char *dot = strrchr(name, '.');
    if (strcmp(kind, "file") || !dot || strcasecmp(dot, ".dsk") != 0) continue;
    ++count;
    const uint32_t bytes = item["size"] | 0UL;
    // Keep room for the mandatory count/truncation footer.
    if (used + 57 >= kTextMax) continue;
    n = snprintf(lastText + used, kTextMax - used,
                 "%.32s %luK\n", name, (unsigned long)(bytes / 1024UL));
    if (n > 0 && (size_t)n < kTextMax - used) {
      used += (size_t)n;
      ++shown;
    }
  }
  snprintf(lastText + used, kTextMax - used,
           "%u IMAGE(S)%s\n", count, shown < count ? " (LIST TRUNCATED)" : "");
  lastOk = true;
}

// Kick off a Teensy-owned job and poll completion. The Teensy writes no more
// than 4096 bytes each pass through its ordinary main loop. No 8 MB buffers,
// no UART file payloads, and no changes to active CP/M mount assignments.
inline void makeImage(const char *name) {
  if (!safeBasename(name)) {setResult(false, "INVALID DISK IMAGE NAME"); return;}
  const char *dot = strrchr(name, '.');
  if (dot && strcasecmp(dot, ".dsk") != 0) {
    setResult(false, "IMAGE MUST END IN .DSK"); return;
  }
  JsonDocument request;
  request["cpm_image"]["op"] = "begin";
  request["cpm_image"]["name"] = name;
  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply, 3000)) {
    setResult(false, "IMAGE CREATE: NO TEENSY REPLY"); return;
  }
  const char *state = reply["cpm_image"]["state"] | "";
  if (strcmp(state, "running") != 0) {
    const char *reason = reply["cpm_image"]["error"] | "image_create_failed";
    char msg[112];
    snprintf(msg, sizeof(msg), "CPDMK FAILED: %.70s", reason);
    setResult(false, msg); return;
  }
  const char *expected = reply["cpm_image"]["name"] | "";
  char canonical[76] = {};
  strlcpy(canonical, name, sizeof(canonical));
  if (!dot) strlcat(canonical, ".DSK", sizeof(canonical));
  if (strcasecmp(expected, canonical) != 0) {
    setResult(false, "CREATE BUSY: ANOTHER IMAGE"); return;
  }

  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < 45000UL) {
    vTaskDelay(pdMS_TO_TICKS(25));
    JsonDocument statusReq;
    statusReq["cpm_image"]["op"] = "status";
    JsonDocument statusReply;
    if (!ebtksStorageExchange(statusReq, statusReply, 3000)) continue;
    const char *now = statusReply["cpm_image"]["state"] | "";
    if (!strcmp(now, "done")) {
      const uint32_t bytes = statusReply["cpm_image"]["written"] | 0UL;
      if (bytes != CPM_NATIVE_BYTES) {
        setResult(false, "IMAGE SIZE VERIFICATION FAILED"); return;
      }
      char msg[112];
      snprintf(msg, sizeof(msg), "CREATED %.70s\n8 MB CP/M DISK READY", canonical);
      setResult(true, msg); return;
    }
    if (!strcmp(now, "error")) {
      const char *reason = statusReply["cpm_image"]["error"] | "sd_error";
      char msg[112];
      snprintf(msg, sizeof(msg), "CPDMK FAILED: %.70s", reason);
      setResult(false, msg); return;
    }
  }
  // Teensy may still be finishing the job; never undo/overwrite a file here.
  setResult(false, "IMAGE CREATE PENDING; CHECK CPDIMGS");
}

// CPDCAT A: runs native BDOS directory search only after a verified empty
// CCP line is auto-terminated on the Z80 CPU thread. Called on the existing
// Teensy-poll worker (never HP I/O ISR); timeout is bounded. No file writes.
inline void catalog(const char *arg) {
  uint8_t drive=0;
  if(!driveIndex(arg,drive)) {
    setResult(false,"CPDCAT: INVALID DRIVE A:..P:");return;
  }
  if(!RUN || !cpmActiveMounts[drive].mounted) {
    setResult(false,"CPDCAT: CP/M DRIVE NOT ACTIVE");return;
  }
  CpmSafePoint::Gate *gate=cpmSafeGatePtr();
  if(!gate || !gate->compatible.load(std::memory_order_acquire) ||
     !gate->request(CpmSafePoint::DIRECTORY,drive+1)) {
    setResult(false,"CPDCAT: CP/M BUSY OR COMMAND LINE NOT EMPTY");return;
  }
  const uint32_t started=millis();
  uint8_t state=CpmSafePoint::WAIT_EMPTY_LINE;
  while((uint32_t)(millis()-started)<12000UL) {
    state=gate->result.load(std::memory_order_acquire);
    if(state!=CpmSafePoint::WAIT_EMPTY_LINE && state!=CpmSafePoint::RUNNING)break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if(state!=CpmSafePoint::PASSED) {
    if(state==CpmSafePoint::WAIT_EMPTY_LINE || state==CpmSafePoint::RUNNING) {
      // Cancel only PENDING requests; never interrupt BDOS once running.
      // An eventual blank Return must NOT revive a timed-out HP command.
      if(state==CpmSafePoint::WAIT_EMPTY_LINE)gate->cancelPending();
      setResult(false,"CPDCAT: TIMEOUT; CHECK CP/M SERVICE STATUS");
    } else if(state==CpmSafePoint::INPUT_NOT_EMPTY) {
      setResult(false,"CPDCAT: CP/M INPUT NOT EMPTY");
    } else if(state==CpmSafePoint::INVALIDATED) {
      setResult(false,"CPDCAT: CP/M WARM BOOT CANCELLED");
    } else if(state==CpmSafePoint::FAILED) {
      char err[108];
      snprintf(err,sizeof(err),"CPDCAT: BDOS %s",
               CpmSafePoint::directoryErrorText(
                   gate->directoryError.load(std::memory_order_acquire)));
      setResult(false,err);
    } else {
      setResult(false,"CPDCAT: UNEXPECTED SERVICE STATE");
    }
    return;
  }
  // The Teensy-poll worker has only a 3584-byte stack. Do NOT copy the
  // 32-entry snapshot onto it: render from the gate's heap-owned snapshot
  // under a short nonblocking lock, avoiding old large-array stack resets.
  if(gate->directoryBusy.test_and_set(std::memory_order_acquire)) {
    setResult(false,"CPDCAT: DIRECTORY RESULT BUSY");return;
  }
  const CpmSafePoint::Directory &snapshot=gate->directory;
  bool formatted=snapshot.drive==char('A'+drive);
  size_t used=0;
  bool outputTruncated=false;
  if(formatted) {
    // HP CRT is 32 columns wide. Use actual line feeds, not a literal
    // backslash+n sequence. Keep room for all mandatory warning footers.
    const int n=snprintf(lastText,kTextMax,
                         "CP/M %c: DIRECTORY\n",char('A'+drive));
    formatted=n>0 && size_t(n)<kTextMax;
    if(formatted)used=size_t(n);
  }
  // Two 12-character 8.3 fields with six spaces between them = 30
  // printable CRT columns. Read adjacent entries left-to-right. The
  // final odd entry is printed on the left without a blank right field.
  // Bounded by the 32-entry snapshot; no heap or large stack buffers.
  for(uint8_t i=0;formatted && i<snapshot.count;i+=2) {
    // Reserve 51 bytes for footer and malformed-name warning. With 32
    // entries the two-column output fits comfortably in kTextMax=680.
    if(used+CpmCatalogFormat::kMaxRowBytes+51>=kTextMax) {
      outputTruncated=true;
      break;
    }
    const char *left=snapshot.entries[i].name;
    const char *right=(i+1<snapshot.count)?snapshot.entries[i+1].name:nullptr;
    const int n=CpmCatalogFormat::formatRow(
        lastText+used,kTextMax-used,left,right);
    formatted=n>0 && size_t(n)<kTextMax-used;
    if(formatted)used+=size_t(n);
  }
  if(formatted) {
    const int n=snprintf(lastText+used,kTextMax-used,"%u EXTENTS%s\n",
                         (unsigned)snapshot.count,
                         (snapshot.truncated||outputTruncated)?
                           " (MORE NOT SHOWN)":"");
    formatted=n>0 && size_t(n)<kTextMax-used;
    if(formatted)used+=size_t(n);
  }
  if(formatted && snapshot.skipped) {
    // Malformed on-disk entries were skipped, not invented or rewritten.
    const int n=snprintf(lastText+used,kTextMax-used,
                         "%u BAD NAME(S) SKIPPED\n",
                         (unsigned)snapshot.skipped);
    formatted=n>0 && size_t(n)<kTextMax-used;
  }
  gate->directoryBusy.clear(std::memory_order_release);
  if(!formatted) {
    setResult(false,"CPDCAT: DIRECTORY RESULT UNAVAILABLE");return;
  }
  lastOk=true;
}


 // B2d: An HP BASIC transfer is a single owned, repeatably queryable
 // transaction. The controller runs only on the existing cpm_keys poll worker.
 // BDOS and Teensy SD data still execute exclusively on the pinned CPU task.
 struct Transfer {
   enum Phase : uint8_t { WAIT_CCP, WAIT_EMPTY, ACTIVE, PASSED, FAILED, CANCELLED };
   uint16_t id=0;
   uint32_t lastClientMs=0;
   Phase phase=WAIT_CCP;
   uint8_t operation=0,drive=0,sourceDrive=0;
   uint8_t filename[11]{},source[11]{};
   uint8_t error=0,sourceIssue=0;
   uint16_t records=0,crc=0xFFFF;
   bool partial=false;
 };
 static Transfer *transfer=nullptr; // allocated lazily; TFT static DRAM guard
 inline bool transferTerminal(Transfer::Phase p) {
   return p==Transfer::PASSED||p==Transfer::FAILED||p==Transfer::CANCELLED;
 }
 inline void transferSnapshot(CpmSafePoint::Gate &gate) {
   Transfer &t=*transfer;
   const uint8_t result=gate.result.load(std::memory_order_acquire);
   if(result==CpmSafePoint::WAIT_EMPTY_LINE) {t.phase=Transfer::WAIT_EMPTY;return;}
   if(result==CpmSafePoint::RUNNING) {t.phase=Transfer::ACTIVE;return;}
   if(result==CpmSafePoint::PASSED || result==CpmSafePoint::FAILED ||
      result==CpmSafePoint::INPUT_NOT_EMPTY || result==CpmSafePoint::INVALIDATED ||
      result==CpmSafePoint::NEVER) {
     t.phase=result==CpmSafePoint::PASSED?Transfer::PASSED:Transfer::FAILED;
     t.error=gate.fileError.load(std::memory_order_acquire);
     t.sourceIssue=gate.importSourceIssue.load(std::memory_order_acquire);
     t.partial=gate.importPartialPossible.load(std::memory_order_acquire) ||
       (t.operation==10 && t.phase==Transfer::ACTIVE &&
        (result==CpmSafePoint::NEVER || result==CpmSafePoint::INVALIDATED));
     // Both B2b export and B2c import publish actual transferred record
     // counts and accumulated CRC through these progress atomics. The
     // importTotalRecords/ExpectedCrc fields are source preflight totals,
     // not evidence that a failed write finished those records.
     t.records=gate.exportRecords.load(std::memory_order_acquire);
     t.crc=gate.exportCrc.load(std::memory_order_acquire);
     if(result==CpmSafePoint::NEVER)
       t.error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
     gate.hpTransferReserved.store(false,std::memory_order_release);
   }
 }
 inline void transferPoll() {
   if(!transfer || transferTerminal(transfer->phase))return;
   CpmSafePoint::Gate *gate=cpmSafeGatePtr();
   if(!gate || !RUN || !gate->compatible.load(std::memory_order_acquire)) {
     // If an active BDOS call remains marked RUNNING, we cannot safely
     // declare completion or release ownership merely due to a guest flag.
     if(gate && gate->result.load(std::memory_order_acquire)==CpmSafePoint::RUNNING) {
       transfer->phase=Transfer::ACTIVE;
       return;
     }
     transfer->phase=Transfer::FAILED;
     transfer->error=CpmSafePoint::FILE_NOT_RUNNING;
     if(gate) {
       // Never release a potentially active service merely because the
       // guest reset; the gate reset path must first clear its RUNNING state.
       if(gate->result.load(std::memory_order_acquire)!=CpmSafePoint::RUNNING)
         gate->hpTransferReserved.store(false,std::memory_order_release);
     }
     return;
   }
   if(transfer->phase==Transfer::WAIT_CCP) {
     const bool accepted=transfer->operation==9?
       gate->request(CpmSafePoint::EXPORT_SD,transfer->drive,
                     transfer->filename,0,nullptr,0,false,true):
       gate->request(CpmSafePoint::IMPORT_SD,transfer->drive,
                     transfer->filename,0,transfer->source,
                     transfer->sourceDrive,false,true);
     if(accepted)transfer->phase=Transfer::WAIT_EMPTY;
     return;
   }
   if(gate->operation.load(std::memory_order_acquire)!=
        (transfer->operation==9?CpmSafePoint::EXPORT_SD:CpmSafePoint::IMPORT_SD)) {
     transfer->phase=Transfer::FAILED;
     transfer->error=CpmSafePoint::FILE_BDOS_INTERRUPTED;
     return; // keep reservation: unexpected gate replacement needs inspection
   }
   transferSnapshot(*gate);
 }
 inline void transferReply() {
   lastTransferPhase='E';
   if(!transfer){setResult(false,"TRANSFER NOT STARTED");return;}
   const Transfer &t=*transfer;
   if(t.phase==Transfer::WAIT_CCP) {
     lastTransferPhase='W';
     setResult(true,"WAITING FOR CP/M A> (NOT STARTED)");return;
   }
   if(t.phase==Transfer::WAIT_EMPTY) {
     lastTransferPhase='P';
     setResult(true,"WAITING FOR VERIFIED BLANK CCP RETURN");return;
   }
   if(t.phase==Transfer::ACTIVE) {
     lastTransferPhase='R';
     setResult(true,"TRANSFER RUNNING - DO NOT ABORT");return;
   }
   if(t.phase==Transfer::CANCELLED) {
     lastTransferPhase='X';
     setResult(true,"TRANSFER CANCELLED BEFORE BDOS START");return;
   }
   char msg[224];
   if(t.phase==Transfer::PASSED) {
     lastTransferPhase='S';
     snprintf(msg,sizeof(msg),"%s PASSED\n%u RECORDS %lu BYTES\nCRC16 %u",
       t.operation==9?"CPEXPORT":"CPIMPORT",(unsigned)t.records,
       (unsigned long)t.records*128UL,(unsigned)t.crc);
     setResult(true,msg);
   } else {
     lastTransferPhase='F';
     snprintf(msg,sizeof(msg),"%s FAILED\n%s / %s%s",
       t.operation==9?"CPEXPORT":"CPIMPORT",
       CpmSafePoint::fileErrorText(t.error),
       CpmSafePoint::sourceIssueText(t.sourceIssue),
       t.partial?"\nPOSSIBLE PARTIAL CP/M FILE":"");
     setResult(false,msg);
   }
 }
 inline void transferCommand(uint8_t op,const char *args,uint16_t seq,
                             uint16_t txn) {
   CpmSafePoint::Gate *gate=cpmSafeGatePtr();
   if(op==9 || op==10) {
     if(transfer && !transferTerminal(transfer->phase)) {
       setResult(false,"ANOTHER HP TRANSFER IS ACTIVE");return;
     }
     uint8_t dstDrive=0,srcDrive=0,dst[11]{},src[11]{};
     bool valid=false;
     if(op==9) valid=CpmFileReadService::parseName(args,dstDrive,dst);
     else {
       const char *sep=args?strchr(args,'|'):nullptr;
       if(sep && sep!=args && (size_t)(sep-args)<=14 &&
          strlen(sep+1)<=14) {
         char sourceName[16]{};
         memcpy(sourceName,args,(size_t)(sep-args));
         valid=CpmFileReadService::parseName(sourceName,srcDrive,src) &&
               CpmFileReadService::parseName(sep+1,dstDrive,dst);
       }
     }
     if(!valid){setResult(false,"INVALID 8.3 SOURCE OR DESTINATION");return;}
     if(!gate || !RUN || !gate->compatible.load(std::memory_order_acquire)) {
       setResult(false,"NATIVE CP/M IS NOT RUNNING");return;
     }
     if(!gate->reserveForHp()) {
       setResult(false,"CP/M SERVICE ALREADY IN USE");return;
     }
     if(!transfer)transfer=new(std::nothrow) Transfer;
     if(!transfer) {
       gate->hpTransferReserved.store(false,std::memory_order_release);
       setResult(false,"LOW MEMORY FOR TRANSFER CONTROL");return;
     }
     *transfer=Transfer{};
     transfer->id=seq;transfer->operation=op;
     transfer->lastClientMs=millis();
     transfer->drive=dstDrive;transfer->sourceDrive=srcDrive;
     memcpy(transfer->filename,dst,11);
     memcpy(transfer->source,src,11);
     transferPoll();
     transferReply();
     return;
   }
   if(!transfer || txn!=transfer->id) {
     setResult(false,"UNKNOWN TRANSFER TRANSACTION");return;
   }
   if(op==11) {
     transfer->lastClientMs=millis();
     transferPoll();transferReply();return;
   }
   if(op==12) {
     transfer->lastClientMs=millis();
     if(transferTerminal(transfer->phase)) {transferReply();return;}
     if(transfer->phase==Transfer::WAIT_CCP) {
       transfer->phase=Transfer::CANCELLED;
       if(gate)gate->hpTransferReserved.store(false,std::memory_order_release);
       transferReply();return;
     }
     if(gate && gate->cancelPending()) {
       transfer->phase=Transfer::CANCELLED;
       gate->hpTransferReserved.store(false,std::memory_order_release);
       transferReply();return;
     }
     // A losing CAS means BDOS may already be in progress. NEVER cancel it.
     transferPoll();
     if(transfer && transfer->phase==Transfer::ACTIVE) {
       lastTransferPhase='R';
       setResult(false,"ALREADY STARTED - WAIT FOR COMPLETION");return;
     }
     transferReply();return;
   }
   setResult(false,"INVALID TRANSFER CONTROL OP");
 }

// Revisit the outstanding gate from the existing cpm_keys poll loop.
 // A lost HP BASIC connection may not leave a request armed forever.
 // Expiry can only cancel a request that has not begun guest BDOS.
 inline void transferWatchdog() {
   if(!transfer || transferTerminal(transfer->phase))return;
   CpmSafePoint::Gate *gate=cpmSafeGatePtr();
   if((uint32_t)(millis()-transfer->lastClientMs)>60000UL) {
     if(transfer->phase==Transfer::WAIT_CCP) {
       transfer->phase=Transfer::CANCELLED;
       if(gate)gate->hpTransferReserved.store(false,std::memory_order_release);
       return;
     }
     if(transfer->phase==Transfer::WAIT_EMPTY && gate &&
        gate->cancelPending()) {
       transfer->phase=Transfer::CANCELLED;
       gate->hpTransferReserved.store(false,std::memory_order_release);
       return;
     }
   }
   transferPoll();
 }
inline void execute(uint8_t op,const char *args) {
  char msg[112]={};
  uint8_t drive=0;
  if(op==7){catalog(args);return;}
  if(op==1){status();return;}
  if(op==5){images();return;}
  if(op==6){makeImage(args);return;}
  if(op==4) {
    // The same persistent-state verification and Z80-only reset as the
    // Media Manager Apply endpoint. Caller must ensure CP/M is at a prompt.
    if(!cpmStoreVerifyDesired())setResult(false,"MOUNT STORE VERIFY FAILED");
    else {RUN=false;setResult(true,"Z80 RESET REQUESTED");}
    return;
  }
  if(op==3) {
    if(!driveIndex(args,drive)) {setResult(false,"INVALID DRIVE A:..P:");return;}
    if(!cpmStoreUnmountDesired(drive)) {
      setResult(false,"UNMOUNT NOT SAVED");return;
    }
    snprintf(msg,sizeof(msg),"%c: UNMOUNT PENDING\nCPRESET REQUIRED",char('A'+drive));
    setResult(true,msg);
    return;
  }
  if(op==2) {
    const char *sep=args?strchr(args,'|'):nullptr;
    if(!sep || sep==args || (sep-args)>2) {
      setResult(false,"MOUNT NEEDS DRIVE|IMAGE");return;
    }
    char driveStr[4]={};
    memcpy(driveStr,args,sep-args);
    if(!driveIndex(driveStr,drive) || !safeBasename(sep+1)) {
      setResult(false,"INVALID DRIVE OR IMAGE");return;
    }
    char path[CPM_MEDIA_PATH_MAX]={};
    snprintf(path,sizeof(path),"%s/%s",CPM_DISK_ROOT,sep+1);
    if(!cpmStoreMountDesired(drive,path)) {
      setResult(false,"IMAGE MISSING OR UNSUPPORTED");return;
    }
    snprintf(msg,sizeof(msg),"%c: %.32s PENDING\nCPRESET REQUIRED",
             char('A'+drive),sep+1);
    setResult(true,msg);
    return;
  }
  setResult(false,"UNKNOWN CP/M COMMAND");
}
inline void receiveCommand(JsonVariantConst command) {
  if(command.isNull())return;
  const int seq=command["seq"] | -1;
  const int op=command["op"] | -1;
  if(seq<0||seq>65535)return;
  // Only first use needs memory; preserve lean static TFT memory image.
  if(!lastText)lastText=(char *)malloc(kTextMax);
  if(!lastText){lastSequence=(uint16_t)seq;haveLast=true;lastOk=false;pendingAck=true;return;}
  // Repeated command after lost ACK gets the *original* result; in
  // particular CPRESET must not run twice.
  if(!haveLast || lastSequence!=(uint16_t)seq) {
    lastSequence=(uint16_t)seq;
    lastOperation=(uint8_t)op;
    lastTransferPhase='E';
    haveLast=true;
    const char *args=command["args"] | "";
    const int txn=command["txn"] | -1;
    if(op<1||op>12||strlen(args)>91)setResult(false,"INVALID CP/M REQUEST");
    else if(op>=9)transferCommand((uint8_t)op,args,(uint16_t)seq,
                                 txn>=0&&txn<=65535?(uint16_t)txn:0);
    else execute((uint8_t)op,args);
  }
  pendingAck=true;
}
inline void appendAck(JsonObject request) {
  transferWatchdog();
  if(!pendingAck)return;
  JsonObject ack=request["ack"].to<JsonObject>();
  ack["seq"]=lastSequence;
  ack["ok"]=lastOk;
  ack["text"]=lastText?lastText:"CPM85 MEMORY LOW";
  if(lastOperation>=9) {
    ack["phase"]=String(lastTransferPhase);
    if(transfer)ack["txn"]=transfer->id;
  }
}
inline void ackPollCompleted() {pendingAck=false;}
} // namespace Cpm85DriveControl
