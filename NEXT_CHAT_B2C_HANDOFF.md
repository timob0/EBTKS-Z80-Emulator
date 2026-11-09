# SIMON85 / CP/M integration — START HERE for next chat (2026-11-10)

> **Status: B2b EXPORT and B2c IMPORT passed on real HP-85 hardware.
> Next immediate task: B2c cleanup, then B2d HP-85 BASIC transfer commands.**
> This section supersedes historical "B2c not yet tested", "do not flash
> until diagnostics complete", and other earlier-in-time instructions
> further down this chronological handoff. Keep that history for diagnosis.

## Exact working baseline and branch policy

- **ESP32** repository `timob0/EBTKS-Z80-Emulator`, branch
  `feature/cpm-file-transfer-b2c`. Physically flashed and tested image:
  **`5cc28cca44adb9e5a4305799e603eccfb4fd6e2b`**, TFT target
  `esp32dev-tft`, Z80 CPU task stack **8192 bytes**. The branch HEAD
  may be newer because documentation was committed after hardware validation;
  do not mistake documentation-only HEAD for the tested firmware binary.
- **Teensy 4.1** repository `timob0/ebtks-alt-firmware`, branch
  `feature/cpm-file-transfer-b2c`, last recorded firmware commit
  **`5d94a0af752ef4be208939b22d2467576bbf71ee`**. No Teensy
  change was needed for the final B2c 8 KiB ESP32 fix.
- **HP Option ROM** `CPM85 v0.04d`: previously accepted,
  `CPREBOOT`/`CPRESET` implemented. No B2c-specific ROM change;
  B2d is the planned next ROM update. Do not modify SIMON85/PRINTER85
  or merge unrelated ROM branches as part of B2c cleanup.
- **`main` untouched** for the active B2c implementation. Do not merge
  until user agrees and full native/PlatformIO/hardware regressions pass.
- **Flashing constraint:** ESP32 OTA firmware update is unreliable for
  unresolved reasons. User has to flash via serial -> Teensy -> ESP Prog;
  this is slow. Minimize flashing cycles: do source/ELF analysis and
  native builds first; consolidate changes into one deliberate final
  cleaned build. Preserve current good firmware and backed-up SD images.
- Do not claim CI runs or local compilation/tests have passed unless
  actually observed in their respective outputs. The user did report
  successful TFT firmware build and static DRAM check.

## B2b/B2c physical acceptance (DO NOT re-test needlessly)

- B2b exports from **native CP/M BDOS** to Teensy SD
  `/cpm/transfers/`, with safe-point/CCP coordination, 128-byte
  records and CRC16-CCITT-FALSE verification. Export
  `A:STAT.COM` produced `/cpm/transfers/A_STAT.COM`,
  **42 records x 128 = 5376 bytes, CRC 54346**.
- B2c read-only source preflight passed with 42 records / CRC 54346
  and clean Teensy source-session release. B2c import from Teensy SD
  `A:STAT.COM` to *new* CP/M `A:IMPTST.COM` initially triggered
  `ESP_RST_PANIC` (`reset_reason=4`). The RTC trace survived
  and located the pre-panic stage at BDOS `MAKE` (function 22),
  record 0, historical stack watermark 1472 bytes free on a
  **4096-byte CPU-task stack**.
- Source/ELF analysis on the original compiled 4 KiB firmware identified
  at least **3216 bytes of simultaneously active function frames**
  on the MAKE -> Z80 -> virtual disk write/JSON path, excluding deeper
  library/handler calls. `CpmSdImport::execute` = 144 B,
  `CPUTask` = 752 B, `CpmFileWriteService::execute` = 768 B,
  `cpmMountedDiskWrite` = 928 B (includes its **685-byte**
  Base64 buffer); the 1008-byte directory-listing execute frame is
  **not** on the B2c MAKE path.
  The issue is deep **ESP32 host C++ call-stack usage**, not an
  accumulating recursive `z80BridgeRun()`: the normal emulator run
  returned to CPUTask at the safe point before the B2c BDOS trampoline
  started. PIP file copy within A: and earlier Multiplan installer worked
  under ordinary CP/M, so ordinary virtual-disk writes are proven.
- Increasing Z80 CPU-task allocation to **8192 bytes** made the native
  B2c import pass with **`state=passed`, 42 records,
  5376 bytes, CRC16=54346, no errors, no partial-warning,
  no forced Z80 reset and no ESP32 panic**. BDOS independent readback
  passed. RTC watermark after import: **3840 bytes unused**, meaning
  **4352 bytes peak used** (256 bytes more than entire original 4 KiB
  allocation). This strongly supports stack exhaustion as the root
  cause, although no original panic backtrace was recovered.
- User independently verified on physical CP/M A: drive:
  `IMPTST.COM` visible and `STAT` says **42 records, 8 KiB
  allocated** (normal CP/M allocation rounding; content is 5376 B).
  **B2c FUNCTIONALLY ACCEPTED.** Do not request another import merely
  to confirm this result.
- Hardware health with accepted build: no watchdog/panic/brownout,
  no SPI disk read failures; free heap about 70 KiB, min free 8-bit
  heap **25612 B**, largest current 8-bit block **28660 B**,
  TFT/Web terminal running. TFT static DRAM `124512` bytes
  (`.data=28440`, `.bss=96072`), passed local guard
  `124544` with 32-byte margin. Runtime 8 KiB task allocation
  costs about 4096 B extra internal heap compared with former 4 KiB.
- All current source/export/import functions retain intentionally
  **bounded single-file size**: B2a highest record index 63, B2b 64
  records, B2c 63 records. This is NOT yet arbitrary-length
  CP/M file transfer. ZIP/batch must not assume these limits removed.

