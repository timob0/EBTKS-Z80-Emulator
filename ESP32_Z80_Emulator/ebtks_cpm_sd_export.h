#pragma once
#include <stdint.h>
#include <string.h>
#include "cpm_file_read_service.h"
#include "ebtks_storage.h"

// First end-to-end B2b checkpoint: reuse hardware-accepted B2a's exact
// read-only BDOS record service. Run all BDOS work only on the Z80 CPU task,
// after the existing verified native CCP D382h empty-return checkpoint.
// One checkpoint per COPY (not per record); bounded to <=63 records plus EOF.
// This implementation deliberately prioritizes safety over O(n^2) rescanning;
// persistent-open multi-record acceleration is a later separate checkpoint.
namespace CpmSdExport {
inline uint16_t crcAppend(uint16_t seed,const uint8_t *data,size_t n) {
  for(size_t i=0;i<n;++i) {
    seed ^= uint16_t(data[i])<<8;
    for(unsigned b=0;b<8;++b)
      seed=(seed&0x8000)?uint16_t((seed<<1)^0x1021):uint16_t(seed<<1);
  }
  return seed;
}
inline bool sourceName(uint8_t drive,const uint8_t name[11],char (&out)[16]) {
  if(drive<1 || drive>16 || !name)return false;
  unsigned p=0;
  out[p++]=char('A'+drive-1);out[p++]=':';
  bool any=false;
  for(unsigned i=0;i<8;++i)if(name[i]!=' ') {
    out[p++]=char(name[i]);any=true;
  }
  if(!any)return false;
  bool hasExt=false;
  for(unsigned i=8;i<11;++i)if(name[i]!=' ')hasExt=true;
  if(hasExt) {
    out[p++]='.';
    for(unsigned i=8;i<11;++i)if(name[i]!=' ')out[p++]=char(name[i]);
  }
  out[p]=0;
  return true;
}
inline bool rpc(JsonDocument &command,JsonDocument &response) {
  if(!ebtksStorageExchange(command,response,5000))return false;
  return response["cpm_export"]["ok"] | false;
}
inline bool begin(const char *name,uint32_t &session) {
  JsonDocument cmd,answer;
  cmd["cpm_export"]["op"]="begin";
  cmd["cpm_export"]["name"]=name;
  if(!rpc(cmd,answer))return false;
  session=answer["cpm_export"]["session"] | (uint32_t)0;
  return session!=0 &&
    strcmp(answer["cpm_export"]["state"] | "", "active")==0;
}
inline bool append(uint32_t session,uint16_t record,const uint8_t *bytes,
                   uint16_t recordCrc,uint16_t cumulativeCrc) {
  char encoded[4*((128+2)/3)+1];
  size_t size=0;
  if(mbedtls_base64_encode((unsigned char*)encoded,sizeof(encoded),
                           &size,bytes,128)!=0)return false;
  encoded[size]=0;
  JsonDocument cmd,answer;
  JsonObject v=cmd["cpm_export"].to<JsonObject>();
  v["op"]="write";v["session"]=session;v["record"]=record;
  v["crc16_ccitt_false"]=recordCrc;v["data"]=encoded;
  if(!rpc(cmd,answer))return false;
  JsonVariantConst reply=answer["cpm_export"].as<JsonVariantConst>();
  return (reply["session"] | (uint32_t)0)==session &&
         (reply["records"] | -1)==int(record+1) &&
         (reply["crc16_ccitt_false"] | -1)==int(cumulativeCrc);
}
inline bool queryCommitted(uint32_t session,uint16_t records,uint16_t crc) {
  JsonDocument cmd,answer;
  cmd["cpm_export"]["op"]="status";
  if(!rpc(cmd,answer))return false;
  JsonVariantConst reply=answer["cpm_export"].as<JsonVariantConst>();
  return strcmp(reply["state"] | "","done")==0 &&
    (reply["session"] | (uint32_t)0)==session &&
    (reply["records"] | -1)==int(records) &&
    (reply["crc16_ccitt_false"] | -1)==int(crc);
}
inline bool finish(uint32_t session,uint16_t records,uint16_t crc) {
  JsonDocument cmd,answer;
  JsonObject v=cmd["cpm_export"].to<JsonObject>();
  v["op"]="finish";v["session"]=session;v["records"]=records;
  v["crc16_ccitt_false"]=crc;
  if(rpc(cmd,answer))return queryCommitted(session,records,crc);
  // The Teensy may have committed but its UART acknowledgement was lost.
  // Read-only status resolves this without retrying the rename.
  return queryCommitted(session,records,crc);
}
inline void abort(uint32_t session) {
  JsonDocument cmd,answer;
  cmd["cpm_export"]["op"]="abort";
  cmd["cpm_export"]["session"]=session;
  rpc(cmd,answer);
}

// The caller owns the safe-point gate and publishes the completed result.
// Do not call from an AsyncTCP callback, console task or HP-bus ISR.
inline bool execute(Z80Bridge *bridge,CpmSafePoint::Gate &gate,bool &needReset,
                    void (*yieldCpu)()=nullptr) {
  needReset=false;
  uint8_t filename[11];
  for(unsigned i=0;i<11;++i)
    filename[i]=gate.requestedFile[i].load(std::memory_order_relaxed);
  const uint8_t drive=gate.requestedDrive.load(std::memory_order_acquire);
  char name[16]={};
  if(!sourceName(drive,filename,name)) {
    gate.fileError.store(CpmSafePoint::FILE_BAD_REQUEST,std::memory_order_release);
    return false;
  }
  uint32_t session=0;
  // No BDOS call or guest modification if a matching Teensy is unavailable
  // or SD cannot create an exclusive staging file.
  if(!begin(name,session)) {
    gate.fileError.store(CpmSafePoint::FILE_SD_STAGE_FAILED,std::memory_order_release);
    return false;
  }
  uint16_t count=0,fullCrc=0xFFFF;
  bool finished=false,ok=true;
  for(uint16_t index=0;index<=CpmFileReadService::kMaxRecord;++index) {
    CpmSafePoint::FileRecord record;
    bool recordReset=false;
    if(!CpmFileReadService::execute(bridge,filename,drive,index,
                                    record,recordReset,yieldCpu)) {
      gate.fileError.store(record.error,std::memory_order_release);
      needReset=recordReset;
      ok=false;break;
    }
    if(record.eof) {finished=true;break;}
    const uint16_t nextCrc=crcAppend(fullCrc,record.data,128);
    if(!record.hasData ||
       !append(session,index,record.data,record.crc16,nextCrc)) {
      gate.fileError.store(CpmSafePoint::FILE_SD_WRITE_FAILED,
                           std::memory_order_release);
      ok=false;break;
    }
    ++count;fullCrc=nextCrc;
    gate.exportRecords.store(count,std::memory_order_release);
    gate.exportCrc.store(fullCrc,std::memory_order_release);
    if(yieldCpu)yieldCpu();
  }
  if(ok && !finished) {
    ok=false;
    gate.fileError.store(CpmSafePoint::FILE_EXPORT_LIMIT,
                         std::memory_order_release);
  }
  if(ok && !finish(session,count,fullCrc)) {
    ok=false;
    gate.fileError.store(CpmSafePoint::FILE_SD_COMMIT_FAILED,
                         std::memory_order_release);
  }
  if(!ok)abort(session); // Only cleans up a stage owned by this session.
  return ok;
}
} // namespace CpmSdExport
