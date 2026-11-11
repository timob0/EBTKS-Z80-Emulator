# HP-85 / CP/M integration and GSX roadmap

**Agreed direction (2026-10-18):** Prioritize **new capabilities** rather than further CP/M read throughput optimization. Real CP/M SPI read-ahead works: Multiplan 10 s → 3.7 s startup, Zork 8 s → 3 s, 81.4% CP/M cache-hit rate over 1,260 reads, 234 validated batches, 0 UART fallbacks. HP bus handling and the conservative IRQ-free `poll_low_v2` handshake must remain untouched.

**This is a planning document only:** no firmware changes or new hardware behavior are implied by writing it. The TFT panel has **not arrived yet** and should not block milestone 1. The user will report its arrival; the controller, wiring and spare CN3 pins are not yet confirmed. Historical HP 82900A genuine CP/M compatibility is a separate later effort.

## Seven milestones, as prioritized

| # | Feature | Goal / initial acceptance gate | Key constraints |
|---|---|---|---|
| 1 | Media Manager file upload/download + reverse SPI | Browser can transfer a complete test disk image to a **new unmounted** SD path and download an identical copy, with whole-file checksum, interrupted-transfer recovery and explicit post-verification mount | Keep existing live CP/M disk reads and UART writes unchanged. Test ESP32 → Teensy over SPI independently and preserve HP bus ISR |
| 2 | Physical 4-inch TFT native CP/M terminal | Operate CP/M in 80×24 text mode without browser or Telnet, preserving those existing consoles | Await actual module pinout/controller; no full-framebuffer assumption, and avoid conflicting with CN3 SPI link |
| 3 | HP keyboard ↔ CP/M/TFT input | Explicit attach/detach control so the HP keyboard can operate CP/M's TFT terminal; recover to HP BASIC reliably | Use existing Teensy HP bus hooks; **never attach Arduino GPIO IRQ** to HP bus interrupt group |
| 4 | CP/M printer and plotter via HP peripherals | Route CP/M **LST:** output (optional `PRN:` alias or front-end if needed) to selected HP printer path, later separate plotter/HP-GL path | CP/M 2.2's standard logical list device is **LST:**; printer text and plotter commands are distinct data types; HP-IB ownership/handshake must be validated |
| 5 | HP-85 text terminal + HP BASIC `CPM` integration | Control CP/M and map disks/devices from HP keyboard and 32-column HP CRT when TFT/browser are unavailable | Limited terminal emulation with clear mode switch and F-key controls; preserve autonomous HP-85 and Z80 operation |
| 6 | Historically compatible CP/M-80 GSX | Run a stock GSX-compatible test program with an EBTKS TFT device driver; later consider drawing to HP peripherals | Start with GDOS/GIOS dynamically loaded drivers and standard `ASSIGN.SYS`, not a speculative ROM; memory-resident option later |
| 7 | Optional SIMON85 graphics → TFT/GSX | SIMON85 HP BASIC drawing can target TFT through an opt-in GSX-compatible graphics backend, with original HP CRT still working | Shared renderer/command ABI, scaling and text/fill semantics; do not require CP/M to stop servicing disk/console |

## Milestone 1: secure image transfer, isolated from live CP/M sector writes

### 1A. Preserve and label known-good firmware

- Keep a reproducible tested tag/commit for **both** feature branches: IRQ-free Teensy `poll_low_v2`, ESP32 4 MHz framed read and UART writes.
- Initial work uses only an opt-in Media Manager transfer endpoint and a **scratch path** under a safe SD staging area; it never hooks CP/M `disk_write` or changes a mounted disk image.
- The HP-85 stays responsive during transfers; no new IRQ attaches on Teensy GPIO6 or any other pin sharing `IRQ_GPIO6789`.

### 1B. Download and reverse-transfer bring-up

- **Download:** reuse the already tested Teensy-master MOSI → ESP32-slave receive path, without changing disk image contents. Expose a proper *streamed* HTTP response with backpressure rather than buffering whole images in the ESP32.
- **Upload:** browser streams data to ESP32 over HTTP; ESP32 must supply each CRC-framed payload on **ESP32 SPI slave MISO**, and the Teensy SPI master clocks it into a scratch buffer/file. This is the *reverse payload direction*, not a reversal of which processor owns SCK/CS.
- Start with deterministic RAM patterns and end-to-end per-frame CRC/sequence/session tests **before any SD writes**. Then write **only a new temporary file** in a staging directory. Keep MISO signaling and READY LOW/HIGH arbitration explicit and independent of the proven read link. Previous full-duplex experiments do **not** prove persistent bulk file uploads.
- Bound queue/HTTP backpressure; never allocate an 8 MiB buffer in ESP32 DRAM. Provide cancel, progress, timeouts and an error state that does not attempt to reuse DMA of uncertain ownership.

### 1C. Safe commit / verification / image validation

- Require an unambiguous new filename, path allowlist and free-space check. Block overwriting both **active** and **desired** mounted image paths, including writable and read-only media.
- Persist to a uniquely named staging file first, with per-frame CRC and complete size, and verify its **whole-file digest** (prefer SHA-256 if practical, or an explicitly documented integrity CRC), ideally by rereading the staged file. Reject partial files and wrong supported image dimensions/headers.
- Only after complete verification and user confirmation, publish or rename it as a **new unmounted disk image**, then allow explicit mounting/Apply Z80 reset. **Do not promise FAT rename is power-fail atomic**; use staging state and startup recovery/cleanup.
- Include a clean power-loss and deliberate interrupted-transfer test. Keep raw disk-image backups and never erase an original image automatically.

### 1D. Gate to ordinary CP/M writes

