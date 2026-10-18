# CP/M file service API and future HP-85 LOADBIN bridge

**Design requirement agreed 2026-10-18. Documentation only: no new file API, HP command, ROM, endpoint or SPI write implementation has been built or tested.** Development on ESP32/Teensy `feature/media-manager-spi-upload`, with merged SPI-read baseline preserved on both `main` branches.

## Motivation / user's intended command syntax

The Media Manager file-transfer functions should be available as a **headless file service** rather than being hidden inside Web UI route handlers. This will later let a **`CPM` HP-85 LOADBIN / option-ROM integration** expose HP BASIC commands for exchanging individual files with CP/M:

```text
CPMCP "AFILE:D330","A:AFILE"    ! Copy a file from HP-IB mass storage D330 to CP/M A:
CPMCP "A:AFILE","AFILE:D330"    ! Copy the CP/M A: file to the HP-IB device
CPMCAT "A:"                     ! List the CP/M A: directory on the HP-85
CPMPURGE "A:SOMEFILE"           ! Delete SOMEFILE from the CP/M A: drive
```

These are **requested illustrative HP BASIC statements, not commands presently implemented or confirmed to be tokenized/accepted by HP-85 BASIC**. When implementing the LOADBIN/ROM, verify valid HP BASIC keyword, argument and device-specifier syntax and interactions with SIMON85/PRINTER85; the `D330` example denotes an HP-IB mass storage specifier as supplied by the user. Avoid existing HP BASIC keyword collisions. The HP/CPM bridge must not assume that HP-85 binary file formats and CP/M record streams are mutually executable; initially copy **raw file bytes**, with optional explicit text or HP-format converters later.

**Goal:** the same underlying file operations power (a) Media Manager standalone ZIP import/export, (b) optional authenticated Web/UI access, (c) independent scripted local tools if permitted, and eventually (d) HP BASIC's `CPMCP`, `CPMCAT`, `CPMPURGE`, without reimplementing CP/M directory and allocation logic for each caller.

## Architectural boundaries

```text
Browser ZIP import/export ---+
Media Manager image tools ----+--> CP/M FILE SERVICE (typed internal API)
Future HP-85 CPM LOADBIN ----+          |
future scripted client ------+          +--> image filesystem parser/writer
                                      |    (UNMOUNTED images: supported DPB/DPH, staging)
                                      |
                                      +--> CP/M-coordinated live-file agent
                                           (FUTURE: BDOS/BIOS, Z80 filesystem coherent)
                                                        |
                                          ESP32 media mount NVS / Z80 CP/M
                                                        |
                                UART control + framed SPI file payload transfer
                                                        |
                                         Teensy 4.1: SD + HP bus/HP-IB gateway
```

The core API should be **transport-independent**: a service running within the emulator/EBTKS application with typed command/control requests and bounded binary streams. Neither HTTP routes nor the future HP LOADBIN should directly implement block allocations, traverse FAT image paths, or reach into ESP32 Z80 RAM without coordination. An HTTP facade is optional; it is **not a requirement** for HP-85 commands to roundtrip over Wi-Fi. HP BASIC accesses the internal bridge through the established HP-85/Teensy interface, not by having ESP32 drive the HP-85 bus. UART remains the serial control plane (serialized); bulk binary frames use the existing Teensy-master/ESP32-slave SPI link, with direction chosen per transfer. No new Teensy READY attachInterrupt: `IRQ_GPIO6789` belongs to the HP bus ISR.

The canonical drive mapping continues to use ESP32 NVS `cpmmount` desired/active assignments. APIs must expose mounted/unmounted and supported native/HP LIF geometry; APIs that address an image path must use a validated opaque image identifier or allowlisted `/cpm/disks/<basename>`, never a raw arbitrary SD path. Drive-letter operations require strict A:–P: bounds and optional CP/M user area (0–15).

## Proposed portable file-service verbs (contract, not endpoints)

