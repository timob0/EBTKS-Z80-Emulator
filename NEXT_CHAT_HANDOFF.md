# Handoff / durable project memory for the next development chat

> **AUTHORITATIVE HARDWARE BASELINE — 2026-10-18 (supersedes older bring-up notes below):** User physically measured continuity: Teensy module **pin 12 → CN3-29**, ESP32-WROOM module **pad 28 / GPIO17 → CN3-17**; the correct MISO jumper is **CN3-29 ↔ CN3-17** on the actual board. The published V3.0 EBTKS schematic incorrectly places E17 at CN3-15 and our earlier CN3-13 mapping was wrong (CN3-13 is GPIO4). After correcting this, **bidirectional SPI mode 0 at 8 MHz repeatedly passed 128 × 512-byte sectors (65,536 bytes EACH direction, 131,072 total), five cycling patterns, matched MOSI CRC=3840 and MISO CRC=20381 on final block, zero byte mismatches**, Teensy loop total 106,939 us, wall 5,181,721 us. **Mode 1 at 1–4 MHz works**, but at 8 MHz repeatedly fails MISO; run 21's first 16 received bytes were precisely one-bit ROR of expected values, a strong mode/timing bit-slip signature. Its whole-sector CRC did not match a uniform one-bit rotation. Existing ~20 cm Dupont jumpers may affect timing margin. **Do not assume 8 MHz is production-safe:** ESP-IDF documents ~7.2 MHz nominal GPIO-matrix MISO limit. Recommend **mode 0 / 8 MHz only for stress testing**, and **mode 0 / 4 MHz for initial real disk integration** with CRC, sequence, retries and fallback. SD/CP/M disk traffic remains original UART/JSON/Base64; no normal storage code has changed. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md) and [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md). No further GPIO continuity test is needed on this board; the software debug endpoint can remain as optional diagnostic.

**Read first:** [PROJECT_STATUS.md](PROJECT_STATUS.md) gives the concise status and authoritative hardware-tested baseline. This file preserves decisions, why they were made, and the likely next task. Written 2026-10-17.

> **Hardware-confirmed stress result (2026-10-18):** On both repositories' `feature/cn3-spi-read-test` branches, user reports **all SPI tests passed across 1–8 MHz, 1–128 sectors and multiple data patterns**. Longest reported test: 8 MHz, 128 × 512-byte sectors (65,536 B), rotating 5 patterns, run counter 30, zero mismatches, ESP and Teensy final-sector CRC both 3840, aggregate ESP CRC 59319, no reboot required. Teensy timed SPI transfer loops totaled **106,910 us**, min/max **834/836 us** per sector; full wall time **3,845,289 us**. Measured throughput: **598.634 KiB/s** inside Teensy loops versus **16.644 KiB/s** end-to-end with one UART request/reply per sector. This confirms *Teensy MOSI → ESP32 SPI2 receive* under the tested conditions; **ESP32 MISO → Teensy receive, disk data transfers, and a pipelined/burst protocol are not yet tested**. Working CP/M still uses UART/JSON/Base64.

> **Immediate follow-up:** Current development focus is SPI binary transport, *not* the older TFT milestone in the historical sections below. First test reverse ESP32→Teensy MISO transfer (the wire was connected but not data-validated); then reduce per-sector UART coordination and integrate *opt-in*, CRC/sequence-protected 512-byte SD/CP/M transfers. Preserve working CP/M, HP bus ISR, UART protocol, WebSockets, OTA, and the last tested `main` state. [SPI test record](SPI_TEST_README.md).

> **Bidirectional SPI test extension (2026-10-18, source ready for local build):** Both feature branches now support opt-in 512-byte *simultaneous MOSI + MISO* tests. The Teensy SPI0 master checks incoming MISO bytes/CRC while sending MOSI; ESP32 SPI2 slave queues aligned DMA TX/RX buffers and checks MOSI bytes/CRC. The browser `/spitest` offers a `Full duplex` direction selector, retains `MOSI only` regression mode, and starts at 1 MHz / 1 sector. A different deterministic pattern is used on MISO to rule out simple loopback. **This new bidirectional firmware has not yet been compiled or hardware-tested;** only the previous 1–8 MHz receive-only baseline is proven. Follow [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md), pull and build both repos, then test one full-duplex sector at 1 MHz before trying 2/4/8 MHz and 128 sectors. Disk I/O remains UART/JSON/Base64; no media was touched.

