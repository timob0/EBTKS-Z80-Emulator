#pragma once
// EXPERIMENTAL, opt-in-only CN3 512-byte SPI read test.
// ESP32 SPI2 (HSPI) slave; Teensy 4.1 SPI0 master. UART/JSON handles the
// control request and returned CRC. No disk image data is read or written.
// Tested firmware on main remains unchanged; this feature branch is untested.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <driver/spi_slave.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <string.h>

static constexpr spi_host_device_t SPI_TEST_HOST = HSPI_HOST;
static constexpr int SPI_TEST_MOSI = 22;   // CN3-21  E22 ESP input (Teensy T11)
static constexpr int SPI_TEST_MISO = 17;   // CN3-13  E17 ESP output (Teensy T12)
static constexpr int SPI_TEST_SCLK = 16;   // CN3-14  E16 ESP input (Teensy T13)
static constexpr int SPI_TEST_CS = 32;     // CN3-4   E34 input; external 10k pull-up
static constexpr int SPI_TEST_READY = 27;  // CN3-8   E27 output (Teensy T06)
static constexpr size_t SPI_TEST_BYTES = 512;

// A single 512-byte transaction, non-concurrent with disk I/O through the
// link mutex. Once armed, only the Teensy controls CS/SCK. DMA memory and the
// transaction descriptor remain alive even on a timeout: no use-after-free.
static uint8_t *spiTestRx = nullptr;
static spi_slave_transaction_t spiTestTrans = {};
static bool spiTestInitialized = false;
static volatile bool spiTestNeedsReboot = false;
static volatile int spiTestState = 0; // 0 idle, 1 running, 2 passed, 3 failed
static volatile uint32_t spiTestElapsedUs = 0;
static volatile uint32_t spiTestRunNumber = 0;
static volatile uint16_t spiTestCrcLocal = 0;
static volatile uint16_t spiTestCrcRemote = 0;
static volatile int spiTestMismatchAt = -1;
static const char *volatile spiTestDetail = "Not tested";

// ISR-context callbacks: assert READY ONLY after SPI registers are armed.
// Do not use Serial, allocations, locks or the Teensy UART in these callbacks.
static void IRAM_ATTR spiTestPostSetup(spi_slave_transaction_t *)
{
  gpio_set_level((gpio_num_t)SPI_TEST_READY, 1);
}
static void IRAM_ATTR spiTestPostTrans(spi_slave_transaction_t *)
{
  gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
}

inline uint16_t spiTestCrcByte(uint16_t crc, uint8_t value)
{
  crc ^= value;
  for (unsigned j = 0; j < 8; ++j)
    crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
  return crc;
}