## NEXT: B2c cleanup (first job in next chat)

1. Read `B2C_DIAGNOSTIC_CLEANUP_LEDGER.md` and review every entry.
   **Keep the tested 8192-byte CPU task allocation** and useful
   source-error/session reporting. Do not accidentally revert it to
   4096 or untested 6144 bytes.
2. Remove temporary RTC trace/`CpmImportDiag`, per-stage BDOS/disk
   instrumentation, diagnostic-only endpoint `/api/cpm/service/diag`,
   and associated static assertions/tests **only after identifying the
   correct final design**. Review read-only source preflight,
   source-session release/recover and source-error codes individually
   for retention; do not remove useful production safeguards merely
   because they were developed during debugging.
3. Check related files: `cpm_import_diag.h`,
   `ESP32_Z80_Emulator.ino`, `cpu_redcode.h`,
   `cpm_file_write_service.h`, `cpm_safe_point.h`,
   `ebtks_cpm_sd_import.h`, `ebtks_web.h`,
   `tests/check_b2c_diagnostics.py`, CI and docs. Teensy recovery/
   release code and tests must be retained or purposefully adjusted.
4. Run native Z80 regression suite, Teensy native tests, ESP32
   `esp32dev` and `esp32dev-tft` PlatformIO builds, plus TFT static
   DRAM and runtime memory considerations. Plan **one** controlled
   regression flash of the cleaned firmware rather than many diagnostic
   flashes. User must approve any merge to `main`.
5. The historical notes below document previous investigations. Do
   not follow their old instructions to repeat the successful import,
   use the 6 KiB trial, or request a diagnostic flash.

## NEXT AFTER CLEANUP: B2d — HP-85 BASIC single-file commands

- Implement CPM85 Option ROM BASIC keywords
  `CPEXPORT "A:STAT.COM"` and
  `CPIMPORT "A:STAT.COM","A:COPY.COM"`
  (provisional exact spelling; check HP BASIC shortest-unique-keyword
  collisions like earlier `PLOT`/`PLOTTER` incident).
- HP-85 ROM -> EBTKS Teensy -> ESP32 **control/status mailbox**;
  reuse tested Teensy SD and ESP32 native-BDOS data path. HP-85 does
  not shuttle record payloads or reimplement CP/M filesystem operations.
- **Blocking** by user preference. Tell operator to leave CP/M idle at
  native CCP `A>` prompt before starting. Proposed start wait ~10 s,
  then on HP native CRT prompt:
  `CP/M NOT IDLE - RETURN TO A> PROMPT; C=CONTINUE A=ABORT`.
  Continue retries another bounded window; Abort cancels only a
  **not-yet-started** transfer and returns to HP BASIC with no
  unexpectedly queued operation. If active BDOS has begun, **never
  abort mid-write**; continue waiting and report result/partial warning.
- ESP32 `Gate::request()` **currently refuses immediately if
  mode != CCP_INPUT** (application busy); accepted
  `WAIT_EMPTY_LINE` means a verified safe point has not yet occurred.
  `Gate::cancelPending()` atomically cancels only WAIT_EMPTY_LINE,
  but is not yet exposed through B2d mailbox. Avoid duplicate requests
  on Continue or races with RUNNING. Handle start timeout differently
  from an operation stalled after start; define a safe bounded
  watchdog/status reporting strategy.
- Report success/failure, records, bytes, CRC and actionable errors;
  import still means CREATE-NEW, no overwrite and no silent cleanup
  of potentially partial CP/M files.
- Test native HP CRT/keyboard interaction without compromising existing
  `CPM` session, K8 exit, `CPREBOOT`, or independent Z80 operation.
- Do B2d on a new feature branch after B2c is regression-accepted;
  merge only with explicit approval.

## FOLLOW-ON ROADMAP: bulk files / ZIP

- **B2e**: remove 63/64-record ceiling (multi-extent CP/M 2.2 files,
  EOF, 128-byte record semantics, extended counters/CRC, progress and
  memory-bounded transfers); then wildcard/batch copy. This is a
  prerequisite, not a minor afterthought: many CP/M applications are
  bigger than 8 KiB.
- **B2f**: `CPUNZIP "GAMES.ZIP","B:"` (provisional) —
  Teensy streams ZIP entries from SD; ESP32 repeats native BDOS
  create-new/write/close/readback for each file. ZIPs typically contain
  individual 8.3 CP/M files, **not uniform original disk images**.
  No raw `.DSK` surgery. Standard stored/DEFLATE entries if
  streaming library and resources permit; validate CRC32, size,
  FAT files and mapping rules.
- **B2g**: `CPZIP "A:*.COM","UTILS.ZIP"` (provisional) —
  native CP/M wildcard enumeration/export, Teensy builds new archive
  transactionally via temp file then publishes complete ZIP.
- Protect CP/M flat 8.3 naming, no silent overwrites, no `../`
  paths, reject ambiguous long names/duplicates/unsafe archives,
  encryption, ZIP64/multivolume and zip bombs unless explicitly
  supported. Plan preflight and capacity check before writes;
  abort only before active BDOS; on failure disclose completed files
  and possible partial current file. Archive-level atomicity on CP/M
  is not guaranteed. Non-128-aligned ZIP payloads need explicit
  CP/M EOF/padding policy; native CP/M cannot preserve exact trailing
  byte count without metadata. Consider using a RAM-bounded
  decompressor on Teensy rather than buffering the ZIP.

## SEPARATE FUTURE HARDWARE: Tang Nano 9K

- User ordered a **Sipeed Tang Nano 9K plus matched 5-inch 40-pin
  parallel RGB LCD** as a bundle. The target is eventually installing
  the display in the HP-85 printer hatch; a 7-inch panel is too large.
  Check actual module dimensions, backlight, viewing angle and
  power/pinout when received.
