# Current project status — EBTKS Z80 / CP/M on HP Series-80

> **NEXT EXPERIMENT — GENERIC BATCHED BINARY SPI STREAM (2026-10-18; SOURCE READY, NOT YET LOCALLY BUILT/HARDWARE-TESTED):** Implemented on **both** `feature/cn3-spi-read-test` branches. User proposed a *general HP/Teensy → ESP32 bulk transport* useful beyond CP/M SD sectors, e.g. future HP-85 graphics/alpha screen dumps. New independent versioned 528-byte CRC-protected frame (16-byte header identifying producer kind, session, block, 512-byte payload length, payload CRC, flags and header CRC) lives in `EBTKS_SPI_Stream_Frame.h` (Teensy) and `spi_stream_frame.h` (ESP32). Kind 1 currently produces **read-only SD-image sectors**; kind 2 reserved for a future CRT producer **not yet implemented**. ESP32 takes the serialized UART mutex, sends **one stream-start UART message per 1/16/128-sector batch** after a separate preflight UART request, then sequentially reaps/queues **528-byte SPI2 RX DMA frames**, while Teensy reads SD file sectors and clocks each frame on SPI0 at fixed **mode 0 / 4 MHz** with HP interrupts enabled. Frame header CRC, payload CRC, session, index, kind and final aggregate CRC checked; SD read errors sent as flagged frames; uncertain DMA ownership requires reboot. New `/spitest` button **Batched framed SD stream (4 MHz, read-only)**, endpoint `POST /api/spitest/stream`; existing synthetic test, original two-UART-round-trip/sector SD diagnostic, CP/M UART/Base64 disk traffic, HP-85, Wi-Fi/OTA and `main` are unchanged. Next: **pull/build/flash both feature branches; test 1, then 16, then 128 real sectors while HP-85 idle, then 128 sectors during SIMON85 graphics; compare wall times to original ~7.710 s / 128 sectors.** [Frame specification, safety notes, build instructions](SPI_STREAM_PROTOCOL.md). We cannot compile/test in this environment; no claimed hardware validation yet.



> **REAL SD→SPI UNDER SIMON85 GRAPHICS LOAD — PASSED (2026-10-18):** The user ran the HP-85's graphics-intensive BASIC/SIMON85 drawing and ellipse-filling program while independently testing **real SD image reads** from `/cpm/disks/A.dsk` over SPI at fixed **4 MHz, mode 0**. **Run 35:** 16/16 × 512 B = 8,192 B PASS, last-sector `sd_crc_prepared=sd_crc_sent=crc_esp=57620`, `aggregate_crc_esp=38047`, `sd_read_us=39883`, `teensy_transfer_us=21918` (1,353–1,389 µs/sector), `wall_us=973037`, **8.222 KiB/s** end-to-end, `needs_reboot=false`. **Run 36:** 128/128 × 512 B = 65,536 B PASS, last-sector `sd_crc_prepared=sd_crc_sent=crc_esp=36668`, `aggregate_crc_esp=46519`, `sd_read_us=323208`, `teensy_transfer_us=177550` (1,343–1,436 µs/sector), `wall_us=7685974`, **8.327 KiB/s** end-to-end, `needs_reboot=false`. Both completed with no CRC mismatches. Compared with earlier **HP-idle** 16/128-sector tests (wall 960,936 / 7,710,205 µs; throughput 8.325 / 8.301 KiB/s), variation was only **+1.26%** wall for 16 sectors and **−0.31%** wall for 128 sectors. The user observed **no perceptible slowdown or interference with HP-85 graphics**. The exact aggregate CRCs also matched the idle runs, verifying equivalent received sector streams. **Milestone reached:** live HP-85 graphics bus service, actual SD card image reads, and SPI DMA reception operated concurrently without errors under these tested conditions. Not a formal worst-case real-time or long-duration guarantee; current test is still **read-only MOSI SD→ESP**, not live CP/M disk-over-SPI or SPI disk writes. **Next engineering work:** reduce two UART round trips per sector and later integrate opt-in CP/M read path with robust fallback; preserve working CP/M UART/Base64 transport and `main` branches.