> **Full-duplex MISO debugging update (2026-10-18):** User hardware test failed at 8 MHz and again at 1 MHz with matched MOSI CRCs and `first_mismatch_byte=-1`, but `first_mismatch_miso=0` and Teensy MISO RX CRC **47937** (CRC of 512 zero bytes); ESP32 TX CRC was nonzero. Initial jumper mistakenly connected Teensy CN3-29 to ESP CN3-15; user corrected it to **ESP CN3-13 / GPIO17**, but the all-zero symptom persisted. Espressif's ESP-IDF 4.4 SPI slave docs note DMA MISO timing prefers **SPI modes 1/3**; legacy test used mode 0. Both feature branches now include a *new, untested* SPI mode 0/1 selector (matching master and slave) and MISO first-16-byte hex capture (`miso_first16_hex`). **User should pull/build/flash BOTH updated branches, then test full-duplex mode 1 at 1 MHz, one sector**. If still zeros, inspect GPIO17 drive/continuity physically with a logic analyzer, not faster SPI. Preserve previous one-way diagnostic and CP/M UART disk path. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md).

> **MISO isolation follow-up, 2026-10-18:** Even after correcting Teensy CN3-29 ↔ ESP CN3-13, **1 MHz SPI mode 1 full duplex fails** with Teensy `miso_first16_hex=00000000000000000000000000000000`, MISO receive CRC **47937** (512 zero bytes), expected ESP TX CRC **13577**, while MOSI CRCs match (**60981**); first MISO mismatch 0. A new **GPIO-only MISO line test**, not yet built/flashed, is committed to BOTH feature branches: POST `/api/spitest/gpio` or press **Check MISO wire (GPIO LOW/HIGH)** on `/spitest`. ESP32 releases SPI2, drives GPIO17 low and high; Teensy temporarily releases SPI0 and samples pin12 digitally through UART. Inspect `gpio_low_read` and `gpio_high_read`: expected 0/1. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md) for build/use/interpretation. Disk path unaffected.

> **IMPORTANT MISO PIN MAPPING CORRECTION 2026-10-18:** The original wiring diagram/table erroneously identified ESP GPIO17 as CN3-13. **That pin is E04/GPIO4** on the published schematic. Module GPIO17 is WROOM module pad 28. The published drawing suggests CN3-15 (E17), but the user reports actual board module pin 28 goes to CN3-17. **STOP prescribing a CN3 pin until physical, power-off continuity is measured from WROOM pad 28 to CN3-15/17.** GPIO17 is still the correct firmware selection; no software reassignment until actual hardware validated. Also update derived SVG after final mapping. See [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md).

## Starting a new chat

Use a message such as:

> Continue the EBTKS/HP-85 parallel Z80 CP/M project at https://github.com/timob0/EBTKS-Z80-Emulator (main). First read PROJECT_STATUS.md and NEXT_CHAT_HANDOFF.md, then examine the current source before editing. Baseline application commit is 5f586f35197627319e7b8280ef62e3807b6ceee3, tested on physical hardware. We want to integrate the 4-inch SPI TFT next. Preserve the Teensy protocol UART, CP/M warm boot, native/LIF mixed media and browser-only VT100 backpressure.

**No reliance on chat memory is required:** these notes are version-controlled and readable by a GitHub-connected chat. The next assistant must still inspect current code and verify the checkout/head before changing it. Do not assume documentation commit SHAs are firmware-tested commits.

## Project goal and architecture

An original HP-85 / EBTKS runs its normal HP environment while its already-present ESP32 runs an **entire second machine**: a virtual Z80 with CP/M 2.2, SD-backed drives, browser console and Telnet console. The Teensy is responsible for the SD card, HP Series-80 bus timing and existing EBTKS functionality. The ESP32 is not allowed to talk directly to the HP bus. The HP computer remains active while Z80 software runs.

Three distinct communication domains must never be conflated:

