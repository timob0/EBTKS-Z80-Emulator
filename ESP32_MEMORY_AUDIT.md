# ESP32 SRAM inventory and Z80 32-bit IRAM feasibility

## 2026-10-21 combined HP/Web/CP/M/BBS hardware acceptance and low-water warning

User ran HP-85 `CPSTATUS` repeatedly, then CP/M F: `MODEM.COM`, dialed a BBS, logged off, and exited MODEM successfully. Web UI pages remained responsive, and Web Media Manager continued to display correct mappings. User unmounted a drive in **Web Media Manager**, and a subsequent HP-85 `CPSTATUS` reflected the change. This verifies that both interfaces see the same *desired* NVS mount table; unmount becomes active only after CP/M virtual reset. Previous blank Media Manager page / HTTP unresponsive incident **did not reproduce** in this combined session. The test demonstrates HP `CPSTATUS` v0.03e correct text, read-only cross-interface state, and successful Web unmount; HP `CPDMNT`, `CPDUMNT` and `CPRESET` still require physical acceptance.

Memory snapshot at `uptime_ms=962093` (~16.0 min) after session:

| Measurement | Bytes / value |
| --- | ---: |
| `heap_free` | 58,336 |
| `free_8bit` | 19,160 |
| `largest_8bit` | 17,396 |
| **`min_free_8bit` (historic)** | **628** |
| `min_heap_free` (historic) | 39,804 |
| `modem_state` | 2 |
| `modem_dial_refused_low_heap` | 0 |
| `telnet_listener_mode` | `shared_web_task` |
| `cpm_boot_ready` (last capture) | 70,852 INTERNAL / 31,676 8BIT / 27,636 largest |
| `tcp_closed` checkpoint | 57,476 INTERNAL / 18,300 8BIT / 14,836 largest |

**Interpretation:** The current 8BIT free is >19 KiB, with a 17.4-KiB contiguous block, and no low-memory dial refusal. No persistent leak established, and Web responsiveness held. The extraordinarily low **628-byte cumulative 8BIT minimum** nonetheless means that the system had very little remaining byte-addressable allocation reserve at some point since restart. It does not identify which operation caused the dip, and it is not current free space. **Do not silently mark heap safety as resolved or relax modem guard.** Note that `cpm_boot_ready` is captured inside `ESP32_Z80_Emulator.ino::loop()` after `bootstrap()` and can be overwritten at every Z80 *virtual* reset. It is not necessarily a fixed once-per-ESP32-coldboot checkpoint. Previous snapshot showed 81,464 INTERNAL/42,288 8BIT; difference alone is not proof of leak.

Suggested next work if user wishes to pursue memory margin: correlate minimum with fresh reboot and isolated `/api/cpm/media` refresh vs repeated HP `CPSTATUS` vs MODEM connect/disconnect. Use runtime allocation-failure tracing or targeted per-operation free/min probes, preserving the hardware-accepted firmware. Existing media-status API returned valid full JSON (six mounted drives plus SD media) when tested directly; don't claim NVS/media loss. No code changes or new flashes for this documented acceptance.

## 2026-10-21: hardware modem/Web/TFT retest after clean ESP32 build

User rebuilt and flashed ESP32 `feature/cpm85-drive-control` TFT target (`esp32dev-tft`) after discovering an older running image still had the `/api/system/memory/pools` HTTP prefix-shadowing bug. **Physical endpoints now behave correctly:** `/api/system/memory` includes `min_free_8bit`, and `/api/system/memory/pools` independently returns the seven overlapping capability classes. The Web-TFT terminal once again matches the 16-color ANSI/CP437 TFT visually.

**Real MODEM retest succeeded:** From Web Terminal, on CP/M F: drive, MODEM.COM connected successfully and the Web UI remained responsive throughout. Earlier dial-refused/frozen-Web behavior was not reproduced. Final snapshot at uptime 282,763 ms:

| Metric | Value |
| --- | ---: |
| `heap_free` / INTERNAL current | 57,548 bytes |
| `free_8bit` current | 18,372 bytes |
| `largest_8bit` current | 15,348 bytes |
| `min_heap_free` historic | 42,064 bytes |
| **`min_free_8bit` historic** | **2,888 bytes** |
| `modem_state` | 2 (worker running) |
| `modem_dial_refused_low_heap` | 0 |
| `telnet_listener_mode` | `shared_web_task` |

