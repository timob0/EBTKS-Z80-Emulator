# ZIP / individual file import into CP/M images — Media Manager follow-on

**Proposal recorded 2026-10-18; NOT implemented in firmware.** User provided `TP_301A.ZIP` and requested a practical way to load standalone CP/M-80 programs into the EBTKS virtual disk environment, ideally through Media Manager. The archive contains **18 CP/M-native files**, including `TURBO.COM` (Borland Turbo Pascal **3.01A, CP/M-80/Z80**; no `TP.COM`), `TINST.COM`, Turbo support files, and MicroCalc source/example files. `READ.ME` explicitly describes Turbo Pascal 3.0 CP/M-80; executable strings identify 3.01A. The original disk is preconfigured for **Microbee VDU** and may need terminal setup with `TINST` for the emulator's VT100 mode.

## Revised practical priority following ESP32 heap / HTTP experiments (2026-10-18)

The user has **paused full-image downloads/uploads as a roadmap priority**, accepting direct management of raw 8MiB/616KiB SD images by a PC. The compatibility raw downloader remains a possible occasional fallback, not a target for acceleration. A real 616KiB download completed in 184.5s, spending 13.3s on SPI versus 165.6s in HTTP-header waits; during transfer ESP32 had only 9,204 bytes of *largest contiguous* allocatable 8-bit heap despite 65,500 bytes total free, rising to 28,660B/79,948B afterward. The experimental full-image SPI-backed HTTP stream proved unsafe and was removed. **Do not resume implementing fast 8MiB HTTP/raw-transfer endpoints until user requests it.**

**Priority 1: Native 8MiB empty disk creation on Teensy SD only**, requiring neither ESP32-held image nor ZIP nor reverse SPI: start job by small JSON command, check SD space/collisions and both desired+active mount tables, write and initialize 8,388,608 bytes using a small reusable local SD buffer and periodic progress updates, ensure directory entries in initial 16KiB are E5h per native DPB, verify final size and first directory blocks, publish only verified new file. Avoid holding Teensy UART reply for the full operation; step a background/state-machine job outside HP ISR and return progress via separate queries.

**Priority 2: Reusable headless unmounted-image CP/M file API**, with verified directory/extent/allocation handling (`list/stat/read/write/delete`), explicit session ownership and safe atomic-ish staging / recovery preserving existing files. CP/M 2.2 file sizes are record-based, so honor 128-byte records and avoid guessing host file lengths. File service logic should not be tied to browser/HTTP and should later accommodate HP-85 BASIC.

**Priority 3: ZIP as file exchange, not disk-image transport.** For browser-selected ZIPs, parsing/DEFLATE can run in the browser with ample PC RAM, transmitting **individual extracted CP/M files in small bounded chunks** ESP32→Teensy (no entire image in ESP32 heap). Export assembles ZIP in browser from individual file reads. If the ZIP is already located on Teensy SD, an optional later on-device ZIP reader can stream its entries using a bounded decompressor workspace (DEFLATE can need a 32KiB history window; actual library requirements need measurement). Both routes call the same headless file API and operate on *unmounted* images initially. ZIP should never overwrite mounted images or bypass CP/M extent validation. Existing off-device ZIP→DSK tooling stays a valid manual workaround. Do not equate good total free heap with a guaranteed large contiguous allocation.

## Proven EBTKS native CP/M disk layout (from authoritative source)

In ESP32 `cpm_geometry_dynamic.h`, the native CP/M 2.2 DPB is:

```text
SPT=128, BSH=5, BLM=31, EXM=1, DSM=2047,
DRM=511, AL0=F0h, AL1=00h, CKS=0, OFF=0.
```

