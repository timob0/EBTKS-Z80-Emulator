#!/bin/bash
set -e

PORT="/dev/cu.usbmodem141960601"

esptool --chip esp32 -p "$PORT" -b 115200 \
  --before no-reset --after no-reset \
  read-flash 0x8000 0x1000 flash-partitions.bin

python3 -c "import struct; b=open('flash-partitions.bin','rb').read(); [print('%-16s offset=0x%06X size=0x%X' % (x[5].split(bytes([0]))[0].decode(),x[3],x[4])) for i in range(0,len(b)-31,32) if b[i:i+2]==bytes.fromhex('AA50') for x in [struct.unpack_from('<HBBII16sI',b,i)]]"

