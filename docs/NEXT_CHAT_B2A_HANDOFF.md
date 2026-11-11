> **SUPERSEDED FOR NEW WORK:** Stage B2a has now passed the user's physical functional tests. The active B2b SD export checkpoint is [NEXT_CHAT_B2B_SD_HANDOFF.md](NEXT_CHAT_B2B_SD_HANDOFF.md), with test guide [CPM_FILE_TRANSFER_B2B_SD.md](CPM_FILE_TRANSFER_B2B_SD.md). This older document is retained for the B2a recovery baseline and historical acceptance procedure. B2b builds and hardware acceptance are still pending.

# SIMON85 — B2a file-transfer continuation checkpoint

**Saved 2026-10-25, at user request. This is the CURRENT starting point for the next chat.**
Project: HP-85 + EBTKS Teensy + ESP32 Z80/CP/M 2.2.
**Do not resume the quarantined HP-native RMIDLE/CMENU trials.**
The user deliberately pivoted to file transfer after two physical HP-85 crashes
in earlier console-hook experiments.

## Repository and tested revision

Active development repository: **`timob0/EBTKS-Z80-Emulator`**.

- **New ESP32-only branch:** `feature/cpm-file-transfer-b2a`
  (created from the hardware-accepted `main` Stage B1 baseline).
- **Latest fully verified application/docs SHA before this handoff:**
  `d910fd36edd168e0b94883e357d0163a9b99f83b`.
- **GREEN GitHub Actions run:** [40228527192](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/40228527192),
  including all native redcode Z80 regressions; `esp32dev` and
  `esp32dev-tft` PlatformIO builds; TFT DRAM budget check; two
  opt-in Telnet compile builds; alternate SPI read-ahead-off headless
  regression; and embedded Git revision checks.
- New handoff/documentation-only commits after that run will change the
  branch HEAD and `EBTKS_BUILD_GIT_COMMIT`; confirm the latest Actions
  run before directing the user to pull, build or flash. Avoid claiming
  new documentation-only HEAD itself has passed CI until it has.
- **No physical hardware test of B2a yet.** The user's next action is
  local Mac build and controlled ESP32 flashing/hardware acceptance.
- No `main` branches changed, no recovery firmware overwritten.
  **Neither** `timob0/ebtks-alt-firmware` (Teensy) **nor**
  `timob0/HP85-Option-Roms` (HP LOADBIN) needs any source change
  or reflash for B2a.

## Implemented B2a functionality

This is the **first read-only CP/M live-file transfer checkpoint**.
The accepted **Stage B1** code had proved that the ESP32 can safely
run BDOS 17/18 for `CPDCAT` at a verified native CCP `D382h`
save point: automatic empty command-line Return, Z80 CPU state
saving/restoration, DMA memory protection, and no BDOS reentrancy
during foreground CP/M applications. B2a reuses the same mechanism.

- `ESP32_Z80_Emulator/cpm_file_read_service.h`: exact CP/M 2.2
  BDOS **15 OPEN**, sequential **20 READ** through selected record,
  **16 CLOSE**. Keeps the native FCB alive across calls, then
  restores Z80 CPU and C000h–C0FFh scratch and default DMA 0080h.
  Strict `A:`–`P:` drive-qualified 8.3 file names, current CP/M user.
  Single **128-byte record**, zero-based index **0..63**, or EOF.
  Missing file, EOF, DMA, BDOS trap/timeout error classification.
  Pure read-only: no `MAKE`, `WRITE`, sector edits or SD writes.
- `ESP32_Z80_Emulator/cpm_safe_point.h`: new **`READ_RECORD`**
  operation (not `FILE_READ`, which collides with Arduino FS
  macro); heap-owned 128-byte snapshot, error and checkpoint
  results; request cancellation on typed/nonempty CCP input,
  automatic blank Return; sticky last read error/reset status.
- `ESP32_Z80_Emulator/cpu_redcode.h`: invocation ONLY in the pinned
  Z80 CPU worker at proven CCP checkpoint; trapped/incomplete BDOS
  requests trigger **Z80-only** reset, preserving diagnostic data.
- `ESP32_Z80_Emulator/ebtks_web.h`: LAN endpoint
  **`POST /api/cpm/service/file?name=A%3ASTAT.COM&record=0`**
  arms a read only; **`GET /api/cpm/service/file`** reports
  `state`, `file_error`, `record`, `has_data`, `eof`,
  `length`, lossless `data` as **256 hex digits for 128 bytes**,
  and `crc16_ccitt_false`. Web callback never touches Z80 memory.
  Endpoint has **no network-level authentication**; expose only
  on a trusted private LAN; don't port-forward it.
