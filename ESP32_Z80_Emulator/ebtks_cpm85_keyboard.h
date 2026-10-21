#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include "ebtks_cpm85_keymap.h"

// CPM85 HP-85 physical keyboard bridge.  Never shares a hardware UART with
// Z80 console data; all Teensy traffic uses the CRC-framed ebtksLinkTransaction
// under the existing disk-service mutex.
//
// IMPORTANT: HP keyboard bytes are received from a second core. They enter
// a dedicated FreeRTOS queue, drained by the existing console router thread.
#ifndef EBTKS_CPM85_KEYBOARD_ENABLE
#define EBTKS_CPM85_KEYBOARD_ENABLE 0
#endif

namespace Cpm85DriveControl {
  void receiveCommand(JsonVariantConst command);
  void appendAck(JsonObject request);
  void ackPollCompleted();
}
namespace Cpm85Input {
#if EBTKS_CPM85_KEYBOARD_ENABLE
static QueueHandle_t keyQueue=nullptr;
static TaskHandle_t taskHandle=nullptr;
static volatile bool active=false;
static volatile uint32_t badReplies=0, queued=0, dropped=0;
static constexpr uint8_t kMaxBatch=24;
static constexpr int kQueueSize=128;
// Only the serial router writes the tracker; the keyboard worker only reads
// its published mode. This works for both headless and TFT terminal targets.
static Cpm85Keymap::CursorModeTracker cursorTracker;
static std::atomic<bool> applicationCursor(false);

inline void observeConsoleOutput(const uint8_t* bytes,size_t n) {
  if(!bytes || !n)return;
  cursorTracker.feed(bytes,n);
  applicationCursor.store(cursorTracker.applicationCursor(),std::memory_order_relaxed);
}

// A single producer (this worker) and one consumer (the serial router).
// Reserve the whole input sequence BEFORE writing its first byte. If the
// queue fills, discard the entire key instead of leaving a partial ESC [.
inline bool emitSequence(const Cpm85Keymap::InputBytes& seq) {
  if(seq.len==0)return true; // Unmapped HP system keys are intentionally ignored.
  if(!keyQueue || uxQueueSpacesAvailable(keyQueue)<seq.len) {
    ++dropped;
    return false;
  }
  for(uint8_t i=0;i<seq.len;++i) {
    if(xQueueSend(keyQueue,&seq.bytes[i],0)!=pdTRUE) {
      // Should not occur: no second queue producer can consume reserved space.
      ++dropped;
      return false;
    }
  }
  queued+=seq.len;
  return true;
}
inline void translate(uint8_t hp) {
  // CTRL, Backspace and END LINE retain the physical-machine-tested bytes.
  // Arrows use normal VT100 CSI or application SS3 mode depending on host
  // DECCKM commands; editing/function keys use conventional ANSI/xterm input.
  const auto seq=Cpm85Keymap::toInputBytes(
      hp,applicationCursor.load(std::memory_order_relaxed));
  emitSequence(seq);
}
inline void worker(void*) {
  uint32_t missing=0;
  for(;;) {
    String reply;
    JsonDocument requestDoc;
    JsonObject poll=requestDoc["cpm_keys"].to<JsonObject>();
    poll["max"]=24;
    Cpm85DriveControl::appendAck(poll);
    String request;
    serializeJson(requestDoc,request);
    const bool ok=ebtksLinkTransaction(request,reply,350);
    if(ok) {
      JsonDocument doc;
      auto err=deserializeJson(doc,reply);
      JsonVariantConst item=doc["cpm_keys"];
      if(!err && !item.isNull()) {
        missing=0;
        Cpm85DriveControl::ackPollCompleted();
        Cpm85DriveControl::receiveCommand(item["command"]);
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
  while(n<32) {
    // Preserve pending bytes (especially ESC sequences) if the final Z80
    // UART ring is full. The next serialTask cycle will resume draining.
    uint16_t next=rxInPtr+1;
    if(next>=sizeof(rxBuf))next=0;
    if(next==rxOutPtr)break;
    if(xQueueReceive(keyQueue,&c,0)!=pdTRUE)break;
    consoleRxEnqueue(c);
    ++n;
  }
}
#else
inline void start(){}
inline void drain(){}
inline void observeConsoleOutput(const uint8_t*,size_t){}
#endif
} // namespace Cpm85Input
