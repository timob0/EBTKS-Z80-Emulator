# CPM85 — experimental HP-85 physical keyboard to CP/M

## Hardware validation — 2026-10-19

**FIRST COMPLETE END-TO-END HP-85→CP/M KEYBOARD FORWARDING: PASSED ON THE USER'S PHYSICAL MACHINE.** The user built/flashed the coordinated development firmwares and tested CPM85 v0.02a. After issuing `CPM` on the HP-85, keypresses were successfully delivered to the running CP/M system. K8 detached the physical keyboard and returned control to HP BASIC **without stopping CP/M**. Issuing `CPM` again reattached to the *same running* CP/M session. These are confirmed, not merely CI results.

The test confirms HP CHIDLE capture/release, Teensy I/O/FIFO and serial polling, ESP32 input routing, and independent CP/M execution working together in this reported scenario. It does **not** yet establish complete HP special-key translations, sustained fast-typing losslessness, CRC-failure replay, FIFO overflow behavior on hardware, or long-duration CP/M disk/TFT stress stability. Keep all three production `main` branches unchanged pending additional tests and explicit merge approval.

**Development only; all three main branches remain unchanged.** This builds on the physical-machine-confirmed `CPM85 v0.01b` LOADBIN keyboard capture, K8 detach and `CPCOUNT`/`CPLAST` diagnostics.

## Architecture

- HP-85 `CPM85 v0.02a` installs the **existing proven CHIDLE hook**. Every non-K8 captured `KEYHIT` is written to EBTKS write-only I/O address **0177741 octal** (relative I/O function index **0341 octal**, 0xE1). `CPM` writes 1 to control address **0177742 octal** (index 0342 / 0xE2), K8 or CPOFF writes 0. K8 release remains local and immediate; no ESP32 response needed.
- Teensy `feature/cpm85-keyboard-forwarding` registers tiny HP-bus ISR callbacks for these two previously unused EBTKS port indices. The callbacks perform only bounded FIFO operations; **no serial, SD, SPI, allocation or logging in the bus ISR**. 128 slots, 127 available; overflowing events are counted.
- ESP32 `feature/cpm85-keyboard-forwarding` polls a **CRC-checked JSON** `{"cpm_keys":{"max":24}}` request via the existing UART1 `ebtksLinkTransaction` and shared mutex. Teensy replies `{"cpm_keys":{"active":bool,"overflow":int,"keys":[...]}}`, up to 24 raw HP keycodes per poll. No unsolicited UART bytes are injected into the storage transport. The low-priority ESP worker translates printable ASCII, HP END LINE (0232 octal → CR) and simple ASCII control keys; special HP navigation/keypad translations await keycode measurements. The worker queues translated bytes to the **existing** CP/M input router, which feeds `consoleRxEnqueue` without changes to the Z80 disk or output paths.
- The operation is independent of the presence of the TFT: `esp32dev` remains headless for Web/Telnet, `esp32dev-tft` retains the 80×24 ANSI ST7796 mirror. Both **development** builds include the optional key worker. ESP32 and Teensy production `main` remain unchanged.

## Physical Backspace and CTRL keys — ESP32 dev update (2026-10-20)

**Implementation committed on this development branch; physical hardware regression still pending.**
The user's 12 MHz TFT/font/dirty-span build **has now been tested on the HP-85**:
the font fills the display height, and CP/M plus Multiplan feel more responsive.
ST7796 hardware scrolling is deferred by user decision; keep the existing renderer.

The HP-85A/B `KEYHIT` codes use **octal** values (per HP/EBTKS reference).
Raw bytes already travel intact through CPM85 v0.02a, the Teensy FIFO and the
CRC-polling UART protocol; the ESP32-only key translator is changed:

