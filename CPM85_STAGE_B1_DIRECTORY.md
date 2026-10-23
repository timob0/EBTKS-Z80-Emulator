# Stage B1 — read-only CP/M BDOS directory service (experimental)

**Source branch:** `feature/cpm85-file-transfer-hpib-ui` (ESP32 only)
**Status:** implementation and native/CI checks pending. **NOT hardware-accepted.**
The previously accepted Stage A probe, memory-optimized firmware, HP/Teensy
code, mount table, disk-image writes and `recovery/` are unchanged.

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
