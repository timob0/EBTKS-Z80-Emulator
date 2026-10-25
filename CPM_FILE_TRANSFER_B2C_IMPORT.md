# Stage B2c — read an SD transfer into a new native CP/M file

**Status 2026-10-25: development implementation committed; CI, Mac
builds, and physical tests are NOT YET accepted.** First B2b forward-copy
milestone was physically demonstrated for A:STAT.COM (42 × 128 bytes,
SD CRC readback, one A>), while the user's additional B2b negative-file
test and independent Mac/SD byte comparison were still in progress.
Do not conflate B2b acceptance with B2c.

## Feature branches (no main/HP ROM changes)

- ESP32 `timob0/EBTKS-Z80-Emulator`: `feature/cpm-file-transfer-b2c`
  from the physical-test B2b branch `feature/cpm-file-transfer-b2a`.
- Teensy `timob0/ebtks-alt-firmware`: `feature/cpm-file-transfer-b2c`
  from `feature/cpm-file-transfer-b2b-sd`.
- HP Option ROM/LOADBIN: unchanged. No RMIDLE or C2/C2b console work.
- Preserve known-good flashed B2b ESP32 and Teensy images until both new
  PlatformIO builds and all CI regression jobs pass.

## First bounded protocol

The caller supplies two exact A:–P: CP/M 8.3 names:
`source=A:STAT.COM` refers ONLY to the already completed Teensy SD file
`/cpm/transfers/A_STAT.COM`, while `name=A:IMPTST.COM` is the NEW
destination file on the mounted live CP/M A: drive. SD paths are never
accepted from the network. The source SD file is never edited.

Teensy `cpm_import` UART verbs: `begin`, `read`, `finish`, `status`,
`abort`. `begin` checks source existence, nonzero/128-aligned length,
max 63 logical records (8064 bytes), reads entire SD source once for
source CRC-16/CCITT-FALSE, then creates an exclusive read session.
`read` serves only the next 128-byte record in order with CRC.
All Teensy operations use the already accepted CRC-framed UART transport.
There are no Teensy writes to a disk image on this path.

The ESP32 HTTP request is merely an opt-in command, with an extra
`confirm=CREATE_NEW` parameter. The existing verified native
CCP D382h safe point performs all guest BDOS accesses in the pinned
Z80 CPU task. The read-only Teensy source is preflighted BEFORE making
any CP/M changes. The CP/M Z80 trampoline invokes:
1. BDOS 17 SEARCH FIRST for the exact target FCB. MAKE is allowed only
   if the search returns 0xFF (absent). Never overwrite existing files.
2. BDOS 22 MAKE using a clean FCB, then N calls to BDOS 21 WRITE
   SEQUENTIAL, one per source 128-byte record.
3. BDOS 16 CLOSE. On ordinary failures (disk-full, read or checksum
   errors), CLOSE is still attempted to leave CP/M metadata consistent.
   If a BDOS invocation was interrupted, do not reenter BDOS: request
   Z80-only reset, preserving postmortem diagnostics.
4. For clean writes, use BDOS 15 OPEN then BDOS 20 READ SEQUENTIAL to
   independently check ALL record bytes via full CRC, EOF on the next
   record, and BDOS 16 CLOSE.
5. Restore CPU/stack/FCB scratch, default DMA 0080 and bridge trap state.
   Resume the CCP only after verified completion.

A failed import following a possible MAKE does **not** automatically
ERASE/delete the file. The operator must inspect the CP/M target.
`partial_cpm_file_possible` is set before MAKE, remains true for any
failure, and is preserved in `last_import_partial_possible` across a
Z80-only reset. Do NOT run the partially created executable. A separate,
explicitly confirmed cleanup procedure is future work.

## Builds — wait for green CI and local linking before any flash

Teensy:
```bash
git fetch origin
git switch feature/cpm-file-transfer-b2c
git pull --ff-only
pio run -e teensy41
```

ESP32:
```bash
git fetch origin
git switch feature/cpm-file-transfer-b2c
git pull --ff-only
pio run -e esp32dev
pio run -e esp32dev-tft
```

The CI workflows now include both B2c branches, the Teensy native SD
source tests, and a real-Z80 native simulated BDOS write/readback test.
Do not flash while any build/link/regression fails. When explicitly
ready, flash Teensy first and ESP32 TFT second. Preserve known-good
firmware and recovery path.

## First physical test — on backup of writable disk image

**Only after the user confirms the remaining B2b checks and both new
builds are green.** Make a backup of the Teensy SD and A: .DSK with
the system safely stopped; use a disposable/test CP/M mounted disk if
possible. At CP/M `A>`, run `DIR A:` and confirm `IMPTST.COM` does
NOT exist. Do not run Multiplan, MODEM or another CP/M command
concurrently with import.

Preflight:
```bash
curl -sS "$ESP32_URL/api/cpm/service/status"
curl -i -sS -X POST \
  "$ESP32_URL/api/cpm/service/import?source=A%3ASTAT.COM&name=A%3AIMPTST.COM&confirm=CREATE_NEW"
curl -sS "$ESP32_URL/api/cpm/service/import"
```

The POST is only accepted (202). Poll GET until `passed` or
`failed`. Success expected:
- `state=passed`, `file_error=none`,
  `partial_cpm_file_possible=false`, no forced reset.
- `records=42`, `expected_records=42`,
  `bytes=5376`.
- `crc16_ccitt_false=54346` (0xD44A),
  `expected_crc16_ccitt_false=54346` (source B2b CRC).
- `source=A:STAT.COM`, `target=A:IMPTST.COM`.

Verify `DIR A:` sees the newly created name. Then end-to-end
read it back with the accepted B2b exporter (it should go to a new
separate SD file `/cpm/transfers/A_IMPTST.COM`) and compare
`A_IMPTST.COM` versus `A_STAT.COM` byte-for-byte on the Mac after
safe SD removal. This is the independent integrity proof; do not
claim it from matching transport CRCs alone.

## Negative tests, only after successful and verified import
- Missing `confirm=CREATE_NEW` => HTTP 428; no BDOS.
- Wrong source/path/drive name => HTTP 400; no BDOS.
- Import to an already existing CP/M target => controlled
  `cpm_destination_exists`, no writes and no partial warning.
- Missing SD source => `teensy_sd_source_failed` before MAKE.
- Request with a foreground CP/M application running => rejected 409.
- No automatic overwrite, no auto-delete of partial files.
- Any forced Z80 reset, unexpected warm boot, memory stress or disk
  corruption => STOP physical B2c tests; capture `GET /api/cpm/service/import`
  and `GET /api/system/health` before reboot.
- Regress CP/M DIR/Multiplan/MODEM, HP CPDCAT, K8, Web/TFT terminal,
  Media Manager, and B2b forward export.

**Trusted LAN only.** The POST endpoint does not implement authenticated
remote writes; do not port-forward it.
