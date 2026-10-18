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

static constexpr spi_host_device_t SPI_TEST_HOST = HSPI_HOST;
static constexpr int SPI_TEST_MOSI = 22;   // CN3-21
static constexpr int SPI_TEST_MISO = 17;   // CN3-13
static constexpr int SPI_TEST_SCLK = 16;   // CN3-14
static constexpr int SPI_TEST_CS = 32;     // CN3-4, external 10k pull-up
static constexpr int SPI_TEST_READY = 27;  // CN3-8
static constexpr size_t SPI_TEST_BYTES = 512;
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
static volatile uint8_t spiTestMode = 1;  // ESP-IDF recommends mode 1 with DMA TX
static volatile bool spiTestGpioMode = false; // separate GPIO-only isolation check
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
      heap_caps_malloc(SPI_TEST_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!spiTestRx) {
    spiTestDetail = "ESP32 RX DMA allocation failed";
    return false;
  }
  if (!spiTestTx) spiTestTx = static_cast<uint8_t *>(
      heap_caps_malloc(SPI_TEST_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
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
  bus.max_transfer_sz = SPI_TEST_BYTES;

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
button,select{font:inherit;padding:10px;border-radius:5px}
button{background:#2383d1;color:white;border:0;cursor:pointer}
button:disabled{opacity:.45}
label{display:inline-block;margin:8px 16px 8px 0}
pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#111;padding:15px;border-radius:6px}
a{color:#80c4ff}
</style></head><body><h2>CN3 SPI speed &amp; bidirectional reliability</h2>
<p><strong>Feature branch only.</strong> Teensy SPI master / ESP32 SPI slave.
Each block is an independently checked 512-byte DMA transaction. Full duplex
transmits different patterns in both directions simultaneously, checking
every byte at the receiving end and comparing independent CRCs via UART.
No CP/M disks are read or written; normal CP/M still uses UART/Base64.</p>
<p>Both test firmwares and the five CN3 jumpers + 10k CS pull-up are required.
Leave CP/M idle at the prompt during stress testing. Start with a single
1 MHz full-duplex sector in <strong>SPI mode 1</strong> before increasing the
clock. ESP-IDF 4.4's SPI slave DMA TX timing recommends modes 1/3.
Mode 0 remains available as a diagnostic comparison.</p>
<label>Direction <select id="bidi">
<option value="1" selected>Full duplex: Teensy ↔ ESP32</option>
<option value="0">MOSI only: Teensy → ESP32 (regression)</option>
</select></label>
<label>SPI mode <select id="mode">
<option value="1" selected>Mode 1 (DMA TX recommended)</option>
<option value="0">Mode 0 (previous baseline)</option>
</select></label>
<label>SPI clock <select id="hz"><option value="1000000" selected>1 MHz</option>
<option value="2000000">2 MHz</option><option value="4000000">4 MHz</option>
<option value="8000000">8 MHz</option></select></label>
<label>Transfer count <select id="sectors"><option value="1" selected>1 sector (512 B)</option>
<option value="16">16 sectors (8 KiB)</option>
<option value="128">128 sectors (64 KiB)</option></select></label>
<label>Pattern <select id="pattern">
<option value="5" selected>Cycle five patterns</option>
<option value="0">Sequence/ramp</option><option value="1">All 00</option>
<option value="2">All FF</option><option value="3">Alternating 55/AA</option>
<option value="4">Walking one</option></select></label>
<p><button id="run" onclick="start()">Run SPI test</button>
<button id="gpio" onclick="gpioCheck()">Check MISO wire (GPIO LOW/HIGH)</button></p>
<pre id="out">Loading status…</pre>
<p>If MISO fails, inspect <code>miso_first16_hex</code>. Thirty-two zeros
means the first 16 Teensy-received bytes were all zero; use a 1 MHz test
and check the MISO jumper CN3-13 (ESP GPIO17) ↔ CN3-29 (Teensy pin 12).
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
setInterval(refresh,700);refresh();
</script></body></html>
)SPIHTML";

inline void spiTestRegister(AsyncWebServer &server)
{
  server.on("/spitest", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", SPI_TEST_PAGE);
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
    doc["test_kind"] = spiTestGpioMode ? "gpio_miso" : "spi";
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
