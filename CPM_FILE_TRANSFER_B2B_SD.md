# Stage B2b checkpoint 1 — live CP/M -> Teensy SD staged copy

Status: **implemented on development branches, hardware acceptance pending**.
No main-branch, HP option-ROM, RMIDLE or mounted-image changes.

## Branches and ordering
- ESP32: `timob0/EBTKS-Z80-Emulator`, `feature/cpm-file-transfer-b2a`
  (build/test the newly added B2b code only after both CI jobs and local
  PlatformIO builds succeed; B2a pre-B2b recovery source is `e01211f`).
- Teensy: `timob0/ebtks-alt-firmware`,
  `feature/cpm-file-transfer-b2b-sd`, created from accepted `main`.
- HP LOADBIN/Option ROMs: unchanged.

This first B2b uses the exact B2a per-record read service repeatedly **under
a single native CCP D382h safe-point invocation**. It avoids 43 separate CCP
prompts for a 42-record export but still repeats BDOS OPEN/read-through/CLOSE
for each record, so it is deliberately inefficient until the later persistent
FCB streaming checkpoint.

## Safety invariants
- **Source only:** drive-qualified A:–P: 8.3 file, current CP/M user area.
- **Destination only:** Teensy SD `/cpm/transfers/<drive>_<name>`
  (example `/cpm/transfers/A_STAT.COM`); no caller-specified SD path.
- Teensy creates `/cpm/transfers/.<drive>_<name>.part` with
  `O_CREAT|O_EXCL`; does not overwrite existing complete or partial files.
- UART carries 128 binary bytes per record as Base64 with per-record CRC-16/
  CCITT-FALSE. Teensy accepts in-order records for one session only.
- The Teensy checks count and cumulative CRC, syncs and closes the partial,
  opens it read-only to verify size and full-file CRC, then renames to the
  destination if and only if it does not exist.
- On handled failures/abort, Teensy attempts to remove only the temporary
  file created by that transaction. Sudden power failure can leave a
  `.part` file; it is **not** automatically deleted or overwritten.
- No writes to mounted `.DSK` files, no CP/M BDOS MAKE/WRITE, no raw
  sector edits. All BDOS runs on the Z80 worker; the AsyncTCP callback only
  arms the request.
- Limit: 64 READ requests including the required EOF: no more than
  **63 data records (8,064 bytes)** can be published. Longer files fail
  without committing an incomplete copy.
- No transport-layer authentication on the new POST endpoint; use only
  on a trusted private LAN. Do not forward the ESP32 Web UI to the Internet.
- Mac B2a export's CRC checking and Teensy B2b file readback verify their
  respective transfers. For independent source correctness, physically
  compare the completed SD file to the earlier Mac export of the same CP/M
  file; avoid modifying the source or changing mounted images between tests.

## Before flashing — compile/CI gates
On the Mac, from the **separate** repositories:

```bash
# Teensy repository:
git fetch origin
git switch feature/cpm-file-transfer-b2b-sd
git pull --ff-only
pio run -e teensy41

# ESP32 repository:
git fetch origin
git switch feature/cpm-file-transfer-b2a
git pull --ff-only
pio run -e esp32dev
pio run -e esp32dev-tft
```

Verify GitHub Actions native+firmware jobs on BOTH feature branches.
Do not flash when CI/local builds are red. Retain currently flashed known-good
Teensy and ESP32 recovery images. After both pass, flash **Teensy first** by
the familiar Teensy update path, then OTA flash ESP32 TFT build; do not
modify the HP LOADBIN. If only one side is flashed, the export must fail
closed at SD staging initialization.

## First hardware experiment
Back up the SD card/writable images. Leave native CP/M at `A>` and do not
type or change mounts during the transaction. Verify `A:STAT.COM` exists.
Expected first test file: 42 records = 5,376 CP/M logical bytes.

```bash
curl -sS "$ESP32_URL/api/cpm/service/status"
curl -i -sS -X POST \
  "$ESP32_URL/api/cpm/service/export?name=A%3ASTAT.COM"
curl -sS "$ESP32_URL/api/cpm/service/export"
```

The POST is only `202 accepted:true`. Poll GET until `state=passed`.
Expected `records=42`, `bytes=5376`, `file_error=none` and
`destination=/cpm/transfers/A_STAT.COM`. Only one additional `A>`
prompt is expected. CRC16 reflects the complete 5,376-byte logical file
(start FFFF; poly 1021) rather than per-record CRC.

Safely unmount/power off before moving the SD card to the Mac. Compare
`/cpm/transfers/A_STAT.COM` **byte for byte** against the prior
`/tmp/STAT-B2A.COM`. The new directory is separate from `/cpm/disks`.
Use `cmp` and `shasum -a 256`, then verify ordinary CP/M/HP functions.

Negative tests, after the successful copy:
1. Re-request same file — stage begin fails and original SD destination
   remains unchanged.
2. Request nonexistent `A:NOTHERE.TST` — file_not_found, no committed file.
3. Reject invalid drive/path source with HTTP 400.
4. Export a larger-than-limit file — fail without publishing any file.
5. After every failure, check `GET /api/cpm/service/export`, CP/M DIR,
   HP CPDCAT, MODEM, Multiplan, Media Manager, TFT/Web terminal and
   `/api/system/health`. Unexpected Z80-only reset is a stop condition.

No reverse-direction writes yet. The next checkpoint after this is persistent
BDOS FCB streaming followed by separately gated Teensy-SD -> CP/M BDOS
MAKE/WRITE/CLOSE with explicit incomplete-file handling.
