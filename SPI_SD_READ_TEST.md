# Read-only SD sector → SPI transport test (4 MHz, mode 0)

**Status: real SD-to-SPI hardware tests PASSED at 4 MHz, SPI mode 0 for 1, 16 and 128 sectors, INCLUDING 16- and 128-sector runs while SIMON85 graphics executed on the HP-85.** This is a diagnostic step between the proven *synthetic* SPI tests and changing the live CP/M disk transport. The real CP/M `disk_read`/`disk_write` requests still use the established UART/JSON/Base64 path. **This new test never writes to SD or changes CP/M mounts.** Both feature branches must be pulled and flashed together.

## Hardware results

> **REAL SD→SPI UNDER SIMON85 GRAPHICS LOAD — PASSED (2026-10-18):** The user ran the HP-85's graphics-intensive BASIC/SIMON85 drawing and ellipse-filling program while independently testing **real SD image reads** from `/cpm/disks/A.dsk` over SPI at fixed **4 MHz, mode 0**. **Run 35:** 16/16 × 512 B = 8,192 B PASS, last-sector `sd_crc_prepared=sd_crc_sent=crc_esp=57620`, `aggregate_crc_esp=38047`, `sd_read_us=39883`, `teensy_transfer_us=21918` (1,353–1,389 µs/sector), `wall_us=973037`, **8.222 KiB/s** end-to-end, `needs_reboot=false`. **Run 36:** 128/128 × 512 B = 65,536 B PASS, last-sector `sd_crc_prepared=sd_crc_sent=crc_esp=36668`, `aggregate_crc_esp=46519`, `sd_read_us=323208`, `teensy_transfer_us=177550` (1,343–1,436 µs/sector), `wall_us=7685974`, **8.327 KiB/s** end-to-end, `needs_reboot=false`. Both completed with no CRC mismatches. Compared with earlier **HP-idle** 16/128-sector tests (wall 960,936 / 7,710,205 µs; throughput 8.325 / 8.301 KiB/s), variation was only **+1.26%** wall for 16 sectors and **−0.31%** wall for 128 sectors. The user observed **no perceptible slowdown or interference with HP-85 graphics**. The exact aggregate CRCs also matched the idle runs, verifying equivalent received sector streams. **Milestone reached:** live HP-85 graphics bus service, actual SD card image reads, and SPI DMA reception operated concurrently without errors under these tested conditions. Not a formal worst-case real-time or long-duration guarantee; current test is still **read-only MOSI SD→ESP**, not live CP/M disk-over-SPI or SPI disk writes. **Next engineering work:** reduce two UART round trips per sector and later integrate opt-in CP/M read path with robust fallback; preserve working CP/M UART/Base64 transport and `main` branches.

> **REAL SD→SPI MULTI-SECTOR VALIDATION — 2026-10-18, HP-85 IDLE:** Following the one-sector pass, the user tested actual `/cpm/disks/A.dsk` (8,388,608 bytes) with Teensy SD `O_RDONLY` → ESP32 SPI2 RX at **4 MHz, mode 0**. **16-sector run 33 PASS:** 16/16 × 512 B = 8,192 B, `sd_crc_prepared=sd_crc_sent=crc_esp=57620` (last sector), `aggregate_crc_esp=38047`, `sd_read_us=40209`, `teensy_transfer_us=23149` (per sector 1438–1455 µs), `wall_us=960936`, **8.325 KiB/s** end-to-end, no mismatches/reboot. **128-sector run 34 PASS:** 128/128 × 512 B = 65,536 B, `sd_crc_prepared=sd_crc_sent=crc_esp=36668` (last sector), `aggregate_crc_esp=46519`, `sd_read_us=325208`, `teensy_transfer_us=184970` (1428–1459 µs/sector), `wall_us=7710205`, **8.301 KiB/s** end-to-end, no mismatches/reboot. Each sector independently checked SD prepare CRC, Teensy SPI send CRC and ESP SPI receive CRC; the displayed last-sector CRCs alone are not the only integrity check. In the 128-sector run SD timing represented **4.22%**, SPI loop **2.40%**, and remaining UART/scheduling/protocol coordination **93.38%** of the wall time (~7,200,027 µs); residual is not separately instrumented. **These 16/128-sector tests were performed with the HP-85 idle**, not simultaneously with SIMON85. The previous successful 8-MHz SPI+SIMON85 test used synthetic patterns, not real SD reads. **Next test:** repeat the real-SD 128-sector test at 4 MHz mode 0 while SIMON85 draws/fills ellipses; verify graphics progress/HP bus responsiveness and disk CRCs. Existing CP/M UART/Base64 disk service unchanged; no SD writes, no `main` merge.

