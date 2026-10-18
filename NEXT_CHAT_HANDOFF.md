# Handoff / durable project memory for the next development chat

## CURRENT HANDOFF — authoritative decisions (2026-10-18)

**Start here; dated material is archived at the end.** This is the hardware-tested **EBTKS v3 / HP-85 + ESP32 Z80 CP/M 2.2** integration. The HP-85 continues normal BASIC/SIMON85 operation while the Teensy 4.1 services its bus and SD card and the ESP32 independently runs CP/M. The user considers current read performance *finished* and asks for new functionality, not more SPI/cache tuning.

**Established baseline:**

- Working 4 MHz SPI mode 0, **Teensy SPI master → ESP32 SPI2 slave**, 528-byte frames (16-byte header + 512-byte payload), session/sequence/header/payload CRCs, UART control plane. The **interrupt-free `poll_low_v2` READY handshake** deliberately holds READY LOW for roughly 5 ms between frames. Never regress this to `attachInterrupt()`: the EBTKS HP bus ISR owns shared **`IRQ_GPIO6789`**, and an earlier Arduino READY interrupt locked up the HP-85.
- Opt-in live **CP/M disk READS** over SPI use aligned **16-sector / 8 KiB read-ahead**, allocated lazily from ESP32 heap to avoid `.dram0.bss` overflow, verified before publishing the cache, with UART/Base64 fallback on errors. **All live CP/M disk WRITES remain on UART/Base64**, and writes/reset/mode switches invalidate the read cache. SPI read mode is **OFF by default after ESP32 reboot**; enabled from `/cpmmedia`. Keep firmware and user disk-image backups.
- Real hardware results: **Multiplan install 36 → 23 s**, **Multiplan startup 10 → 3.7 s**, **Zork startup 8 → 3 s** (UART → SPI). CP/M write/copy/readback passed. During a later cumulative sample, **1,026 cache hits / 234 misses = 81.43%**, 234 successful 16-sector batches / 3,744 frames, **0 SPI failures / 0 UART fallbacks**, free heap **79,376 B**. Standalone 128-sector SPI streaming passed repeatedly with SIMON85 graphics and HP `SDCAT` concurrently. `DIR` is fast enough that the browser renderer can now be slower than a Telnet client.
- Keep **Z80 CP/M 2.2 warm boot, Web terminal VT100 backpressure, native 8 MiB and read-only HP 82900 LIF media mounts, NVS drive assignments, Wi-Fi, /updater, /ws and normal HP BASIC services** untouched.
- Working code was flashed and tested by the user before the later docs-only commits. Current working repository branches are **`feature/cn3-spi-read-test` on BOTH ESP32 and Teensy**, not assumed merged to `main`. Tested code pointers and build details follow below.

## Seven user-approved milestones — preserve this order

| Phase | Objective | Acceptance / dependency |
|---|---|---|
| **1 NOW** | **Enhance Media Manager: safe disk-image upload, download and backup**, using **new reverse SPI data transfer ESP32 → Teensy** where useful | Isolated upload transfer must **not** change the working live CP/M `disk_write`; only publish fully verified unmounted images |
| **2** | **Physical 4-inch TFT as native CP/M terminal** | Hardware **not arrived yet**; inspect actual TFT/connector/pins before design or wiring; preserve Web terminal and Telnet |
| **3** | **HP-85 keyboard ↔ CP/M/TFT** | Attach/detach and input-focus ownership so no browser/Telnet is required; HP BASIC can regain keyboard control |
| **4** | **HP printer/plotter peripheral integration** | CP/M standard text printer is BIOS **`LIST` / `LST:`** (a `PRN:` mapping can be a convenience alias); map via buffered Teensy/HP-IB service. Treat HP-GL/plotter graphics as a separate channel |
| **5** | **HP CRT terminal & BASIC `CPM` control command** | A constrained 32-column HP-85 CP/M terminal, plus F-key or programmatic management of keyboard, peripheral and disk mappings |
| **6** | **GSX-80 in CP/M** | Native GSX `GDOS/GIOS` + **loadable GIOS TFT driver**, not ROM first; test stock GSX calls / application compatibility; GEM-like experiments are later |
| **7** | **SIMON85 → GSX-like TFT graphics output** | Optional rerouting of HP BASIC graphics commands through a shared renderer/command interface; keep normal HP-85 CRT drawing as default |

**Separate later historical interest:** original **HP 82900A CP/M firmware/ROM** compatibility (not the current native CP/M 2.2 emulator). It may be explored after TFT integration but is **not an immediate dependency**. Do not confuse mounting original HP LIF images with booting original HP 82900A CP/M firmware.

## IMMEDIATE NEXT WORK: Media Manager image transfer (milestone 1)

1. **Preserve the tested firmware** and disk images; work on feature branches. Review `cpm_media_page.h`, `cpm_mount_store.h`, `cpm_media.h`, `spi_disk_read.h`, `spi_link_test.h`, `ebtks_link.h`, and Teensy `include/EBTKS_ESP.h`, `include/EBTKS_SPI_Stream.h`. These names and roles are further described below.
2. Prove reverse **binary transport in RAM first**, from **ESP32 SPI-slave MISO → Teensy SPI-master MISO receive**, with the Teensy *still generating clock/CS*; do not assume a successful Teensy-to-ESP32 read means writes work. Prove session/frame index/length/CRC and READY synchronization with patterns **without SD writes, mounted media or HP bus changes**. Use a new opt-in transfer test, no changes to live CP/M read/write call paths. Any uncertain DMA ownership requires an ESP32 reboot, never blind reuse.
3. Add a **streamed browser upload endpoint**, using bounded ESP32 buffers and backpressure (no multi-megabyte image buffer); write to a **new, unmounted temporary/staging file** on Teensy SD. Support progress, cancel and interrupted-upload recovery. Block replacement of *all active and desired* mounted files.
4. Validate exact byte length and whole-file digest (prefer a cryptographic hash if feasible, or a documented strong integrity check) **including a readback of the staged file**; confirm format/geometry for supported CP/M image types. Only then publish it under an explicitly chosen **new** `/cpm/disks` filename and permit user-initiated mount/Apply Reset. FAT rename must **not** be represented as universally atomic across power loss. Never overwrite the only copy of an image.
5. Implement disk-image **download/backup** via the already tested Teensy MOSI → ESP32 SPI stream (with HTTP streaming/backpressure) and verify a downloaded image against the original. Test cancellation, short/failed transfer, insufficient space, errors, and operation while HP graphics is active.
6. Keep ordinary CP/M sector writes on UART for now. Further acceleration matters mainly for bulk image transfers; only revisit live disk writes when measured needs and durability/error propagation justify the risk.