The Media Manager `cpm_media.h` recognizes **exactly 8,388,608 bytes** as a native R/W `ebtks8m` image. BIOS disk I/O maps 512-byte host sectors directly to image file sectors; there are no boot tracks or LIF headers in the native format. The directory consists of 512 entries × 32 bytes = 16 KiB occupying allocation blocks 0–3; user data starts at block 4. Allocation blocks are 4 KiB. On the 8 MiB medium, directory pointers are **eight 16-bit little-endian block IDs** per entry. Unused directory slots contain E5h. For EXM=1, directory entry extent count uses `records=(EX & 1)*128+RC`, so up to **32 KiB** of file data can occupy one physical directory entry. Larger files require multiple physical extents (EX/S2).

Equivalent `cpmtools` `diskdefs` entry for host conversion (not yet cross-validated with a local cpmtools run):

```text
diskdef ebtks8m
  seclen 512
  tracks 512
  sectrk 32
  blocksize 4096
  maxdir 512
  skew 0
  boottrk 0
  os 2.2
end
```

From the user-supplied Turbo ZIP, a **new unmounted** `TP301A.DSK` was generated on the assistant's host sandbox: 18 ZIP entries transformed into uppercase legal 8.3 CP/M files under user 0, 43 × 4-KiB data blocks, 18 directory slots, 8 MiB total. Independent directory traversal recovered each original file byte-for-byte, with SHA256 checks. Whole-image SHA256:

`c6dcb920c29baf9dc92d5a3f6851e2c113232ea53e4717536f57903981c69b4e`

**The generated image is structurally validated but NOT yet verified with a user CP/M `DIR` / `TURBO` hardware run.** User obtained the downloadable image separately in chat; the ZIP/archive itself and derived commercial program files should **not be committed into the public firmware repo**. Only design details and checksums belong here.

## Headless file service as common backend (new requirement)

**Approved 2026-10-18:** file import/export must be backed by a **reusable non-Web CP/M file-service API**, not logic tied to HTTP request handlers. Core operations will include directory listing, per-file stat, bounded binary read and staged write, file deletion and optional import/export transactions, keyed by a validated drive or disk-image identifier and 8.3 name plus CP/M user area. The Web UI/ZIP exporter becomes one client; a later HP-85 `CPM` LOADBIN with `CPMCP`, `CPMCAT`, `CPMPURGE` becomes another. An HTTP adapter is optional; **HP commands must not require Wi-Fi or a browser**.

Example proposed HP BASIC usage: `CPMCP "AFILE:D330","A:AFILE"` (HP-IB device D330 → CP/M A:), the reversed command to copy to HP disk, `CPMCAT "A:"` to list, and `CPMPURGE "A:SOMEFILE"` to delete. These are design examples, not commands currently recognized by HP BASIC. HP BASIC/HP-IB file handling requires its own validated adapter. **Critical:** unmounted-image read/write parser is the first safe implementation; a **mounted/live CP/M drive needs a separate BDOS-coordinated agent** before allowing `CPMCP` writes or `CPMPURGE`. Direct image edits while CP/M holds cached directory/allocation state are prohibited. See [`CPM_FILE_API_HP_BRIDGE_DESIGN.md`](CPM_FILE_API_HP_BRIDGE_DESIGN.md) for the contract, state machine and safeguards.

## Approved user workflow: create, populate and mount named hard disks

**Clarification agreed 2026-10-18:** The user explicitly wants **(1) Create New Hard Disk**, an empty writable 8 MiB EBTKS-native CP/M disk with a user-selected SD filename, and **(2) Import ZIP / Files INTO A SPECIFIED IMAGE**, including **an existing populated disk**, without discarding its current files. A typical flow:

```text
Media Manager → New Hard Disk → Programming.dsk → Create (unmounted)
              → Import Files → TP_301A.ZIP → target Programming.dsk
              → Preview & Verify → Commit
              → Assign Programming.dsk to E: → Apply / Reset CP/M
CP/M prompt: E:  then DIR  then TURBO
```

