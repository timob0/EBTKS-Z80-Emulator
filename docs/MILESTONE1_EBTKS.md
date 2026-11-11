# EBTKS Milestone 1

This milestone adapts the upstream ESP32 Z80 emulator to the EBTKS v3.x ESP32 hardware and proves the emulator/console side before adding Teensy-backed storage.

## Hardware target

Confirmed EBTKS ESP32:

- ESP32-D0WD-V3 revision 3.0
- 40 MHz crystal
- 4 MB flash
- diagnostic UART0 remapped to GPIO25/GPIO26
- Teensy link on UART1, RX GPIO3 / TX GPIO1, 115200 8N1

The Teensy UART is reserved by `ebtks_link.h` and is not used as the Z80/CP/M console.

## Milestone 1 behavior

- Z80 CPU remains on core 1.
- ESP-local SD access is disabled through a null backend; no SD-related GPIO is touched.
- `bootstrap()` therefore falls back to the upstream compiled-in monitor/BASIC image.
- Console output from the emulated UART goes to both Telnet and a WebSocket browser terminal.
- Browser keyboard input and Telnet input feed the emulated UART receive buffer.
- Existing EBTKS Wi-Fi provisioning is preserved: saved credentials are used first, otherwise the `EBTKS_AP` / `password` configuration AP is started.
- Existing EBTKS mDNS name `esp32_ebtks.local` is preserved.
- The browser firmware updater remains available at `/updater` and uses the same `Update` API/OTA-partition mechanism as the existing EBTKS ESP firmware.
- The original EBTKS Teensy JSON websocket remains at `/ws`.
- The Z80 terminal uses a separate websocket at `/z80ws`, so CP/M console bytes never enter the Teensy protocol stream.
- App-only OTA continues to leave the SPIFFS partition untouched.

## Web UI

The shared HTTP server now exposes:

- `/` - EBTKS home page. If the existing `/index.html` is present in SPIFFS it is served unchanged; otherwise an embedded fallback page is used.
- `/ws` - original EBTKS browser <-> Teensy JSON bridge.
- `/updater` - browser firmware updater. If `/updater.html` exists in SPIFFS it is used; otherwise an embedded fallback updater is served.
- `/z80` - Z80/CP/M browser terminal.
- `/z80ws` - dedicated binary websocket for Z80 terminal traffic.
- `/z80/status` - small emulator status endpoint.

The original EBTKS `style.css` is also served from SPIFFS when present.

## PlatformIO build

The repository root is a PlatformIO project. Open the repository folder itself in VSCode/PlatformIO and build the `esp32dev` environment.

`platformio.ini` targets the classic ESP32 Dev Module and uses the same async networking stack and WiFiManager fork used by the rebuilt EBTKS ESP32 firmware:

- `ESP32Async/AsyncTCP @ 3.3.2`
- `ESP32Async/ESPAsyncWebServer @ 3.6.0`
- `alanswx/ESPAsyncWiFiManager` at commit `3b9a58a031d32da908e205d60c7c58eedc8bd6ee`

The original Arduino sketch remains in `ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino`. PlatformIO builds that same sketch through `pio_src/main.cpp`.

From a shell at the repository root:

    pio run

The OTA application image will be generated at:

    .pio/build/esp32dev/firmware.bin

No serial upload port is hard-coded because the normal EBTKS workflow is to install `firmware.bin` through the existing web updater.

## Browser terminal

After the ESP joins Wi-Fi, open:

    http://<ESP-IP>/z80

The Z80 terminal WebSocket endpoint is `/z80ws`.

## Telnet terminal

For an independent bring-up path:

    telnet <ESP-IP> 23

## Milestone 2

Replace the null SD backend with an EBTKS storage client over the reserved Teensy UART so `/disks/A.dsk` etc. live on the Teensy SD card without changing the Z80 disk-controller interface.