- **First FPGA milestone:** replace sluggish SPI ST7796 TFT terminal
  with hardware-refresh 80-column RGB text display, CP437,
  ANSI/VT100 color, efficient hardware scrolling; keep existing
  Teensy storage and optional ESP32 TFT builds for compatibility.
  ESP32 sends chars/attributes via a designed interface (e.g. SPI),
  not full pixel redraws; FPGA handles scanout. Board/display timing
  must be established with Sipeed example on bench first.
- **Architectural preference:** add a small software-programmable
  RISC-V or comparable softcore to handle terminal control, future
  FAT32 file service and graphics commands; keep time-critical RGB
  timing/video refresh in HDL. Availability of logic/BRAM/PSRAM,
  framebuffers and simultaneous softcore/peripheral fit must be
  demonstrated by synthesis; no assumption that all features will
  fit at once. Starting with CPU core does **not** commit us to
  building a third Unix-like computer.
- **Optional Tang TF storage**: user prefers **FAT32 files** over raw
  disk slots to copy `.dsk` images with a card reader and support
  later applications. Potentially mount 8 MiB CP/M drives from
  Tang TF while retaining Teensy SD service for LIF disks,
  traditional EBTKS filesystem and full backwards compatibility.
  Extend mount routing by **backend** (Teensy vs Tang) independently
  of **media format** (`ebtks8m` vs `hp82900_lif`).
  One filesystem owner should coordinate FAT32 access to prevent
  concurrent corruption; prefer streaming/binary sector commands,
  no duplicate CP/M directory/allocation implementation.
- **Potential extensions, not committed deliverables:**
  GSX/VDI graphics coprocessor for CP/M, HP-GL interpreter/virtual
  HP-IB plotter accessible from HP-85 `PLOTTER IS`, common drawing
  primitives, SD file server, optional independent software
  environment on the softcore. Share graphics raster backend between
  GSX and HP-GL; keep video refresh responsive while softcore works.
- Keep Tang architecture separate from immediate B2c/B2d tasks
  until hardware arrives.

## Start-of-next-chat instruction

Read this **topmost current-status section** and
`B2C_DIAGNOSTIC_CLEANUP_LEDGER.md` first. Begin with the B2c
diagnostic cleanup review, preserving the working 8 KiB stack and
the B2b/B2c transport behavior. Do not propose another speculative
flash or re-ask for already confirmed hardware outputs. After cleanup
and regression, proceed to B2d blocking HP-85 BASIC file transfers.

---

## Future B2e: HP-85 bulk and ZIP CP/M transfers (2026-11-10 design discussion)

After B2d HP-85 BASIC single-file `CPEXPORT`/`CPIMPORT` is
functional and the B2c diagnostics cleaned, add archive/bulk
transfers. User often obtains historical CP/M packages in ZIP
archives containing individual CP/M files, **not raw CP/M disk
images**. Teensy should own ZIP archive processing; ESP32 still
uses native CP/M BDOS for each file on mounted drives, so no
filesystem-format-specific disk image editing.

**Critical prerequisite found in current source**:
`CpmFileWriteService::kMaxRecords=63`;
`Cpm85SdImport::kMaxRecords=63`; B2b
`Cpm85SdExport::kMaxRecords=64`; B2a
`CpmFileReadService::kMaxRecord=63`. Thus B2b/B2c are limited to
~8 KiB single files. Before ZIP/bulk, extend CP/M files across
multiple extents, sequential record index/EOF, CRC and counted
reads/writes (no assumption ZIP entries fit 63 records); add
longer-transfer watchdog/progress/cancellation semantics.

**Tentative BASIC syntax**, subject to ROM keyword compatibility:
`CPUNZIP "GAMES.ZIP","B:"` and
`CPZIP "A:*.COM","UTILS.ZIP"`. Preserve simple
`CPEXPORT` and `CPIMPORT` syntax; avoid interpreting a
regular .ZIP CP/M file as an archive implicitly. A useful first
intermediate milestone is multi-file wildcard transfer without ZIP.

For ZIP import: Teensy reads a FAT ZIP on SD, validates central
directory and entry metadata, streams stored/DEFLATE contents
into fixed small buffers; ESP32 writes a sequence of CP/M
files with BDOS MAKE/WRITE/CLOSE and independent readback, not
raw sector edits. For ZIP export: ESP32 iterates matching CP/M
files (wildcards, current user area), streams 128-byte records
to Teensy; Teensy writes a temporary ZIP with per-entry CRC32
and atomically publishes only when complete.

First-version compatibility/safety rules:
- CP/M has flat 8.3 names; reject or explicitly map unsupported ZIP
  paths/long names, nested directories, duplicate/colliding names,
  unsafe `../` paths, unsupported file attributes; no silent overwrites
  of existing CP/M files or destination ZIPs.
- Only standard ZIP stored (method 0) and DEFLATE (method 8) initially,
  if a RAM-bounded streaming library is available; reject encrypted,
  ZIP64, multi-volume and oversized/zip-bomb entries with clear errors.
- Specify conversion of arbitrary ZIP entry lengths to 128-byte
  CP/M logical records (e.g. CP/M EOF padding), and note that ordinary
  CP/M files cannot preserve arbitrary exact byte lengths for
  later re-export without side metadata.
- Perform archive preflight: filenames, counts, free CP/M allocation,
  conflicts, maximum expanded bytes, optional dry-run list.
- Commands can remain blocking, displaying file progress; wait for
  CCP idle before starting, allow pre-start abort/continue,
  **never interrupt active BDOS**. A multi-file failure may leave
  completed files and a partial current file; report exactly which.
  Do not promise full archive atomicity on CP/M disk.

