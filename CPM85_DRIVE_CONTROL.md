# CPM85 v0.03a — HP-85 control of CP/M disk assignments

## 2026-10-22 — Hardware test: CPDIMGS / CPDMK request-validator mismatch fixed in ESP32

HP-85 user reflashed Teensy/ESP32 and loaded CPM85 v0.04a. Both `CPDIMGS` and `CPDMK` printed **`INVALID CP/M REQUEST`**. `GET /api/system/health` confirmed the ESP32 ran `bbd7d079504236e10468872e3e23ba3ca7fe741a`, so not stale firmware. The exact source defect was ESP32 `cpm85_drive_control.h`: `execute()` handled opcodes **5/6**, but `receiveCommand()` still had `if(op<1||op>4||strlen(args)>91)`, discarding both. Corrected to `op>6`, plus host static test automatically checks the dispatch/validator agreement. **Only rebuild/flash ESP32**; existing HP/Teensy image implementation and original commands unchanged. Await physical retest before merging draft PRs to main. This new hardware result does not establish correctness of Teensy SD image creation yet. The simultaneously observed `spi_cache_storage:"dram8"` indicates 8-KiB SPI cache fallback into 8-bit RAM vs previous IRAM32, but is independent of this command-ABI bug; preserve baseline and investigate only separately if necessary.

## 2026-10-22 — CPDIMGS / CPDMK implementation (CI passed; hardware acceptance pending)

This final drive-control feature adds HP BASIC commands through **CPM85 v0.04a** in the coordinated `feature/cpm85-drive-control` branches:

- `CPDIMGS`: lists `.DSK` files from `/cpm/disks` (not `.LIF` or all SD files), case-insensitively, with KiB sizes. Uses the existing Teensy directory RPC and prints a count and explicit truncation notice if the HP reply's 680-byte payload budget is exceeded. No mount or disk modification.
- `CPDMK "WORK.DSK"`: creates a new exactly **8,388,608-byte** CP/M native image under `/cpm/disks`. Optional extension `CPDMK "WORK"` appends `.DSK`. Input allows alphanumeric, underscore, hyphen, dot, rejects relative paths/traversal, hidden names and other extensions. Existing file names are never truncated or overwritten (Teensy checks `SD.exists` and opens `O_EXCL|O_CREAT`). Creation writes `0xE5` in all sectors, including empty CP/M directory entries. Teensy writes at most **4096 bytes per ordinary loop iteration** using a static 512-byte sector pattern buffer; the ESP32 only sends begin/status control JSON and polls until completion. An incomplete image is removed on Teensy SD write/sync/size verification failure. The CP/M mount table is not changed; use `CPDMNT "G:","WORK.DSK"` then `CPRESET`.
- CPDMK's HP-side polling timeout was selectively extended while existing `CPSTATUS/CPDMNT/CPDUMNT/CPRESET` timers and keyboard interrupt logic stay unchanged. The ESP32 caps its wait to 45 seconds; if the job is still running, the reply says to inspect with CPDIMGS. No 8 MiB transfer over JSON/Base64/UART.

