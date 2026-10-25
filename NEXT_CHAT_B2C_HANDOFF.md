# ACTIVE NEXT-CHAT CHECKPOINT — Stage B2c SD -> CP/M (2026-10-25)

Read [CPM_FILE_TRANSFER_B2C_IMPORT.md](CPM_FILE_TRANSFER_B2C_IMPORT.md)
first. This is the current checkpoint. Historical `NEXT_CHAT_HANDOFF.md`,
`NEXT_CHAT_B2A_HANDOFF.md`, and `NEXT_CHAT_B2B_SD_HANDOFF.md`
are retained for recovery baselines and previous milestones.

## User hardware acceptance baseline

B2a native read-only CCP D382h service confirmed on physical HP-85:
`A:STAT.COM` 42 records, 5376 bytes, every CRC16 correct, EOF,
file-not-found and range rejection. CP/M DIR, HP CPDCAT, MODEM,
Multiplan, TFT/Web, Media Manager regressions successful.
B2b direct CP/M->Teensy SD export `/cpm/transfers/A_STAT.COM`
**physically PASSED**: 42 records, 5376 logical bytes, cumulative CRC16
54346 (0xD44A), SD readback and commit, only one extra A> prompt,
normal SDCAT visibility, and duplicate-destination failure with
`teensy_sd_stage_failed` and no reset. User is still finishing two
remaining B2b acceptance checks: missing-source negative test and
independent SD-versus-Mac byte comparison.

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
- HP Option ROM/LOADBIN and ALL main branches remain unchanged.

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

**CRITICAL NOT YET DONE**: Verify GitHub Actions jobs (GitHub
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
