#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "ebtks_cpm85_keymap.h"

// CPM85 developer-only keyboard bridge.  Never shares a hardware UART with
// Z80 console data; all Teensy traffic uses the CRC-framed ebtksLinkTransaction
// under the existing disk-service mutex.
//
// IMPORTANT: HP keyboard bytes are received from a second core. They enter
// a dedicated FreeRTOS queue, drained by the existing console router thread.
#ifndef EBTKS_CPM85_KEYBOARD_ENABLE
#define EBTKS_CPM85_KEYBOARD_ENABLE 0
#endif

namespace Cpm85Input {
#if EBTKS_CPM85_KEYBOARD_ENABLE
static QueueHandle_t keyQueue=nullptr;
static TaskHandle_t taskHandle=nullptr;
static volatile bool active=false;
static volatile uint32_t badReplies=0, queued=0, dropped=0;
static constexpr uint8_t kMaxBatch=24;
static constexpr int kQueueSize=128;

inline bool emit(uint8_t value) {
  if(!keyQueue || xQueueSend(keyQueue,&value,0)!=pdTRUE) {
    ++dropped;
    return false;
  }
  ++queued;
  return true;
}
inline void translate(uint8_t hp) {
  // Translate physical HP KEYHIT into one CP/M input byte. BACK SPACE,
  // END LINE and four cursor arrows (WordStar-style ^E/^X/^S/^D) are
  // recognized. CTRL+key codes 0..31 (including NUL) pass unchanged;
  // K8 stays HP-local and other HP function keys remain unmapped.
  const int mapped=Cpm85Keymap::toConsoleByte(hp);
  if(mapped>=0)emit(static_cast<uint8_t>(mapped));
}
inline void worker(void*) {
  uint32_t missing=0;
  for(;;) {
    String reply;
    const bool ok=ebtksLinkTransaction("{\"cpm_keys\":{\"max\":24}}",reply,200);
    if(ok) {
      JsonDocument doc;
      auto err=deserializeJson(doc,reply);
      JsonVariantConst item=doc["cpm_keys"];
      if(!err && !item.isNull()) {
        missing=0;
        const bool nowActive=item["active"] | false;
        active=nowActive;
        // Process pending data even after K8 detached the HP keyboard:
        // detaching must not discard previously typed CP/M characters.
        JsonArrayConst arr=item["keys"].as<JsonArrayConst>();
        for(JsonVariantConst v:arr) {
          int code=v.as<int>();
          if(code>=0 && code<=255 && code!=0207)translate((uint8_t)code);
        }
      } else {
        ++badReplies;
        ++missing;
      }
    } else {
      ++badReplies;
      ++missing;
    }
    // Poll frequently only when the physical HP keyboard is attached;
    // no stale console characters cross a detach/reattach boundary.
    const uint32_t waitMs=active?35:(missing>3?1000:250);
    vTaskDelay(pdMS_TO_TICKS(waitMs));
  }
}
inline void start() {
  if(taskHandle)return;
  if(!keyQueue)keyQueue=xQueueCreate(kQueueSize,sizeof(uint8_t));
  if(!keyQueue) {Serial.println("CPM85: keyboard queue allocation failed");return;}
  BaseType_t result=xTaskCreatePinnedToCore(worker,"CPM85Keys",3584,nullptr,1,&taskHandle,0);
  if(result!=pdPASS){taskHandle=nullptr;Serial.println("CPM85: worker startup failed");}
}
inline void drain() {
  if(!keyQueue)return;
  uint8_t c;
  int n=0;
  while(n<32 && xQueueReceive(keyQueue,&c,0)==pdTRUE) {
    consoleRxEnqueue(c);
    ++n;
  }
}
#else
inline void start(){}
inline void drain(){}
#endif
} // namespace Cpm85Input
