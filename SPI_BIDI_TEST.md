# CN3 SPI full-duplex diagnostic — build and first hardware test

**Status (2026-10-18):** MOSI-only passed at 1–8 MHz and 128 sectors, but initial **full-duplex mode-0 hardware tests failed** at both 8 MHz and 1 MHz. ESP32's MOSI RX matched the Teensy CRC, while Teensy received what appears to be **512 zero bytes** on MISO (CRC-16 = 47937, first MISO mismatch at byte 0). An incorrect CN3-29 → CN3-15 jumper was corrected to **CN3-29 → CN3-13**, yet the symptom remained. **A NEW diagnostic revision has been committed to both `feature/cn3-spi-read-test` branches but has not yet been flashed or hardware-tested:** the web test supports matching SPI modes 0/1 on both devices and exposes the Teensy's first 16 actually sampled MISO bytes. Start with mode 1 at 1 MHz. No CP/M disk image is accessed.

**WIRING SAFETY CORRECTION (2026-10-18):** The previously published jumper instruction ESP CN3-13 ↔ Teensy CN3-29 was **wrong**: CN3-13 is E04/GPIO4, not GPIO17. The EBTKS V3.0 schematic places net E17 on CN3-15, while the user reports physical module GPIO17 (ESP32-WROOM pin 28) to CN3-17. **Treat the CN3 contact as unresolved until power-off continuity verifies which contact reaches module pad 28.** Keep the firmware GPIO as 17; do not assume a particular CN3 pin from our previous notes. [See wiring status](CN3_SPI_PROPOSAL.md).

## Design and safety

- Full-duplex **512-byte SPI mode-0 or mode-1 transaction**: Teensy SPI0 master clocks MOSI data and simultaneously receives MISO data; ESP32 SPI2 slave RX DMA receives MOSI and TX DMA sends a different deterministic pattern on MISO. Both ends use the same selected mode. The diagnostic defaults to **mode 1**, which Espressif documents as suitable for slave DMA timing, while preserving mode 0 for comparison. See [ESP-IDF v4.4 SPI slave restrictions](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/spi_slave.html).
- Both receivers check **every byte** against a locally generated expected pattern, calculate **CRC-16/Modbus**, and compare CRCs over the existing serialized UART `spi_probe` status reply. The Teensy reports its **first MISO mismatch index**. ESP32 reports its **first MOSI mismatch index**.
- The optional request flag `"bidi":true` enables the MISO test; omitted or `false` keeps the original MOSI-only operation. The browser has a direction selector.
- For each 512-byte transaction, ESP32 fills aligned `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` **TX and RX** buffers before queuing its transaction. Its post-setup ISR signals ESP_READY high only when DMA is armed; Teensy waits for READY, then clocks bytes. The next transaction is queued only after the previous one is completed and acknowledged.
- If DMA completion is uncertain, the ESP32 requires a **reboot** before retrying to avoid reusing still-owned DMA memory. All HP-bus interrupts stay enabled.
- **No disk image is read or written; CP/M storage stays on its original UART/Base64 protocol.** Wi-Fi, HP-85 bus service, WebSocket, OTA, SD services and the existing `main` baseline are not modified. Do not merge this experimental branch to `main` pending testing.

The MISO byte stream is intentionally different from the simultaneous MOSI stream, *including* for constant 00/FF pattern selections, so a short/echo/loopback cannot falsely pass the entire sector:

```cpp
miso[i] = mosi_pattern(i, seq ^ 0xA7, block, pattern)
        ^ uint8_t(0x5A + i * 11 + block * 7);
```

The pattern function is independently implemented on each CPU and was statically checked for parity across the repositories. The original MOSI pattern/CRC result for sequence 13 and block 0 is still **59244**.

## Pull, build and flash

Keep known-good binaries accessible for rollback. Reuse **the same five already-verified CN3 jumpers and 10 kΩ external CS pull-up**; power down before inspecting physical wiring. No rewiring is necessary if the existing MISO jumper was installed.

Teensy (via your normal USB flashing procedure):

```sh
cd ebtks-alt-firmware
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e teensy41
pio run -e teensy41 -t upload
```

ESP32 (via the working EBTKS OTA browser updater):