> **REAL SD→SPI MULTI-SECTOR VALIDATION — 2026-10-18, HP-85 IDLE:** Following the one-sector pass, the user tested actual `/cpm/disks/A.dsk` (8,388,608 bytes) with Teensy SD `O_RDONLY` → ESP32 SPI2 RX at **4 MHz, mode 0**. **16-sector run 33 PASS:** 16/16 × 512 B = 8,192 B, `sd_crc_prepared=sd_crc_sent=crc_esp=57620` (last sector), `aggregate_crc_esp=38047`, `sd_read_us=40209`, `teensy_transfer_us=23149` (per sector 1438–1455 µs), `wall_us=960936`, **8.325 KiB/s** end-to-end, no mismatches/reboot. **128-sector run 34 PASS:** 128/128 × 512 B = 65,536 B, `sd_crc_prepared=sd_crc_sent=crc_esp=36668` (last sector), `aggregate_crc_esp=46519`, `sd_read_us=325208`, `teensy_transfer_us=184970` (1428–1459 µs/sector), `wall_us=7710205`, **8.301 KiB/s** end-to-end, no mismatches/reboot. Each sector independently checked SD prepare CRC, Teensy SPI send CRC and ESP SPI receive CRC; the displayed last-sector CRCs alone are not the only integrity check. In the 128-sector run SD timing represented **4.22%**, SPI loop **2.40%**, and remaining UART/scheduling/protocol coordination **93.38%** of the wall time (~7,200,027 µs); residual is not separately instrumented. **The 16/128-sector runs described in this paragraph were performed with HP-85 idle; subsequent graphics-active runs 35/36 passed, as recorded at the top of this file.** The previous successful 8-MHz SPI+SIMON85 test used synthetic patterns, not real SD reads. **Next stage:** optimize UART/SPI coordination and then safely trial opt-in CP/M disk reads with fallback; the graphics-active SD/SPI stress tests have passed. Existing CP/M UART/Base64 disk service unchanged; no SD writes, no `main` merge.



> **FIRST REAL SD-SECTOR→SPI HARDWARE PASS — 2026-10-18:** User rebuilt/flashed both feature-branch firmwares and ran `/spitest`'s **read-only SD sector test**, **SPI mode 0 at 4 MHz**. Source file `/cpm/disks/A.dsk` (8,388,608 bytes), starting file sector **0**, **1 × 512-byte** sector, run **32**, `state=passed`, `completed_sectors=1`, `needs_reboot=false`, no mismatches. **Three independently reported CRC-16/Modbus values all 14717:** `sd_crc_prepared=14717` (Teensy after SD read), `sd_crc_sent=14717` (Teensy SPI TX), `crc_esp=14717` (ESP32 SPI RX). `sd_first16_hex=00444F574E4C4F4144434F4D00000004`. Teensy measured SD open/seek/read/CRC/close **2,514 µs**, SPI transfer loop **1,444 µs**, total wall **66,117 µs** (other coordination ≈62,159 µs), loop throughput **346.26 KiB/s**, wall throughput **7.56 KiB/s** for this single sector. **This established initial read-only SD→SPI byte integrity for one real file sector; subsequent tests confirmed 16 and 128 sectors while the HP-85 was idle.** Concurrent SD+SIMON85 operation was subsequently verified in runs 35/36. Live CP/M disk reads using SPI, any write path and production transport throughput remain unverified. The working CP/M disk transport remains UART/JSON/Base64; neither `main` nor media modified by these tests. [Test guide](SPI_SD_READ_TEST.md).



