# EBTKS Z80 / CP/M integration — current project links

**Current mainline status — 21 October 2026:** Hardware-validated CP/M/Z80, TFT, 16-color/CP437 Web terminal, HP keyboard integration, IRAM SPI disk cache, inbound Telnet and CP/M Hayes Wi-Fi modem are merged in the ESP32 **`main`** branch via [PR #6](https://github.com/timob0/EBTKS-Z80-Emulator/pull/6) (commit `b3d02b43`). The post-merge native/firmware CI [passed](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39602560884). For the definitive current state and next-chat instructions see **[NEXT_CHAT_QUICKSTART.md](NEXT_CHAT_QUICKSTART.md)**; [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md) is the full historical chronology, including superseded experimental status statements. The matching Teensy and HP Option-ROM firmware remain separately tested and **were not merged by PR #6**.

## ESP32 build configurations

There are exactly **two supported PlatformIO build targets**:

| Environment | Display | Console access | Command |
| --- | --- | --- | --- |
| `esp32dev` (default) | No external TFT or LovyanGFX dependency | CP/M over Web UI and Telnet | `pio run -e esp32dev` |
| `esp32dev-tft` | External 320×480 ST7796 SPI, 80×24 ANSI/VT100/CP437 terminal | CP/M over TFT **and** Web UI/Telnet | `pio run -e esp32dev-tft` |

Upload the selected `.pio/build/<environment>/firmware.bin` through the existing ESP32 browser firmware updater. Both targets include the same Z80 CPU, SD service, Wi-Fi and SPI disk read-ahead. The TFT option uses a separate VSPI master (4 MHz) and draws on a bounded low-priority task without a full framebuffer. Only use the TFT build with correctly verified display wiring and supply; do not infer physical CN3 pin numbering from the published drawing.

The previous experimental `esp32dev-tft-ansi`, `esp32dev-tft-terminal` and color-bar `esp32dev-tft` builds are historical configurations of the TFT feature branch, **not additional supported build environments**. The current `esp32dev-tft` produces the fully featured ANSI/CP437 terminal, equivalent to the former `esp32dev-tft-ansi`. The old revisions remain in Git history for diagnosis and rollback.


**New user?** Follow the [Getting started guide](GETTING_STARTED.md) for the Teensy/ESP32 flashing sequence, SD-card layout, 8 MiB disk and HP 82900 LIF images, and a web UI walkthrough.

**CN3 SPI and TFT:** [CN3 SPI wiring notes](CN3_SPI_PROPOSAL.md) include historical schematic assumptions. The Teensy-master/ESP32-slave CP/M disk link has been hardware-validated, and the independent ST7796 TFT on ESP32 VSPI has also been hardware-tested. TFT GPIO14/23/13/33/19 assignments and signal/power precautions are documented in [TFT_BRINGUP_PLAN.md](TFT_BRINGUP_PLAN.md).

> **Current verified baseline (2026-10-19):** ESP32 [`main`](https://github.com/timob0/EBTKS-Z80-Emulator/tree/main) includes the user-tested portable redcode/Z80 CPU replacement and Media Manager/SPI improvements ([PR #3](https://github.com/timob0/EBTKS-Z80-Emulator/pull/3)), **plus the user-hardware-tested ST7796 CP/M ANSI/VT100/CP437 terminal** ([merged PR #4](https://github.com/timob0/EBTKS-Z80-Emulator/pull/4)). Turbo Pascal's ANSI colors and DEC graphics match Telnet on the TFT, and Multiplan shows inverse text/cursor highlighting. Two firmware build targets (above) are CI validated; both retain Web UI/Telnet. Teensy [`main`](https://github.com/timob0/ebtks-alt-firmware/tree/main) remains unchanged and previously hardware-tested.
>
> **Current functionality:** CP/M, Multiplan, Zork1, Turbo Pascal and TINST; web terminal, remote HP display, disk mounts and browser OTA; default-on 4 MHz CRC-framed 16-sector SPI read-ahead with UART fallback, **UART-only writes**, and dot-prefixed SD metadata hidden from Media Manager. The SD File Service Test browser page and Media Manager transport control panel were retired; useful SPI diagnostic endpoints remain. PlatformIO pinned to `espressif32@7.1.3`. Separate CPU library is LGPL-3.0-or-later; verify binary distribution obligations before external releases.
>
> **Branch policy:** **Do not create any new feature or fix branch without first obtaining explicit user approval.** The supported release branch is `main` in both repositories. [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md) is the living authoritative status. The next candidate task is Teensy-local creation of an empty 8 MiB CP/M image (discuss/approve before editing Teensy); accelerating whole-image web transfers is not a priority.
>
> The standalone emulator README below is historical and may describe different hardware.

**For new development conversations:** Read [NEXT_CHAT_QUICKSTART.md](NEXT_CHAT_QUICKSTART.md) **first**, then the top of [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md); check repository branches and request explicit user approval before any new development branch.

---

# ESP32-Z80-Emulator


Update 28/04/23

CPM SD commands: sdfiles, sdpath and sdcopy now send serial output to telnet session.
Other minor tweaks.

*************************

*** Update 19th April 2023 ***

Added support for ESP32-S3 Devkit plus a few minor changes


****************************

*** Update 8th April 2023 ***

V2.2 Many changes and improvements:

Now supports Over The Air (OTA) updates from the Arduino IDE

Telnet client is forced into Character mode to more closely emulate a serial terminal


****************************

*** Update 7th June 2022 ***

V2.1 Split the code over several files for readability and a few minor tweaks.


****************************

*** Update 18th April 2022 ***

V2. Improvements to CPU Task and single Step / Breakpoints.

Note
There is a problem with the V2.0.x ESP32 core that in most cases causes the SD card to perform slowly, if at all. It would appear these issues will be fixed in version 2.03 of the core which is due for imminent release.


****************************

*** Update 13th July 2021 ***

Improved WiFi connection reliability. 
****************************

*** Update 12th June 2021 ***

Various code improvements and added experimental Telnet support so you can telnet to the Z80 over WiFi. On a Telnet connection being made the Z80 will reboot, when telnet is in use output will also be mirrored to the serial console.
****************************

*** Update 7th June 2021 ***

Fixed nasty bug that affected multiplication in MBASIC and the ability to enter commands in Zork. Add, subtract, compare and DAA instructions have been improved.
Now supports the TTGO-T2 board with colour OLED the display currently implements RED and Green 'virtual' LEDs to show the bit status of both GPIO ports and a blue 'virtual' LED to indicate disc activity.
****************************


This is very much a work in progress however Nascom Basic and CP/M 2.2 do run quite well.
It's not pretty but it does work !!

On a techincal note the serial receive code has been put in a separate task on the ESP32 the reason is that just checking for a received character really slows down the emulator so this has been relegated to a task running on Core 0 of the ESP so it doesn't impact the main emulator code running on Core 1.

The ESP32 code is configured for a Lilygo TTGO-T1 board with integrated SD card reader although this should run on most ESP32 Dev boards however it may be necessary to re-confiure the SPI ports for the SD card and also the GPIO Pins assigned to Z80 GPIO Ports and breakpoint switches.

The emulator handles all the original 8080 derived Z80 instructions and also implements some of the additional Z80 instructions, all in all over 350 instructions, enough to get Nascom basic and CP/M running although BBC Basic doesn't currently run under CP/M however mbasic does. For those interrested in retro games Zork also runs fine.

I haven't had time to perform benchmarks but comparing it to a homemade Z80 SBC running at 12Mhz it seems very comparable in it's performance.

The system boots from either SD Card or SPIFFS and initially looks for a file boot.txt on the SD card this is a simple text file that lists the rom images and the address in Hex to where they should be loaded. if no SD card is found then boot.txt is read from SPIFFS. Although it would be possible to put the CP/M ROM images on SPIFFS there would be insufficient space for a disk image and this would not be recommended for wear reasons on SPIFFS, and anyway the virtual disk controller is only configured for SD cards.

This version of CP/M can address 16 disks A - P of 8.4MB, each disk is held as a single file on the SD card in the /disks folder i.e. A.dsk, B.dsk...... P.dsk.
I've only included one disk image bit this can be cloned; format.com will erase any disk.


Additional CP/M utilities are provided:

sdfies.com  - Lists files in the current SD card directory, defaults to /downloads

sdpath.com - sets the SD card path, defaults to /downloads.

sdcopy.com - copies a file on the SD card to CP/M disk


Switch one should be a normally off toggle switch and when closed enables single step mode, switch 2 is a push button and is used to step to the next instruction or breakpoint.

The variable BP sets the breakpoint address.

The variable BPMode sets the Breakpoint action:
Mode 0 - Halt immediately and single step thereafter until switch one is turned off
Mode 1 - Halt at breakpoint and single step thereafter
Mode 2 - Stop each time the breakpoint is reached

Whenver the Z80 stops an a breakpoint all registers are dumped to the console along with the next 4 bytes of code and the top stack entry.

If switch one is on when the ESP32 starts then breakpoint mode is enabled, note that this slows the emulator down by around 50%. Pressing switch 2 will start the Z80 allowing you to turn switch one off if you don't want the CPU to stop at the first breakpoint straight away.
From V1.2 I've added the ability to set the Breakpoint address and mode interractively on startup through the console. Just boot the ESP32 with switch one on.

The emulator has a virtual 6850 UART that has a base address of 0x80 the baud rate is fixed by the emulator sofware and is configure for 115200 baud.
The Arduino serial monitor is unsuitable from interractive sessions and I would recommend "Serial" on OSX, "Putty" on windows or "Miniterm" on Linux. 

The emulator has 2 virtual Z80 GPIO Ports, Port A is at 0x00 and is 8 bits in size and ports B is at 0x82 with 2 bits. This should be enough to allow parallel data transfer with the Port B pins being used to strobe and acknowledge.
Ports 0x01 and 0x03 allow the individual IO port pins to be configured as inputs or outputs setting the bits in this port to 1 configure the the matching IO port bit to output and 0 to input. All ports are by default outputs.

The Nascom Basic and CP/M 2.2 code is heavilly based on the work of Grant Searle in particular his 7 chip Z80 SBC and RC-14 computer. the Z80 UART Monitor can be found here:: https://github.com/fiskabollen/z80Monitor


Note for Lilygo TTGO-T1 users the maximum sketch upload speed in the Arduino IDE seems to be 230400 baud and code uploads sometimes fails if there is an SD card in the reader so you may need to eject the card before uploading the sketch.


