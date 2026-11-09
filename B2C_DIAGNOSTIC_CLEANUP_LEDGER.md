# B2c diagnostic ledger and cleanup contract (2026-11-09)

## Policy

**Temporary diagnostics must not silently become production features.**
The inventory below is the scope of all code added after the original B2c
feature commits. Every item must receive an explicit *remove*, *retain with
reason*, or *replace with a tested permanent implementation* decision before
B2c is merged to `main`. Keep all diagnostic changes on their feature
branches in the meantime. No HP Option ROM changes were made.

Original B2c baseline commits:
- ESP32 `timob0/EBTKS-Z80-Emulator`: `d58c6811e1f7d2faa8fdd75140eb024131fec331`
- Teensy `timob0/ebtks-alt-firmware`: `833ac458a1845cfabee1b5ab186fd8e3a6bb8b83`

Code changes are identified by comparing each baseline with
`feature/cpm-file-transfer-b2c`. Do NOT assume all modifications are
tracing; protocol/recovery improvements may prove worth retaining.

## ESP32 diagnostic/change inventory

| File | Added / changed | Disposition before production merge |
|---|---|---|
| `ESP32_Z80_Emulator/cpm_import_diag.h` | RTC_NOINIT stage trace, reset retention, stack watermark, disk-I/O entry/return | **REMOVE entirely** after crash diagnosis, unless a separately reviewed permanent health feature is requested |
| `ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino` | `CpmImportDiag::onBoot()`, compile-time `EBTKS_Z80_CPU_TASK_STACK_BYTES` set to 6144 | **REMOVE RTC call and diagnostic macro**; decide production CPU stack size only from measured max/heap margin |
| `ESP32_Z80_Emulator/cpu_redcode.h` | RTC checkpoints around diskRead/diskWrite, preflight operation, post-service RTC status | **REMOVE RTC hooks**; decide whether to keep read-only source preflight |
| `ESP32_Z80_Emulator/cpm_file_write_service.h` | Optional `TraceCallback`, trace calls around BDOS functions | **REMOVE tracing callback and invocations** when diagnosis complete |
| `ESP32_Z80_Emulator/cpm_safe_point.h` | `PREFLIGHT_SD` operation, source-error codes, recovery flag, sticky source reason | **Review**: retain useful error propagation; remove only preflight-only fields if preflight removed |
| `ESP32_Z80_Emulator/ebtks_cpm_sd_import.h` | Read-only preflight/recover/release helpers; reason-code mapping; RTC trace calls | **REMOVE RTC calls and traceBdos**; retain source-error propagation; decide whether source preflight remains supported |
| `ESP32_Z80_Emulator/ebtks_web.h` | `/api/cpm/service/diag`, `/api/cpm/service/source`, import reason fields | **REMOVE /diag**; retain import reason fields; review whether /source is a useful permanent read-only API |
| `tests/check_b2c_diagnostics.py` | Static verification of diagnostics, preflight, RTC, default 6 KiB stack | **REMOVE or rewrite** to test retained production behavior |
| `.github/workflows/z80-core-native.yml` | Run extra B2c static tests and offline frame-parser selftest | **Remove only steps** for tools/tests actually removed |
| `tools/report_b2c_stack_frames.py` | Offline objdump-only stack frame analyzer | **Retain as developer tooling** if useful; not built into firmware, no runtime overhead |
| `B2C_DIAGNOSTIC_TEST.md` | Diagnostic procedure and panic evidence | **Archive or replace** with accepted B2c regression procedure |
| `NEXT_CHAT_B2C_HANDOFF.md` | Updated diagnostic/handoff notes | **Keep historical record**, mark diagnostic completion |

## Teensy diagnostic/change inventory

| File | Added / changed | Disposition before production merge |
|---|---|---|
| `include/EBTKS_CPM85_SD_Import.h` | `release` closes clean read-only source; source-name-guarded `recover` closes orphan descriptor | **Review to retain** for safe source/session management; no CP/M disk changes |
| `include/EBTKS_ESP.h` | UART `cpm_import` release/recover verbs | **Retain only if ESP32 uses/needs protocol** after final design |
| `test/cpm85_sd_import_native.cpp` | Native tests for release/recover behavior | **Retain/adjust** alongside any retained protocol |

## Confirmed facts and current working hypothesis

- B2b export and B2c read-only preflight pass for A:STAT.COM:
  42 x 128 bytes, CRC16 54346.
- Real B2c import crashes with ESP32 `ESP_RST_PANIC` (reason 4).
  RTC trace retained `stage=bdos_make`, record 0, and
  `stack_unused_bytes=1472` from the **historical** task watermark
  observed *before* the MAKE invocation. Import did not return.
- Ordinary CP/M PIP copy within A: succeeds. Multiplan installer also
  previously exercised normal CP/M file operations.
- `CPUTask`'s normal `z80BridgeRun()` has returned before service
  calls `CpmDirectoryService::invoke()`. This is **nested host function
  calls, NOT accumulating recursion**.
- `cpmMountedDiskWrite()` has a fixed 685-byte local Base64 buffer for
  a 512-byte sector. The deeper host call chain is consistent with
  stack overflow but **not yet a proved cause of panic**.
- On ESP-IDF, `uxTaskGetStackHighWaterMark` measures a historical
  minimum unused stack in **bytes**; the 1472-byte number is not the
  instantaneous amount free at MAKE.
- Stack budget, exact panic cause and guest BDOS stack interaction remain
  open. `ESP_RST_PANIC` is not itself proof of task stack overflow.