The last successful `after_tcp_connect` stage showed 60,744 free INTERNAL bytes, 21,568 free 8BIT and largest 8BIT 15,348. `modem_task` stage recorded 58,924 / 19,748 / 15,348. The historical `min_free_8bit=2,888` **is very low**, despite normal current reserve and responsive Web UI, so mark extreme-stress/allocation margins as **not fully proven**. ESP-IDF allocator low-water marks and current snapshots are not interchangeable or necessarily simultaneous, particularly across overlapping region capabilities. Do not relax dial guard or claim general OOM immunity. Further work, only if necessary, should trace when the 8BIT minimum drops and whether an allocation fails; consider separate memory reservation tests without perturbing this hardware-proven build. **No firmware changes in this documentation update.**

The CP/M drive-control branch is still in development. HP LOADBIN `CPM85 v0.03e` compilation/tests passed but **physical `CPSTATUS` newline acceptance remains unconfirmed**, and `CPDMNT`, `CPDUMNT`, `CPRESET` hardware tests are outstanding. Keep the accepted terminal/modem regression separate from those unfinished disk-management features.


**Accepted result:** Real hardware confirmed `cache_storage="iram32"` for the 8,192-byte SPI read-ahead cache, zero SPI failures, and reliable HTTP alongside real Synchronet BBS modem operation. Browser terminal ANSI-16/CP437 and modem Ctrl+X exit were subsequently hardware-validated; the entire ESP32 integration merged into `main` via [PR #6](https://github.com/timob0/EBTKS-Z80-Emulator/pull/6), post-merge CI [passed](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39602560884). **No Z80 RAM repartition is needed** for the demonstrated workload. Consult [NEXT_CHAT_QUICKSTART.md](NEXT_CHAT_QUICKSTART.md) for final current status; earlier experiment plans below are historical.

**SIMON85 / EBTKS classic ESP32, developed on `feature/cpm-wifi-modem` and merged into stable `main` via [PR #6](https://github.com/timob0/EBTKS-Z80-Emulator/pull/6) on 21 October 2026.**
Source audit completed 21 October 2026. Linker report and runtime region diagnostics
were added; physical test has now confirmed IRAM cache relocation and
responsive HTTP alongside an extended Internet BBS session (see below).

## Real hardware acceptance: IRAM cache + Synchronet BBS + working Web UI (21 October 2026)

**User confirmed success on the physical HP-85/Teensy/ESP32 system** after
flashing the development branch containing the 8,192-byte IRAM32 SPI cache
and shared Web/Telnet listener. All tested Web UI pages remained reachable
before, during, and after a real Synchronet BBS session. CP/M and TFT kept
working; SPI read-ahead returned zero errors and no UART fallback.
The previously reproducible concurrent HTTP outage **did not recur**.

| Phase | `heap_free` INTERNAL | `free_8bit` | `largest_8bit` |
|---|---:|---:|---:|
| Reboot + CP/M idle | 68,980 | 29,772 | 28,660 |
| HP remote display opened | 67,040 | 27,832 | 26,612 |
| Media Manager opened | 68,564 | 29,356 | 28,660 |
| Web Z80 terminal opened | 66,520 | 27,312 | 26,612 |
| `DIR` in terminal | 66,408 | 27,200 | 25,588 |
| MODEM.COM launched | 58,940 | 19,732 | 18,420 |
| MODEM + AT/ATI | 59,148 | 19,940 | 19,444 |
| Dialed Synchronet, connected | 56,820 | 17,612 | 12,788 |
| After BBS session (uptime 707 s) | 57,596 | 18,408 | 17,396 |

**Direct proof of allocation relocation:** prior version's stable
INTERNAL−8BIT heap gap was 47,416 B; new measurements yield 39,208 B
throughout the run (±20 B at final sample), a **8,208 B reduction**
matching an 8,192-byte IRAM cache plus allocator overhead. SPI endpoint:
`cache_storage=iram32`, 16-sector/8,192-byte cache, at least
**112 verified frames, 7 successful batches, 64 hits, 7 misses,
0 failures, 0 UART fallbacks**, retained 4 MHz transport. The cache
repeatedly served real CP/M disk operations and not merely memory tests.

Prior experimental modem-active `largest_8bit=6,900` B, compared to
**19,444 B** now; improvement **12,544 B (~2.82x)** with multiple
simultaneous services. This reflects both cache relocation and Telnet
task consolidation and workload differences, not IRAM relocation alone.
The old modem-dial refusal/frozen Web UI failure is no longer reproduced,
with `modem_state=2` and `modem_dial_refused_low_heap=0`.

**Remaining reservation:** `min_heap_free` (INTERNAL low-water) declined
to **39,856 B** during the longer session. It is a historic low, not
current available RAM, and the corresponding minimum 8BIT value was not
captured. Do not subtract current INTERNAL−8BIT pool gaps from an
all-time per-region low watermark as though they were simultaneous.
The existing `/api/system/memory/pools` endpoint already includes
`min_free` for `byte_8bit` (from ESP-IDF allocator low-water data), so
**request that JSON next without reflashing**. Sustained stress tests
should also check simultaneous WebSocket/Telnet/large CRT/updater-page
responses and modem reconnects. Do not issue actual OTA writes during
stress experiments. Physical field results are strong evidence that the
original problem has been resolved, not exhaustive proof against future
heap exhaustion.

**Next direction:** keep byte-addressed 64-KiB Z80 RAM unchanged.
Do not prematurely shrink task stacks or rewrite CPU memory mapping.
Preserve this tested build/commit as a regression baseline; experimental
branch remains separate from `main` until final acceptance decision.

## Diagnostic endpoint routing correction (21 October 2026)

User reported that requests to `GET /api/system/memory/pools`
returned the *same* JSON as `GET /api/system/memory`, rather than
the advertised `pools` array. This is a genuine server-routing bug:
ESPAsyncWebServer 3.6.0 permits a callback registered on `/memory`
to match the child path `/memory/pools`, and selects the first
registered match. The shorter route had been registered first.

**Fixed on development branch:** the more-specific `/memory/pools`
handler now registers **before** `/memory`, with a static regression
assertion enforcing the order. We additionally expose
`min_free_8bit` (ESP-IDF's `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)`)
on the existing small memory endpoint for easier stress testing.
Neither the SPI disk cache, Telnet/CP/M, modem workers nor OTA flash
handler was modified. This change requires a new ESP32 application
firmware upload to take effect; it is not yet physically validated.

After a refreshed build is installed, verify that `/api/system/memory`
returns `min_free_8bit` and `/api/system/memory/pools` returns
`pools`, including the `byte_8bit` entry with `min_free`. The
historic minimum is an aggregate of per-region low-water marks, not
necessarily an exact simultaneous global low-water value.
The previous hardware-validated application may safely remain installed
until it is convenient to run this diagnostics-only regression.

## Why 520 KiB is not available as a single 520-KiB heap

Espressif documents the classic ESP32 as having **520 KiB internal SRAM**:
approximately **320 KiB DRAM** and **200 KiB IRAM**. Portions are reserved
for instruction caches and executable code; others hold statically linked
variables, the Arduino framework, FreeRTOS and Wi-Fi/LwIP allocations.
Only unallocated regions enter the capability-based heap allocator.

Important: **the available heap capability sets OVERLAP.** A DRAM region
can be both INTERNAL and 8BIT/DMA/32BIT, while instruction RAM may permit
aligned 32-bit access but not ordinary byte loads. Do not sum the totals
returned for INTERNAL, 8BIT, 32BIT, EXEC and DMA.

From the physical HP-85 system, the difference between
`ESP.getFreeHeap()` (internal) and free MALLOC_CAP_8BIT was 47,416 B
at several distinct startup stages:

| Stage | INTERNAL free | 8BIT free | Largest 8BIT | Non-largest 8BIT |
|---|---:|---:|---:|---:|
| Web started | 121,744 | 74,328 | 73,716 | 612 |
| TFT started | 85,720 | 38,304 | 36,852 | 1,452 |
| Modem worker started | 55,740 | 8,324 | 7,924 | 400 |

These figures establish **shortage of byte-accessible capacity**, not
severe 8-bit heap fragmentation. The other 47,416 B are not ordinary
byte-addressable heap. They are likely principally unused instruction
RAM, but the new capability endpoint must verify this rather than infer
exact memory-region boundaries from one subtraction.

## Confirmed TFT linker inventory (CI)

The first ELF report from [CI run 39575413661](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39575413661) shows:

| ELF section | Exact size |
|---|---:|
| `.dram0.bss` | 96,112 bytes |
| `.dram0.data` | 28,400 bytes |
| **Total statically linked DRAM** | **124,512 bytes** |
| `.iram0.text` | 81,811 bytes |
| `.iram0.vectors` | 1,027 bytes |

The `RAM` symbol is exactly **65,536 bytes**, more than half the linked
static DRAM. Other confirmed DRAM symbols include `g_cnxMgr` (3,800 B),
`z80DebugBuffer` (2,048 B), `cpmDesiredMounts` and `cpmActiveMounts`
(1,584 B each), and `rxBuf` / `txBuf` (1,024 B each).

**Important interpretation:** The initial unfiltered `nm` output also
listed flash-backed `const` HTML and BASIC ROM with lowercase
`d` symbol codes. These must **not** be counted as DRAM. The reporting
script now filters actual ESP32 DRAM addresses (`0x3FFAE000` through
`0x3FFFFFFF`) and separately shows flash-mapped constants.
The `size -A` section figures above are authoritative for static RAM.

## Audited firmware allocations

The figures below are allocations visible directly in application source
or stated stack reservations, **not** a complete accounting of library/OS
memory. Runtime allocations include allocator overhead and vary by boot,
network clients and ESP-IDF configuration.

| Component (source) | Bytes | Allocation / lifecycle | Notes |
|---|---:|---|---|
| **Z80 memory** (`globals.h`) | **65,536** | `uint8_t RAM[65536]` static global | Full byte-addressable address space; currently permanent static DRAM |
| Console UART (`globals.h`) | 2,048 | `rxBuf` + `txBuf` static | Ordinary byte rings; do not relocate to word-only IRAM |
| Emulator port state (`globals.h`) | 512 | `pIn` + `pOut` static | Byte I/O state |
| Opcode counters (`globals.h`) | 2,048 declared | `POP[256]` and `POPcb[256]`, uint32_t static | Only actual use in legacy CPU source is commented out; linker may already discard; inspect ELF report |
| Debug ring (`z80_debug.h`) | 2,048 | Static log | Preserve for crash diagnostics |
| Browser -> Teensy command queue (`ebtks_web.h`) | 4,096 payload | 8 elements × 512 B, runtime FreeRTOS queue | Queue metadata extra |
| **CP/M warm-boot snapshot** (`cpm_warmboot.h`) | **5,632** | Heap `malloc(CPM_SHADOW_SIZE)` | Resident CCP+BDOS backup, retained for warm boot |
| **CP/M SPI read-ahead** (`spi_disk_read.h`) | **8,192** | `MALLOC_CAP_8BIT` heap cache | Not freed when user disables SPI; worker/DMA may still refer to it |
| **SPI disk worker stack** (`spi_disk_read.h`) | **8,192** | FreeRTOS stack | 6,324 B unused in one physical snapshot; not peak-stress proof |
| SPI DMA buffers (`spi_link_test.h`) | 1,056 | RX+TX 528 B each, DMA-capable internal heap | Driver descriptors and other overhead extra |
| TFT VT100 screen (`tft_terminal_vt100.h`) | At least 5,760 | 1,920 character bytes + 3,840 style bytes; heap-allocated screen | Additional parser and dirty-region data; no framebuffer |
| TFT worker stack (`ebtks_tft_terminal.h`) | 4,096 | FreeRTOS stack | 2,976 B unused in one measurement |
| CPU worker stack (`ESP32_Z80_Emulator.ino`) | 4,096 | FreeRTOS stack | Core-1 Z80 engine |
| Console router stack (`ESP32_Z80_Emulator.ino`) | 3,072 | FreeRTOS stack | Active Telnet/Web/TFT console output |
| Web background stack (`ebtks_web.h`) | 4,096 | FreeRTOS stack | Required for Web/OTA and shared Telnet listener |
| HP keyboard worker stack (`ebtks_cpm85_keyboard.h`) | 3,584 | FreeRTOS stack | HP85 input bridge |
| Telnet acceptor stack (`telnet.h`) | 3,072 legacy; **0 dedicated** on new branch | Shared Web task by default | Optimization committed, requires hardware Telnet regression |
| Wi-Fi modem worker (`ebtks_modem.h`) | 5,120 stack + approximately 2 KiB buffers/objects | Created on first access to I/O ports 88h..8Fh | Guard refuses TCP dial when 8BIT-largest block too small |
| ESP-IDF/Arduino/AsyncTCP/Wi-Fi | Not determined in source | RTOS/stack, network sockets and heap buffers | Quantify with early boot and region checkpoint deltas |

The Z80 array alone consumes **12.3% of nominal 520 KiB**,
or **20% of the nominal 320 KiB DRAM**. The main reason it is so visible
is that unlike network/CPM buffers it is statically allocated before
`setup()` and so never appears as a drop in our heap snapshots.

**Beware linker removal:** Declarations of unused arrays are not proof
that the compiled binary retains them; the ELF symbol audit decides.
Similarly, `const` ROM and Web page arrays can live in flash.

## What is still missing from that 520-KiB total?

Static code and data have to be read from the actual compiled ELF.
Wi-Fi/Arduino/FreeRTOS have already allocated memory by the time user
`setup()` is called. The new `arduino_setup_entry` snapshot is the
earliest app-level baseline, *not* a chip reset baseline.

To make the accounting measurable, three diagnostic channels are added:

1. `GET /api/system/memory` now includes `arduino_setup_entry` and
   `cpm_boot_ready`, bracketing ESP32 Web/Wi-Fi startup and CP/M shadow
   initialization. Existing phase snapshots and per-task stack high-water
   metrics remain unchanged.
2. `GET /api/system/memory/pools` exposes per-capability heap
   `total`, `allocated`, `free`, `largest`, `min_free` and free/allocated
   block counts. Each row is independent and capability sets overlap.
   `total` means managed allocator region bytes, not all physical RAM.
3. `tools/report_esp32_memory.py` reads the ELF with Xtensa
   `size` and `nm`, reporting static DRAM/IRAM sections and largest
   BSS/DATA symbols. The GitHub build uploads artifacts
   `esp32-memory-reports`, including reports for both TFT and headless.

From the ESP32 repository:

```sh
git switch feature/cpm-wifi-modem
git pull --ff-only
pio run -e esp32dev-tft
python3 tools/report_esp32_memory.py .pio/build/esp32dev-tft/firmware.elf
```

The same report is created in CI and can be inspected without flashing.

## Could we put the Z80's 64 KiB into 32-bit-only RAM?

**Not by changing a type in one place.** The selected Z80 core
(`cpu_redcode.h` and `z80_bridge.h`) takes a `uint8_t* memory`
and calls byte-oriented memory callbacks for every instruction fetch,
read and write. CP/M boot/disk/BIOS code directly accesses
`RAM[address]`, passes `&RAM[address]` to byte-copy routines and
depends on `sizeof(RAM)==65536`. Replacing this with packed uint32_t
memory requires refactoring **all** consumers, DMA/disk byte transfers,
and performing rigorous Z80 regression tests.

A packed implementation might implement byte reads by aligned
32-bit word loads, shifts and masks; writes require read-modify-write.
That is safe for `MALLOC_CAP_32BIT` **only if the actual allocator
returns adequately aligned word-accessible storage** and no code ever
performs byte or unaligned accesses to it. It is not inherently faster
just because Xtensa instructions are 32-bit: the Z80 remains byte-addressed,
and mask/shift overhead may make the emulator slower.

Moreover the observed extra free internal-but-not-8BIT memory is
~47 KiB, **less than the 64 KiB Z80 image**. A hybrid memory map
could move a subset of RAM pages after verifying the available
word-only heap and measuring throughput, but it is a much larger
refactor than optimizing an existing read-mostly buffer.

### Safer first 32-bit RAM candidate

The 5,632-byte CP/M warm-boot snapshot is accessed only when taking
or restoring a resident CCP/BDOS image. It could be stored as
`uint32_t` words in `MALLOC_CAP_32BIT` memory with explicit 32-bit-only
packing/unpacking, preserving full 8-bit CP/M/Z80 RAM elsewhere.
With enough unused 32-bit-only heap, that may release ~5.5 KiB
ordinary byte-capable DRAM without affecting hot opcode fetches.
It still requires careful fallbacks and native/hardware tests.

The present diagnostic build **does not move that allocation**;
it also leaves the modem dial guard, CP/M BIOS, SPI/TFT and Z80 memory
mapping unchanged.

## Experimental SPI read-ahead IRAM32 cache (2026-10-21)

The physical memory issue makes the SPI cache an excellent early target:
the CRC-validated **8,192-byte, 16-sector read-ahead cache** in
`spi_disk_read.h` is *not* the peripheral DMA buffer. A 528-byte
DMA receive frame is already transferred and validated in ordinary
DMA-capable DRAM before committing its 512-byte payload into the
read-ahead cache. The cache can therefore live in word-accessible IRAM.

**Implemented on the existing experimental modem branch**, enabled by
default for controlled tests; no Z80 RAM address-map changes:
- New `spi_cache_words.h` packs each 512-byte sector into 128 aligned
  `uint32_t` words, and performs the inverse unpack on sector fetch.
  The actual IRAM pointer is **never passed to memcpy or byte-indexed**.
- Prefer an 8-KiB `MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT|MALLOC_CAP_EXEC`
  allocation, and verify at runtime that free MALLOC_CAP_8BIT has not
  decreased. If no appropriate region can be used, free the tentative
  allocation and fall back to the **original** MALLOC_CAP_8BIT cache.
- Existing 528-byte RX and TX DMA buffers, CRC/frame handshake,
  read-ahead geometry, Z80 memory and FreeRTOS stacks are unchanged.
  DMA buffers cannot be relocated to 32-bit-only IRAM. Nor can the
  SPI worker's 8-KiB task stack be moved there directly.
- `GET /api/cpm/spireads` reports `cache_storage: "iram32"`,
  `"dram8"`, or `"none"` as well as cache bytes and backend default.
  `GET /api/system/health` exposes `spi_cache_storage`.
- Compile-time A/B opt-out: `-DEBTKS_SPI_CACHE_WORD32=0` restores
  the old DRAM-only cache without changing CP/M behavior.
- Native 16-sector pack/unpack tests verify every byte, boundary,
  independently rewritten sector, and isolation of adjacent sectors.
  ESP32 platform builds and actual 4-MHz SPI/CP/M media benchmarks
  remain required for hardware acceptance.

**Expected improvement:** If `cache_storage=iram32` is reported,
we expect roughly **8,192 extra bytes of free 8-bit heap** compared
with the old DRAM cache. Generic total internal free heap may be
approximately unchanged because 8 KiB is *relocated* to a different
internal memory region, not actually freed. DMA and stack allocations
remain in byte-capable DRAM. The reserve may be enough when combined
with the Telnet task consolidation and (later) 5,632-byte packed
CP/M shadow relocation, but this must be confirmed while opening
full Web pages and then using the modem. Until validated, the modem
dial admission guard remains unchanged. Keep Teensy USB serial
recovery available. **Never infer that a successful native/CI build
is a physical SPI hardware test.**

## Next controlled experiments

First capture the CI ELF report, then compare hardware pool metrics at
fresh boot, CP/M prompt, Web Terminal session, MODEM/AT/ATI. Confirm
that the Web UI remains usable throughout. Next prototype the
word-packed CP/M snapshot with a compile-time A/B switch and tests.
Only then consider shrinking SPI task stacks, on-demand reclamation or
hybrid 32-bit Z80 memory, each measured separately.

**Rollback:** The proven Teensy USB `ESP32 Prog` recovery procedure is
in `ESP32_RECOVERY_USB.md`; stable `main` remains untouched.

## Espressif references

- https://docs.espressif.com/projects/esp-idf/en/v5.1.1/esp32/api-guides/memory-types.html
- https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/system/mem_alloc.html