The disk filename `Programming.dsk` names the **SD-hosted virtual hard disk**, not a CP/M filesystem directory. CP/M 2.2 has no ordinary nested directories; it has drives A:–P: and user areas (commonly 0–15), which are separate from hierarchy. A named image for each application group is a natural organization method. Native image must remain exactly **8,388,608 bytes**, with a proper empty E5h-filled CP/M directory covering the first **16 KiB** and no allocated user file blocks; it is a *filesystem-initialized disk*, not merely an arbitrarily named empty 0-byte file.

### New Hard Disk operation: local Teensy SD creation (no reverse SPI required)

- Accept a friendly basename and produce a canonical path directly beneath `/cpm/disks/`, such as `/cpm/disks/Programming.dsk`. Enforce basename-only input, length limits, case-insensitive FAT name collisions, no path separators, dot-dot, reserved names or user-controlled directory paths.
- Refuse if the target already exists or is present in **either desired or active** drive assignments. Never overwrite existing media, including an image with the same FAT-canonical filename.
- Check sufficient card free space **including the staging file**. Teensy should create/preallocate and initialize the full **8 MiB locally**, in **bounded write chunks** that leave the HP bus interrupts serviced. If filesystem allocation produces unwritten holes, ensure deterministic contents; at minimum directory sectors must be exactly E5h, and the rest of the image must be initialized safely.
- Verify actual file length and sample/readback directory sectors before publishing the disk in Media Manager. A failure or reset leaves a recognizable staging file for cleanup, not a partially initialized selectable image. Do not claim FAT rename or sync operations are magically atomic against power loss.
- On success, the disk appears **unmounted** in the existing image catalog and is eligible for assignment to any free drive A:–P: by the existing Mount / Apply workflow. Newly created native disks are **R/W**, unlike original HP 82900 LIF mounts.
- Ideally expose progress/cancel and an explicit Create Complete state, without holding the HP-85 bus ISR or stalling ongoing SD activity; creation is not a request to start/interrupt CP/M.

### Import ZIP / individual files into a selected disk (including existing content)

- User first selects a **destination image** from supported native `ebtks8m` images; choose any existing *unmounted* `.dsk` (or the newly created blank disk), **not** a mounted LIF image. Importing into an image already mounted in active *or desired* configurations must be refused, even while Z80 happens to be idle. User can unmount/Apply first, then import safely.
- Select ZIP or multiple loose files; preview list of CP/M 8.3 uppercase names, CP/M user area (default 0), source sizes, projected records and allocation blocks, existing-name collisions and whether sufficient blocks/directory entries remain. CP/M stores files in 128-byte records; pad final records without altering their earlier data. Reject archive traversal, symlinks/exotic entries, oversize/uncompressed bombs, duplicate 8.3 names, malformed ZIPs and unsupported file types rather than silently skipping or overwriting them.
- **Do not rebuild a blank image for every import**. First **copy the existing 8 MiB disk locally on the Teensy SD** to a separate temporary/working path (space check required), then read and validate its directory and allocation bitmap. Allocate new blocks/extents exclusively from free space, preserve all existing user files and CP/M user numbers, then add extracted new file data and directory entries to the *staging copy*.
- Prefer **browser-side ZIP extraction plus bounded SPI transfer of just extracted file bytes**. The Teensy owns the actual SD image; there's no intrinsic need to transfer the entire 8 MiB image browser→ESP32→Teensy merely to add a 30 KiB `.COM`. A small Teensy-side native CP/M image writer can update allocation map and directory while the reverse SPI stream sends file payload chunks. This needs a carefully verified native filesystem writer and safe concurrency. An alternate first implementation may assemble the full modified image on the host/browser and upload it through the staged SPI file service; that is simpler but slower and requires roundtripping/copying all 8 MiB. Choose the approach based on implementation/testing complexity, not premature micro-optimization.
- Default on same-name collision: **abort or ask explicitly**. A future Replace File option must first allocate new data safely, validate it, then update directory extents in the working copy; no silent overwrites. Don't claim arbitrary ZIP archives contain CP/M executable programs; import merely preserves their file bytes and filenames.
- After transfer, **reread the staging image** to verify imported file contents and untouched existing files plus CP/M directory/extent/allocation consistency. Compare per-import source digests with reconstructed file payload up to original unpadded lengths. Record a verifiable whole-image digest and require user confirmation to publish. Because the final filename already exists, preserve a named original backup or provide a safe staged-copy swap/recovery scheme that works despite potential FAT rename/power-loss interruptions. Never destroy the only known-good copy.
- Refresh catalog after completion; mounting to E: is a separate deliberate action and existing Apply/Reset CP/M rules remain unchanged. Ordinary live CP/M writes remain **UART**, and read cache invalidation/rules are not changed by archive import.

