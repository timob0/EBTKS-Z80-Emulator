#pragma once
// EXPERIMENTAL, opt-in CN3 SPI0 Teensy-master / SPI2 ESP32-slave test.
// Independent 512-byte DMA transactions, optional full-duplex with two CRCs.
// Per-sector UART control, no CP/M disk or SD access.
// No CP/M disk, SD or normal storage transport is altered.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <driver/spi_slave.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "spi_stream_frame.h"

static constexpr spi_host_device_t SPI_TEST_HOST = HSPI_HOST;
static constexpr int SPI_TEST_MOSI = 22;   // CN3-21
static constexpr int SPI_TEST_MISO = 17;   // CN3-17 on tested board; WROOM pad 28
static constexpr int SPI_TEST_SCLK = 16;   // CN3-14
static constexpr int SPI_TEST_CS = 32;     // CN3-4, external 10k pull-up
static constexpr int SPI_TEST_READY = 27;  // CN3-8
static constexpr size_t SPI_TEST_BYTES = 512;
static constexpr size_t SPI_TEST_DMA_CAPACITY = EbtksStream::FRAME_BYTES;
static constexpr uint16_t SPI_TEST_MAX_SECTORS = 128;

// DMA buffer + descriptor remain allocated for the lifetime of the firmware.
// If a queued DMA transaction never finishes, retries require a reboot.
static uint8_t *spiTestRx = nullptr;
static uint8_t *spiTestTx = nullptr; // DMA-capable and word-aligned, like RX
static spi_slave_transaction_t spiTestTrans = {};
static bool spiTestInitialized = false;
static uint8_t spiTestInitializedMode = 255; // mode is fixed at slave init
static volatile bool spiTestNeedsReboot = false;
static volatile int spiTestState = 0; // 0 idle, 1 running, 2 passed, 3 failed
static volatile uint32_t spiTestRunNumber = 0;
static volatile uint32_t spiTestHz = 1000000;
static volatile uint16_t spiTestSectors = 1;
static volatile uint8_t spiTestPattern = 0; // 0..4 fixed; 5=cycle by sector
static volatile bool spiTestBidi = false; // false keeps original MOSI-only test
static volatile uint8_t spiTestMode = 0;  // 8 MHz mode 0 hardware-tested; mode 1 selectable
static volatile bool spiTestGpioMode = false; // separate GPIO-only isolation check
static volatile bool spiTestSdMode = false; // separate SD-to-SPI read-only diagnostic
static volatile bool spiTestStreamMode = false; // batched, CRC-framed diagnostic
static volatile uint16_t spiTestStreamSession = 0;
static volatile uint16_t spiTestStreamFrames = 0;
static volatile uint16_t spiTestStreamBadFrames = 0;
static volatile uint32_t spiTestStreamWireBytes = 0;
static char spiTestStreamError[40] = {};
// Detailed DMA diagnostics distinguish short frame, ESP-IDF driver error,
// and unexpected descriptor pointer; previously all said "DMA incomplete".
static volatile int spiTestStreamDmaErr = 0;
static volatile uint32_t spiTestStreamDmaBits = 0;
static volatile bool spiTestStreamDmaDescriptorMatch = false;
static volatile int spiTestStreamReadyPin = -1;
static char spiTestStreamDmaCause[48] = {};
static volatile uint32_t spiTestStreamReadyLowPhases = 0;
static volatile uint32_t spiTestStreamReadyWaitUs = 0;
static volatile uint32_t spiTestStreamReadyMaxWaitUs = 0;
static volatile uint8_t spiTestStreamReadyWaitFailure = 0;
// One persistent worker avoids repeated FreeRTOS task-stack allocation and
// deferred vTaskDelete heap reclamation after every browser-triggered run.
static TaskHandle_t spiTestStreamTaskHandle = nullptr;
static volatile uint32_t spiTestStreamRunsStarted = 0;
static volatile uint32_t spiTestStreamLaunchFailures = 0;
static volatile uint32_t spiTestStreamPrepareParseFailures = 0;
static volatile uint32_t spiTestHeapAtLaunch = 0;
static volatile uint32_t spiTestHeapAtFinish = 0;
static volatile uint32_t spiTestHeapLargestBlock = 0;
static volatile uint32_t spiTestStreamStackFree = 0;
static char spiTestPrepareReplySample[192] = {};
static char spiTestPrepareError[32] = {};
static char spiTestSdPath[120] = "/cpm/disks/A.dsk";
static volatile uint32_t spiTestSdFirstSector = 0;
static volatile uint32_t spiTestSdReadUs = 0;
static volatile uint32_t spiTestSdFileSize = 0;
static volatile uint16_t spiTestSdPreparedCrc = 0;
static volatile uint16_t spiTestSdSentCrc = 0;
static char spiTestSdFirst16Hex[33] = {}; // first 16 actual SD bytes received
static volatile int spiTestGpioLow = -1;
static volatile int spiTestGpioHigh = -1;
static char spiTestMisoHeadHex[33] = {}; // first 16 actual Teensy RX bytes
static volatile uint16_t spiTestCompleted = 0;
static volatile int spiTestFailSector = -1;
static volatile int spiTestFailPattern = -1;
static volatile int spiTestMismatchAt = -1;
static volatile uint16_t spiTestCrcLocal = 0;
static volatile uint16_t spiTestCrcRemote = 0;
static volatile uint16_t spiTestMisoTxCrc = 0; // ESP32 DMA TX CRC
static volatile uint16_t spiTestMisoRxCrc = 0; // Teensy SPI RX CRC
static volatile int spiTestMisoMismatchAt = -1; // observed by Teensy
static volatile uint16_t spiTestAggregateCrc = 0xFFFF;
static volatile uint16_t spiTestAggregateMisoTxCrc = 0xFFFF;
static volatile uint32_t spiTestElapsedUs = 0;  // sum of Teensy's loop times
static volatile uint32_t spiTestWallUs = 0;     // includes UART coordination
static volatile uint32_t spiTestMinUs = 0;
static volatile uint32_t spiTestMaxUs = 0;
static const char *volatile spiTestDetail = "Not tested";

static void IRAM_ATTR spiTestPostSetup(spi_slave_transaction_t *)
{
  gpio_set_level((gpio_num_t)SPI_TEST_READY, 1);
}
static void IRAM_ATTR spiTestPostTrans(spi_slave_transaction_t *)
{
  gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
}

inline bool spiTestValidHz(uint32_t hz)
{
  return hz == 1000000UL || hz == 2000000UL ||
         hz == 4000000UL || hz == 8000000UL;
}

// Must match Teensy's independent generator. Pattern 0, block 0 is the
// exact original test pattern (0xA5 XOR low8(i*37 + seq)).
inline uint8_t spiTestByte(uint16_t index, uint8_t seq,
                           uint16_t block, uint8_t pattern)
{
  switch (pattern) {
    case 0: return (uint8_t)(0xA5U ^
                            (uint8_t)(index * 37U + seq + block * 13U));
    case 1: return 0x00;
    case 2: return 0xFF;
    case 3: return (index & 1U) ? 0xAA : 0x55;
    case 4: return (uint8_t)(1U << ((index + block) & 7U));
    default: return 0;
  }
}

// Must match Teensy's independent MISO expectation. Reverse patterns
// intentionally differ from MOSI, even for all-zero/all-FF selections.
inline uint8_t spiTestReverseByte(uint16_t index, uint8_t seq,
                                  uint16_t block, uint8_t pattern)
{
  return (uint8_t)(spiTestByte(index, (uint8_t)(seq ^ 0xA7U),
                              block, pattern) ^
                   (uint8_t)(0x5AU + index * 11U + block * 7U));
}

inline uint16_t spiTestCrcByte(uint16_t crc, uint8_t value)
{
  crc ^= value;
  for (unsigned j = 0; j < 8; ++j)
    crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
  return crc;
}