> **Read-only SD→SPI diagnostic (2026-10-18, now hardware-tested):** Companion Teensy and ESP32 feature branches now have a separate **4 MHz / SPI mode 0 / MOSI** experiment for real 512-byte sector reads from a named image **directly under `/cpm/disks/`**. The Teensy uses `SD.open(..., O_RDONLY)`, seeks and reads the sector into a dedicated buffer, calculates CRC, and returns only prep metadata over UART (`spi_sd_prepare`). **Only after successful SD preparation** does ESP32 queue its 512-byte SPI2 RX DMA and request `spi_sd_send`, which clocks those exact bytes via Teensy SPI0 at fixed 4 MHz, mode 0. ESP32 verifies prepared CRC, on-wire Teensy CRC, received SPI CRC and sequence/sector; output includes SD read timing, SPI loop timing and end-to-end timing. `/spitest` now includes a distinct **Read SD sectors over SPI (4 MHz)** button, image path, starting sector and 1/16/128 count. Old synthetic full-duplex stress tests and GPIO check remain; **normal CP/M disk transport remains UART/JSON/Base64, no SD write call was added, and `main` remains unchanged**. Goal: repeat 128-sector SD reads while SIMON85 graphics run, and check HP-85 responsiveness. **User locally compiled/flashed and verified 1-, 16-, and 128-sector tests with HP-85 idle**; concurrent SIMON85+SD test remains pending. [Full first-run guide](SPI_SD_READ_TEST.md).



> **Concurrent HP-85 graphics + SPI load test (2026-10-18; physical EBTKS):** User ran a graphics-intensive HP BASIC program using **SIMON85** to draw and fill ellipses *while* exercising the CN3 SPI diagnostic at **8 MHz for 128 × 512-byte sectors**. The SPI test passed, the HP-85 continued drawing throughout, and the user observed **no perceptible graphics slowdown or interference**. This is meaningful hardware evidence that Teensy 4.1 services the live HP-85 bus while running the SPI transfer loop with interrupts enabled. The speed comparison is qualitative (not instrumented), and the SPI sectors were synthetic patterns, **not simultaneous SD-card or CP/M disk I/O**. Continue with opt-in binary sector integration at conservative SPI mode 0 / 4 MHz, retaining the existing UART/Base64 fallback; test SD I/O + HP graphics together before treating the new disk protocol as validated.



> **AUTHORITATIVE HARDWARE BASELINE — 2026-10-18 (supersedes older bring-up notes below):** User physically measured continuity: Teensy module **pin 12 → CN3-29**, ESP32-WROOM module **pad 28 / GPIO17 → CN3-17**; the correct MISO jumper is **CN3-29 ↔ CN3-17** on the actual board. The published V3.0 EBTKS schematic incorrectly places E17 at CN3-15 and our earlier CN3-13 mapping was wrong (CN3-13 is GPIO4). After correcting this, **bidirectional SPI mode 0 at 8 MHz repeatedly passed 128 × 512-byte sectors (65,536 bytes EACH direction, 131,072 total), five cycling patterns, matched MOSI CRC=3840 and MISO CRC=20381 on final block, zero byte mismatches**, Teensy loop total 106,939 us, wall 5,181,721 us. **Mode 1 at 1–4 MHz works**, but at 8 MHz repeatedly fails MISO; run 21's first 16 received bytes were precisely one-bit ROR of expected values, a strong mode/timing bit-slip signature. Its whole-sector CRC did not match a uniform one-bit rotation. Existing ~20 cm Dupont jumpers may affect timing margin. **Do not assume 8 MHz is production-safe:** ESP-IDF documents ~7.2 MHz nominal GPIO-matrix MISO limit. Recommend **mode 0 / 8 MHz only for stress testing**, and **mode 0 / 4 MHz for initial real disk integration** with CRC, sequence, retries and fallback. SD/CP/M disk traffic remains original UART/JSON/Base64; no normal storage code has changed. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md) and [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md). No further GPIO continuity test is needed on this board; the software debug endpoint can remain as optional diagnostic.

**Hardware-validated on 2026-10-17.** This is the current EBTKS-integrated ESP32 implementation, **not** the original 2021/2023 standalone emulator described in the historical README below.

