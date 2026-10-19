# TFT display bring-up: safe wiring audit and first-test plan

## ST7796 FIRST HARDWARE TEST SUCCEEDED — 2026-10-19

**User-observed result:** the display powers up and shows `EBTKS ST7796 SPI TEST` at the top, six left-to-right color bars, and smaller text beneath them after flashing the TFT-enabled ESP32 firmware. This confirms the panel, selected VSPI GPIO lines and LovyanGFX ST7796 write-only graphics path work together physically at 4 MHz. The older warning elsewhere in this document that the TFT has not yet been powered/flashed is historical and superseded by this verified result.

**Still to validate:** Boot and run CP/M, exercise real disk reads with SPI read-ahead enabled, check Wi-Fi/Web UI/HP-85 remote display, `/api/system/health` for runtime free/minimum/largest heap and uptime, and `/api/cpm/spireads` for failures. Since the pattern is rendered once *before* CP/M starts, successful boot graphics do **not** establish uninterrupted simultaneous display redraws and live disk transfers. Do not assume sustained frame rate, touch, external SD support, or continuous LCD refresh. TFT module VCC arrangement and exact CN3 contact numbers (different from schematic) remain unreported; record when known. Do not merge experimental feature branch until owner verifies regression and requests merge.

---



## 2026-10-19 FIRST FIRMWARE PROTOTYPE — one-shot ST7796 draw