No new SPI-enabled live CP/M writes are required for successful image upload. Continue to use the reliable UART/Base64 `disk_write` for ordinary CP/M files. Revisit live writes only if measured end-user needs justify the extra durability and CBIOS error-propagation work.

## Milestone 2: native TFT CP/M terminal

**Hardware is not here yet.** On arrival: document **actual board controller**, wiring, voltage/current, touch controller/I²C address and CN3 spare GPIOs. Current Teensy↔ESP32 SPI link uses physical pins and an ESP32 SPI slave controller; a TFT generally expects an **SPI master**. Do **not** assume it can share that SPI interface without scheduling or conflicts. Check feasibility of a separate ESP32 SPI host/bus or a designed arbiter before wiring; protect all existing UART, SPI and HP bus pins.

A 480×320 panel (to be verified) can potentially show 80 columns × 24 rows at 6×12 character cells and a status strip. A full 16-bit framebuffer would require ~300 KiB, unsuitable for current constrained ESP32 DRAM. Prefer a small cell/attribute buffer and dirty rows/rectangles, using the LCD's internal display RAM. VT100 parser currently lives in Web terminal JavaScript; design a native ESP32 display model rather than assuming direct code reuse. Keep Telnet and browser displays available as separate mirrors.

## Milestones 3–5: real HP / CP/M interoperability

A dedicated keyboard/console **focus state machine** is more important than a keyboard hook alone. Proposed states: HP BASIC owns keys; CP/M owns TFT; CP/M optional HP CRT terminal; emergency detach/back to HP BASIC. Define explicit focus ownership, escape key(s), reset behavior and clean recovery. Avoid blocking the HP-85 bus ISR during SD, printer or display activity.

For printer integration, start with CP/M BIOS **LIST / LST:** text stream → buffered mailbox → Teensy-managed existing printer path, with configurable backend and error/backpressure behavior. Add HP-IB output only after confirming the actual peripheral/IB service path; a plotter needs its own raw HP-GL or GSX plotter driver rather than passing arbitrary text to LST. Do not assume `PRN:` is a standard CP/M-80 logical device name.

The HP-85-side `CPM` / `CPM85` integration can be an option-ROM or BASIC command adapter with F-key navigation, drive/console-device mapping, and an intentionally restricted CRT text mode. Coordinate with SIMON85/PRINTER85 ROM availability and existing keyboard hooks; avoid keyword collisions or replacing the HP-85's native services. Preserve the HP-85 and Z80 as independent active machines.

## Milestones 6–7: GSX-80, GEM ideas and SIMON85 graphics

**Historical GSX-80 architecture:** `GSX.SYS` / `GENGRAF.COM` provide device-independent GDOS support, `ASSIGN.SYS` maps workstation numbers to dynamically loaded device-specific GIOS `DD*.PRL` drivers. GSX is accessed through **BDOS function 115 (73h)**, with five 16-bit-array pointers in the parameter block. The device categories conventionally include **screen 1–9, plotter 11–19, printer 21–29**. Graphics operations include polylines, markers, text, filled polygons and generalized drawing primitives. Source: https://www.seasip.info/Cpm/gsx.html ; original reference: https://bitsavers.org/pdf/digitalResearch/gsx/GSX-80_Graphics_Extension_Programmers_Guide_Nov82.pdf .

First prove a stock **loadable** GSX-compatible CP/M test application, with `ASSIGN.SYS` and an EBTKS `DDTFT.PRL`-style display driver. Scope memory consumption/driver relocation carefully. A genuine GSX resident-ROM implementation would require an explicit Z80 memory-map/banked-memory strategy and is **not a prerequisite** for using GSX. A Turbo Pascal-80 `BDOS(115,...)` wrapper is plausible; do not assume native CP/M Turbo Pascal includes a GSX graphics library. There is historic discussion of a PolyPascal GSX interface: https://retrocomputing.stackexchange.com/questions/31326/how-can-i-use-gsx-in-turbo-pascal-for-cpm-80 .

GSX influenced GEM's VDI interface, but GSX alone **does not implement GEM's application/event/window/desktop services**. A simplified GEM-like desktop would be a separate later experimental layer after a compliant VDI driver exists.

For SIMON85, do not try to load a Z80 `.PRL` driver into the HP-85 option ROM. Instead define a small **common graphics command protocol/renderer** that is consumable both by the GSX GIOS driver (Z80 side) and by an optional SIMON85 drawing backend (HP side). Convert SIMON85 scaled coordinates, style and clipping explicitly. One mode could proxy GSX operations through the Z80 for a visible two-machine demonstration, but a direct firmware renderer should remain available without needing an active CP/M graphics process. The native HP-85 CRT drawing path must remain the default and regression-tested.

## Cross-feature invariants

1. Protect the HP bus interrupt vector and all existing HP services. **Never call Arduino `attachInterrupt()` on READY** on Teensy; IRQ_GPIO6789 is reserved for timing-sensitive EBTKS bus service.
2. Keep one SPI-master/CS owner (Teensy) in the existing link, unless hardware review explicitly designs another bus. Each transfer direction has independent test gates.
3. CP/M read cache and the old UART write path stay working throughout Media Manager development.
4. No global large fixed DRAM allocations on ESP32; the 8 KiB CP/M read cache already required lazy heap allocation.
5. Maintain working Web UI/update, browser VT100, Telnet, CP/M warm boot, mounts, HP-85 BASIC and SIMON85 graphics while developing each optional feature.

**Order stays fixed by user preference:** 1) Media Manager + SPI upload, 2) TFT terminal, 3) HP keyboard/TFT CP/M use, 4) HP peripheral printer/plotter, 5) HP CRT terminal & `CPM` command, 6) CP/M GSX, 7) SIMON85 redirected to TFT via GSX-style drawing. Historical original HP 82900A firmware emulation is worthwhile later, but deliberately outside this sequence.
