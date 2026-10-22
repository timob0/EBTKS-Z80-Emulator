#!/bin/bash
set -e

PORT="/dev/cu.usbmodem141960601"

test -f firmware.bin || {
    echo "ERROR: firmware.bin not found"
    exit 1
}

esptool --chip esp32 -p "$PORT" -b 115200 \
  --before no-reset --after no-reset \
  write-flash \
  0x10000 firmware.bin \
  0x150000 firmware.bin

