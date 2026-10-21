# ESP32 emergency recovery via the EBTKS Teensy USB bridge (macOS)

**Status: successfully used on the physical EBTKS/HP-85 system on 21 October 2026.**

This note records the proven procedure for restoring an ESP32 application
when the EBTKS Web UI and its **firmware updater are no longer functional**.
The recovery uses the **Teensy 4.1's built-in USB-to-ESP32 programming
bridge**, so no additional USB/UART adapter or board rewiring is needed.

This procedure preserves the ESP32 bootloader, partition table, Wi-Fi
configuration (NVS), OTA metadata and SPIFFS contents. It flashes only
the two application partitions.

## System and tools

- EBTKS v3.x with classic ESP32 and Teensy 4.1, using the Teensy's existing USB connection.
- macOS Teensy USB serial device: **\`/dev/cu.usbmodem141960601\`**.
  (The device name can change if the USB topology or Teensy re-enumeration changes.)
- \`esptool\` installed and accessible as a shell command, plus \`python3\` for
  inspecting the partition-table dump.
- ESP32 firmware **\`firmware.bin\` from the known-good \`main\` branch**, built with
  \`pio run -e esp32dev-tft\`. Do not recover with an unverified modem build.
- Two shell scripts: \`rdflash.sh\` and \`wflash.sh\`, reproduced below.

**Safety rule:** The \`firmware.bin\` file is *application-only*. Never flash it at
\`0x0000\`, never run \`erase-flash\`, and do not overwrite the bootloader,
partition table, NVS, OTA data or SPIFFS.

## 1. Prepare the firmware and the working directory

In the ESP32 repository, build the stable firmware:

\`\`\`bash
git fetch origin
git switch main
git pull --ff-only
pio run -e esp32dev-tft
\`\`\`

The application image is created as:

\`\`\`text
.pio/build/esp32dev-tft/firmware.bin
\`\`\`

Because \`wflash.sh\` refers to \`firmware.bin\` without a directory prefix,
run the scripts from the directory holding that file, for example:

\`\`\`bash
cd .pio/build/esp32dev-tft
# Copy rdflash.sh and wflash.sh into this directory if necessary.
ls -lh firmware.bin
\`\`\`

Check that this is the **stable main** build before proceeding.
The separate experimental modem branch remains unmerged and should not be
flashed as part of recovery.

## 2. Enable the Teensy's ESP32 programming bridge

1. Connect the Mac to the **Teensy 4.1 USB port** (not directly to the ESP32).
2. Open the EBTKS **USB service console** on
   \`/dev/cu.usbmodem141960601\` at **115200 baud**.
   Keep the HP-85 idle: while the programmer bridge is active, the Teensy's
   normal EBTKS polling/services are suspended.
3. At the \`EBTKS>\` service prompt enter:

   \`\`\`text
   ESP32 Prog
   \`\`\`

4. The Teensy takes the ESP32 into ROM download mode via its ESP32 EN/BOOT
   control signals. The console should display ESP32 ROM startup text similar
   to \`DOWNLOAD_BOOT\` and then:

   \`\`\`text
   Ready to disconnect Tera Term and run esptool.py....
   \`\`\`

5. **Completely close/disconnect the serial terminal** so \`esptool\` can
   open the same USB device. No second serial program can hold that port.

The bridge sends data between the Teensy USB serial connection and the
ESP32 hardware UART at 115200 baud. It automatically leaves programming
mode and reboots the ESP32 after **30 seconds without incoming serial
activity**. Have each \`esptool\` command ready before entering \`ESP32 Prog\`.
If the bridge times out between steps, repeat this section. The explicit
\`--before no-reset --after no-reset\` arguments are important because
the Teensy, not esptool DTR/RTS, controls ESP32 boot mode.

Source: \`timob0/ebtks-alt-firmware/src/ESP_Programmer.cpp\` and the
\`ESP32 Prog\` service-console entry in \`src/EBTKS_Utilities.cpp\`.
The bridge implementation is the same in the tested Teensy
\`feature/cpm85-keyboard-forwarding\` and \`main\` sources.

## 3. Download and inspect the ESP32 partition table

Create **\`rdflash.sh\`** with exactly these commands:

\`\`\`bash
# download partition table
esptool --chip esp32 -p /dev/cu.usbmodem141960601 -b 115200 --before no-reset --after no-reset read-flash 0x8000 0x1000 flash-partitions.bin

# display partition info
python3 -c "import struct; b=open('flash-partitions.bin','rb').read(); [print('%-16s offset=0x%06X size=0x%X' % (x[5].split(bytes([0]))[0].decode(),x[3],x[4])) for i in range(0,len(b)-31,32) if b[i:i+2]==bytes.fromhex('AA50') for x in [struct.unpack_from('<HBBII16sI',b,i)]]"
\`\`\`

After step 2 has placed the ESP32 in download mode and the terminal is closed,
run from the working directory:

\`\`\`bash
bash rdflash.sh
\`\`\`

The first command **reads**, without writing to flash, the 4096-byte region
at \`0x8000\` into \`flash-partitions.bin\`. The second command decodes the
32-byte ESP32 partition entries and prints their offsets and sizes.

### Actual partition output from this ESP32

\`\`\`text
nvs              offset=0x009000 size=0x5000
otadata          offset=0x00E000 size=0x2000
app0             offset=0x010000 size=0x140000
app1             offset=0x150000 size=0x140000
spiffs           offset=0x290000 size=0x170000
\`\`\`

| Partition | Offset | Size | Recovery action |
|---|---:|---:|---|
| NVS | \`0x009000\` | \`0x5000\` | Leave intact (Wi-Fi/configuration) |
| OTA metadata | \`0x00E000\` | \`0x2000\` | Leave intact |
| App 0 | \`0x010000\` | \`0x140000\` (1,280 KiB) | Flash stable application |
| App 1 | \`0x150000\` | \`0x140000\` (1,280 KiB) | Flash stable application |
| SPIFFS | \`0x290000\` | \`0x170000\` | Leave intact (web assets) |

**Always compare the actual output before flashing.**
This table is specific to the recovered ESP32. In particular, SPIFFS is
\`0x170000\`, not \`0x160000\`. If the physical partition offsets or slot sizes
do **not** match these values, do not run the fixed-offset write script.

Also ensure that the application image fits within each \`0x140000\`-byte
application slot.

## 4. Re-enter programming mode and reflash both applications

The partition-read command may have allowed the Teensy service programmer
to time out. **Reopen the Teensy service console, enter \`ESP32 Prog\`
again, confirm download mode, and close the terminal** before proceeding.

Create **\`wflash.sh\`** with the tested command:

\`\`\`bash
# Loads the firmware.bin to both application partitions - verify offsets with rdflash before!
esptool --chip esp32 -p /dev/cu.usbmodem141960601 -b 115200 --before no-reset --after no-reset write-flash 0x10000 firmware.bin 0x150000 firmware.bin
\`\`\`

Make sure the current directory contains the **known-good** \`firmware.bin\`
and that the partition table matches the output in section 3. Then:

\`\`\`bash
bash wflash.sh
\`\`\`

This installs the **same stable application image** in both OTA slots.
Whichever slot the retained OTA metadata selects, the ESP32 will boot the
stable application. **No erasure or modification of any other partition
is required.**

Keep the USB connection and power stable during the write. Inspect the
\`esptool\` output and proceed only if it reports successful completion/
verification. If it fails, do not format or erase flash; correct the
connection/bridge state and retry the same controlled procedure.

## 5. Leave programming mode and verify recovery

1. Close \`esptool\`/any serial program and allow the Teensy bridge's
   **30-second idle timeout** to reset the ESP32 into normal run mode.
2. Confirm that the ESP32 reconnects to Wi-Fi.
3. Recheck the ESP32 Web UI:
   - \`/z80\` — CP/M/Z80 terminal
   - \`/cpmmedia\` — CP/M disk Media Manager
   - \`/crt\` — HP-85 remote CRT display
   - \`/updater\` — ESP32 firmware updater
4. Confirm existing CP/M disk access, TFT output and HP-85 keyboard
   bridge still behave as before.

**Result on 2026-10-21:** Recovery was successful and the ESP32 returned
to the known-good firmware. The experimental modem firmware caused a
serious Web UI/OTA regression and remains **unmerged**; its exact runtime
failure mechanism has not yet been conclusively established.

## Troubleshooting and precautions

- **Serial port busy:** Exit/disconnect the Teensy serial console completely
  before starting \`esptool\`.
- **Cannot connect to ESP32 ROM bootloader:** Re-enter \`ESP32 Prog\` and
  run the command promptly before the 30-second idle timeout.
- **Incorrect working directory:** The scripts use relative paths:
  \`flash-partitions.bin\` is written/read in the current directory, and
  \`wflash.sh\` expects \`firmware.bin\` there too.
- **Another serial device name:** Verify the Teensy USB device path on macOS,
  then edit the \`-p\` argument in both scripts as appropriate.
- **Partition mismatch:** Stop; do not assume hard-coded offsets from
  a different ESP32 or partition scheme.
- **Different esptool major versions:** This procedure uses the
  \`read-flash\`, \`write-flash\` and \`no-reset\` spelling from the
  successfully used installation. Older versions may use underscores.
- **Never run \`erase-flash\`; never flash \`firmware.bin\` at \`0x0000\`.**
  A full factory image is a different artifact requiring the correct
  bootloader and partition table.

Related project files: \`GETTING_STARTED.md\`,
\`NEXT_CHAT_HANDOFF.md\`, \`platformio.ini\`.