- The temporary 6 KiB CPU-task stack and extra disk I/O checkpoints are
  committed on the ESP32 feature branch but **not hardware validated**.
- The installed 4 KiB diagnostic ESP32 image reported commit
  `902b22a92206b8a5a064cff86c46afb334ef90b0`.

## Measured 4 KiB crashed-build ELF frame inventory (2026-11-09)

The user pulled GitHub source **without rebuilding**, then ran the
existing `.pio/build/esp32dev-tft/firmware.elf` through the offline
Xtensa objdump analyzer. Its parser selftest passed and 6239 functions
were disassembled. The ELF is understood to be the pre-6KiB diagnostic
build; verify build revision independently before relying on addresses.

Important emitted frames on a *potentially simultaneous* MAKE/write path:

| Function | Xtensa frame bytes |
|---|---:|
| CPUTask | 752 |
| CpmFileWriteService::execute | 768 |
| CpmDirectoryService::invoke | 48 |
| z80BridgeRun | 32 |
| z80_run | 32 |
| z80BridgeOut | 32 |
| ebtksBridgePortOut | 32 |
| portOut | 32 |
| diskWrite | 64 |
| cpmMountedDiskWrite | 928 |
| ebtksStorageExchange | 80 |
| serializeJson | 272 |

**Subtotal = 3072 bytes.** This subtotal is NOT the actual maximum:
the unreported `CpmSdImport::execute` frame, additional indirect or
inlined/library calls, runtime/ABI stack, and mutually exclusive
branches must be analyzed. `CpmDirectoryService::execute` with a
1008-byte frame is **NOT on the B2c import path**; do not count it.
The 928-byte disk-write frame already contains the 685-byte Base64
array; never add that buffer again.

The live RTC high-water value was 1472 bytes unused before MAKE on a
4096-byte task. Therefore 2624 bytes had been used at some prior point,
but that watermark is historical and cannot be algebraically added
to the disassembly subtotal. The full MAKE serial-write path is
credible for stack exhaustion; still **not proven** without exact
exception type or complete compiled call graph.

The offline script now supports `--all-matches`, which lists
`CpmSdImport::execute` and any additional matching functions without
altering the ELF or requiring an upload.

## Complete all-matches ELF stack-frame result (2026-11-09)

The second offline run used `--all-matches` with the unchanged ELF
(6239 disassembled functions). It established
`CpmSdImport::execute=144 B` and confirmed that the earlier report
had only truncated the list of symbols.

The statically identified simultaneous frames on the MAKE-path JSON
**serialization** branch are:
`CPUTask=752`, `CpmSdImport::execute=144`,
`CpmFileWriteService::execute=768`,
`CpmDirectoryService::invoke=48`,
`z80BridgeRun=32`, `z80_run=32`,
three port-output callbacks/dispatchers `32+32+32`,
`diskWrite=64`, `cpmMountedDiskWrite=928`,
`ebtksStorageExchange=80`, and `serializeJson=272` bytes.

**Sum = 3216 bytes (78.5% of the original 4096-byte CPU-task
allocation); only 880 bytes remain for deeper runtime, instruction
handlers, indirect callbacks and ABI/interrupt costs on that branch.**

Note that `serializeJson` and `ebtksLinkTransaction` are serial phases:
do not count both simultaneously. The 685-byte Base64 output buffer is
already included in `cpmMountedDiskWrite=928 B`. The 1008-byte
`CpmDirectoryService::execute` is NOT on this path. The earlier
1472-byte unused stack was a historical watermark at MAKE entry,
not an instantaneous stack-pointer measurement. These figures make
4 KiB stack inadequacy the leading cause of panic but do not yet
establish an exact complete maximum or exception subtype.

**For the next deliberate and preferably final hardware-validation
flash:** size the CPU task conservatively (8 KiB is an attractive
candidate, versus the currently committed untested 6 KiB trial), verify
runtime heap/memory safeguards via build and inspection, and decide
which temporary tracing to clean up before flashing. Don't treat the
temporary 6 KiB setting as an accepted final fix without analysis.

## Next steps requiring NO firmware flash

1. Inspect an **existing** `.pio/build/esp32dev-tft/firmware.elf`:
   ```sh
   python3 tools/report_b2c_stack_frames.py --selftest
   python3 tools/report_b2c_stack_frames.py .pio/build/esp32dev-tft/firmware.elf
   ```
   The script invokes PlatformIO's Xtensa objdump. It does not compile or
   flash. It reports emitted function frame sizes and explicitly does not
   claim a whole-path maximum. Use the ELF of the *4 KiB build that crashed*
   if available; record Git SHA to avoid mixing binaries.
2. Resolve inlining and indirect callback edges with Xtensa disassembly,
   including the disk write/mbedTLS/Arduino runtime, and compare the possible
   simultaneous stack use against the 1472-byte historical watermark.
3. For a fully rebuilt static source analysis, GCC `-fstack-usage`
   generates `.su` records. This is an **offline compiler build only**,
   not a reason to flash.
4. Decide one compact permanent fix, run native tests and both ESP32 build
   targets; verify TFT DRAM/heap margins before **one** deliberate hardware
   validation flash on a backed-up/disposable A.dsk.
5. Once physically accepted, remove/keep every ledger item intentionally,
   run regressions again, and only then consider merging.

**Do not run another B2c import on the potentially modified disk image
without a verified backup. Do not flash a diagnostic build merely to
gather more data that a source/ELF analysis can supply.**
