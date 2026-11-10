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
  uint8_t sourceIssue=CpmSafePoint::SRC_NONE;
};
inline bool exchange(JsonDocument &cmd,JsonDocument &reply) {
  return ebtksStorageExchange(cmd,reply,5000);
}
inline uint8_t reasonCode(const char *reason) {
  if(!reason)return CpmSafePoint::SRC_BAD_REPLY;
  if(!strcmp(reason,"import_in_progress"))return CpmSafePoint::SRC_IMPORT_ACTIVE;
  if(!strcmp(reason,"source_not_found"))return CpmSafePoint::SRC_NOT_FOUND;
  if(!strcmp(reason,"source_must_be_1_to_63_records"))return CpmSafePoint::SRC_BAD_SIZE;
  if(!strcmp(reason,"source_read_or_seek_failed"))return CpmSafePoint::SRC_READ_SEEK;
  if(!strcmp(reason,"invalid_source_name") ||
     !strcmp(reason,"invalid_source_path"))return CpmSafePoint::SRC_BAD_NAME;
  if(!strcmp(reason,"export_in_progress"))return CpmSafePoint::SRC_EXPORT_ACTIVE;
  if(!strcmp(reason,"active_source_mismatch"))return CpmSafePoint::SRC_ACTIVE_NAME_MISMATCH;
  if(!strcmp(reason,"no_active_import"))return CpmSafePoint::SRC_NO_ACTIVE;
  return CpmSafePoint::SRC_OTHER;
}
inline bool begin(const char *source,Session &session) {
  JsonDocument cmd,reply;
  cmd["cpm_import"]["op"]="begin";
  cmd["cpm_import"]["source"]=source;
  if(!exchange(cmd,reply)) {
    session.sourceIssue=CpmSafePoint::SRC_UART_FAILED;
    return false;
  }
  JsonVariantConst r=reply["cpm_import"].as<JsonVariantConst>();
  if(!(r["ok"] | false)) {
    session.sourceIssue=reasonCode(r["reason"] | "");
    return false;
  }
  const int count=r["records"] | -1;
  const int crc=r["crc16_ccitt_false"] | -1;
  const uint32_t token=r["session"] | (uint32_t)0;
  if(token==0 || count<1 || count>63 || crc<0 || crc>65535 ||
     strcmp(r["state"] | "","active")!=0) {
    session.sourceIssue=CpmSafePoint::SRC_BAD_REPLY;
    return false;
  }
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
  if(!exchange(cmd,reply)) {
    session->sourceIssue=CpmSafePoint::SRC_UART_FAILED;return false;
  }
  if(!(reply["cpm_import"]["ok"] | false)) {
    session->sourceIssue=reasonCode(reply["cpm_import"]["reason"] | "");
    return false;
  }
  JsonVariantConst r=reply["cpm_import"].as<JsonVariantConst>();
  const char *encoded=r["data"] | "";
  const int crc=r["record_crc16"] | -1;
  if((r["session"] | (uint32_t)0)!=session->token ||
     (r["record"] | -1)!=(int)index ||
     (r["length"] | -1)!=128 || crc<0 || crc>65535 ||
     strlen(encoded)!=172) {
    session->sourceIssue=CpmSafePoint::SRC_BAD_REPLY;return false;
  }
  size_t decoded=0;
  if(mbedtls_base64_decode(bytes,128,&decoded,
           (const unsigned char*)encoded,strlen(encoded))!=0 ||
     decoded!=128) {
    session->sourceIssue=CpmSafePoint::SRC_BAD_REPLY;return false;
  }
  recordCrc=(uint16_t)crc;
  if(CpmSafePoint::fileRecordCrc(bytes,128)!=recordCrc) {
    session->sourceIssue=CpmSafePoint::SRC_BAD_REPLY;return false;
  }
  return true;
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
// A clean, read-only session close. Preflight performs no BDOS operations.
inline bool release(uint32_t token) {
  JsonDocument cmd,reply;
  cmd["cpm_import"]["op"]="release";
  cmd["cpm_import"]["session"]=token;
  return exchange(cmd,reply) && (reply["cpm_import"]["ok"] | false) &&
         (reply["cpm_import"]["session"] | (uint32_t)0)==token &&
         strcmp(reply["cpm_import"]["state"] | "","done")==0;
}
// Recover only a read-only session for the explicitly named source.
// The ESP32 never issues recovery automatically during a real import.
inline uint8_t recover(const char *source) {
  JsonDocument cmd,reply;
  cmd["cpm_import"]["op"]="recover";
  cmd["cpm_import"]["source"]=source;
  if(!exchange(cmd,reply))return CpmSafePoint::SRC_UART_FAILED;
  if(!(reply["cpm_import"]["ok"] | false))
    return reasonCode(reply["cpm_import"]["reason"] | "");
  return CpmSafePoint::SRC_NONE;
}
inline bool preflight(CpmSafePoint::Gate &gate) {
  uint8_t fcb[11];
  for(unsigned i=0;i<11;++i)
    fcb[i]=gate.requestedSourceFile[i].load(std::memory_order_relaxed);
  const uint8_t drive=gate.requestedSourceDrive.load(std::memory_order_acquire);
  char source[16]{};
  if(!CpmSdExport::sourceName(drive,fcb,source)) {
    gate.fileError.store(CpmSafePoint::FILE_BAD_REQUEST,std::memory_order_release);
    return false;
  }
  if(gate.importRecoverRequested.load(std::memory_order_acquire)) {
    const uint8_t err=recover(source);
    if(err!=CpmSafePoint::SRC_NONE) {
      gate.importSourceIssue.store(err,std::memory_order_release);
      gate.fileError.store(CpmSafePoint::FILE_SD_SOURCE_FAILED,std::memory_order_release);
      return false;
    }
  }
  Session session;
  if(!begin(source,session)) {
    gate.importSourceIssue.store(session.sourceIssue,std::memory_order_release);
    gate.fileError.store(CpmSafePoint::FILE_SD_SOURCE_FAILED,std::memory_order_release);
    return false;
  }
  gate.importTotalRecords.store(session.records,std::memory_order_release);
  gate.importExpectedCrc.store(session.expectedCrc,std::memory_order_release);
  if(!release(session.token)) {
    abort(session.token);
    gate.importSourceIssue.store(CpmSafePoint::SRC_RELEASE_FAILED,
                                 std::memory_order_release);
    gate.fileError.store(CpmSafePoint::FILE_SD_SOURCE_FAILED,std::memory_order_release);
    return false;
  }
  return true;
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
    gate.importSourceIssue.store(session.sourceIssue,std::memory_order_release);
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
  gate.importSourceIssue.store(session.sourceIssue,std::memory_order_release);
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
    gate.importSourceIssue.store(CpmSafePoint::SRC_RELEASE_FAILED,
                                 std::memory_order_release);
    abort(session.token);
    return false;
  }
  return true;
}
} // namespace CpmSdImport