| Operation | Essential request | Result / behavior |
|---|---|---|
| `listFiles(target, userFilter)` | Drive letter or image ID; optional user-area filter | CP/M 8.3 names, user area, attributes, record length and optionally stable file token |
| `statFile(target, name, user)` | Qualified 8.3 filename | Records, attributes, allocation/geometry-safe file size, optional revision |
| `openRead(target, file)` / `readChunk(handle,...)` | File target, byte offset, bounded requested length | Binary blocks + length, CRC/session and end-of-file indication |
| `beginWrite(target, filename, mode)` / `writeChunk(ticket,...)` / `commitWrite(ticket)` | Destination, collision policy, expected length/digest | Staged/provisional creation, verified complete commit, explicit status; no silent replacement |
| `deleteFile(target, name, user, expectedRevision)` | Exact target + identity; authenticated/confirmed | Transactional delete or explicitly unsupported/busy response; never mutate active image behind BDOS |
| `createDisk(name, profile)` | New safe basename, `ebtks8m` profile | Locally initialized 8 MiB image, verified before publish, unmounted |
| `exportFiles(target, selector)` / `importFiles(target,...)` | Selected CP/M files or ZIP manifest inputs | File-oriented streaming used by ZIP UI and HP bridge; preserve CP/M user area/attributes where possible |
| `downloadImage(id)` / `uploadImage(id,...)` | Whole raw image | **Separate** exact-image backup/restore path; not interchangeable with file export |

Internally return structured **error codes** such as `NOT_FOUND`, `EXISTS`, `NO_SPACE`, `UNSUPPORTED_PROFILE`, `MOUNTED_BUSY`, `READ_ONLY`, `INVALID_NAME`, `BAD_DIRECTORY`, `STALE_HANDLE`, `IO_FAILURE`, `HASH_MISMATCH`, `CANCELLED`. A transaction token/handle, transfer offset, byte count and CRC are required for resumable/retry-safe bounded chunk streams. Do not promise exactly-once behavior without idempotency and commit tracking. Validate filename case folding, 8.3 and user-number mapping consistently for both Web ZIP imports and HP commands.

**File-length caveat:** CP/M 2.2 records are 128 bytes; `readChunk` gives the logical CP/M-record bytes unless a trusted manifest from a previous import preserves a precise host length. Never implicitly strip 0x1A, 0x00 or 0xE5 from binary files. ZIP optional metadata is specified in [CPM_ZIP_IMPORT_DESIGN.md](CPM_ZIP_IMPORT_DESIGN.md).

## Critical distinction: offline filesystem operations vs a live mounted drive

**Stage 1, feasible now:** the Media Manager file service parses/edits **unmounted** `.DSK` images (or read-only supported HP 82900 CP/M LIF images for export) using a validated CP/M filesystem reader/writer. Mutating operations work on **a staged copy** of the image with readback and a verifiable commit; no active or desired mount may reference it. This supports ZIP import/export and file-level transfers for archival/media preparation without risking the running CP/M instance.

**Future Stage 2, required for the user's transparent `CPMCP "...","A:..."` and `CPMPURGE "A:..."`:** a mounted Z80 CP/M drive **cannot be modified behind CP/M's back**. The running CP/M BIOS/BDOS and programs may cache directory sectors, allocation maps, random-record state or file control blocks; editing the raw image file directly would risk lost updates and corruption even if all SPI frames pass CRC. Implement a separately coordinated **live CP/M file agent** that calls CP/M file services through a carefully defined Z80/BDOS-side protocol, or an equivalent pause/quiesce/flush/reinitialize mechanism validated against the native CBIOS. Do **not** assume pausing the CPU alone flushes BDOS caches or makes an active image safe to patch. Reject writes/deletions to mounted images until this second path is explicitly engineered and hardware-tested; do not silently fall back to raw sector edits.

Live-read directory/export also requires a coherent snapshot (through the CP/M agent or a proven quiesced snapshot), not ad hoc scanning of changing sectors. A mounted drive may be read-only at filesystem level yet still require conservative synchronization. Serializing the UART mutex alone is insufficient to solve this.

