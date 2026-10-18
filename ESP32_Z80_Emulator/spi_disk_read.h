#pragma once
// Opt-in CP/M READ-ONLY binary SPI transport. The normal UART/Base64 read
// and ALL CP/M writes remain in cpm_media.h. The bulk framing comes from the
// existing interrupt-free, hardware-tested 4MHz/MODE0 stream diagnostic.
//
// A dedicated persistent core-0 worker performs ALL SPI DMA operations.
// Z80 core-1 never calls into the slave driver; it waits for a bounded result.
// 16-sector aligned read-ahead is stored in a private 8 KiB cache and is
// committed only after per-frame CRC/sequence checks AND aggregate verification.
// Cache is always invalidated before a CP/M write or transport mode change.
// A failed SPI request disables live SPI reads for this boot and falls back to
// the existing UART path; no partial SPI sector reaches Z80 RAM.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "spi_stream_frame.h"

// Globals declared in spi_link_test.h, included before this header:
// spiDiskReadsEnabled, spiDiskJobActive, spiTestNeedsReboot,
// spiTestStreamTaskHandle, spiTestInitialized, spiTestRx, spiTestTrans etc.

static constexpr uint16_t SPI_DISK_CACHE_SECTORS = 16;
static constexpr size_t SPI_DISK_CACHE_BYTES =
    SPI_DISK_CACHE_SECTORS * EbtksStream::PAYLOAD_BYTES;
// DO NOT reserve 8 KiB in .dram0.bss: the classic ESP32's static DRAM
// linker region is almost full. This buffer is allocated from the runtime
// heap on first SPI enable, retained for the boot, and NEVER freed while
// a core-0 DMA worker might still reference it.
static uint8_t *spiDiskCache = nullptr;
static char spiDiskCachePath[96] = {};
static uint32_t spiDiskCacheFirst = 0;
static uint16_t spiDiskCacheCount = 0;
static bool spiDiskCacheValid = false;
static volatile bool spiDiskBootDisabled = false;
static volatile bool spiDiskJobPending = false;
static SemaphoreHandle_t spiDiskDone = nullptr;
static TaskHandle_t spiDiskWorkerHandle = nullptr;
static uint16_t spiDiskNextSession = 0;
static char spiDiskJobPath[96] = {};
static uint32_t spiDiskJobSector = 0;
static uint16_t spiDiskJobCount = 0;
static uint16_t spiDiskJobSession = 0;
static volatile bool spiDiskJobSuccess = false;
static char spiDiskLastError[64] = {};
static volatile uint32_t spiDiskHits = 0;
static volatile uint32_t spiDiskMisses = 0;
static volatile uint32_t spiDiskBatches = 0;
static volatile uint32_t spiDiskFrames = 0;
static volatile uint32_t spiDiskFallbacks = 0;
static volatile uint32_t spiDiskReadFailures = 0;
static volatile uint32_t spiDiskTotalReadUs = 0;
static volatile uint32_t spiDiskLastBatchUs = 0;
static volatile uint32_t spiDiskLastBatchFrames = 0;
static volatile uint32_t spiDiskCacheInvalidations = 0;

// Allocate only when the operator enables SPI reads. With the original
// static 8192-byte array, dram0_0_seg overflowed by 6728 bytes at link.
// Keeping the pointer in .bss restores >8 KiB of link-time DRAM budget.
// Never use PSRAM or a DMA buffer for this cache: SPI driver owns its own
// DMA-capable receive buffers, and this is only a validated payload staging
// area. Keep substantial RAM for Z80, AsyncTCP, Wi-Fi and other workers.
inline bool spiDiskEnsureCache()
{
  if (spiDiskCache) return true;
  const size_t reserveAfterAlloc = 32768;
  const size_t minLargestBlock = SPI_DISK_CACHE_BYTES + 4096;
  if (ESP.getFreeHeap() < SPI_DISK_CACHE_BYTES + reserveAfterAlloc ||
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < minLargestBlock)
    return false;

  uint8_t *buffer = static_cast<uint8_t *>(
      heap_caps_malloc(SPI_DISK_CACHE_BYTES, MALLOC_CAP_8BIT));
  if (!buffer) return false;
  spiDiskCache = buffer;
  // Cache is never valid until a complete CRC/sequence-checked SPI batch
  // commits it; no need to clear uninitialized bytes here.
  return true;
}