- **Physical ESP32↔Teensy UART1 protocol**: GPIO3 RX / GPIO1 TX, 115200 8N1; `TeensySerial`, `ebtks_link.h`; serialized requests/replies and CRC. **Never log or send console bytes here.**
- **Virtual Z80 UART / CP/M console**: Z80 I/O port `0x80` and line status `0x85`; CPU writes to the emulated transmit ring; console router forwards its output to Telnet and browser; browser/Telnet keypresses feed RX ring.
- **Diagnostics**: `EbtksDebugSerial` memory sink surfaced by `/z80debug`. No dedicated hardware diagnostic UART in the current EBTKS wiring. Do not call `Serial.begin()` or enable WiFiManager UART diagnostics.

The existing main web service `/`, `/updater`, `/ws` and the Teensy storage client share ESP services and must keep working. `/crt` is an independent HP-85 32×16 text / 256×192 graphics snapshot browser feature, not the Z80 terminal.

Source orientation:

| File | Responsibility |
|---|---|
| `ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino` | Startup, task creation, Z80 boot/reboot loop and warm-boot integration |
| `pio_src/main.cpp` | PlatformIO wrapper including the Arduino sketch |
| `ESP32_Z80_Emulator/globals.h` | Z80 registers/RAM, virtual ports, TX/RX rings |
| `ESP32_Z80_Emulator/cpu.h` | Z80 instruction interpreter and I/O port operations |
| `ESP32_Z80_Emulator/serial.h` | Console ring draining, Telnet input parsing, WebSocket forwarding |
| `ESP32_Z80_Emulator/telnet.h` | Telnet accept task (port 23) |
| `ESP32_Z80_Emulator/web_terminal_v2.h` | `/z80` inline browser HTML/JS VT100/HP-86 parser, WebSocket handler and backpressure |
| `ESP32_Z80_Emulator/ebtks_web.h` | Existing EBTKS web service, updater, shared web tasks |
| `ESP32_Z80_Emulator/ebtks_link.h` | UART1 protocol-only physical connection to Teensy |
| `ESP32_Z80_Emulator/ebtks_storage.h` | Teensy SD file/disk services |
| `ESP32_Z80_Emulator/cpm_mount_store.h` | Authoritative `cpmmount` ESP32 NVS mount table (SD table migration only) |
| `ESP32_Z80_Emulator/cpm_geometry_dynamic.h` | 16-drive DPH/DPB discovery and mixed geometry patching |
| `ESP32_Z80_Emulator/cpm_warmboot.h` | Heap shadow and WBOOT/BDOS + optional HP key shims |
| `ESP32_Z80_Emulator/cpm_media_page.h` | Browser `/cpmmedia` page and endpoints |
| `ESP32_Z80_Emulator/z80_debug.h` | Memory debug sink |

## CP/M memory / boot invariants

The current **native** CP/M 2.2 layout (not original HP 82900 resident CP/M):

```text
0000: JP WBOOT (installed by native CBIOS)
0005: JP D806  (MUST remain unchanged: programs read 0006/0007!)
D000: native CCP
D806: native BDOS entry (body begins at D811)
E600: native CBIOS jump table
E603: native BIOS WBOOT jump
FFEB...: reserved-area shims installed at runtime by cpm_warmboot.h
```

Bootstrap loads Teensy SD files:

```text
/cpm/boot.txt
/cpm/cboot.rom at 0000
/cpm/cpm22.rom at D000
/cpm/cbios.rom at E600
```

With Teensy SD unavailable, the existing Z80 BASIC fallback is retained.

### Why warm boot was required / traps to avoid

Microsoft Multiplan and INSTALL worked while running but previously reset the *whole Z80 VM* after exit (HALT appeared around `D840`). Many large CP/M programs reuse CCP space, relying on WBOOT to reload resident code. The historical native CBIOS only jumped to the in-memory CCP.

The working solution snapshots `D000–E5FF` (0x1600 / **5632 bytes**) into **heap** memory at startup and hooks the internal Z80 breakpoint at `E603`; restore the snapshot at WBOOT, re-install the BDOS-entry shim, then let the original CBIOS reset state and return to CCP. Several `CP/M warm boot: restored ... (count=N)` lines are normal termination events, not CPU resets.

