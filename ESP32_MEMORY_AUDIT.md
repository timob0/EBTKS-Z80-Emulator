# ESP32 SRAM inventory and Z80 32-bit IRAM feasibility

**SIMON85 / EBTKS classic ESP32, experimental `feature/cpm-wifi-modem` branch.**
Source audit completed 21 October 2026. Runtime capability instrumentation and
ELF static symbol reports have been committed, but still need physical test.

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