Suggested order: (1) B2c cleanup and B2d single-file ROM UI;
(2) lift 63/64-record constraint and verify larger files;
(3) native CP/M wildcard/batch transfer; (4) streaming CPUNZIP;
(5) streaming CPZIP. Keep separate from optional Tang Nano 9K
FAT32 storage and display work.

No executable firmware changes from this proposal.

---

## 2026-11-10: B2d HP-85 blocking BASIC commands — proposed interaction

User accepted B2d Option ROM BASIC entry points `CPEXPORT` and
`CPIMPORT` after B2c cleanup, and **prefers blocking commands**.
The commands should clearly tell the HP-85 operator to leave native CP/M
idle at the CCP prompt (e.g. `A>`) before issuing transfers.

Agreed interaction direction:
- Start a bounded wait (initial proposal 10 s, configurable during design).
- If native CP/M is busy or safe point unavailable at timeout, show
  `CP/M NOT IDLE - RETURN TO A> PROMPT. CONTINUE WAIT (C) / ABORT (A)?`
  through HP-85 native CRT/keyboard; Continue retries another bounded
  interval, Abort ends the HP BASIC command with no import created.
- Once a transfer actually enters the BDOS/file-transfer phase, block
  until it completes and show result/error, count and CRC; do **not**
  offer unsafe mid-write abort. Distinguish a bounded start timeout from
  an in-progress stall/watchdog with safe error reporting.
- No duplicate operation must be queued after Continue; Abort must
  guarantee a not-yet-started operation cannot run later.
- **Current ESP32 Gate::request() refuses immediately when mode!=CCP_INPUT**
  (application still running); it does NOT queue a transfer while busy.
  `WAIT_EMPTY_LINE` is different: it is an accepted request awaiting
  the verified blank CCP return.
- `Gate::cancelPending()` already exists and atomically cancels only
  `WAIT_EMPTY_LINE`. No public or HP mailbox cancellation is exposed yet.
  The B2d control interface should reuse this safely. Races where the
  operation transitions to RUNNING must be reported as already started
  and handled by waiting for completion, not abandoned.
- Design new HP85<->Teensy<->ESP32 control/status mailbox so BASIC
  blocks cooperatively, without running CP/M BDOS in the HP-85.
  Existing export/import engine/data path is reused unchanged.

The exact ROM keyboard prompts/timeout must be reviewed against the
existing CPM85 ROM conventions before implementation. Nothing was
changed in executable firmware by recording this specification.

---

## 2026-11-10: Native CP/M confirms imported file — B2c accepted

User checked physical CP/M A: after successful B2c import:
`IMPTST.COM` present; STAT = **42 records** / **8 KiB allocated**.
42 x 128 = 5376 logical bytes, CRC16-CCITT-FALSE 54346 matched
original source and native BDOS readback. Allocation rounding to
8 KiB is expected. ESP32 8 KiB CPU-task stack build at
`5cc28cca44adb9e5a4305799e603eccfb4fd6e2b` had
measured 4352-byte peak CPU stack use and no reboot/panic.

**B2c functionally accepted on real hardware.** Preserve current
working ESP32/Teensy firmware and A.dsk backups. No further import or
flash required for B2c functional proof. Next work: review and
remove temporary instrumentation per
`B2C_DIAGNOSTIC_CLEANUP_LEDGER.md`, keep 8192-byte stack and selected
production error reporting, then native and PlatformIO build checks,
one controlled cleaned-build regression before main merge with
explicit user approval.

---

## 2026-11-10: Physical B2c SUCCESS, stack exhaustion strongly corroborated

User flashed ESP32 B2c SHA
`5cc28cca44adb9e5a4305799e603eccfb4fd6e2b` with
8,192-byte CPU-task stack. Native B2c import Teensy SD
`A:STAT.COM` -> CP/M `A:IMPTST.COM` PASSED: 42 records,
5376 bytes, expected/actual CRC16-CCITT-FALSE 54346,
`partial_cpm_file_possible=false`, no source/file errors,
no Z80-only reset, no ESP32 panic. RTC `import_complete/passed`
has `stack_unused_bytes=3840`, so peak CPU-task use is
**4352 bytes**, greater than the previous 4096-byte allocation
by 256 bytes. Strong evidence the prior MAKE reset was CPU-task
stack exhaustion, although original exception backtrace unavailable.
Heap remains healthy (minimum free8 25612, largest free8 28660),
SPI failures 0, TFT operational.

**Next**: ask user to verify `DIR IMPTST.COM` in CP/M; do not perform
another experimental import or flash. After acceptance, preserve
8 KiB stack and execute explicit diagnostic removal/retain review
in `B2C_DIAGNOSTIC_CLEANUP_LEDGER.md`. Build/tests then one final
cleanup flash, user approval required before main merge.
Teensy unchanged, HP Option ROM unchanged.

Tang Nano 9K + matched 5-inch 40-pin RGB display ordered for future
FPGA video/programmable softcore, FAT32 storage, GSX/HPGL possibilities.
This is separate from B2c immediate work.

---

## 2026-11-09: ELF full-frame analysis -> conservative 8 KiB stack candidate

User supplied complete `--all-matches` disassembly report from the unchanged
existing 4 KiB B2c ELF. `CpmSdImport::execute` = 144 bytes;
identified simultaneous MAKE -> virtual disk JSON serialization frame
sum **3216 bytes** (78.5% of 4096). This deliberately excludes the
1008-byte directory-listing execution frame and sequential alternatives.
Only 880 bytes remained for deeper library/indirect calls and interrupt
margin. `ESP_RST_PANIC` and the RTC `bdos_make` trace strongly support,
but do not mathematically prove, ESP32 CPU stack exhaustion.

