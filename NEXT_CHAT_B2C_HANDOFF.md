# ACTIVE NEXT-CHAT CHECKPOINT — Stage B2c SD -> CP/M (2026-10-25)

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
