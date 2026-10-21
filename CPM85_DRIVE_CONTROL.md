# CPM85 v0.03a — HP-85 control of CP/M disk assignments

**Experimental implementation, 21 October 2026.** Not hardware tested until user confirms the three coordinated builds. The existing, accepted ESP32 `main`, Teensy keyboard branch and HP CPM85 v0.02a remain available for rollback; do not merge this branch before physical acceptance.

## 2026-10-21 follow-up: CPSTATUS formatting v0.03e

**Physical result:** After reflashing Teensy, ESP32 and CPM85, the `CPSTATUS` mailbox finally returned the live active image table on the HP-85. In v0.03b it appeared on one line with up-arrow glyphs instead of newlines. In v0.03c the HP command returned cleanly but printed nothing. Both confirmed that the current firmware/hardware could communicate and the remaining work was the HP BASIC display routine.

**Source-level causes found:** The assembler `D` suffix selects **decimal**: LF is `10D` / octal `12`, not `12D`; CR is `13D` / octal `15`, not `15D`. v0.03c also erroneously used `LDM R50,=3,0` claiming 768 bytes (in fact 3), and `STMD R50,...` to save an eight-byte register group into a two-byte buffer. The HP-85 manual specifies that multi-byte operations extend through the next register boundary: `R50` spans `R50-R57` (8 bytes) while `R56` spans `R56-R57` (2 bytes).

**Correction:** CPM85 **v0.03e** on HP85-Option-Roms `feature/cpm85-drive-control`: restore the v0.03b byte-by-byte printable ASCII output via HP `OUTCHR`; ignore incoming CR `13D`; on LF `10D`, call HP `OUTSTR` with a single literal ASCII space `BYT 40` to perform native CRT newline. Use `R56-R57` as a 768-byte read limit (little-endian `=0,3`) and exactly two-byte `STMD/LDMD R56` save around CRT output. The HP mailbox waiting loop also uses 16-bit `R56`. Static regression and CAPASM/CAPLIF CI passed; **hardware validation pending**. Only the HP LOADBIN needs rebuilding; do **not** reflash Teensy or ESP32 for this formatting fix. Test only `CPVER` and `CPSTATUS` first and report actual output before mount commands.

## 2026-10-21 hardware finding and v0.03b correction

**Physical test:** HP BASIC `CPSTATUS` appeared to hang while Z80 CP/M and the ESP32 Web UI kept working. HP RESET recovered the BASIC prompt and the existing `CPM` keyboard attachment remained usable. Do not test mount/unmount/reset commands until the corrected read-only status command is confirmed.

**Root cause confirmed in source:** The EBTKS `ioReadFuncPtr_t` callback returns a **bool claiming the read cycle**, and must assign the response byte to global `readData`. The new firmware incorrectly returned the status/reply byte as if the callback signature were `uint8_t (*)()`. This never placed the correct byte on the bus, and returning `false` for a status of 0 left the cycle unclaimed. The proven write-only keyboard path was unaffected.

**Fix:** On the Teensy `feature/cpm85-drive-control` branch, `onReadCpm85DriveState` and `onReadCpm85DriveReply` now assign `readData` and return true unconditionally, including when the byte is zero. Added `test/check_cpm85_drive_read_abi.py` to catch regression. On the HP85 ROM repository's same feature branch, **CPM85 v0.03b** caps reply reads at 768 bytes and reduces the bounded mailbox timeout. ESP32 code does not change for this fix.

**Retest:** Build and flash the *updated Teensy firmware*, then assemble/load CPM85 v0.03b on HP-85. **No ESP32 reflash needed** if already running `feature/cpm85-drive-control`. Try only `CPVER` and `CPSTATUS` first. After they return normally and the displayed mappings match Media Manager, proceed to the earlier mount/unmount/reset tests.


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
