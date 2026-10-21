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
  for(uint8_t i=0;i<CPM_DRIVE_COUNT && used<kTextMax-85;++i) {
    const CpmMountEntry &a=cpmActiveMounts[i];
    const CpmMountEntry &d=cpmDesiredMounts[i];
    if(!a.mounted && !d.mounted)continue;
    const char *active=a.mounted?basename(a.path):"-";
    const char *desired=d.mounted?basename(d.path):"-";
    const bool changed=!cpmMountEntryEqual(a,d);
    int n=snprintf(lastText+used,kTextMax-used,
                   "%c: %.21s%s",char('A'+i),active,changed?" -> ":"\n");
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

inline void execute(uint8_t op,const char *args) {
  char msg[112]={};
  uint8_t drive=0;
  if(op==1){status();return;}
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
    if(op<1||op>4||strlen(args)>91)setResult(false,"INVALID CP/M REQUEST");
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
