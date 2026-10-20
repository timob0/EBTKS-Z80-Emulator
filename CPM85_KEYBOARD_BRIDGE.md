# CPM85 — experimental HP-85 physical keyboard to CP/M

## Hardware validation — 2026-10-19

**FIRST COMPLETE END-TO-END HP-85→CP/M KEYBOARD FORWARDING: PASSED ON THE USER'S PHYSICAL MACHINE.** The user built/flashed the coordinated development firmwares and tested CPM85 v0.02a. After issuing `CPM` on the HP-85, keypresses were successfully delivered to the running CP/M system. K8 detached the physical keyboard and returned control to HP BASIC **without stopping CP/M**. Issuing `CPM` again reattached to the *same running* CP/M session. These are confirmed, not merely CI results.

The test confirms HP CHIDLE capture/release, Teensy I/O/FIFO and serial polling, ESP32 input routing, and independent CP/M execution working together in this reported scenario. It does **not** yet establish complete HP special-key translations, sustained fast-typing losslessness, CRC-failure replay, FIFO overflow behavior on hardware, or long-duration CP/M disk/TFT stress stability. Keep all three production `main` branches unchanged pending additional tests and explicit merge approval.

**Development only; all three main branches remain unchanged.** This builds on the physical-machine-confirmed `CPM85 v0.01b` LOADBIN keyboard capture, K8 detach and `CPCOUNT`/`CPLAST` diagnostics.

## HP LINE → ESC, CONT → TAB; shifted keys unmapped (2026-10-21)

**Latest physical key-label correction from user:** CLEAR is the **SHIFT**
function of LINE. The intended dedicated ESC key is the unshifted **LINE**
key, just as CONT (unshifted) is the dedicated TAB key and SCRATCH is its
SHIFT function. The EBTKS HP-85A/B special-key table calls the unshifted
LINE function `-LINE`, with raw code **0240 octal (160 decimal)**; shifted
CLEAR produces **0222 octal (146 decimal)**.

**Current CPM85 translation, while attached:**

| Physical HP function | Raw code (octal) | CP/M byte |
| --- | --- | --- |
| LINE (unshifted; `-LINE` in EBTKS table) | `0240` | `1Bh` ESC |
| CLEAR (SHIFT+LINE) | `0222` | unmapped |
| CONT (unshifted) | `0217` | `09h` TAB |
| SCRATCH (SHIFT+CONT) | `0255` | unmapped |

The earlier experimental CLEAR→ESC mapping was not verified by an
ESC-aware application (Multiplan cancels some commands using CTRL+C).
Only LINE now produces ESC; shifted CLEAR cannot produce a stray ESC.
Normal ASCII Ctrl mappings, END LINE, BACK SPACE, VT100/ANSI arrow,
Home/Insert/Delete/Page/F1–F7 sequences, and K8 detach/re-attach are
unchanged. The HP85 `CPM85 v0.02a` LOADBIN / Teensy and ESP32
display/disk paths are untouched, as are all `main` branches. These
mappings apply **only while CPM85 is attached**, and return to HP BASIC
key behavior on K8 detach.

Tests assert LINE yields one ESC in both VT100 cursor modes, shifted
CLEAR is completely suppressed, CONT is TAB, SCRATCH is suppressed, and
mixed input retains correct order. **Hardware test pending for LINE→ESC.**
After CI passes, pull/rebuild only ESP32 `feature/cpm85-keyboard-forwarding`
with `pio run -e esp32dev-tft`, OTA upload firmware.bin, and test the
unshifted LINE key in an application that uses ESC to close a menu or
exit a prompt. Check CONT gives TAB, SHIFT+LINE/CLEAR doesn't send ESC,
SHIFT+CONT/SCRATCH doesn't send TAB, and K8 restores HP BASIC.

