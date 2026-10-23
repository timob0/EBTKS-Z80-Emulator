# SIMON85 / EBTKS — start here in the next ChatGPT conversation

**CPDCAT SECOND HARDWARE RESULT (2026-10-23; NOT RESOLVED):**
The user reported `CPDCAT: CANCELLED OR DIRECTORY READ FAILED` at an
apparently idle CP/M prompt after the auto-Return bridge fix. Unlike
the previous timeout, the service reached a terminal error/cancellation
state. The HP text conflated multiple internal states: root cause must
be established from `GET /api/cpm/service/status` and GET
`/api/cpm/service/directory` before any new request. ESP32 feature
branch now records `checkpoint` and `directory_error` read-only
diagnostics, displays separate HP failure labels, and adds native
tests. The HP/Teensy feature branch and `main`/`recovery` remain
untouched. Details in [CPDCAT_STAGE_B1.md](CPDCAT_STAGE_B1.md).
Await fresh branch-tip CI and local Mac TFT linker before ESP32 flash.

**CPDCAT HW TIMEOUT DIAGNOSIS AND FIX (2026-10-23; NOT YET RETESTED):**
HP `CPDCAT` reached the ESP32 service request but timed out. Verified source
bug: bridge `service_fetch` only forwarded the old four CCP PCs and never
called the new BDOS line count (D9EF) or BIOS CONIN polling (E826/E831)
observers. ESP32 feature branch fixed `z80_bridge.h` exact-PC filter and
added a real redcode opcode-fetch regression. Native CI passes for the fix;
both ESP32 variant/link and local macOS TFT validation are required before
refashing. No Teensy or HP source changes were needed. Documented in
[CPDCAT_STAGE_B1.md](CPDCAT_STAGE_B1.md). Preserve all baseline/recovery
firmware; do not merge until repeat hardware verification.

**LATEST EXPERIMENT: HP BASIC CPDCAT + automatic empty CCP Return (2026-10-23).**
The directory B1 hardware test and normal DIR/Multiplan/Media Manager regression
all passed. On three *isolated* feature branches only, CPDCAT has now been
implemented (NOT hardware-accepted): ESP32
`feature/cpm85-file-transfer-hpib-ui`, Teensy
`feature/cpm85-catalog`, HP LOADBIN
`feature/cpm85-catalog` (CPM85 0.04c). Mailbox opcode 7 requests
`CPDCAT "A:"`, which enumerates up to 32 CP/M directory extents.
Automatic Return is injected on the Z80 CPU task **ONLY** when the real
native CCP input line is proven empty, no queued real keys exist, and the
exact BIOS console-input status opcode is executing. The existing D382
post-BDOS safe point remains authoritative. Explicit FCB drive does not
change default drive. Browser directory POST now auto-completes also.
The non-BDOS `/api/cpm/service/probe` stays manual.
**Read [CPDCAT_STAGE_B1.md](CPDCAT_STAGE_B1.md) for build order and tests.**
The three GitHub CI builds, macOS TFT link, exact flashed SHA, and physical
HP/CPM regression must all pass before merge. `main` / `recovery/`
are untouched.


**NEW STAGE B1 DEVELOPMENT (2026-10-23; FIRST BDOS DIRECTORY HARDWARE PROBE PASS, REGRESSION PENDING):**
On ESP32-only branch `feature/cpm85-file-transfer-hpib-ui`, a bounded, read-only
BDOS directory search was added behind the hardware-proven CCP safe-point gate.
`POST /api/cpm/service/directory` arms it; a blank CP/M Return executes BDOS
17/18 on the CPU worker, and `GET` on that endpoint retrieves up to 12
directory *extents* for the current drive/user. It verifies native BDOS vector
and default DMA, preserves CPU/TPA/DMA, and forces a Z80-only reset on an
interrupted BDOS operation. Old non-BDOS probe is retained. New native
`tests/cpm_directory_native.cpp` and both ESP32 CI builds are enabled on this
branch; check the latest CI and local Mac TFT link before any flash.
**No Teensy/HP changes, no writes, no transfer or management UI yet.**
The full cautious procedure is in [CPM85_STAGE_B1_DIRECTORY.md](CPM85_STAGE_B1_DIRECTORY.md).
`main`, `recovery/` and the previously hardware-tested baseline were not
modified. The follow-on main commit `7b75be7` updates only user-maintained
`recovery/firmware.bin` and is intentionally not brought onto this branch.