**Last hardware-tested application commit:** [`5f586f35197627319e7b8280ef62e3807b6ceee3`](https://github.com/timob0/EBTKS-Z80-Emulator/commit/5f586f35197627319e7b8280ef62e3807b6ceee3). Later documentation-only commits do not change the firmware. See [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md) for architecture, debugging lessons, and the next hardware milestone.

> **Hardware-confirmed stress result (2026-10-18):** On both repositories' `feature/cn3-spi-read-test` branches, user reports **all SPI tests passed across 1–8 MHz, 1–128 sectors and multiple data patterns**. Longest reported test: 8 MHz, 128 × 512-byte sectors (65,536 B), rotating 5 patterns, run counter 30, zero mismatches, ESP and Teensy final-sector CRC both 3840, aggregate ESP CRC 59319, no reboot required. Teensy timed SPI transfer loops totaled **106,910 us**, min/max **834/836 us** per sector; full wall time **3,845,289 us**. Measured throughput: **598.634 KiB/s** inside Teensy loops versus **16.644 KiB/s** end-to-end with one UART request/reply per sector. This confirms *Teensy MOSI → ESP32 SPI2 receive* under the tested conditions; **ESP32 MISO → Teensy receive, disk data transfers, and a pipelined/burst protocol are not yet tested**. Working CP/M still uses UART/JSON/Base64.

**Next SPI milestone:** Validate reverse-direction (ESP32 MISO → Teensy) data, then prototype low-overhead binary sector exchanges and measure end-to-end latency with real SD I/O. Keep ESP/Teensy `main` baselines and existing CP/M/HP/Wi-Fi/OTA services intact. [Stress-test details](SPI_TEST_README.md).

> **Bidirectional SPI test extension (2026-10-18, source ready for local build):** Both feature branches now support opt-in 512-byte *simultaneous MOSI + MISO* tests. The Teensy SPI0 master checks incoming MISO bytes/CRC while sending MOSI; ESP32 SPI2 slave queues aligned DMA TX/RX buffers and checks MOSI bytes/CRC. The browser `/spitest` offers a `Full duplex` direction selector, retains `MOSI only` regression mode, and starts at 1 MHz / 1 sector. A different deterministic pattern is used on MISO to rule out simple loopback. **This new bidirectional firmware has not yet been compiled or hardware-tested;** only the previous 1–8 MHz receive-only baseline is proven. Follow [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md), pull and build both repos, then test one full-duplex sector at 1 MHz before trying 2/4/8 MHz and 128 sectors. Disk I/O remains UART/JSON/Base64; no media was touched.

> **Full-duplex MISO debugging update (2026-10-18):** User hardware test failed at 8 MHz and again at 1 MHz with matched MOSI CRCs and `first_mismatch_byte=-1`, but `first_mismatch_miso=0` and Teensy MISO RX CRC **47937** (CRC of 512 zero bytes); ESP32 TX CRC was nonzero. Initial jumper mistakenly connected Teensy CN3-29 to ESP CN3-15; user corrected it to **ESP CN3-13 / GPIO17**, but the all-zero symptom persisted. Espressif's ESP-IDF 4.4 SPI slave docs note DMA MISO timing prefers **SPI modes 1/3**; legacy test used mode 0. Both feature branches now include a *new, untested* SPI mode 0/1 selector (matching master and slave) and MISO first-16-byte hex capture (`miso_first16_hex`). **User should pull/build/flash BOTH updated branches, then test full-duplex mode 1 at 1 MHz, one sector**. If still zeros, inspect GPIO17 drive/continuity physically with a logic analyzer, not faster SPI. Preserve previous one-way diagnostic and CP/M UART disk path. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md).

> **MISO isolation follow-up, 2026-10-18:** Even after correcting Teensy CN3-29 ↔ ESP CN3-13, **1 MHz SPI mode 1 full duplex fails** with Teensy `miso_first16_hex=00000000000000000000000000000000`, MISO receive CRC **47937** (512 zero bytes), expected ESP TX CRC **13577**, while MOSI CRCs match (**60981**); first MISO mismatch 0. A new **GPIO-only MISO line test**, not yet built/flashed, is committed to BOTH feature branches: POST `/api/spitest/gpio` or press **Check MISO wire (GPIO LOW/HIGH)** on `/spitest`. ESP32 releases SPI2, drives GPIO17 low and high; Teensy temporarily releases SPI0 and samples pin12 digitally through UART. Inspect `gpio_low_read` and `gpio_high_read`: expected 0/1. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md) for build/use/interpretation. Disk path unaffected.

