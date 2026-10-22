# SIMON85 / EBTKS — start here in the next ChatGPT conversation

**2026-10-22 — TFT BBS ANSI auto-detection fix now implemented (HARDWARE TEST PENDING):** User observed BBS ANSI colors through Web-Terminal or attached Telnet, but **monochrome on TFT**. Source root cause: browser-only JavaScript replied to VT100 `ESC[c`, `ESC Z`, `ESC[5n`, `ESC[6n`; TFT rendering parser explicitly ignored those probes. ESP32 `feature/cpm85-drive-control` now contains a small allocation-free `console_ansi_probe.h` cursor/probe tracker invoked from the common `serialTask`. It returns `ESC[?1;0c` (DA/DECID), `ESC[0n` (DSR status), `ESC[row;colR` (DSR cursor) to **Z80 CON: input**, which MODEM.COM can relay to a remote BBS. Browser JS stopped responding to those probes to avoid duplicate replies; if an external inbound Telnet client is connected the native responder abstains because that client should answer. No TFT drawing changes, no added task/heap, no Teensy/HP changes. **CI fully passed** for code commit `533168b4a376` ([GitHub Actions run 39831102066](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39831102066)): native JS/C++ and ESP32 headless/TFT builds; hardware BBS color still unverified. Test on TFT *without a Web/Telnet client connected*, then Web and Telnet regressions, check health/heap. Details: `CPM_WIFI_MODEM.md`, `NEXT_CHAT_HANDOFF.md`.

**Latest development milestone — 22 October 2026:** User **hardware-confirmed full CPM85 v0.03f drive-control operation** with coordinated `feature/cpm85-drive-control` branches: HP BASIC `CPDMNT` successfully assigns a disk, `CPRESET` activates it and reboots **only the Z80/CP/M**, the new drive is accessible under CP/M, and HP BASIC `CPDUMNT` successfully removes its pending mount. `CPSTATUS` and shared Web Media Manager state had already been verified. This closes the previous mount/unmount timeout and inadvertent Z80-reboot regression after fixing ESP32 `cpmStoreSaveDesired` NVS verification to avoid overflowing the 3,584-byte HP keyboard worker stack. **No source changes required after this user test; hardware acceptance passed.** Development branches **NOT YET MERGED**: HP `timob0/HP85-Option-Roms`, Teensy `timob0/ebtks-alt-firmware`, ESP32 `timob0/EBTKS-Z80-Emulator` all on `feature/cpm85-drive-control`. Stable ESP32 `main` remains prior merged terminal/modem baseline. User has not yet explicitly verified that a second `CPRESET` after `CPDUMNT` makes drive inaccessible to CP/M, so if seeking exhaustive coverage test that before merge. A separate historical ESP32 `min_free_8bit=628` B transient remains a monitoring/stress caveat; no evidence here of an ongoing leak. See `CPM85_DRIVE_CONTROL.md` and `ESP32_MEMORY_AUDIT.md`; do not confuse old “pending physical test” notes lower in historical files with current acceptance.

**Status: 21 October 2026.** This short note supersedes outdated status messages in the historical, append-only `NEXT_CHAT_HANDOFF.md`. Consult that file for detailed history and measurements, not for old instructions to flash experimental firmware or merge old branches.

## Current production baseline