Two failed approaches explain important compatibility constraints:

1. A static 5632-byte global buffer overflowed linker `.dram0.bss` by 2960 bytes. Keep large new buffers off static DRAM, measure heap and task stacks, and do not presume PSRAM.
2. Redirecting the standard page-zero `JP` at `0005` to a high-address BDOS shim made MP/INSTALL hang or print garbage. CP/M programs read `0006/0007` as their upper TPA boundary. The working code preserves `0005: JP D806` and intercepts function 0 *behind* D806. Do not reintroduce the bad vector patch.

The same header currently also patches two native CBIOS `CONIN` tails to emulate HP keyboard special-byte return convention, but this **did not solve HP-86 Multiplan arrows**. No further work is requested on that compatibility mode.

## Virtual disks and tested software

The Teensy SD directory `/cpm/disks` contains real disk images; native `A.dsk` is 8 MiB R/W. HP 82900 CP/M LIF floppies are 630784-byte `.lif` images (e.g. `cpm_1.lif`, `cpm_2_MP.lif`, `cpm_3_f80.lif`) exposed read-only. Their logical CP/M address space begins at file offset **0** and the first 256 KiB are mapped, including HP-specific reserved tracks. Do not extract only the LIF `E010` payload.

HP 82900 DPB:

```text
SPT=32, BSH=3, BLM=7, EXM=0, DSM=243, DRM=127,
AL0=F0h, AL1=00h, CKS=32, OFF=3
```

Native EBTKS DPB uses SPT 128 with 512-byte host sectors. The host LBA mapping for HP floppy is `track*8 + hostSector`, rather than native `track*32 + hostSector`. Current geometry code scans runtime CBIOS state to locate the native DPB and all drive DPHs, restores native geometry, then patches each HP volume separately. Sample successful log: `base=E633 nativeDPB=E733`, with B-D each given dedicated HP DPB/CSV. Addresses are runtime-discovered, not an invariant to hardcode.

Drive letters A:P are selected through `/cpmmedia`. Active/desired mounts live in RAM, desired table persisted to ESP32 NVS namespace `cpmmount`, validated at activation; changing assignments requires explicit **Apply / Reset CP/M** while idle at the prompt. Mounts do not change underneath live CP/M. `/cpm/mounts.cfg` is an older *migration* format, not the authoritative current table. Real media stay on Teensy SD.

User validated `DIR`, `MBASIC`, `Zork I` reading data files, Microsoft Multiplan installation and runtime, and clean exit of MP/INSTALL after warm-boot fix. Existing working Multiplan copy is configured for **DEC VT-100** and browser VT100 is the preferred terminal. HP-86 Multiplan installation was also exercised, but navigation keys do not behave correctly in that mode.

## Browser-only terminal truncation — solved, must not regress

Symptom observed: with only `/z80` connected, `DIR` truncated and Multiplan drew only line numbers/incomplete command areas. Connecting Telnet caused both clients to display all output.

Cause: the old CPU `OUT(80h)` writer filled an unchecked `txBuf[1024]` ring, overwriting unread data; asynchronous `binaryAll()` was not backpressured and its queue could fill during rapid bursts; browser rendered the complete 80×24 grid for every tiny frame. Telnet synchronous `WiFiClient.write()` unintentionally paced output.

Changes in the tested baseline:

- `cpu.h`: single-producer UART ring waits for free slot rather than overwriting unread bytes.
- `serial.h`: forwards larger chunks (256 bytes) to both sinks, supports partial Telnet writes, applies ring backpressure also to `outString`.
- `web_terminal_v2.h`: waits until WebSocket clients are writable before queuing another frame, with a timeout for an unresponsive client; requestAnimationFrame coalesces browser rendering.
- Telnet remains optional and raw. Browser keyboard binary frames alone become Z80 input; text `TERMTRACE` frames are only diagnostics, not CP/M keystrokes.

**Physical test result:** after these changes, `DIR` was complete and Multiplan rendered properly with only a browser connected. This is the last tested feature and should be protected by regression testing.

## Original HP 82900A is a separate future project