> **Critical SPI wiring correction (2026-10-18):** Previous project wiring docs incorrectly equated ESP GPIO17 with CN3-13; **CN3-13 is E04/GPIO4**. The published EBTKS V3.0 drawing appears to show E17 on CN3-15, whereas the user reports WROOM pad 28 (GPIO17) wired to CN3-17 on the actual board. **Do not assume either pin without power-off continuity.** All-zero MISO tests so far may reflect incorrect physical routing. No firmware GPIO renumbering: ESP slave MISO remains GPIO17. See [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md).

## What is confirmed on physical EBTKS hardware

| Subsystem / test | Result |
|---|---|
| ESP32 Z80 emulator alongside HP-85 | Works independently; HP-85 continues to run |
| Browser terminal `/z80`, VT100, WebSocket `/z80ws` | Fully usable, including complete `DIR` and Multiplan screens **without any Telnet connection** |
| Telnet port 23 | Works as an independent raw terminal connection; connecting does not reset CP/M |
| WebSocket + Telnet simultaneously | Both receive Z80 console output |
| Microsoft Multiplan, installed for VT100 | Full-screen use works; clean return to CP/M |
| Microsoft INSTALL | Installs and exits cleanly |
| MBASIC, Zork I | Previously tested successfully |
| CP/M warm boot | Clean program exits; repeated shadow restorations without reloading boot images |
| Web Media Manager `/cpmmedia` | Drive assignment, NVS persistence, Apply/Reset Z80 |
| Mixed media | Native 8 MiB R/W disks and original HP 82900 LIF images R/O, with per-drive dynamic CBIOS geometry |
| Wi-Fi provisioning, main EBTKS web UI, OTA updater, `/ws` | Preserved and operational |
| Browser HP-86 / HP 82900 terminal mode | Most display output usable, **cursor/navigation keys remain ineffective in HP-86-installed Multiplan**; leave as known limitation |
| Physical SPI TFT/touchscreen | **Not yet integrated**; planned next milestone |

The **last confirmed fix** corrected console output truncation when only the browser was connected. Z80 UART TX used to overwrite unread bytes in a 1024-byte ring, while asynchronous WebSocket frames could fill their outgoing queue. Telnet TCP writes incidentally provided pacing. The current implementation blocks TX on a full ring, waits for WebSocket queue capacity, batches output, and coalesces browser repaints via `requestAnimationFrame`. After flashing, the user confirmed the browser terminal was very usable without Telnet. Do not regress or remove this backpressure path.

## Tested system snapshot

