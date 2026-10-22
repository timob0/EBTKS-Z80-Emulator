#pragma once
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <new>
#include <stdint.h>

// Passive Wi-Fi and console-stall diagnostics. Counter storage uses spare
// 32-bit executable/internal heap to avoid the tight TFT static DRAM limit.
// All accesses are aligned 32-bit atomic operations; no byte-wise IRAM stores.
// No additional task, socket, polling loop, or changes to network policies.
namespace EbtksNetDiag {
enum Key : uint8_t {
  WIFI_GOT_IP, WIFI_DISCONNECT, WIFI_LAST_IP_MS, WIFI_LAST_DISCONNECT_MS,
  WIFI_DISCONNECT_REASON, WEB_WAIT_EVENTS, WEB_WAIT_TOTAL_MS, WEB_WAIT_MAX_MS,
  WEB_TIMEOUTS, WEB_WAITING, TELNET_ZERO_WRITES, TELNET_STALL_EVENTS,
  TELNET_STALL_TOTAL_MS, TELNET_STALL_MAX_MS, TELNET_STALLING, KEY_COUNT
};
struct Counters { std::atomic<uint32_t> slot[KEY_COUNT]; };
// One pointer in scarce static byte-addressable DRAM; no static counter bank.
static std::atomic<Counters*> counters{nullptr};

inline uint32_t read(Key k) {
  Counters *c = counters.load(std::memory_order_acquire);
  return c ? c->slot[k].load(std::memory_order_relaxed) : 0;
}
inline void set(Key k, uint32_t value) {
  Counters *c = counters.load(std::memory_order_acquire);
  if (c) c->slot[k].store(value, std::memory_order_relaxed);
}
inline void increment(Key k, uint32_t by = 1) {
  Counters *c = counters.load(std::memory_order_acquire);
  if (c) c->slot[k].fetch_add(by, std::memory_order_relaxed);
}
inline void setMax(Key k, uint32_t value) {
  Counters *c = counters.load(std::memory_order_acquire);
  if (!c) return;
  std::atomic<uint32_t> &target = c->slot[k];
  uint32_t old = target.load(std::memory_order_relaxed);
  while (old < value &&
         !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}

// Callback executes in framework event task. Never allocate, call WiFi,
// log, wait or modify reconnect policy inside this observer.
inline void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    increment(WIFI_GOT_IP);
    set(WIFI_LAST_IP_MS, millis());
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    increment(WIFI_DISCONNECT);
    set(WIFI_LAST_DISCONNECT_MS, millis());
    set(WIFI_DISCONNECT_REASON, info.wifi_sta_disconnected.reason);
  }
}

inline void begin() {
  // Allocate only once before Wi-Fi provisioning, outside static DRAM.
  // ALLOC_CAP_32BIT | EXEC permits spare instruction RAM for 32-bit words.
  void *mem = heap_caps_malloc(sizeof(Counters),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT | MALLOC_CAP_EXEC);
  if (mem) {
    Counters *fresh = new (mem) Counters;
    // Do not memset a 32-bit-only allocation.
    for (unsigned i = 0; i < KEY_COUNT; ++i)
      fresh->slot[i].store(0, std::memory_order_relaxed);
    counters.store(fresh, std::memory_order_release);
  }
  // Existing Wi-Fi manager and auto-connect configuration are unchanged.
  WiFi.onEvent(wifiEvent);
}

inline void websocketWaitBegin() { increment(WEB_WAIT_EVENTS); set(WEB_WAITING, 1); }
inline void websocketWaitEnd(uint32_t elapsedMs, bool timedOut) {
  increment(WEB_WAIT_TOTAL_MS, elapsedMs);
  setMax(WEB_WAIT_MAX_MS, elapsedMs);
  if (timedOut) increment(WEB_TIMEOUTS);
  set(WEB_WAITING, 0);
}
inline void telnetWriteZero(bool firstInStall) {
  increment(TELNET_ZERO_WRITES);
  if (firstInStall) { increment(TELNET_STALL_EVENTS); set(TELNET_STALLING, 1); }
}
inline void telnetStallEnd(uint32_t elapsedMs) {
  increment(TELNET_STALL_TOTAL_MS, elapsedMs);
  setMax(TELNET_STALL_MAX_MS, elapsedMs);
  set(TELNET_STALLING, 0);
}

inline void append(JsonDocument &v) {
  // Only the health API samples RSSI/status: no ongoing Wi-Fi queries.
  const wl_status_t status = WiFi.status();
  const bool connected = status == WL_CONNECTED;
  v["wifi_connected"] = connected;
  v["wifi_status_code"] = (int)status;
  if (connected) v["wifi_rssi_dbm"] = WiFi.RSSI();
  else v["wifi_rssi_dbm"] = nullptr;
  v["network_counters_available"] =
      counters.load(std::memory_order_acquire) != nullptr;
  const uint32_t gotIp = read(WIFI_GOT_IP);
  v["wifi_got_ip_events"] = gotIp;
  v["wifi_reconnects"] = gotIp > 1 ? gotIp - 1 : 0;
  v["wifi_disconnect_events"] = read(WIFI_DISCONNECT);
  v["wifi_last_got_ip_ms"] = read(WIFI_LAST_IP_MS);
  v["wifi_last_disconnect_ms"] = read(WIFI_LAST_DISCONNECT_MS);
  v["wifi_last_disconnect_reason"] = read(WIFI_DISCONNECT_REASON);
  v["websocket_queue_wait_events"] = read(WEB_WAIT_EVENTS);
  v["websocket_queue_waiting"] = read(WEB_WAITING) != 0;
  v["websocket_queue_wait_total_ms"] = read(WEB_WAIT_TOTAL_MS);
  v["websocket_queue_wait_max_ms"] = read(WEB_WAIT_MAX_MS);
  v["websocket_timeout_disconnects"] = read(WEB_TIMEOUTS);
  v["telnet_write_zero_events"] = read(TELNET_ZERO_WRITES);
  v["telnet_write_stall_events"] = read(TELNET_STALL_EVENTS);
  v["telnet_write_stalling"] = read(TELNET_STALLING) != 0;
  v["telnet_write_stall_total_ms"] = read(TELNET_STALL_TOTAL_MS);
  v["telnet_write_stall_max_ms"] = read(TELNET_STALL_MAX_MS);
}
} // namespace EbtksNetDiag
