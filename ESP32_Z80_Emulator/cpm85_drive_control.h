#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// HP/CPM management over the already-running cpm_keys poll. Only this
// control worker touches the mailbox; the established UART mutex serializes
// its SD/NVS transactions against CP/M disk I/O. Never send unsolicited UART.
namespace Cpm85DriveControl {
static bool pendingAck=false, haveLast=false;
static uint16_t lastSequence=0;
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

inline void execute(uint8_t op,const char *args) {
  char msg[112]={};
  uint8_t drive=0;
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
    haveLast=true;
    const char *args=command["args"] | "";
    // Match Teensy mailbox opcodes 1..6 (5=CPDIMGS, 6=CPDMK).
    if(op<1||op>6||strlen(args)>91)setResult(false,"INVALID CP/M REQUEST");
    else execute((uint8_t)op,args);
  }
  pendingAck=true;
}
inline void appendAck(JsonObject request) {
  if(!pendingAck)return;
  JsonObject ack=request["ack"].to<JsonObject>();
  ack["seq"]=lastSequence;
  ack["ok"]=lastOk;
  ack["text"]=lastText?lastText:"CPM85 MEMORY LOW";
}
inline void ackPollCompleted() {pendingAck=false;}
} // namespace Cpm85DriveControl