Original HP hardware includes a 2 KiB CP/M ROM, resident CP/M `D800–FFFF` (CCP `D800`, BDOS `E000`, BIOS `EE00`), and HP-specific console/disk hardware interface with port meanings that conflict with this native emulator. The present system **mounts original HP floppy images** but **does not boot the original HP firmware**. Do not treat these as the same effort. See `HP82900_STAGE1.md` for historical context.

Other longer-term possibilities, not currently requested: HP BASIC `LOADBIN` front end `CPM85` with F-keys to attach the CP/M console, route output to HP-85 CRT/printer, and mount images through the existing service; genuine HP 82900A ROM emulation.

## Next milestone: 4-inch TFT

A Hosyond 4-inch SPI display with advertised **ST7796S**, **480×320 landscape** / native 320×480, **FT6336U I²C capacitive touch** was ordered for the next development milestone. Do not assume those components are confirmed until testing the actual PCB. Test ST7796S first; verify label/wiring and controller; some similar modules use ILI9488. Check orientation, backlight supply/current and whether onboard microSD shares the display SPI bus. No additional microSD is required for CP/M since Teensy SD remains authoritative.

An 80-column VT100 grid with 6×12 glyphs fills 480×288; remaining 32 pixels can be a small status/touch-key strip. But the known-good browser terminal emulator is **inline JavaScript** in `web_terminal_v2.h`, *not* an ESP32 C++ state engine. A hardware TFT implementation will need a native Z80-output parser/screen buffer or carefully designed shared state stream; do not promise reuse of browser JS on hardware. Need to decide whether to keep browser parsing independent initially or centralize native parsing. For first bring-up, display init + simple color/pixel/font test only.

**Before wiring:** examine the real EBTKS connector and schematic; GPIO1/GPIO3 are permanently occupied by the Teensy protocol; do not guess other free pins or 5V tolerance. Avoid large static DRAM allocations (the linker was already almost full before the heap shadow change). Confirm whether TFT DMA/full framebuffer is feasible or choose small character-state / row dirty-region updates.

Desired staged tests:

1. Confirm physical module/controller, voltage/current, PCB pinout, ESP32 accessible pins, shared SPI constraints, touch addresses.
2. Minimal display initialization/rotation/graphics test without disturbing CP/M/Teensy/OTA.
3. Implement readable 80×24 cells, cursor, VT100 attribute subset, and actual escaped output, preserving WebSocket and Telnet behavior.
4. Optionally integrate touch for key shortcuts / attach / status; revisit broader CP/M UI only when core TFT console is stable.

## Future change discipline and quick tests

- **Before code changes:** read both status files and current relevant source. Keep a known working commit/tag. All technical changes must be made deliberately; no speculative rewiring of UART, NVS or CP/M ABI.
- **ESP32 compile:** `pio run -t clean && pio run`. User OTA flashes `.pio/build/esp32dev/firmware.bin`. PlatformIO uses `pio_src/main.cpp` which includes `ESP32_Z80_Emulator.ino`.
- **Regression matrix:** verify EBTKS original Wi-Fi/main UI/updater/`/ws`; normal CP/M startup; disk mounts B/C/D; browser-only `DIR`; VT100 browser-only Multiplan screen/navigation; MP exit to `A>` with `CP/M warm boot: restored...`; Telnet connects without reset and can coexist; HP-85 still runs.
- **Diagnostics:** `/z80debug`. Runtime `Z80 halted @ ...` may mean user-requested reset, not a genuine HALT. A persistent `Booting CP/M...` after every MP exit is a regression.
- **No hardware tested after future code edits until user confirms:** keep a distinction between a source review/build passing and a real EBTKS test.

## Historical documentation caution

The root README is largely inherited from the original standalone Z80 emulator (TTGO ESP32 SD card, old UART/debug assumptions, etc.). Milestone docs `MILESTONE1_EBTKS.md`, `MILESTONE2_EBTKS.md`, `HP82900_STAGE1.md`, and `CPM_MEDIA_MANAGER.md` explain the evolution but are not guaranteed to describe the latest implementation. **For the present tested design, prioritize current source, this handoff and PROJECT_STATUS.md.**