**Safe milestone-1 acceptance:** new 8 MiB image can be uploaded and downloaded **byte-identically**, verified and then mounted without disturbing HP BASIC, existing CP/M media or ROMs; deliberately interrupted transfer leaves original images and mount tables intact.

## TFT, HP integration and GSX — architecture to retain

- **TFT:** user will signal panel arrival. Advertised ST7796S / 480×320 landscape, possible FT6336U touch, **all unverified** until PCB inspection. The ESP32 is **SPI slave on existing CN3 storage link**, whereas TFT expects master-type display access; verify free CN3 pins, peripheral ownership, voltage, reset/backlight and TFT controller before wiring. A native **80×24 VT100** text model is the target. ESP32 Web terminal VT100 emulation currently lives in **JavaScript**; a C++ parser/cell buffer or shared state must be designed. Avoid a full 16-bit framebuffer (~300 KiB).
- **HP/CP/M bridge:** design a keyboard-focus state machine and a nonblocking, mailbox-style service boundary. The original HP-85 continues executing; no HP-bus ISR waits on SPI/SD/network. Standard CP/M **`LST:`** print streams can be directed through an existing Teensy printer or HP-IB service with buffer/backpressure. A plotter/HP-GL or GSX plotter path is a different command stream and needs explicit mapping. The HP-85 CRT is **32 columns** and offers only limited terminal capability; eventual `CPM` option-ROM/BASIC integration will need screen, keyboard, printer and disk-mapping controls.
- **GSX/graphics:** Digital Research **GSX-80** separates GDOS (device-independent) and GIOS (device-specific) with `GSX.SYS`, `GENGRAF.COM`, `ASSIGN.SYS`, and loadable `DD*.PRL` drivers. The standard CP/M BDOS graphics function is **115**. Build and test a loadable TFT GIOS implementation before considering a resident/ROM GSX. Turbo Pascal access may be possible via an explicit wrapper; **do not assume built-in Turbo Pascal GSX support**. GSX influenced GEM/VDI, but a minimal GEM-like desktop requires additional event/window/application layers.
- **SIMON85/GSX cooperation:** define **shared firmware drawing primitives** (line/polyline, fill, text, scaling, clipping, attributes) accepted by a Z80 GIOS driver and by an opt-in SIMON85 backend. This permits HP BASIC drawing to TFT without loading a Z80 `.PRL` into an HP-85 option ROM or requiring a GSX app to own the display. A Z80-hosted GSX relay could later demonstrate both machines actively cooperating; preserve native SIMON85 HP-85 CRT drawing.

## Essential supporting documents and links