The ESP32 B2c feature branch was updated from the untested 6144-byte
trial to a **8192-byte** CPU task allocation by default
(`EBTKS_Z80_CPU_TASK_STACK_BYTES`, compile-time override retained).
This costs ~4096 additional bytes of runtime heap versus the original
4 KiB task; verify runtime 8-bit heap, Wi-Fi and TFT DRAM when flashed.
No build/flash/hardware validation has been performed for this candidate.
Keep diagnostic hooks for one controlled validation, then remove/review
every listed hook in `B2C_DIAGNOSTIC_CLEANUP_LEDGER.md` **before merge**.
The user's OTA path is unreliable; avoid unnecessary serial
Teensy->ESP programming cycles.

---

## 2026-11-09: Stop flashing; source/ELF stack analysis before more experiments

User reports normal CP/M PIP A: file copy succeeded; earlier Multiplan installer
also worked. That exercises ordinary mounted-disk writes but does not give
B2c the same ESP32 host C++ stack depth. The first `z80BridgeRun`
**returns** on the CCP service breakpoint before B2c invokes another
`z80BridgeRun`; it is not runaway recursion. The nested
`CPUTask -> CpmSdImport -> CpmFileWriteService ->
CpmDirectoryService::invoke -> z80_run -> portOut -> diskWrite ->
cpmMountedDiskWrite -> ebtksStorageExchange -> UART` chain can be
considerably deeper than normal CP/M writes.

At the RTC MAKE checkpoint, the 4 KiB ESP32 CPU task had a 1472-byte
historical minimum free-stack watermark (2624 bytes max used so far).
The 512-byte virtual-disk write includes a **685-byte local Base64 buffer**
in `cpm_media.h`. This is compelling circumstantial stack-pressure
evidence, not a precise static bound or proof that stack overflow caused
`ESP_RST_PANIC`. Compiler inlining, call-frame/register spills and
library code must be inspected.

**No new flash recommended.** New offline tool
`tools/report_b2c_stack_frames.py` obtains Xtensa emitted frame sizes
from an *existing ELF* using PlatformIO's objdump; run
`python3 tools/report_b2c_stack_frames.py --selftest` and
`python3 tools/report_b2c_stack_frames.py .pio/build/esp32dev-tft/firmware.elf`.
The parser reports individual frames only, not a complete call-chain
max; follow with `-fstack-usage` compiler data/disassembly as necessary.
Only after source/ELF analysis choose one permanent fix and perform a
single validation flash. The already committed 6 KiB A/B stack build is
**not yet hardware tested** and not assumed permanent.

A comprehensive removal/retain inventory is committed at
`B2C_DIAGNOSTIC_CLEANUP_LEDGER.md`, with exact baselines
ESP32 `d58c6811`, Teensy `833ac458`; no tracing will be merged
unreviewed into `main`.

---

## 2026-11-09: RTC proves native BDOS MAKE ESP32 panic

Read-only SD preflight passed: 42 records, CRC16 54346; source session
cleanly released. The first real import generated ESP32 panic
`reset_reason=4`, `reset_panic=true`. RTC
`before_last_esp_boot.stage=bdos_make` (`status=running`,
`record=0`, CPU task stack watermark 1,472 bytes).
Therefore first native MAKE(22) had been entered but not returned;
this is NOT merely a Z80-only reboot, nor evidence of an SD source error.
The exact cause and whether MAKE modified A.dsk remain unproven.

Follow-up committed to ESP32 `feature/cpm-file-transfer-b2c`
only: instrument virtual disk read/write entry/return in RTC diagnostic
and use a reversible 6 KiB Z80 CPU task stack (baseline 4 KiB via
`-DEBTKS_Z80_CPU_TASK_STACK_BYTES=4096` for later A/B).
No CP/M BDOS algorithm or Teensy code changes in this follow-up.
Extended static test and `B2C_DIAGNOSTIC_TEST.md` instructions.

**Immediate next step:** inspect/backup physical A.dsk; don't execute
IMPTST.COM or retry import using potentially modified disk. Pull the
updated ESP32 branch and compile native checks, both PlatformIO targets
and TFT DRAM budget; DO NOT flash until build success. Teensy B2c remains
at `5d94a0af752ef4be208939b22d2467576bbf71ee`.
Once code/build and backup are verified, single controlled import on a
disposable mounted image, capture /api/cpm/service/diag right after any
reset, and inspect the deeper virtual disk/stack checkpoint. No main merge.

---

# B2c diagnostic implementation checkpoint — 2026-11-09

This checkpoint supersedes the older OTA-era notes below for the immediate
next action. **No ESP32/Teensy B2c diagnostic source has yet been compiled
locally or tested on hardware. Do not flash until the native and PlatformIO
build gates pass.** No `main` or HP Option ROM changes.

Hardware evidence leading here:
- B2b physically accepted: SD `/cpm/transfers/A_STAT.COM` exists with
  5,376 bytes / 42 CP/M records, CRC16 54346.
- First B2c import POST was accepted but appeared to restart ESP32 and lose
  its Wi-Fi association; diagnostic state was reset to `never`.
- A subsequent import failed `teensy_sd_source_failed`, zero records,
  no CP/M destination, no Z80-only forced-reset marker.
- Source exists (SDCAT 5,376 bytes); a stale Teensy read-only session
  is plausible but **unproven**. No cause of apparent ESP32 restart proven.