| Physical HP-85 key | KEYHIT value | Forwarded CP/M byte |
| --- | --- | --- |
| BACK SPACE | octal 0231 (153) | `08h` (BS), same as Telnet |
| FAST BACK SPACE | octal 0233 (155) | `08h` (BS) |
| END LINE | octal 0232 (154) | `0Dh` (CR) |
| CTRL+@ through CTRL+_ | decimal 0–31 | Unchanged ASCII control byte, **including NUL 00h** |
| Ordinary printable text | decimal 32–126 | Unchanged |
| DEL (ASCII) | decimal 127 | `08h` (BS), matching Web/Telnet |
| K8 (detach) | octal 0207 (135) | Never forwarded; handled locally |
| Other HP-specific keys | octal 0200–0377 | Safely ignored for now |

Example controls now forwarded: CTRL+C `03h`, CTRL+H `08h`,
CTRL+I `09h`, CTRL+M `0Dh`, CTRL+Q `11h`, CTRL+S `13h`,
CTRL+Z `1Ah`, CTRL+[ `1Bh`, through CTRL+_ `1Fh`.
Unlike the previous translator, it does not silently drop Ctrl+B, Ctrl+D,
Ctrl+K, etc. No synthesized VT100 navigation sequences have been introduced:
editing/navigation mappings require checking the CP/M application's expected
terminal input dialect before choosing sequences.

The pure `ebtks_cpm85_keymap.h` function and
`tests/cpm85_keymap_native.cpp` cover all 32 control codes, printable ASCII,
BACK SPACE / FAST BACK SPACE / END LINE, ASCII DEL, and K8 isolation.
The existing static keyboard bridge check is updated, and the native test is
wired into `.github/workflows/z80-core-native.yml`.
Firmware compilation in CI and physical validation must be checked independently
before calling this hardware-proven.

**Next local steps:** preserve known-good binaries; pull only the **ESP32**
`feature/cpm85-keyboard-forwarding` branch and build
`pio run -e esp32dev-tft`, OTA flash
`.pio/build/esp32dev-tft/firmware.bin`.
No Teensy or CPM85 rebuild is necessary. In CP/M first type a misspelled
`DIRX`, use physical BACK SPACE to erase X, then END LINE to execute DIR.
Try CTRL+H as another erase and verify CTRL+C/CTRL+S/CTRL+Q deliberately
at the CP/M prompt or while paging output. Exercise Multiplan's existing
editing functions, then K8 detach and CPM reattach. Confirm no regressions
in Web/Telnet, 12 MHz TFT, or disk I/O. All production `main` branches remain
unchanged.

## ST7796 redraw optimization experiment — 2026-10-19

**Firmware status:** ESP32 `feature/cpm85-keyboard-forwarding` only. Teensy firmware and CPM85 v0.02a LOADBIN are unchanged. **User confirmed on physical HP-85** that the 12 MHz optimized renderer scales the font to fill the display height and improves responsiveness of CP/M and Multiplan. Hardware scrolling remains deferred.

- The independent VSPI panel write clock increases from **4 MHz to 12 MHz**. An 8 MHz fallback is available by uncommenting `-DEBTKS_TFT_SPI_HZ=8000000` in the `[env:esp32dev-tft]` build flags in `platformio.ini`. Keep the CP/M Teensy-ESP32 HSPI disk link unchanged.
- LovyanGFX `Font0` uses `setTextSize(1.0f,1.5f)`: character glyph is now **6×12 pixels**, still on the original 6×13 cell pitch. This preserves **80 columns × 24 rows**, ANSI colors and original CP437/DEC graphics. The 480-pixel width was already mathematically filled at 80×6, and the original 13-pixel row pitch uses 312 of the 320 display rows; no wider glyph is possible without dropping below 80 columns. The 8-pixel bottom strip remains unused by design.
- The VT100 model now tracks **inclusive first/last changed columns per row**. Typing a single cell repaints only its 6×13 region, versus clearing/reprinting all 80 columns. Scroll and clear operations still invalidate all affected rows: we **do not** claim hardware vertical scrolling.
- Draws erase directly to each style run's actual background color rather than first blacking the row, preserving inverse/background/underline while reducing flicker. An outer `startWrite()/endWrite()` batches each row. The worker scans all dirty rows, yielding after every four, instead of an artificial three-rows-plus-35-ms throttle. It runs on the existing low-priority TFT task, not inside the console or disk workers.
- No frame buffer, new display allocations, new library, or transport protocol changes.

**Suggested physical test:** Build only `pio run -e esp32dev-tft` on the ESP32 branch; upload `.pio/build/esp32dev-tft/firmware.bin` via the existing Web UI. No Teensy/CPM85 reflashing. Confirm CP/M `DIR` scrolling, Multiplan inverse/cursor, Turbo Pascal ANSI color and DEC drawing, TFT during CP/M disk reads and HP keyboard detach/reattach. Watch for blank rows, stale letters, misaligned glyphs, corrupted drawing, or ESP32 heap/SPI errors. Compare to Telnet. If display corruption occurs, rebuild at 8 MHz; the old 4 MHz firmware remains available from branch history or previous saved binaries. Collect subjective redraw times and any measurable output if possible.

## Build/flash order once both CI checks pass

1. Back up the known-working **Teensy and ESP32** binaries and current `CPM85 v0.01b` LOADBIN. Power off the HP-85 before modifying the Teensy firmware.
2. Pull Teensy development branch and run `pio run -e teensy41`; flash `.pio/build/teensy41/firmware.hex` using the existing Teensy 4.1 procedure. The extra I/O ports are inert until the new CPM85 writes them.
3. Pull ESP32 development branch and run either `pio run -e esp32dev-tft` (attached ST7796) or `pio run -e esp32dev` (headless Web/Telnet). Upload `.pio/build/<env>/firmware.bin` using the established ESP32 Web UI firmware updater. No changes to Wi-Fi credentials, CP/M disk images or TFT GPIO wiring.
4. Build `CPM85 v0.02a` from [HP85-Option-Roms' existing CPM85 development branch](https://github.com/timob0/HP85-Option-Roms/tree/feature/cpm85-keyboard-capture) with `./py311/bin/capasm src/cpm85.asm -b build/CPM85.bin -l build/CPM85.lst -r 2 -c` and `./py311/bin/caplif build/CPM85.bin -l build/CPM85.dat -f CPM85`, then `LOADBIN "CPM85"`. Check `CPVER` before `CPM`.
5. In a CP/M Telnet or TFT terminal, use `CPM` on the HP-85 and type a short printable string followed by END LINE. Compare to typing via Telnet. Press K8; BASIC must respond immediately and `PRINT CPLAST` must show 135. Run `CPM` again; confirm CP/M remained running. If unexpected, detach via K8 or RESET the HP-85 and restore the old firmware as appropriate.

## Known limits of first test

- **Hardware status:** CPM85 v0.02a Teensy→ESP32 forwarding, K8 detach and live CP/M reattachment were user-verified; the newer backspace/control-key map is not yet hardware-verified.
- ASCII text and all CTRL codes are mapped, and BACK SPACE / FAST BACK SPACE / END LINE have documented explicit translations. Navigation, COPY/PAPER and other HP function/editor keys are still unmapped pending selection of the correct CP/M terminal input dialect. Unknown special keycodes are dropped, not sent to CP/M as garbage.
- Keyboard transport is best-effort with a small FIFO and polling. If the request/reply CRC fails after a Teensy batch is dequeued, that batch can be lost; no exactly-once ACK/replay mechanism yet. FIFO overflow counter exposes bursts beyond its capacity. Do not type irreplaceable work during early testing.
- The extra control/keyboard I/O addresses are **experimental EBTKS extensions** and must not be assumed standard or safe for unrelated HP-85 peripherals.
- The extra worker uses a few KiB of ESP32 heap/stack. Check heap, CP/M read-ahead operation and long-running stress before considering this merge-ready.
