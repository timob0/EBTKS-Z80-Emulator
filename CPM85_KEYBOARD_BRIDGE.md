# CPM85 — experimental HP-85 physical keyboard to CP/M

**Development only; all three main branches remain unchanged.** This builds on the physical-machine-confirmed `CPM85 v0.01b` LOADBIN keyboard capture, K8 detach and `CPCOUNT`/`CPLAST` diagnostics.

## Architecture

- HP-85 `CPM85 v0.02a` installs the **existing proven CHIDLE hook**. Every non-K8 captured `KEYHIT` is written to EBTKS write-only I/O address **0177741 octal** (relative I/O function index **0341 octal**, 0xE1). `CPM` writes 1 to control address **0177742 octal** (index 0342 / 0xE2), K8 or CPOFF writes 0. K8 release remains local and immediate; no ESP32 response needed.
- Teensy `feature/cpm85-keyboard-forwarding` registers tiny HP-bus ISR callbacks for these two previously unused EBTKS port indices. The callbacks perform only bounded FIFO operations; **no serial, SD, SPI, allocation or logging in the bus ISR**. 128 slots, 127 available; overflowing events are counted.
- ESP32 `feature/cpm85-keyboard-forwarding` polls a **CRC-checked JSON** `{"cpm_keys":{"max":24}}` request via the existing UART1 `ebtksLinkTransaction` and shared mutex. Teensy replies `{"cpm_keys":{"active":bool,"overflow":int,"keys":[...]}}`, up to 24 raw HP keycodes per poll. No unsolicited UART bytes are injected into the storage transport. The low-priority ESP worker translates printable ASCII, HP END LINE (0232 octal → CR) and simple ASCII control keys; special HP navigation/keypad translations await keycode measurements. The worker queues translated bytes to the **existing** CP/M input router, which feeds `consoleRxEnqueue` without changes to the Z80 disk or output paths.
- The operation is independent of the presence of the TFT: `esp32dev` remains headless for Web/Telnet, `esp32dev-tft` retains the 80×24 ANSI ST7796 mirror. Both **development** builds include the optional key worker. ESP32 and Teensy production `main` remain unchanged.

## Build/flash order once both CI checks pass

1. Back up the known-working **Teensy and ESP32** binaries and current `CPM85 v0.01b` LOADBIN. Power off the HP-85 before modifying the Teensy firmware.
2. Pull Teensy development branch and run `pio run -e teensy41`; flash `.pio/build/teensy41/firmware.hex` using the existing Teensy 4.1 procedure. The extra I/O ports are inert until the new CPM85 writes them.
3. Pull ESP32 development branch and run either `pio run -e esp32dev-tft` (attached ST7796) or `pio run -e esp32dev` (headless Web/Telnet). Upload `.pio/build/<env>/firmware.bin` using the established ESP32 Web UI firmware updater. No changes to Wi-Fi credentials, CP/M disk images or TFT GPIO wiring.
4. Build `CPM85 v0.02a` from [HP85-Option-Roms' existing CPM85 development branch](https://github.com/timob0/HP85-Option-Roms/tree/feature/cpm85-keyboard-capture) with `./py311/bin/capasm src/cpm85.asm -b build/CPM85.bin -l build/CPM85.lst -r 2 -c` and `./py311/bin/caplif build/CPM85.bin -l build/CPM85.dat -f CPM85`, then `LOADBIN "CPM85"`. Check `CPVER` before `CPM`.
5. In a CP/M Telnet or TFT terminal, use `CPM` on the HP-85 and type a short printable string followed by END LINE. Compare to typing via Telnet. Press K8; BASIC must respond immediately and `PRINT CPLAST` must show 135. Run `CPM` again; confirm CP/M remained running. If unexpected, detach via K8 or RESET the HP-85 and restore the old firmware as appropriate.

## Known limits of first test

- **Untested on HP-85 hardware:** These new Teensy–ESP32 forwarding changes need user validation. Only the earlier CPM85 v0.01b hook and numeric diagnostics are hardware-proven.
- Basic character and END LINE mapping only for now. Physical HP special keys, upper-case/lower-case shift, cursor keys, COPY/PAPER and editor keys need captured keycode examples to map safely. Unknown special keycodes are dropped, not sent to CP/M as garbage.
- Keyboard transport is best-effort with a small FIFO and polling. If the request/reply CRC fails after a Teensy batch is dequeued, that batch can be lost; no exactly-once ACK/replay mechanism yet. FIFO overflow counter exposes bursts beyond its capacity. Do not type irreplaceable work during early testing.
- The extra control/keyboard I/O addresses are **experimental EBTKS extensions** and must not be assumed standard or safe for unrelated HP-85 peripherals.
- The extra worker uses a few KiB of ESP32 heap/stack. Check heap, CP/M read-ahead operation and long-running stress before considering this merge-ready.
