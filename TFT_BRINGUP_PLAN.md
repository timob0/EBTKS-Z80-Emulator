# TFT display bring-up: safe wiring audit and first-test plan

**Status:** design and wiring audit only, 2026-10-19. **No TFT wiring has been approved, no display driver added, no firmware built/flashed for TFT.**

## Repository and scope

- This branch: [`feature/tft-vspi-bringup`](https://github.com/timob0/EBTKS-Z80-Emulator/tree/feature/tft-vspi-bringup), created **with user's explicit approval** from the user-tested ESP32 `main` at `bba69cf816e3b5e35c0ea74ebe68fe114d4db23c`.
- [ESP32 `main`](https://github.com/timob0/EBTKS-Z80-Emulator/tree/main) is unchanged; [Teensy firmware `main`](https://github.com/timob0/ebtks-alt-firmware/tree/main) is unchanged. No more branches without explicit user consent.
- Keep existing Wi-Fi, Web UI, web updater, Z80 CP/M, HP85 remote display and CP/M disk read-ahead fully functional.
- First test: a small opt-in ESP32 graphics/color-bar/self-test rendered to the TFT, **not** a CRT mirror and not a new Z80 output device. Do not assume a framebuffer or touchscreen integration.

## Critical unknown: actual arrived TFT module

Do **not** connect display or add a controller-specific driver until user provides **either** a good front/back photo showing controller marking and all header labels, **or** an exact vendor model/product URL with the complete pinout, power/logic tolerances and backlight specifications. Record:

- Display controller (e.g. ILI9341, ST7789, ST7735, HX8357 etc.—examples only, not identified).
- SPI 4-wire or another protocol (8/16-bit parallel, RGB, I²C, or SD-card SPI sharing); labeled pin list; dimensions/resolution; touch controller if fitted.
- Whether module accepts 3.3 V power and 3.3 V logic **directly**, includes onboard LDO/level shifters, and whether LED backlight is current-limited or requires a separate resistor/transistor.
- Presence of `DC`/`RS`, `RST`, `BL`/`LED`, `MISO`/`SDO`, `T_CS` and `SD_CS`. Do not assume unlabelled TFT modules have the same wiring.
- EBTKS board revision and exact accessible header pins; use a multimeter for the **actual user's board**, because the published EBTKS schematic previously disagreed with continuity testing.

## SPI buses and already-occupied ESP32 pins (source-backed)

The live, hardware-proven CP/M 4 MHz SPI disk link uses **ESP32 SPI2/HSPI as slave**, with all of:

| Live disk-link signal | ESP32 GPIO | User's continuity-checked CN3 pin |
| --- | ---: | ---: |
| Teensy MOSI → ESP32 MOSI | GPIO22 | 21 |
| ESP32 MISO → Teensy | GPIO17 | **17** |
| Teensy SCK → ESP32 | GPIO16 | 14 |
| Teensy CS_N → ESP32 | GPIO32 | 4 (10 kΩ external 3.3 V pull-up installed) |
| ESP_READY → Teensy | GPIO27 | 8 |

Those five GPIOs are **not** available to drive the TFT while SPI read-ahead runs. ESP32 UART GPIO1/3 remains reserved for Teensy control/JSON and must not be repurposed. The HP-85 and Teensy SD interface must remain unchanged.

A **separate ESP32 SPI3/VSPI master** is technically plausible (not yet verified for the user's module or CN3): usual classic ESP32 candidate pins `SCK=GPIO18`, `MOSI=GPIO23`, optional `MISO=GPIO19`, `CS=GPIO5`. These are **GPIO numbers**, not verified *CN3 physical pin locations*. [Original wiring notes](CN3_SPI_PROPOSAL.md) guessed CN3-17 for GPIO18, but on the physical board that CN3 pin was later measured to be **GPIO17 already in active use**. **Never attach TFT SCK to CN3-17**. Confirm the actual EBTKS GPIO18 contact (or an alternative free GPIO) using continuity before proposing a wiring table.

GPIO5 is an **ESP32 boot strapping pin**; a TFT that pulls its CS low at power-on could prevent ESP32 startup. Prefer a different verified, otherwise-unused output-capable GPIO for TFT CS; verify DC and reset GPIO availability too. TFT touch, backlight and onboard SD may require further signals; all must be checked against existing uses and power budget.

**Electrical limits:** 3.3 V GPIO signaling. Do not infer TFT VCC from connector power rails: confirm regulator, allowable VCC and LED current on the actual module. Ground must be common before applying signal lines. Power off the EBTKS board while wiring.

## Minimal, reversible test proposal (after module identification)

1. Confirm all TFT module pins, chosen ESP32 GPIO-to-CN3 mapping, power/backlight current, GPIO5 strapping and pin overlap via meter and datasheet.
2. Select a mature display driver matching the **confirmed** controller, pinned to a compatible version with `espressif32@7.1.3` / Arduino-ESP32 2.x; prefer using the ESP32's *separate VSPI master*, not the active CP/M slave DMA bus. Disable touch/SD for first bring-up.
3. Make firmware **compile-time opt-in** (`EBTKS_TFT_ENABLE=0` default), without altering the tested CPU or Teensy. An optional web-invoked pattern is safer than continuous full-screen refresh. Test init at conservative SPI clock (e.g. 4 MHz) with color bars and simple text. Do not have the test start during boot until basic pin safety confirmed.
4. Do not allocate a full RGB565 framebuffer: a 320×240 16-bit frame is **153,600 bytes** (larger screens worse). Use small line/rectangle buffers or the display library's streaming draw API; the ESP32 heap has already shown fragmentation with Wi-Fi and SPI tasks. Verify runtime `/api/system/health` free/min/largest heap and `/api/cpm/spireads` for failures.
5. Compile/test an **OFF** configuration and a **TFT-ON** configuration with CI; check binary size/DRAM and CPUTask stack, and only then have the user flash as a hardware experiment. Ensure CP/M and disk reads still work when TFT rendering occurs; no changes to Teensy.
6. If electrically safe and fast enough, later consider rendering a mirror from the existing CRT display buffers. **Do not promise live full-screen CRT mirroring yet:** frame bandwidth, page state and CPU scheduling must be profiled.

**Stop gate:** Await exact TFT product/model and pinout before assigning physical wiring or committing display-driver code. A label such as just "SPI TFT" is not sufficient. Do not energize an unknown display or guess 5 V/3.3 V tolerance.

See `NEXT_CHAT_HANDOFF.md` top live checkpoint; historical CN3 proposal claims are not authoritative where physical measurements supersede them.