inline bool spiTestInit()
{
  if (spiTestInitialized) return true;
  pinMode(SPI_TEST_READY, OUTPUT);
  digitalWrite(SPI_TEST_READY, LOW);

  // Allocate from DMA-capable HEAP, not static DRAM; internal RAM is tight.
  spiTestRx = static_cast<uint8_t *>(
      heap_caps_malloc(SPI_TEST_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!spiTestRx) {
    spiTestDetail = "ESP32 DMA allocation failed";
    return false;
  }
  spi_bus_config_t bus = {};
  bus.mosi_io_num = SPI_TEST_MOSI;
  bus.miso_io_num = SPI_TEST_MISO;
  bus.sclk_io_num = SPI_TEST_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = SPI_TEST_BYTES;

  spi_slave_interface_config_t slave = {};
  slave.spics_io_num = SPI_TEST_CS;
  slave.mode = 0;                 // Teensy uses SPI_MODE0
  slave.queue_size = 1;
  slave.post_setup_cb = spiTestPostSetup;
  slave.post_trans_cb = spiTestPostTrans;

  // Use explicit DMA channel 1 (ESP-IDF 4.4 / ESP32 Arduino v2 target).
  const esp_err_t err = spi_slave_initialize(SPI_TEST_HOST, &bus, &slave, SPI_DMA_CH1);
  if (err != ESP_OK) {
    Serial.printf("SPI test: spi_slave_initialize failed: %s\n", esp_err_to_name(err));
    spiTestDetail = "ESP32 SPI2 slave initialization failed";
    heap_caps_free(spiTestRx);
    spiTestRx = nullptr;
    return false;
  }
  spiTestInitialized = true;
  Serial.println("SPI test: ESP32 SPI2 slave initialized (GPIO22/17/16, CS=32, READY=27)");
  return true;
}

inline void spiTestWorker(void *)
{
  spiTestElapsedUs = 0;
  spiTestCrcLocal = 0;
  spiTestCrcRemote = 0;
  spiTestMismatchAt = -1;
  spiTestDetail = "Initializing SPI2 slave";

  if (!spiTestInit()) {
    spiTestState = 3;
    vTaskDelete(nullptr);
    return;
  }

  memset(spiTestRx, 0, SPI_TEST_BYTES);
  memset(&spiTestTrans, 0, sizeof(spiTestTrans));
  spiTestTrans.length = SPI_TEST_BYTES * 8;
  spiTestTrans.rx_buffer = spiTestRx;
  // No TX buffer: first experiment only sends data Teensy -> ESP32.

  gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
  const esp_err_t queued = spi_slave_queue_trans(
      SPI_TEST_HOST, &spiTestTrans, pdMS_TO_TICKS(1000));
  if (queued != ESP_OK) {
    spiTestDetail = "Could not queue SPI slave DMA transaction";
    spiTestNeedsReboot = true;
    spiTestState = 3;
    Serial.printf("SPI test: queue failure: %s\n", esp_err_to_name(queued));
    vTaskDelete(nullptr);
    return;
  }

  // SPI transaction is queued BEFORE we tell Teensy to begin on UART.
  // post_setup_cb raises READY only after the peripheral is actually armed.
  const uint8_t seq = (uint8_t)(spiTestRunNumber & 0xffU);
  JsonDocument req;
  JsonObject probe = req["spi_probe"].to<JsonObject>();
  probe["seq"] = seq;
  probe["len"] = SPI_TEST_BYTES;
  probe["hz"] = 1000000;
  String message;
  serializeJson(req, message);
  String response;
  spiTestDetail = "Waiting for Teensy transfer";
  const bool replied = ebtksLinkTransaction(message, response, 3000);

  spi_slave_transaction_t *done = nullptr;
  const esp_err_t received = spi_slave_get_trans_result(
      SPI_TEST_HOST, &done, pdMS_TO_TICKS(200));
  if (received != ESP_OK || done != &spiTestTrans) {
    spiTestNeedsReboot = true; // Keep queued transaction/buffer valid until reboot.
    spiTestDetail = "SPI did not complete; reboot ESP32 before retrying";
    spiTestState = 3;
    Serial.printf("SPI test: transfer timeout/mismatch: %s, UART reply=%s\n",
                  esp_err_to_name(received), replied ? response.c_str() : "<timeout>");
    vTaskDelete(nullptr);
    return;
  }

  // Ownership of the DMA buffer has returned to the application.
  if (spiTestTrans.trans_len != SPI_TEST_BYTES * 8) {
    spiTestDetail = "Received wrong number of SPI bits";
    spiTestState = 3;
    Serial.printf("SPI test: expected 4096 bits; got %u\n",
                  (unsigned)spiTestTrans.trans_len);
    vTaskDelete(nullptr);
    return;
  }

  uint16_t actualCrc = 0xffff, expectedCrc = 0xffff;
  int mismatch = -1;
  for (size_t i = 0; i < SPI_TEST_BYTES; ++i) {
    const uint8_t expected = (uint8_t)(0xA5U ^ (uint8_t)(i * 37U + seq));
    actualCrc = spiTestCrcByte(actualCrc, spiTestRx[i]);
    expectedCrc = spiTestCrcByte(expectedCrc, expected);
    if (mismatch == -1 && expected != spiTestRx[i]) mismatch = (int)i;
  }
  spiTestCrcLocal = actualCrc;
  spiTestMismatchAt = mismatch;

  if (!replied) {
    spiTestDetail = "SPI completed but Teensy UART status timed out";
    spiTestState = 3;
    Serial.println("SPI test: no UART completion reply");
    vTaskDelete(nullptr);
    return;
  }

  JsonDocument reply;
  const auto parseError = deserializeJson(reply, response);
  if (parseError) {
    spiTestDetail = "Malformed Teensy SPI test UART reply";
    spiTestState = 3;
    vTaskDelete(nullptr);
    return;
  }
  const bool remoteOk = reply["spi_probe"]["ok"] | false;
  const int remoteSeq = reply["spi_probe"]["seq"] | -1;
  const int remoteLen = reply["spi_probe"]["len"] | -1;
  spiTestCrcRemote = (uint16_t)(reply["spi_probe"]["crc"] | 0);
  spiTestElapsedUs = reply["spi_probe"]["us"] | 0UL;
  if (!remoteOk || remoteSeq != seq || remoteLen != (int)SPI_TEST_BYTES ||
      mismatch >= 0 || actualCrc != expectedCrc || actualCrc != spiTestCrcRemote) {
    spiTestDetail = "Data or Teensy-reported CRC mismatch";
    spiTestState = 3;
    Serial.printf("SPI test FAILED seq=%u len=%d crc local=%04X remote=%04X expected=%04X mismatch=%d\n",
                  seq, remoteLen, actualCrc, spiTestCrcRemote, expectedCrc, mismatch);
  } else {
    spiTestDetail = "512 bytes verified; no CP/M disk accessed";
    spiTestState = 2;
    Serial.printf("SPI test PASS seq=%u 512 bytes CRC=%04X Teensy measured=%lu us\n",
                  seq, actualCrc, (unsigned long)spiTestElapsedUs);
  }
  vTaskDelete(nullptr);
}

inline bool spiTestStart()
{
  // Web requests are dispatched on the async web server task. Do not block.
  // Only one worker can run; leave a failed incomplete transaction disabled
  // until reboot rather than risking DMA descriptor reuse.
  if (spiTestState == 1 || spiTestNeedsReboot) return false;
  spiTestState = 1;
  ++spiTestRunNumber;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      spiTestWorker, "SpiCN3Test", 4096, nullptr, 1, nullptr, 0);
  if (ok != pdPASS) {
    spiTestState = 3;
    spiTestDetail = "Could not create SPI test worker";
    return false;
  }
  return true;
}