> **FIRST REAL SD-SECTOR→SPI HARDWARE PASS — 2026-10-18:** User rebuilt/flashed both feature-branch firmwares and ran `/spitest`'s **read-only SD sector test**, **SPI mode 0 at 4 MHz**. Source file `/cpm/disks/A.dsk` (8,388,608 bytes), starting file sector **0**, **1 × 512-byte** sector, run **32**, `state=passed`, `completed_sectors=1`, `needs_reboot=false`, no mismatches. **Three independently reported CRC-16/Modbus values all 14717:** `sd_crc_prepared=14717` (Teensy after SD read), `sd_crc_sent=14717` (Teensy SPI TX), `crc_esp=14717` (ESP32 SPI RX). `sd_first16_hex=00444F574E4C4F4144434F4D00000004`. Teensy measured SD open/seek/read/CRC/close **2,514 µs**, SPI transfer loop **1,444 µs**, total wall **66,117 µs** (other coordination ≈62,159 µs), loop throughput **346.26 KiB/s**, wall throughput **7.56 KiB/s** for this single sector. **This established initial read-only SD→SPI byte integrity for one real file sector; subsequent tests confirmed 16 and 128 sectors while the HP-85 was idle.** SD+SIMON85 concurrent operation was subsequently verified in 16- and 128-sector tests. CP/M reads using SPI, any write path and production transport throughput remain unverified; next optimize the per-sector UART/SPI handshake. The working CP/M disk transport remains UART/JSON/Base64; neither `main` nor media modified by these tests. [Test guide](SPI_SD_READ_TEST.md).

## Exactly what is being tested

- ESP32 web UI `/spitest` has an additional **Read SD sectors over SPI (4 MHz)** action. The operator chooses an image under `/cpm/disks/`, an absolute 512-byte file-sector index and the number of sectors (1, 16 or 128).
- The Teensy opens the image with **`O_RDONLY`**, seeks to the requested offset, reads **512 actual bytes** into a dedicated buffer, calculates a CRC-16/Modbus, closes the file and responds to `spi_sd_prepare` over the existing UART with its status and CRC. No Base64 image payload is sent over UART.
- **Only if preparation succeeds** does ESP32 arm its RX DMA descriptor. ESP then sends a **separate `spi_sd_send` UART request**; Teensy clocks the prepared raw bytes over **SPI0 master, 4,000,000 Hz, mode 0**, with HP-bus interrupts enabled. ESP32 receives on SPI2 slave DMA, computes its own CRC and compares it with both Teensy's SD buffer CRC and Teensy's actual SPI loop CRC. Sequence and absolute sector indices are checked in both UART phases.
- This *two-phase per-sector protocol* intentionally avoids leaving a queued DMA descriptor waiting if SD opening, seeking or reading fails. If the second phase is interrupted after DMA is queued, `needs_reboot=true` protects DMA ownership; reboot ESP32 before retrying.
- All read-only data stays out of the Z80 RAM and CP/M live disk I/O. The test does **not** run CP/M filesystem code, modify a mounted image, perform real CP/M writes, or benchmark the optimized future production protocol. `end_to_end_kib_s` includes SD open/seek/read/close, two UART command/reply cycles, SPI transfer, task scheduling and checks. `spi_kib_s` measures the Teensy software transfer loop only.

## Board and firmware

Correct MISO jumper on the **physically continuity-tested board**: Teensy pin 12/CN3-29 ↔ ESP32 GPIO17 (WROOM pad 28)/CN3-17. The published EBTKS V3.0 schematic is incorrect about this CN3 pin; see [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md). Keep all five jumpers and the 10 kΩ CS_N pullup. Existing synthetic mode-0 8 MHz full-duplex 128-sector runs, even while SIMON85 drew/filled ellipses on the HP-85, have already passed. New *SD read + SPI + graphics* concurrency is **not** yet validated.

The new SD test is deliberately **fixed at 4 MHz and mode 0** regardless of the synthetic-test clock or mode selectors. It uses MOSI only (Teensy SD → ESP32); bidirectional signal integrity has already been demonstrated by the older synthetic test.

