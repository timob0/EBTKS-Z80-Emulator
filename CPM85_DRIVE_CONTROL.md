# CPM85 v0.03a — HP-85 control of CP/M disk assignments

**Experimental implementation, 21 October 2026.** Not hardware tested until user confirms the three coordinated builds. The existing, accepted ESP32 `main`, Teensy keyboard branch and HP CPM85 v0.02a remain available for rollback; do not merge this branch before physical acceptance.

## HP BASIC API

```basic
LOADBIN "CPM85"
CPSTATUS
CPDMNT "B:","WORK.DSK"
CPDMNT "C:","PASCAL.DSK"
CPDUMNT "D:"
CPSTATUS
CPRESET
```

- `CPM` continues to attach the physical keyboard to CP/M; K8 continues to detach without resetting CP/M.
- `CPSTATUS` lists active assignments and any different desired assignments in a compact HP-85 CRT format. Filenames in the status display are abbreviated to 14 characters; full names remain in Media Manager.
- `CPDMNT` and `CPDUMNT` **change only the desired assignment**, persist it in ESP32 `cpmmount` NVS and print `CPRESET REQUIRED`. They never reset CP/M.
- Image argument is a *basename* under `/cpm/disks/`, not the current HP `SDCD` directory. Supported currently: native 8 MiB `.dsk` (R/W) and HP 82900 `.lif` (R/O); validation uses existing Media Manager.
- `CPRESET` checks persistent NVS and requests the same **Z80-only** reset as Media Manager's Apply control. HP BASIC, Teensy, Wi-Fi and the full ESP32 firmware stay alive. This intentionally works even when no mount is pending.

**Safety:** Call `CPRESET` *only with CP/M at an idle command prompt*, after exiting applications and allowing pending disk I/O to complete. The legacy CP/M BIOS caches disk-sector writes; there is no proven automatic “safe to reset” detection. Do not use `CPRESET` while a CP/M program is saving data. Make a backup of writable image files before first testing.

`CPIMAGES` was intentionally omitted; `SDCAT` already lists `/cpm/disks`.

## Coordinated source branches

| Processor | Repository | Branch |
| --- | --- | --- |
| HP-85 LOADBIN | `timob0/HP85-Option-Roms` | `feature/cpm85-drive-control` |
| Teensy 4.1 | `timob0/ebtks-alt-firmware` | `feature/cpm85-drive-control` |
| ESP32 ST7796 | `timob0/EBTKS-Z80-Emulator` | `feature/cpm85-drive-control` |

Build or install in order: Teensy, ESP32, CPM85. The HP keyboard bridge and all previously validated TFT, Telnet, Web, modem and SPI functionality must remain working. The HP module still assembles to `build/CPM85.dat` through CAPASM 1.0.1 and CAPLIF. ESP32's physical TFT variant is `pio run -e esp32dev-tft`. Teensy variant is `pio run -e teensy41`. Before physical flash, confirm all three GitHub CI runs pass.

## Control-plane implementation

The HP-85 LOADBIN issues short commands over four EBTKS I/O addresses (octal): `0177743` argument byte write, `0177744` opcode write (0=clear/abort, 1=status, 2=mount, 3=unmount, 4=reset), `0177745` status byte read (0 idle, 1 busy, 2 success, 3 failure), and `0177746` reply byte read (NUL releases mailbox). Original keyboard `0177741` and `0177742` are unchanged.

Teensy ISR callbacks only copy/read bounded bytes in RAM. The existing ESP32-initiated UART `cpm_keys` poll carries one short command and a reply acknowledgement, multiplexed with the existing physical keyboard events. ESP32 command handling uses the same `cpmStore*` NVS-backed mount APIs as Web Media Manager; command sequence numbers prevent repeated reset on retransmission. No HTTP/Wi-Fi request is issued from HP BASIC and no disk-image bytes are transferred.

The ESP32 reply buffer is allocated only on first drive-control request, rather than consuming permanently reserved TFT `.dram0.bss`. If it cannot be allocated, the command reports a low-memory error. HP-side timeouts clear the Teensy mailbox; if an operation times out while persistence is in progress, use `CPSTATUS` to inspect actual assignment state before retrying.

## Suggested physical acceptance tests

1. **Safe read-only smoke test:** Back up `/cpm/disks` images, load `CPM85`, run `CPVER`, `CPSTATUS`, and inspect reported A: and any existing mounts against the Web Media Manager. Confirm HP BASIC returns normally and CP/M remains active.
2. With a known, supported spare image (e.g. `WORK.DSK`) present, run `CPDMNT "B:","WORK.DSK"`. It should print that the mount is pending and ask for `CPRESET`. `CPSTATUS` should show B: old→new. CP/M B: should **not** change yet.
3. Test `CPDUMNT "B:"`, then two `CPDMNT` operations on different drives without a reset. Confirm persistent pending changes and no unexpected reset.
4. Deliberately try a non-existent image and an invalid drive; verify a clear error and no change to active or pending NVS state. Use `CPSTATUS` again after any timeout.
5. Exit CP/M programs to the command prompt, then `CPRESET` once. Confirm Z80 restarts with pending mappings; HP BASIC remains alive, Web/TFT/Telnet remain usable and no ESP32 reset occurs.
6. Recheck `CPM` attach, K8 detach, CP/M `DIR`, ordinary R/W file operations on a backed-up writable disk, `/cpmmedia` and updater accessibility.

No part of this milestone edits a CP/M directory directly. File-level import/export via HP BASIC belongs to a separate later BDOS-coordinated stage.
