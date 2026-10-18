# CN3 SPI full-duplex diagnostic — build and first hardware test

**Status: HARDWARE-VALIDATED full duplex.** See the verified result section below for the measured CN3-17 ↔ CN3-29 wiring, repeatable 8 MHz mode-0 pass and 8 MHz mode-1 failure. Historical bring-up failures below occurred before correct wiring.

**WIRING RESOLUTION:** Measured GPIO17 (ESP module pad 28) to ESP CN3-17; Teensy module pin 12 / CN3-29 connected physically; see verified section below. The published EBTKS V3.0 schematic contradicts this measurement and earlier CN3-13 instructions were wrong.

## Verified bidirectional hardware results (2026-10-18; supersedes earlier bring-up notes)

**Physical mapping settled by user multimeter tests:** On the tested EBTKS board, ESP32-WROOM module **pad 28 (GPIO17)** is connected to **CN3-17**. The Teensy module **pin 12** connects to **CN3-29**. The actual successful MISO jumper is **CN3-17 ↔ CN3-29**. Published EBTKS V3.0 schematic labels E17 on CN3-15, while CN3-13 is E04/GPIO4; **our earlier CN3-13 prescription was incorrect**. The user verified end-to-end connectivity between both module pins after rewiring. Do not assume the same assignment for other board revisions without continuity checks.

**Full-duplex mode 0 at 8 MHz: repeatably PASSED** with 128 sectors / 512 bytes per sector / pattern mode 5 (cycling all five data patterns), both directions verified byte for byte and with CRCs. User repeated this several times successfully:

```json
{
  "state": "passed", "runs": 26, "clock_hz": 8000000,
  "bidirectional": true, "spi_mode": 0, "pattern": 5,
  "sectors": 128, "completed_sectors": 128,
  "completed_bytes": 65536, "completed_bytes_total": 131072,
  "first_mismatch_byte": -1, "first_mismatch_miso": -1,
  "crc_esp": 3840, "crc_teensy": 3840,
  "miso_crc_esp_tx": 20381, "miso_crc_teensy_rx": 20381,
  "aggregate_crc_esp": 3739, "aggregate_miso_crc_esp_tx": 48510,
  "teensy_transfer_us": 106939,
  "teensy_min_us": 835, "teensy_max_us": 836,
  "wall_us": 5181721,
  "spi_kib_s": 598.4720261,
  "end_to_end_kib_s": 12.35110883,
  "full_duplex_total_kib_s": 1196.944052,
  "needs_reboot": false
}
```

This represents **64 KiB per direction / 128 KiB total** in ~5.18 s of diagnostic wall time; the SPI transfer-loop component averaged ~835.5 µs/sector. The much longer wall time still includes per-sector UART coordination, not an uninterrupted 128-KiB burst.