**Native/firmware CI successful before documentation update:** HP CAPASM/CAPLIF + keyword ABI: [39846359109](https://github.com/timob0/HP85-Option-Roms/actions/runs/39846359109); Teensy firmware plus full 8 MiB mock creation, no overwrite, and failure cleanup: [39846771145](https://github.com/timob0/ebtks-alt-firmware/actions/runs/39846771145); ESP32 headless and TFT: [39846176138](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39846176138). These tests **do not establish physical HP/SD acceptance**. User must build/flash ALL THREE matching development variants, then test CPVER/CPDIMGS/CPDMK on a harmless new name, verify 8,388,608-byte SD file and CP/M access after mount/reset. Do not merge feature branches into main or promote the recovery/ stable firmware image before physical acceptance. The earlier Wi-Fi diagnostics startup regression remains removed.

**Next branch after merge:** HP management console using PRINTER85 saved KEY LABEL buffers and K4/K8 previous/next pages, with management page 1 media, page 2 file transfer, page 3 device mappings; not implemented in this milestone.


## 2026-10-22 — FULL DRIVE-CONTROL HARDWARE ACCEPTANCE (LATEST)

**USER-VERIFIED SUCCESS:** After reflashing the ESP32 TFT `feature/cpm85-drive-control` image with the small-stack NVS verification correction and retaining HP `CPM85 v0.03f` and matching Teensy firmware, HP BASIC `CPDMNT` now completes normally and sets a disk assignment; `CPRESET` starts the Z80/CP/M with that assignment applied; user then **successfully accessed the newly mounted drive from CP/M**. Returning to HP BASIC, `CPDUMNT` also worked to remove the mount. The previously observed unexpected Z80 reset plus `CPM85 DRIVE TIMEOUT` on mount and unmount has not recurred in this acceptance report.

**Acceptance status:** `CPSTATUS` multi-line listing **PASS**, `CPDMNT` **PASS**, `CPRESET` **PASS**, CP/M access to mounted media after reset **PASS**, `CPDUMNT` **PASS**. Earlier simultaneous Web Media Manager and HP `CPSTATUS` consistency **PASS**. Note mount/unmount always update **desired** NVS mapping first; active CP/M mapping is changed only on `CPRESET`/Apply-Reset. User did not explicitly report a *second* `CPRESET` and CP/M access-failure verification following unmount; this optional final regression remains unconfirmed, not a failure.

**Diagnosis retrospective:** The source-level stack-overflow explanation remains highly consistent: prior NVS verification allocated two 16-drive tables inside a 3,584-byte FreeRTOS keyboard worker; per-entry verifier removed them without growing the task stack, and subsequent hardware operation succeeded. The ESP32 reset reason during the earlier crash was not captured, so label stack overflow a well-supported inference, not a directly measured panic. ESP32 memory low-water concern from prior modem stress (`min_free_8bit=628` B) remains an independent open observation and was not retested in this report.

**Branches:** coordinated `feature/cpm85-drive-control` on ESP32 `timob0/EBTKS-Z80-Emulator`, Teensy `timob0/ebtks-alt-firmware`, and HP LOADBIN `timob0/HP85-Option-Roms`. **Do not claim merged**; user has confirmed hardware acceptance but has not requested integration into `main` in this turn. No additional firmware changes or flashes required for the validated workflow.

## 2026-10-22: CPDMNT and CPDUMNT trigger ESP32/Z80 restart; stack-overflow fix

**Hardware feedback on CPM85 v0.03f:** Both HP BASIC `CPDMNT` and `CPDUMNT` cause the Z80 to reboot and then the HP reports `CPM85 DRIVE TIMEOUT`. HP `CPSTATUS` previously worked. User suspected accidental `CPRESET`; source verification shows opcodes 2 (mount) and 3 (unmount) do **not** set `RUN=false`. The *only* deliberate HP-driven Z80 reset is opcode 4 in `Cpm85DriveControl::execute()`.

**High-confidence diagnosis from ESP32 source (reset reason not yet measured):** The HP command is dispatched within the `CPM85Keys` FreeRTOS task, which is created with a **3,584-byte stack** (`ebtks_cpm85_keyboard.h`). Both mount and unmount call `cpmStoreSaveDesired()`, which previously placed `CpmMountEntry expected[16]` and `CpmMountEntry check[16]` on that same stack. With `CpmMountEntry` containing a 96-byte path plus flags, those arrays consume roughly **3,168 bytes before function-call frames**. This readily explains a keyboard task stack overflow and subsequent ESP32 reset, causing both Z80 reboot and HP reply timeout. The Web Media Manager invokes the same persistence operation from a different task and has been successfully unmounting images, consistent with task-specific stack exhaustion. **Crash signature remains to be confirmed on hardware using uptime/reset reason.**

**Targeted source correction:** In ESP32 `feature/cpm85-drive-control`, `cpm_mount_store.h` now verifies NVS entries one drive at a time with a small canonical string buffer and a single `Preferences` reader, using `cpmStoreVerifyPrefs(cpmDesiredMounts)`. Removed both 16-entry local arrays from `cpmStoreSaveDesired`, and the 16-entry array from `cpmStoreVerifyDesired` (used by `CPRESET` on the same task). Retained existing NVS namespace/format, write verification, Web routes, and reset semantics. Added a regression in `tests/check_cpm85_bridge.py` to prohibit those stack arrays. **Do not increase the HP keyboard stack and reduce the already tight 8BIT memory reserve as the first fix.** No source modifications are necessary on Teensy or HP LOADBIN v0.03f.

**Hardware acceptance still pending:** Build and flash only ESP32 `esp32dev-tft` from the current `feature/cpm85-drive-control` tip, after CI passes; keep HP CPM85 v0.03f and existing Teensy. Check `/api/system/health` uptime/reset reason before starting. HP `CPVER`, `CPSTATUS`; then `CPDMNT "G:","TP301A.DSK"` and check G: **Next reset** in Web Media Manager (must **not** reboot). Then `CPDUMNT "G:"` and check G: pending unmount; only after those pass, test `CPRESET` at the CP/M prompt with no I/O. If another reboot occurs, inspect `/api/system/health` `reset_reason` immediately after boot and the ESP serial panic/stack-canary log. Hardware success has **not** yet been established.

## 2026-10-21: CPDMNT hang root cause and HP-only v0.03f correction

**New physical failure on CPM85 v0.03e:** `CPDMNT "G:","TP301A.DSK"` hung HP BASIC and made no visible desired G: mount in Media Manager. Physical RESET aborted the HP command; `CPSTATUS` still displayed the previous list. **Do not claim HP mount/unmount/reset validated.**

**Root cause established from HP-85 Assembler manual (CPU register boundaries, section 3-4) and source:** The v0.03e runtime used `POMD R40,-R12`, `POMD R42,-R12`, `POMD R44,-R12` and `POMD R46,-R12` as though each popped two bytes from HP BASIC's parsed string parameters. In fact R40 spans an **8-byte** multi-byte register group, R42 spans **6**, R44 spans **4**, and R46 spans **2** bytes; these instructions consumed far too much of R12, corrupting the runtime parameter stack before completing the command. Further `LDM R40,R44` and `LDM R42,R46` moves overlapped the R40–R47 register group, creating additional corruption. The same issue existed in `CPDUMNT`.

**Fixed in HP85-Option-Roms `feature/cpm85-drive-control`, now CPM85 v0.03f:** Pop each BASIC string address and length into an exact **2-byte** register pair within R20–R37 (octal): image R26/R24, drive R30/R32, in last-argument-first stack order. Stream drive using R30/R32; move image with `LDM R30,R26` and `LDM R32,R24` (2 bytes each), then stream image. `CPWRDR` and `CPWRIM` now use R30/R32 so pointer increments, length checks, and decrements all operate in 2-byte groups. Expanded static tests reject high-register `POMD` use and verify exact operand order. **GitHub CI run 39655664676 passed both static ABI checks and CAPASM/CAPLIF assembly/packaging.** Hardware testing v0.03f is still required.

**Safe next test:** Pull/build/reload **only HP CPM85.bin/.dat** v0.03f, verify `CPVER`, `CPSTATUS`; try `CPDMNT "G:","TP301A.DSK"` (pre-existing spare image, drive G unmounted), then `CPSTATUS` and Web Media Manager to confirm desired/pending G: only. If successful, `CPDUMNT "G:"` and verify pending mount removed, before any `CPRESET`. If a command hangs, use RESET and report result. Preserve known-working ESP32/Teensy builds; no need to flash them for this HP-only fix. After passing these, separately test `CPRESET` at the CP/M prompt with no disk I/O.

## 2026-10-21 successful combined BBS and cross-interface unmount test

User successfully ran HP `CPSTATUS` repeatedly, dialed a live BBS through CP/M MODEM.COM, logged off, exited MODEM, and retained functional Web UI and Media Manager throughout. Unmount in Web Manager was visible from HP `CPSTATUS` (desired NVS assignment; active only after reset). Previous HTTP/Media Manager failure did not recur. **No leak proved, but minimum free byte-addressable heap historically fell to only 628 B** with current 19,160 B free / 17,396 B largest. See `ESP32_MEMORY_AUDIT.md`. HP-side `CPDMNT`, `CPDUMNT`, `CPRESET` still require dedicated acceptance. Firmware unchanged.

**ESP32 platform regression accepted (2026-10-21):** After a clean ESP32 TFT feature-branch rebuild, modem dialing worked and the Web UI remained responsive; the TFT and Web ANSI/CP437 terminal displays matched. Both memory diagnostic routes now return separate correct responses. The historical 8BIT minimum nonetheless dropped to 2,888 B; see `ESP32_MEMORY_AUDIT.md`. This validates the terminal/modem/platform side under the tested workload **not** HP `CPSTATUS` v0.03e newline output or disk-control commands; those physical tests remain pending. Do not merge drive-control branch before acceptance.

**Experimental implementation, 21 October 2026.** Not hardware tested until user confirms the three coordinated builds. The existing, accepted ESP32 `main`, Teensy keyboard branch and HP CPM85 v0.02a remain available for rollback; do not merge this branch before physical acceptance.

## 2026-10-21 physical CPSTATUS success, Web Media Manager JSON failure

**Confirmed on physical HP-85:** HP LOADBIN CPM85 v0.03e now prints `CPSTATUS` drive assignment listing correctly with separate CRT lines (A: `A.dsk`, B: `cpm_1.lif`, etc.). HP command completes without hanging. This closes the HP status rendering regression; **it does not yet validate `CPDMNT`, `CPDUMNT` or `CPRESET`**.

**New problem under investigation:** Web `/cpmmedia` shows empty Drive assignments and Available SD-card media tables, and Safari reports `The string did not match the expected pattern.` `CPSTATUS` works from the same live ESP32, so do not assume the NVS assignments have disappeared. Browser's `refreshAll()` uses `fetch('/api/cpm/media')`, `await r.json()`, `render()`. A response/body parsing exception can leave both tables blank. Backend `cpmStoreBuildMediaStatusJson(String &out)` allocates ArduinoJson documents for 16 drives and Teensy directory, then serializes to String; errors from allocator/serialization are not checked before returning HTTP 200. This makes low-memory or truncated JSON a plausible cause, NOT YET proven. The recently observed historical `min_free_8bit=2888` B strengthens the need to inspect actual response.

**Next safe read-only diagnosis:** Open `http://<ESP32-IP>/api/cpm/media` directly or use `curl` to capture HTTP status, actual response body and JSON validity on the laptop. Also capture `/api/system/memory` at the same time. If raw API JSON is good, debug client JS; if invalid/empty, inspect the media JSON serialization and network response allocations. Do not modify drive mapping, reset CP/M, or flash an speculative fix until response is known. Existing ESP32/Teensy unchanged.

**Follow-up hardware result:** The user opened `/api/cpm/media` directly and obtained a complete JSON response: `ok:true`, `store:"esp32-nvs"`, `pending:false`, `media_ok:true`, 16 drives, six active mount assignments A–F matching desired, and the disk-image listing. A: `A.dsk`, B: `cpm_1.lif`, C: `cpm_2_MP.lif`, D: `cpm_3_f80.lif`, E: `NEW.DSK`, F: `MODEM8M.DSK`. Listing also includes macOS `._` metadata files; these are ignored client-side but are still included in API JSON. HTTP retained responsiveness. Next memory snapshot at uptime 340,816 ms showed free INTERNAL 67,108 B, free 8BIT 27,932 B, largest 8BIT 26,612 B, historic min 8BIT unchanged at 3,292 B. Relative to prior pools (67,300 / 28,076 / 26,612), the permanent delta is negligible. This isolated request **does not reproduce** Web outage/leak. Browser page blank/Safari generic pattern error remains unproven; next compare same-origin `/cpmmedia` via IP with browser developer network/console, especially whether browser fetch body is intermittently malformed; do not assume backend corruption or reset mounts.

## 2026-10-21 follow-up: CPSTATUS formatting v0.03e

**Physical result:** After reflashing Teensy, ESP32 and CPM85, the `CPSTATUS` mailbox finally returned the live active image table on the HP-85. In v0.03b it appeared on one line with up-arrow glyphs instead of newlines. In v0.03c the HP command returned cleanly but printed nothing. Both confirmed that the current firmware/hardware could communicate and the remaining work was the HP BASIC display routine.

**Source-level causes found:** The assembler `D` suffix selects **decimal**: LF is `10D` / octal `12`, not `12D`; CR is `13D` / octal `15`, not `15D`. v0.03c also erroneously used `LDM R50,=3,0` claiming 768 bytes (in fact 3), and `STMD R50,...` to save an eight-byte register group into a two-byte buffer. The HP-85 manual specifies that multi-byte operations extend through the next register boundary: `R50` spans `R50-R57` (8 bytes) while `R56` spans `R56-R57` (2 bytes).

**Correction:** CPM85 **v0.03e** on HP85-Option-Roms `feature/cpm85-drive-control`: restore the v0.03b byte-by-byte printable ASCII output via HP `OUTCHR`; ignore incoming CR `13D`; on LF `10D`, call HP `OUTSTR` with a single literal ASCII space `BYT 40` to perform native CRT newline. Use `R56-R57` as a 768-byte read limit (little-endian `=0,3`) and exactly two-byte `STMD/LDMD R56` save around CRT output. The HP mailbox waiting loop also uses 16-bit `R56`. Static regression and CAPASM/CAPLIF CI passed; **hardware validation pending**. Only the HP LOADBIN needs rebuilding; do **not** reflash Teensy or ESP32 for this formatting fix. Test only `CPVER` and `CPSTATUS` first and report actual output before mount commands.

## 2026-10-21 hardware finding and v0.03b correction

**Physical test:** HP BASIC `CPSTATUS` appeared to hang while Z80 CP/M and the ESP32 Web UI kept working. HP RESET recovered the BASIC prompt and the existing `CPM` keyboard attachment remained usable. Do not test mount/unmount/reset commands until the corrected read-only status command is confirmed.

**Root cause confirmed in source:** The EBTKS `ioReadFuncPtr_t` callback returns a **bool claiming the read cycle**, and must assign the response byte to global `readData`. The new firmware incorrectly returned the status/reply byte as if the callback signature were `uint8_t (*)()`. This never placed the correct byte on the bus, and returning `false` for a status of 0 left the cycle unclaimed. The proven write-only keyboard path was unaffected.

**Fix:** On the Teensy `feature/cpm85-drive-control` branch, `onReadCpm85DriveState` and `onReadCpm85DriveReply` now assign `readData` and return true unconditionally, including when the byte is zero. Added `test/check_cpm85_drive_read_abi.py` to catch regression. On the HP85 ROM repository's same feature branch, **CPM85 v0.03b** caps reply reads at 768 bytes and reduces the bounded mailbox timeout. ESP32 code does not change for this fix.

**Retest:** Build and flash the *updated Teensy firmware*, then assemble/load CPM85 v0.03b on HP-85. **No ESP32 reflash needed** if already running `feature/cpm85-drive-control`. Try only `CPVER` and `CPSTATUS` first. After they return normally and the displayed mappings match Media Manager, proceed to the earlier mount/unmount/reset tests.


## HP BASIC API

```basic
LOADBIN "CPM85"
CPSTATUS
CPDMNT "B:","WORK.DSK"
CPDMNT "C:","PASCAL.DSK"
CPDUMNT "D:"
CPSTATUS
CPRESET
```

- `CPM` continues to attach the physical keyboard to CP/M; K8 continues to detach without resetting CP/M.
- `CPSTATUS` lists active assignments and any different desired assignments in a compact HP-85 CRT format. Filenames in the status display are abbreviated to 14 characters; full names remain in Media Manager.
- `CPDMNT` and `CPDUMNT` **change only the desired assignment**, persist it in ESP32 `cpmmount` NVS and print `CPRESET REQUIRED`. They never reset CP/M.
- Image argument is a *basename* under `/cpm/disks/`, not the current HP `SDCD` directory. Supported currently: native 8 MiB `.dsk` (R/W) and HP 82900 `.lif` (R/O); validation uses existing Media Manager.
- `CPRESET` checks persistent NVS and requests the same **Z80-only** reset as Media Manager's Apply control. HP BASIC, Teensy, Wi-Fi and the full ESP32 firmware stay alive. This intentionally works even when no mount is pending.

**Safety:** Call `CPRESET` *only with CP/M at an idle command prompt*, after exiting applications and allowing pending disk I/O to complete. The legacy CP/M BIOS caches disk-sector writes; there is no proven automatic “safe to reset” detection. Do not use `CPRESET` while a CP/M program is saving data. Make a backup of writable image files before first testing.

`CPIMAGES` was intentionally omitted; `SDCAT` already lists `/cpm/disks`.

## Coordinated source branches

| Processor | Repository | Branch |
| --- | --- | --- |
| HP-85 LOADBIN | `timob0/HP85-Option-Roms` | `feature/cpm85-drive-control` |
| Teensy 4.1 | `timob0/ebtks-alt-firmware` | `feature/cpm85-drive-control` |
| ESP32 ST7796 | `timob0/EBTKS-Z80-Emulator` | `feature/cpm85-drive-control` |

Build or install in order: Teensy, ESP32, CPM85. The HP keyboard bridge and all previously validated TFT, Telnet, Web, modem and SPI functionality must remain working. The HP module still assembles to `build/CPM85.dat` through CAPASM 1.0.1 and CAPLIF. ESP32's physical TFT variant is `pio run -e esp32dev-tft`. Teensy variant is `pio run -e teensy41`. Before physical flash, confirm all three GitHub CI runs pass.

## Control-plane implementation

The HP-85 LOADBIN issues short commands over four EBTKS I/O addresses (octal): `0177743` argument byte write, `0177744` opcode write (0=clear/abort, 1=status, 2=mount, 3=unmount, 4=reset), `0177745` status byte read (0 idle, 1 busy, 2 success, 3 failure), and `0177746` reply byte read (NUL releases mailbox). Original keyboard `0177741` and `0177742` are unchanged.

Teensy ISR callbacks only copy/read bounded bytes in RAM. The existing ESP32-initiated UART `cpm_keys` poll carries one short command and a reply acknowledgement, multiplexed with the existing physical keyboard events. ESP32 command handling uses the same `cpmStore*` NVS-backed mount APIs as Web Media Manager; command sequence numbers prevent repeated reset on retransmission. No HTTP/Wi-Fi request is issued from HP BASIC and no disk-image bytes are transferred.

The ESP32 reply buffer is allocated only on first drive-control request, rather than consuming permanently reserved TFT `.dram0.bss`. If it cannot be allocated, the command reports a low-memory error. HP-side timeouts clear the Teensy mailbox; if an operation times out while persistence is in progress, use `CPSTATUS` to inspect actual assignment state before retrying.

## Suggested physical acceptance tests

1. **Safe read-only smoke test:** Back up `/cpm/disks` images, load `CPM85`, run `CPVER`, `CPSTATUS`, and inspect reported A: and any existing mounts against the Web Media Manager. Confirm HP BASIC returns normally and CP/M remains active.
2. With a known, supported spare image (e.g. `WORK.DSK`) present, run `CPDMNT "B:","WORK.DSK"`. It should print that the mount is pending and ask for `CPRESET`. `CPSTATUS` should show B: old→new. CP/M B: should **not** change yet.
3. Test `CPDUMNT "B:"`, then two `CPDMNT` operations on different drives without a reset. Confirm persistent pending changes and no unexpected reset.
4. Deliberately try a non-existent image and an invalid drive; verify a clear error and no change to active or pending NVS state. Use `CPSTATUS` again after any timeout.
5. Exit CP/M programs to the command prompt, then `CPRESET` once. Confirm Z80 restarts with pending mappings; HP BASIC remains alive, Web/TFT/Telnet remain usable and no ESP32 reset occurs.
6. Recheck `CPM` attach, K8 detach, CP/M `DIR`, ordinary R/W file operations on a backed-up writable disk, `/cpmmedia` and updater accessibility.

No part of this milestone edits a CP/M directory directly. File-level import/export via HP BASIC belongs to a separate later BDOS-coordinated stage.
