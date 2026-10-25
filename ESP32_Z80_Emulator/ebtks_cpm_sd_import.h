#pragma once
#include <stdint.h>
#include <string.h>
#include "cpm_file_write_service.h"
#include "ebtks_storage.h"
#include "ebtks_cpm_sd_export.h"

// B2c: Teensy SD (published transfers ONLY) -> new live CP/M filename.
// The fixed 128-byte buffer and strict 63-record bound are deliberate.
// BDOS calls run solely from the pinned CPU worker at native CCP D382h.
namespace CpmSdImport {
struct Session {
  uint32_t token=0;
  uint16_t records=0;
  uint16_t expectedCrc=0xFFFF;
};
inline bool exchange(JsonDocument &cmd,JsonDocument &reply) {
  return ebtksStorageExchange(cmd,reply,5000);
}
inline bool begin(const char *source,Session &session) {
  JsonDocument cmd,reply;
  cmd["cpm_import"]["op"]="begin";
  cmd["cpm_import"]["source"]=source;
  if(!exchange(cmd,reply) || !(reply["cpm_import"]["ok"] | false))
    return false;
  JsonVariantConst r=reply["cpm_import"].as<JsonVariantConst>();
  const int count=r["records"] | -1;
  const int crc=r["crc16_ccitt_false"] | -1;
  const uint32_t token=r["session"] | (uint32_t)0;
  if(token==0 || count<1 || count>63 || crc<0 || crc>65535 ||
     strcmp(r["state"] | "","active")!=0)return false;
  session.token=token;
  session.records=(uint16_t)count;
  session.expectedCrc=(uint16_t)crc;
  return true;
}
inline bool read(void *context,uint16_t index,uint8_t bytes[128],
                 uint16_t &recordCrc) {
  Session *session=static_cast<Session*>(context);
  if(!session || !session->token || index>=session->records)return false;
  JsonDocument cmd,reply;
  JsonObject v=cmd["cpm_import"].to<JsonObject>();
  v["op"]="read";v["session"]=session->token;v["record"]=index;
  if(!exchange(cmd,reply) || !(reply["cpm_import"]["ok"] | false))
    return false;
  JsonVariantConst r=reply["cpm_import"].as<JsonVariantConst>();
  const char *encoded=r["data"] | "";
  const int crc=r["record_crc16"] | -1;
  if((r["session"] | (uint32_t)0)!=session->token ||
     (r["record"] | -1)!=(int)index ||
     (r["length"] | -1)!=128 || crc<0 || crc>65535 ||
     strlen(encoded)!=172)return false;
  size_t decoded=0;
  if(mbedtls_base64_decode(bytes,128,&decoded,
           (const unsigned char*)encoded,strlen(encoded))!=0 ||
     decoded!=128)return false;
  recordCrc=(uint16_t)crc;
  return CpmSafePoint::fileRecordCrc(bytes,128)==recordCrc;
}
inline bool finish(uint32_t token) {
  JsonDocument cmd,reply;
  cmd["cpm_import"]["op"]="finish";
  cmd["cpm_import"]["session"]=token;
  if(!exchange(cmd,reply) || !(reply["cpm_import"]["ok"] | false))
    return false;
  return (reply["cpm_import"]["session"] | (uint32_t)0)==token &&
         strcmp(reply["cpm_import"]["state"] | "","done")==0;
}
inline void abort(uint32_t token) {
  JsonDocument cmd,reply;
  cmd["cpm_import"]["op"]="abort";
  cmd["cpm_import"]["session"]=token;
  exchange(cmd,reply);
}
inline bool execute(Z80Bridge *bridge,CpmSafePoint::Gate &gate,
                    bool &needReset,void (*yieldCpu)()=nullptr) {
  needReset=false;
  uint8_t sourceFcb[11],destFcb[11];
  for(unsigned i=0;i<11;++i) {
    sourceFcb[i]=gate.requestedSourceFile[i].load(std::memory_order_relaxed);
    destFcb[i]=gate.requestedFile[i].load(std::memory_order_relaxed);
  }
  const uint8_t sourceDrive=gate.requestedSourceDrive.load(std::memory_order_acquire);
  const uint8_t destDrive=gate.requestedDrive.load(std::memory_order_acquire);
  char sourceName[16]{};
  if(!CpmSdExport::sourceName(sourceDrive,sourceFcb,sourceName)) {
    gate.fileError.store(CpmSafePoint::FILE_BAD_REQUEST,std::memory_order_release);
    return false;
  }
  Session session;
  // Ensure the complete, valid 128-byte-aligned SD source was scanned
  // before calling even a single mutating native CP/M BDOS function.
  if(!begin(sourceName,session)) {
    gate.fileError.store(CpmSafePoint::FILE_SD_SOURCE_FAILED,
                         std::memory_order_release);
    return false;
  }
  gate.importTotalRecords.store(session.records,std::memory_order_release);
  gate.importExpectedCrc.store(session.expectedCrc,std::memory_order_release);

  CpmFileWriteService::Outcome outcome;
  const bool wrote=CpmFileWriteService::execute(
     bridge,destFcb,destDrive,session.records,session.expectedCrc,
     &read,&session,outcome,needReset,yieldCpu,&gate);
  gate.importPartialPossible.store(outcome.partialPossible,
                                   std::memory_order_release);
  gate.fileError.store(outcome.error,std::memory_order_release);
  if(!wrote) {
    abort(session.token);
    return false;
  }
  if(!finish(session.token)) {
    // CP/M readback succeeded; do NOT reissue MAKE or retry writes.
    // Any ambiguity here is limited to closing the read-only SD session.
    gate.fileError.store(CpmSafePoint::FILE_SD_SOURCE_FAILED,
                         std::memory_order_release);
    abort(session.token);
    return false;
  }
  return true;
}
} // namespace CpmSdImport
