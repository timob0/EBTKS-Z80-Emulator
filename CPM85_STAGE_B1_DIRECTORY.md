# Stage B1 — read-only CP/M BDOS directory service (experimental)

**Source branch:** `feature/cpm85-file-transfer-hpib-ui` (ESP32 only)
**Superseded for CURRENT feature branch:** the no-key CPDCAT/auto-Return
extension described in [CPDCAT_STAGE_B1.md](CPDCAT_STAGE_B1.md) now
automatically completes a *verified empty* CCP BDOS 10 line before the D382
trap. The manual-Return procedure below documents the **first** tested
B1 implementation only; the new extension is not yet hardware accepted.

**Status (2026-10-23):** CI green for source SHA `55a7c0803ba4b66106e5637317af8f90c0057078`; first physical read-only BDOS directory request **PASS**. Full post-operation hardware regression and exact flashed firmware SHA are **not yet reported**. Do not mark Stage B1 fully accepted or merge.
The previously accepted Stage A probe, memory-optimized firmware, HP/Teensy
code, mount table, disk-image writes and `recovery/` are unchanged.

## First physical test and current status

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

## What it does

An HTTP POST arms a one-shot request while the native CP/M 2.2 CCP is waiting
for interactive input. The operator must press a **blank RETURN** on the
actual CP/M terminal. The exact-PC D382 gate then executes CP/M BDOS
**Search First (17)** and **Search Next (18)** on the pinned Z80 CPU task,
using a wildcard FCB for the *currently selected drive and user area*.

A BDOS directory entry is an **extent**, not necessarily a unique file.
The first 12 extent entries are returned, with a `truncated` indication if a
13th exists. No file sizes or multi-extent aggregation are attempted yet.
This is a safe architectural bring-up, not `CPFGET` or `CPFPUT`.

- No disk sectors or image paths are written by the new service.
- No HTTP callback reads or modifies guest Z80 RAM.
- Native CCP signatures and safe-point/blank-input rules remain mandatory.
- Checks the standard BDOS page-zero jump to D806 and BDOS USERDMA=0080.
- Saves/restores the exact Z80 CPU state, C000–C0FF scratch/FCB/stack,
  and 0080–00FF DMA contents. Uses bounded CPU slices with cooperative yields.
- BDOS can update normal *internal* directory-search and drive-login caches;
  it is **not** a bitwise restoration of BDOS internal memory.
- An unexpected/interrupted BDOS invocation requests **Z80-only reset**.
  A failed preflight does not reset or enter BDOS.
- Does not change the normal E603 warm-boot handler outside the service.

## HTTP protocol (local trusted network only)

1. At the normal `A>` prompt, check
   `GET http://<ESP32-IP>/api/cpm/service/status`. Confirm
   `supported=true`, `ccp_idle=true` and `mode=ccp_input`.
2. Arm:
   `curl -i -X POST http://<ESP32-IP>/api/cpm/service/directory`.
   HTTP 202 means queued, **not completed**.
3. Press a completely **blank RETURN** in CP/M. Commands other than blank
   Return cancel the request, and applications such as MODEM return 409.
4. Get results:
   `curl -s http://<ESP32-IP>/api/cpm/service/directory`.
   Successful result: `state=passed`, `scope=current_drive_current_user`,
   `entries_are=directory_extents_not_file_sizes`, `count`, `truncated`
   and an `entries` array (name, user, extent, records).
5. Check normal `DIR`, Multiplan, drive read/write, Web, HP keyboard, TFT
   and Media Manager after the operation.

No transfer API, drive argument, user area switch, delete, write, image
mutation or printer mapping is implemented here. `POST .../probe` still
executes the original non-BDOS Stage A probe.

## Acceptance gates

**Before any firmware flash:** wait for GitHub native regression and both
PlatformIO firmware builds to pass; check `tests/check_esp32_dram_budget.py`
(124,544-byte static TFT DRAM ceiling), and rebuild locally on macOS:

```sh
git fetch origin
git switch feature/cpm85-file-transfer-hpib-ui
git pull --ff-only
pio run -e esp32dev-tft -t clean
pio run -e esp32dev-tft
```

If local linking succeeds and CI is green, only flash the **experimental
ESP32** build with the existing tested recovery mechanism available. Verify
the running SHA with `/api/system/health`. Do not reflash Teensy or HP LOADBIN.

First physical tests: normal prompt, one-file A: directory, empty directory,
populated directory >12 extents (verify truncation), cancellation with `DIR`,
409 while running MODEM, and full CPU/console/media regression. On an error
or unexpected reset stop testing and keep feature branch unmerged.

**Known design limitation:** CP/M 2.2 directory searches may log in the
selected drive, and the native BDOS may update allocation/search metadata.
The routine never edits the mounted image at the raw-sector level; it does not
promise full internal BDOS memory restoration. Stage B1 should be accepted
on hardware before adding BDOS file reads or Stage B2 SD output.
