#pragma once
#include <Arduino.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

// B2c diagnostic only. No filesystem, UART, heap, or extra task.
// RTC_NOINIT may survive a software/watchdog/reset-pin reboot but NOT
// a true loss of power. A missing previous snapshot is not evidence
// that the import never started. Only the pinned Z80 task writes stages.
namespace CpmImportDiag {
enum Stage : uint8_t {
  BOOT=0, SOURCE_RECOVER=1, SOURCE_BEGIN=2, SOURCE_READY=3,
  BDOS_SEARCH=4, BDOS_MAKE=5, SOURCE_READ=6, BDOS_WRITE=7,
  BDOS_CLOSE=8, VERIFY_OPEN=9, VERIFY_READ=10, VERIFY_CLOSE=11,
  SOURCE_RELEASE=12, IMPORT_COMPLETE=13
};
enum Status : uint8_t { IDLE=0, RUNNING=1, PASSED=2, FAILED=3,
                        Z80_RESET_REQUESTED=4 };
static constexpr uint32_t kMagic=0xB2C3D1A9UL;
struct Trace {
  uint32_t magic;
  uint32_t inverse;
  uint32_t stage;
  uint32_t status;
  uint32_t record;
  uint32_t stack_unused_bytes;
  uint32_t uptime_ms;
};
// Not DRAM/BSS: the TFT build has an extremely tight static DRAM limit.
RTC_NOINIT_ATTR static volatile Trace active;
RTC_NOINIT_ATTR static volatile Trace previous;

inline bool valid(const volatile Trace &t) {
  return t.magic==kMagic && t.inverse==~kMagic &&
         t.stage<=IMPORT_COMPLETE && t.status<=Z80_RESET_REQUESTED;
}
inline void onBoot() {
  if(valid(active)) {
    previous.magic=kMagic;previous.inverse=~kMagic;
    previous.stage=active.stage;previous.status=active.status;
    previous.record=active.record;
    previous.stack_unused_bytes=active.stack_unused_bytes;
    previous.uptime_ms=active.uptime_ms;
  } else {
    previous.magic=0;previous.inverse=0;
  }
  active.magic=kMagic;active.inverse=~kMagic;
  active.stage=BOOT;active.status=IDLE;active.record=0;
  active.stack_unused_bytes=0;active.uptime_ms=millis();
}
inline void mark(Stage s,uint32_t record=0) {
  active.stage=(uint32_t)s;active.status=RUNNING;
  active.record=record;
  active.stack_unused_bytes=(uint32_t)uxTaskGetStackHighWaterMark(nullptr);
  active.uptime_ms=millis();
}
inline void finish(Status status) {
  active.status=(uint32_t)status;
  active.stack_unused_bytes=(uint32_t)uxTaskGetStackHighWaterMark(nullptr);
  active.uptime_ms=millis();
}
inline const char *stageText(uint32_t s) {
  switch(s) {
    case BOOT:return "boot";
    case SOURCE_RECOVER:return "sd_source_recover";
    case SOURCE_BEGIN:return "sd_source_begin";
    case SOURCE_READY:return "sd_source_ready";
    case BDOS_SEARCH:return "bdos_search_first";
    case BDOS_MAKE:return "bdos_make";
    case SOURCE_READ:return "sd_source_read";
    case BDOS_WRITE:return "bdos_write_sequential";
    case BDOS_CLOSE:return "bdos_close";
    case VERIFY_OPEN:return "bdos_verify_open";
    case VERIFY_READ:return "bdos_verify_read";
    case VERIFY_CLOSE:return "bdos_verify_close";
    case SOURCE_RELEASE:return "sd_source_release";
    case IMPORT_COMPLETE:return "import_complete";
    default:return "unknown";
  }
}
inline const char *statusText(uint32_t s) {
  switch(s) {
    case IDLE:return "idle";
    case RUNNING:return "running";
    case PASSED:return "passed";
    case FAILED:return "failed";
    case Z80_RESET_REQUESTED:return "z80_reset_requested";
    default:return "unknown";
  }
}
template<class JsonObj> inline void append(JsonObj dest,const volatile Trace &t) {
  const bool available=valid(t);
  dest["valid"]=available;
  if(!available)return;
  dest["stage"]=stageText(t.stage);
  dest["status"]=statusText(t.status);
  dest["record"]=t.record;
  dest["stack_unused_bytes"]=t.stack_unused_bytes;
  dest["uptime_ms"]=t.uptime_ms;
}
} // namespace CpmImportDiag