Reference: [EBTKS HP-85A/B KEYHIT keycode table](https://www.fliptronics.com/EBTKS/_downloads/98e10facfac7c36aab48e2d1fb2ef0e4/EBTKS_Website_as_PDF_2023_06_16.pdf).

## Historical superseded CLEAR → ESC and CONT → TAB test (not current mapping)

**Corrected physical key identity:** The HP-85 key is labeled **CONT** for
its unshifted action; holding SHIFT changes it to **SCRATCH**. The
[EBTKS HP-85A/B keycode table](https://www.fliptronics.com/EBTKS/_downloads/98e10facfac7c36aab48e2d1fb2ef0e4/EBTKS_Website_as_PDF_2023_06_16.pdf)
confirms they emit separate KEYHIT codes. The previous dev firmware mapped
SCRATCH to TAB, and the user **verified SHIFT+CONT (SCRATCH) worked as TAB**
but clarified that the intended TAB key is **unshifted CONT**.

| Physical HP-85 key/function | KEYHIT octal / decimal | Current CP/M input |
| --- | --- | --- |
| CLEAR (clear line) | `0222` / 146 | `0x1B` = ESC |
| CONT (unshifted) | `0217` / 143 | `0x09` = TAB |
| SCRATCH (SHIFT+CONT) | `0255` / 173 | Unmapped; swallowed while attached |
| -LINE (separate key) | `0240` / 160 | Unmapped (unchanged) |

The user could not confirm CLEAR/ESC in Multiplan; Multiplan uses CTRL+C
to cancel certain operations. **Do not interpret Multiplan's Ctrl+C behavior
as a failure of ESC translation.** The CLEAR/ESC code mapping remains
implemented but physical functionality is not independently confirmed.

All ordinary CTRL byte mappings (including CTRL+I → TAB), BACK SPACE,
END LINE, VT100 arrows/Home/Insert/Delete/PgUp/PgDn/F1–F7 and local
K8 detach are preserved. Only the ESP32 keyboard translation and
regression tests are changed; no Teensy, HP CPM85 LOADBIN, TFT,
disk link or production `main` changes. The HP-85 keyboard behaves
normally after K8 detach.

**Test after CI green:** Pull and build ESP32
`feature/cpm85-keyboard-forwarding` with `pio run -e esp32dev-tft`,
then OTA upload `.pio/build/esp32dev-tft/firmware.bin`. With physical
`CPM` attached, test **unshifted CONT → TAB** in a Tab-aware editor,
and verify **SHIFT+CONT no longer injects TAB**. Verify CLEAR emits ESC
in a program that recognizes ESC rather than relying on Multiplan's
cancel command. Retest Ctrl+I, arrows, Backspace and K8 detachment.
**This corrected CONT mapping remains pending physical verification.**

## VT100/ANSI keyboard hardware acceptance — 2026-10-21

**USER-REPORTED PASS:** After flashing the VT100/ANSI physical-keyboard build,
the user confirmed **"That works."** Treat this as working real-hardware
acceptance for the new mapping, not an exhaustive validation of every key
variant, DECCKM sequence or stress condition. Earlier BACK SPACE/CTRL was
also tested in Multiplan, Turbo Pascal, and ED.

The physical HP-85 does not have dedicated TAB or ESC keys, but the original
HP-83/85 character/keycode table defines **CTRL+I = TAB (ASCII 09h)** and
**CTRL+[ = ESC (ASCII 1Bh)**. The keyboard bridge passes both unchanged via
its established CTRL byte handling. No new firmware is needed; physical
verification of those specific combinations has not yet been reported.

## VT100/ANSI keyboard input profile — 2026-10-21 (implementation, now hardware accepted)

**User-requested design correction:** The physical HP-85 keyboard defaults to
normal **VT100/ANSI input**, rather than hardwired WordStar control codes.
This better supports BBS software and terminal applications. Legacy Turbo
Pascal/Multiplan installations that expect WordStar arrows can still receive
the *explicit* Ctrl+E/X/S/D combinations from the HP keyboard. Configure
terminal input appropriately in each application; output ANSI/VT100 display
emulation by itself does not make every CP/M program understand ANSI input.

Physical HP-85A/B keycodes (all **octal**) and output bytes:

| HP-85 physical key | Raw HP KEYHIT | CP/M input in normal mode |
| --- | --- | --- |
| Up / Down | `0241` / `0242` | `ESC [ A` / `ESC [ B` |
| Right / Left | `0235` / `0234` | `ESC [ C` / `ESC [ D` |
| Home Cursor | `0245` | `ESC [ H` |
| INS/RPL | `0243` | `ESC [ 2 ~` (Insert) |
| -CHR | `0244` | `ESC [ 3 ~` (Delete) |
| Roll Up / Roll Down | `0236` / `0237` | `ESC [ 5 ~` / `ESC [ 6 ~` (PgUp/PgDn) |
| K1 / K2 / K3 / K4 | `0200`–`0203` | `ESC O P` through `ESC O S` (F1–F4) |
| K5 / K6 / K7 | `0204`–`0206` | `ESC [ 15 ~` / `ESC [ 17 ~` / `ESC [ 18 ~` (F5–F7) |
| K8 | `0207` | HP-local detach to BASIC, **never forwarded** |

When CP/M emits `ESC [ ? 1 h`, a tiny output-side parser enables VT100
**application cursor mode**: four arrows send `ESC O A/B/C/D`, and Home
sends `ESC O H`. `ESC [ ? 1 l` or VT100 reset `ESC c` returns to
normal mode. Private-mode settings can share CSI params and arrive split
across output chunks. This independent parser works with TFT enabled or
disabled and leaves Telnet/Web/TFT output unchanged.

Original physical BACK SPACE, FAST BACK SPACE, END LINE, all ASCII/CTRL
characters **including NUL**, and K8 behavior are retained. The rest of
the HP system keys, including -LINE and HP BASIC's separate DELETE key,
remain suppressed as they have no unambiguous ANSI terminal equivalent.
Arrow input emits **3 bytes per press**; five bytes is the largest function
key sequence. The keyboard worker now reserves space in its 128-byte FIFO
for each *entire* key sequence or drops that whole key on full FIFO instead
of leaking half an escape prefix. Its downstream serial-task drain also
keeps buffered bytes when the last virtual Z80 UART input ring is full.

The pure `ebtks_cpm85_keymap.h` holds both the mapping and DECCKM parser,
covered by the updated native regression; `ebtks_cpm85_keyboard.h` handles
ESP32 queueing and atomic cursor-mode publication, while `serial.h`
passively feeds outgoing CP/M characters to the mode tracker.
No Teensy or CPM85 v0.02a rebuild, CP/M disk, TFT renderer or production
`main` modification. Native and firmware CI checks must complete and
**physical acceptance has now been reported** for this new mapping; individual edge cases remain unverified.

**Hardware acceptance:** Save the user-verified previous firmware and
OTA-flash only the ESP32 `esp32dev-tft` firmware from
`feature/cpm85-keyboard-forwarding` after CI green. Check all four
arrows and Home in an ANSI-configured screen program/BBS, then INS/RPL,
-CHR, Roll Up/Down and K1–K7 where supported. Verify normal and
application cursor modes with a program that requests DECCKM. For classic
Turbo Pascal and Multiplan, verify manual WordStar Ctrl+E/X/S/D still
work, and configure the application for an ANSI-compatible terminal if
it supports one. Retest Backspace, Ctrl, K8/re-CPM, web terminal, Telnet,
TFT and disk access. Roll back the prior ESP32 build if necessary.

References: [EBTKS HP-85 key table](https://www.fliptronics.com/EBTKS/_downloads/98e10facfac7c36aab48e2d1fb2ef0e4/EBTKS_Website_as_PDF_2023_06_16.pdf),
[DEC VT101 keyboard modes](https://vt100.net/dec/ek-vt101-ug-002.pdf),
[xterm control sequences](https://www.x.org/docs/xterm/ctlseqs.pdf).

## Backspace and control-key hardware acceptance — 2026-10-21

**PASS on user's physical HP-85.** After flashing the ESP32-only keyboard
mapping update, user reports that Backspace/Ctrl-key operation **totally works**,
tested under **CP/M Multiplan, Turbo Pascal, and ED**. This provides real
application validation of the new input translation beyond native/CI tests.
This is not a claim that every possible control combination, HP special key,
or long-duration transport error recovery was tested.

No Teensy or CPM85 v0.02a change was needed. Retain this dev firmware as the
known-good working test baseline. Remaining optional work: measured mappings
for physical navigation/function keys and explicit CP/M terminal dialect
choice, FIFO overflow/CRC replay reliability testing. ST7796 hardware
scrolling has been postponed at the user's request. Production `main`
branches are unchanged.

## Architecture

- HP-85 `CPM85 v0.02a` installs the **existing proven CHIDLE hook**. Every non-K8 captured `KEYHIT` is written to EBTKS write-only I/O address **0177741 octal** (relative I/O function index **0341 octal**, 0xE1). `CPM` writes 1 to control address **0177742 octal** (index 0342 / 0xE2), K8 or CPOFF writes 0. K8 release remains local and immediate; no ESP32 response needed.
- Teensy `feature/cpm85-keyboard-forwarding` registers tiny HP-bus ISR callbacks for these two previously unused EBTKS port indices. The callbacks perform only bounded FIFO operations; **no serial, SD, SPI, allocation or logging in the bus ISR**. 128 slots, 127 available; overflowing events are counted.
- ESP32 `feature/cpm85-keyboard-forwarding` polls a **CRC-checked JSON** `{"cpm_keys":{"max":24}}` request via the existing UART1 `ebtksLinkTransaction` and shared mutex. Teensy replies `{"cpm_keys":{"active":bool,"overflow":int,"keys":[...]}}`, up to 24 raw HP keycodes per poll. No unsolicited UART bytes are injected into the storage transport. The low-priority ESP worker translates printable ASCII, HP END LINE (0232 octal → CR) and simple ASCII control keys; special HP navigation/keypad translations await keycode measurements. The worker queues translated bytes to the **existing** CP/M input router, which feeds `consoleRxEnqueue` without changes to the Z80 disk or output paths.
- The operation is independent of the presence of the TFT: `esp32dev` remains headless for Web/Telnet, `esp32dev-tft` retains the 80×24 ANSI ST7796 mirror. Both **development** builds include the optional key worker. ESP32 and Teensy production `main` remain unchanged.

## Physical Backspace and CTRL keys — ESP32 dev update (2026-10-20)

**Implementation committed on this development branch and hardware-verified by the user in Multiplan, Turbo Pascal and ED (2026-10-21).**
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
Native mapping regressions and both ESP32 firmware targets passed CI (run
[#39484098413](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39484098413));
the user's subsequent Multiplan, Turbo Pascal and ED tests establish physical
application-level validation.

**Previously completed local test procedure (retain for regression):** preserve known-good binaries; pull only the **ESP32**
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

## Historical superseded WordStar-arrow experiment — 2026-10-21 (not current behavior)

**Superseded by the ANSI/VT100 input profile documented above.** The temporary CTRL+E/X/S/D arrow translation was committed, passed CI, but is no longer the current source behavior.

## Physical HP-85 cursor arrows for CP/M — development update, 2026-10-21


**Implemented, not yet hardware-validated.** The HP-85A/B physical cursor
key codes are confirmed by the EBTKS special-key table. Both Microsoft
Multiplan for CP/M and classic Turbo Pascal's WordStar-style editor use the
same ASCII control characters for movement. This gives a safe, shared
single-byte input mapping (no VT100 escape-sequence injection needed):

| HP-85A/B key | Raw KEYHIT (octal / decimal) | CP/M console byte |
| --- | --- | --- |
| LEFT CURSOR | `0234` / 156 | `0x13` = CTRL+S |
| RIGHT CURSOR | `0235` / 157 | `0x04` = CTRL+D |
| UP CURSOR | `0241` / 161 | `0x05` = CTRL+E |
| DOWN CURSOR | `0242` / 162 | `0x18` = CTRL+X |

Source: [EBTKS key code reference](https://www.fliptronics.com/EBTKS/_downloads/98e10facfac7c36aab48e2d1fb2ef0e4/EBTKS_Website_as_PDF_2023_06_16.pdf), [Microsoft Multiplan CP/M manual](https://www.bitsavers.org/pdf/microsoft/cpm/Microsoft_Multiplan_for_CPM_1982.pdf), and [Turbo Pascal editor key reference](https://www.bitsavers.org/pdf/borland/turbo_pascal/Turbo_Pascal_Reference_Manual_Feb84.pdf).

Implemented in `ESP32_Z80_Emulator/ebtks_cpm85_keymap.h`;
`tests/cpm85_keymap_native.cpp` now verifies the full four-key table,
preservation of all existing backspace/CTRL/K8 behavior, suppression of
other special codes, and ordering of a mixed input sequence. The CI static
check has been extended. No changes to the Teensy or CPM85 v0.02a, TFT
12 MHz renderer, SD/CP/M disk link or either production `main` branch.
Existing known-good rollback is the preceding ESP32 development commit
`3eed5aae71800510cc9f41c544e6a98b6936ede6`.

**Application compatibility:** In CP/M command-line mode, CTRL+S can
act as XOFF (pause output), and CTRL+E / CTRL+X have other editing
meanings. The arrow translation is targeted at Multiplan/Turbo Pascal
*when those editors are active*, not as universally meaningful
navigation in the CP/M shell. HOME, INSERT/REPLACE, -CHAR, -LINE, DELETE,
ROLL UP/DOWN and K1..K7 remain intentionally unmapped because their
applications' command conventions vary; mapping them without a selected
profile risks destructive editing or triggering CP/M console controls.

**Next hardware validation:** once GitHub CI shows both native and firmware
targets passed, pull existing ESP32 `feature/cpm85-keyboard-forwarding`,
build `pio run -e esp32dev-tft`, and OTA flash the new `firmware.bin`.
Teensy firmware and CPM85 LOADBIN remain unchanged. In Multiplan, move
one cell in each direction, including repeated arrows; in Turbo Pascal,
create/open a disposable source file and move the edit cursor in all four
directions across lines. Check Backspace, Ctrl keys, K8 return, re-CPM,
TFT output, and disk/Web/Telnet. If a cursor key acts unexpectedly,
switch back to known-good ESP32 bin and report the key/application.
**These physical results have not yet been reported.**

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

- **Hardware status:** CPM85 v0.02a Teensy→ESP32 forwarding, K8 detach and live CP/M reattachment were user-verified; the newer backspace/control-key map has also been hardware-verified in Multiplan, Turbo Pascal and ED.
- ASCII text and all CTRL codes are mapped, and BACK SPACE / FAST BACK SPACE / END LINE have documented explicit translations. Navigation, COPY/PAPER and other HP function/editor keys are still unmapped pending selection of the correct CP/M terminal input dialect. Unknown special keycodes are dropped, not sent to CP/M as garbage.
- Keyboard transport is best-effort with a small FIFO and polling. If the request/reply CRC fails after a Teensy batch is dequeued, that batch can be lost; no exactly-once ACK/replay mechanism yet. FIFO overflow counter exposes bursts beyond its capacity. Do not type irreplaceable work during early testing.
- The extra control/keyboard I/O addresses are **experimental EBTKS extensions** and must not be assumed standard or safe for unrelated HP-85 peripherals.
- The extra worker uses a few KiB of ESP32 heap/stack. Check heap, CP/M read-ahead operation and long-running stress before considering this merge-ready.
