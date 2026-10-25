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
#include "ebtks_firmware_options.h"
#include "ebtks_static_pages.h"

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

#if EBTKS_WEB_TERMINAL_ENABLE
#define EBTKS_HOME_Z80_LINK "<button onclick=\"location.href='/z80'\">Z80 / CP/M Terminal</button>"
#elif EBTKS_TELNET_ENABLE
#define EBTKS_HOME_Z80_LINK "<p class=\"small\">CP/M console: physical TFT or Telnet port 23</p>"
#else
#define EBTKS_HOME_Z80_LINK "<p class=\"small\">CP/M console: physical TFT</p>"
#endif

static const char EBTKS_HOME_FALLBACK[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-EBTKS</title><style>
body{font-family:Verdana,sans-serif;text-align:center;background:#252525;color:#eaeaea}main{max-width:420px;margin:3rem auto}button{border:0;border-radius:.3rem;background:#1fa3ec;color:white;line-height:2.4rem;font-size:1.1rem;width:100%;margin:.45rem 0;cursor:pointer}button:hover{background:#0e70a4}.small{font-size:.85rem;color:#aaa}
</style></head><body><main><h2>EBTKS</h2><p>ESP32 services</p>
<button onclick="location.href='/crt'">HP-80 Remote Display</button>
)HTML" EBTKS_HOME_Z80_LINK R"HTML(
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

// Preserve identity HTML in flash for clients without gzip support.
// The matching compressed bytes come from PlatformIO's build-time generator.
inline void ebtksServeHome(AsyncWebServerRequest *request)
{
  ebtksServeStaticHtml(request,
      reinterpret_cast<const uint8_t *>(EBTKS_HOME_FALLBACK),
      sizeof(EBTKS_HOME_FALLBACK)-1U,
      EBTKS_HOME_GZIP,sizeof(EBTKS_HOME_GZIP),
      "Home page response allocation failed");
}

inline void ebtksServeUpdater(AsyncWebServerRequest *request)
{
  // Existing SPIFFS updater remains the authoritative override.
  // Its content is user-provided, so don't label it gzip unless it is.
  if (SPIFFS.exists("/updater.html")) {
    request->send(SPIFFS,"/updater.html","text/html");
  } else {
    ebtksServeStaticHtml(request,
      reinterpret_cast<const uint8_t *>(EBTKS_UPDATER_FALLBACK),
      sizeof(EBTKS_UPDATER_FALLBACK)-1U,
      EBTKS_UPDATER_FALLBACK_GZIP,sizeof(EBTKS_UPDATER_FALLBACK_GZIP),
      "Updater page response allocation failed");
  }
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
#if EBTKS_TELNET_ENABLE && EBTKS_TELNET_SHARED_WORKER
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
    webTerminalCleanupClients();

    // MODEM stays available after ATH/carrier loss for another dial. Once
    // the verified native CP/M CCP is accepting a new command, however,
    // MODEM has exited: release its worker, stack, FIFO and socket resources.
    // Never infer guest program termination from a TCP disconnect alone.
    if (CpmSafePoint::Gate* gate=cpmSafeGatePtr()) {
      if (gate->compatible.load(std::memory_order_acquire) &&
          gate->mode.load(std::memory_order_acquire)==CpmSafePoint::CCP_INPUT)
        CpmWifiModem::requestReleaseAtCcp();
    }
    // Create the modem only upon a guest UART-port access; also safely
    // reap its suspended worker after returning to the CCP.
    CpmWifiModem::service();

    // Process at most one requested media listing in THIS background worker,
    // never on the AsyncTCP network task. No extra 4 KiB worker stack.
    CpmMediaAsync::service();

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
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool supported=gate && gate->compatible.load(std::memory_order_acquire);
      const uint8_t mode=gate?gate->mode.load(std::memory_order_acquire):CpmSafePoint::UNKNOWN;
      const uint8_t result=gate?gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER;
      v["supported"]=supported;
      v["ccp_idle"]=supported && mode==CpmSafePoint::CCP_INPUT && (bool)RUN;
      v["mode"]=CpmSafePoint::modeText(mode);
      v["probe"]=CpmSafePoint::resultText(result);
      v["operation"]=CpmSafePoint::operationText(
          gate?gate->operation.load(std::memory_order_acquire):CpmSafePoint::PROBE);
      v["checkpoint"]=CpmSafePoint::checkpointText(
          gate?gate->checkpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["directory_error"]=CpmSafePoint::directoryErrorText(
          gate?gate->directoryError.load(std::memory_order_acquire):CpmSafePoint::DIR_OK);
      // Z80-only cold restart clears the live 'state' to 'never'; the last
      // completed operation remains visible in the same heap-owned Gate.
      v["last_directory_state"]=CpmSafePoint::resultText(
          gate?gate->lastDirectoryResult.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["last_directory_error"]=CpmSafePoint::directoryErrorText(
          gate?gate->lastDirectoryError.load(std::memory_order_acquire):CpmSafePoint::DIR_OK);
      v["last_directory_checkpoint"]=CpmSafePoint::checkpointText(
          gate?gate->lastDirectoryCheckpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["last_directory_forced_z80_reset"]=gate &&
          gate->lastDirectoryReset.load(std::memory_order_acquire);
      v["git_commit"]=EBTKS_BUILD_GIT_COMMIT;
      v["web_terminal_enabled"]=(bool)EBTKS_WEB_TERMINAL_ENABLE;
      v["tft_terminal_enabled"]=(bool)EBTKS_TFT_TERMINAL_MIRROR;
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
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool supported=gate && gate->compatible.load(std::memory_order_acquire);
      const bool accepted=(bool)RUN && supported && gate->request();
      JsonDocument v;
      v["accepted"]=accepted;
      v["supported"]=supported;
      v["mode"]=CpmSafePoint::modeText(
          gate?gate->mode.load(std::memory_order_acquire):CpmSafePoint::UNKNOWN);
      v["probe"]=CpmSafePoint::resultText(
          gate?gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["instruction"]=accepted?
          "Press blank RETURN at the CP/M prompt; nonempty command cancels":
          "CP/M busy, unsupported, or probe already pending";
      String body;serializeJson(v,body);
      r->send(accepted?202:409,"application/json",body);
    });

  // Stage B1: directory search is read-only BDOS 17/18 and never starts
  // from the AsyncTCP task. Only the pinned Z80 worker touches guest RAM.
  ebtksServer.on("/api/cpm/service/directory",HTTP_POST,
    [](AsyncWebServerRequest *r) {
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool supported=gate && gate->compatible.load(std::memory_order_acquire);
      const bool accepted=(bool)RUN && supported &&
          gate->request(CpmSafePoint::DIRECTORY);
      JsonDocument v;
      v["accepted"]=accepted;
      v["supported"]=supported;
      v["operation"]="directory";
      v["state"]=CpmSafePoint::resultText(
          gate?gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["instruction"]=accepted?
          "Directory runs automatically at the verified empty CCP input line; no key required":
          "CP/M busy, unsupported, or another service request pending";
      String body;serializeJson(v,body);
      r->send(accepted?202:409,"application/json",body);
    });
  ebtksServer.on("/api/cpm/service/directory",HTTP_GET,
    [](AsyncWebServerRequest *r) {
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const uint8_t state=gate?
          gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER;
      const bool correctOperation=gate &&
          gate->operation.load(std::memory_order_acquire)==CpmSafePoint::DIRECTORY;
      JsonDocument v;
      v["supported"]=gate && gate->compatible.load(std::memory_order_acquire);
      v["operation"]=correctOperation?"directory":"none";
      v["state"]=correctOperation?CpmSafePoint::resultText(state):"not_requested";
      v["checkpoint"]=CpmSafePoint::checkpointText(
          gate?gate->checkpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["directory_error"]=CpmSafePoint::directoryErrorText(
          gate?gate->directoryError.load(std::memory_order_acquire):CpmSafePoint::DIR_OK);
      // Z80-only cold restart clears the live 'state' to 'never'; the last
      // completed operation remains visible in the same heap-owned Gate.
      v["last_directory_state"]=CpmSafePoint::resultText(
          gate?gate->lastDirectoryResult.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["last_directory_error"]=CpmSafePoint::directoryErrorText(
          gate?gate->lastDirectoryError.load(std::memory_order_acquire):CpmSafePoint::DIR_OK);
      v["last_directory_checkpoint"]=CpmSafePoint::checkpointText(
          gate?gate->lastDirectoryCheckpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["last_directory_forced_z80_reset"]=gate &&
          gate->lastDirectoryReset.load(std::memory_order_acquire);
      if(correctOperation && state==CpmSafePoint::PASSED) {
        CpmSafePoint::Directory snapshot;
        if(!gate->copyDirectory(snapshot)) {
          r->send(503,"application/json","{\"error\":\"snapshot_busy\"}");
          return;
        }
        v["count"]=snapshot.count;
        v["truncated"]=snapshot.truncated;
        v["skipped_malformed_entries"]=snapshot.skipped;
        if(snapshot.skipped) {
          // Index, byte offset and raw byte of first malformed 8.3 name:
          // disclose metadata, never raw file payload or guest memory.
          v["first_bad_search_index"]=snapshot.firstBadIndex;
          v["first_bad_name_offset"]=snapshot.firstBadOffset;
          v["first_bad_name_byte"]=snapshot.firstBadByte;
        }
        v["drive"]=String(snapshot.drive);
        v["scope"]=gate->requestedDrive.load(std::memory_order_acquire)?
            "requested_drive_current_user":"current_drive_current_user";
        v["entries_are"]="directory_extents_not_file_sizes";
        JsonArray entries=v["entries"].to<JsonArray>();
        for(uint8_t i=0;i<snapshot.count;++i) {
          JsonObject entry=entries.add<JsonObject>();
          entry["name"]=snapshot.entries[i].name;
          entry["user"]=snapshot.entries[i].user;
          entry["extent"]=snapshot.entries[i].extent;
          entry["records"]=snapshot.entries[i].records;
        }
      }
      String body;serializeJson(v,body);
      r->send(200,"application/json",body);
    });

  // Stage B2a, read-only CP/M single-record diagnostic. NEVER access
  // guest memory or call BDOS from an AsyncTCP callback. Accept only a
  // strict A:..P: 8.3 name; user area is the current CP/M user.
  // E.g. POST /api/cpm/service/file?name=A%3ASTAT.COM&record=0
  ebtksServer.on("/api/cpm/service/file",HTTP_POST,
    [](AsyncWebServerRequest *r) {
      if(!r->hasParam("name") || !r->hasParam("record")) {
        r->send(400,"application/json","{\"error\":\"name_and_record_required\"}");
        return;
      }
      const String spec=r->getParam("name")->value();
      const String index=r->getParam("record")->value();
      uint8_t drive=0, fcb[11]{};
      if(!CpmFileReadService::parseName(spec.c_str(),drive,fcb) ||
         index.length()==0 || index.length()>2) {
        r->send(400,"application/json","{\"error\":\"invalid_8_3_or_record\"}");
        return;
      }
      uint16_t record=0;
      for(size_t i=0;i<index.length();++i) {
        if(index[i]<'0' || index[i]>'9') {
          r->send(400,"application/json","{\"error\":\"invalid_record\"}");
          return;
        }
        record=uint16_t(record*10+index[i]-'0');
      }
      if(record>CpmFileReadService::kMaxRecord) {
        r->send(400,"application/json","{\"error\":\"record_limit_63\"}");
        return;
      }
      CpmSafePoint::Gate* gate=cpmSafeGatePtr();
      const bool supported=gate && gate->compatible.load(std::memory_order_acquire);
      const bool accepted=(bool)RUN && supported &&
           gate->request(CpmSafePoint::READ_RECORD,drive,fcb,record);
      JsonDocument v;
      v["accepted"]=accepted;
      v["supported"]=supported;
      v["operation"]="file_read";
      v["name"]=spec;
      v["record"]=record;
      v["state"]=CpmSafePoint::resultText(gate?
          gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["instruction"]=accepted?
          "Automatic empty CCP return; read runs only at verified D382 boundary":
          "CCP not ready, running application, incompatible image, or request pending";
      String body;serializeJson(v,body);
      r->send(accepted?202:409,"application/json",body);
    });
  ebtksServer.on("/api/cpm/service/file",HTTP_GET,
    [](AsyncWebServerRequest *r) {
      CpmSafePoint::Gate* gate=cpmSafeGatePtr();
      const bool correct=gate &&
          gate->operation.load(std::memory_order_acquire)==CpmSafePoint::READ_RECORD;
      const uint8_t state=gate?gate->result.load(std::memory_order_acquire):
          CpmSafePoint::NEVER;
      JsonDocument v;
      v["supported"]=gate && gate->compatible.load(std::memory_order_acquire);
      v["operation"]=correct?"file_read":"none";
      v["state"]=correct?CpmSafePoint::resultText(state):"not_requested";
      v["checkpoint"]=CpmSafePoint::checkpointText(
          gate?gate->checkpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["file_error"]=CpmSafePoint::fileErrorText(
          gate?gate->fileError.load(std::memory_order_acquire):CpmSafePoint::FILE_OK);
      v["record_max"]=CpmFileReadService::kMaxRecord;
      v["last_file_state"]=CpmSafePoint::resultText(gate?
          gate->lastFileResult.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["last_file_error"]=CpmSafePoint::fileErrorText(gate?
          gate->lastFileError.load(std::memory_order_acquire):CpmSafePoint::FILE_OK);
      v["last_file_checkpoint"]=CpmSafePoint::checkpointText(gate?
          gate->lastFileCheckpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["last_file_forced_z80_reset"]=gate &&
          gate->lastFileReset.load(std::memory_order_acquire);
      if(correct && state==CpmSafePoint::PASSED) {
        CpmSafePoint::FileRecord data;
        if(!gate->copyFile(data)) {
          r->send(503,"application/json","{\"error\":\"file_snapshot_busy\"}");
          return;
        }
        v["record"]=data.record;
        v["has_data"]=data.hasData;
        v["eof"]=data.eof;
        v["length"]=data.hasData?128:0;
        if(data.hasData) {
          // Hex is lossless: 0x00, 0x1A and 0xE5 are NOT filtered.
          char hex[257];
          static const char digits[]="0123456789ABCDEF";
          for(unsigned i=0;i<128;++i) {
            hex[i*2]=digits[data.data[i]>>4];
            hex[i*2+1]=digits[data.data[i]&15];
          }
          hex[256]='\0';
          v["encoding"]="hex";
          v["crc16_ccitt_false"]=data.crc16;
          v["data"]=hex;
        }
      }
      String body;serializeJson(v,body);
      r->send(200,"application/json",body);
    });

  // Stage B2b: opt-in, trusted-LAN-only CP/M -> Teensy SD export.
  // HTTP only arms the safe-point request; the pinned Z80 task executes it.
  // Teensy confines output to exclusive files below /cpm/transfers.
  ebtksServer.on("/api/cpm/service/export",HTTP_POST,
    [](AsyncWebServerRequest *r) {
      if(!r->hasParam("name")) {
        r->send(400,"application/json","{\"error\":\"name_required\"}");
        return;
      }
      const String spec=r->getParam("name")->value();
      uint8_t drive=0,fcb[11]{};
      if(!CpmFileReadService::parseName(spec.c_str(),drive,fcb)) {
        r->send(400,"application/json","{\"error\":\"invalid_8_3_source\"}");
        return;
      }
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool supported=gate && gate->compatible.load(std::memory_order_acquire);
      const bool accepted=(bool)RUN && supported &&
        gate->request(CpmSafePoint::EXPORT_SD,drive,fcb,0);
      JsonDocument v;
      v["accepted"]=accepted;
      v["operation"]="sd_export";
      v["source"]=spec;
      v["state"]=CpmSafePoint::resultText(gate?
        gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["instruction"]=accepted?
        "Automatic empty Return; files only under /cpm/transfers on Teensy":
        "CCP not idle, another request active, or unsupported native CP/M";
      String body;serializeJson(v,body);
      r->send(accepted?202:409,"application/json",body);
    });
  ebtksServer.on("/api/cpm/service/export",HTTP_GET,
    [](AsyncWebServerRequest *r) {
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool correct=gate &&
        gate->operation.load(std::memory_order_acquire)==CpmSafePoint::EXPORT_SD;
      JsonDocument v;
      v["supported"]=gate && gate->compatible.load(std::memory_order_acquire);
      v["operation"]=correct?"sd_export":"not_requested";
      v["state"]=correct?CpmSafePoint::resultText(
        gate->result.load(std::memory_order_acquire)):"not_requested";
      v["checkpoint"]=CpmSafePoint::checkpointText(gate?
        gate->checkpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["file_error"]=CpmSafePoint::fileErrorText(gate?
        gate->fileError.load(std::memory_order_acquire):CpmSafePoint::FILE_OK);
      v["records"]=correct?gate->exportRecords.load(std::memory_order_acquire):0;
      v["bytes"]=uint32_t(correct?gate->exportRecords.load(std::memory_order_acquire):0)*128U;
      v["crc16_ccitt_false"]=correct?gate->exportCrc.load(std::memory_order_acquire):0xFFFF;
      v["last_file_state"]=CpmSafePoint::resultText(gate?
        gate->lastFileResult.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["last_file_error"]=CpmSafePoint::fileErrorText(gate?
        gate->lastFileError.load(std::memory_order_acquire):CpmSafePoint::FILE_OK);
      v["last_file_forced_z80_reset"]=gate &&
        gate->lastFileReset.load(std::memory_order_acquire);
      if(correct) {
        const uint8_t drive=gate->requestedDrive.load(std::memory_order_acquire);
        if(drive>=1 && drive<=16) {
          String target="/cpm/transfers/";
          target+=char('A'+drive-1);target+='_';
          for(unsigned i=0;i<8;++i) {
            const char ch=(char)gate->requestedFile[i].load(std::memory_order_relaxed);
            if(ch!=' ')target+=ch;
          }
          bool ext=false;
          for(unsigned i=8;i<11;++i)
            if(gate->requestedFile[i].load(std::memory_order_relaxed)!=' ')ext=true;
          if(ext) {
            target+='.';
            for(unsigned i=8;i<11;++i) {
              const char ch=(char)gate->requestedFile[i].load(std::memory_order_relaxed);
              if(ch!=' ')target+=ch;
            }
          }
          v["destination"]=target;
        }
      }
      String body;serializeJson(v,body);
      r->send(200,"application/json",body);
    });

  // B2c - write to a NEW native CP/M name, from a published Teensy
  // /cpm/transfers file only. An explicit confirmation string is mandatory.
  // This HTTP callback only arms the native CCP safe point; never calls BDOS.
  ebtksServer.on("/api/cpm/service/import",HTTP_POST,
    [](AsyncWebServerRequest *r) {
      if(!r->hasParam("source") || !r->hasParam("name")) {
        r->send(400,"application/json","{\"error\":\"source_and_name_required\"}");
        return;
      }
      if(!r->hasParam("confirm") ||
         r->getParam("confirm")->value()!="CREATE_NEW") {
        r->send(428,"application/json","{\"error\":\"confirm_CREATE_NEW_required\"}");
        return;
      }
      const String source=r->getParam("source")->value();
      const String target=r->getParam("name")->value();
      uint8_t srcDrive=0,dstDrive=0,srcFcb[11]{},dstFcb[11]{};
      if(!CpmFileReadService::parseName(source.c_str(),srcDrive,srcFcb) ||
         !CpmFileReadService::parseName(target.c_str(),dstDrive,dstFcb)) {
        r->send(400,"application/json","{\"error\":\"invalid_source_or_target_8_3\"}");
        return;
      }
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool supported=gate && gate->compatible.load(std::memory_order_acquire);
      const bool accepted=(bool)RUN && supported &&
        gate->request(CpmSafePoint::IMPORT_SD,dstDrive,dstFcb,0,srcFcb,srcDrive);
      JsonDocument v;
      v["accepted"]=accepted;
      v["operation"]="sd_import";
      v["source"]=source;
      v["target"]=target;
      v["state"]=CpmSafePoint::resultText(gate?
        gate->result.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["instruction"]=accepted?
        "Import runs at verified native CCP; CP/M MAKE only if target absent":
        "CCP not idle, another request active, or unsupported native CP/M";
      String body;serializeJson(v,body);
      r->send(accepted?202:409,"application/json",body);
    });
  ebtksServer.on("/api/cpm/service/import",HTTP_GET,
    [](AsyncWebServerRequest *r) {
      CpmSafePoint::Gate *gate=cpmSafeGatePtr();
      const bool correct=gate &&
        gate->operation.load(std::memory_order_acquire)==CpmSafePoint::IMPORT_SD;
      JsonDocument v;
      v["supported"]=gate && gate->compatible.load(std::memory_order_acquire);
      v["operation"]=correct?"sd_import":"not_requested";
      v["state"]=correct?CpmSafePoint::resultText(
        gate->result.load(std::memory_order_acquire)):"not_requested";
      v["checkpoint"]=CpmSafePoint::checkpointText(gate?
        gate->checkpoint.load(std::memory_order_acquire):CpmSafePoint::IDLE);
      v["file_error"]=CpmSafePoint::fileErrorText(gate?
        gate->fileError.load(std::memory_order_acquire):CpmSafePoint::FILE_OK);
      v["records"]=correct?gate->exportRecords.load(std::memory_order_acquire):0;
      v["bytes"]=uint32_t(correct?
        gate->exportRecords.load(std::memory_order_acquire):0)*128U;
      v["crc16_ccitt_false"]=correct?
        gate->exportCrc.load(std::memory_order_acquire):0xFFFF;
      v["expected_records"]=correct?
        gate->importTotalRecords.load(std::memory_order_acquire):0;
      v["expected_crc16_ccitt_false"]=correct?
        gate->importExpectedCrc.load(std::memory_order_acquire):0xFFFF;
      v["partial_cpm_file_possible"]=correct &&
        gate->importPartialPossible.load(std::memory_order_acquire);
      v["last_file_state"]=CpmSafePoint::resultText(gate?
        gate->lastFileResult.load(std::memory_order_acquire):CpmSafePoint::NEVER);
      v["last_file_error"]=CpmSafePoint::fileErrorText(gate?
        gate->lastFileError.load(std::memory_order_acquire):CpmSafePoint::FILE_OK);
      v["last_file_forced_z80_reset"]=gate &&
        gate->lastFileReset.load(std::memory_order_acquire);
      if(correct) {
        const auto addName=[&](const char *field,
             const std::atomic<uint8_t> name[11],uint8_t drive) {
          if(drive<1 || drive>16)return;
          String spec;spec.reserve(16);
          spec+=char('A'+drive-1);spec+=':';
          for(unsigned i=0;i<8;++i) {
            const char ch=(char)name[i].load(std::memory_order_relaxed);
            if(ch!=' ')spec+=ch;
          }
          bool ext=false;
          for(unsigned i=8;i<11;++i)
            if(name[i].load(std::memory_order_relaxed)!=' ')ext=true;
          if(ext) {
            spec+='.';
            for(unsigned i=8;i<11;++i) {
              const char ch=(char)name[i].load(std::memory_order_relaxed);
              if(ch!=' ')spec+=ch;
            }
          }
          v[field]=spec;
        };
        addName("source",gate->requestedSourceFile,
                gate->requestedSourceDrive.load(std::memory_order_acquire));
        addName("target",gate->requestedFile,
                gate->requestedDrive.load(std::memory_order_acquire));
      }
      String body;serializeJson(v,body);
      r->send(200,"application/json",body);
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
      doc["telnet_enabled"]=(bool)EBTKS_TELNET_ENABLE;
#if EBTKS_TELNET_ENABLE
      doc["telnet_listener_mode"]=EBTKS_TELNET_SHARED_WORKER?"shared_web_task":"dedicated_task";
      doc["telnet_connected"]=serverClient.connected();
#else
      doc["telnet_listener_mode"]="disabled";
      doc["telnet_connected"]=false;
#endif
      doc["web_terminal_enabled"]=(bool)EBTKS_WEB_TERMINAL_ENABLE;
      doc["tft_terminal_enabled"]=(bool)EBTKS_TFT_TERMINAL_MIRROR;
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

#if EBTKS_TELNET_ENABLE && EBTKS_TELNET_SHARED_WORKER
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
  #if EBTKS_WEB_TERMINAL_ENABLE
  Serial.print("Z80 terminal: http://"); Serial.print(WiFi.localIP()); Serial.println("/z80");
  #endif
  Serial.print("CP/M media manager: http://"); Serial.print(WiFi.localIP()); Serial.println("/cpmmedia");
  Serial.print("Firmware updater: http://"); Serial.print(WiFi.localIP()); Serial.println("/updater");

  return true;
}