```sh
cd EBTKS-Z80-Emulator
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Upload only `.pio/build/esp32dev/firmware.bin` in `/updater`. No ESP32 flash erase or Teensy SD replacement required. Teensy must have `"CRTRemote": true` in SD-root `CONFIG.TXT`, as for the previous diagnostic.

## Next isolation test: GPIO17 to Teensy pin12 (new firmware; not yet validated)

**Observed hardware failure:** With the jumper corrected to ESP GPIO17 (WROOM pad 28; connector pin pending continuity) → Teensy CN3-29 (T12), **full-duplex mode 1 at 1 MHz still fails**. The user reported `miso_first16_hex=00000000000000000000000000000000`, `miso_crc_teensy_rx=47937` (512 zero bytes), `miso_crc_esp_tx=13577`, `first_mismatch_miso=0`, while both MOSI CRCs match at 60981. Failure occurs on sector 0; longer runs add no diagnostic value.

**New optional diagnostic** (source committed on 2026-10-18, **not compiled or hardware-tested**) tests GPIO electrical continuity *without the SPI peripheral or DMA*. ESP32 temporarily releases SPI2 when idle, drives its **GPIO17 (CN3-13)** LOW and HIGH in turn as a standard output, then sends a `spi_gpio_probe` command over the existing UART. Teensy briefly releases SPI0 and samples physical pin **12 (CN3-29)** as a normal GPIO input with pulldown, returning the observed level. SPI0 is restored; ESP32 sets GPIO17 to input afterward. No CS/SCLK transactions and no disk access take place.

After pulling and rebuilding **both** feature branches, open `/spitest` and press **Check MISO wire (GPIO LOW/HIGH)**. Alternatively, POST `/api/spitest/gpio` and GET `/api/spitest`.

Expected when the pin-to-pin signal path is good:

```json
{
  "state": "passed",
  "test_kind": "gpio_miso",
  "gpio_low_read": 0,
  "gpio_high_read": 1,
  "needs_reboot": false
}
```

Other SPI status fields remain present but are not relevant in GPIO mode. Interpret failures:

- LOW=0, HIGH=0: ESP GPIO17 high drive does not arrive at Teensy pin12; investigate continuity, a short to ground or incorrect pin mapping.
- LOW=1, HIGH=1: pin seems held high, or output drive isn't reaching Teensy.
- LOW=1, HIGH=0: swapped/inverted or unexpected output behavior.
- Either reading = -1: UART command/response or sampling failed; inspect `/z80debug`.
- LOW=0, HIGH=1 but MISO still zeros during SPI: the wire can carry static levels, so investigate SPI peripheral GPIO routing, DMA TX behavior, or SPI pin-mux configuration. A GPIO pass **does not** prove dynamic 1–8 MHz MISO integrity.

Run only with CP/M idle at prompt; retain original five jumpers and 10 kΩ CS pull-up. The GPIO test must not be run concurrently with any SPI test; the web endpoint rejects busy requests. If `needs_reboot=true`, reboot before retrying. Power down before physically moving jumpers.

## First tests

With CP/M idle at the `A>` prompt and HP-85 normal, open `http://<EBTKS-IP>/spitest`.

1. **Full duplex, SPI mode 1, 1 MHz, 1 sector, sequence/ramp** — first diagnostic of the new MISO timing. Collect `spi_mode`, `miso_first16_hex`, both MISO CRCs and `first_mismatch_miso` even if it fails.
2. **MOSI only, SPI mode 0, 1 MHz, 1 sector, sequence/ramp** — check the proven regression path (switching mode triggers safe ESP32 SPI slave reinitialization between runs).
3. If mode 1 succeeds at 1 MHz, repeat full duplex at **2 MHz, 4 MHz**, then optionally **8 MHz**, one sector each; do not run 128-sector stress before one sector passes.
4. **Full duplex, 4 MHz, 128 sectors, Cycle five patterns** — stress 64 KiB in each direction.
5. **Full duplex, 8 MHz, 128 sectors, Cycle five patterns** — final 64 KiB each way, if 8 MHz single-sector passes.
6. Optionally run 16/128 sectors with fixed 00, FF, 55/AA or walking-one; confirm the HP-85 remains responsive throughout.

At 8 MHz, MOSI input timing was previously reliable, but **MISO output timing is a different path**. It has been failing *even at 1 MHz in mode 0*, with a CRC matching an all-zero 512-byte receive buffer. Inspect `miso_first16_hex`: 32 zero hex characters indicate the first 16 bytes are all zero; nonzero values suggest shifting or corruption. If mode 1 also yields all zeros, check MISO continuity from the CN3 contact continuity-verified to ESP GPIO17 (WROOM pad 28) to CN3-29 (Teensy pin 12) **with power off**, then probe GPIO17/MISO with a logic analyzer or oscilloscope if available, rather than increasing clock speed. GPIO-matrix SPI MISO has a documented conservative ~7.2 MHz ceiling in ESP-IDF, so even a successful 8 MHz MOSI-only run does not establish an 8 MHz bidirectional margin. <https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/spi_slave.html>

On a successful bidirectional 128-sector test, expect:

```json
{
  "state": "passed",
  "bidirectional": true,
  "bytes": 65536,
  "completed_sectors": 128,
  "completed_bytes": 65536,
  "completed_bytes_total": 131072,
  "first_mismatch_byte": -1,
  "first_mismatch_miso": -1,
  "needs_reboot": false
}
```

The response additionally contains numeric `miso_crc_esp_tx` and `miso_crc_teensy_rx` fields. These two values should match each other, and their exact value depends on sequence, pattern, and the last sector. Similarly `crc_esp` must equal `crc_teensy` for MOSI. The aggregate MISO CRC is calculated locally on ESP transmit bytes, whereas actual received MISO CRC is compared on every sector.

`spi_kib_s` / `end_to_end_kib_s` remain **per-direction** throughput, and `full_duplex_total_kib_s` is **total bytes in both directions divided by summed SPI transfer-loop time**, not the end-to-end rate. The Teensy loop now includes incoming-byte CRC and expected-data comparisons, so a direct speed comparison with the older one-way test must account for extra software work.

A complete failed/incomplete DMA transaction requires ESP32 reboot if `needs_reboot=true`. After obtaining the test results, check normal CP/M `DIR`, browser-only VT100, Telnet, `/ws`, updater, Teensy SD features and HP-85 operation. The next milestone is a lower-overhead binary disk sector protocol, **not** an automatic modification of the current storage path.
