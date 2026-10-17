# CN3 SPI speed and reliability experiment (feature branches)

**Status (2026-10-18):** Original 1 MHz / 512-byte test **passed on physical hardware** (reported by user: 13 runs, CRC ESP=Teensy=59244, transfer-loop time 4543 us, no mismatch). The extended multi-speed/multi-sector stress test in this branch is **new source, not yet compiled or hardware-validated**. Do not merge into `main` yet.

The two matching branches are:

- ESP32: https://github.com/timob0/EBTKS-Z80-Emulator/tree/feature/cn3-spi-read-test
- Teensy: https://github.com/timob0/ebtks-alt-firmware/tree/feature/cn3-spi-read-test

The purpose is to measure reliability at 1, 2, 4, and 8 MHz and verify 128 consecutive 512-byte transactions (64 KiB), without modifying any live disk image. ESP32 remains the SPI2 slave receiving data; Teensy remains SPI0 master sending known patterns. **Normal CP/M disk access still uses UART/JSON/Base64.** The web UI, OTA, HP bus ISR, SD handling, and UART control protocol are preserved.

## Wiring and prerequisites

Use exactly the wiring verified for the first SPI test. See [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md) for detailed orientation and 3.3 V cautions.

| Wire | Teensy / CN3 | ESP32 / CN3 |
|---|---|---|
| MOSI | T11 / pin 33 | E22 / pin 21, GPIO22 |
| MISO | T12 / pin 29 | E17 / pin 13, GPIO17 |
| CLK | T13 / pin 24 | E16 / pin 14, GPIO16 |
| CS_N | T26 / pin 30 | E34 / pin 4, GPIO32 |
| READY | T06 / pin 26 | E27 / pin 8, GPIO27 |

Keep the external **10 kΩ** pull-up from CN3 pin 4 to 3.3 V (CN3 pin 36). Do **not** tie CS directly to 3.3 V. Firmware must have `"CRTRemote": true` in Teensy's SD-root `CONFIG.TXT` for the existing UART control service.

## Build and flash both feature branches

From the Teensy checkout:

```sh
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e teensy41
# Use your established Teensy 4.1 USB upload method:
pio run -e teensy41 -t upload
```

From the ESP32 checkout:

```sh
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Flash ESP32 application image `.pio/build/esp32dev/firmware.bin` using the existing EBTKS web updater (`/updater`). Do not erase/reformat flash or the Teensy SD card. These changes were pushed as source only: **compilation and hardware flashing remain to be performed locally**.

## Run the speed and reliability matrix

First verify the normal HP-85 operation, `/z80` CP/M prompt, and `/updater` page. Leave CP/M idle at the prompt while testing. Open:

```text
http://<EBTKS-IP>/spitest
```

Choose SPI clock, transfer count, and pattern; press **Run SPI test**. The selectors offer:

- Clock: **1, 2, 4, 8 MHz**
- Count: **1**, **16**, or **128** individual 512-byte transactions (512 B, 8 KiB, 64 KiB)
- Pattern: sequence/ramp, all 00, all FF, alternating 55/AA, walking one, or **Cycle five patterns** (rotates by sector)
- Every sector gets its own sequence and block index; ESP32 checks every byte plus CRC-16/Modbus, and compares with Teensy's returned CRC, sequence, block, pattern, length and actual clock selection.

Recommended sequence (stop at first failure and capture diagnostics):

1. **1 MHz, 1 sector, sequence/ramp** — regression of the previous success.
2. **2 MHz, 1 sector, cycle patterns** — short clock bring-up (single sector uses the first pattern).
3. **4 MHz, 1 sector, cycle patterns** — higher clock bring-up.
4. **4 MHz, 128 sectors, cycle patterns** — 64 KiB of verified data, exercising all five patterns.
5. **8 MHz, 1 sector, sequence/ramp**, then **8 MHz, 128 sectors, cycle patterns** if the short test passes.
6. Optionally repeat the 128-sector test at **1 and 2 MHz** and run individual fixed patterns. Check HP-85 keyboard/display responsiveness during a long test.

If an attempt fails, save the full `/api/spitest` JSON and `/z80debug` lines, including the failing sector, mismatch byte, CRCs, and reboot flag. If `needs_reboot=true`, **reboot ESP32 before another test**; do not try to reuse a possibly incomplete DMA transaction. Normal disk I/O is unchanged even when a test fails.

For automation or manual HTTP requests, POST URL query parameters, e.g.:

```sh
curl -X POST 'http://<EBTKS-IP>/api/spitest?hz=4000000&sectors=128&pattern=5'
curl 'http://<EBTKS-IP>/api/spitest'
```

Values: `hz=1000000|2000000|4000000|8000000`, `sectors=1|16|128`, `pattern=0|1|2|3|4|5` (5 cycles).

## Interpret the results

`GET /api/spitest` provides the original status keys plus:

| Field | Meaning |
|---|---|
| `state` / `needs_reboot` | idle, running, passed, failed; protect incomplete DMA |
| `clock_hz` / `pattern` / `sectors` | selected configuration; pattern 5 means cycling |
| `completed_sectors`, `completed_bytes` | verified progress, not merely attempted transfers |
| `failed_sector`, `failed_pattern`, `first_mismatch_byte` | zero-based first failing sector/pattern/byte; -1 when none |
| `crc_esp`, `crc_teensy` | CRC-16 of **last processed** sector; equal on success |
| `aggregate_crc_esp` | continuous CRC of all received sectors; local diagnostic only |
| `teensy_transfer_us` | **sum of Teensy's timed SPI.transfer/CRC loops** over verified sectors |
| `teensy_min_us`, `teensy_max_us` | minimum/maximum individual verified-sector loop times |
| `wall_us` | full elapsed run, including UART command/reply on every sector |
| `spi_kib_s` | verified bytes / summed Teensy loop time (KiB/s) |
| `end_to_end_kib_s` | verified bytes / full run duration (KiB/s) |

For a full passed 128-sector run, `completed_sectors=128`, `completed_bytes=65536`, `failed_sector=-1`, and `first_mismatch_byte=-1`. Note that `teensy_transfer_us` measures software CRC and host bus interrupt overhead **as well as the wire transfer**, not an isolated SPI hardware throughput.

**Important limitation:** 128 sectors means **128 sequential independent SPI DMA transactions**, each initiated by a short UART `spi_probe` command and confirmed by a UART CRC/status reply. It does **not** mean one uninterrupted 64 KiB SPI transaction, and `end_to_end_kib_s` is therefore conservative for a future optimized binary disk protocol. This design intentionally retains the proven safe READY/DMA synchronization between blocks. The ESP32 DMA buffer remains only 512 bytes.

## What this does NOT test

- SPI MISO data returning from ESP32 to Teensy (still unverified).
- Real file access, sector reads, disk writes, or CP/M ABI.
- A pipelined/burst binary sector transport (that is a later step).
- Data integrity during arbitrary DMA timeouts; incomplete transactions force reboot before retry.
- A compile/test result on hardware until explicitly reported.

**Regression after flashing:** CP/M `DIR`, browser-only VT100, Telnet, main EBTKS web UI and updater, `/ws`, Teensy SD services, and uninterrupted HP-85 operation. See [PROJECT_STATUS.md](PROJECT_STATUS.md) and [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md) for the known-good baseline.