### Export files from a CP/M image as a portable ZIP (approved)

**New requirement agreed 2026-10-18:** In the Media Manager, select a disk image and choose **Export Files as ZIP**. The ZIP, not the raw disk format, is our **cross-image / cross-machine exchange format**: e.g. extract CP/M files from an original **HP 82900 CP/M LIF image** and import them into a native **8 MiB `Programming.dsk`**, or back up and share programs edited in CP/M. This does **not** require direct ZIP mounting or converting entire LIF volumes.

Keep two separate, clearly described download actions:

- **Export Files as ZIP:** parse the selected supported CP/M filesystem, traverse allocated extents/directories, reconstruct named files and provide a conventional ZIP to the browser. The result transfers CP/M **files**, not boot/geometry/unused sectors, and is suitable for import into another supported image profile.
- **Download Raw Image:** transfer the exact original `.DSK` or `.LIF` bytes, with SHA-256 when feasible, for complete backup, bootability/reserved tracks, forensic preservation and recovery. ZIP export is **not** a substitute for this backup.

Initial **safe export**: require the source image to be **unmounted in both active and desired mount tables**, since a running CP/M filesystem can change midway through traversal and produce inconsistent files. It is reasonable to consider safe read-only mounted images later if a stable snapshot is guaranteed. Read from Teensy SD through verified Teensy→ESP32 SPI framed transport, with bounded streaming and browser ZIP generation / compressed streaming rather than holding a whole 8 MiB image or ZIP in ESP32 RAM. For original HP LIF, support **specifically recognized HP 82900 CP/M media** using its validated **256 KiB logical CP/M region and original HP DPB**, not arbitrary HP LIF file collections. Native EBTKS `.DSK` needs its separate DPB. On any malformed directory, duplicate allocation, invalid extent, CRC/SPI error or incomplete download, abort with an explanation instead of silently emitting a misleading complete archive.

ZIP usability rules:

- Files belonging to **CP/M user area 0** appear at ZIP root as ordinary uppercase `8.3` paths (`TURBO.COM`, `HELLO.PAS`) for direct modern OS use. For users 1–15, use deterministic folders such as `USER01/HELLO.PAS` and `USER15/HELLO.PAS` so duplicate CP/M filenames can coexist. These are **ZIP folders only**, not CP/M directories.
- Preserve file contents **as stored in CP/M records**, without text conversion or guessing that a trailing `0x1A` is removable. CP/M 2.2 tracks 128-byte record counts, not a reliable original modern host byte length; therefore some extracted files will have trailing padding. A ZIP export is lossless at the logical **CP/M-record** level, but it cannot always recover the exact pre-import host length.
- Preserve CP/M directory user/attribute information by an **optional** manifest, e.g. `_EBTKS_CPM_MANIFEST.json` at ZIP root. Normal ZIP files without it import to user 0 with default attributes, and conventional unzip programs can ignore it. The manifest is **not imported as a CP/M application file**. Reserve that entry name and handle filename collisions safely.
- A potential version-1 manifest entry contains `schema_version`, source image profile/geometry identifier (informational, not mandatory for target disk), and per-file `zip_path`, `cpm_name`, `user`, available CP/M attribute bits, `record_count`, and SHA-256 of exported bytes. Only include `original_host_length` **when provably known** and still valid—never infer it by stripping trailing NUL, E5h or Ctrl-Z bytes. Define/validate the exact schema before implementing it; do not accept arbitrary manifest paths or trust unverified manifest metadata.
- When **importing** such an exported ZIP, validate every path/digest and restore supported user areas/attributes when compatible with the destination. Present warnings about unsupported metadata instead of silently discarding it. A plain third-party ZIP remains equally valid.
- Before download, offer file selection (all files initially, optional checkboxes later), count, total extracted logical bytes, user areas and expected browser memory usage. Preserve download integrity/progress; do not change CP/M disk contents, mount assignments, Wi-Fi/OTA or production UART disk writes.

