#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Hardware diagnostics for the classic ESP32's heavily fragmented DRAM.
// Each sample is taken OUTSIDE interrupt/port critical sections; only the
// compact copy is locked. At most one sample per named lifecycle stage;
// no heap allocation or socket I/O in the sampling function itself.
namespace EbtksMemory {
enum Stage : uint8_t {
  WEB_READY, STORAGE_READY, CPU_CONSOLES_READY, KEYBOARD_READY,
  SPI_READY, TFT_READY, MODEM_READY, MODEM_DIAL_BEFORE,
  MODEM_DIAL_AFTER, MODEM_HUNG_UP, COUNT
};
struct Sample {
  uint32_t freeTotal, free8Bit, largest8Bit, internalFree, internalLargest;
  bool valid;
};
static Sample samples[COUNT]={};
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
inline const char* name(Stage s){
  switch(s){
    case WEB_READY:return "web";
    case STORAGE_READY:return "teensy_storage";
    case CPU_CONSOLES_READY:return "cpu_console_telnet";
    case KEYBOARD_READY:return "keyboard";
    case SPI_READY:return "spi_disk_cache";
    case TFT_READY:return "tft";
    case MODEM_READY:return "modem_task";
    case MODEM_DIAL_BEFORE:return "before_tcp_connect";
    case MODEM_DIAL_AFTER:return "after_tcp_connect";
    case MODEM_HUNG_UP:return "tcp_closed";
    default:return "unknown";
  }
}
inline void capture(Stage stage){
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
inline void stack(JsonObject& tasks,const char* label,TaskHandle_t h){
  if(h)tasks[label]=(uint32_t)uxTaskGetStackHighWaterMark(h);
}
} // namespace EbtksMemory