**TFT CI BUILD VERIFIED — 2026-10-19:** GitHub Actions [run #39272754062](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39272754062) passed all four jobs after fixing Arduino's `MOSI` macro collision using `kTft*` GPIO identifiers. TFT static RAM 124,376 bytes (38.0% of available), flash 1,178,581 bytes (89.9% of app partition). Remaining app-partition room 132,139 bytes; runtime heap must be checked in hardware. The display binary is **compiled and ready for a controlled user flash after power-supply confirmation**, but it has **not** been tested on a physically powered TFT. The regular `esp32dev` build remains TFT-disabled and has passed CI as well.


**User reports that they traced the actual ESP32 GPIO nets to their physical CN3 connector and attached the display following our GPIO plan.** Since the board's connector silk/schematic labels disagree, the **actual CN3 contact numbers have not yet been shared back**, so don't publish proposed physical CN3 numbers as proven. The trial uses ESP32 GPIO14 SCK, 23 MOSI, 13 CS, 33 LCD_RS/D-C, 19 LCD_RST. No touch/SD/MISO or display LED-control signal is configured. Confirm **actual LCD VCC voltage, source current capacity and common ground** before energizing, regardless of the user's signal-wiring checks.

### Firmware layout

- `platformio.ini`: existing `esp32dev` build stays TFT-disabled by default; separately selects **`esp32dev-tft`** using `pio run -e esp32dev-tft`. This pins and installs LovyanGFX `1.2.7` only in the experiment, build flag `-DEBTKS_TFT_ENABLE=1`.
- `ESP32_Z80_Emulator/ebtks_tft_test.h` provides a custom LovyanGFX `Panel_ST7796` + `Bus_SPI` on **VSPI_HOST** with remapped GPIO14 clock, 4 MHz SPI mode 0, DMA disabled, no display readback. It statically rejects any overlap with live HSPI disk-link GPIO22/17/16/32/27, UART GPIO1/3 or ESP32 boot strap pins. `EBTKS_TFT_ENABLE` defaults to **0** unless explicitly enabled.
- `ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino` performs one short **startup-only** draw after the Wi-Fi/terminal tasks and SPI read-cache initialization, but before the main CP/M bootstrap. Leaves static six-color bars, a border and text on the LCD; it does **not** add a TFT FreeRTOS worker or continuously redraw. Draw logs to `/z80debug` include heap before/after, but because the LCD's MISO is disconnected the firmware **cannot confirm visually successful output**.
- GitHub Actions on this feature branch runs the same native core/UI regressions, SPI-on/SPI-off builds, and a separate TFT-on firmware build, plus a static TFT wiring/isolation check. **Do not call it flash-ready until that TFT build finishes successfully.**

### First physical test checklist (once CI is green)

1. **Power off all components before making/adjusting connections.** Confirm the TFT VCC is supplied from an appropriate verified 5 V source (if this variant requires 5 V), common ground, and that no +5 V line reaches an ESP32 GPIO.
2. Flash *only ESP32* with `.pio/build/esp32dev-tft/firmware.bin` through the already-tested browser OTA updater; keep last-known-good `main` build for rollback. Do not flash the Teensy or copy the test binary over the baseline.
3. On restart, display should first briefly initialize, then show a **480×320 landscape** border, six vertical bars (red, green, blue, yellow, cyan, magenta) and the `EBTKS - ST7796 SPI TEST` legend. Static graphics remain while CP/M runs.
4. Confirm CP/M boots, keyboard/terminal, disk reads and Web UI continue working. Read `/api/system/health` (heap, SPI flags), `/api/cpm/spireads` (cache hit/read failures), and `/z80debug` (startup log) if there is a problem.
5. A dark screen alone does not prove an SPI problem: check actual TFT VCC, backlight, reset, DC, panel orientation and controller variant. Library is write-only; no hardware readback; the `LED` pin may be unconnected on this matching LCDwiki family if the board has automatic backlight control.
6. If abnormal ESP32 reset, Wi-Fi/CPM failure, or unstable SPI read-ahead occurs, immediately roll back to the ESP32 `main` binary; leave Teensy alone. Never merge the TFT experiment before the user tests it on physical hardware.

---


## 2026-10-19 MODULE IDENTIFIED AND PROVISIONAL CN3 PIN MAP — SUPERSEDES EARLIER MODULE-UNKNOWN GATE

User's actual arrived module photo clearly shows 14 header signals, bottom-to-top:
`VCC, GND, LCD_CS, LCD_RST, LCD_RS, SDI(MOSI), SCK, LED, SDO(MISO), CTP_SCL, CTP_RST, CTP_SDA, CTP_INT, SD_CS`.
Packaging explicitly states **ST7796, 320×480, SPI**. A 14-pin FPC connector mirrors this header. PCB visible: `LVC245A` logic chip, onboard regulator `U1`, backlight transistors. Pinout **strongly matches** LCDwiki's `3.5inch IPS SPI Module ST7796` (MSP3525/3526) and Elecrow 3.5/4-inch capacitive SPI models, **but the actual SKU and electrical supply specifications are not yet independently proven**. The header’s `CTP_*` labels correspond to optional capacitive touch **I²C**, not the LCD SPI data pins.

Pin functions and physical header numbering **from matching published module documentation**, not separately metered on this board: [LCDwiki 3.5-inch ST7796](https://www.lcdwiki.com/3.5inch_IPS_SPI_Module_ST7796), [LCDwiki 4-inch ST7796 capacitive](https://www.lcdwiki.com/4.0inch_Capacitive_SPI_Module_ST7796). Module head orientation is 1=bottom VCC; 14=top SD_CS when viewing user's photographed PCB lettering.

### PROPOSED display-only bring-up wire list (NOT authorized to connect until actual CN3 continuity and 5 V source checked)

Use the ESP32 GPIO matrix to put the *separate VSPI master* clock on **GPIO14**, deliberately **not GPIO18** because the supposed GPIO18 CN3-17 pad is physically the live disk-link GPIO17. LCD MISO, touch and LCD module SD card are omitted for this minimal write-only visual test.

| ST7796 header / pin | Function | Candidate EBTKS CN3 contact / GPIO | WROOM-32D module pad for meter | Status |
| --- | --- | --- | --- | --- |
| `VCC` / 1 | LCD module supply | **Regulated 5 V source outside CN3, subject to verified power budget** | — | **Power not yet approved**; 3.5 inch example specifies 5 V and approx 95 mA backlight |
| `GND` / 2 | Common ground | CN3-39 or -40 | — | Schematic proposes ground; confirm continuity |
| `LCD_CS` / 3 | Active-low display select | CN3-9 / GPIO13 (`E13`) | GPIO13 / module pad **16** | **Tentative; measure actual board first** |
| `LCD_RST` / 4 | Active-low LCD reset | CN3-18 / GPIO19 (`E19`) | GPIO19 / module pad **31** | **Tentative; measure**; use MISO-capable free GPIO as output instead |
| `LCD_RS` / 5 | D/C (data vs command) | CN3-3 / GPIO33 (`E33`) | GPIO33 / module pad **9** | **Tentative; measure** |
| `SDI(MOSI)` / 6 | LCD SPI data from ESP32 | CN3-22 / GPIO23 (`E23`) | GPIO23 / module pad **37** | **Tentative; measure** |
| `SCK` / 7 | LCD SPI clock, remapped VSPI | CN3-7 / GPIO14 (`E14`) | GPIO14 / module pad **13** | **Tentative; measure** |
| `LED` / 8 | LCD backlight control | Leave open for initial trial if backlight auto-enabled | — | Docs say may be unconnected if not controlling brightness; verify |
| `SDO(MISO)` / 9 | SPI data back from module | Leave unconnected | — | No LCD reads in first test |
| `CTP_SCL, CTP_RST, CTP_SDA, CTP_INT` / 10–13 | Capacitive touch controller | Leave unconnected | — | Touch deferred; I²C not SPI |
| `SD_CS` / 14 | LCD board's SD slot CS | Leave unconnected | — | Do not insert/use SD card |

**All CN3 contact numbers above are provisional suggestions based on the EBTKS V3.0 [published schematic](https://www.fliptronics.com/EBTKS/_downloads/edcd877f44e5f6331b8197916090c26a/Schematic_EBTKS_V3.0_2021-04-10_17-10-00.pdf), and should be considered possible locations to probe, NOT confirmed net assignments.** The previously verified live link's GPIO17↔CN3-17 contradiction shows that the board/revision/pin interpretation can differ from schematic. If a continuity test on any candidate fails, **STOP**, report which CN3 contact actually connects to the specified WROOM module pad; do not use the proposed number anyway. Verify pin-1 orientation physically before probing; connect/disconnect only while powered off.

**Power issue:** matching 3.5-inch LCDwiki module lists **working voltage 5.0 V**, backlight **95 mA**, power **0.5 W**; the 4-inch counterpart allows 5.0/3.3 V but recommends 5 V and specifies ~103 mA backlight. EBTKS CN3 supplies **3.3 V**, not a demonstrated 5 V power output. Do not assume 3.3 V regulator has 100+ mA spare current or that this particular LCD works at 3.3 V VCC. Supply the module from an appropriately regulated/current-rated 5 V rail after establishing the EBTKS/HP-85 source power budget, or a separate regulated 5 V supply with common ground and careful powering sequence. `U1`/the level shifter in the photo **support** the matching design but do **not** prove board-specific voltage ratings. Never apply 5 V directly to an ESP32 GPIO, and do not connect power until verified.

Suggested firmware pin config once verified (GPIO numbers, not CN3 contact numbers):
`spi_host=VSPI; SCLK=14; MOSI=23; MISO=-1; CS=13; DC=33; RST=19`.
No GPIO5/12/15 strap pin used and no overlap with active HSPI CP/M data link (GPIO22,17,16,32,27). First step is a **compile-time-disabled by default ST7796 test**, using an established driver such as pinned Bodmer/TFT_eSPI or LovyanGFX (both support ST7796), conservative 4 MHz SPI, no full-frame buffer, and on-demand test only; run CI OFF and ON builds before user flashing. Do not initialize display or connect any wires before confirming the five metered mappings and power.

---

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
4. Do not allocate a full RGB565 framebuffer: **this 320×480 module needs 307,200 bytes** for one 16-bit full frame (a 320×240 panel would need 153,600 bytes). Use small line/rectangle buffers or the display library's streaming draw API; the ESP32 heap has already shown fragmentation with Wi-Fi and SPI tasks. Verify runtime `/api/system/health` free/min/largest heap and `/api/cpm/spireads` for failures.
5. Compile/test an **OFF** configuration and a **TFT-ON** configuration with CI; check binary size/DRAM and CPUTask stack, and only then have the user flash as a hardware experiment. Ensure CP/M and disk reads still work when TFT rendering occurs; no changes to Teensy.
6. If electrically safe and fast enough, later consider rendering a mirror from the existing CRT display buffers. **Do not promise live full-screen CRT mirroring yet:** frame bandwidth, page state and CPU scheduling must be profiled.

**Stop gate:** Await exact TFT product/model and pinout before assigning physical wiring or committing display-driver code. A label such as just "SPI TFT" is not sufficient. Do not energize an unknown display or guess 5 V/3.3 V tolerance.

See `NEXT_CHAT_HANDOFF.md` top live checkpoint; historical CN3 proposal claims are not authoritative where physical measurements supersede them.