- **ESP32 repository:** [`timob0/EBTKS-Z80-Emulator`](https://github.com/timob0/EBTKS-Z80-Emulator), branch `main`.
- **Companion Teensy firmware:** [`timob0/ebtks-alt-firmware`](https://github.com/timob0/ebtks-alt-firmware), branch `main`. No Teensy update was required for the recent terminal/warm-boot changes.
- **CP/M:** native CP/M 2.2, boot files on the **Teensy's** SD card under `/cpm`; separate virtual Z80 machine, not the original HP 82900A boot ROM.
- **Media example from successful testing:** A: `/cpm/disks/A.dsk` (native 8 MiB, R/W); B: `cpm_1.lif`; C: `cpm_2_MP.lif`; D: `cpm_3_f80.lif` (HP 82900 LIF, R/O). Actual assignments are user-specific.
- **Active drive assignment authority:** ESP32 NVS namespace **`cpmmount`**, not `/cpm/mounts.cfg`. The SD configuration file is accepted for initial migration only; see `ESP32_Z80_Emulator/cpm_mount_store.h`. Changes become active at Z80 Reset/Apply, never mid-session.
- **Active disk location:** Teensy SD `/cpm/disks`, not ESP32 flash or a second ESP32 SD slot.

## Browser routes and workflow

| Route | Purpose |
|---|---|
| `/` | Existing EBTKS web UI |
| `/updater` | OTA firmware update |
| `/ws` | Existing Teensy EBTKS browser JSON bridge |
| `/z80` | Raw / VT100 / HP-86 Z80 browser terminal |
| `/z80ws` | Z80 terminal binary WebSocket |
| `/cpmmedia` | CP/M A:P media manager |
| `/crt` | HP-85 CRT snapshot display |
| `/z80debug` | **Memory-backed** ESP32/Z80 diagnostic log |
| `/z80debug-clear` | Clear diagnostic log |

Standard local build from repository root:

```sh
git pull
pio run -t clean
pio run
```

OTA image: `.pio/build/esp32dev/firmware.bin`. Flash it through the existing browser updater. ESP32 only for present firmware; do not flash the Teensy merely to update ESP terminal code.

## Critical invariants

1. **ESP32↔Teensy UART is exclusively hardware protocol traffic.** `HardwareSerial TeensySerial(1)`, 115200 8N1, GPIO3 RX/GPIO1 TX. Never route `Serial.print`, WiFiManager messages, terminal output, or debug data onto those pins. The current `Serial` macro redirects logging to the in-memory `/z80debug` sink.
2. **Teensy owns the SD card and the HP bus.** ESP32 CP/M disk access uses serialized, CRC-checked JSON/Base64 transactions through the existing link. Keep `/ws`, storage and other link users serialized.
3. **Preserve the CP/M ABI:** Z80 page zero at `0005h` must continue to expose the real BDOS entry `D806h` because applications inspect `0006h/0007h` to determine the memory limit. Never redirect that vector to a high-memory shim.
4. **Preserve warm boot:** a 5632-byte heap-allocated pristine copy of `D000–E5FF`; WBOOT trap at `E603h` restores it. BDOS function 0 is routed through a shim behind `D806h`. Multiplan and INSTALL exits are the regression test.
5. **Preserve browser-only console backpressure:** Z80 ring must not overwrite unread bytes; WebSocket queue must not be overrun. Telnet must remain optional.
6. **Do not assume available ESP32 pins or RAM for a new TFT.** Check actual EBTKS board connector/schematic, ESP32 variant and backlight load before modifying the pin map or adding framebuffers.

## Known limitations / open issues

- HP-86 mode: arrow keys flash Multiplan's command area but do not navigate; a special-key `CONIN` compatibility shim was attempted and is still present in the tested code, **without resolving this problem**. VT100 is preferred and fully functional. Do not spend time on HP-86 unless specifically requested.
- The message `Z80 halted @ xxxx - restarting virtual machine` is ambiguous: deliberate Web/Media Manager reset can also set `RUN=false`. It is not necessarily a Z80 `HALT` opcode.
- HP LIF disks are deliberately read-only; unknown/raw CP/M image geometries unsupported unless a profile is added.
- Current historical CBIOS does not reliably propagate disk write errors to CP/M; do not try to write to R/O or unmounted drives.
- Older milestone docs remain valuable history but some implementation details have changed. Especially: `CPM_MEDIA_MANAGER.md` describes the earlier SD mount table, and `MILESTONE2_EBTKS.md` mentions an earlier diagnostic UART. The present source + this status take precedence.

## Next milestone

**Integrate the ordered 4-inch SPI TFT with optional capacitive touch, preserving the working VT100 terminal and all disk/HP services.** Expected display module: Hosyond 4.0-inch, advertised ST7796S (verify actual controller on arrival), native 320×480, landscape 480×320, FT6336U touch over I²C. An 80×24 screen at 6×12 pixels/cell uses 480×288 and leaves 32 pixels for status/touch controls.

The browser parser lives in `web_terminal_v2.h` **as JavaScript**, so a physical SPI TFT cannot use it directly. Design an ESP-side terminal model/parser or an explicitly shared screen-state protocol rather than assuming JavaScript can be reused as C++. Start with pinout and minimal panel tests, then the renderer and keyboard/touch plan. See [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md).