On **Teensy** checkout:

```sh
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e teensy41
pio run -e teensy41 -t upload
```

On **ESP32** checkout:

```sh
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Upload `.pio/build/esp32dev/firmware.bin` with the normal `/updater` page; no flash erase or SD card reformat. Retain your known-good firmwares. Leave CP/M idle at the prompt for diagnostics and do not change drive mounts mid-test.

## Suggested regression sequence

1. Verify SIMON85 normal graphics operation and CP/M `DIR` before testing. Open `http://<EBTKS-IP>/spitest`. Select synthetic **mode 0, 4 MHz, 1 sector, Full duplex** and verify PASS.
2. Find a valid disk image filename in `/cpmmedia` or the existing SD file service, e.g. `/cpm/disks/A.dsk` **only if that file exists**. The SD test rejects missing, nested, or `../` paths and sector reads beyond EOF.
3. Under **Real SD sector read → SPI**, enter the exact image path and starting sector `0`. Set *Transfer count* to **1 sector** and press **Read SD sectors over SPI (4 MHz)**. Confirm `"test_kind":"sd_readonly_spi"`, `"state":"passed"`, `"completed_sectors":1`, `"sd_crc_match":true` and `"needs_reboot":false`. Check that `sd_crc_prepared == sd_crc_sent == crc_esp`. `sd_first16_hex` is the first 16 bytes from the **last received sector**, for independent manual inspection if desired.
4. Repeat with **16 sectors**, then **128 sectors** if the selected image contains at least 64 KiB starting from the requested sector. This opens, reads and closes the image for each separate sector, intentionally stressing SD access.
5. **Completed successfully on 2026-10-18:** 16 and 128 real-SD sectors while a graphics-intensive HP BASIC / SIMON85 ellipse drawing/fill program ran, with no observable graphics slowdown and matching CRCs.
6. Optionally test a higher file sector index, to exercise SD seeking, and a read-only LIF image if one is listed in Media Manager. Never select a nonexistent file for the first run.

The SD test only recognizes image paths directly below `/cpm/disks/`; it does not accept arbitrary SD-card paths. If the image is missing, the test should fail at the *prepare* phase **without setting** `needs_reboot`; no DMA transaction is queued. The Teensy reports details on ESP32 Serial.

The REST API is also available:

```sh
curl -X POST 'http://<EBTKS-IP>/api/spitest/sd?path=%2Fcpm%2Fdisks%2FA.dsk&first=0&sectors=1'
curl 'http://<EBTKS-IP>/api/spitest'
```

Interpret the new JSON keys:

| Key | Meaning |
|---|---|
| `test_kind` | `sd_readonly_spi` (versus `spi` or `gpio_miso`) |
| `sd_read_only`, `sd_file`, `sd_start_sector` | Selected read-only source and first 512-byte file-sector index |
| `sd_file_bytes` | Teensy reported file size, capped at 32 bits |
| `sd_read_us` | Sum of SD open/seek/read/close elapsed µs, including local CRC calculation |
| `sd_crc_prepared` | Teensy's CRC of last file sector after SD read |
| `sd_crc_sent` | Teensy's CRC of bytes transmitted over SPI |
| `crc_esp` | ESP32's CRC of last sector actually received through SPI RX DMA |
| `sd_crc_match` | Last-sector CRC agreement (only meaningful with `state=passed` or `completed_sectors>0`) |
| `sd_first16_hex` | First 16 actual bytes received on the most recent sector |
| `teensy_transfer_us`, `wall_us` | SPI loop sum and total diagnostic wall clock, including UART and SD |

**Failure policy:** Stop at the first failed sector. Save the full JSON and ESP32 serial log. On SD open/EOF failures no DMA has been queued, so correct the path/index and retry. On uncertain DMA completion, **reboot the ESP32**. Do not silently fall back within this diagnostic, because the test needs to expose transport errors; when a real production CP/M SPI path is developed, that path will require explicit fallback/retry and CRC guarantees.

## Remaining milestones

This test does not yet exercise **CP/M program execution using disk data supplied by SPI**, **SPI disk writes**, or an optimized low-overhead streaming protocol. These follow only after the read-only SD load test and preservation of HP-85, Wi-Fi, OTA and Z80 regressions. Keep `main` untouched.