inline void spiDiskInvalidateCache()
{
  spiDiskCacheValid = false;
  spiDiskCacheCount = 0;
  spiDiskCachePath[0] = 0;
  ++spiDiskCacheInvalidations;
}

inline void spiDiskSetError(const char *reason, bool dmaUncertain = false)
{
  // Retain the first useful failure reason instead of overwriting it with
  // the later aggregate-failure consequence. Count failing batches, not
  // the number of defensive checks reporting that same failed batch.
  if (!spiDiskLastError[0]) {
    strlcpy(spiDiskLastError, reason, sizeof(spiDiskLastError));
    ++spiDiskReadFailures;
  }
  if (dmaUncertain) spiTestNeedsReboot = true;
}

inline bool spiDiskDoBatch()
{
  const uint32_t startUs = micros();
  spiDiskJobSuccess = false;
  spiDiskLastBatchFrames = 0;
  spiDiskLastBatchUs = 0;
  spiDiskLastError[0] = 0;
  if (spiTestNeedsReboot || spiTestState == 1 || !spiDiskJobCount ||
      !spiDiskJobPath[0] || !spiTestInit(0)) {
    spiDiskSetError("SPI driver not ready or diagnostic busy");
    return false;
  }
  const uint16_t count = spiDiskJobCount;
  const uint16_t session = spiDiskJobSession;

  // PREPARE is a harmless, repeatable UART preflight: no SPI DMA is armed.
  JsonDocument prep;
  JsonObject pre = prep["spi_stream_prepare"].to<JsonObject>();
  pre["path"] = spiDiskJobPath;
  pre["first"] = spiDiskJobSector;
  pre["count"] = count;
  pre["session"] = session;
  String request, reply;
  serializeJson(prep, request);
  bool prepared = false;
  for (uint8_t attempt = 0; attempt < 2 && !prepared; ++attempt) {
    reply = "";
    const bool responded = ebtksLinkTransaction(request, reply, 3000);
    if (responded) {
      JsonDocument parsed;
      if (!deserializeJson(parsed, reply)) {
        JsonVariantConst result = parsed["spi_stream_prepare"];
        const char *protocol = result["ready_protocol"] | "";
        prepared = (strcmp(protocol, "poll_low_v2") == 0) &&
                   (bool)(result["ok"] | false) &&
                   ((int)(result["session"] | -1) == (int)session) &&
                   ((uint32_t)(result["first"] | UINT32_MAX) == spiDiskJobSector) &&
                   ((int)(result["count"] | -1) == count);
      }
    }
    if (!prepared && !attempt) vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!prepared) {
    spiDiskSetError("Teensy PREPARE rejected or protocol mismatch");
    return false;
  }

  // Serialize UART send/completion and its final CRC with the other ESP
  // UART clients. No legacy UART transaction can eat the stream completion.
  if (xSemaphoreTake(ebtksLinkMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    spiDiskSetError("UART mutex unavailable");
    return false;
  }
  bool ok = true;
  bool dmaUncertain = false;
  uint16_t aggregate = 0xFFFF;
  int firstBad = -1;
  const uint32_t bits = (uint32_t)EbtksStream::FRAME_BYTES * 8U;
  auto queue = [&]() -> bool {
    memset(spiTestRx, 0, EbtksStream::FRAME_BYTES);
    memset(&spiTestTrans, 0, sizeof(spiTestTrans));
    spiTestTrans.length = bits;
    spiTestTrans.rx_buffer = spiTestRx;
    spiTestTrans.tx_buffer = nullptr;
    gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
    return spi_slave_queue_trans(SPI_TEST_HOST, &spiTestTrans,
                                pdMS_TO_TICKS(1500)) == ESP_OK;
  };

  if (!queue()) {
    ok = false;
    dmaUncertain = true;
    spiDiskSetError("First SPI DMA queue failed", dmaUncertain);
  } else {
    while (TeensySerial.available()) TeensySerial.read();
    JsonDocument send;
    send["spi_stream_send"]["session"] = session;
    String message;
    serializeJson(send, message);
    TeensySerial.print(message);
    TeensySerial.write('\r');

    for (uint16_t block = 0; block < count; ++block) {
      spi_slave_transaction_t *done = nullptr;
      const esp_err_t err = spi_slave_get_trans_result(
          SPI_TEST_HOST, &done, pdMS_TO_TICKS(2500));
      if (err != ESP_OK || done != &spiTestTrans ||
          spiTestTrans.trans_len != bits) {
        if (firstBad < 0) firstBad = block;
        dmaUncertain = true;
        ok = false;
        spiDiskSetError(err != ESP_OK ? esp_err_to_name(err) :
                        "short or unexpected SPI DMA result", true);
        break;
      }
      ++spiDiskLastBatchFrames;
      const EbtksStream::Check check = EbtksStream::check(
          spiTestRx, EbtksStream::KIND_SD_READ, session, block);
      if (check != EbtksStream::Check::OK) {
        if (firstBad < 0) firstBad = block;
        ok = false;   // drain remaining frames; never accept partial batch
        spiDiskSetError(EbtksStream::checkName(check));
      } else {
        const uint8_t *data = spiTestRx + EbtksStream::HEADER_BYTES;
        memcpy(spiDiskCache + (size_t)block * EbtksStream::PAYLOAD_BYTES,
               data, EbtksStream::PAYLOAD_BYTES);
        aggregate = EbtksStream::crc16(data, EbtksStream::PAYLOAD_BYTES,
                                       aggregate);
      }

      if (block + 1 < count) {
        // Exact poll_low_v2 handshake validated with 128 sectors and live
        // HP-85 graphics. NEVER replace this with Teensy attachInterrupt:
        // custom EBTKS IRQ_GPIO6789 owns the HP bus interrupt vector.
        gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        if (!queue()) {
          ok = false;
          dmaUncertain = true;
          spiDiskSetError("Next SPI DMA queue failed", true);
          break;
        }
      }
    }

    if (!dmaUncertain) {
      String answer;
      const bool replied = spiStreamReadReplyLocked(answer, 3500);
      if (!replied) {
        ok = false;
        spiDiskSetError("SPI stream final UART reply missing");
      } else {
        JsonDocument report;
        if (deserializeJson(report, answer)) {
          ok = false;
          spiDiskSetError("SPI stream final UART JSON malformed");
        } else {
          JsonVariantConst result = report["spi_stream_send"];
          const uint16_t remoteCrc = (uint16_t)(result["aggregate_crc"] | 0);
          const int remoteSession = result["session"] | -1;
          const int remoteCount = result["count"] | -1;
          const int remoteFrames = result["frames"] | -1;
          const int remoteGood = result["good"] | -1;
          const bool remoteOk = result["ok"] | false;
          if (!remoteOk || remoteSession != session ||
              remoteCount != count || remoteFrames != count ||
              remoteGood != count || remoteCrc != aggregate || firstBad >= 0) {
            ok = false;
            spiDiskSetError("Stream aggregate CRC or completion mismatch");
          }
        }
      }
    }
  }
  xSemaphoreGive(ebtksLinkMutex);
  spiDiskLastBatchUs = (uint32_t)(micros() - startUs);
  spiDiskTotalReadUs += spiDiskLastBatchUs;
  if (ok && !dmaUncertain) {
    ++spiDiskBatches;
    spiDiskFrames += count;
    spiDiskLastError[0] = 0;
  }
  return ok && !dmaUncertain;
}

inline void spiDiskWorker(void *)
{
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const bool ok = spiDiskDoBatch();
    spiDiskJobSuccess = ok;
    if (!ok) {
      // Do not retry uncertain DMA state. Disable SPI for the remainder of
      // this ESP32 boot (UART fallback remains available).
      spiDiskBootDisabled = true;
      spiDiskReadsEnabled = false;
    }
    spiDiskJobPending = false;
    spiDiskJobActive = false;
    if (spiDiskDone) xSemaphoreGive(spiDiskDone);
  }
}

