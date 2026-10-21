#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Hardware diagnostics for ESP32 byte-accessible versus 32-bit-only RAM.
// Each sample is taken OUTSIDE interrupt/port critical sections; only the
// compact copy is locked. At most one sample per named lifecycle stage;
// no heap allocation or socket I/O in the sampling function itself.
namespace EbtksMemory {
enum Stage : uint8_t {
  ARDUINO_SETUP_ENTRY, WEB_READY, STORAGE_READY, CPU_CONSOLES_READY, KEYBOARD_READY,
  SPI_READY, TFT_READY, CPM_BOOT_READY, MODEM_READY, MODEM_DIAL_BEFORE,
  MODEM_DIAL_AFTER, MODEM_HUNG_UP, COUNT
};
struct Sample {
  uint32_t freeTotal, free8Bit, largest8Bit, internalFree, internalLargest;
  bool valid;
};
// The TFT target has <200 bytes spare static DRAM. Reserve snapshot
// storage once on runtime heap, after the main Web server has started.
// First capture is setup-side WEB_READY before any CP/M modem use.
static Sample* samples=nullptr;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
inline const char* name(Stage s){
  switch(s){
    case ARDUINO_SETUP_ENTRY:return "arduino_setup_entry";
    case WEB_READY:return "web";
    case STORAGE_READY:return "teensy_storage";
    case CPU_CONSOLES_READY:return "cpu_console_telnet";
    case KEYBOARD_READY:return "keyboard";
    case SPI_READY:return "spi_disk_cache";
    case TFT_READY:return "tft";
    case CPM_BOOT_READY:return "cpm_boot_ready";
    case MODEM_READY:return "modem_task";
    case MODEM_DIAL_BEFORE:return "before_tcp_connect";
    case MODEM_DIAL_AFTER:return "after_tcp_connect";
    case MODEM_HUNG_UP:return "tcp_closed";
    default:return "unknown";
  }
}
inline void capture(Stage stage){
  if(!samples){
    // One-time initialization on setup; subsequent worker captures reuse
    // the same fixed table and NEVER allocate again.
    samples=static_cast<Sample*>(calloc(COUNT,sizeof(Sample)));
    if(!samples)return;
  }
  Sample sample={};
  sample.freeTotal=ESP.getFreeHeap();
  sample.free8Bit=heap_caps_get_free_size(MALLOC_CAP_8BIT);
  sample.largest8Bit=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  sample.internalFree=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  sample.internalLargest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  sample.valid=true;
  portENTER_CRITICAL(&mux);
  samples[(uint8_t)stage]=sample;
  portEXIT_CRITICAL(&mux);
}
inline void append(JsonDocument& doc){
  JsonArray a=doc["stages"].to<JsonArray>();
  if(!samples)return;
  for(uint8_t i=0;i<COUNT;++i){
    Sample s;
    portENTER_CRITICAL(&mux);
    s=samples[i];
    portEXIT_CRITICAL(&mux);
    if(!s.valid)continue;
    JsonObject item=a.add<JsonObject>();
    item["stage"]=name((Stage)i);
    item["total"]=s.freeTotal;
    item["free8"]=s.free8Bit;
    item["largest8"]=s.largest8Bit;
    item["internal"]=s.internalFree;
    item["internal_largest"]=s.internalLargest;
  }
}
// Capability sets overlap (for example INTERNAL and 8BIT). Do not add rows.
// Managed-heap totals exclude static allocations, instruction cache and ROM.
inline void appendHeapPools(JsonDocument& doc){
  struct Pool {const char* name; uint32_t caps;};
  const Pool pools[]={
    {"internal",MALLOC_CAP_INTERNAL},
    {"byte_8bit",MALLOC_CAP_8BIT},
    {"internal_byte_8bit",MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT},
    {"word_32bit",MALLOC_CAP_32BIT},
    {"internal_word_32bit",MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT},
    {"executable",MALLOC_CAP_EXEC},
    {"dma",MALLOC_CAP_DMA}
  };
  JsonArray list=doc["pools"].to<JsonArray>();
  for(const Pool& pool:pools){
    multi_heap_info_t info={};
    heap_caps_get_info(&info,pool.caps);
    JsonObject row=list.add<JsonObject>();
    row["name"]=pool.name;
    row["total"]=(uint32_t)heap_caps_get_total_size(pool.caps);
    row["allocated"]=(uint32_t)info.total_allocated_bytes;
    row["free"]=(uint32_t)info.total_free_bytes;
    row["largest"]=(uint32_t)info.largest_free_block;
    row["free_blocks"]=(uint32_t)info.free_blocks;
    row["allocated_blocks"]=(uint32_t)info.allocated_blocks;
    row["min_free"]=(uint32_t)info.minimum_free_bytes;
  }
}
inline void stack(JsonObject& tasks,const char* label,TaskHandle_t h){
  if(h)tasks[label]=(uint32_t)uxTaskGetStackHighWaterMark(h);
}
} // namespace EbtksMemory
