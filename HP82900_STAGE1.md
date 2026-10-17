# HP 82900A compatibility - Stage 1

Stage 1 keeps the existing EBTKS CP/M system on drive A: unchanged and exposes an original HP 82900A 5.25-inch CP/M LIF image as read-only drive B:.

## SD-card preparation

Copy one of the original HP CP/M LIF disk images to the Teensy SD card as:

```
/cpm/disks/B.dsk
```

The file contents remain the original `.lif` image. Only the filename is changed because the existing Teensy fast-disk service maps Z80 drive letters to `A.dsk`, `B.dsk`, etc.

Keep the original image backed up. Stage 1 rejects drive-B writes in the ESP32 firmware.

## Geometry

The HP 82900A 5.25-inch CP/M format uses:

```
SPT = 32
BSH = 3
BLM = 7
EXM = 0
DSM = 243
DRM = 127
AL0 = F0h
AL1 = 00h
CKS = 32
OFF = 3
```

The complete CP/M block device is the first 256 KiB of the LIF image. The current EBTKS BIOS uses 512-byte host sectors and calculates its host LBA with 32 host sectors per track. For drive B only, the ESP32 translates:

```
biosSector = track * 32 + hostSector
rawSector  = track * 8  + hostSector
```

where `hostSector` is 0..7 for the HP format.

The HP DPB and 32-byte checksum vector are installed at runtime in the unused `FD87h..FFE7h` filler area of the currently supplied `cbios.rom`, and only drive B's DPH is repointed. The firmware checks the expected original DPH bytes before applying the patch.

## Read-only limitation

Drive B is deliberately protected during Stage 1. The existing generic BIOS does not yet propagate host write failures back to CP/M, so do not use B: as a destination even though the ESP32 rejects the physical write.

This stage is intended for directory access, copying files from B: to A:, and running copied applications.

## Initial test sequence

After booting the normal EBTKS CP/M system:

```
A>DIR B:
A>STAT B:DSK:
```

Then copy one or more applications from B: to the existing writable A: disk. For example, if present on the selected image:

```
A>PIP A:MBASIC.COM=B:MBASIC.COM[V]
A>PIP A:ZORK1.COM=B:ZORK1.COM[V]
A>PIP A:ZORK1.DAT=B:ZORK1.DAT[V]
```

Run the copied applications from A:. This separates media/geometry compatibility from HP-specific application compatibility.

## Later stages

Once Stage 1 is proven:

1. test a broader set of HP CP/M applications;
2. add a proper mixed-geometry writable BIOS path if useful;
3. load the original HP resident CCP/BDOS/BIOS image at `D800h..FFFFh` and emulate the HP 82900-specific I/O interface;
4. finally add the original 2 KiB `CPMROM.bin` cold-boot path.
