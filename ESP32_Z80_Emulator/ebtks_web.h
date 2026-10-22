#pragma once

#include <FS.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "ebtks_build_revision.h" // generated at firmware build

// Shared EBTKS web server. /ws remains the original Teensy JSON bridge.
// The Z80 terminal has its own independent websocket endpoint.
AsyncWebServer ebtksServer(80);
AsyncWebSocket ebtksWebSocket("/ws");
DNSServer ebtksDns;

volatile bool ebtksRestartRequired = false;

struct EBTKSBrowserRequest {
  char text[512];
};
QueueHandle_t ebtksBrowserRequestQueue = nullptr;

static const char EBTKS_HOME_FALLBACK[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-EBTKS</title><style>
body{font-family:Verdana,sans-serif;text-align:center;background:#252525;color:#eaeaea}main{max-width:420px;margin:3rem auto}button{border:0;border-radius:.3rem;background:#1fa3ec;color:white;line-height:2.4rem;font-size:1.1rem;width:100%;margin:.45rem 0;cursor:pointer}button:hover{background:#0e70a4}.small{font-size:.85rem;color:#aaa}
</style></head><body><main><h2>EBTKS</h2><p>ESP32 services</p>
<button onclick="location.href='/crt'">HP-80 Remote Display</button>
<button onclick="location.href='/z80'">Z80 / CP/M Terminal</button>
<button onclick="location.href='/cpmmedia'">CP/M Media Manager</button>
<button onclick="location.href='/updater'">Firmware Updater</button>
<p class="small">The original EBTKS websocket service remains available at /ws.</p>
</main></body></html>)HTML";

static const char EBTKS_UPDATER_FALLBACK[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32_EBTKS Update</title><style>
body{font-family:Verdana,sans-serif;text-align:center;background:#222;color:#fff;line-height:200%}.bt{background:#333;color:#fff;border:.3ch solid #333;display:inline-block;font-size:18px;margin:8px;padding:.4rem 1rem}
</style></head><body><h2>ESP32_EBTKS Software Update</h2>
<form method="POST" action="/updater" enctype="multipart/form-data"><input type="file" class="bt" name="update" required><br><input type="submit" class="bt" value="Update!"></form>
<p><button class="bt" onclick="history.back()">Back</button></p></body></html>)HTML";

// All static pages use AsyncProgmemResponse. It never duplicates whole
// HTML bodies as mutable heap Strings, unlike AsyncBasicResponse.
inline void ebtksServeFlashPage(AsyncWebServerRequest *request,
                                const char *data,size_t bytes) {
  AsyncWebServerResponse *response=request->beginResponse(
      200,"text/html; charset=utf-8",
      reinterpret_cast<const uint8_t *>(data),bytes);
  if(!response) {
    request->send(503,"text/plain","HTML response allocation failed");
    return;
  }
  response->addHeader("Cache-Control","no-store");
  request->send(response);
}
inline void ebtksServeHome(AsyncWebServerRequest *request)
{
  ebtksServeFlashPage(request,EBTKS_HOME_FALLBACK,
                       sizeof(EBTKS_HOME_FALLBACK)-1U);
}

inline void ebtksServeUpdater(AsyncWebServerRequest *request)
{
  if (SPIFFS.exists("/updater.html")) request->send(SPIFFS, "/updater.html", "text/html");
  else ebtksServeFlashPage(request,EBTKS_UPDATER_FALLBACK,
                            sizeof(EBTKS_UPDATER_FALLBACK)-1U);
}

inline void ebtksQueueWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
  if (!info || !info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
  if (!ebtksBrowserRequestQueue || len >= 512) return;

  EBTKSBrowserRequest request = {};
  memcpy(request.text, data, len);
  request.text[len] = 0;
  if (xQueueSend(ebtksBrowserRequestQueue, &request, 0) != pdTRUE) {
    Serial.println("EBTKS browser request queue full");
  }
}

inline void ebtksWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type) {
    case WS_EVT_DATA:
      ebtksQueueWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_CONNECT:
      Serial.printf("EBTKS websocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("EBTKS websocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

inline void ebtksWebBackgroundTask(void *parameter)
{
  EBTKSBrowserRequest request;

  for (;;) {
#if EBTKS_TELNET_SHARED_WORKER
    // Accept connection only; an active Telnet session is still handled by
    // ConsoleTask. No permanently idle TelnetTask or its 3 KiB stack.
    telnetPoll();
#endif
    if (ebtksBrowserRequestQueue && xQueueReceive(ebtksBrowserRequestQueue, &request, 0) == pdTRUE) {
      String reply;
      if (ebtksLinkTransaction(String(request.text), reply, 3000)) {
        if (ebtksWebSocket.count()) ebtksWebSocket.textAll(reply.c_str());
      } else {
        Serial.println("EBTKS browser Teensy transaction failed");
      }
    }

    ebtksWebSocket.cleanupClients();
    z80WebSocket.cleanupClients();

    // Preserve idle UI memory: no modem socket, FIFOs or task until a
    // CP/M program actually polls the virtual serial port at 88h-8Fh.
    // Allocations are guarded by a free-heap and largest-block reserve.
    CpmWifiModem::service();

    if (ebtksRestartRequired) {
      Serial.println("Firmware update complete; rebooting ESP32");
      delay(1000);
      ESP.restart();
    }

    vTaskDelay(2);
  }
}

inline bool ebtksWebBegin()
{
  if (!SPIFFS.begin(true)) {
    Serial.println("Error mounting SPIFFS");
    return false;
  }

  WiFi.mode(WIFI_STA);

  AsyncWiFiManager wm(&ebtksServer, &ebtksDns);
  // This library defaults to debug enabled and its implementation writes to
  // the framework Serial object.  Keep the inter-processor UART protocol-only.
  wm.setDebugOutput(false);
  bool connected = wm.autoConnect("EBTKS_AP", "password");
  if (!connected) {
    Serial.println("Failed to connect Wi-Fi");
    return false;
  }

  Serial.print("Wi-Fi connected, IP: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin("esp32_ebtks")) {
    Serial.println("Error setting up mDNS responder");
  } else {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: esp32_ebtks.local");
  }

  ebtksBrowserRequestQueue = xQueueCreate(8, sizeof(EBTKSBrowserRequest));
  if (!ebtksBrowserRequestQueue) {
    Serial.println("Unable to allocate EBTKS browser request queue");
    return false;
  }

  ebtksServer.on("/", HTTP_GET, ebtksServeHome);

  ebtksServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists("/style.css")) request->send(SPIFFS, "/style.css", "text/css");
    else request->send(404, "text/plain", "Not found");
  });

  // Read-only CCP safety-state probe, no SD, UART or Z80 register reads
  // from AsyncWebServer callbacks. No device mappings or filesystem writes.
  ebtksServer.on("/api/cpm/service/status",HTTP_GET,
    [](AsyncWebServerRequest *r) {
      JsonDocument v;
      const bool supported=cpmSafeGate.compatible.load(std::memory_order_acquire);
      const uint8_t mode=cpmSafeGate.mode.load(std::memory_order_acquire);
      const uint8_t result=cpmSafeGate.result.load(std::memory_order_acquire);
      v["supported"]=supported;
      v["ccp_idle"]=supported && mode==CpmSafePoint::CCP_INPUT && (bool)RUN;
      v["mode"]=CpmSafePoint::modeText(mode);
      v["probe"]=CpmSafePoint::resultText(result);
      v["git_commit"]=EBTKS_BUILD_GIT_COMMIT;
      String body;serializeJson(v,body);
      r->send(200,"application/json",body);
    });
  // Phase A only: arm a harmless three-instruction Z80 probe while the CCP
  // is in interactive console input. It will execute ONLY if the user presses
  // a BLANK Return, upon re-entry to the D382 CCP prompt before next BDOS.
  // Any nonempty command cancels; running applications are refused. This
  // deliberately DOES NOT inject an Enter or queue work behind a program.
  ebtksServer.on("/api/cpm/service/probe",HTTP_POST,
    [](AsyncWebServerRequest *r) {
      const bool supported=cpmSafeGate.compatible.load(std::memory_order_acquire);
      const bool accepted=(bool)RUN && supported && cpmSafeGate.request();
      JsonDocument v;
      v["accepted"]=accepted;
      v["supported"]=supported;
      v["mode"]=CpmSafePoint::modeText(
          cpmSafeGate.mode.load(std::memory_order_acquire));
      v["probe"]=CpmSafePoint::resultText(
          cpmSafeGate.result.load(std::memory_order_acquire));
      v["instruction"]=accepted?
          "Press blank RETURN at the CP/M prompt; nonempty command cancels":
          "CP/M busy, unsupported, or probe already pending";
      String body;serializeJson(v,body);
      r->send(accepted?202:409,"application/json",body);
    });

  // RAM-only liveness and reset forensics, no SPI/UART/SD work.
  // Read BEFORE and AFTER enabling 4MHz read-ahead from Media Manager.
  // If the ESP resets, its next boot retains the hardware reset reason.
  ebtksServer.on("/api/system/health",HTTP_GET,
    [](AsyncWebServerRequest *r){
      const esp_reset_reason_t reason=esp_reset_reason();
      JsonDocument v;
      v["uptime_ms"]=(uint32_t)millis();
      v["git_commit"]=EBTKS_BUILD_GIT_COMMIT;
      v["reset_reason"]=(int)reason;
      v["reset_watchdog"]=(reason==ESP_RST_INT_WDT ||
                            reason==ESP_RST_TASK_WDT ||
                            reason==ESP_RST_WDT);
      v["reset_panic"]=(reason==ESP_RST_PANIC);
      v["reset_brownout"]=(reason==ESP_RST_BROWNOUT);
      v["heap_free"]=(uint32_t)ESP.getFreeHeap();
      v["cpm_modem_state"]=(unsigned)CpmWifiModem::status();
      v["cpm_modem_active"]=CpmWifiModem::active();
      v["cpm_modem_low_memory_dial_refusals"]=CpmWifiModem::lowMemoryDialRefusals();
      v["heap_min_free"]=(uint32_t)ESP.getMinFreeHeap();
      v["largest_free_8bit_block"]=(uint32_t)
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      v["internal_free"]=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      v["internal_largest_block"]=(uint32_t)
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
      v["spi_enabled"]=(bool)spiDiskReadsEnabled;
      v["spi_cache_allocated"]=spiDiskCacheAllocated();
      v["spi_cache_storage"]=spiDiskCacheStorage();
      v["spi_worker_created"]=spiDiskWorkerHandle!=nullptr;
      v["spi_dma_initialized"]=(bool)spiTestInitialized;
      v["spi_job_active"]=(bool)spiDiskJobActive;
      v["spi_requires_reboot"]=(bool)spiTestNeedsReboot;
      v["spi_read_failures"]=(uint32_t)spiDiskReadFailures;
      String body;serializeJson(v,body);
      r->send(200,"application/json",body);
    });
  // Task-aware RAM profile for staged diagnosis. Query this BEFORE running
  // MODEM and after AT/ATI. Neither endpoint touches Teensy, SD or Wi-Fi
  // sockets, and existing OTA/CPM handlers are preserved unmodified.
  // ESPAsyncWebServer 3.6.0 matches a route on its child paths and
  // selects the FIRST matching handler. Register /memory/pools BEFORE
  // /memory or the shorter route hides the capability inventory.
  // Read-only IDF allocator capability inventory; deliberately separate
  // from the established small /api/system/memory health endpoint.
  // All rows overlap, and must not be added together.
  ebtksServer.on("/api/system/memory/pools",HTTP_GET,
    [](AsyncWebServerRequest *r){
      JsonDocument doc;
      doc["uptime_ms"]=(uint32_t)millis();
      doc["z80_static_ram_bytes"]=(uint32_t)sizeof(RAM);
      doc["capability_rows_overlap"]=true;
      EbtksMemory::appendHeapPools(doc);
      String body;
      serializeJson(doc,body);
      r->send(200,"application/json",body);
    });
  ebtksServer.on("/api/system/memory", HTTP_GET,
    [](AsyncWebServerRequest *r){
      JsonDocument doc;
      doc["uptime_ms"]=(uint32_t)millis();
      doc["heap_free"]=(uint32_t)ESP.getFreeHeap();
      doc["free_8bit"]=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
      doc["largest_8bit"]=(uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      doc["telnet_listener_mode"]=EBTKS_TELNET_SHARED_WORKER?"shared_web_task":"dedicated_task";
      doc["telnet_connected"]=serverClient.connected();
      doc["min_heap_free"]=(uint32_t)ESP.getMinFreeHeap();
      doc["min_free_8bit"]=(uint32_t)
          heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
      doc["modem_state"]=(unsigned)CpmWifiModem::status();
      doc["modem_dial_refused_low_heap"]=CpmWifiModem::lowMemoryDialRefusals();
      doc["min_dial_heap"]=CpmWifiModem::kDialMinimumFreeHeap;
      doc["min_dial_largest8"]=CpmWifiModem::kDialMinimumLargest8Bit;
      EbtksMemory::append(doc);
      JsonObject tasks=doc["stack_unused_bytes"].to<JsonObject>();
      EbtksMemory::stack(tasks,"web",Task5);
      EbtksMemory::stack(tasks,"z80_cpu",Task1);
      EbtksMemory::stack(tasks,"console",Task2);
      EbtksMemory::stack(tasks,"telnet",Task3);
      EbtksMemory::stack(tasks,"hp_keyboard",Cpm85Input::taskHandle);
      EbtksMemory::stack(tasks,"spi_disk",spiDiskWorkerHandle);
      EbtksMemory::stack(tasks,"modem",CpmWifiModem::workerHandle);
#if EBTKS_TFT_TERMINAL_MIRROR
      EbtksMemory::stack(tasks,"tft",EbtksTft::mirrorTaskHandle);
#endif
      String body; serializeJson(doc,body);
      r->send(200,"application/json",body);
    });
  ebtksServer.on("/updater", HTTP_GET, ebtksServeUpdater);

  ebtksServer.on(
    "/updater", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (Update.hasError()) {
        request->send(500, "text/plain", "Firmware update failed. Please check the file and retry.");
        return;
      }
      request->send(200, "text/plain", "Firmware updated successfully. EBTKS will reboot shortly.");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        Serial.printf("OTA Update Start: %s\n", filename.c_str());
        if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) Update.printError(Serial);
      }
      if (!Update.hasError() && len) {
        if (Update.write(data, len) != len) Update.printError(Serial);
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA Update Success: %u bytes\n", (unsigned)(index + len));
          ebtksRestartRequired = true;
        } else Update.printError(Serial);
      }
    }
  );

  ebtksWebSocket.onEvent(ebtksWebSocketEvent);
  ebtksServer.addHandler(&ebtksWebSocket);

  webTerminalRegister(ebtksServer);
  spiTestRegister(ebtksServer);
  spiDiskRegister(ebtksServer); // compile-configurable boot default, runtime control and stats

  ebtksServer.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  ebtksServer.begin();

#if EBTKS_TELNET_SHARED_WORKER
  // Listener stays available for inbound TCP connections, while the
  // already-existing Web worker polls it without a second idle thread.
  telnetBegin();
#endif
  xTaskCreatePinnedToCore(
    ebtksWebBackgroundTask,
    "EBTKSWebTask",
    4096,
    nullptr,
    1,
    &Task5,
    0
  );

  Serial.println("EBTKS web services started");
  Serial.print("Home: http://"); Serial.println(WiFi.localIP());
  Serial.print("HP-80 remote display: http://"); Serial.print(WiFi.localIP()); Serial.println("/crt");
  Serial.print("Z80 terminal: http://"); Serial.print(WiFi.localIP()); Serial.println("/z80");
  Serial.print("CP/M media manager: http://"); Serial.print(WiFi.localIP()); Serial.println("/cpmmedia");
  Serial.print("Firmware updater: http://"); Serial.print(WiFi.localIP()); Serial.println("/updater");

  return true;
}