Implementation committed on existing feature branches:
- Teensy `feature/cpm-file-transfer-b2c`, currently
  `5d94a0af752ef4be208939b22d2467576bbf71ee`.
  New `cpm_import` `release` and source-name-checked `recover` verbs;
  native SD test extended. They only close a read-only descriptor and never
  write/delete the published source or any CP/M disk.
- ESP32 `feature/cpm-file-transfer-b2c`, new
  `ESP32_Z80_Emulator/cpm_import_diag.h`, new
  `tests/check_b2c_diagnostics.py`, and updates to safe-point gate,
  import handler, BDOS-tracing callback, web API, CPU task, and workflow.
  `POST /api/cpm/service/source?name=A%3ASTAT.COM` arms a safe-point
  read-only Teensy SD preflight (no CP/M BDOS calls); GET reports expected
  count/CRC and a classified source error. Explicit option
  `&recover=READ_ONLY_SESSION` is allowed only for manually recovering
  an orphaned session for the SAME source name.
  `GET /api/cpm/service/diag` exposes the current and prior (if retained)
  RTC_NOINIT import stage, index and stack watermark, ESP32 uptime/reset reason.
  Existing `GET /api/cpm/service/import` now exposes `source_error`
  and `last_source_error`. No new task or normal DRAM/BSS buffers.
  RTC trace retention is NOT guaranteed after EN reset or power loss.

**Next user action:** Read `B2C_DIAGNOSTIC_TEST.md`; pull both branches,
run all native checks and Teensy/ESP32 PlatformIO builds, confirm TFT
DRAM budget; only after build success, flash Teensy first, then ESP32 TFT.
At native `A>` perform **only the read-only preflight POST/GET** and
retrieve `/api/cpm/service/diag` and `/api/system/health`.
Do not repeat the writable SD->CP/M import or merge into main until
preflight/boot evidence has been reviewed.

---

# ACTIVE NEXT-CHAT CHECKPOINT — Stage B2c SD -> CP/M (2026-10-25)

### Two-browser OTA upload evidence (2026-10-27)

After the B2c OTA fix was flashed via Teensy USB bridge, both Safari and
Chrome **still lost the initial HTTP upload connection** (Chrome
`ERR_CONNECTION_RESET`); the OTA client-disconnect handler now correctly
aborts the session. Physical app1 partition is 1,310,720 bytes with
1,306,624-byte Update limit; HTTP multipart content length 1,265,431
(including form overhead) easily fits. Safari disconnected after
295,299 firmware bytes; Chrome after 108,119 firmware bytes. During uploads
free8 fell to 15,656/13,312 bytes, largest contiguous free8 to
7,412/10,228 bytes, respectively. Client disconnect was logged; no
Update.write() or finalize errors before it. **Not a flash-capacity
problem and not a browser-specific problem.** Root cause of original
network disconnect remains *unproven*; prime suspects are malloc-cap8
pressure/fragmentation and synchronous flash writes directly in the
AsyncTCP network event task. Do not claim solved by abort fix.

Inspection of pinned ESP32Async/AsyncTCP 3.3.2 found that its default
`CONFIG_ASYNC_TCP_STACK_SIZE` is **16,384 bytes**, a persistent task stack.
The upstream ESPAsyncWebServer configuration guidance recommends 4–8 KiB
for suitable callbacks, but emphasizes measuring usage, and warns against
blind queue-size increases. For a **controlled memory-headroom experiment**,
B2c feature branch now includes three additional commits:
- `bb92069`: set `-DCONFIG_ASYNC_TCP_STACK_SIZE=8192` in the shared
  `env:esp32dev` PlatformIO flags (inherited by TFT); no changes to
  async callback code or ESP/Teensy transport.
- `e88729a`: OTA traces include `async_stack_unused` via
  `uxTaskGetStackHighWaterMark(nullptr)` while inside AsyncTCP callback.
  Need verify at least 2 KiB headroom under full upload before calling
  this safe; if less, roll back/revise.
- `e6a5e82`: static tests require new config and OTA stack telemetry.

**This change has not been compiled or tested on physical hardware.**
It may improve free 8-bit heap by up to ~8 KiB but does NOT prove it
fixes the connection reset; heavy flash writes still run inside the
AsyncTCP callback. Compile/check both targets and TFT static DRAM
guard before any further USB flash. Test ordinary cold boot, HP BASIC,
Teensy storage, CP/M DIR and Web/Media first, then **one** OTA attempt,
log new `async_stack_unused`, progress, free8, largest8, and firmware
commit. Preserve prior recovery binary. No main merge or feature
branch deletion without explicit user approval.

## OTA recovery investigation (2026-10-27; not yet hardware-tested)

The user observed that ESP32 TFT OTA ceased working after B2b (last
hardware-tested B2a commit `e01211f`; current ESP32 runs
`edad55b7a5e21d6074f17fcbb4226c9f39f8f9e2`, the B2b source).
A new B2c binary measured 1,263,776 bytes; documented app slots
are 0x140000 bytes each and previous updater reserves one 0x1000-byte
sector (computed limit 0x13f000 = 1,306,624 bytes). Physical partition
table must still be read/verified on the Mac before USB flashing.

An **initial** browser OTA attempt after fresh power-cycle disconnected
while ESP stayed running; /z80debug contained only
`OTA Update Start: firmware.bin`. ESP health showed no reset/panic,
heap ~62.5 KiB, free 8-bit heap ~22.5 KiB, largest8 ~15.3 KiB,
minimum observed 8-bit free ~4.7 KiB. The 8-KiB SPI disk cache is
already in IRAM32; SPI DMA and persistent SPI disk worker are active.
Do **not** assume this initial upload failure has been explained.
Memory pressure or AsyncTCP callback stall is plausible but unproven.

