> **Historical milestone document:** The active Media Manager now saves and verifies the drive mount table in ESP32 NVS namespace `cpmmount`, not `/cpm/mounts.cfg`. The SD config is read only as a migration source. See [PROJECT_STATUS.md](PROJECT_STATUS.md), [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md), and `ESP32_Z80_Emulator/cpm_mount_store.h` for the current implemented behavior. The original development notes below are retained for context.

# CP/M Media Manager

The CP/M Media Manager assigns disk-image files stored on the EBTKS Teensy SD card to CP/M drive letters without requiring the SD card to be removed or configuration files to be edited by hand.

## Storage layout

All CP/M media files remain in one authoritative location on the Teensy SD card:

```text
/cpm/disks/
```

The persistent drive assignment table is stored alongside the CP/M support files as:

```text
/cpm/mounts.cfg
```

The ESP32 keeps an active copy of the table in RAM while the Z80 is running. Editing a mount does not replace a disk underneath a live CP/M instance. Changes become active only after a virtual Z80/CP/M reset.

## Web interface

Open:

```text
http://<EBTKS-IP>/cpmmedia
```

The page shows:

- A: through P:
- the currently active image for each drive
- the image selected for the next CP/M reset
- all files found in `/cpm/disks`
- detected media type and size
- read/write state

Use **Mount** or **Unmount** to edit the saved table. Use **Apply changes / Reset CP/M** at the CP/M command prompt to restart only the Z80 virtual machine and activate the saved assignments. The HP-85, Wi-Fi and other EBTKS services remain running.

## Supported media profiles

### EBTKS 8 MB hard disk

Detected by an exact image size of 8 MiB. It uses the existing EBTKS CP/M geometry and remains read/write unless the SD filesystem reports the file as read-only.

### HP 82900 CP/M LIF floppy

Detected from the LIF volume header (`0x8000`) and `HPCP/M` volume label. The first 256 KiB of the LIF image are exposed as the CP/M block device using the original HP 82900 disk parameter block:

```text
SPT  = 32
BSH  = 3
BLM  = 7
EXM  = 0
DSM  = 243
DRM  = 127
AL0  = F0h
AL1  = 00h
CKS  = 32
OFF  = 3
```

HP LIF media are intentionally read-only in this first Media Manager milestone.

Files which do not match a supported profile are listed by the web page but cannot yet be mounted.

## First-boot migration

If `/cpm/mounts.cfg` does not exist, the ESP32 imports the historical filename convention automatically:

```text
/cpm/disks/A.dsk -> A:
/cpm/disks/B.dsk -> B:
...
```

Each file is classified from its actual size/signature. This means the stage-1 test arrangement with an 8 MiB `A.dsk` and HP LIF bytes stored as `B.dsk` migrates automatically. The resulting table is then written to `/cpm/mounts.cfg`.

If writing the new table fails, the imported legacy assignments are still used for that boot.

## Fast disk protocol

The ESP32 now includes the mounted image path in each dedicated 512-byte `disk_read` / `disk_write` request. The Teensy validates that paths stay below `/cpm/disks/` before opening them. Teensy firmware remains backward-compatible: a request without a path still maps the drive number to `/cpm/disks/A.dsk`, `/cpm/disks/B.dsk`, and so on.

## Mount-change safety

Apply mount changes only while CP/M is idle at a command prompt. The current CBIOS buffers a 512-byte host sector, so resetting in the middle of an application write could discard a pending buffered write.

Unmounted drives return erased (`E5h`) sectors rather than stale data. The current historical CBIOS does not have a robust host-error return path, so writes to unmounted or read-only media are rejected by the ESP32 but CP/M may not report the failure accurately. Do not use HP LIF drives as write destinations yet.

## Out of scope for this milestone

The Media Manager deliberately does not upload, download, rename or delete image files. Copy media files into `/cpm/disks` using the SD card for now. Bulk web transfer can be added after the ESP32/Teensy link gains the planned faster binary transport.