inline bool spiDiskEnsureWorker()
{
  if (!spiDiskDone) spiDiskDone = xSemaphoreCreateBinary();
  if (!spiDiskDone) return false;
  if (!spiDiskCache && !spiDiskEnsureCache()) return false;
  if (spiDiskWorkerHandle) return true;
  if (ESP.getFreeHeap() < 32000 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 16000)
    return false;
  return xTaskCreatePinnedToCore(spiDiskWorker, "SpiDisk", 8192,
                                nullptr, 1, &spiDiskWorkerHandle, 0) == pdPASS &&
         spiDiskWorkerHandle != nullptr;
}

inline bool spiDiskReadCached(const char *path, uint32_t sector,
                              uint32_t totalSectors, uint8_t *dst)
{
  if (!dst || !path || !spiDiskReadsEnabled || spiDiskBootDisabled ||
      spiTestNeedsReboot || spiTestState == 1 || spiDiskJobActive ||
      sector >= totalSectors) return false;
  // Defensive guard if a future caller bypasses the mode-switch allocation.
  if (!spiDiskCache && !spiDiskEnsureCache()) {
    spiDiskBootDisabled = true;
    spiDiskReadsEnabled = false;
    spiDiskSetError("Cannot allocate 8 KiB SPI disk cache");
    return false;
  }

  if (spiDiskCacheValid && !strcmp(spiDiskCachePath, path) &&
      sector >= spiDiskCacheFirst &&
      sector - spiDiskCacheFirst < spiDiskCacheCount) {
    memcpy(dst, spiDiskCache +
           (size_t)(sector - spiDiskCacheFirst) * EbtksStream::PAYLOAD_BYTES,
           EbtksStream::PAYLOAD_BYTES);
    ++spiDiskHits;
    return true;
  }
  ++spiDiskMisses;
  spiDiskInvalidateCache();
  if (!spiDiskEnsureWorker()) {
    spiDiskBootDisabled = true;
    spiDiskReadsEnabled = false;
    spiDiskSetError("Cannot allocate SPI disk worker");
    return false;
  }

  // Fetch aligned 16-sector windows when entirely in the exposed logical
  // medium. Near the end use a single sector (Teensy supports 1/16/128).
  const uint32_t aligned = sector & ~(uint32_t)(SPI_DISK_CACHE_SECTORS - 1);
  const bool fullWindow = totalSectors - aligned >= SPI_DISK_CACHE_SECTORS;
  const uint32_t first = fullWindow ? aligned : sector;
  const uint16_t count = fullWindow ? SPI_DISK_CACHE_SECTORS : 1;
  strlcpy(spiDiskJobPath, path, sizeof(spiDiskJobPath));
  spiDiskJobSector = first;
  spiDiskJobCount = count;
  spiDiskJobSession = ++spiDiskNextSession;
  if (!spiDiskJobSession) spiDiskJobSession = ++spiDiskNextSession;

  // Drain any stale signal from a previous completed request.
  while (xSemaphoreTake(spiDiskDone, 0) == pdTRUE) {}
  spiDiskJobActive = true;
  spiDiskJobPending = true;
  spiDiskJobSuccess = false;
  xTaskNotifyGive(spiDiskWorkerHandle);

  // Keep storage buffer alive if this ever times out (static cache storage).
  // A timeout disables all further SPI reads for this boot; the worker may
  // still own a DMA descriptor, which must not be reused.
  if (xSemaphoreTake(spiDiskDone, pdMS_TO_TICKS(8500)) != pdTRUE) {
    spiDiskBootDisabled = true;
    spiDiskReadsEnabled = false;
    spiTestNeedsReboot = true;
    spiDiskSetError("SPI disk worker completion timeout", true);
    return false;
  }
  if (!spiDiskJobSuccess) return false;

  spiDiskCacheFirst = first;
  spiDiskCacheCount = count;
  strlcpy(spiDiskCachePath, path, sizeof(spiDiskCachePath));
  spiDiskCacheValid = true;
  memcpy(dst, spiDiskCache + (size_t)(sector-first)*EbtksStream::PAYLOAD_BYTES,
         EbtksStream::PAYLOAD_BYTES);
  return true;
}