- **Agreed, detailed seven-phase plan and acceptance gates:** [`HP_CPM_INTEGRATION_ROADMAP.md`](HP_CPM_INTEGRATION_ROADMAP.md).
- **Confirmed runtime benchmarks, cache statistics, write/readback tests, build and mode controls:** [`SPI_CPM_READ_INTEGRATION.md`](SPI_CPM_READ_INTEGRATION.md).
- **Proven framed SPI layout, READY state machine, failure investigations and why READY interrupts are unsafe:** [`SPI_STREAM_PROTOCOL.md`](SPI_STREAM_PROTOCOL.md).
- **Current tested services / route inventory and historical notes:** [`PROJECT_STATUS.md`](PROJECT_STATUS.md). Some older dated entries describe *superseded* code; use the latest summary and actual branch source.
- **Physical SPI mapping and original proposal:** [`CN3_SPI_PROPOSAL.md`](CN3_SPI_PROPOSAL.md) (historic proposal; verify against the **tested wiring** in stream docs and actual hardware before assuming pins).
- **Original HP 82900A research:** [`HP82900_STAGE1.md`](HP82900_STAGE1.md) (distinct historical project).
- **GSX historical overview/source files:** [John Elliott's GSX-80 archive](https://www.seasip.info/Cpm/gsx.html); [Digital Research GSX-80 Programmer's Guide (Nov. 1982, Bitsavers)](https://bitsavers.org/pdf/digitalResearch/gsx/GSX-80_Graphics_Extension_Programmers_Guide_Nov82.pdf). For the CP/M Turbo Pascal question, [discussion of PolyPascal/GSX bindings](https://retrocomputing.stackexchange.com/questions/31326/how-can-i-use-gsx-in-turbo-pascal-for-cpm-80). Treat any third-party code/license separately from our own firmware.
- **Established ESP32 update workflow:** on `feature/cn3-spi-read-test`, `pio run -e esp32dev -t clean && pio run -e esp32dev`, flash `.pio/build/esp32dev/firmware.bin` via `/updater`. Teensy builds with `pio run -e teensy41`; never flash Teensy merely for an ESP32-only UI change.

**Unbreakable constraints:** preserve tested HP bus ISR; do not attach an Arduino READY pin interrupt; no edits to `main` unless user authorizes a merge; no direct ESP32 access to HP-85 bus or SD card; keep UART1 protocol-only (no logs/console bytes), serialized; normal CP/M disk writes and current read cache must keep working; no unbounded buffers or large `.bss` allocations; distinguish source-reviewed/build-tested from *physical hardware-tested* at every step.

---

## Starting a new chat

Suggested message:

> Continue the HP-85 EBTKS / parallel Z80 CP/M project. Open the **`feature/cn3-spi-read-test`** branch of `timob0/EBTKS-Z80-Emulator` and read **`NEXT_CHAT_HANDOFF.md`**, **`HP_CPM_INTEGRATION_ROADMAP.md`**, **`PROJECT_STATUS.md`** and **`SPI_CPM_READ_INTEGRATION.md`**. We have hardware-validated the IRQ-free 4 MHz SPI CP/M read transport. **Next task is Media Manager disk-image upload/download with a separately tested ESP32-to-Teensy SPI payload path**. Leave live CP/M disk writes on UART and don't alter the proven READY handshake. The TFT hasn't arrived yet; its native-terminal integration is milestone 2. Read the source and confirm branch heads before editing.

**Repository pointers:** ESP32 [`timob0/EBTKS-Z80-Emulator`](https://github.com/timob0/EBTKS-Z80-Emulator/tree/feature/cn3-spi-read-test) and Teensy [`timob0/ebtks-alt-firmware`](https://github.com/timob0/ebtks-alt-firmware/tree/feature/cn3-spi-read-test), **both on `feature/cn3-spi-read-test`**. Do not silently use `main`. Historic firmware-state pointers: ESP32 CP/M SPI read-path/cache-allocation fix [`5a1ac6e5`](https://github.com/timob0/EBTKS-Z80-Emulator/commit/5a1ac6e5d6ada475e3f4489ff44eabc61e910b8e), Teensy tested IRQ-free sender [`beba160e`](https://github.com/timob0/ebtks-alt-firmware/commit/beba160e51d403700f069b9ebf3de956ed4bd6f5). Later documentation commits do **not** imply a new hardware-tested binary. Inspect exact current source/HEAD before modifying anything.

**Do not rely on chat memory:** this repository handoff and linked design records are intended to restore context independently.

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

**Physical test result (earlier milestone):** after these changes, `DIR` was complete and Multiplan rendered properly with only a browser connected. Later **live SPI CP/M read-ahead hardware tests also passed**. Preserve browser backpressure in regression tests, although the browser now renders large `DIR` listings more slowly than Telnet.

## Original HP 82900A is a separate future project

Original HP hardware includes a 2 KiB CP/M ROM, resident CP/M `D800–FFFF` (CCP `D800`, BDOS `E000`, BIOS `EE00`), and HP-specific console/disk hardware interface with port meanings that conflict with this native emulator. The present system **mounts original HP floppy images** but **does not boot the original HP firmware**. Do not treat these as the same effort. See `HP82900_STAGE1.md` for historical context.

**Now explicitly on the approved roadmap:** HP BASIC `CPM`/`CPM85` command to attach keyboard/HP CRT to CP/M and manage devices/disks (milestones 3–5). Genuine HP 82900A firmware emulation is still a distinct, lower-priority historical project, not required to complete the seven milestones.

## Milestone 2: 4-inch TFT (after Media Manager transfers)

A Hosyond 4-inch SPI display with advertised **ST7796S**, **480×320 landscape** / native 320×480, **FT6336U I²C capacitive touch** has been ordered; **as of 2026-10-18 it has not arrived and the user will advise when it does**. TFT integration is milestone **2**, after isolated Media Manager upload/download work. Do not assume those components are confirmed until testing the actual PCB. Test ST7796S first; verify label/wiring and controller; some similar modules use ILI9488. Check orientation, backlight supply/current and whether onboard microSD shares the display SPI bus. No additional microSD is required for CP/M since Teensy SD remains authoritative.

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

## Archived dated engineering progress notes (historical context; superseded by current handoff)

> **USER-PRIORITIZED POST-SPI ROADMAP (2026-10-18):** Read performance is considered **good enough** (Multiplan 3.7s, Zork 3s; browser Web terminal now comparatively slower than Telnet during very fast DIR). **DO NOT optimize cache, SPI clock or handshake now.** TFT panel has **not arrived**; user will notify when it does. Work order: **(1)** Enhance Media Manager with safe browser disk-image upload/download and develop **reverse ESP32→Teensy SPI data** for staging only, **without changing live CP/M UART disk writes**; **(2)** native 4-inch TFT CP/M terminal; **(3)** HP keyboard to operate CP/M/TFT without browser/Telnet; **(4)** bridge CP/M LIST/LST: text to HP printer and HP-IB peripherals, plus separate plotter/HP-GL; **(5)** HP-85 CRT as restricted CP/M terminal + BASIC `CPM` command/mapping UI; **(6)** authentic CP/M-80 GSX (loadable GDOS/GIOS `DD*.PRL` driver first; ROM is optional later); **(7)** optional SIMON85→TFT graphics via common GSX-compatible primitives. Authentic HP 82900A firmware boot is a **separate, lower priority historical goal**, after TFT. GSX classic GDOS via BDOS 115, ASSIGN.SYS driver assignment, GIOS drivers; later GEM-like work is exploratory. **Critical TFT issue:** ESP32 is SPI2 **slave** to Teensy on existing CN3 link; TFT requires master-compatible routing and verified spare GPIO, not pin guesses. No firmware edits made in this roadmap update. Detailed design/acceptance gates: [HP_CPM_INTEGRATION_ROADMAP.md](HP_CPM_INTEGRATION_ROADMAP.md).



> **LIVE CP/M SPI READ-AHEAD API SNAPSHOT — 2026-10-18, after successful Multiplan/Zork tests:** `GET /api/cpm/spireads` returned `enabled=true`, `boot_disabled=false`, `needs_reboot=false`, `active=false`, 8,192-byte heap-allocated cache (`cache_allocated=true`), valid cache path context with `cache_first=1152`, `cache_count=16`; `cache_hits=1026`, `cache_misses=234`, **1,260 logical 512-byte read requests / 81.43% cache-hit rate**; `batch_reads=234`, `frames=3744` (exactly 16 per batch = **1,872 KiB = 1.828 MiB** physical SPI payload). `uart_fallbacks=0`, `spi_failures=0`, `last_error=""`, `cache_invalidations=261`, 27 more than the 234 invalidations attributable to cache misses (likely writes, resets or mode changes, but per-cause counts not instrumented). Last batch **173.980 ms**; total batch time **39.746897 seconds** across 234 batches, average **169.859 ms per 16-sector batch**, physical-batch throughput **47.10 KiB/s** measured cumulatively. `free_heap=79376` bytes (~77.52 KiB); snapshot only, not full memory-leak/soak proof. Observed **5.38 logical read requests per physical batch**, so 16-sector fixed-window prefetch should eventually be profiled against smaller/adaptive windows; because read requests may repeat sectors, the number of distinct sectors actually consumed is unknown. Historical 128-sector standalone diagnostic reached 72.61 KiB/s and measured different batching sizes. Preserve working firmware and keep UART writes/cache invalidation + IRQ-free `poll_low_v2` unchanged during initial application testing. **Firmware unchanged** by this analysis. [Integration guide](SPI_CPM_READ_INTEGRATION.md).



> **REAL CP/M MULTIPLAN/ZORK SPI READ-AHEAD HARDWARE PASS — 2026-10-18:** User successfully compiled/flashed ESP32 CP/M SPI integration after the `.dram0.bss` fix (8 KiB cache moved from static storage to lazy heap allocation). Live CP/M disk reads using the interrupt-free Teensy `poll_low_v2` / 4 MHz MODE0 framed SPI plus 16-sector read-ahead were tested on physical hardware, while writes remained UART. **Measured UART vs SPI:** Multiplan install **36 s → 23 s** (1.57× speedup; 36.1% elapsed-time reduction), Multiplan startup **10 s → 3.7 s** (2.70×; 63.0% reduction), Zork startup **8 s → 3 s** (2.67×; 62.5% reduction). Installer phase observations: reading product files ~**20 s → 10 s** (~2× faster), while linking selected products stayed ~**13 s**; the installer total improved 13 s, and the phase estimates need not sum exactly. User also copied a text file using CP/M and successfully reread it after the write, encouraging evidence that UART writes and SPI-cache invalidation/read coherence are functioning in that scenario. **Do not extrapolate all app speedups from standalone 8.75× stream throughput**; startup depends on access pattern/cache hits and UART writes remain unchanged. Hardware evidence is positive; **full `GET /api/cpm/spireads` snapshot, hit/miss/fallback counts, prolonged soak and write durability testing not yet supplied**. Preserve existing proven `poll_low_v2` firmware and UART fallback; avoid premature changes to READY or HP-85 bus interrupt setup. No source code changed during this report. [Integration details](SPI_CPM_READ_INTEGRATION.md).



> **ESP32 LINK-TIME DRAM FIX (2026-10-18, not yet rebuilt/tested):** First attempted ESP32 CP/M SPI integration build failed at the **linker**, not compilation: `.dram0.bss` overflowed `dram0_0_seg` by **6,728 bytes**. The new global `static uint8_t spiDiskCache[8192]` caused this. Patched `ESP32_Z80_Emulator/spi_disk_read.h` to store only a pointer in static DRAM and **allocate 8,192 bytes at runtime on first explicit SPI-enable request** via `heap_caps_malloc(..., MALLOC_CAP_8BIT)`, with a heap headroom and contiguous-block check. The buffer is deliberately retained for the ESP32 boot, even when switching back to UART, eliminating use-after-free and heap fragmentation from repeated toggles; SPI cache is never marked valid until a complete CRC-checked batch arrives. If allocation is impossible, POST `/api/cpm/spireads?enabled=1` now returns HTTP 503 and the interface remains in **UART mode**. GET status adds `cache_allocated` and `cache_bytes`. **Expected result:** removing the 8 KiB static array should recover enough linker DRAM to clear the 6,728-byte overflow, but a fresh local `pio run -e esp32dev -t clean && pio run -e esp32dev` is required to verify. The proven Teensy `poll_low_v2` firmware, original UART/Base64 CP/M read/write code, SPI READY polling and both `main` branches are unchanged. [Trial guide](SPI_CPM_READ_INTEGRATION.md).



> **OPT-IN LIVE CP/M SPI DISK READS + MULTIPLAN BENCHMARK (2026-10-18; CODE COMMITTED, NOT YET COMPILED OR VALIDATED ON HARDWARE):** User is benchmarking Multiplan installer and executable with the existing UART/Base64 transport. New **ESP32-only** feature-branch implementation hooks the authoritative `cpmMountedDiskRead()` after existing drive/profile/LBA mapping. **Default each ESP32 boot is UART**; `/cpmmedia` adds **Enable SPI read-ahead (4 MHz)** / **Use UART (baseline)** buttons plus `GET/POST /api/cpm/spireads` metrics/switch. A persistent core-0 `SpiDisk` worker reuses the real-hardware-validated **4 MHz MODE0 / `poll_low_v2`** Teensy-master→ESP32-slave generic CRC-framed protocol (Teensy firmware unchanged). It prefetches aligned **16×512-byte read-only sectors** into an 8 KiB cache on a miss, or one sector near logical medium end; verifies each frame's magic/type/session/index/length/headerCRC/payloadCRC plus final Teensy UART aggregate CRC, and serves cache hits directly from RAM. Cache is committed only on full validated batch and keyed by **mounted image path** + logical file-sector range; fallback to existing UART read on any SPI failure, SPI disabled for rest of boot until ESP32 reboot, uncertain DMA triggers `spiTestNeedsReboot`. **All CP/M disk writes still UART/Base64**, invalidate cache before every write; cache also invalidates on CP/M Z80 bootstrap/reset and mode switches. Diagnostics at `/spitest` are blocked while live SPI enabled to avoid SPI2 DMA conflict. ESP32 heap headroom check before creating 8 KiB task, read-mode off by default, no `main` merge. Multiplan startup speedup **not guaranteed**—cache locality and UART writes matter. **Finish UART benchmark and image backup before flashing new ESP32 build; Teensy needs no rebuild.** Then test UART baseline on new firmware, toggle SPI at CP/M prompt, test `DIR`, disposable copy/writes, then Multiplan startup and installer. Capture API stats and fallback counts. [Detailed procedure](SPI_CPM_READ_INTEGRATION.md).



> **VALIDATED FRAMED SPI TRANSPORT BASELINE — 2026-10-18, REAL HARDWARE:** User rebuilt/flashed the **interrupt-free `poll_low_v2` READY handshake** (Teensy feature HEAD `beba160e` / ESP32 feature HEAD `16d0af46` at time of test) and reports stable behavior across approximately **60 repeated runs**; run counter **63**, with approximately **20 × 128-sector** batches, plus successful operation during SIMON85 graphics and an overlapping HP-85 `SDCAT` SD-directory read. The previously observed HP-85 bus hang is **not observed** with this revision; no worker launch or UART prepare errors. Latest **128 × 512 B = 65,536 B payload** from `/cpm/disks/A.dsk`, starting sector 0, **4 MHz SPI mode 0**, read-only, passed: `stream_frames_received=128`, `stream_bad_frames=0`, `needs_reboot=false`, `stream_wire_bytes=67584` (16 B header + 512 B data/frame), `stream_ready_low_phases=127`, `stream_ready_wait_failure=0`, `stream_crc_esp=stream_crc_teensy=42267`, `sd_crc_match=true`, `first_mismatch_byte=-1`, `failed_sector=-1`. Memory: `heap_before_stream=94208`, `heap_after_stream=98052`, `heap_free_now=94216`, `heap_min_free=73652`, `heap_largest_free_block=40948`, `stream_worker_stack_high_water=6008`, `stream_prepare_parse_failures=0`, `stream_worker_launch_failures=0`. Timing: `sd_read_us=44373`, `teensy_transfer_us=165398`, `stream_ready_wait_us=560916` (max one READY wait 9473 µs), `wall_us=881413`; net **72.6107 KiB/s** end-to-end, ~**8.75×** the earlier 128-sector per-sector UART handshake test (~8.30 KiB/s, 7.710 s) without increasing clock. Breakdown of wall time: SD 5.03%, SPI loop 18.77%, READY waiting 63.64%, remaining coordination 12.56% (time categories from separate counters, not a fully instrumented trace). `stream_dma_error=""`/code 0. Other generic DMA diagnostic fields (`stream_dma_actual_bits=0`, `stream_dma_descriptor_matches=false`) are **failure-only snapshots**, not errors when `state=passed`. **Engineering decision:** preserve this as the tested baseline. Do not reintroduce Arduino `attachInterrupt` on Teensy READY—EBTKS custom `IRQ_GPIO6789` owns HP bus servicing. Optional future goals: optimize READY hold only after another controlled baseline; then add opt-in **read-only** CP/M disk-over-SPI with CRC+UART fallback, and later a separate CRT/screen data producer using the reusable frame type. Live CP/M still uses UART/JSON/Base64; no writes or `main` merge.



> **CRITICAL SAFETY UPDATE — HP-85 CRASH ON 1-SECTOR BATCH (2026-10-18; newly patched firmware NOT yet compiled/tested):** User flashed previous dual firmware update with Teensy READY `attachInterrupt()`; pressing `Batched framed SD stream` for even **1 sector** immediately locked HP-85. ESP32 returned `ESP_ERR_TIMEOUT` code 263 after 2,542,203 µs, `stream_frames_received=0`, `READY=1`, healthy heap, `needs_reboot=true`. **ROOT CAUSE IN SOURCE:** EBTKS `setupPinChange()` in `src/EBTKS_Bus_Interface_ISR.cpp` owns custom `pinChange_isr` on shared `IRQ_GPIO6789` to service every HP bus `PHI1` cycle. My normal Arduino `attachInterrupt()` for Teensy READY GPIO6 on that interrupt group was unsafe and can interfere with/corrupt HP bus IRQ handling. **STOP using the READY-interrupt version.** New patch removes all READY interrupt registration, uses ordinary noninterrupt `digitalReadFast()` GPIO polling and an explicitly held READY-LOW phase (5 FreeRTOS ticks) on ESP32 between DMA descriptors. Teensy now waits for READY LOW after each nonfinal CS release, then current READY HIGH before next frame. A new firmware compatibility marker `ready_protocol=poll_low_v2` in UART PREPARE must be returned by Teensy, and ESP32 refuses to queue DMA or send if absent—protecting against mixed safe/unsafe versions. 4 MHz MODE0, read-only and CRC checks unchanged; working CP/M UART/Base64 unaffected. No `main` merge. **Need to rebuild/flash BOTH Teensy and ESP32**. Power-cycle HP-85/EBTKS after lockup, retain known-good firmware, flash ESP32 first, then Teensy, verify BASIC, and only if comfortable test one supervised frame, stop immediately if bus disruption. The new polling scheme remains a conservative diagnostic, **not a proven production handshake**. [Full safety report](SPI_STREAM_PROTOCOL.md).



> **BATCHED SPI FRAME 61 / 128 FAILURE + NEW READY-EDGE FIX (2026-10-18, NOT YET HARDWARE TESTED):** User ran 60 batches after persistent-worker memory repair. 1- and 16-sector tests generally passed; the second 128-sector test failed at **frame 61** (`stream_frames_received=61`, 0 bad frames, `wall_us=176040`), `needs_reboot=true`, healthy ESP32 heap ~94 KB, largest block 36,852 B, worker stack high-water 6,004 B, 0 worker-launch or PREPARE-parse failures. The 176ms wall time is **much shorter than the 2.5-second DMA wait**, so the old generic “DMA incomplete” may represent a short transaction or early driver result. One plausible cause is **stale READY HIGH**: Teensy previously checked only the GPIO level before each frame, with no guarantee it had seen the prior DMA descriptor complete. **Both feature branches have now been updated**: Teensy uses a nonblocking FALLING-edge IRQ latch on READY GPIO6, requiring a new falling edge AND current high before each post-first frame, retaining HP interrupts; ESP32 exposes detailed DMA error, bit count (expected 4224), descriptor match, READY state, and timing, while preserving 4 MHz mode 0, per-frame CRCs, persistent worker, SD read-only and CP/M UART transport. This fixes a credible synchronization race but **is not yet verified on hardware**. Rebuild/flash BOTH branches, test 1→16→multiple 128-sector batches, stop/reboot on `needs_reboot=true`; collect full JSON and `/z80debug` if a failure recurs. [Engineering analysis and test guide](SPI_STREAM_PROTOCOL.md). No `main` merge or CP/M disk writes.



> **URGENT BATCHED SPI STABILITY REPAIR (2026-10-18, NOT YET HARDWARE VERIFIED):** After repeated 4 MHz read-only framed stream tests, user reported intermittent `Stream prepare UART JSON malformed` (run 19), then `Could not launch SPI stream worker` (run 20), followed by unresponsive ESP32 web pages. Both failed before SPI transfers (`stream_frames_received=0`). Code inspection discovered `spiTestStreamWorker` called `vTaskDelete(nullptr)` while function-scope `String` and ArduinoJson `JsonDocument` locals were alive; FreeRTOS does **not unwind C++ destructors** when deleting a task, causing potential heap leaks on every run. **ESP32 feature branch has been patched**: persistent reusable `SpiBatch` worker (8 KiB stack allocated once) invokes `spiTestStreamRun()` that returns normally, safe PREPARE-only UART retry, strict web heap headroom checks, heap/largest-block/stack high-water and malformed-reply diagnostics on `/api/spitest`. Suspected source of web-service exhaustion, pending user compile and repeated hardware confirmation. **Teensy source unchanged in this repair**, both `main` baselines unchanged, normal CP/M UART/Base64 service unaffected. DO NOT run the old batch build again. If old web UI is unresponsive, reset ESP32 before using `/updater`. [Detailed repair and 20–30 repeated 1-sector memory regression](SPI_STREAM_PROTOCOL.md).



> **NEXT EXPERIMENT — GENERIC BATCHED BINARY SPI STREAM (2026-10-18; SOURCE READY, NOT YET LOCALLY BUILT/HARDWARE-TESTED):** Implemented on **both** `feature/cn3-spi-read-test` branches. User proposed a *general HP/Teensy → ESP32 bulk transport* useful beyond CP/M SD sectors, e.g. future HP-85 graphics/alpha screen dumps. New independent versioned 528-byte CRC-protected frame (16-byte header identifying producer kind, session, block, 512-byte payload length, payload CRC, flags and header CRC) lives in `EBTKS_SPI_Stream_Frame.h` (Teensy) and `spi_stream_frame.h` (ESP32). Kind 1 currently produces **read-only SD-image sectors**; kind 2 reserved for a future CRT producer **not yet implemented**. ESP32 takes the serialized UART mutex, sends **one stream-start UART message per 1/16/128-sector batch** after a separate preflight UART request, then sequentially reaps/queues **528-byte SPI2 RX DMA frames**, while Teensy reads SD file sectors and clocks each frame on SPI0 at fixed **mode 0 / 4 MHz** with HP interrupts enabled. Frame header CRC, payload CRC, session, index, kind and final aggregate CRC checked; SD read errors sent as flagged frames; uncertain DMA ownership requires reboot. New `/spitest` button **Batched framed SD stream (4 MHz, read-only)**, endpoint `POST /api/spitest/stream`; existing synthetic test, original two-UART-round-trip/sector SD diagnostic, CP/M UART/Base64 disk traffic, HP-85, Wi-Fi/OTA and `main` are unchanged. Next: **pull/build/flash both feature branches; test 1, then 16, then 128 real sectors while HP-85 idle, then 128 sectors during SIMON85 graphics; compare wall times to original ~7.710 s / 128 sectors.** [Frame specification, safety notes, build instructions](SPI_STREAM_PROTOCOL.md). We cannot compile/test in this environment; no claimed hardware validation yet.



> **REAL SD→SPI UNDER SIMON85 GRAPHICS LOAD — PASSED (2026-10-18):** The user ran the HP-85's graphics-intensive BASIC/SIMON85 drawing and ellipse-filling program while independently testing **real SD image reads** from `/cpm/disks/A.dsk` over SPI at fixed **4 MHz, mode 0**. **Run 35:** 16/16 × 512 B = 8,192 B PASS, last-sector `sd_crc_prepared=sd_crc_sent=crc_esp=57620`, `aggregate_crc_esp=38047`, `sd_read_us=39883`, `teensy_transfer_us=21918` (1,353–1,389 µs/sector), `wall_us=973037`, **8.222 KiB/s** end-to-end, `needs_reboot=false`. **Run 36:** 128/128 × 512 B = 65,536 B PASS, last-sector `sd_crc_prepared=sd_crc_sent=crc_esp=36668`, `aggregate_crc_esp=46519`, `sd_read_us=323208`, `teensy_transfer_us=177550` (1,343–1,436 µs/sector), `wall_us=7685974`, **8.327 KiB/s** end-to-end, `needs_reboot=false`. Both completed with no CRC mismatches. Compared with earlier **HP-idle** 16/128-sector tests (wall 960,936 / 7,710,205 µs; throughput 8.325 / 8.301 KiB/s), variation was only **+1.26%** wall for 16 sectors and **−0.31%** wall for 128 sectors. The user observed **no perceptible slowdown or interference with HP-85 graphics**. The exact aggregate CRCs also matched the idle runs, verifying equivalent received sector streams. **Milestone reached:** live HP-85 graphics bus service, actual SD card image reads, and SPI DMA reception operated concurrently without errors under these tested conditions. Not a formal worst-case real-time or long-duration guarantee; current test is still **read-only MOSI SD→ESP**, not live CP/M disk-over-SPI or SPI disk writes. **Next engineering work:** reduce two UART round trips per sector and later integrate opt-in CP/M read path with robust fallback; preserve working CP/M UART/Base64 transport and `main` branches.



> **REAL SD→SPI MULTI-SECTOR VALIDATION — 2026-10-18, HP-85 IDLE:** Following the one-sector pass, the user tested actual `/cpm/disks/A.dsk` (8,388,608 bytes) with Teensy SD `O_RDONLY` → ESP32 SPI2 RX at **4 MHz, mode 0**. **16-sector run 33 PASS:** 16/16 × 512 B = 8,192 B, `sd_crc_prepared=sd_crc_sent=crc_esp=57620` (last sector), `aggregate_crc_esp=38047`, `sd_read_us=40209`, `teensy_transfer_us=23149` (per sector 1438–1455 µs), `wall_us=960936`, **8.325 KiB/s** end-to-end, no mismatches/reboot. **128-sector run 34 PASS:** 128/128 × 512 B = 65,536 B, `sd_crc_prepared=sd_crc_sent=crc_esp=36668` (last sector), `aggregate_crc_esp=46519`, `sd_read_us=325208`, `teensy_transfer_us=184970` (1428–1459 µs/sector), `wall_us=7710205`, **8.301 KiB/s** end-to-end, no mismatches/reboot. Each sector independently checked SD prepare CRC, Teensy SPI send CRC and ESP SPI receive CRC; the displayed last-sector CRCs alone are not the only integrity check. In the 128-sector run SD timing represented **4.22%**, SPI loop **2.40%**, and remaining UART/scheduling/protocol coordination **93.38%** of the wall time (~7,200,027 µs); residual is not separately instrumented. **The 16/128-sector runs described in this paragraph were performed with HP-85 idle; subsequent graphics-active runs 35/36 passed, as recorded at the top of this file.** The previous successful 8-MHz SPI+SIMON85 test used synthetic patterns, not real SD reads. **Next stage:** optimize UART/SPI coordination and then safely trial opt-in CP/M disk reads with fallback; the graphics-active SD/SPI stress tests have passed. Existing CP/M UART/Base64 disk service unchanged; no SD writes, no `main` merge.



> **FIRST REAL SD-SECTOR→SPI HARDWARE PASS — 2026-10-18:** User rebuilt/flashed both feature-branch firmwares and ran `/spitest`'s **read-only SD sector test**, **SPI mode 0 at 4 MHz**. Source file `/cpm/disks/A.dsk` (8,388,608 bytes), starting file sector **0**, **1 × 512-byte** sector, run **32**, `state=passed`, `completed_sectors=1`, `needs_reboot=false`, no mismatches. **Three independently reported CRC-16/Modbus values all 14717:** `sd_crc_prepared=14717` (Teensy after SD read), `sd_crc_sent=14717` (Teensy SPI TX), `crc_esp=14717` (ESP32 SPI RX). `sd_first16_hex=00444F574E4C4F4144434F4D00000004`. Teensy measured SD open/seek/read/CRC/close **2,514 µs**, SPI transfer loop **1,444 µs**, total wall **66,117 µs** (other coordination ≈62,159 µs), loop throughput **346.26 KiB/s**, wall throughput **7.56 KiB/s** for this single sector. **This established initial read-only SD→SPI byte integrity for one real file sector; subsequent tests confirmed 16 and 128 sectors while the HP-85 was idle.** Concurrent SD+SIMON85 operation was subsequently verified in runs 35/36. Live CP/M disk reads using SPI, any write path and production transport throughput remain unverified. The working CP/M disk transport remains UART/JSON/Base64; neither `main` nor media modified by these tests. [Test guide](SPI_SD_READ_TEST.md).



> **Read-only SD→SPI diagnostic (2026-10-18, now hardware-tested):** Companion Teensy and ESP32 feature branches now have a separate **4 MHz / SPI mode 0 / MOSI** experiment for real 512-byte sector reads from a named image **directly under `/cpm/disks/`**. The Teensy uses `SD.open(..., O_RDONLY)`, seeks and reads the sector into a dedicated buffer, calculates CRC, and returns only prep metadata over UART (`spi_sd_prepare`). **Only after successful SD preparation** does ESP32 queue its 512-byte SPI2 RX DMA and request `spi_sd_send`, which clocks those exact bytes via Teensy SPI0 at fixed 4 MHz, mode 0. ESP32 verifies prepared CRC, on-wire Teensy CRC, received SPI CRC and sequence/sector; output includes SD read timing, SPI loop timing and end-to-end timing. `/spitest` now includes a distinct **Read SD sectors over SPI (4 MHz)** button, image path, starting sector and 1/16/128 count. Old synthetic full-duplex stress tests and GPIO check remain; **normal CP/M disk transport remains UART/JSON/Base64, no SD write call was added, and `main` remains unchanged**. Goal: repeat 128-sector SD reads while SIMON85 graphics run, and check HP-85 responsiveness. **User locally compiled/flashed and verified 1-, 16-, and 128-sector tests with HP-85 idle**; concurrent SIMON85+SD test remains pending. [Full first-run guide](SPI_SD_READ_TEST.md).



> **Concurrent HP-85 graphics + SPI load test (2026-10-18; physical EBTKS):** User ran a graphics-intensive HP BASIC program using **SIMON85** to draw and fill ellipses *while* exercising the CN3 SPI diagnostic at **8 MHz for 128 × 512-byte sectors**. The SPI test passed, the HP-85 continued drawing throughout, and the user observed **no perceptible graphics slowdown or interference**. This is meaningful hardware evidence that Teensy 4.1 services the live HP-85 bus while running the SPI transfer loop with interrupts enabled. The speed comparison is qualitative (not instrumented), and the SPI sectors were synthetic patterns, **not simultaneous SD-card or CP/M disk I/O**. Continue with opt-in binary sector integration at conservative SPI mode 0 / 4 MHz, retaining the existing UART/Base64 fallback; test SD I/O + HP graphics together before treating the new disk protocol as validated.



> **AUTHORITATIVE HARDWARE BASELINE — 2026-10-18 (supersedes older bring-up notes below):** User physically measured continuity: Teensy module **pin 12 → CN3-29**, ESP32-WROOM module **pad 28 / GPIO17 → CN3-17**; the correct MISO jumper is **CN3-29 ↔ CN3-17** on the actual board. The published V3.0 EBTKS schematic incorrectly places E17 at CN3-15 and our earlier CN3-13 mapping was wrong (CN3-13 is GPIO4). After correcting this, **bidirectional SPI mode 0 at 8 MHz repeatedly passed 128 × 512-byte sectors (65,536 bytes EACH direction, 131,072 total), five cycling patterns, matched MOSI CRC=3840 and MISO CRC=20381 on final block, zero byte mismatches**, Teensy loop total 106,939 us, wall 5,181,721 us. **Mode 1 at 1–4 MHz works**, but at 8 MHz repeatedly fails MISO; run 21's first 16 received bytes were precisely one-bit ROR of expected values, a strong mode/timing bit-slip signature. Its whole-sector CRC did not match a uniform one-bit rotation. Existing ~20 cm Dupont jumpers may affect timing margin. **Do not assume 8 MHz is production-safe:** ESP-IDF documents ~7.2 MHz nominal GPIO-matrix MISO limit. Recommend **mode 0 / 8 MHz only for stress testing**, and **mode 0 / 4 MHz for initial real disk integration** with CRC, sequence, retries and fallback. SD/CP/M disk traffic remains original UART/JSON/Base64; no normal storage code has changed. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md) and [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md). No further GPIO continuity test is needed on this board; the software debug endpoint can remain as optional diagnostic.

**Read first:** [PROJECT_STATUS.md](PROJECT_STATUS.md) gives the concise status and authoritative hardware-tested baseline. This file preserves decisions, why they were made, and the likely next task. Written 2026-10-17.

> **Hardware-confirmed stress result (2026-10-18):** On both repositories' `feature/cn3-spi-read-test` branches, user reports **all SPI tests passed across 1–8 MHz, 1–128 sectors and multiple data patterns**. Longest reported test: 8 MHz, 128 × 512-byte sectors (65,536 B), rotating 5 patterns, run counter 30, zero mismatches, ESP and Teensy final-sector CRC both 3840, aggregate ESP CRC 59319, no reboot required. Teensy timed SPI transfer loops totaled **106,910 us**, min/max **834/836 us** per sector; full wall time **3,845,289 us**. Measured throughput: **598.634 KiB/s** inside Teensy loops versus **16.644 KiB/s** end-to-end with one UART request/reply per sector. This confirms *Teensy MOSI → ESP32 SPI2 receive* under the tested conditions; **ESP32 MISO → Teensy receive, disk data transfers, and a pipelined/burst protocol are not yet tested**. Working CP/M still uses UART/JSON/Base64.

> **Immediate follow-up:** Current development focus is SPI binary transport, *not* the older TFT milestone in the historical sections below. First test reverse ESP32→Teensy MISO transfer (the wire was connected but not data-validated); then reduce per-sector UART coordination and integrate *opt-in*, CRC/sequence-protected 512-byte SD/CP/M transfers. Preserve working CP/M, HP bus ISR, UART protocol, WebSockets, OTA, and the last tested `main` state. [SPI test record](SPI_TEST_README.md).

> **Bidirectional SPI test extension (2026-10-18, source ready for local build):** Both feature branches now support opt-in 512-byte *simultaneous MOSI + MISO* tests. The Teensy SPI0 master checks incoming MISO bytes/CRC while sending MOSI; ESP32 SPI2 slave queues aligned DMA TX/RX buffers and checks MOSI bytes/CRC. The browser `/spitest` offers a `Full duplex` direction selector, retains `MOSI only` regression mode, and starts at 1 MHz / 1 sector. A different deterministic pattern is used on MISO to rule out simple loopback. **This new bidirectional firmware has not yet been compiled or hardware-tested;** only the previous 1–8 MHz receive-only baseline is proven. Follow [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md), pull and build both repos, then test one full-duplex sector at 1 MHz before trying 2/4/8 MHz and 128 sectors. Disk I/O remains UART/JSON/Base64; no media was touched.

> **Full-duplex MISO debugging update (2026-10-18):** User hardware test failed at 8 MHz and again at 1 MHz with matched MOSI CRCs and `first_mismatch_byte=-1`, but `first_mismatch_miso=0` and Teensy MISO RX CRC **47937** (CRC of 512 zero bytes); ESP32 TX CRC was nonzero. Initial jumper mistakenly connected Teensy CN3-29 to ESP CN3-15; user corrected it to **ESP CN3-13 / GPIO17**, but the all-zero symptom persisted. Espressif's ESP-IDF 4.4 SPI slave docs note DMA MISO timing prefers **SPI modes 1/3**; legacy test used mode 0. Both feature branches now include a *new, untested* SPI mode 0/1 selector (matching master and slave) and MISO first-16-byte hex capture (`miso_first16_hex`). **User should pull/build/flash BOTH updated branches, then test full-duplex mode 1 at 1 MHz, one sector**. If still zeros, inspect GPIO17 drive/continuity physically with a logic analyzer, not faster SPI. Preserve previous one-way diagnostic and CP/M UART disk path. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md).

> **MISO isolation follow-up, 2026-10-18:** Even after correcting Teensy CN3-29 ↔ ESP CN3-13, **1 MHz SPI mode 1 full duplex fails** with Teensy `miso_first16_hex=00000000000000000000000000000000`, MISO receive CRC **47937** (512 zero bytes), expected ESP TX CRC **13577**, while MOSI CRCs match (**60981**); first MISO mismatch 0. A new **GPIO-only MISO line test**, not yet built/flashed, is committed to BOTH feature branches: POST `/api/spitest/gpio` or press **Check MISO wire (GPIO LOW/HIGH)** on `/spitest`. ESP32 releases SPI2, drives GPIO17 low and high; Teensy temporarily releases SPI0 and samples pin12 digitally through UART. Inspect `gpio_low_read` and `gpio_high_read`: expected 0/1. See [SPI_BIDI_TEST.md](SPI_BIDI_TEST.md) for build/use/interpretation. Disk path unaffected.

> **IMPORTANT MISO PIN MAPPING CORRECTION 2026-10-18:** The original wiring diagram/table erroneously identified ESP GPIO17 as CN3-13. **That pin is E04/GPIO4** on the published schematic. Module GPIO17 is WROOM module pad 28. The published drawing suggests CN3-15 (E17), but the user reports actual board module pin 28 goes to CN3-17. **STOP prescribing a CN3 pin until physical, power-off continuity is measured from WROOM pad 28 to CN3-15/17.** GPIO17 is still the correct firmware selection; no software reassignment until actual hardware validated. Also update derived SVG after final mapping. See [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md).