- ESP32 repo: **[timob0/EBTKS-Z80-Emulator](https://github.com/timob0/EBTKS-Z80-Emulator)**, production branch **`main`**.
- CP/M Wi-Fi modem + RAM optimization + ANSI/CP437 Web terminal were merged via **[PR #6](https://github.com/timob0/EBTKS-Z80-Emulator/pull/6)**, full two-parent merge commit **`b3d02b43bbc17bd8bd146448b65f3bc848dd2115`**. All 63 feature commits preserved; no source diff from the physically tested feature branch. Docs-only update `a2453ecf...` followed.
- **Post-merge CI PASSED**: [run 39602560884](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39602560884); **docs-updated main CI also PASSED**: [run 39602952089](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39602952089). Note: future docs commits may trigger fresh CI; prefer the latest run when resuming.
- HP-85 physical setup: Teensy 4.1 EBTKS firmware + HP CPM85 keyboard-capture module, ESP32 ESP-WROOM classic dual-core, ST7796 SPI TFT. **Only the ESP32 repo was merged**. Separate hardware-tested Teensy `timob0/ebtks-alt-firmware` branch `feature/cpm85-keyboard-forwarding` and HP ROM `timob0/HP85-Option-Roms` branch `feature/cpm85-keyboard-capture` / CPM85 v0.02a LOADBIN remain separate; do **not** assume they have been merged into those repos' `main`.
- Two supported ESP32 PlatformIO targets: `esp32dev` (headless) and `esp32dev-tft` (physical 80×24, VT100, ANSI-16, IBM CP437). Production TFT build: `pio run -e esp32dev-tft`.

## Hardware-validated functions

- Full independent parallel Z80 CP/M system alongside HP-85 BASIC, physical keyboard capture (HP K8 releases it to BASIC; CPM command can reattach), CRT / WebSocket Z80 console, inbound Wi-Fi Telnet and ST7796 TFT mirror.
- User reports **CP/M terminal, screen, keyboard and disk emulation complete and excellent**; Multiplan, Turbo Pascal, CP/M `DIR`, physical keyboard mappings and display previously verified.
- New Web terminal VT100 browser parser renders **ANSI 16 colors**, underline/bold/inverse, complete IBM **CP437** extended 0x80..0xFF glyphs, DEC box drawing, optimized grouped HTML spans. **User physically tested another real Synchronet BBS session using Web Terminal and reports it looks gorgeous.** This closes the Web terminal hardware acceptance milestone.
- CP/M virtual 8250 Wi-Fi/Hayes modem at ports **0x88..0x8F**, utility **`MODEM.COM`** on CP/M disk. `AT`, `ATI`, `ATDTvert.synchro.net:23` all physically tested. **Ctrl+X exits MODEM.COM to CP/M** (user confirmed). For a clean hangup while still connected: one second silence, `+++`, one second silence, `ATH` + Enter, then Ctrl+X. Do **not** assume exiting MODEM.COM alone closes remote TCP socket.
- Incoming Telnet listener shares existing Web background FreeRTOS task rather than reserving separate 3,072-byte stack. `telnet_connected` went 0->1 on physical hardware; inbound connect confirmed. The Telnet/Web/TFT channels share the **same CP/M process**, so `DIR` entered while MODEM.COM is running yields `ERROR` because it goes to modem parser. A prior Z80 coldboot was a **manual reboot by user** due to that confusion, **not a crash or proof of a memory leak**.
- Proven memory fix: SPI 16-sector CRC-framed read-ahead **8,192-byte cache in aligned 32-bit-only IRAM**, `cache_storage="iram32"`, original 8-bit DRAM fallback. SPI DMA RX/TX remain DMA-capable DRAM, writes use UART, read clock 4 MHz. Hardware: at least 112 SPI frames, 7 batches, 64 hits, no CRC/SPI errors or UART fallbacks. Cache relocation + shared Telnet listener stopped earlier Web/HTTP failures while modem connected. At BBS runtime user saw `free_8bit=17612`, `largest_8bit=12788` with functioning Web UI; after session `18408/17396`. Normal MODEM/AT/ATI approximately `19940/19444`. Do **not** split Z80 64KiB native 8-bit RAM without specific new evidence; current integration works.
- Detailed memory audit: `ESP32_MEMORY_AUDIT.md`. On classic ESP32 internal heap includes 32-bit-only IRAM; never mistake `ESP.getFreeHeap()` for free 8-bit heap.
- Read-only diagnostics: `GET /api/system/memory`, `GET /api/system/memory/pools`, `GET /api/system/health`, `GET /api/cpm/spireads`. Prefix route collision was fixed by registering `/memory/pools` before `/memory`; `/memory` also includes `min_free_8bit`. The change is CI-validated; **separate /pools response after the fix was not explicitly user hardware-tested**. It is not a blocker for the successful CP/M integration.

## How to resume

Open a **new chat in the same SIMON85 project**. First ask the assistant to use this file, `NEXT_CHAT_HANDOFF.md`, and specific focused docs. Give the next desired development task; do not treat old experimental handoff sections as outstanding work. The repo, not automatic conversation memory, is the durable source of truth.

Suggested copy-and-paste first message:

> We're continuing SIMON85. Read the current GitHub `main` of `timob0/EBTKS-Z80-Emulator`, especially `NEXT_CHAT_QUICKSTART.md`, the first section of `NEXT_CHAT_HANDOFF.md`, `CPM_WIFI_MODEM.md`, and `ESP32_MEMORY_AUDIT.md`. The complete CP/M/TFT/Web/Telnet/IRAM-SPI/Wi-Fi-modem feature was hardware-validated and merged via PR #6; main CI is green. Treat that as the stable baseline. Do not repeat completed modem or terminal tests and do not modify source or create new feature branches until I specify our next task.

## Local pull/build — only if another build is needed

```sh
git fetch origin
git switch main
git pull --ff-only
pio run -e esp32dev-tft
```

Firmware binary: `.pio/build/esp32dev-tft/firmware.bin`. **Merging on GitHub does not change the ESP32's already flashed, physically tested firmware**; no firmware reflash is necessary just because `main` was merged. Browser firmware updater remains available. Recovery via physical Teensy service console / USB `ESP32 Prog` is documented in `ESP32_RECOVERY_USB.md`.

## Scope and optional future work

Treat the CP/M integration as **finished** unless a new regression appears. Potential *separate future tasks*, only with explicit user direction: HP/Teensy repository merges, further TFT hardware scrolling optimization, convenient SD-card image creation, and later SIMON85/PRINTER85 enhancements. Whole 8MiB disk transfer through the small ESP32 JSON/Base64 HTTP buffers was rejected as impractical; removable SD card management via PC is acceptable. Preserve tested functionality and do not start unrequested changes. **User prefers explicit approval before creation of new Git branches.**

Detailed references:
- [Handoff](NEXT_CHAT_HANDOFF.md) — extensive historical development chronology
- [Modem](CPM_WIFI_MODEM.md)
- [Memory audit](ESP32_MEMORY_AUDIT.md)
- [USB recovery](ESP32_RECOVERY_USB.md)
- [Getting started](GETTING_STARTED.md)
