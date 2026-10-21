# ESP32 recovery without working Web OTA (EBTKS v3.x)

**Problem observed on HP-85 hardware (2026-10-21):** New experimental
`feature/cpm-wifi-modem` ESP32 firmware caused `/z80`, `/crt` and
`/cpmmedia` to terminate HTTP connections. Even firmware updater POST
fails, so do **not** rely on OTA to recover. Root cause may be runtime heap
starvation/fragmentation, not yet conclusively demonstrated by live metrics.
Native tests and firmware CI cannot validate web stability at runtime.

**Known-good source:** ESP32 `timob0/EBTKS-Z80-Emulator` `main` with full
ST7796 TFT, merged physical HP keyboard, CP/M, Teensy SD/SPI and Web UI.
Build with `pio run -e esp32dev-tft` and preserve the resulting
`.pio/build/esp32dev-tft/firmware.bin` application image. It is an
**application-only image**; never flash it at 0x0000, and do not erase flash.

## Built-in EBTKS recovery programmer

Verified by inspection of `timob0/ebtks-alt-firmware/src/ESP_Programmer.cpp`
(same blob on Teensy `main` and `feature/cpm85-keyboard-forwarding`) and
`src/EBTKS_Utilities.cpp`: the Teensy 4.1 USB service console exposes
**`ESP32 Prog`**, which brings ESP32 GPIO0/EN to ROM DOWNLOAD mode
and bridges the Teensy USB CDC serial port to ESP32 UART0 via Teensy
Serial2 at **115200 baud**. Service terminal message:
`Ready to disconnect Tera Term and run esptool.py....`.
Close the serial terminal before using the same USB serial port with
`esptool`. The bridge automatically resets ESP32 into normal run mode
**after 30 seconds of inactivity**; prepare each next command first.
DTR/RTS auto-reset does not control this special bridge: use esptool
`--before no-reset --after no-reset` (esptool 5 spelling) or
`--before no_reset --after no_reset` (esptool 4 spelling).
Leave the HP-85 idle: while this bridge runs the Teensy's normal EBTKS
polling/services are suspended.

### Serial recovery procedure (esptool v5)

1. Identify Teensy USB CDC port, such as `COM4` on Windows or
   `/dev/ttyACM0` on Linux. Install esptool as needed
   (`python -m pip install esptool`). Substitute actual port below.
2. Build ESP32 `main` TFT firmware. Connect to Teensy USB service
   console, send `ESP32 Prog`, wait for download-mode message, then
   immediately **close the terminal**.
3. First read the live partition table; this is **read only**:
   ```sh
   python -m esptool --chip esp32 -p COM4 -b 115200 --before no-reset --after no-reset read-flash 0x8000 0x1000 flash-partitions.bin
   ```
   If the utility says the port is busy, close the serial terminal.
4. Decode it on your computer:
   ```python
   from pathlib import Path
   import struct
   buf=Path("flash-partitions.bin").read_bytes()
   for i in range(0,len(buf),32):
       if buf[i:i+2]!=bytes.fromhex("aa50"): break
       _,type_,sub,offset,size,label,flags=struct.unpack_from("<HBBII16sI",buf,i)
       print(label.split(bytes([0]))[0].decode("ascii"),hex(offset),hex(size))
   ```
   For this project's standard Arduino/PlatformIO 4MB default layout,
   verify BOTH OTA app slots really are `app0/ota_0 0x10000 0x140000`
   and `app1/ota_1 0x150000 0x140000`. The partition table on the
   **physical ESP32** controls; if it differs, STOP and adjust recovery
   plan instead of assuming these offsets.
5. Enter **`ESP32 Prog` again** (the read command may have allowed
   the programmer's 30-second idle timeout to expire). Close terminal.
   Once partition table matches, program the **same known-good application
   binary into both OTA slots**:
   ```sh
   python -m esptool --chip esp32 -p COM4 -b 115200 --before no-reset --after no-reset write-flash 0x10000 .pio/build/esp32dev-tft/firmware.bin 0x150000 .pio/build/esp32dev-tft/firmware.bin
   ```
   This preserves existing NVS Wi-Fi configuration, OTA metadata, SPIFFS,
   bootloader and partition table. ESP32 boot can choose either application
   slot, both now contain stable firmware. Do not add `erase-flash`.
6. After esptool reports write completion/verification, close esptool
   and leave the Teensy USB serial port idle until its programmer timeout
   resets ESP32 into normal firmware. Reopen Web UI `/z80`, `/cpmmedia`,
   `/crt`, `/updater` and validate the old CP/M/HP85/TFT use cases.

With older esptool v4 use underscore forms `no_reset`, `read_flash`,
`write_flash`; v5 hyphen forms above. Avoid `pio run -t upload` because
its usual DTR/RTS reset assumptions are not the EBTKS USB serial bridge.
Do not update Teensy firmware, HP option ROM, SD images or reset NVS.

**Status:** this is verified from project source and ESP flash layout
references, but the actual physical rescue has not yet been performed.
**Once recovered:** keep experimental modem branch **unmerged**, do not flash
it again until web/UI runtime stress has been addressed and appropriate
memory telemetry can be collected.