**2026-10-23 Stage B1 first hardware result (user-reported):** The operator sent
`POST /api/cpm/service/directory` and received HTTP **202**, with
`accepted=true`, `supported=true`, `state=waiting_for_empty_return`.
After blank Return, GET returned `state=passed`, `count=12`,
`truncated=true`, `scope=current_drive_current_user`, and 12 plausible
native BDOS directory extents including `DOWNLOAD.COM`, `STAT.COM`,
`PIP.COM`, `ASM.COM` and `MBASIC.COM` (extent=1), all user 0.
The 12-entry limit is intentional; this is not file aggregation.
The latest GitHub CI run for source SHA `55a7c0803ba4b66106e5637317af8f90c0057078`
passed: https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/40024339445 .
**Not yet evidenced:** post-service `DIR`, Multiplan, HP/TFT/Web/Media
Manager regression, nonempty cancellation, MODEM busy rejection, alternate
current-drive/empty-directory testing, and exact running SHA (no health
reading furnished). No merge, no Teensy/HP firmware changes.


**CURRENT HANDOFF — 23 October 2026, memory-optimized firmware now MERGED:** ESP32 `feature/cpm85-management-console` was merged into `main` through [PR #8](https://github.com/timob0/EBTKS-Z80-Emulator/pull/8), merge commit `025a519d318370bb8d3961b3286bdd5993dd808c`, after the user completed local macOS builds, flash and a successful quick hardware test of the final consolidated TFT target. Full functionality was physically verified earlier: two MODEM/BBS cycles, correct Web Terminal ANSI/CP437, modem worker cleanup on return to CP/M, Media Manager recovering after both cycles, CP/M `DIR`/Multiplan, gzip pages and Home Page link. Final post-second-cycle hardware reading `modem_state=0`, `free_8bit=29452`, `largest_8bit=27636`, no modem task. Supported PlatformIO variants are ONLY `esp32dev` (full headless bundle) and `esp32dev-tft` (same plus ST7796), both with Telnet disabled by default; `-DEBTKS_TELNET_ENABLE=1` is an advanced compile-time option. Both use early IRAM32-only SPI read cache. See [FIRMWARE_VARIANTS.md](FIRMWARE_VARIANTS.md) and [NEXT_CHAT_STAGE_A_CHECKPOINT.md](NEXT_CHAT_STAGE_A_CHECKPOINT.md). **Next work:** start a separate new ESP32 feature branch from current `main` for CP/M file transfer, HPIB output and the management UI; design and coordinate any necessary Teensy/HP changes separately, and never overwrite user-maintained `recovery`. Earlier October 22 text below is chronological history, not current release status.

**2026-10-22 Stage A local ESP32 TFT linker fix:** macOS ld failed on first safe-point build `e2e76c33`: `.dram0.bss region dram0_0_seg overflowed by 16 bytes` though GitHub CI had succeeded. New branch source has moved tiny CCP Gate out of static DRAM into normal heap allocated once by existing Z80 task, reused bridge.device instead of allocating redundant service context/state fields (no new Wi-Fi hooks or special EXEC allocators). CI confirms exactly 16-byte reduction in .dram0.bss / linked static DRAM: 124,552 -> 124,536, stable main reference 124,528. A new CI post-link check `tests/check_esp32_dram_budget.py` enforces <=124,544 static TFT DRAM. Re-pull and rebuild `pio run -e esp32dev-tft -t clean; pio run -e esp32dev-tft`; user must confirm Mac linker success before OTA. Stage A `GET /api/cpm/service/status`, `POST /api/cpm/service/probe` remains hardware-UNTESTED. No Teensy/HP changes, no merge, recovery unchanged.

**LATEST DEVELOPMENT 2026-10-22 — Stage A CP/M CCP-safe service probe (not yet hardware-tested):** On new `feature/cpm85-management-console` (ESP32 repo only), implemented exact-PC CCP prompt/input gate `cpm_safe_point.h` (checks native CP/M 2.2 CCP opcode signatures, D382 prompt -> D19C BDOS function 10, D1A4 BDOS return; nonempty command cancels, warmboot cancels, input-generation race guard); a second redcode/Z80 pre-instruction trap `Z80_BRIDGE_SERVICE` independent of existing E603 warmboot trap; `cpm_service_probe.h` runs a 5-byte Z80 **non-BDOS** program in saved/restored TPA scratch C000 with complete Z80 CPU state restoration. Web API `GET /api/cpm/service/status`, `POST /api/cpm/service/probe`. Only accepts while verified CCP awaiting input; user MUST hit **blank RETURN** in CP/M terminal to invoke at next safe CCP D382. Nonempty command cancels, running app refuses (409), no background queue, no HP/Teensy changes, no CP/M disk/BDOS service yet. Native tests at `tests/cpm_safe_point_native.cpp`, CI workflow now includes this feature branch. Full manual instructions `CPM85_SAFEPOINT_SERVICE.md`. Last physically accepted recovery image must remain untouched. Latest build CI must complete and Git SHA be checked before inviting hardware test. Existing three repos main remain untouched by this Stage A development.

**MERGED + NEW BRANCH — 2026-10-22:** CPM85 v0.04b image/drive/keyboard/modem/TFT functionality has been **physically accepted by user** and merged to `main` in all 3 repos: ESP32 PR #7 main `eca19c576f68ec8a90748356aeaa4b4ac861f092`, Teensy PR #3 main `c8617c07f06c2027ade96570386adb2ec3cc2edb`, HP Option ROMs PR #1 main `691154251a6852767dbec322b19168ec2d4add41`. Fresh branch `feature/cpm85-management-console` created across all 3 from their respective merge commits; ready for next development. User can `git fetch origin && git switch main && git pull --ff-only` in each repo, then `git switch feature/cpm85-management-console` as needed. No need to reflash existing working firmware merely due to Git merge; ESP32 build SHA stamps HEAD so a newly built main firmware would show a different SHA. Existing `recovery/` scripts and firmware preserved and now included in main. Supersedes all earlier 'ready for acceptance' or 'do not merge' notes below. Next scope: reusable PRINTER85 KEY LABEL/save-restore with K4 PREV / K8 NEXT for a paged HP management CRT; file transfers and device mappings in follow-on stages.

**2026-10-22 CPM85 v0.04b UI polish (hardware acceptance pending):** HP user confirms CPDIMGS + CPDMK now work. To correct HP BASIC ENDLINE ERROR 92, HP v0.04b uses native OUTSTR newline on every statement path (including already-attached CPM), tracks trailing LF in drive/image reply to avoid duplicate newlines, and prints `CREATING <imgname>...` before CPDMK blocks. ESP32 CPDIMGS skips filenames starting with dot (macOS `._...` metadata). Tests updated; Teensy needs **no firmware update** for this minor refinement. Rebuild ESP32 TFT for filter, rebuild/load HP CPM85 v0.04b for banner/newline, keep current Teensy firmware. Latest source/CI and remaining hardware tests must be checked; the three PRs remain draft/unmerged until accepted. Separate 8KiB SPI cache fallback ("dram8") observation remains unrelated/unmodified.

**2026-10-22 HARDWARE BUG / FIX — CPDIMGS + CPDMK "INVALID CP/M REQUEST":** User flashed coordinated HP CPM85 v0.04a / Teensy / ESP32 commit `bbd7d079504236e10468872e3e23ba3ca7fe741a`; **both new BASIC commands fail** with `INVALID CP/M REQUEST`. This was precisely traced to **ESP32** `cpm85_drive_control.h` `receiveCommand()`: although `execute()` dispatches opcodes 5/6, common validation still rejected `op>4`. Corrected to `op>6` in ESP32 commit `95e94264c6eba54381f521e4bfbbfbe4c74ffa1b`, plus `tests/check_cpm85_bridge.py` asserts max admitted operation covers every dispatch (commit `7af27f91cd55fcdfee18d3ee4a34bc90c78e7cf6`). **Rebuild/reflash only ESP32 TFT from latest `feature/cpm85-drive-control`.** Keep HP CPM85 v0.04a and Teensy unchanged. Hardware retest needed: `CPDIMGS`, `CPDMK "TESTNEW.DSK"`, duplicate protection, CPDMNT G, CPRESET and CP/M G: read/write. User health before correction verifies ESP32 SHA `bbd7d079...` with no watchdog/panic, but SPI cache storage changed from `iram32` to `dram8` (8KiB scarce DRAM), a separate fallback from a failed/nonaccepted 32-bit IRAM allocation; do not change SPI code as part of this targeted fix. **Do not merge three draft PRs until new hardware test passes.**

**2026-10-22 NEW DEVELOPMENT (HARDWARE TEST PENDING) — CPDIMGS + CPDMK:** Coordinated development branches ESP32 `timob0/EBTKS-Z80-Emulator`, Teensy `timob0/ebtks-alt-firmware`, HP-85 LOADBIN `timob0/HP85-Option-Roms` now implement **CPM85 v0.04a** `CPDIMGS` to list available .DSK images and `CPDMK "NAME.DSK"` (or `CPDMK "NAME"`) to create a new 8 MiB native CP/M hard-disk image in `/cpm/disks`. Teensy creates incrementally using local SD with 4-KiB per main-loop budget, 512-byte static E5 pattern, O_EXCL anti-overwrite, flush+size check and failure cleanup; ESP32 polls compact control JSON, HP uses existing CP/M management mailbox opcodes 5/6 with longer bounded image creation wait. All three CI suites passed at source commits before docs changes (HP run 39846359109, Teensy run 39846771145, ESP32 run 39846176138); user has NOT tested this combo on physical hardware. Keep last stable firmware and recovery/ image untouched. Test `CPVER`, `CPDIMGS`, `CPDMK "NEWWORK.DSK"` with a unique name, `CPDIMGS` again, `CPDMNT "G:","NEWWORK.DSK"`, `CPRESET`, and CP/M G: DIR/write/read. Test duplicate `CPDMK` rejects without overwriting. **MERGE TO MAIN ONLY AFTER HARDWARE ACCEPTANCE** as requested; then start a new feature branch for KEY LABEL paged management UI, CPFGET/CPFPUT, device maps. PRINTER85 ROM has working KEY LABEL save/restore and K4 PREV/K8 NEXT code. No NEXT/PRINTER85 changes in this phase.

**LATEST HARDWARE CONFIRMATION — 2026-10-22, GIT-ID-ONLY BUILD PASSED:** User updated and flashed the corrected ESP32 TFT development firmware and reported success. Actual `GET /api/system/health` returned **`git_commit: "bf4b36b84b5cacfe6af5b5aba5ee94d42ae72952"`**, proving the exact previously CI-tested firmware is running, not an older image. Uptime 40,781 ms; reset_reason 3 (ESP32 software reset), reset_watchdog/panic/brownout all false; heap_free 67,996, heap_min_free 62,188, largest_free_8bit_block 25,588; modem state 0/inactive; SPI cache active in `iram32`, DMA/worker initialized, read failures 0. **Hardware boot and health API firmware identification ACCEPTED.** User has not reported a post-flash MODEM/BBS stress session; do not claim it passed. The unsafe Wi-Fi/RSSI/stall diagnostics remain completely removed/quarantined. **This is the verified running firmware SHA even though later documentation-only Git commits may advance branch HEAD; a new firmware build embeds the new HEAD SHA.** No further firmware flashing is required for this acceptance. Feature branches remain unmerged.

**2026-10-22 — DIAGNOSTICS ROLLED BACK; Git commit ID ONLY (NEW ESP32 BUILD, PHYSICAL TEST PENDING):** Per user request following successful Teensy USB recovery, ESP32 `feature/cpm85-drive-control` runtime sources `ebtks_web.h`, `web_terminal_v2.h`, and `serial.h` are restored **byte-for-byte from physically accepted pre-diagnostics commit `73a90b4dbecb4d15b31a4143d3efc20ab9de281c`**, except for one generated-header include and `v["git_commit"]=EBTKS_BUILD_GIT_COMMIT` added to the existing `/api/system/health` endpoint. `ebtks_network_diag.h` has been DELETED; no `EbtksNetDiag::begin()`, Wi-Fi hooks, IRAM/32-bit allocation, connection/RSSI metrics, or WebSocket/Telnet stall counters remain. PlatformIO `extra_scripts = pre:tools/pio_git_revision.py` generates `.pio/build/<env>/ebtks_build_revision.h` containing the **40-character HEAD SHA only** at compile time; no runtime initialization and no `git_dirty` key. Regression checks prohibit the removed hooks and verify both firmware binaries embed the expected SHA. **CI and user hardware boot/OTA acceptance must be checked; do not claim the new image is hardware verified yet.** Device currently operational after physical recovery to last known-good firmware. Branch remains UNMERGED. Old warning about the diagnostic images below remains historically accurate, but DOES NOT describe the newly reverted source tip.

**RECOVERY CONFIRMED — 2026-10-22:** The user completed the documented Teensy 4.1 USB-to-ESP32 programming-bridge recovery and reported: **"ESP unbricked and back to operation again."** The current physical machine is operational after restoration of the **last known-good, pre-network-diagnostics firmware**. The recovery was performed following the guidance to rebuild commit `73a90b4dbecb4d15b31a4143d3efc20ab9de281c`, but the user has not separately provided a flashed image SHA; **do not claim the precise flashed SHA was measured**. Keep `feature/cpm85-drive-control` tip's network diagnostics/Git identity build **quarantined**, even though CI passed. The working TFT ANSI BBS, drive-control, modem and Web UI baseline should be preserved; no further ESP32 code changes were made after the recovery report. If resuming diagnostics, first isolate the early-boot failure and require controlled hardware tests, not just CI. The original failed-binary warning below is historical and recovery is now **complete**, not pending.

**CRITICAL — 2026-10-22 HARDWARE FAILURE / ROLLBACK REQUIRED:** After flashing the Wi-Fi/network-stall diagnostics + Git-SHA build from the development branch, user reported **ESP32 completely unresponsive**: no TFT display and no Web UI, persisting after power cycle. **DO NOT FLASH any of the diagnostic builds yet**, including CI-green `c09a94647b6bf3fd86fce0f2688b487c587c6cba`. Native and PIO compilation passed but missed a severe hardware startup regression. Leading suspect is `EbtksNetDiag::begin()` in `ebtks_web.h` (runs before `WiFi.mode` / before TFT starts), specifically 32BIT|EXEC memory allocation/initialization or Wi-Fi event callback; NOT PROVEN without crash trace. **IMMEDIATE RECOVERY:** known-good physical TFT+BBS-ANSI hardware-accepted pre-diagnostics ESP32 commit `73a90b4dbecb4d15b31a4143d3efc20ab9de281c`. In repo: `git switch --detach 73a90b4dbecb4d15b31a4143d3efc20ab9de281c; pio run -e esp32dev-tft -t clean; pio run -e esp32dev-tft`. Web OTA currently dead; use proven **Teensy 4.1 USB ESP32 ROM recovery bridge** from `ESP32_RECOVERY_USB.md` (`ESP32 Prog`, read/verify partition table, write only application partitions 0x10000 and 0x150000 if verified; preserve NVS/OTA/SPIFFS). User has successfully used this recovery in the past. **Recovery not confirmed yet.** Do not merge/quarantine broken development tip; restore first, then isolate one telemetry change at a time with hardware boot acceptance. Git-SHA provenance was build-verified but this combined firmware build is not safe.

**2026-10-22 — Wi-Fi/freeze diagnostics and firmware Git identity (new ESP32 image; hardware validation pending):** Extended existing `GET /api/system/health` with `git_commit` (full 40-char Git HEAD SHA embedded by `tools/pio_git_revision.py` during PlatformIO compilation), `git_dirty` (true when local sources differ), Wi-Fi RSSI/status/IP/disconnect/reconnect counters and reasons, current 8-bit free/minimum heap, and WebSocket/Telnet write-stall event/duration counters. Build includes a CI assertion that **both firmware.bin images contain that exact SHA**. The 15-counter bank is allocated at start from spare INTERNAL/32BIT/EXEC memory (aligned 32-bit atomic operations); `network_counters_available` reports allocation success. Important: a previous diagnostic attempt used static 8-bit DRAM and **overflowed TFT linker by 16 bytes**; this was corrected in `ebtks_network_diag.h` before release. No changes to Wi-Fi reconnect settings, WebSocket timeout (10s), Telnet write retries, modem, HP/Teensy or disks. These counters reset with ESP32 restart; RSSI null if disconnected; HTTP cannot report while Wi-Fi entirely unavailable. Existing unlocated `min_free_8bit=628` B remains under investigation. After GitHub CI green, user can flash ESP32 TFT only and collect /api/system/health before/after weak-Wi-Fi tests. **Not merged or hardware tested** yet.

**LATEST HARDWARE ACCEPTANCE — 22 October 2026, TFT ANSI BBS colors: PASS.** After rebuilding/flashing the ESP32 TFT firmware with the shared ANSI capability responder (`console_ansi_probe.h`), the user tested a BBS via the **physical TFT Terminal** and confirmed: "**Excellent - that fixed the color issues when using the TFT Terminal.**" The previous TFT-only monochrome issue is **resolved on hardware**. The fix answers VT100/ANSI identification and status queries from the common Z80 console router, independently of the Web-Terminal. Native, Web, ESP32 headless and TFT CI previously passed (run 39831102066). Keep this source as the verified experimental baseline on `feature/cpm85-drive-control`; no further firmware changes needed for ANSI detection. Three synchronized drive-control branches remain UNMERGED until requested. Previous ESP32 8-bit heap low-water warning remains separate.

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
