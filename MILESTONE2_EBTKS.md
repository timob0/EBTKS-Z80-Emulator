# EBTKS Milestone 2

Milestone 2 keeps the proven Milestone 1 parallel Z80 system and moves its boot files and virtual disks onto the EBTKS Teensy SD card.

## Architecture

The ESP32 never touches the HP Series-80 bus. Z80 disk/file requests are converted into the existing EBTKS JSON/Base64 file protocol on UART1. The Teensy remains responsible for SD-card access and all HP-bus timing.

```
Z80 / CP/M virtual disk controller
        |
        v
ESP32 ebtks_storage.h
        |
 UART1 115200 8N1 (GPIO3/GPIO1)
        |
        v
Teensy ESPComms file services
        |
        v
Teensy SD card
```

The existing EBTKS browser `/ws` bridge and the Z80 storage client now share the UART through one request/reply mutex. Browser requests are queued, so a Teensy reply cannot be consumed by the wrong ESP32 client.

## SD-card layout

All CP/M-specific files now live below `/cpm` so they cannot collide with existing HP Series-80 disk images or other EBTKS files.

Copy the repository directory `SD Card Files/cpm/` to `/cpm/` on the Teensy SD card. Milestone 2 expects:

```
/cpm/boot.txt
/cpm/cboot.rom
/cpm/cpm22.rom
/cpm/cbios.rom
/cpm/disks/A.dsk
/cpm/disks/B.dsk      (optional)
...
/cpm/download/        (optional, for upstream file-transfer tools)
```

The supplied `/cpm/boot.txt` selects CP/M 2.2:

```
/cpm/cboot.rom  0000
/cpm/cpm22.rom  D000
/cpm/cbios.rom  E600
```

The existing root-level `/disks` directory is therefore left entirely for HP Series-80 disk images.

If `/cpm/boot.txt` cannot be opened, the ESP32 deliberately falls back to the built-in Z80 BASIC image used by Milestone 1.

## Transport details

The Teensy protocol accepts JSON requests terminated by carriage return and responds with JSON followed by a decimal CRC and CR/LF. Milestone 2 verifies that CRC before parsing any storage reply.

The Teensy receive line is limited to 512 bytes. Therefore 512-byte CP/M disk sectors are chunked:

- reads: up to 256 binary bytes per transaction
- writes: up to 192 binary bytes per transaction

The smaller write chunk keeps Base64 plus JSON comfortably below the Teensy request-line limit.

The current Teensy `write` service contains a historical case mismatch: it tests `write` but reads the payload from `Write.data`. The Milestone 2 client sends both keys, so the existing Teensy firmware can be used without changing that code.

## Browser and Wi-Fi services

Milestone 1 behaviour is preserved:

- `/` existing EBTKS home page / SPIFFS UI
- `/updater` browser firmware updater
- `/ws` normal EBTKS browser-to-Teensy JSON bridge
- `/z80` Z80/CP/M terminal
- `/z80ws` Z80 terminal WebSocket
- Telnet port 23 remains available

## Build

From the repository root:

```
pio run -t clean
pio run
```

The app-only OTA image is:

```
.pio/build/esp32dev/firmware.bin
```

## First boot diagnostics

On ESP32 diagnostic UART0 (GPIO25/GPIO26), Milestone 2 reports:

```
Probing Teensy-backed Z80 storage... ready
```

If that says `not available`, the emulator still starts but uses the Milestone 1 BASIC fallback. Check that `/cpm/boot.txt` is present on the Teensy SD card and that the Teensy ESP communications service is being polled.

### Current Teensy polling note

The current `ebtks-alt-firmware` main loop only calls `esp32.poll()` when `Remote CRT` is enabled. Milestone 2 therefore requires `Remote CRT` to remain enabled with the present Teensy firmware. A subsequent small Teensy-side cleanup should make ESP service polling unconditional so storage is independent of the CRT setting.