inline bool spiDiskSetEnabled(bool requested)
{
  // The read-ahead allocation is intentionally retained after disable:
  // mode changes don't fragment heap or free a buffer possibly in use
  // by the Z80 or the persistent core-0 worker.
  if (!requested) {
    spiDiskReadsEnabled = false;
    spiDiskInvalidateCache();
    return true;
  }
  if (spiDiskBootDisabled || spiTestNeedsReboot ||
      spiDiskJobActive || spiTestState == 1) return false;

  // Refuse the switch instead of reporting SPI enabled when memory is short.
  // No DMA, UART command, image read or Z80 RAM modification has occurred.
  if (!spiDiskEnsureCache()) {
    spiDiskReadsEnabled = false;
    strlcpy(spiDiskLastError, "Cannot allocate 8 KiB SPI cache; UART remains active",
            sizeof(spiDiskLastError));
    return false;
  }
  spiDiskReadsEnabled = true;
  spiDiskInvalidateCache();
  return true;
}

inline void spiDiskRegister(AsyncWebServer &server)
{
  server.on("/api/cpm/spireads", HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument doc;
    doc["enabled"] = (bool)spiDiskReadsEnabled;
    doc["boot_disabled"] = (bool)spiDiskBootDisabled;
    doc["needs_reboot"] = (bool)spiTestNeedsReboot;
    doc["active"] = (bool)spiDiskJobActive;
    doc["cache_valid"] = (bool)spiDiskCacheValid;
    doc["cache_allocated"] = spiDiskCache != nullptr;
    doc["cache_bytes"] = spiDiskCache ? (uint32_t)SPI_DISK_CACHE_BYTES : 0U;
    doc["cache_first"] = (uint32_t)spiDiskCacheFirst;
    doc["cache_count"] = (unsigned)spiDiskCacheCount;
    doc["cache_hits"] = (uint32_t)spiDiskHits;
    doc["cache_misses"] = (uint32_t)spiDiskMisses;
    doc["batch_reads"] = (uint32_t)spiDiskBatches;
    doc["frames"] = (uint32_t)spiDiskFrames;
    doc["uart_fallbacks"] = (uint32_t)spiDiskFallbacks;
    doc["spi_failures"] = (uint32_t)spiDiskReadFailures;
    doc["cache_invalidations"] = (uint32_t)spiDiskCacheInvalidations;
    doc["last_batch_us"] = (uint32_t)spiDiskLastBatchUs;
    doc["cumulative_batch_us"] = (uint32_t)spiDiskTotalReadUs;
    doc["free_heap"] = (uint32_t)ESP.getFreeHeap();
    doc["last_batch_frames"] = (uint32_t)spiDiskLastBatchFrames;
    doc["last_error"] = spiDiskLastError;
    doc["clock_hz"] = 4000000;
    doc["spi_mode"] = 0;
    doc["read_ahead_sectors"] = SPI_DISK_CACHE_SECTORS;
    doc["writes"] = "UART";
    String output;
    serializeJson(doc, output);
    r->send(200, "application/json", output);
  });
  server.on("/api/cpm/spireads", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!r->hasParam("enabled")) {
      r->send(400, "text/plain", "Provide ?enabled=0 or ?enabled=1");
      return;
    }
    const String value = r->getParam("enabled")->value();
    if (value != "0" && value != "1") {
      r->send(400, "text/plain", "enabled must be 0 or 1");
      return;
    }
    if (value == "1" && (spiDiskBootDisabled || spiTestNeedsReboot ||
                         spiDiskJobActive || spiTestState == 1)) {
      r->send(409, "text/plain", "SPI unavailable/busy; reboot after error");
      return;
    }
    if (value == "0" && spiDiskJobActive) {
      r->send(409, "text/plain", "Wait for in-flight SPI read to finish");
      return;
    }
    if (!spiDiskSetEnabled(value == "1")) {
      r->send(503, "text/plain", spiDiskLastError[0] ?
              spiDiskLastError : "SPI mode switch refused");
      return;
    }
    r->send(200, "application/json", spiDiskReadsEnabled ?
            "{\"ok\":true,\"enabled\":true}" :
            "{\"ok\":true,\"enabled\":false}");
  });
}
