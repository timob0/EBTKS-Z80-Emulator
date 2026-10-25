# SIMON85 — Stage B2b SD export checkpoint (2026-10-25)

This is the **current** development continuation checkpoint. Read
`CPM_FILE_TRANSFER_B2B_SD.md` for the exact physical test sequence.
Do not resume quarantined HP RMIDLE/CMENU experiments.

## User-accepted baseline
The user physically verified the B2a ESP32 firmware
`e01211f455779be6cae7a7a54b3e7b75bbe9bd83`: native CP/M
`A:STAT.COM` read-only record service, Mac export of 42 logical records
(5,376 bytes) with all per-record CRC16 checks, correct EOF, missing
file classification and record-64 rejection. DIR, CPDCAT, MODEM,
Multiplan, Web/TFT terminal and Media Manager regressions also passed.
Independent byte-for-byte source comparison remains outstanding.

## New development (not yet physically tested)
- ESP32 repository `timob0/EBTKS-Z80-Emulator`:
  `feature/cpm-file-transfer-b2a` (now contains experimental B2b additions).
  B2a accepted parent `e01211f` remains reachable and known-good firmware
  should remain available.
- Teensy `timob0/ebtks-alt-firmware`:
  `feature/cpm-file-transfer-b2b-sd`, created from accepted Teensy `main`.
- HP `timob0/HP85-Option-Roms` unchanged; no HP reload.
- No `main` branch changed. No mounted .DSK direct edit or live CP/M write.

The ESP32 new `POST /api/cpm/service/export?name=A%3ASTAT.COM`
arms a single native D382 CCP checkpoint. The pinned Z80 worker then calls
the B2a read-only BDOS 15/20/16 service for records 0..63, sending each
verified 128-byte record via existing CRC-framed Teensy UART. It does
not maintain an open BDOS FCB between B2a calls yet: correctness first.
A missing EOF within the bound aborts. `GET /api/cpm/service/export`
shows state, progress, errors, and final path.

The Teensy new dedicated `cpm_export` UART op implements
begin/write/finish/status/abort, strict 8.3 source parsing and fixed
destination `/cpm/transfers/A_STAT.COM`. A unique
`.A_STAT.COM.part` is created exclusive; each record checks CCITT
CRC, an end-to-end CRC is checked, the SD file is synced/closed and
read back for size/CRC, and only then renamed to the destination.
Existing destination/partial names cannot be overwritten. Interrupted
power loss can leave a blocked stale .part file, to inspect manually.

**CI and local Mac builds are NOT yet verified** in this checkpoint.
The Teensy development branch's `.github/workflows/cpm85-bridge.yml`
now triggers native SD staging tests and Teensy41 PlatformIO builds.
The ESP32's existing `z80-core-native.yml` triggers native tests
and both `esp32dev` and `esp32dev-tft` builds/DRAM guard.

## Next action
1. Check both branches' GitHub Actions; inspect/fix failures on feature
   branches only. Local Mac build all three firmware targets.
2. Flash only after build success: Teensy feature firmware first, ESP32
   TFT feature firmware second; preserve known-good recovery binaries.
3. With CP/M idle `A>`, POST export A:STAT.COM and poll GET.
   Expected 42 records, 5,376 bytes and one additional CCP prompt.
4. Compare SD `/cpm/transfers/A_STAT.COM` byte-for-byte with the Mac
   `/tmp/STAT-B2A.COM` exported from the same CP/M source.
5. Reject duplicate target, missing file, invalid name, oversized file;
   regress CP/M/HP/TFT/Media Manager; capture health.
6. Only after hardware acceptance implement optimized persistent BDOS
   FCB streaming and, later, separately gated Teensy SD -> CP/M writes.
