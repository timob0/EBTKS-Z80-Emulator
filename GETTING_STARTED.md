# Getting started: EBTKS Z80 CP/M 2.2

**Installation and first-use guide — 17 October 2026**

This guide is for owners of an **EBTKS v3.x board with its original Teensy 4.1 and classic ESP32**, installed in an HP Series-80 computer (tested on the HP-85). It installs our **parallel Z80/CP/M emulator**, not HP's original HP 82900A CP/M boot ROM.

The HP-85 keeps running independently. The Teensy continues servicing the HP computer and owns the SD card; the ESP32 runs Z80/CP/M, serves the browser terminal and Media Manager, and offers Telnet access. No extra ESP32 SD card, Z80 processor, display or serial terminal is needed.

**This is a practical guide to the tested integrated build, not the historical standalone ESP32 emulator described farther down in the original README.** For technical design and regression notes see [PROJECT_STATUS.md](PROJECT_STATUS.md) and [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md).

## 1. What you need

- A working EBTKS v3.x / Teensy 4.1 / ESP32 installation with an SD card and access to its existing web firmware updater.
- A computer with Git and [PlatformIO](https://platformio.org/) (CLI or VS Code extension), USB access to the **Teensy** for its firmware upload, and a web browser.
- A 2.4 GHz Wi-Fi network reachable by the ESP32 (or another network supported by its Wi-Fi manager).
- Two source repositories:
  - **Teensy:** [`timob0/ebtks-alt-firmware`](https://github.com/timob0/ebtks-alt-firmware), PlatformIO environment `teensy41`.
  - **ESP32 Z80 emulator:** [`timob0/EBTKS-Z80-Emulator`](https://github.com/timob0/EBTKS-Z80-Emulator), PlatformIO environment `esp32dev`.

The ESP32 application baseline [`5f586f3`](https://github.com/timob0/EBTKS-Z80-Emulator/commit/5f586f35197627319e7b8280ef62e3807b6ceee3) was tested on real hardware on 2026-10-17. Subsequent documentation-only commits do not alter that tested firmware. Hardware variants other than the stated EBTKS setup have not been validated.

**Before you begin:** Back up the existing Teensy SD card, especially its root `CONFIG.TXT`, HP disk images, and any `/cpm/disks/*.dsk` files containing user data. Make sure you can recover/flash your existing firmware before attempting an update. Never remove the card while EBTKS is powered.

## 2. Flashing order: Teensy first, ESP32 second

There are **two independent firmware images**. Updating the ESP32 does **not** update the Teensy, and vice versa. The suggested first-time order is:

**Step 1 — Build and flash the Teensy 4.1 firmware**

```sh
git clone https://github.com/timob0/ebtks-alt-firmware.git
cd ebtks-alt-firmware
pio run -e teensy41
```

The Teensy application output is normally `.pio/build/teensy41/firmware.hex`.

Connect the Teensy 4.1 to your host by USB and use the normal Teensy/PlatformIO upload procedure:

```sh
pio run -e teensy41 -t upload
```

Depending on the Teensy Loader/USB connection, its physical Program button may be needed. Follow the normal EBTKS/Teensy programming precautions for your board. **Do not upload the ESP32 `firmware.bin` to the Teensy.**

The companion Teensy firmware adds the necessary file/sector services, including the explicit disk-image paths required by the Media Manager. Keep your HP-85-specific `CONFIG.TXT` settings; do not replace your existing working configuration with the repository's sample wholesale.

**Step 2 — Prepare the Teensy SD card**

With the EBTKS powered off, mount the Teensy SD card on your host computer.

In the SD card's **root `CONFIG.TXT`**, ensure the setting is:

```json
"CRTRemote": true
```

Keep the rest of the JSON configuration intact and valid. **This is currently required:** in the tested Teensy code, ESP32 communication servicing (`esp32.poll()`) is conditional on the **Remote CRT** setting. A sample configuration in the Teensy repo may still have `CRTRemote` set to `false`. With it disabled, CP/M may fall back to BASIC or SD operations may not work, even if both firmware images are flashed.

Then copy the directory [`SD Card Files/cpm/`](SD%20Card%20Files/cpm/) from the **ESP32 repository** into the **root of the Teensy SD card**. Keep the `/cpm/` folder structure and the provided `A.dsk` file intact. Section 3 shows the resulting layout.

Eject the card safely, put it back in the EBTKS and start the HP/EBTKS system. Check that ordinary HP-85 EBTKS functionality still works.

**Step 3 — Build the ESP32 firmware**

```sh
git clone https://github.com/timob0/EBTKS-Z80-Emulator.git
cd EBTKS-Z80-Emulator
pio run -e esp32dev -t clean
pio run -e esp32dev
```

The firmware artifact is:

```text
.pio/build/esp32dev/firmware.bin
```

This is an **ESP32 application-only OTA image**, not a combined factory flash image. It relies on the pre-existing EBTKS ESP32 bootloader, partition layout and working web updater. **Do not flash this app-only file at address 0x0000 on a blank ESP32.**

**Step 4 — Flash the ESP32 through the EBTKS web updater**

If your ESP32 already has the EBTKS web UI:

1. Connect to the EBTKS IP address or open `http://esp32_ebtks.local/` if mDNS resolves on your network.
2. Open **Firmware Updater**, or go directly to `http://<EBTKS-IP>/updater`.
3. Select the **ESP32** file `.pio/build/esp32dev/firmware.bin`.
4. Submit the update and let the ESP32 reboot. Do not interrupt power or update the Teensy in the middle.
5. Reopen the EBTKS home page and select **Z80 / CP/M Terminal**.

The application update preserves the existing ESP32 SPIFFS web assets. If the SPIFFS home/updater pages are missing, an embedded fallback page is provided.

**First-ever ESP32 flash / no working updater?** The application-only image is insufficient to provision a completely blank ESP32. EBTKS provides a board-specific Teensy USB-to-ESP32 serial programming bridge (see `src/ESP_Programmer.cpp` in the Teensy firmware), but the first-time bootloader/partition flashing procedure is **not verified in this guide**. Consult the EBTKS hardware flashing instructions or prepare the correct combined image before proceeding; do not guess ESP32 flash offsets.

**Step 5 — Wi-Fi configuration**

The ESP32 normally reconnects using stored credentials. If it has none, the code starts a Wi-Fi configuration access point named `EBTKS_AP` with password `password`. Join it, use the Wi-Fi manager's setup page to select your own network, and then reconnect your browser to the assigned EBTKS IP address. You may also try `http://esp32_ebtks.local/`; if mDNS is not available on your computer/network, find the address in your router's client list. Keep the firmware updater on a trusted local network, not exposed to the internet.

## 3. SD-card filesystem explained

The **physical SD card belongs to the Teensy** and already contains your normal EBTKS/HP Series-80 files. The CP/M subsystem adds a separate `/cpm/` directory, without relocating the HP files.

An illustrative card layout after setup:

```text
TEENSY SD CARD ROOT
|
+-- CONFIG.TXT                    EBTKS / Teensy configuration
+-- disks/                       Existing HP-85/HP Series-80 media
|   +-- [your HP disk images]    Not CP/M A:/B: images
|
+-- roms85/                      Existing HP-85 option ROMs, if used
+-- tapes/                       Existing tape images, if used
|
+-- cpm/                         Parallel ESP32 Z80/CP/M system
    +-- boot.txt                 Boot-image filenames and load addresses
    +-- cboot.rom                Z80 initial jump code
    +-- cpm22.rom                Native CP/M 2.2 CCP + BDOS
    +-- cbios.rom                Native EBTKS CBIOS
    |
    +-- disks/                   CP/M virtual disks (arbitrary filenames)
    |   +-- A.dsk                Supplied native 8 MiB disk image
    |   +-- cpm_1.lif            Optional original HP CP/M floppy
    |   +-- cpm_2_MP.lif         Optional original HP Multiplan floppy
    |   +-- cpm_3_f80.lif        Optional original HP Fortran floppy
    |   +-- WORK.DSK             Example of another native 8 MiB image
    |
    +-- download/                Optional CP/M file-transfer working area
```

Only the boot files, `/cpm/disks/A.dsk`, and the `/cpm/disks` directory are supplied in the ESP32 repository. The named HP `.lif` files above are **examples**, not bundled downloads; users must supply their own legally obtained original images. `WORK.DSK` and `download/` are illustrative/optional.

The supplied `/cpm/boot.txt` loads:

```text
/cpm/cboot.rom  0000
/cpm/cpm22.rom  D000
/cpm/cbios.rom  E600
```

**Do not confuse `/disks/` and `/cpm/disks/`.** The former belongs to the HP side of EBTKS; the latter belongs to the **parallel** Z80/CP/M virtual drives.

### Native 8 MiB `.dsk` images

The repository provides a ready-made `/cpm/disks/A.dsk`, **8,388,608 bytes (8 MiB)**, which is the writable native EBTKS CP/M hard-disk profile. The emulator recognizes native images by this **exact file size**, rather than requiring a particular drive-letter filename. You may make a backup copy or duplicate this image to create another native virtual disk (e.g. `WORK.DSK`) and mount it to another letter. Cloning the supplied formatted image is easier and safer than creating an arbitrary 8 MiB zero-filled file, which would not necessarily contain a usable CP/M filesystem.

The SD-card file itself is a **block-device image**, not a directory of individual CP/M files. A PC file manager will normally show only the `.dsk` container, whereas `DIR` inside CP/M shows files within the image. To copy between CP/M drives, use CP/M programs such as `PIP` if present.

The Media Manager can choose a disk image for a CP/M drive, but it **does not upload, download, create, rename or delete SD-card image files**. For those operations, power down and use a computer to manage the Teensy SD card.

### Original HP 82900 CP/M LIF floppies

The other supported profile is an original **HP 82900A CP/M LIF floppy image**. The images tested were **630,784 bytes** each. Identification is based on the LIF header signature `0x8000`, the volume label `HPCP/M`, and a plausible size (at least 256 KiB, less than 2 MiB); merely renaming an arbitrary disk to `.lif` does not make it compatible.

Important differences:

| Feature | Native EBTKS hard disk | HP 82900 LIF floppy |
|---|---|---|
| Example file | `A.dsk` | `cpm_2_MP.lif` |
| Typical size | **Exactly 8,388,608 bytes** | **630,784 bytes** in tested originals |
| Media detection | Exact size | LIF signature, label and size |
| CP/M access | Read/write (if underlying SD file permits) | **Read-only** |
| Geometry | EBTKS native DPB | HP-specific DPB and track mapping |
| Intended use | Working drive; install programs/data | Read original floppy, copy programs to native writable drive |

The emulator maps the **first 256 KiB from byte offset 0** of each HP LIF image as the logical CP/M medium, including the HP-reserved tracks; it does **not** extract the LIF `E010` entry and mount that extracted payload. Copy the *complete, unchanged* original LIF image to `/cpm/disks/`.

The HP disk parameter block is `SPT=32`, `BSH=3`, `BLM=7`, `EXM=0`, `DSM=243`, `DRM=127`, `AL0=F0h`, `AL1=00h`, `CKS=32`, `OFF=3`. The disk mapper converts HP host sectors to the correct physical offsets. Several HP images can be mounted on different drives simultaneously; the firmware patches each drive's CBIOS geometry separately.

**Do not attempt to write to LIF drives.** The ESP32 prevents physical writes, but the historical CBIOS may not report an error to the CP/M application reliably. For installation, copy files from a LIF drive to the native R/W `A:` drive first. Original HP ROM booting is a **different, future compatibility project**; mounting HP LIF disks does not require or start the HP CP/M BIOS.

### Where drive assignments are saved

The **current** Media Manager stores the drive A:–P: assignment table in the ESP32's nonvolatile storage (NVS), namespace **`cpmmount`**. The old SD file `/cpm/mounts.cfg`, if present, is only a first-time migration source. Otherwise the first run imports legacy names such as `A.dsk` into matching drive letters. Later rename/mount actions persist to NVS; they do not rename the underlying files.

## 4. Web UI quick tour

Use `http://<EBTKS-IP>/` as the home page.

| Page / service | What it does |
|---|---|
| `/` | Original EBTKS landing page; launch Z80 terminal, Media Manager, Remote CRT, updater and SD test |
| `/z80` | **Primary CP/M terminal**: mode selector, Clear, Reset Z80, Diagnostics, navigation to Media Manager/CRT/home |
| `/cpmmedia` | List CP/M drive letters A:–P:, active vs next-boot image and available `/cpm/disks` media; Mount / Unmount / Apply changes |
| `/crt` | Remote **HP-85** CRT view; not the Z80 terminal |
| `/sdtest` | Test the ESP32-to-Teensy SD file-service connection |
| `/updater` | Install a new ESP32 **app-only `firmware.bin`** without separately reflashing the Teensy |
| `/z80debug` | Read Z80/ESP diagnostics buffered in RAM, rather than a hardware UART |
| `/z80debug-clear` | Clear those diagnostics |
| `/ws`, `/z80ws` | Existing EBTKS browser bridge and separate Z80 terminal WebSocket endpoints |

**Terminal tips:** Select **VT100** in `/z80`, click into the black terminal area to focus the keyboard, then press Enter if the CP/M prompt has not appeared yet. The browser supports Raw, VT100 and experimental HP-86/82900 modes; **VT100 is the recommended and tested mode** for full-screen Microsoft Multiplan. In HP-86 mode, the current implementation's arrow keys do not reliably navigate Multiplan. Switching terminal display modes is not the same as changing an application's terminal installation setting.

The **Clear** button only clears the *browser's current terminal display*; it does not reset CP/M or erase disk contents. **Reset Z80** restarts the *virtual Z80 machine*, not the HP-85. **Diagnostics** opens a view of the RAM-backed log.

**Media Manager walkthrough:**

1. Put complete supported `.dsk` or `.lif` image files in `/cpm/disks/` on the Teensy SD card while the EBTKS is powered off.
2. Open `/cpmmedia`. Click **Refresh** to inspect available media; select the image for each drive and use **Mount** (or **Unmount**) to save the pending assignment.
3. Make sure CP/M is idle at the `A>` prompt; **never apply/reset during an active disk write**.
4. Click **Apply changes / Reset CP/M**. This restarts only the Z80 and makes the saved drive table active.
5. Return to `/z80`; `DIR B:` or `DIR C:` should reflect the newly mounted medium.

A drive can have any allowed filename under `/cpm/disks`; the file need not be named after its drive letter. A mount change is not live until Apply/Reset.

**Telnet alternative:** Connect to TCP port 23, for example `telnet <EBTKS-IP> 23`, using a VT100-capable terminal. Telnet and the browser can be connected concurrently, displaying the same virtual Z80 console output. Opening Telnet **does not reset** CP/M; Telnet is not required for complete browser output.

## 5. First-boot walkthrough

1. After installing both firmwares, copying `/cpm`, and setting `CRTRemote=true`, open `http://<EBTKS-IP>/z80`, select **VT100**, and press Enter.
2. Confirm CP/M 2.2 boots and a drive prompt appears (typically `A>`). The boot ROM files remain on the Teensy SD card.
3. Try:
   ```text
   A>DIR
   A>DIR B:
   ```
   The second command requires a mounted B: disk; if only the supplied `A.dsk` is installed, B: may not contain anything.
4. Open `/cpmmedia`. Confirm `A.dsk` is recognized as **EBTKS 8 MB hard disk [R/W]**. For optional HP LIF media, mount each image as **HP 82900 CP/M floppy [R/O]**, Apply/Reset, and retry `DIR B:`.
5. If `PIP.COM` and a particular source file exist on the mounted disks, you can copy a CP/M file from B: to writable A:, for example:
   ```text
   A>PIP A:EXAMPLE.COM=B:EXAMPLE.COM[V]
   ```
   Replace `EXAMPLE.COM` with an actual filename visible in `DIR B:`. Do not assume any particular utility or application is on every disk.
6. Optionally try a CP/M application already present, such as `MBASIC`, `Zork` or an installed **VT100-configured** `MP` (Microsoft Multiplan). Exiting Multiplan and INSTALL should return directly to CP/M without a complete re-boot from SD.
7. Verify the original HP-85 continues to work while CP/M is running.

## 6. Troubleshooting

| Symptom | Things to check |
|---|---|
| Cannot open EBTKS web page | Wi-Fi credentials, `EBTKS_AP` provisioning, router IP, and mDNS availability; try IP instead of `esp32_ebtks.local` |
| Z80 starts built-in BASIC instead of CP/M | Check `/cpm/boot.txt`, all three ROM files, the updated Teensy firmware and `"CRTRemote": true` |
| `/sdtest` cannot communicate with Teensy | Ensure **both** firmwares are updated, Teensy SD card works and the Teensy ESP polling setting is active |
| A: or B: does not appear | Inspect `/cpmmedia` for unsupported image size/signature, missing file or pending assignment; Apply/Reset while idle |
| HP LIF image listed as unknown | Verify full original file, `HPCP/M` header, size and placement under `/cpm/disks`; an ordinary HP LIF disk is not necessarily an HP 82900 **CP/M** disk |
| Multiplan screen/keys are wrong | Use the **VT100-installed** Multiplan and browser **VT100** mode; do not mix its HP-86 installer profile with a VT100 console |
| CP/M screen is blank after opening page | Focus the terminal; press Enter. WebSocket displays live output, not an automatically replayed screen snapshot |
| Device says `Z80 halted @ ...` after clicking Reset | The log's wording is ambiguous; an intentional Web/Media Manager reset can produce it |
| Application exit causes a full SD boot each time | Possible warm-boot regression; check `/z80debug` for `CP/M warm boot: restored D000-E5FF` instead |
| Missing DIR entries or partial Multiplan screen | Use firmware **at least** the tested `5f586f3` baseline; earlier builds dropped browser output unless Telnet was connected |

Useful successful diagnostics at `/z80debug` include:

```text
Probing Teensy-backed Z80 storage... ready
CP/M warm boot shadow: captured 5632 bytes D000-E5FF
CP/M warm boot: BDOS base preserved at D806; function-0 shim active
Starting Z80
```

Normal program exits may print `CP/M warm boot: restored D000-E5FF (count=N)` without reloading all three CP/M ROM files. **Never debug the ESP32–Teensy hardware link by sending text to GPIO1/GPIO3**: this serial connection is exclusively for the inter-processor protocol.

## 7. Current limitations

- HP LIF CP/M images are R/O; unknown CP/M geometries are not mounted.
- The Media Manager changes **mount assignments**, not SD-card file contents or filenames. Image uploads/downloads still require SD-card access on a computer.
- HP-86 browser keyboard navigation remains experimental; VT100 is recommended.
- The original HP 82900 ROM/BIOS boot process has **not** been emulated.
- The planned 4-inch SPI TFT / touchscreen is **not yet part of this firmware**.
- The ESP32 updater is for application-only OTA on a **previously provisioned ESP32**, not a blank factory image.

For details and current development status, see [PROJECT_STATUS.md](PROJECT_STATUS.md), [NEXT_CHAT_HANDOFF.md](NEXT_CHAT_HANDOFF.md), and the historical milestone documents, taking care to prefer the current source when they disagree.