If an HP BASIC `CPMCP` request targets an active drive before the live agent exists, return a clear `MOUNTED_BUSY`/unsupported error and guide the user to detach/unmount or perform the copy using CP/M itself. This staged policy preserves the already hardware-proven CP/M disk-read cache and original UART/Base64 writes.

## HP-85 LOADBIN bridge specifics (later milestones 3–5)

- Define a small versioned **HP BASIC LOADBIN / option-ROM opcode layer**: `CPMCP`, `CPMCAT`, `CPMPURGE`; possibly later `CPMDIR`, mapping/status and `CPM` console attachment. Use actual HP BASIC argument evaluation and parser conventions; check SIMON85/PRINTER85 keyword uniqueness and bus hook availability before adding tokens.
- `CPMCP` must accept exactly **one CP/M drive-qualified operand** and **one HP-side file reference** for cross-machine transfer initially. Validate drive A:–P:, 8.3 names, user area convention, and explicit source/destination. Avoid assuming that CP/M's A: drive is the host's HP-IB device or that HP file locators are simply SD paths.
- HP-IB storage `D330` is handled via **HP-85/Teensy gateway or HP BASIC file/record I/O**, preserving normal HP bus timing and device ownership. The ESP32 never talks directly to HP-IB or HP-85 bus lines. Design buffering so the HP-85 is not stalled in a timing-sensitive ISR while ESP32 waits on Wi-Fi/SD/SPI.
- For `CPMCAT`, return a bounded stream of names/sizes/attributes to HP BASIC output, with pagination if necessary. For `CPMPURGE`, require an exact name and a fail-safe delete path; errors must be reflected as meaningful HP BASIC error/status codes. Concurrency/mount safety is mandatory.
- File transfer semantics need a **binary mode**, with no HP newline/record conversions; optionally add a deliberate text-translation mode, and consider HP BASIC/program/data file encodings separately. If necessary transfer an HP file as a serialized opaque byte stream or tagged file representation; do not claim it becomes directly runnable on the opposite CPU architecture.
- The internal service is the **same** one used for Media Manager, but bridge transport differs: later HP BASIC invokes it via hardware-facing Teensy mailbox, without requiring a browser, Telnet or network connection. Long operations run in non-ISR tasks and report progress/cancel/status.

## Safety, testing and delivery order

1. **Current milestone:** build a testable reverse SPI **RAM-only** frame diagnostic; then a safe SD staging binary upload. Preserve working CP/M sector reads, UART writes, HP-85 ISR, Web/Telnet and updater.
2. Add **New 8 MiB Hard Disk**, plus a verified CP/M file parser/writer for **unmounted** images. Implement `listFiles/statFile/readChunk` and staged create/delete/import operations as an internal API first, not inside Web HTML handlers.
3. Build Media Manager **ZIP import/export** on those same primitives; normal ZIP files supported, optional CP/M manifest for metadata and user areas; verify roundtrips native .DSK ↔ ZIP and recognized HP .LIF → ZIP → native .DSK.
4. Introduce a **separate** live BDOS-coordinated file-agent design and test matrix, before promising transparent access to CP/M mounted drives; eventually expose `CPMCP`, `CPMCAT`, `CPMPURGE` via HP option ROM/LOADBIN.
5. Regression: verify Z80 write+re-read, serial/telnet/web terminals, Multiplan/Zork, rapid directory listings, overlapping HP BASIC/SIMON85 graphics/SD reads, abort/corrupt transfers, filesystem collision/space exhaustion, and externally modified image rejection.

**This API is an architectural contract, not a concrete HTTP URL or a promise of native ROM command availability.** The method names above are illustrative. Decide wire packet schema, buffer ownership and error-code mapping in source when implementing; do not expose anonymous unauthenticated destructive file endpoints over Wi-Fi by default.

**Related:** [Media Manager stage 1](MEDIA_MANAGER_SPI_UPLOAD_STAGE1.md), [ZIP/native image conversion](CPM_ZIP_IMPORT_DESIGN.md), [full HP/CPM roadmap](HP_CPM_INTEGRATION_ROADMAP.md), [future-chat handoff](NEXT_CHAT_HANDOFF.md).