Subsequent same-boot attempts logged `No Error` and
`Not Enough Space`. Inspection of pinned Arduino-ESP32 2.0.17
Updater.cpp confirmed `Update.begin` returns false without setting
an error when an update is still active, and `Update.write` sets
`UPDATE_ERROR_SPACE` after exceeding the prior session's remaining
allowance. Old updater lacked disconnect abort and continued to write
after `Update.begin` failure. This **retry-session bug is confirmed**,
but is distinct from the unproven cause of the initial B2b upload drop.

ESP32 B2c branch has OTA-only commits:
- `da8ce05`: runtime-heap request-owned OTA state (one static pointer
  because TFT static DRAM guard is tight); HTTP onDisconnect abort;
  no writes after failed begin; reject concurrent uploads; log actual
  next OTA partition, size, allowed image bytes, HTTP content length;
  progress at 64 KiB crossings, 8-bit RAM/contiguous block, final
  verification and disconnect offset.
- `78cb82f`: static regression assertions in
  `tests/check_web_ui_cleanup.py` for OTA session handling.

No main merge, no Teensy/HP modifications. Local firmware and CI
builds **not confirmed**. Operator should pull this B2c ESP32 feature
branch, run `python3 tests/check_web_ui_cleanup.py`,
`bash tests/run_z80_core_native.sh`, `pio run -e esp32dev-tft`,
`python3 tests/check_esp32_dram_budget.py` and check image size.
Use documented `ESP32_RECOVERY_USB.md` Teensy USB programming bridge
to install the B2c diagnostic firmware after **reading the actual
partition table**, retaining the present recovery binary. Do not
retry unstable browser OTA to install this correction. Test a *single*
OTA upload afterward (when stable), capture `/z80debug` and
`/api/system/memory`, compare firmware Git SHA on reboot.
No further feature testing until OTA/heap stability is established.


Read [CPM_FILE_TRANSFER_B2C_IMPORT.md](CPM_FILE_TRANSFER_B2C_IMPORT.md)
first. This is the current checkpoint. Historical `NEXT_CHAT_HANDOFF.md`,
`NEXT_CHAT_B2A_HANDOFF.md`, and `NEXT_CHAT_B2B_SD_HANDOFF.md`
are retained for recovery baselines and previous milestones.

## B2c resumed (2026-10-26)

User explicitly confirmed **all remaining B2b regressions passed**.
Do not ask for B2b retesting as a prerequisite. CPREBOOT is working,
so the Wi-Fi loss/reassociation workaround is now available.

Active feature branches **unchanged**:
ESP32 and Teensy `feature/cpm-file-transfer-b2c`;
HP ROM `feature/cpm85-esp-reboot` (v0.04d, already hardware accepted).
No new branches, no merges to `main`.

B2c code review found a definite native CI blocker in ESP32
`tests/run_z80_core_native.sh`: the g++ file-read and file-write
commands were inadvertently concatenated, with a duplicate file-read
execution. Fixed and committed in ESP32 branch `e59fab5790ddbe9c7d5f559077cdab1f8fc6f0c8`.
Teensy native `test/cpm85_sd_import_native.cpp` was expanded with
a full **42 × 128-byte** test source, verifying records, CRC and clean
completion, commit `833ac458a1845cfabee1b5ab186fd8e3a6bb8b83`.
No known transfer-path code changes were needed after current review.

**Still not verified:** current GitHub Actions run (connector exposes only
PR-triggered runs), Mac local PlatformIO builds (Teensy, ESP32 headless
and TFT), or B2c physical transfer. Do not claim those succeeded.
Do not flash B2c until local builds and native regression jobs pass.

**First B2c physical test after local build gate:** preserve/backup
writable A: .DSK, ensure `A:IMPTST.COM` does NOT exist, use native
CCP `A>` idle; POST
`/api/cpm/service/import?source=A%3ASTAT.COM&name=A%3AIMPTST.COM&confirm=CREATE_NEW`.
GET the same route to poll; expect `passed`, 42 records,
5376 bytes, CRC 54346, no partial file and no reset. Then `DIR A:`,
B2b-export `A:IMPTST.COM` to `A_IMPTST.COM` and compare against
original `A_STAT.COM` with `cmp` on Mac. Never execute the copied
COM binary as a first integrity test. Do not merge before hardware
verification.

## CPREBOOT acceptance and branch consolidation (2026-10-26)

**Completed and physically confirmed:** The user reports HP-85 `CPVER`
now displays **CPM85 0.04d** and `CPREBOOT` works as expected. This
full **ESP32 EN hardware reset** is requested through the existing HP
BASIC -> Teensy mailbox (opcode 8), handled *locally on the Teensy*
after the HP-side acknowledgement, and therefore does **not** depend
on Wi-Fi or a functioning ESP32 serial service. It restarts the
ESP32/Z80/CPM/Wi-Fi, not the HP-85 or Teensy itself. This command is
distinct from `CPRESET` (Z80-only); a separate `CPRESET` regression
report is still pending. The earlier CAPASM bare-octal `=8` mistake
was fixed to `=10`; old SD-deployed CPM85 0.05c was replaced by
0.04d after correcting deployment of `CPM85.dat`.