inline bool spiTestInit(uint8_t mode)
{
  if (mode > 1) return false;
  if (spiTestInitialized && spiTestInitializedMode == mode) return true;
  pinMode(SPI_TEST_READY, OUTPUT);
  digitalWrite(SPI_TEST_READY, LOW);

  // The slave SPI mode cannot change in the middle of a transaction.
  // A new mode is selected only for a new worker, after ALL prior DMA
  // transactions have completed. Incomplete DMA blocks further testing.
  if (spiTestInitialized) {
    const esp_err_t released = spi_slave_free(SPI_TEST_HOST);
    if (released != ESP_OK) {
      spiTestDetail = "Could not reconfigure SPI mode; reboot ESP32";
      spiTestNeedsReboot = true;
      Serial.printf("SPI test: slave_free failure: %s\n",
                    esp_err_to_name(released));
      return false;
    }
    spiTestInitialized = false;
    spiTestInitializedMode = 255;
  }

  if (!spiTestRx) spiTestRx = static_cast<uint8_t *>(
      heap_caps_malloc(SPI_TEST_DMA_CAPACITY, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!spiTestRx) {
    spiTestDetail = "ESP32 RX DMA allocation failed";
    return false;
  }
  if (!spiTestTx) spiTestTx = static_cast<uint8_t *>(
      heap_caps_malloc(SPI_TEST_DMA_CAPACITY, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!spiTestTx) {
    spiTestDetail = "ESP32 TX DMA allocation failed";
    heap_caps_free(spiTestRx);
    spiTestRx = nullptr;
    return false;
  }
  spi_bus_config_t bus = {};
  bus.mosi_io_num = SPI_TEST_MOSI;
  bus.miso_io_num = SPI_TEST_MISO;
  bus.sclk_io_num = SPI_TEST_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = SPI_TEST_DMA_CAPACITY;

  spi_slave_interface_config_t slave = {};
  slave.spics_io_num = SPI_TEST_CS;
  slave.mode = mode;
  slave.queue_size = 1;
  slave.post_setup_cb = spiTestPostSetup;
  slave.post_trans_cb = spiTestPostTrans;

  const esp_err_t err = spi_slave_initialize(
      SPI_TEST_HOST, &bus, &slave, SPI_DMA_CH1);
  if (err != ESP_OK) {
    Serial.printf("SPI test: slave init failure: %s\n", esp_err_to_name(err));
    spiTestDetail = "ESP32 SPI2 slave initialization failed";
    heap_caps_free(spiTestRx);
    spiTestRx = nullptr;
    heap_caps_free(spiTestTx);
    spiTestTx = nullptr;
    return false;
  }
  spiTestInitialized = true;
  spiTestInitializedMode = mode;
  Serial.printf("SPI test: SPI2 slave initialized mode=%u (GPIO22/17/16 CS=32 READY=27)\n",
                (unsigned)mode);
  return true;
}

inline void spiTestFailure(const char *why, bool requireReboot = false)
{
  spiTestDetail = why;
  if (requireReboot) spiTestNeedsReboot = true;
  spiTestState = 3;
}

inline void spiTestWorker(void *)
{
  spiTestCompleted = 0;
  spiTestFailSector = -1;
  spiTestFailPattern = -1;
  spiTestMismatchAt = -1;
  spiTestCrcLocal = 0;
  spiTestCrcRemote = 0;
  spiTestMisoTxCrc = 0;
  spiTestMisoRxCrc = 0;
  spiTestMisoMismatchAt = -1;
  spiTestMisoHeadHex[0] = '\0';
  spiTestAggregateCrc = 0xFFFF;
  spiTestAggregateMisoTxCrc = 0xFFFF;
  spiTestElapsedUs = 0;
  spiTestWallUs = 0;
  spiTestMinUs = 0;
  spiTestMaxUs = 0;
  spiTestDetail = "Initializing SPI2 slave";

  const uint8_t mode = spiTestMode;
  if (!spiTestInit(mode)) {
    spiTestState = 3;
    vTaskDelete(nullptr);
    return;
  }

  const uint32_t hz = spiTestHz;
  const uint16_t sectors = spiTestSectors;
  const uint8_t patternMode = spiTestPattern;
  const bool bidi = spiTestBidi;
  const uint32_t startUs = micros();
  spiTestDetail = "Transferring and verifying 512-byte sectors";

  // Each sector is separately DMA-armed, clocked and acknowledged over UART.
  // This deliberately does not rely on timing a READY-low pulse between
  // back-to-back queued DMA descriptors.
  for (uint16_t block = 0; block < sectors; ++block) {
    const uint8_t seq = (uint8_t)((spiTestRunNumber + block) & 0xFFU);
    const uint8_t pattern = (patternMode == 5) ? (uint8_t)(block % 5U) : patternMode;
    spiTestFailSector = (int)block;
    spiTestFailPattern = (int)pattern;
    spiTestMisoMismatchAt = -1;
    spiTestMisoTxCrc = 0;
    spiTestMisoRxCrc = 0;
    memset(spiTestRx, 0, SPI_TEST_BYTES);

    // DMA owns TX/RX memory from queue_trans through get_trans_result.
    // Prepare *before* arming READY. Never modify TX while DMA is active.
    uint16_t txCrc = 0xFFFF;
    if (bidi) {
      for (size_t i = 0; i < SPI_TEST_BYTES; ++i) {
        const uint8_t value = spiTestReverseByte((uint16_t)i, seq, block, pattern);
        spiTestTx[i] = value;
        txCrc = spiTestCrcByte(txCrc, value);
      }
      spiTestMisoTxCrc = txCrc;
    }
    memset(&spiTestTrans, 0, sizeof(spiTestTrans));
    spiTestTrans.length = SPI_TEST_BYTES * 8;
    spiTestTrans.rx_buffer = spiTestRx;
    spiTestTrans.tx_buffer = bidi ? spiTestTx : nullptr;

    gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
    const esp_err_t queued = spi_slave_queue_trans(
        SPI_TEST_HOST, &spiTestTrans, pdMS_TO_TICKS(1000));
    if (queued != ESP_OK) {
      spiTestFailure("Could not queue SPI DMA transaction; reboot before retry", true);
      Serial.printf("SPI test: queue failure at block %u: %s\n",
                    block, esp_err_to_name(queued));
      break;
    }

    // The DMA transaction is queued BEFORE the Teensy receives this command.
    JsonDocument req;
    JsonObject probe = req["spi_probe"].to<JsonObject>();
    probe["seq"] = seq;
    probe["len"] = SPI_TEST_BYTES;
    probe["hz"] = hz;
    probe["block"] = block;
    probe["pattern"] = pattern;
    probe["bidi"] = bidi;
    probe["mode"] = mode;
    String message;
    serializeJson(req, message);
    String response;
    const bool replied = ebtksLinkTransaction(message, response, 3000);

    spi_slave_transaction_t *done = nullptr;
    const esp_err_t received = spi_slave_get_trans_result(
        SPI_TEST_HOST, &done, pdMS_TO_TICKS(200));
    if (received != ESP_OK || done != &spiTestTrans) {
      // The peripheral may still own the descriptor and buffer.
      spiTestFailure("DMA incomplete; reboot ESP32 before retry", true);
      Serial.printf("SPI test: DMA incomplete at block %u: %s; UART=%s\n",
                    block, esp_err_to_name(received),
                    replied ? response.c_str() : "<timeout>");
      break;
    }
    if (spiTestTrans.trans_len != SPI_TEST_BYTES * 8) {
      spiTestFailure("SPI transaction received wrong number of bits");
      Serial.printf("SPI test: block %u got %u bits (expected 4096)\n",
                    block, (unsigned)spiTestTrans.trans_len);
      break;
    }

    uint16_t actualCrc = 0xFFFF, expectedCrc = 0xFFFF;
    int mismatch = -1;
    for (size_t i = 0; i < SPI_TEST_BYTES; ++i) {
      const uint8_t expected = spiTestByte((uint16_t)i, seq, block, pattern);
      actualCrc = spiTestCrcByte(actualCrc, spiTestRx[i]);
      expectedCrc = spiTestCrcByte(expectedCrc, expected);
      spiTestAggregateCrc = spiTestCrcByte(spiTestAggregateCrc, spiTestRx[i]);
      if (mismatch == -1 && spiTestRx[i] != expected) mismatch = (int)i;
    }
    spiTestCrcLocal = actualCrc;
    spiTestMismatchAt = mismatch;

    if (!replied) {
      spiTestFailure("SPI received, but Teensy UART result timed out");
      break;
    }
    JsonDocument reply;
    if (deserializeJson(reply, response)) {
      spiTestFailure("Malformed Teensy SPI probe UART reply");
      break;
    }
    const bool ok = reply["spi_probe"]["ok"] | false;
    const int remoteSeq = reply["spi_probe"]["seq"] | -1;
    const int remoteLen = reply["spi_probe"]["len"] | -1;
    const int remoteBlock = reply["spi_probe"]["block"] | -1;
    const int remotePattern = reply["spi_probe"]["pattern"] | -1;
    const uint32_t remoteHz = reply["spi_probe"]["hz"] | 0UL;
    const bool remoteBidi = reply["spi_probe"]["bidi"] | false;
    const int remoteMode = reply["spi_probe"]["mode"] | -1;
    const char *misoHead = reply["spi_probe"]["miso_head_hex"] | "";
    strlcpy(spiTestMisoHeadHex, misoHead, sizeof(spiTestMisoHeadHex));
    const uint16_t remoteCrc = (uint16_t)(reply["spi_probe"]["crc"] | 0);
    const uint16_t remoteMisoCrc = (uint16_t)(reply["spi_probe"]["miso_crc"] | 0);
    const int remoteMisoMismatch = reply["spi_probe"]["miso_first_mismatch"] | -2;
    const uint32_t elapsed = reply["spi_probe"]["us"] | 0UL;
    spiTestCrcRemote = remoteCrc;
    spiTestMisoRxCrc = bidi ? remoteMisoCrc : 0;
    spiTestMisoMismatchAt = bidi ? remoteMisoMismatch : -1;

    // Each direction is verified independently: ESP sees actual MOSI DMA
    // bytes and Teensy reports the actual MISO SPI.transfer() receive CRC.
    // The independent first-mismatch comparisons catch CRC collisions too.
    const bool mosiOk = mismatch < 0 && actualCrc == expectedCrc &&
                        actualCrc == remoteCrc;
    const bool misoOk = !bidi ||
                        (remoteMisoMismatch == -1 && remoteMisoCrc == txCrc);
    if (!ok || remoteSeq != seq || remoteLen != (int)SPI_TEST_BYTES ||
        remoteBlock != block || remotePattern != pattern || remoteHz != hz ||
        remoteBidi != bidi || remoteMode != mode ||
        !mosiOk || !misoOk || elapsed == 0) {
      spiTestFailure("SPI MOSI/MISO bytes, CRC or Teensy status mismatch");
      Serial.printf("SPI test FAIL block=%u pattern=%u hz=%lu mode=%u seq=%u "
                    "len=%d bits=%u MOSI ESP=%04X Teensy=%04X expected=%04X at=%d "
                    "MISO ESP_TX=%04X Teensy_RX=%04X at=%d bidi=%u MISO_HEAD=%s\n",
                    block, pattern, (unsigned long)hz, (unsigned)mode,
                    seq, remoteLen, (unsigned)spiTestTrans.trans_len,
                    actualCrc, remoteCrc, expectedCrc, mismatch,
                    bidi ? txCrc : 0, remoteMisoCrc, remoteMisoMismatch,
                    bidi ? 1U : 0U, spiTestMisoHeadHex);
      break;
    }

    if (bidi) {
      for (size_t i = 0; i < SPI_TEST_BYTES; ++i)
        spiTestAggregateMisoTxCrc = spiTestCrcByte(
            spiTestAggregateMisoTxCrc, spiTestTx[i]);
    }
    spiTestElapsedUs += elapsed;
    if (block == 0 || elapsed < spiTestMinUs) spiTestMinUs = elapsed;
    if (elapsed > spiTestMaxUs) spiTestMaxUs = elapsed;
    ++spiTestCompleted;
    spiTestMismatchAt = -1;
    spiTestFailSector = -1;
    spiTestFailPattern = -1;
  }
  spiTestWallUs = (uint32_t)(micros() - startUs);
  if (spiTestCompleted == sectors && spiTestState == 1) {
    spiTestDetail = bidi ? "Both SPI directions verified; no CP/M disk accessed"
                         : "All SPI bytes verified; no CP/M disk accessed";
    spiTestState = 2;
    Serial.printf("SPI test PASS %u x 512 bytes @ %lu Hz, bidi=%u "
                  "Teensy sum=%lu us wall=%lu us\n",
                  sectors, (unsigned long)hz, bidi ? 1U : 0U,
                  (unsigned long)spiTestElapsedUs, (unsigned long)spiTestWallUs);
  } else {
    Serial.printf("SPI test FAILED after %u/%u sectors @ %lu Hz, sector=%d: %s\n",
                  spiTestCompleted, sectors, (unsigned long)hz,
                  (int)spiTestFailSector, spiTestDetail);
  }
  vTaskDelete(nullptr);
}

// GPIO-only isolation test; no SCLK/CS pulses, no SPI DMA or disk I/O.
// The UART handler on Teensy temporarily makes pin 12 a plain GPIO input.
// ESP32 GPIO17 is released from the SPI slave driver before GPIO drive.
// The two software samples decide whether the conductor/pin path works.
inline bool spiTestGpioCheckLevel(int level, int &sample)
{
  digitalWrite(SPI_TEST_MISO, level ? HIGH : LOW);
  vTaskDelay(pdMS_TO_TICKS(10)); // settle before serialized Teensy request
  JsonDocument req;
  JsonObject p = req["spi_gpio_probe"].to<JsonObject>();
  p["level"] = level;
  String message;
  serializeJson(req, message);
  String response;
  if (!ebtksLinkTransaction(message, response, 1500)) return false;
  JsonDocument reply;
  if (deserializeJson(reply, response)) return false;
  sample = reply["spi_gpio_probe"]["read"] | -1;
  const int echoed = reply["spi_gpio_probe"]["level"] | -1;
  return echoed == level && sample >= 0 && sample <= 1;
}

inline void spiTestGpioWorker(void *)
{
  const uint32_t started = micros();
  spiTestDetail = "GPIO MISO test: releasing ESP32 SPI2";
  spiTestGpioLow = -1;
  spiTestGpioHigh = -1;
  spiTestCompleted = 0;
  spiTestFailSector = -1;
  spiTestMismatchAt = -1;
  spiTestMisoMismatchAt = -1;
  spiTestCrcLocal = 0;
  spiTestCrcRemote = 0;
  spiTestMisoTxCrc = 0;
  spiTestMisoRxCrc = 0;
  spiTestElapsedUs = 0;
  spiTestMinUs = 0;
  spiTestMaxUs = 0;
  spiTestWallUs = 0;
  spiTestMisoHeadHex[0] = '\0';

  // Never free a slave while DMA owns a transaction. The API refuses
  // running concurrent tests, and incomplete SPI DMA sets needs_reboot.
  if (spiTestInitialized) {
    const esp_err_t err = spi_slave_free(SPI_TEST_HOST);
    if (err != ESP_OK) {
      spiTestFailure("GPIO test: SPI2 release failed; reboot required", true);
      Serial.printf("SPI GPIO probe: spi_slave_free failed: %s\n",
                    esp_err_to_name(err));
      vTaskDelete(nullptr);
      return;
    }
    spiTestInitialized = false;
    spiTestInitializedMode = 255;
  }

  // SPI driver is now detached from GPIO17. Drive it as an ordinary output
  // for two separate UART-triggered Teensy digital input samples.
  pinMode(SPI_TEST_MISO, OUTPUT);
  digitalWrite(SPI_TEST_MISO, LOW);
  spiTestDetail = "GPIO MISO test: sampling LOW then HIGH";
  int low = -1, high = -1;
  const bool lowReply = spiTestGpioCheckLevel(0, low);
  const bool highReply = spiTestGpioCheckLevel(1, high);
  spiTestGpioLow = low;
  spiTestGpioHigh = high;

  // Return this pin to high impedance. The next SPI run reinitializes SPI2
  // and its GPIO matrix mapping. Avoid leaving GPIO17 driven after a test.
  digitalWrite(SPI_TEST_MISO, LOW);
  pinMode(SPI_TEST_MISO, INPUT);
  spiTestWallUs = (uint32_t)(micros() - started);

  if (!lowReply || !highReply) {
    spiTestFailure("GPIO MISO test: Teensy UART sample/status failed");
  } else if (low != 0 || high != 1) {
    spiTestFailure("GPIO MISO test: pin 12 did not follow ESP GPIO17 LOW/HIGH");
  } else {
    spiTestDetail = "GPIO17 -> Teensy pin 12 LOW/HIGH passed; test SPI TX next";
    spiTestState = 2;
  }
  Serial.printf("SPI GPIO MISO probe: low=%d (%u) high=%d (%u) status=%s\n",
                low, lowReply ? 1U : 0U, high, highReply ? 1U : 0U,
                spiTestDetail);
  vTaskDelete(nullptr);
}

inline bool spiTestGpioStart()
{
  if (spiTestState == 1 || spiTestNeedsReboot) return false;
  spiTestState = 1;
  spiTestGpioMode = true;
  spiTestSdMode = false;
  spiTestStreamMode = false;
  spiTestHz = 0;
  spiTestSectors = 0;
  spiTestPattern = 0;
  spiTestBidi = false;
  ++spiTestRunNumber;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      spiTestGpioWorker, "SpiMisoGpio", 4096, nullptr, 1, nullptr, 0);
  if (ok != pdPASS) {
    spiTestFailure("Could not create GPIO MISO worker");
    return false;
  }
  return true;
}

// Real SD read-only sector diagnostic. This is NOT a CP/M disk transport.
// It uses two UART commands per sector: prepare (SD O_RDONLY open/seek/read
// and CRC before DMA), then send (only after ESP32 queues 512-byte RX DMA).
// No Base64 data and no writes anywhere in this test. The path is restricted
// to a single file below /cpm/disks; no CP/M image is mounted or changed.
inline bool spiTestSdPathValid(const String &path)
{
  const String root = "/cpm/disks/";
  return path.startsWith(root) && path.length() > root.length() &&
         path.length() < sizeof(spiTestSdPath) &&
         path.indexOf("..") < 0 &&
         path.indexOf('/', root.length()) < 0 &&
         path.indexOf('\\', root.length()) < 0;
}

inline void spiTestSdWorker(void *)
{
  spiTestDetail = "Initializing SPI for read-only SD sector test";
  spiTestCompleted = 0;
  spiTestFailSector = -1;
  spiTestFailPattern = -1;
  spiTestMismatchAt = -1;
  spiTestMisoMismatchAt = -1;
  spiTestCrcLocal = 0;
  spiTestCrcRemote = 0;
  spiTestAggregateCrc = 0xFFFF;
  spiTestMisoTxCrc = 0;
  spiTestMisoRxCrc = 0;
  spiTestAggregateMisoTxCrc = 0xFFFF;
  spiTestMisoHeadHex[0] = '\0';
  spiTestSdFirst16Hex[0] = '\0';
  spiTestSdPreparedCrc = 0;
  spiTestSdSentCrc = 0;
  spiTestSdReadUs = 0;
  spiTestSdFileSize = 0;
  spiTestWallUs = 0;
  spiTestElapsedUs = 0;
  spiTestMinUs = 0;
  spiTestMaxUs = 0;

  // The test always uses MODE0 / 4 MHz, regardless of synthetic-test UI.
  if (!spiTestInit(0)) {
    spiTestFailure("SD SPI test: ESP32 slave init failed");
    vTaskDelete(nullptr);
    return;
  }
  const uint16_t sectors = spiTestSectors;
  const uint32_t first = spiTestSdFirstSector;
  const uint32_t started = micros();
  spiTestDetail = "Reading real SD sectors via SPI (4 MHz, read-only)";

  for (uint16_t block = 0; block < sectors; ++block) {
    const uint8_t seq = (uint8_t)((spiTestRunNumber + block) & 0xFFU);
    const uint32_t absoluteSector = first + block;
    spiTestFailSector = (int)block;

    // Stage 1: read the SD file before committing a DMA descriptor.
    // On open/EOF errors no SPI DMA is queued: safe to retry with a new path.
    JsonDocument prepReq;
    JsonObject p = prepReq["spi_sd_prepare"].to<JsonObject>();
    p["seq"] = seq;
    p["sector"] = absoluteSector;
    p["path"] = spiTestSdPath;
    String req, response;
    serializeJson(prepReq, req);
    const bool prepReplied = ebtksLinkTransaction(req, response, 3000);
    if (!prepReplied) {
      spiTestFailure("SD prepare UART timeout (no DMA was queued)");
      Serial.printf("SPI SD prepare UART timeout: path=%s sector=%lu\n",
                    spiTestSdPath, (unsigned long)absoluteSector);
      break;
    }
    JsonDocument prep;
    if (deserializeJson(prep, response)) {
      spiTestFailure("Malformed Teensy SD prepare reply (no DMA queued)");
      break;
    }
    JsonVariantConst pr = prep["spi_sd_prepare"];
    const bool prepared = pr["ok"] | false;
    const int prepSeq = pr["seq"] | -1;
    const uint32_t prepSector = pr["sector"] | UINT32_MAX;
    const int prepLen = pr["len"] | -1;
    const uint16_t preparedCrc = (uint16_t)(pr["crc"] | 0);
    const uint32_t sdUs = pr["sd_us"] | 0UL;
    const uint32_t fileSize = pr["file_size"] | 0UL;
    const char *why = pr["reason"] | "unknown";
    spiTestSdPreparedCrc = preparedCrc;
    spiTestSdFileSize = fileSize;
    spiTestSdReadUs += sdUs;
    if (!prepared || prepSeq != seq || prepSector != absoluteSector ||
        prepLen != (int)SPI_TEST_BYTES || sdUs == 0) {
      spiTestFailure("SD file read failed or invalid prepare reply (no DMA queued)");
      Serial.printf("SPI SD prepare failed: %s sector=%lu reason=%s len=%d\n",
                    spiTestSdPath, (unsigned long)absoluteSector, why, prepLen);
      break;
    }

    // Stage 2: queue DMA THEN ask Teensy to clock its prepared sector.
    memset(spiTestRx, 0, SPI_TEST_BYTES);
    memset(&spiTestTrans, 0, sizeof(spiTestTrans));
    spiTestTrans.length = SPI_TEST_BYTES * 8;
    spiTestTrans.rx_buffer = spiTestRx; // Teensy -> ESP32; RX only
    spiTestTrans.tx_buffer = nullptr;
    gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
    const esp_err_t queued = spi_slave_queue_trans(
        SPI_TEST_HOST, &spiTestTrans, pdMS_TO_TICKS(1000));
    if (queued != ESP_OK) {
      spiTestFailure("SD test SPI DMA queue failed; reboot before retry", true);
      Serial.printf("SPI SD DMA queue: %s\n", esp_err_to_name(queued));
      break;
    }

    JsonDocument sendReq;
    JsonObject send = sendReq["spi_sd_send"].to<JsonObject>();
    send["seq"] = seq;
    send["sector"] = absoluteSector;
    req = "";
    response = "";
    serializeJson(sendReq, req);
    const bool sentReplied = ebtksLinkTransaction(req, response, 3000);

    spi_slave_transaction_t *done = nullptr;
    const esp_err_t received = spi_slave_get_trans_result(
        SPI_TEST_HOST, &done, pdMS_TO_TICKS(200));
    if (received != ESP_OK || done != &spiTestTrans) {
      // Could still be DMA-owned, even if Teensy replied with a failure.
      spiTestFailure("SD test SPI DMA incomplete; ESP32 reboot required", true);
      Serial.printf("SPI SD DMA incomplete: %s sector=%lu UART=%s\n",
                    esp_err_to_name(received), (unsigned long)absoluteSector,
                    sentReplied ? response.c_str() : "<timeout>");
      break;
    }

    if (spiTestTrans.trans_len != SPI_TEST_BYTES * 8) {
      spiTestFailure("SD test SPI short transaction");
      break;
    }

    uint16_t actualCrc = 0xFFFF;
    static const char hexDigits[] = "0123456789ABCDEF";
    for (uint16_t i = 0; i < SPI_TEST_BYTES; ++i) {
      const uint8_t b = spiTestRx[i];
      actualCrc = spiTestCrcByte(actualCrc, b);
      spiTestAggregateCrc = spiTestCrcByte(spiTestAggregateCrc, b);
      if (i < 16) {
        spiTestSdFirst16Hex[2U*i] = hexDigits[b >> 4];
        spiTestSdFirst16Hex[2U*i+1] = hexDigits[b & 15U];
      }
    }
    spiTestSdFirst16Hex[32] = '\0';
    spiTestCrcLocal = actualCrc;
    if (!sentReplied) {
      spiTestFailure("SD sector arrived but Teensy UART send reply timed out");
      break;
    }

    JsonDocument sendReply;
    if (deserializeJson(sendReply, response)) {
      spiTestFailure("Malformed Teensy SD send reply");
      break;
    }
    JsonVariantConst sr = sendReply["spi_sd_send"];
    const bool sendOk = sr["ok"] | false;
    const int sendSeq = sr["seq"] | -1;
    const uint32_t sendSector = sr["sector"] | UINT32_MAX;
    const int sendLen = sr["len"] | -1;
    const uint16_t sendCrc = (uint16_t)(sr["crc"] | 0);
    const uint32_t spiUs = sr["us"] | 0UL;
    spiTestCrcRemote = sendCrc;
    spiTestSdSentCrc = sendCrc;

    if (!sendOk || sendSeq != seq || sendSector != absoluteSector ||
        sendLen != (int)SPI_TEST_BYTES || spiUs == 0 ||
        actualCrc != preparedCrc || sendCrc != preparedCrc) {
      spiTestFailure("SD/SPI data CRC, length or sequencing mismatch");
      Serial.printf("SPI SD FAIL: path=%s sector=%lu ESP_RX=%04X SD=%04X SPI_TX=%04X "
                    "len=%d us=%lu first16=%s\n",
                    spiTestSdPath, (unsigned long)absoluteSector, actualCrc,
                    preparedCrc, sendCrc, sendLen, (unsigned long)spiUs,
                    spiTestSdFirst16Hex);
      break;
    }

    spiTestElapsedUs += spiUs;
    if (block == 0 || spiUs < spiTestMinUs) spiTestMinUs = spiUs;
    if (spiUs > spiTestMaxUs) spiTestMaxUs = spiUs;
    ++spiTestCompleted;
    spiTestFailSector = -1;
  }

  spiTestWallUs = (uint32_t)(micros() - started);
  if (spiTestCompleted == sectors && spiTestState == 1) {
    spiTestDetail = "Real SD sectors verified over 4 MHz SPI; no disk writes";
    spiTestState = 2;
    Serial.printf("SPI SD READ PASS: %s first=%lu count=%u spi_us=%lu sd_us=%lu wall_us=%lu\n",
                  spiTestSdPath, (unsigned long)first, sectors,
                  (unsigned long)spiTestElapsedUs,
                  (unsigned long)spiTestSdReadUs,
                  (unsigned long)spiTestWallUs);
  } else {
    Serial.printf("SPI SD READ FAILED: %s first=%lu passed=%u/%u at=%d: %s\n",
                  spiTestSdPath, (unsigned long)first, spiTestCompleted,
                  sectors, spiTestFailSector, spiTestDetail);
  }
  vTaskDelete(nullptr);
}

inline bool spiTestSdStart(const String &path, uint32_t first, uint16_t sectors)
{
  // No automatic writes, retries or live CP/M transport switching.
  if (!spiTestSdPathValid(path) ||
      !(sectors == 1 || sectors == 16 || sectors == SPI_TEST_MAX_SECTORS) ||
      first > ((UINT32_MAX / SPI_TEST_BYTES) - SPI_TEST_MAX_SECTORS) ||
      spiTestState == 1 || spiTestNeedsReboot) return false;
  strlcpy(spiTestSdPath, path.c_str(), sizeof(spiTestSdPath));
  spiTestSdFirstSector = first;
  spiTestSectors = sectors;
  spiTestSdMode = true;
  spiTestStreamMode = false;
  spiTestGpioMode = false;
  spiTestHz = 4000000;
  spiTestMode = 0;
  spiTestPattern = 0;
  spiTestBidi = false;
  spiTestState = 1;
  ++spiTestRunNumber;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      spiTestSdWorker, "SpiSdRead", 6144, nullptr, 1, nullptr, 0);
  if (ok != pdPASS) {
    spiTestFailure("Could not create SD SPI worker");
    return false;
  }
  return true;
}

