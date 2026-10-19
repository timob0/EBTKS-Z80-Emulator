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

  // RAM-only liveness and reset forensics, no SPI/UART/SD work.
  // Read BEFORE and AFTER enabling 4MHz read-ahead from Media Manager.
  // If the ESP resets, its next boot retains the hardware reset reason.
  ebtksServer.on("/api/system/health",HTTP_GET,
    [](AsyncWebServerRequest *r){
      const esp_reset_reason_t reason=esp_reset_reason();
      JsonDocument v;
      v["uptime_ms"]=(uint32_t)millis();
      v["reset_reason"]=(int)reason;
      v["reset_watchdog"]=(reason==ESP_RST_INT_WDT ||
                            reason==ESP_RST_TASK_WDT ||
                            reason==ESP_RST_WDT);
      v["reset_panic"]=(reason==ESP_RST_PANIC);
      v["reset_brownout"]=(reason==ESP_RST_BROWNOUT);
      v["heap_free"]=(uint32_t)ESP.getFreeHeap();
      v["heap_min_free"]=(uint32_t)ESP.getMinFreeHeap();
      v["largest_free_8bit_block"]=(uint32_t)
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      v["internal_free"]=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      v["internal_largest_block"]=(uint32_t)
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
      v["spi_enabled"]=(bool)spiDiskReadsEnabled;
      v["spi_cache_allocated"]=spiDiskCache!=nullptr;
      v["spi_worker_created"]=spiDiskWorkerHandle!=nullptr;
      v["spi_dma_initialized"]=(bool)spiTestInitialized;
      v["spi_job_active"]=(bool)spiDiskJobActive;
      v["spi_requires_reboot"]=(bool)spiTestNeedsReboot;
      v["spi_read_failures"]=(uint32_t)spiDiskReadFailures;
      String body;serializeJson(v,body);
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
