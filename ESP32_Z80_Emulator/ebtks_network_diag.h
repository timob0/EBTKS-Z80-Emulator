#pragma once
#include <WiFi.h>
#include <atomic>
#include <stdint.h>

// Read-only diagnostics for Wi-Fi instability and CP/M console TCP stalls.
// No network operations, buffers, polling tasks, logs or policy changes.
// Counters last until ESP32 reset and may wrap after ~2^32 increments.
namespace EbtksNetDiag {
static std::atomic<uint32_t> wifiGotIpEvents{0};
static std::atomic<uint32_t> wifiDisconnectEvents{0};
static std::atomic<uint32_t> wifiLastGotIpMs{0};
static std::atomic<uint32_t> wifiLastDisconnectMs{0};
static std::atomic<uint32_t> wifiLastDisconnectReason{0};

static std::atomic<uint32_t> websocketWaitEvents{0};
static std::atomic<uint32_t> websocketWaitTotalMs{0};
static std::atomic<uint32_t> websocketWaitMaxMs{0};
static std::atomic<uint32_t> websocketTimeouts{0};
static std::atomic<bool> websocketWaiting{false};

static std::atomic<uint32_t> telnetZeroWrites{0};
static std::atomic<uint32_t> telnetStallEvents{0};
static std::atomic<uint32_t> telnetStallTotalMs{0};
static std::atomic<uint32_t> telnetStallMaxMs{0};
static std::atomic<bool> telnetStalling{false};

inline void maxDuration(std::atomic<uint32_t> &target, uint32_t elapsed) {
  uint32_t old = target.load(std::memory_order_relaxed);
  while (old < elapsed &&
         !target.compare_exchange_weak(old, elapsed, std::memory_order_relaxed)) {}
}

// Registered before WiFi.mode()/autoConnect(). ESP32 invokes this from its
// Wi-Fi event task: never call WiFi, allocate, print or block inside callback.
inline void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    wifiGotIpEvents.fetch_add(1, std::memory_order_relaxed);
    wifiLastGotIpMs.store(millis(), std::memory_order_relaxed);
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    wifiDisconnectEvents.fetch_add(1, std::memory_order_relaxed);
    wifiLastDisconnectMs.store(millis(), std::memory_order_relaxed);
    wifiLastDisconnectReason.store(info.wifi_sta_disconnected.reason,
                                   std::memory_order_relaxed);
  }
}

inline void begin() {
  // Passive observer only. Do not change Wi-Fi credentials, auto-reconnect,
  // modem settings, network timeouts or Web/Telnet buffer policies.
  WiFi.onEvent(wifiEvent);
}

inline void websocketWaitBegin() {
  websocketWaitEvents.fetch_add(1, std::memory_order_relaxed);
  websocketWaiting.store(true, std::memory_order_relaxed);
}
inline void websocketWaitEnd(uint32_t elapsedMs, bool timedOut) {
  websocketWaitTotalMs.fetch_add(elapsedMs, std::memory_order_relaxed);
  maxDuration(websocketWaitMaxMs, elapsedMs);
  if (timedOut) websocketTimeouts.fetch_add(1, std::memory_order_relaxed);
  websocketWaiting.store(false, std::memory_order_relaxed);
}

inline void telnetWriteZero(bool firstInStall) {
  telnetZeroWrites.fetch_add(1, std::memory_order_relaxed);
  if (firstInStall) {
    telnetStallEvents.fetch_add(1, std::memory_order_relaxed);
    telnetStalling.store(true, std::memory_order_relaxed);
  }
}
inline void telnetStallEnd(uint32_t elapsedMs) {
  telnetStallTotalMs.fetch_add(elapsedMs, std::memory_order_relaxed);
  maxDuration(telnetStallMaxMs, elapsedMs);
  telnetStalling.store(false, std::memory_order_relaxed);
}

inline void append(JsonDocument &v) {
  // WiFi.status()/RSSI() are queried only on explicit HTTP health requests.
  const wl_status_t status = WiFi.status();
  const bool connected = status == WL_CONNECTED;
  v["wifi_connected"] = connected;
  v["wifi_status_code"] = (int)status;
  if (connected) v["wifi_rssi_dbm"] = WiFi.RSSI();
  else v["wifi_rssi_dbm"] = nullptr;

  const uint32_t gotIp = wifiGotIpEvents.load(std::memory_order_relaxed);
  v["wifi_got_ip_events"] = gotIp;
  v["wifi_reconnects"] = gotIp > 1 ? gotIp - 1 : 0;
  v["wifi_disconnect_events"] = wifiDisconnectEvents.load(std::memory_order_relaxed);
  v["wifi_last_got_ip_ms"] = wifiLastGotIpMs.load(std::memory_order_relaxed);
  v["wifi_last_disconnect_ms"] = wifiLastDisconnectMs.load(std::memory_order_relaxed);
  v["wifi_last_disconnect_reason"] =
      wifiLastDisconnectReason.load(std::memory_order_relaxed);

  v["websocket_queue_wait_events"] = websocketWaitEvents.load(std::memory_order_relaxed);
  v["websocket_queue_waiting"] = websocketWaiting.load(std::memory_order_relaxed);
  v["websocket_queue_wait_total_ms"] =
      websocketWaitTotalMs.load(std::memory_order_relaxed);
  v["websocket_queue_wait_max_ms"] =
      websocketWaitMaxMs.load(std::memory_order_relaxed);
  v["websocket_timeout_disconnects"] = websocketTimeouts.load(std::memory_order_relaxed);

  v["telnet_write_zero_events"] = telnetZeroWrites.load(std::memory_order_relaxed);
  v["telnet_write_stall_events"] = telnetStallEvents.load(std::memory_order_relaxed);
  v["telnet_write_stalling"] = telnetStalling.load(std::memory_order_relaxed);
  v["telnet_write_stall_total_ms"] = telnetStallTotalMs.load(std::memory_order_relaxed);
  v["telnet_write_stall_max_ms"] = telnetStallMaxMs.load(std::memory_order_relaxed);
}
} // namespace EbtksNetDiag