**Mode 1 findings:** 1 MHz full-duplex passed after the correct jumper was installed; user reports mode 1 passes up through 4 MHz. However, **8 MHz mode 1 repeatedly fails on MISO**, while MOSI continues to verify correctly. Representative failed mode-1 8 MHz sector: first MISO mismatch byte 0, ESP TX CRC 50622, Teensy RX CRC 41658, raw first 16 receive bytes `A68B94FFB2AFD4DBE6F3A49F8AEFF4C3`. These are **not all zeros** as in the earlier bad-wiring tests. This is a mode-dependent high-speed MISO problem, potentially due to launch/sample phase, GPIO matrix delay, wiring capacitance/reflections, or timing margin; the cabling alone is **not established as the cause**. See [Espressif's SPI slave timing guidance](https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/api-reference/peripherals/spi_slave.html): GPIO-matrix MISO output has a conservative ~7.2 MHz limit under nominal load. Successful 8 MHz mode-0 runs are a valuable empirical baseline, **not a guaranteed production margin** with 20-cm Dupont jumpers.

**Byte-level diagnostic (8 MHz mode-1 failure, run 21):** Using `seq=(runs + sector) & 255` = 21, sector 0, pattern 0, the expected ESP MISO first-16 bytes were `4D1729FF655FA9B7CDE7493F15DFE987`. The Teensy actually reported `A68B94FFB2AFD4DBE6F3A49F8AEFF4C3`. **Each of those 16 actual bytes equals the corresponding expected byte rotated right by exactly 1 bit**, pointing specifically to SPI sample/launch edge misalignment in mode 1 at 8 MHz, rather than a dead/open MISO wire. The expected full-sector CRC=50622, observed full-sector CRC=41658; the latter **does not** equal a whole-sector one-bit rotation CRC (27231), so do not extrapolate beyond the captured 16 bytes. In the successful mode-0 run 26, the reported MISO sample for the final sector (block 127, `seq=153`, pattern 2) `2C21160B00F5EADFD4C9BEB3A89D9287` matches the locally generated expected bytes exactly.

**Operating recommendation:** Use **mode 0 / 8 MHz** for further supervised stress tests with this hardware; if integrating real CP/M disk I/O before hardware signal integrity is improved, choose **mode 0 / 4 MHz** as a conservative starting point with CRC/sequence checking and retry/fallback. Test large numbers of sectors, all patterns, HP-85 activity, and both directions before deployment. CP/M disk storage still uses its original UART/JSON/Base64 path: these are transport-only tests, with no SD or disk images accessed.

## Design and safety

- Full-duplex **512-byte SPI mode-0 or mode-1 transaction**: Teensy SPI0 master clocks MOSI data and simultaneously receives MISO data; ESP32 SPI2 slave RX DMA receives MOSI and TX DMA sends a different deterministic pattern on MISO. Both ends use the same selected mode. The diagnostic now defaults to the physically proven **mode 0**, while mode 1 remains selectable. Mode 1 works through 4 MHz but repeatedly fails at 8 MHz on this hardware. See [ESP-IDF v4.4 SPI slave restrictions](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/spi_slave.html).
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

## Historical GPIO isolation diagnostic (superseded by continuity measurement and working SPI)

**Observed hardware failure:** With the jumper corrected to ESP GPIO17 (WROOM pad 28; connector pin pending continuity) → Teensy CN3-29 (T12), **full-duplex mode 1 at 1 MHz still fails**. The user reported `miso_first16_hex=00000000000000000000000000000000`, `miso_crc_teensy_rx=47937` (512 zero bytes), `miso_crc_esp_tx=13577`, `first_mismatch_miso=0`, while both MOSI CRCs match at 60981. Failure occurs on sector 0; longer runs add no diagnostic value.

**Optional diagnostic** (source present; not needed now that the user has verified continuity and working SPI) tests GPIO electrical continuity *without the SPI peripheral or DMA*. ESP32 temporarily releases SPI2 when idle, drives its **GPIO17 (physically verified as CN3-17 on this board)** LOW and HIGH in turn as a standard output, then sends a `spi_gpio_probe` command over the existing UART. Teensy briefly releases SPI0 and samples physical pin **12 (CN3-29)** as a normal GPIO input with pulldown, returning the observed level. SPI0 is restored; ESP32 sets GPIO17 to input afterward. No CS/SCLK transactions and no disk access take place.

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

## Historical first-test plan (superseded by successful mode-0 8 MHz bidirectional tests)

With CP/M idle at the `A>` prompt and HP-85 normal, open `http://<EBTKS-IP>/spitest`.

1. **Full duplex, SPI mode 0, 1 MHz, 1 sector, sequence/ramp** — short sanity check of the now-verified MISO path. Collect `spi_mode`, `miso_first16_hex`, both MISO CRCs and `first_mismatch_miso` even if it fails.
2. **MOSI only, SPI mode 0, 1 MHz, 1 sector, sequence/ramp** — check the proven regression path (switching mode triggers safe ESP32 SPI slave reinitialization between runs).
3. For new builds test mode 0 at 2/4/8 MHz before long runs; note mode 1 has repeated MISO failures at 8 MHz on this hardware.
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