// One control request per batch rather than two UART round trips/sector.
// The Teensy receives its sole 'send' request while we hold the UART mutex;
// we must keep feeding independently validated SPI RX DMA transactions until
// all frames arrive. This same transport can later carry CRT frames using a
// different KIND, without coupling the SPI framing to SD.
inline bool spiStreamReadReplyLocked(String &reply, uint32_t timeoutMs)
{
  String line;
  line.reserve(1024);
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < timeoutMs) {
    while (TeensySerial.available()) {
      const char c = (char)TeensySerial.read();
      if (c == '\n') return ebtksDecodeReplyEnvelope(line, reply);
      if (line.length() >= EBTKS_LINK_MAX_REPLY - 1) return false;
      line += c;
    }
    vTaskDelay(1);
  }
  return false;
}

inline void spiTestStreamRun()
{
  spiTestHeapAtLaunch = (uint32_t)ESP.getFreeHeap();
  spiTestDetail = "Preparing read-only batched SPI stream";
  spiTestCompleted = 0;
  spiTestFailSector = -1;
  spiTestFailPattern = -1;
  spiTestMismatchAt = -1;
  spiTestMisoMismatchAt = -1;
  spiTestCrcLocal = 0;
  spiTestCrcRemote = 0;
  spiTestAggregateCrc = 0xFFFF;
  spiTestSdReadUs = 0;
  spiTestSdFileSize = 0;
  spiTestSdPreparedCrc = 0;
  spiTestSdSentCrc = 0;
  spiTestSdFirst16Hex[0] = '\0';
  spiTestStreamFrames = 0;
  spiTestStreamBadFrames = 0;
  spiTestStreamWireBytes = 0;
  spiTestStreamError[0] = '\0';
  spiTestStreamDmaErr = 0;
  spiTestStreamDmaBits = 0;
  spiTestStreamDmaDescriptorMatch = false;
  spiTestStreamReadyPin = -1;
  spiTestStreamDmaCause[0] = '\0';
  spiTestStreamReadyLowPhases = 0;
  spiTestStreamReadyWaitUs = 0;
  spiTestStreamReadyMaxWaitUs = 0;
  spiTestStreamReadyWaitFailure = 0;
  spiTestPrepareReplySample[0] = '\0';
  spiTestPrepareError[0] = '\0';
  spiTestMisoHeadHex[0] = '\0';
  spiTestMisoTxCrc = 0;
  spiTestMisoRxCrc = 0;
  spiTestWallUs = 0;
  spiTestElapsedUs = 0;
  spiTestMinUs = 0;
  spiTestMaxUs = 0;

  const uint32_t started = micros();
  const uint16_t session = spiTestStreamSession;
  const uint16_t count = spiTestSectors;
  const uint32_t first = spiTestSdFirstSector;
  if (!spiTestInit(0)) {
    spiTestFailure("SPI stream: slave init failed");
    
    return;
  }

  // Phase A: validate entire source file span BEFORE arming any DMA.
  JsonDocument prepRequest;
  JsonObject pre = prepRequest["spi_stream_prepare"].to<JsonObject>();
  pre["path"] = spiTestSdPath;
  pre["first"] = first;
  pre["count"] = count;
  pre["session"] = session;
  String req, reply;
  serializeJson(prepRequest, req);
  JsonDocument preDoc;
  bool preparedJson = false;
  // No SPI descriptor exists yet. It is safe to retry a malformed/missing
  // UART PREPARE once; never blindly retry after streaming has begun.
  for (uint8_t attempt = 0; attempt < 2 && !preparedJson; ++attempt) {
    reply = "";
    preDoc.clear();
    const bool transportOk = ebtksLinkTransaction(req, reply, 3000);
    strlcpy(spiTestPrepareError,
            transportOk ? "missing_spi_stream_prepare" : "UART_timeout_or_CRC",
            sizeof(spiTestPrepareError));
    bool jsonParsed = false;
    if (transportOk) {
      const DeserializationError error = deserializeJson(preDoc, reply);
      jsonParsed = !error;
      if (error) strlcpy(spiTestPrepareError, error.c_str(),
                         sizeof(spiTestPrepareError));
    }
    preparedJson = transportOk && jsonParsed &&
                   preDoc["spi_stream_prepare"].is<JsonObject>();
    if (!preparedJson) {
      ++spiTestStreamPrepareParseFailures;
      const char *reason = spiTestPrepareError;
      // Sanitized bounded sample of decoded reply (no unbounded JSON logs).
      for (size_t i = 0; i < sizeof(spiTestPrepareReplySample) - 1; ++i) {
        if (i >= reply.length()) {
          spiTestPrepareReplySample[i] = '\0';
          break;
        }
        const char c = reply[i];
        spiTestPrepareReplySample[i] = (c >= 32 && c < 127) ? c : '?';
        spiTestPrepareReplySample[i + 1] = '\0';
      }
      Serial.printf("SPI stream PREPARE invalid attempt=%u cause=%s "
                    "len=%u heap=%lu sample='%s'\n",
                    (unsigned)(attempt + 1), reason, (unsigned)reply.length(),
                    (unsigned long)ESP.getFreeHeap(), spiTestPrepareReplySample);
      if (attempt == 0) vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  if (!preparedJson) {
    spiTestFailure("Stream prepare failed JSON/UART after 2 attempts; no DMA queued");
    spiTestWallUs = (uint32_t)(micros() - started);
    return;
  }
  spiTestPrepareError[0] = '\0';
  spiTestPrepareReplySample[0] = '\0';
  JsonVariantConst pr = preDoc["spi_stream_prepare"];
  const bool ready = pr["ok"] | false;
  const int returnedSession = pr["session"] | -1;
  const uint32_t returnedFirst = pr["first"] | UINT32_MAX;
  const int returnedCount = pr["count"] | -1;
  spiTestSdFileSize = pr["file_size"] | 0UL;
  if (!ready || returnedSession != session || returnedFirst != first ||
      returnedCount != count) {
    spiTestFailure("Stream prepare rejected path or sector span; no DMA queued");
    Serial.printf("SPI stream prepare failed: %s start=%lu count=%u reason=%s\n",
                  spiTestSdPath, (unsigned long)first, (unsigned)count,
                  pr["reason"] | "unknown");
    
    return;
  }

  // Lock the serialized Teensy UART for the duration of the batch. Normal
  // CP/M disk requests and browser commands cannot interleave stream frames.
  if (xSemaphoreTake(ebtksLinkMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    spiTestFailure("Stream UART busy (DMA not queued)");
    
    return;
  }

  // The first descriptor must be armed before the Teensy sees the request.
  // A single descriptor is reused only AFTER each DMA completion is reaped.
  const size_t wireBytes = EbtksStream::FRAME_BYTES;
  const uint32_t wireBits = (uint32_t)wireBytes * 8U;
  bool dmaOk = true;
  auto arm = [&]() -> bool {
    memset(spiTestRx, 0, wireBytes);
    memset(&spiTestTrans, 0, sizeof(spiTestTrans));
    spiTestTrans.length = wireBits;
    spiTestTrans.rx_buffer = spiTestRx;
    spiTestTrans.tx_buffer = nullptr;
    gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
    return spi_slave_queue_trans(SPI_TEST_HOST, &spiTestTrans,
                                pdMS_TO_TICKS(1500)) == ESP_OK;
  };

  if (!arm()) {
    xSemaphoreGive(ebtksLinkMutex);
    spiTestFailure("Stream first DMA descriptor queue failed; reboot", true);
    
    return;
  }

  // Single UART send request; no UART traffic per data frame.
  while (TeensySerial.available()) TeensySerial.read();
  JsonDocument sendReq;
  sendReq["spi_stream_send"]["session"] = session;
  String sendMessage;
  serializeJson(sendReq, sendMessage);
  TeensySerial.print(sendMessage);
  TeensySerial.write('\r');

  int firstBad = -1;
  static const char hd[] = "0123456789ABCDEF";
  for (uint16_t block = 0; block < count; ++block) {
    spiTestFailSector = (int)block; // retain DMA timeout location
    spi_slave_transaction_t *done = nullptr;
    const esp_err_t err = spi_slave_get_trans_result(
        SPI_TEST_HOST, &done, pdMS_TO_TICKS(2500));
    // Record all branches of the failure separately. At 176ms wall time
    // an early short transaction is more plausible than a 2.5s timeout.
    const bool descriptorMatch = err == ESP_OK && done == &spiTestTrans;
    const uint32_t actualBits = descriptorMatch ?
        (uint32_t)spiTestTrans.trans_len : 0;
    if (err != ESP_OK || !descriptorMatch || actualBits != wireBits) {
      spiTestStreamDmaErr = (int)err;
      spiTestStreamDmaBits = actualBits;
      spiTestStreamDmaDescriptorMatch = descriptorMatch;
      spiTestStreamReadyPin = digitalRead(SPI_TEST_READY) ? 1 : 0;
      const char *why = err != ESP_OK ? esp_err_to_name(err) :
                        !descriptorMatch ? "wrong_dma_descriptor" :
                                           "short_spi_transaction";
      strlcpy(spiTestStreamDmaCause, why,
              sizeof(spiTestStreamDmaCause));
      spiTestFailure("Stream SPI DMA result failed; ESP32 reboot required", true);
      Serial.printf("SPI stream DMA FAIL frame=%u cause=%s esp_err=%d "
                    "bits=%lu/%lu desc_match=%u READY=%d time_ms=%lu\n",
                    (unsigned)block, why, (int)err,
                    (unsigned long)actualBits, (unsigned long)wireBits,
                    descriptorMatch ? 1U : 0U,
                    (int)spiTestStreamReadyPin,
                    (unsigned long)((micros() - started) / 1000U));
      dmaOk = false;
      break;
    }
    ++spiTestStreamFrames;
    spiTestStreamWireBytes += wireBytes;

    const EbtksStream::Check check = EbtksStream::check(
        spiTestRx, EbtksStream::KIND_SD_READ, session, block);
    const uint8_t *payload = spiTestRx + EbtksStream::HEADER_BYTES;
    const uint16_t payloadCrc = EbtksStream::crc16(payload, EbtksStream::PAYLOAD_BYTES);
    spiTestCrcLocal = payloadCrc;
    for (uint8_t i = 0; i < 16; ++i) {
      spiTestSdFirst16Hex[2U*i] = hd[payload[i] >> 4];
      spiTestSdFirst16Hex[2U*i+1] = hd[payload[i] & 15];
    }
    spiTestSdFirst16Hex[32] = '\0';
    if (check != EbtksStream::Check::OK) {
      ++spiTestStreamBadFrames;
      if (firstBad < 0) {
        firstBad = block;
        strlcpy(spiTestStreamError, EbtksStream::checkName(check),
                sizeof(spiTestStreamError));
      }
    } else {
      spiTestAggregateCrc = EbtksStream::crc16(
          payload, EbtksStream::PAYLOAD_BYTES, spiTestAggregateCrc);
      ++spiTestCompleted;
    }
    // We still drain all requested frames after CRC errors. This prevents
    // leaving DMA queued or truncating a session on a recoverable mismatch.
    if (block + 1 < count) {
      // CRITICAL: No attachInterrupt() is permitted on Teensy READY because
      // the HP-85 timing ISR owns IRQ_GPIO6789. Keep READY LOW for several
      // milliseconds between DMA descriptors so Teensy's ordinary GPIO
      // polling cannot miss the completion (LOW) phase. On the Teensy
      // this is checked immediately after CS goes HIGH, before SD.read().
      // The delay affects only this opt-in diagnostic, not normal CP/M I/O.
      gpio_set_level((gpio_num_t)SPI_TEST_READY, 0);
      vTaskDelay(pdMS_TO_TICKS(5));
      if (!arm()) {
        spiTestFailure("Stream next DMA queue failed; ESP32 reboot required", true);
        dmaOk = false;
        break;
      }
    }
  }

  // Only read the final UART result once all SPI DMA descriptors have been
  // reaped. The Teensy cannot reply before its last SPI transfer completes.
  String serialReply;
  const bool uartOk = dmaOk && spiStreamReadReplyLocked(serialReply, 3500);
  xSemaphoreGive(ebtksLinkMutex);
  if (!dmaOk) {
    spiTestWallUs = (uint32_t)(micros() - started);
    
    return;
  }
  spiTestWallUs = (uint32_t)(micros() - started);
  if (firstBad >= 0) spiTestFailSector = firstBad;
  if (!uartOk) {
    spiTestFailure("Stream UART final reply invalid/timed out (all DMA received)");
    
    return;
  }
  JsonDocument response;
  if (deserializeJson(response, serialReply)) {
    spiTestFailure("Stream final UART reply is malformed");
    
    return;
  }
  JsonVariantConst sr = response["spi_stream_send"];
  const bool ok = sr["ok"] | false;
  const int replySession = sr["session"] | -1;
  const int framesSent = sr["frames"] | -1;
  const int sourceGood = sr["good"] | -1;
  const int replyCount = sr["count"] | -1;
  const uint16_t senderAggregate = (uint16_t)(sr["aggregate_crc"] | 0);
  spiTestSdReadUs = sr["sd_us"] | 0UL;
  spiTestElapsedUs = sr["spi_us"] | 0UL;
  spiTestMinUs = sr["min_us"] | 0UL;
  spiTestMaxUs = sr["max_us"] | 0UL;
  spiTestCrcRemote = senderAggregate;
  spiTestSdSentCrc = senderAggregate;
  spiTestStreamReadyLowPhases = sr["ready_low_phases"] | 0UL;
  spiTestStreamReadyWaitUs = sr["ready_wait_us"] | 0UL;
  spiTestStreamReadyMaxWaitUs = sr["ready_max_wait_us"] | 0UL;
  spiTestStreamReadyWaitFailure = sr["ready_wait_failure"] | 0;

  if (!ok || replySession != session || framesSent != count ||
      sourceGood != count || replyCount != count || firstBad >= 0 ||
      spiTestStreamFrames != count || spiTestCompleted != count ||
      spiTestAggregateCrc != senderAggregate || spiTestElapsedUs == 0) {
    spiTestFailure("Stream frame integrity, session or aggregate CRC mismatch");
    Serial.printf("SPI stream FAIL: path=%s session=%u received=%u/%u "
                  "bad=%u first_bad=%d err=%s recvCRC=%04X teenCRC=%04X "
                  "teen=%s\n",
                  spiTestSdPath, (unsigned)session,
                  (unsigned)spiTestStreamFrames, (unsigned)count,
                  (unsigned)spiTestStreamBadFrames, firstBad,
                  spiTestStreamError, (unsigned)spiTestAggregateCrc,
                  (unsigned)senderAggregate, sr["reason"] | "unknown");
  } else {
    spiTestDetail = "Batched real SD SPI stream verified; no image writes";
    spiTestFailSector = -1;
    spiTestState = 2;
    Serial.printf("SPI stream PASS: %s sector=%lu count=%u SD_us=%lu SPI_us=%lu "
                  "wall_us=%lu aggregate=%04X\n",
                  spiTestSdPath, (unsigned long)first, (unsigned)count,
                  (unsigned long)spiTestSdReadUs,
                  (unsigned long)spiTestElapsedUs,
                  (unsigned long)spiTestWallUs,
                  (unsigned)spiTestAggregateCrc);
  }
  
}

// Persistent worker stays blocked when idle. A single task allocation
// replaces repeated create/delete calls that can fragment the ESP32 heap.
inline void spiTestStreamWorker(void *)
{
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    spiTestStreamRun();
    spiTestHeapAtFinish = (uint32_t)ESP.getFreeHeap();
    spiTestStreamStackFree = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    spiTestHeapLargestBlock =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  }
}

inline bool spiTestStreamStart(const String &path, uint32_t first, uint16_t sectors)
{
  if (!spiTestSdPathValid(path) ||
      !(sectors == 1 || sectors == 16 || sectors == SPI_TEST_MAX_SECTORS) ||
      first > ((UINT32_MAX / SPI_TEST_BYTES) - SPI_TEST_MAX_SECTORS) ||
      spiTestState == 1 || spiTestNeedsReboot) return false;
  strlcpy(spiTestSdPath, path.c_str(), sizeof(spiTestSdPath));
  spiTestSdFirstSector = first;
  spiTestStreamSession = (uint16_t)((spiTestRunNumber + 1U) & 0xFFFFU);
  spiTestSectors = sectors;
  spiTestGpioMode = false;
  spiTestSdMode = false;
  spiTestStreamMode = true;
  spiTestMode = 0;
  spiTestHz = 4000000;
  spiTestBidi = false;
  spiTestPattern = 0;
  // The persistent task is created only once. Require enough free heap
  // and one contiguous stack allocation before admitting a new run.
  // Future runs reuse the stack, eliminating deferred deletion backlog.
  spiTestHeapAtLaunch = (uint32_t)ESP.getFreeHeap();
  spiTestHeapLargestBlock =
      (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  // Even when the worker already exists, leave room for AsyncTCP,
  // Wi-Fi, Z80 and web-server allocations. Never run on exhausted heap.
  if (spiTestHeapAtLaunch < 16384 || spiTestHeapLargestBlock < 4096) {
    ++spiTestStreamLaunchFailures;
    spiTestFailure("Stream refused: low ESP32 heap, reboot before more tests", true);
    Serial.printf("SPI stream LOW HEAP: free=%lu largest=%lu\n",
                  (unsigned long)spiTestHeapAtLaunch,
                  (unsigned long)spiTestHeapLargestBlock);
    return false;
  }
  if (!spiTestStreamTaskHandle) {
    if (spiTestHeapAtLaunch < 24576 || spiTestHeapLargestBlock < 12288) {
      ++spiTestStreamLaunchFailures;
      spiTestFailure("Insufficient ESP32 heap for stream worker; reboot or roll back", true);
      Serial.printf("SPI stream REFUSED: free=%lu largest=%lu\n",
                    (unsigned long)spiTestHeapAtLaunch,
                    (unsigned long)spiTestHeapLargestBlock);
      return false;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        spiTestStreamWorker, "SpiBatch", 8192, nullptr, 1,
        &spiTestStreamTaskHandle, 0);
    if (created != pdPASS || !spiTestStreamTaskHandle) {
      ++spiTestStreamLaunchFailures;
      spiTestFailure("Could not allocate persistent SPI worker; heap diagnostic available", true);
      Serial.printf("SPI stream task ALLOCATION FAILED: free=%lu largest=%lu\n",
                    (unsigned long)spiTestHeapAtLaunch,
                    (unsigned long)spiTestHeapLargestBlock);
      return false;
    }
  }
  spiTestState = 1;
  ++spiTestRunNumber;
  ++spiTestStreamRunsStarted;
  xTaskNotifyGive(spiTestStreamTaskHandle);
  return true;
}

inline bool spiTestStart(uint32_t hz = 1000000, uint16_t sectors = 1,
                         uint8_t pattern = 0, bool bidi = false,
                         uint8_t mode = 0)
{
  if (mode > 1 || !spiTestValidHz(hz) ||
      !(sectors == 1 || sectors == 16 || sectors == SPI_TEST_MAX_SECTORS) ||
      pattern > 5 || spiTestState == 1 || spiTestNeedsReboot) return false;
  spiTestHz = hz;
  spiTestSectors = sectors;
  spiTestPattern = pattern;
  spiTestBidi = bidi;
  spiTestMode = mode;
  spiTestGpioMode = false;
  spiTestSdMode = false;
  spiTestStreamMode = false;
  spiTestGpioLow = -1;
  spiTestGpioHigh = -1;
  spiTestState = 1;
  ++spiTestRunNumber;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      spiTestWorker, "SpiCN3Test", 4096, nullptr, 1, nullptr, 0);
  if (ok != pdPASS) {
    spiTestFailure("Could not create SPI test worker");
    return false;
  }
  return true;
}

static const char SPI_TEST_PAGE[] PROGMEM = R"SPIHTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS CN3 SPI full-duplex diagnostics</title>
<style>
body{font:16px system-ui;background:#202124;color:#f0f0f0;max-width:780px;margin:30px auto;padding:15px}
button,select,input{font:inherit;padding:10px;border-radius:5px}
button{background:#2383d1;color:white;border:0;cursor:pointer}
button:disabled{opacity:.45}
label{display:inline-block;margin:8px 16px 8px 0}
pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#111;padding:15px;border-radius:6px}
a{color:#80c4ff}
</style></head><body><h2>CN3 SPI speed &amp; bidirectional reliability</h2>
<p><strong>Feature branch only.</strong> Teensy SPI master / ESP32 SPI slave.
Choose the proven synthetic SPI diagnostic or the new <strong>read-only
SD disk-image sector test</strong>. Synthetic full duplex verifies both
directions. The SD test reads actual sectors from a file in /cpm/disks/
with O_RDONLY, validates SD/read/SPI CRCs, and sends raw bytes without
Base64. It never writes an image. Normal CP/M still uses UART/Base64.</p>
<p>Both test firmwares and the five CN3 jumpers + 10k CS pull-up are required.
Leave CP/M idle at the prompt during stress testing. Start with a single
1 MHz full-duplex sector in <strong>SPI mode 0</strong> before increasing the
clock. On this board SPI mode 0 repeatedly passed 8 MHz/128 sectors.
Mode 1 is available for comparison but fails MISO at 8 MHz.
ESP-IDF notes the GPIO-matrix MISO timing margin is limited at 8 MHz.</p>
<label>Direction <select id="bidi">
<option value="1" selected>Full duplex: Teensy ↔ ESP32</option>
<option value="0">MOSI only: Teensy → ESP32 (regression)</option>
</select></label>
<label>SPI mode <select id="mode">
<option value="0" selected>Mode 0 (8 MHz hardware-tested)</option>
<option value="1">Mode 1 (8 MHz MISO failures observed)</option>
</select></label>
<label>SPI clock <select id="hz"><option value="1000000">1 MHz</option>
<option value="2000000">2 MHz</option><option value="4000000" selected>4 MHz</option>
<option value="8000000">8 MHz</option></select></label>
<label>Transfer count <select id="sectors"><option value="1" selected>1 sector (512 B)</option>
<option value="16">16 sectors (8 KiB)</option>
<option value="128">128 sectors (64 KiB)</option></select></label>
<label>Pattern <select id="pattern">
<option value="5" selected>Cycle five patterns</option>
<option value="0">Sequence/ramp</option><option value="1">All 00</option>
<option value="2">All FF</option><option value="3">Alternating 55/AA</option>
<option value="4">Walking one</option></select></label>
<p><button id="run" onclick="start()">Run synthetic SPI test</button>
<button id="gpio" onclick="gpioCheck()">Check MISO wire (GPIO LOW/HIGH)</button></p>
<hr><h3>Real SD sector read → SPI (4 MHz, mode 0, READ-ONLY)</h3>
<p>Leave CP/M at the prompt. Enter the exact image filename from
<a href="/cpmmedia">CP/M Media Manager</a>, under <code>/cpm/disks/</code>.
The test opens it <code>O_RDONLY</code>; it does not mount, modify or write
the image. Select 1 sector first, then 16 and 128 while SIMON85 draws.</p>
<label>SD image <input id="sdpath" size="30" value="/cpm/disks/A.dsk"></label>
<label>Starting 512-byte sector <input id="sdsector" type="number" min="0" step="1" value="0" size="8"></label>
<p><button id="sdRun" onclick="startSd()">Read SD sectors over SPI (4 MHz)</button></p>
<p><button id="streamRun" onclick="startStream()">Batched framed SD stream (4 MHz, read-only)</button></p>
<p><small>The batched test uses a reusable 16-byte session/block/CRC header
plus 512 raw bytes per SPI frame, two UART commands per entire batch rather
than per sector. The type field can later identify an HP-85 screen stream.
The working CP/M disk service is not affected.</small></p>
<pre id="out">Loading status…</pre>
<p>If MISO fails, inspect <code>miso_first16_hex</code>. Thirty-two zeros
means the first 16 Teensy-received bytes were all zero; use a 1 MHz test
and check the MISO jumper ESP GPIO17 (CN3-17 physically verified on this board) ↔ CN3-29 (Teensy pin 12).
8 MHz MISO over GPIO matrix is beyond ESP-IDF's conservative timing limit.
</p><p>In full duplex each 512-byte transaction carries 512 bytes <em>each way</em>.
SPI KiB/s is quoted per direction and includes Teensy software CRC, pattern
checks and HP-bus interrupt overhead. End-to-end KiB/s additionally includes
one UART round trip per sector, not a continuous 64 KiB burst. On incomplete
DMA the ESP32 must reboot before another test. Save the JSON and
<a href="/z80debug">debug log</a> if either direction fails.</p>
<p><a href="/z80">CP/M terminal</a> · <a href="/z80debug">Diagnostics</a></p>
<script>
async function refresh(){
  try {
    const r=await fetch('/api/spitest'), j=await r.json();
    document.getElementById('out').textContent=JSON.stringify(j,null,2);
    document.getElementById('run').disabled=j.state==='running'||j.needs_reboot;
    document.getElementById('gpio').disabled=j.state==='running'||j.needs_reboot;
    document.getElementById('sdRun').disabled=j.state==='running'||j.needs_reboot;
    document.getElementById('streamRun').disabled=j.state==='running'||j.needs_reboot;
  } catch(e) {document.getElementById('out').textContent=String(e)}
}
async function start(){
  const hz=document.getElementById('hz').value,
    sectors=document.getElementById('sectors').value,
    pattern=document.getElementById('pattern').value,
    bidi=document.getElementById('bidi').value,
    mode=document.getElementById('mode').value;
  const u='/api/spitest?hz='+hz+'&sectors='+sectors+'&pattern='+pattern+'&bidi='+bidi+'&mode='+mode;
  const r=await fetch(u,{method:'POST'});
  if(!r.ok)alert(await r.text());
  refresh();
}
async function gpioCheck(){
  const r=await fetch('/api/spitest/gpio',{method:'POST'});
  if(!r.ok)alert(await r.text());
  refresh();
}
async function startStream(){
  const path=document.getElementById('sdpath').value.trim(),
    first=document.getElementById('sdsector').value,
    sectors=document.getElementById('sectors').value;
  const u='/api/spitest/stream?path='+encodeURIComponent(path)+'&first='+
    encodeURIComponent(first)+'&sectors='+encodeURIComponent(sectors);
  const r=await fetch(u,{method:'POST'});
  if(!r.ok)alert(await r.text());
  refresh();
}
async function startSd(){
  const path=document.getElementById('sdpath').value.trim(),
    first=document.getElementById('sdsector').value,
    sectors=document.getElementById('sectors').value;
  const u='/api/spitest/sd?path='+encodeURIComponent(path)+'&first='+
    encodeURIComponent(first)+'&sectors='+encodeURIComponent(sectors);
  const r=await fetch(u,{method:'POST'});
  if(!r.ok)alert(await r.text());
  refresh();
}
setInterval(refresh,700);refresh();
</script></body></html>
)SPIHTML";

inline void spiTestRegister(AsyncWebServer &server)
{
  server.on("/spitest", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", SPI_TEST_PAGE);
  });
  server.on("/api/spitest/stream", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!r->hasParam("path") || !r->hasParam("first") ||
        !r->hasParam("sectors")) {
      r->send(400, "text/plain", "Stream needs path, first, sectors");
      return;
    }
    const String path = r->getParam("path")->value();
    const String firstText = r->getParam("first")->value();
    const String countText = r->getParam("sectors")->value();
    bool digits = firstText.length() > 0 && firstText.length() <= 7;
    for (size_t i = 0; i < firstText.length(); ++i)
      if (firstText[i] < '0' || firstText[i] > '9') digits = false;
    const uint32_t first = digits ?
        (uint32_t)strtoul(firstText.c_str(), nullptr, 10) : UINT32_MAX;
    const uint16_t count = countText == "1" ? 1 :
                           countText == "16" ? 16 :
                           countText == "128" ? 128 : 0;
    if (!digits || !spiTestSdPathValid(path) || !count ||
        first > ((UINT32_MAX / SPI_TEST_BYTES) - SPI_TEST_MAX_SECTORS)) {
      r->send(400, "text/plain", "Invalid /cpm/disks file, sector or count");
    } else if (!spiTestStreamStart(path, first, count)) {
      r->send(409, "text/plain", "Stream busy or ESP32 reboot required");
    } else {
      r->send(202, "application/json",
              "{\"started\":true,\"framed\":true,\"read_only\":true,\"clock_hz\":4000000}");
    }
  });
  server.on("/api/spitest/sd", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!r->hasParam("path") || !r->hasParam("first") ||
        !r->hasParam("sectors")) {
      r->send(400, "text/plain", "SD test needs path, first, sectors");
      return;
    }
    const String path = r->getParam("path")->value();
    const String startText = r->getParam("first")->value();
    const String sectorsText = r->getParam("sectors")->value();
    // Strict unsigned decimal parsing, including rejecting negatives and
    // overflow; no accidental wraparound before the bounds check.
    bool validDigits = startText.length() > 0 && startText.length() <= 7;
    for (size_t i = 0; i < startText.length(); ++i)
      if (startText[i] < '0' || startText[i] > '9') validDigits = false;
    const uint32_t first = validDigits ?
        (uint32_t)strtoul(startText.c_str(), nullptr, 10) : UINT32_MAX;
    const uint16_t sectors = (sectorsText == "1") ? 1 :
                             (sectorsText == "16") ? 16 :
                             (sectorsText == "128") ? 128 : 0;
    if (!validDigits || !spiTestSdPathValid(path) || !sectors ||
        first > ((UINT32_MAX / SPI_TEST_BYTES) - SPI_TEST_MAX_SECTORS)) {
      r->send(400, "text/plain",
              "Invalid SD file, start sector or count; use /cpm/disks/<file>");
    } else if (!spiTestSdStart(path, first, sectors)) {
      r->send(409, "text/plain", "SD SPI test busy or ESP32 reboot required");
    } else {
      r->send(202, "application/json", "{\"started\":true,\"read_only\":true,\"clock_hz\":4000000}");
    }
  });
  server.on("/api/spitest/gpio", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!spiTestGpioStart())
      r->send(409, "text/plain", "GPIO MISO test busy or requires ESP32 reboot");
    else
      r->send(202, "application/json", "{\"started\":true}");
  });
  server.on("/api/spitest", HTTP_POST, [](AsyncWebServerRequest *r) {
    uint32_t hz = 1000000;
    uint16_t sectors = 1;
    uint8_t pattern = 0;
    if (r->hasParam("hz")) hz = (uint32_t)r->getParam("hz")->value().toInt();
    if (r->hasParam("sectors")) sectors = (uint16_t)r->getParam("sectors")->value().toInt();
    if (r->hasParam("pattern")) pattern = (uint8_t)r->getParam("pattern")->value().toInt();
    const String bidiParam = r->hasParam("bidi") ? r->getParam("bidi")->value() : "0";
    const bool bidi = bidiParam == "1";
    const String modeParam = r->hasParam("mode") ? r->getParam("mode")->value() : "0";
    const uint8_t mode = modeParam == "1" ? 1 : 0;
    if ((modeParam != "0" && modeParam != "1") || !spiTestValidHz(hz) || !(sectors == 1 || sectors == 16 ||
          sectors == SPI_TEST_MAX_SECTORS) || pattern > 5 ||
          (bidiParam != "0" && bidiParam != "1")) {
      r->send(400, "text/plain", "Invalid SPI settings");
    } else if (!spiTestStart(hz, sectors, pattern, bidi, mode)) {
      r->send(409, "text/plain", "SPI test busy or requires ESP32 reboot");
    } else {
      r->send(202, "application/json", "{\"started\":true}");
    }
  });
  server.on("/api/spitest", HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument doc;
    const int state = spiTestState;
    doc["state"] = state == 0 ? "idle" : state == 1 ? "running" :
                   state == 2 ? "passed" : "failed";
    doc["detail"] = spiTestDetail;
    doc["test_kind"] = spiTestGpioMode ? "gpio_miso" :
                       spiTestStreamMode ? "sd_stream_framed" :
                       spiTestSdMode ? "sd_readonly_spi" : "spi";
    doc["gpio_low_read"] = (int)spiTestGpioLow;
    doc["gpio_high_read"] = (int)spiTestGpioHigh;
    doc["needs_reboot"] = (bool)spiTestNeedsReboot;
    doc["runs"] = (uint32_t)spiTestRunNumber;
    doc["bytes"] = (uint32_t)spiTestSectors * SPI_TEST_BYTES;
    doc["clock_hz"] = (uint32_t)spiTestHz;
    doc["bidirectional"] = (bool)spiTestBidi;
    doc["spi_mode"] = (unsigned)spiTestMode;
    doc["miso_first16_hex"] = spiTestMisoHeadHex;
    doc["pattern"] = (unsigned)spiTestPattern;
    doc["sectors"] = (unsigned)spiTestSectors;
    doc["completed_sectors"] = (unsigned)spiTestCompleted;
    doc["completed_bytes"] = (uint32_t)spiTestCompleted * SPI_TEST_BYTES;
    doc["completed_bytes_total"] = (uint32_t)spiTestCompleted *
                                    SPI_TEST_BYTES * (spiTestBidi ? 2U : 1U);
    doc["sd_read_only"] = (bool)(spiTestSdMode || spiTestStreamMode);
    if (spiTestSdMode || spiTestStreamMode) {
      doc["sd_file"] = spiTestSdPath;
      doc["sd_start_sector"] = (uint32_t)spiTestSdFirstSector;
      doc["sd_file_bytes"] = (uint32_t)spiTestSdFileSize;
      doc["sd_read_us"] = (uint32_t)spiTestSdReadUs;
      if (spiTestSdMode) {
        doc["sd_crc_prepared"] = (uint16_t)spiTestSdPreparedCrc;
        doc["sd_crc_sent"] = (uint16_t)spiTestSdSentCrc;
      }
      doc["sd_first16_hex"] = spiTestSdFirst16Hex;
      doc["sd_crc_match"] = spiTestStreamMode ?
          (spiTestState == 2 && spiTestAggregateCrc == spiTestSdSentCrc) :
          (spiTestSdPreparedCrc == spiTestSdSentCrc &&
           spiTestCrcLocal == spiTestSdPreparedCrc);
    }
    if (spiTestStreamMode) {
      doc["stream_header_bytes"] = (unsigned)EbtksStream::HEADER_BYTES;
      doc["stream_frame_bytes"] = (unsigned)EbtksStream::FRAME_BYTES;
      doc["stream_session"] = (uint16_t)spiTestStreamSession;
      doc["stream_frames_received"] = (uint16_t)spiTestStreamFrames;
      doc["stream_bad_frames"] = (uint16_t)spiTestStreamBadFrames;
      doc["stream_wire_bytes"] = (uint32_t)spiTestStreamWireBytes;
      doc["stream_first_error"] = spiTestStreamError;
      doc["stream_dma_error"] = spiTestStreamDmaCause;
      doc["stream_dma_error_code"] = (int)spiTestStreamDmaErr;
      doc["stream_dma_actual_bits"] = (uint32_t)spiTestStreamDmaBits;
      doc["stream_dma_expected_bits"] =
          (uint32_t)EbtksStream::FRAME_BYTES * 8U;
      doc["stream_dma_descriptor_matches"] = (bool)spiTestStreamDmaDescriptorMatch;
      doc["stream_dma_ready_level"] = (int)spiTestStreamReadyPin;
      doc["stream_ready_low_phases"] = (uint32_t)spiTestStreamReadyLowPhases;
      doc["stream_ready_wait_us"] = (uint32_t)spiTestStreamReadyWaitUs;
      doc["stream_ready_max_wait_us"] = (uint32_t)spiTestStreamReadyMaxWaitUs;
      doc["stream_ready_wait_failure"] = (unsigned)spiTestStreamReadyWaitFailure;
      doc["stream_prepare_error"] = spiTestPrepareError;
      doc["stream_prepare_reply_sample"] = spiTestPrepareReplySample;
      doc["stream_prepare_parse_failures"] = (uint32_t)spiTestStreamPrepareParseFailures;
      doc["stream_runs_started"] = (uint32_t)spiTestStreamRunsStarted;
      doc["stream_worker_launch_failures"] = (uint32_t)spiTestStreamLaunchFailures;
      doc["heap_before_stream"] = (uint32_t)spiTestHeapAtLaunch;
      doc["heap_after_stream"] = (uint32_t)spiTestHeapAtFinish;
      doc["heap_free_now"] = (uint32_t)ESP.getFreeHeap();
      doc["heap_min_free"] = (uint32_t)ESP.getMinFreeHeap();
      doc["heap_largest_free_block"] =
          (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      doc["stream_worker_stack_high_water"] = (uint32_t)spiTestStreamStackFree;
      doc["stream_crc_esp"] = (uint16_t)spiTestAggregateCrc;
      doc["stream_crc_teensy"] = (uint16_t)spiTestCrcRemote;
    }
    doc["failed_sector"] = (int)spiTestFailSector;
    doc["failed_pattern"] = (int)spiTestFailPattern;
    doc["first_mismatch_byte"] = (int)spiTestMismatchAt; // MOSI on ESP32
    doc["first_mismatch_miso"] = (int)spiTestMisoMismatchAt; // MISO on Teensy
    doc["crc_esp"] = (uint16_t)spiTestCrcLocal;
    doc["crc_teensy"] = (uint16_t)spiTestCrcRemote;
    doc["miso_crc_esp_tx"] = (uint16_t)spiTestMisoTxCrc;
    doc["miso_crc_teensy_rx"] = (uint16_t)spiTestMisoRxCrc;
    doc["aggregate_crc_esp"] = (uint16_t)spiTestAggregateCrc;
    doc["aggregate_miso_crc_esp_tx"] = (uint16_t)spiTestAggregateMisoTxCrc;
    doc["teensy_transfer_us"] = (uint32_t)spiTestElapsedUs;
    doc["teensy_min_us"] = (uint32_t)spiTestMinUs;
    doc["teensy_max_us"] = (uint32_t)spiTestMaxUs;
    doc["wall_us"] = (uint32_t)spiTestWallUs;
    const double bytes = (double)spiTestCompleted * SPI_TEST_BYTES;
    doc["spi_kib_s"] = spiTestElapsedUs
       ? (bytes * 1000000.0 / 1024.0 / spiTestElapsedUs) : 0.0;
    doc["end_to_end_kib_s"] = spiTestWallUs
       ? (bytes * 1000000.0 / 1024.0 / spiTestWallUs) : 0.0;
    doc["full_duplex_total_kib_s"] = spiTestElapsedUs
       ? (bytes * (spiTestBidi ? 2.0 : 1.0) * 1000000.0 /
          1024.0 / spiTestElapsedUs) : 0.0;
    String json;
    serializeJson(doc, json);
    r->send(200, "application/json", json);
  });
}