- **Teensy active branch:** `feature/cpm-file-transfer-b2c`. The former
  `feature/cpm85-esp-reboot` changes were already merged into this
  active branch in [Teensy PR #5](https://github.com/timob0/ebtks-alt-firmware/pull/5),
  merge commit `3fb5e73ead73ac4ec48b3fccd3746af1439b2380`.
  Comparison verifies the old reboot branch has **zero commits**
  absent from the active B2c branch. Do not merge it again.
- **ESP32 active branch:** `feature/cpm-file-transfer-b2c`; no
  CPREBOOT-specific ESP32 source changes.
- **HP ROM active branch:** `feature/cpm85-esp-reboot` contains tested
  CPM85 v0.04d. **There is no HP85-Option-Roms B2c branch.** Retain
  this tested HP branch until final user-authorized coordinated merge
  into `main`; do not invent another branch.
- **No `main` merges or branch deletions yet.** User requested that
  full B2c file-transfer testing finish first, followed by coordinated
  `main` integration and cleanup. No new branches without explicit
  permission.

## User hardware acceptance baseline

B2a native read-only CCP D382h service confirmed on physical HP-85:
`A:STAT.COM` 42 records, 5376 bytes, every CRC16 correct, EOF,
file-not-found and range rejection. CP/M DIR, HP CPDCAT, MODEM,
Multiplan, TFT/Web, Media Manager regressions successful.
B2b direct CP/M->Teensy SD export `/cpm/transfers/A_STAT.COM`
**physically PASSED**: 42 records, 5376 logical bytes, cumulative CRC16
54346 (0xD44A), SD readback and commit, only one extra A> prompt,
normal SDCAT visibility, and duplicate-destination failure with
`teensy_sd_stage_failed` and no reset. User subsequently confirmed all
remaining B2b acceptance checks passed (2026-10-26).

## New B2c work — committed, NOT YET build/CI or hardware validated

Two new B2c-only branches created from their existing B2b lines:
- `timob0/EBTKS-Z80-Emulator` branch
  `feature/cpm-file-transfer-b2c`;
  previous accepted B2b ESP32 branch `feature/cpm-file-transfer-b2a`
  was NOT changed by B2c work.
- `timob0/ebtks-alt-firmware` branch
  `feature/cpm-file-transfer-b2c`;
  previous physically accepted Teensy B2b branch
  `feature/cpm-file-transfer-b2b-sd` untouched.
- At original B2c branch creation, HP Option ROM/LOADBIN and ALL main branches
  were unchanged. **Superseded for HP by the newer CPREBOOT acceptance above;
  all `main` branches are still unchanged.**

Teensy new `include/EBTKS_CPM85_SD_Import.h` and
`include/EBTKS_ESP.h` `cpm_import` read-only UART RPC.
Requires an already published `/cpm/transfers/<drive>_<name>`,
never arbitrary SD path. `begin` scans entire 128-byte-aligned
SD source for CRC16 and rejects empty, fractional, >63 record files;
`read` provides sequential 128B Base64+CRC, `finish/status/abort`
close without deleting original source. Native SD mock + new
`test/cpm85_sd_import_native.cpp`; Teensy CI now triggers B2c branch.

ESP32 new `cpm_file_write_service.h`, `ebtks_cpm_sd_import.h`,
safe-point `IMPORT_SD`, CPU worker branch, and new
`/api/cpm/service/import` POST and GET. POST requires two strict
A:–P: 8.3 specs plus explicit `confirm=CREATE_NEW`. Gate accepts
only native interactive CCP and automatic empty Return. Z80 CPU
worker performs BDOS 17 SEARCH FIRST (refuse preexisting),
22 MAKE, 21 WRITE sequential, 16 CLOSE and independent BDOS
15/20/16 reopen/readback full CRC+EOF. Restores original registers,
C000-C0FF scratch and 0080 DMA; interrupted BDOS forces
Z80-only reset. No raw mounted .DSK edits. If failure after
MAKE, the partial file may exist and will NOT be automatically
deleted. Live `partial_cpm_file_possible` and sticky
`last_import_partial_possible` expose it across resets.
Native real-Z80 fixture tests BDOS operations, overwrite rejection,
source failure, write failure, corrupt readback and interruption;
CI now triggers B2c branch.

**B2c BUILD/TEST GATE NOT YET DONE**: Verify GitHub Actions jobs (GitHub
connector `fetch_commit_workflow_runs` only returns PR-triggered
runs; no positive CI result was available in this session). Run Mac
`pio run -e teensy41`, `pio run -e esp32dev`, and
`pio run -e esp32dev-tft`. Solve any native or compiler failures
in B2c branches ONLY, and ensure TFT DRAM threshold. DO NOT tell
user to flash before verified green builds, completion of outstanding
B2b tests and backup of writable CP/M disk image. Maintain accepted
recovery binaries.

## First B2c test after all gates

Use `source=A:STAT.COM` (the Teensy SD `A_STAT.COM`) and a
never-existing CP/M destination `name=A:IMPTST.COM` after verifying
it is absent from `DIR A:`. Back up the A: disk image first.
```bash
curl -sS "$ESP32_URL/api/cpm/service/status"
curl -i -sS -X POST "$ESP32_URL/api/cpm/service/import?source=A%3ASTAT.COM&name=A%3AIMPTST.COM&confirm=CREATE_NEW"
curl -sS "$ESP32_URL/api/cpm/service/import"
```
Poll GET to terminal. Expected `state=passed`,
`file_error=none`, `records=expected_records=42`, `bytes=5376`,
`crc16_ccitt_false=expected_crc16_ccitt_false=54346`,
`partial_cpm_file_possible=false`. Then `DIR A:` and perform a
B2b export of the new `A:IMPTST.COM` to a new SD
`/cpm/transfers/A_IMPTST.COM`; compare independently against
original `/cpm/transfers/A_STAT.COM` on the Mac. Do NOT run the
new COM executable without verification. Test collision/missing
SD source/invalid-confirm on spare filenames and verify no
unexpected resets, no HP/CPM/TFT/SD regressions.

If an import fails with partial possible, STOP: inspect the target
in CP/M and do NOT run it or auto-delete it. Preserve original
`/cpm/transfers/A_STAT.COM` and all mounted disk images.