- `tests/cpm_file_read_native.cpp`: real emulated Z80 BDOS
  trampoline fixture for open/read/close, multiple records,
  EOF/missing file, bad DMA, interrupted BDOS, exact CPU/scratch
  and DMA restore; further gate tests cover request authorization,
  automated Return, cancellation and result snapshots.
- `tools/cpm_b2a_read.py`: standard-library-only Mac exporter,
  polls safe-point readiness, requests records sequentially,
  verifies CRC-16, writes a **new** Mac file only after clean
  EOF, never overwrites existing destination.
  Limit: **64 records = 8,192 logical CP/M bytes**.
  128-byte CP/M records may contain original-padding bytes
  (including `1A`); exact original host byte length is unknown.
  This is NOT yet efficient or general large-file transfer.

## Local build / physical acceptance still needed

On the Mac in the **ESP32 repository** (Teensy/HP unchanged):

```bash
git fetch origin
git switch feature/cpm-file-transfer-b2a
git pull --ff-only
pio run -e esp32dev
pio run -e esp32dev-tft
```

First confirm both compile/link successfully **locally on macOS**,
especially the DRAM-constrained TFT build. If they do, flash only
the user's target ESP32 using their **existing preferred firmware
update method** (usually the Web UI); retain the accepted main
firmware for recovery. No B2a hardware/transfer data exists yet.
DO NOT claim acceptance or merge to main before user tests.

At an actual CP/M `A>` prompt, with no active foreground app,
choose a known small file (such as `A:STAT.COM` if present).
Example LAN-only probes:

```bash
curl -s http://<ESP32-IP>/api/cpm/service/status
curl -s -X POST 'http://<ESP32-IP>/api/cpm/service/file?name=A%3ASTAT.COM&record=0'
curl -s http://<ESP32-IP>/api/cpm/service/file
python3 tools/cpm_b2a_read.py http://<ESP32-IP> A:STAT.COM /tmp/STAT-B2A.COM
```

`POST` should return HTTP **202, accepted:true**, not file contents.
Wait for `GET` status `state=passed`; check `file_error=none`,
`length=128`, hex data and CRC for an existing record. On EOF
`has_data=false`, `eof=true`, `length=0`. File absent should
be a controlled BDOS `file_not_found`, not a guest reset.
Test record 0, later record, EOF, missing file, cancel during
typed CCP command; confirm the Mac script's checksum and output.
Any unexpected Z80-only reset: capture GET endpoint's sticky
`last_file_*` and `/api/system/health`.

Regress accepted CP/M `DIR`, HP `CPDCAT`, mounted disk/Media
Manager, MODEM and Multiplan, TFT/Web terminal, HP keyboard and
memory health. B2a's deliberate 64-record ceiling and repeated
OPEN+read-through per request are **temporary**.

## Decisions / next development steps

**Current user request was to SAVE STATE, not to merge or redesign.**
Do not add more executable code merely to satisfy the handoff.

1. Wait for **user's Mac build + physical B2a read results**.
2. If failures, diagnose without mutating mounted disk images;
   fix only ESP32 feature branch and rerun both CI builds.
3. On acceptance, plan **B2b**: efficient bounded multi-record
   export from CP/M to Teensy SD staging; then **B2c**
   write/import via BDOS with explicit rollback semantics; later
   **B2d** native HP `CPMCP` + HP-IB mass-storage endpoint.
4. Keep CP/M file operations in the **Z80 CPU task** at the safe
   point, NEVER in AsyncTCP, Teensy HP-bus ISR, or through raw
   mounted .DSK sector writes.
5. The project handoff is `NEXT_CHAT_HANDOFF.md` (very large,
   mostly historical); this concise checkpoint takes precedence
   for current B2a. Additional B2a test guide:
   `CPM_FILE_TRANSFER_B2A.md`.
   Earlier Stage B2 architecture:
   `CPM85_FILE_TRANSFER_STAGE_B2_PLAN.md` on the older
   `feature/cpm85-hpib-output` branch (design/history).

## Ready-to-paste new chat prompt

> Continue the SIMON85 project from
> `timob0/EBTKS-Z80-Emulator`
> `feature/cpm-file-transfer-b2a`. Please read
> `NEXT_CHAT_B2A_HANDOFF.md` and
> `CPM_FILE_TRANSFER_B2A.md` first.
> B2a ESP32-only native CP/M BDOS read-only record service is
> implemented, with successful CI run 40228527192 at source
> SHA d910fd36edd168e0b94883e357d0163a9b99f83b.
> No B2a physical hardware acceptance yet.
> We need to continue with my local Mac build, flash and CP/M
> read-only tests. Keep the rejected RMIDLE/CMENU branch
> quarantined; leave Teensy, HP Option ROMs and main unchanged.