**Acceptance:** a portable ZIP exported from a populated native CP/M `.DSK` must reimport into a **different empty** native image with identical CP/M user area, names, attributes and **128-byte record payloads**; validate with `DIR` and binary readback. Repeat for original HP 82900 CP/M LIF→native disk with known test files. Retain a raw-image download path verified byte-for-byte. This extension may follow the safe upload and filesystem parser stages; the initial reverse-SPI RAM-only test remains first.

### Staged delivery / acceptance

1. Implement and verify **Create New Hard Disk** locally on Teensy SD, independently of SPI transfer acceleration (safe on empty/low-space cards, with full 8 MiB size and valid directory).
2. Establish RAM-only reverse SPI framed transfer as already specified in [MEDIA_MANAGER_SPI_UPLOAD_STAGE1.md](MEDIA_MANAGER_SPI_UPLOAD_STAGE1.md), then staged binary upload/download with CRC and whole-file verification.
3. Offer archive extraction and file import into a **new blank image** using the selected import architecture; verify actual `DIR`, `TURBO` and CP/M file-save behavior.
4. Extend the same importer to **existing populated, unmounted native images**, preserving all old entries, supporting non-colliding additions and reporting space/collision failures. Include deliberate aborted imports and power-loss simulation.
5. Only then consider file replacement, deleting files from an image, ZIP export, or original HP LIF image creation.

**Why not make ZIP directly mountable by CP/M?** CP/M expects a block device with a directory, extents and allocation map. A ZIP is not a CP/M disk image. A virtual ZIP-backed disk would need synthetic directory sectors, on-demand inflate/random access, cache management, and an explicit **read-only or write-overlay** policy. It is possible as a distinct adapter, but adds complexity and offers less value than converting once to a normal writable EBTKS-native `.DSK`. Similarly, a plain ZIP or bare file does not become an HP 82900 **LIF** image by renaming it `.lif`: HP LIF images require the genuine LIF disk structure and the current Media Manager deliberately mounts them **R/O**. Native `.DSK` is preferable for newly imported software.

## Current manual workaround

Extract or construct an **8 MiB `.DSK`** on a desktop, then place it at `/cpm/disks/TP301A.DSK` on the Teensy SD with power off. In `/cpmmedia` select a vacant drive such as E:, Mount, Apply/Reset CP/M; then run `E:`, `DIR`, `TURBO`. Test `TINST` if Microbee terminal codes conflict with VT100. Keep original images backed up and avoid touching mounted disk-image paths.

**Sources:** ESP32 `cpm_media.h`, `cpm_geometry_dynamic.h`, `disk.h`; [cpmtools directory format](https://seasip.info/Cpm/format22.html); [CP/M filesystem manual](https://manpages.ubuntu.com/manpages/questing/man5/cpm.5.html). [Media Manager SPI upload-stage plan](MEDIA_MANAGER_SPI_UPLOAD_STAGE1.md); [project handoff](NEXT_CHAT_HANDOFF.md).