static const char SPI_TEST_PAGE[] PROGMEM = R"SPIHTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS CN3 SPI diagnostic</title>
<style>body{font:16px system-ui;background:#202124;color:#f0f0f0;max-width:720px;margin:30px auto;padding:15px}
button{background:#2383d1;color:white;border:0;border-radius:5px;padding:12px 18px;cursor:pointer}
pre{white-space:pre-wrap;background:#111;padding:15px;border-radius:6px}a{color:#80c4ff}</style></head>
<body><h2>CN3 SPI link experiment</h2>
<p><strong>Feature-branch test only.</strong> Teensy is SPI master, ESP32 is SPI slave.
This transmits a 512-byte pattern at 1 MHz and checks CRC and every byte. No SD image is touched.</p>
<p>Only start after BOTH SPI-test firmwares are flashed and CN3 wiring + 10k CS pull-up verified.</p>
<button id="run" onclick="start()">Run 512-byte test</button>
<pre id="out">Waiting for test...</pre><p><a href="/z80">CP/M terminal</a> · <a href="/z80debug">Z80 diagnostics</a></p>
<script>
async function refresh(){try{let r=await fetch('/api/spitest');let j=await r.json();document.getElementById('out').textContent=JSON.stringify(j,null,2);document.getElementById('run').disabled=j.state==='running'||j.needs_reboot;}catch(e){document.getElementById('out').textContent=String(e)}}
async function start(){const r=await fetch('/api/spitest',{method:'POST'});if(!r.ok){alert(await r.text())}refresh()}
setInterval(refresh,700);refresh()
</script></body></html>
)SPIHTML";

inline void spiTestRegister(AsyncWebServer &server)
{
  server.on("/spitest", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", SPI_TEST_PAGE);
  });
  server.on("/api/spitest", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!spiTestStart()) r->send(409, "text/plain", "SPI test already active or requires ESP32 reboot");
    else r->send(202, "application/json", "{\"started\":true}");
  });
  server.on("/api/spitest", HTTP_GET, [](AsyncWebServerRequest *r) {
    String json;
    JsonDocument doc;
    const int state = spiTestState;
    doc["state"] = state == 0 ? "idle" : state == 1 ? "running" :
                   state == 2 ? "passed" : "failed";
    doc["detail"] = spiTestDetail;
    doc["needs_reboot"] = (bool)spiTestNeedsReboot;
    doc["runs"] = (uint32_t)spiTestRunNumber;
    doc["bytes"] = SPI_TEST_BYTES;
    doc["clock_hz"] = 1000000;
    doc["teensy_transfer_us"] = (uint32_t)spiTestElapsedUs;
    doc["crc_esp"] = (uint16_t)spiTestCrcLocal;
    doc["crc_teensy"] = (uint16_t)spiTestCrcRemote;
    doc["first_mismatch_byte"] = (int)spiTestMismatchAt;
    serializeJson(doc, json);
    r->send(200, "application/json", json);
  });
}
