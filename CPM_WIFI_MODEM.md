# CPM Wi-Fi modem -- development milestone (2026-10-21)

The ESP32 now contains a **separate, polling 8250-compatible virtual modem**
at Z80 I/O ports **88h..8Fh**, alongside the existing CP/M console at
80h/85h. This subsystem was developed on `feature/cpm-wifi-modem`, successfully
merged to stable `main` via [PR #6](https://github.com/timob0/EBTKS-Z80-Emulator/pull/6)
on 21 October 2026 after physical HP-85 acceptance. Historical diagnostics
and experimental limitations below document its development history.

## Build validation (source commit `38bb93ea`)

GitHub Actions run [39518316821](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39518316821)
has **passed** native Z80/8250/Hayes tests, MODEM.COM generator check,
both headless and ST7796 builds, and headless SPI-off regression.

| Build | Flash | OTA limit | Static RAM |
| --- | ---: | ---: | ---: |
| ESP32 headless | 1,129,165 B | 1,310,720 B | 123,280 B |
| ESP32 ST7796 TFT | 1,191,033 B | 1,310,720 B | 124,504 B |

The TFT image has **119,687 bytes available** in the OTA slot.
Static DRAM was very close to its linker limit: the modem's UART, Hayes,
socket and network FIFOs were dynamically allocated as a result.
**A later hardware test confirmed successful AT commands and initial BBS
connection, but sustained network use and concurrent Web UI behavior
remain untested.** Save the previously verified firmware for rollback.

## Hardware regression identified (2026-10-21)

**Physical user report:** after flashing first modem firmware, the ESP32
`/z80`, `/cpmmedia` and `/crt` pages terminated HTTP connections
unexpectedly. Only the firmware updater page loaded. A low-heap condition
is the leading explanation, **not yet confirmed by runtime health figures**.

**Recovery hardening:** instead of allocating the modem UART FIFOs, Hayes
parser, WiFiClient and dedicated 5120-byte task stack during ESP32 boot,
the feature now leaves the modem completely dormant until a CP/M application
reads or writes port `88h`–`8Fh`. It then requests activation through the
existing low-priority Web worker. While activation is pending, `LSR` reports
not-ready, so correctly polling clients such as MODEM.COM wait. The modem
startup is denied rather than starving the UI if heap is below 48 KiB or
largest allocatable 8-bit block is below 8 KiB. When allocation is
permanently refused, the polling UART reports transmitter ready (but
silently discards data) so that the MODEM.COM utility can still exit
cleanly with Ctrl+X rather than spin indefinitely. Startup state `0`=idle,
`1`=requested, `5`=initializing, `2`=active, `3`=low-memory refusal,
`4`=allocation/task failure is included in `/api/system/health`.

The large Z80 terminal HTML is now served directly from flash (like
Media Manager and CRT), eliminating a ~17 KiB transient heap copy. The
modem's changed startup was subsequently **physically validated for
boot, AT/ATI and outbound BBS dialing** (see below). If a regression
recurs, use the proven Teensy USB serial recovery path documented in
`ESP32_RECOVERY_USB.md`; Web OTA was inoperative on the earlier build.

## Physical HP-85 acceptance -- corrected modem, successful real BBS dial (2026-10-21)

**User hardware result:** Corrected firmware from `feature/cpm-wifi-modem`
(lazy worker activation + flash-backed large Z80 page) was flashed
successfully. All previously failing Web UI pages initially functioned
normally. The CP/M program `MODEM.COM` on F: displayed its greeting,
then `AT` returned `OK` and `ATI` returned `EBTKS Wi-Fi modem 0.1`
plus `OK`. Dialing `ATDTvert.synchro.net:23` yielded `CONNECT 9600`
followed by a genuine Synchronet BBS banner and additional remote text.
This demonstrates real hardware Z80 I/O -> Hayes parser -> ESP32 Wi-Fi
TCP -> remote BBS receive. Do not equate this with a tested sustained
session, ANSI/VT100 fidelity, upload/download, or concurrent Web UI stability.

Two user-provided health snapshots, each with SPI read-ahead/cache/worker
enabled and `spi_read_failures=0`:

| Field | Before MODEM (uptime ~56 s) | After AT/ATI (uptime ~302 s) |
| --- | ---: | ---: |
| `heap_free` | 65,400 | 55,976 |
| `heap_min_free` | 47,588 | 47,588 |
| `largest_free_8bit_block` | 16,372 | 5,620 |
| `internal_free` | 65,400 | 55,916 |
| `internal_largest_block` | 47,092 | 47,092 |
| `cpm_modem_state` | 0 (dormant) | 2 (active) |

**Important memory finding:** 9,424 bytes less total free heap and a
10,752-byte reduction in the largest free 8-bit block (down to **5,620 B**).
Large HTTP requests may still suffer under concurrent BBS traffic, even
though the lightweight `/api/system/health` endpoint worked with active
modem. The reported `heap_min_free` is a lifetime low-water mark and
does not distinguish the immediate cause of this change.

**Next physical checks before merging:** Try `/z80`, `/cpmmedia`,
`/crt`, and GET `/updater` *while connected to the BBS* and report
current free/largest heap; interact with BBS to check input and
terminal output, exit using guard-time `+++` / `ATH`, confirm
CP/M `Ctrl+X` exit and K8 detach, and recheck heap over reconnections.
Do not upload firmware as an updater test. No OTA flash or Teensy changes
needed now. Modem development branch remains **unmerged**.

## Physical freeze under live BBS traffic; staged RAM investigation (2026-10-21)

**User clarified:** After successfully receiving the remote Synchronet BBS
banner, the physical ESP32 became unresponsive: *all* Web UI pages and
even the separate Wi-Fi Telnet service could not be reached. Power cycling
restored functionality. This **supersedes** any interpretation that the
first live BBS dial passed overall acceptance; the branch is NOT safe to
merge and runtime network functionality remains experimental.

The prior snapshot *after AT/ATI and before the dial* was already
`heap_free=55976` but `largest_free_8bit_block=5620`, down from
`16372` before launching MODEM. Fragmentation / contiguous RAM shortage
is a leading hypothesis, **not proof** of the freeze's cause. Other
possibilities include the modem worker blocking the Wi-Fi stack,
CPU/TFT/terminal backpressure, or a watchdogless deadlock.

**Conservative diagnostic change:** Save low-overhead free/contiguous
heap snapshots after Web startup, Teensy probe, core tasks, keyboard,
SPI cache, TFT, modem worker, before/after TCP connect and on hangup.
A new read-only endpoint `/api/system/memory` reports those stage
snapshots and unused-stack high-water readings for the Web, CPU, console,
Telnet, keyboard, SPI, modem and TFT workers. In the modem worker,
**refuse an outbound ATD/ATDT TCP dial before calling WiFiClient::connect**
unless free heap is at least **48 KiB** and the largest allocatable 8-bit
block is at least **12 KiB**. Refusal emits `MEMORY LOW - DIAL REFUSED`
and `NO CARRIER` and is counted in health and memory endpoints. This
threshold is intentionally conservative rather than a proven minimum.
The previously observed active modem's 5.6 KiB largest block would
**refuse the dial** instead of risking the entire ESP32/OTA interface.
`AT`, `ATI`, `MODEM` and CP/M/TFT/Web console continue to work.

**Next development cycle:** Run ONLY the instrumented memory profile first
before reducing any task stack. Identify oversized stack reservations,
SPI/TFT footprint, allocation fragmentation and the benefit of sharing
existing network worker stacks or shrinking FIFO sizes. Test independent
HTTP/Telnet/Web UI operation before attempting another Internet dial.
Do not weaken the dial guard until a controlled optimization demonstrably
restores robust contiguous space. The fixed version is *not yet hardware
validated* and must not be flashed without rollback readiness.

## Proposed adaptive service lifetime -- design, not yet implemented

The user proposes dynamically starting/stopping services and requesting
consent to release resources when the ESP32 is under memory pressure.
That is a good longer-term design, but tasks and browser pages are not
one-to-one. Prioritize graceful, reversible shedding:

| Component | Current resource / lifetime | Possible safe memory-management action |
|---|---|---|
| HTTP server, AsyncTCP, Web background | Shared network core and 4 KiB background task | **Always retain** for management, OTA and recovery. Do not shut down to save RAM. |
| Z80 browser terminal | WebSocket handler on common server, **not** a dedicated task | Allocate per-client network buffers only while connected; close inactive WebSocket sessions. Handler registration itself is cheap. |
| HP remote CRT/snapshot | Client-side page using common `/ws`; **not** a dedicated task | Send alpha snapshots only while browser subscribes; allocate graphics capture buffers only while capturing; release when done/disconnected. |
| Incoming Telnet console | Dedicated 3072-byte `TelnetTask` + session socket | Integrate lightweight incoming connection acceptor into network supervisor; no separate Telnet polling task when idle; socket allocated on accept and closed on disconnect. Must preserve listen availability. |
| ST7796 mirror | Dedicated 4096-byte task + 80x24 state + graphics allocations | Optional user-selected suspend/restore after renderer is quiescent; preserve a path to restart and clear/redraw. Never force-disable visible TFT without permission. |
| SPI disk read-ahead | Dedicated 8192-byte task + 8192-byte cache, worker may hold DMA state | Optional fallback to UART with *safe worker teardown and buffer release after in-flight I/O completes*. Merely turning off SPI transport currently **does not free its cache/task**, by design. |
| Physical HP-85 keyboard | Dedicated 3584-byte polling task | On-demand polling while keyboard captured; sleeping an existing task saves CPU, **not its stack RAM**. Need safe lifecycle or sharing before claiming RAM savings. |
| Modem | Dedicated 5120-byte worker + 1 KiB UART FIFOs, socket and other bounded buffers | Already lazy at first use. On explicit hangup/dispose, eventually release socket and worker *after a serialized quiescence handshake*; automatic shutdown on modem inactivity must not break arbitrary CP/M serial programs. |
| Z80 CPU/console | 4096-byte CPU / 3072-byte console tasks + 64 KiB Z80 RAM | **Keep running** for CP/M operation; do not stop to optimize modem memory. |

A future **Memory Manager** Web UI can show used/free DRAM, largest
allocatable 8-bit block, per-task stack high-water margins and
connection/activity state. It should offer clear *user-approved*
actions, e.g. "Disconnect BBS", "Disconnect inactive Web terminal",
"Pause TFT mirror", or "Switch CP/M disk reads to UART and release SPI
resources when idle", with documented performance/UX trade-offs.
Once capacity falls below defined reserves, proactively refuse *new*
resource-heavy operations and present a warning **while the existing
Web/OTA control plane still works**. It should not terminate active
CP/M disk I/O, HP keyboard capture, or OTA uploads.

These changes require a staged design and physical tests; simply
calling `vTaskDelete` or `vTaskSuspend` without owning all objects,
DMA transfers and task references risks deadlocks and use-after-free.
First collect `/api/system/memory` diagnostics on the guarded branch
before choosing which stack or cache should actually be reclaimed.

## Incremental optimization: Telnet listener shares Web task (2026-10-21)

**Live physical memory profile confirmed severe 8-bit heap pressure.**
After reboot: `heap_free=65796`, largest free 8-bit block 17,396.
After opening Web terminal: 62,048 and 13,812.
After MODEM + AT/ATI: 55,504 and **6,900**. ATDT
was correctly rejected by the guard (`NO CARRIER`) **but ordinary
Web pages subsequently became unreachable**, while the very small
`/api/system/memory` handler continued working. This is *selective*
HTTP memory failure, not proof of a complete ESP32 crash.
At `modem_task` checkpoint the full 8-bit heap was only 8,324
bytes even though total free internal heap was 55,740: approximately
47 KiB was unavailable for normal byte-oriented allocations.
The previously observed failed dial never reached `WiFiClient::connect`.

**First isolated component optimization**: remove the permanently running
3,072-byte **TelnetTask** from the default configuration and fold
`telnetPoll()` (inbound accept only) into the already-existing
`EBTKSWebTask`. TCP port 23 remains listening at all times; CP/M console
I/O for connected clients is unchanged and remains in `ConsoleTask`.
This should save the Telnet task stack and task metadata without altering
HP keyboard, SPI disk worker, TFT renderer, modem, OTA or WebSocket
semantics. Compile-time A/B override
`-DEBTKS_TELNET_SHARED_WORKER=0` restores the old TelnetTask.
`/api/system/memory` now exposes live `free_8bit` and the Telnet
listener mode/connected flag, so savings can be compared on physical
hardware. **Not yet physically verified; do not claim free 8-bit savings
or Telnet connect/close behavior until tested.**

Next, consider *separate* controlled reductions to SPI worker's large
reserved stack (8192 bytes, observed unused HWM 6324) or an
on-demand worker lifecycle. Do **not** shrink Web/OTA and TFT or
indiscriminately suspend asynchronous SPI/DMA operations.
The memory profile and modem dial guard remain enabled; retain serial
recovery. The modem feature stays unmerged.

## Heap-capability diagnosis -- **not primarily fragmentation** (2026-10-21)

**Correction to earlier speculation:** On the classic ESP32 the Arduino
`ESP.getFreeHeap()` sums `MALLOC_CAP_INTERNAL`, including internal
regions unavailable to ordinary byte-addressable `malloc()`. The
8-bit figures from `heap_caps_get_free_size(MALLOC_CAP_8BIT)`
and `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` must be
compared *to one another*, not directly to `ESP.getFreeHeap()`.

Physical checkpoint data:

| Stage | Free INTERNAL | Free 8-bit | Largest 8-bit | Free 8-bit outside largest |
|---|---:|---:|---:|---:|
| Web ready | 121,744 | 74,328 | 73,716 | 612 |
| TFT ready | 85,720 | 38,304 | 36,852 | 1,452 |
| Modem task | 55,740 | 8,324 | 7,924 | 400 |

**All checkpoints have precisely a 47,416-byte difference between
internal heap and free 8-bit heap.** Likely much of that free internal
memory is 32-bit-only IRAM; only appropriately aligned, 32-bit-only
data structures can safely use it with `MALLOC_CAP_32BIT`. Byte buffers,
strings, DMA buffers and general network structures cannot be moved
there indiscriminately. An accidental byte load/store on such memory
can crash the classic ESP32.

At refused dial, `heap_free=55944`, `largest8=7924`; assuming
the 47,416-byte category offset remained constant, free8 was ~8,528 B,
so only ~604 B lay outside the largest chunk. **This is almost
entirely a 8-bit memory *capacity* problem, not severe fragmentation.**
The Web UI failing while a tiny JSON endpoint still answers is
consistent with memory-dependent allocations failing, but allocation
failure must be instrumented before asserting that as the full cause.

**FreeRTOS does not have a safe online compactor** that relocates live
tasks, WiFi buffers and C++ allocations. Freeing memory coalesces
adjacent blocks, but changing allocation order alone cannot turn the
32-bit-only pool into normal byte-addressable DRAM.

**Optimization priority:** preserve Web/OTA reserve; compare live
`free_8bit` and `largest_8bit`; audit peak task-stack high-water
marks before controlled stack reductions; reclaim SPI cache/worker only
after implementing proper DMA/worker quiescence; avoid unnecessary
network/client buffers; consider `MALLOC_CAP_32BIT` only for verified
word-only structures. A separate startup-versus-post-boot allocation
delta exists: `TFT_READY` occurs before full CP/M boot and the first
SPI disk reads, so the fall from the TFT checkpoint to idle runtime
must *not* all be attributed to TFT itself.

Official references:
- https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/Esp.cpp
- https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32/api-reference/system/mem_alloc.html
- https://docs.espressif.com/projects/esp-idf/en/v5.1.7/esp32/api-reference/system/heap_debug.html

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
The next low-cost observability improvement could show
`heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)`, and sustained stress
tests should check simultaneous WebSocket/Telnet/large CRT/OTA page
responses and modem reconnects. Do not issue actual OTA writes during
stress experiments. Physical field results are strong evidence that the
original problem has been resolved, not exhaustive proof against future
heap exhaustion.

**Next direction:** keep byte-addressed 64-KiB Z80 RAM unchanged.
Do not prematurely shrink task stacks or rewrite CPU memory mapping.
Preserve this tested build/commit as a regression baseline; experimental
branch remains separate from `main` until final acceptance decision.

## Web terminal parity with ST7796 ANSI/CP437 (2026-10-21)

Updated browser `/z80` terminal (flash-backed `EBTKS_TERM_PAGE`) to
render the **same 16 ANSI standard/bright foreground/background colors**
as the TFT, including SGR bold, underline, inverse, default-color reset,
cell-background-aware clearing, and correct skip of unsupported 256-color
and truecolor commands. The physical TFT's 16-color RGB565 palette was
converted to CSS RGB and its default green/black foreground/background
preserved. Raw bytes 0x80..0xFF now map to **IBM CP437** Unicode glyphs
rather than Latin-1; DEC G0/G1 Special Graphics maps full TFT-supported
line drawing and symbols. 80x24 VT100, HP86/Soroc key mode and raw mode
are retained. Adjacent equal-style cells are batched into a common
HTML span to avoid 80 independent elements for a colored line.

Changes affect only the browser-side JS and CSS stored in flash; no new
FreeRTOS tasks or network buffers, and unchanged Z80 I/O, modem, SPI
and TFT native parser. `tests/web_terminal_browser_native.js` runs the
**actual embedded browser code** inside a simulated DOM + WebSocket
under Node.js and verifies ANSI, CP437, DEC graphics, styles, HTML
escaping and HP backward compatibility. CI executes it with the native
regressions. Hardware acceptance is still needed after confirming
the TFT firmware CI build is green. Preserve the proven stable IRAM
cache and BBS firmware during initial browser validation.

## Architecture

- The Z80 port I/O dispatch calls **only** `CpmWifiModem::readPort/writePort`,
  a short FreeRTOS critical section around heap-free UART register logic;
  DNS, sockets, TCP and any blocking network work **never run in CPU code**.
- One low-priority core-0 worker owns a single `WiFiClient`, the Hayes
  parser, protocol state and network send queues.
- Modem receive and transmit FIFO buffers (512 each), plus a 512-byte TCP
  transmit queue and 256-byte response queue, are allocated on the ESP32
  runtime heap **on first virtual modem port use**. Allocation failure leaves the device inactive
  and records a message in `/z80debug`. They are not statically allocated
  in the scarce linker `.dram0.bss` section; the WiFiClient and
  Hayes parser instances are likewise allocated on the runtime heap.
- Existing HP-85/Teensy keyboard capture, CP/M boot/media, ESP32 OTA,
  WebSocket/Telnet server, and TFT routing are unchanged.

## Creating a transferable 8 MiB disk image

The 183-byte terminal executable is called **`MODEM.COM` on CP/M**.
The historical development name `MODEMTERM.COM` contains nine characters
before the extension, which exceeds the CP/M 2.2 8.3 filename limit.
The `tools/build_modemterm.py` generator now defaults to `MODEM.COM`.
In a raw EBTKS 8 MiB image (DSM=2047, BSH=5, DRM=511, AL0=F0h,
OFF=0), its first directory entry belongs to user 0, spans two 128-byte
records, and places the program at allocation block 4 (offset 16 KiB).
An image containing this program is ideal for mounting temporarily to
B: and copying into an existing A: with `PIP A:=B:MODEM.COM`. Never overwrite
the existing mounted A.dsk to transfer the utility.

## 8250 register map

| Z80 I/O | 8250 register | Initial support |
| --- | --- | --- |
| `88h` | RBR (read) / THR (write); DLL when DLAB=1 | buffered RX/TX |
| `89h` | IER; DLM when DLAB=1 | register preserved; no interrupts yet |
| `8Ah` | IIR (read) / FCR (write) | no IRQ asserted; FIFO clear |
| `8Bh` | LCR | divisor latch access, mode bits stored |
| `8Ch` | MCR | stored |
| `8Dh` | LSR | bit0 data ready, bit1 RX overrun, bit5 TX ready, bit6 TX empty |
| `8Eh` | MSR | CTS+DSR asserted; DCD only when TCP connected |
| `8Fh` | SCR | read/write |

This is an intentionally **polling-only subset**, not full interrupt-level
16550 emulation. The baud divisor is retained but does not throttle TCP.
FIFO fullness is visible via LSR bit5: software should poll it rather
than blindly sending unlimited bytes. Socket read is gated on free RX space
to apply backpressure.

## Hayes-style commands

Commands use normal ASCII ending in carriage return on the **modem device**.
Do **not** type them at the plain CP/M CCP prompt; use the MODEM utility
or a communications program adapted to serial ports 88h..8Fh.

| Command | Action |
| --- | --- |
| `AT` | Returns OK |
| `ATI` | Modem identity and OK |
| `ATDhostname:port` or `ATDThostname:port` | DNS + outbound TCP connection |
| `ATDhostname` | Defaults to Telnet port 23 |
| `ATH` | Close TCP connection |
| `ATZ`, `AT&F` | Reset command options; ATZ also hangs up |
| `ATO` | Resume online mode after escape |
| `ATE0`, `ATE1` | Local modem echo off/on |
| `ATV0`, `ATV1` | Numeric/text result codes |
| `ATQ0`, `ATQ1` | Results on/off |
| `+++` | In online mode: guarded escape after 1s idle, 1s afterward |

Result codes are OK, CONNECT 9600 (simulated), NO CARRIER, ERROR; numeric
codes are 0, 1, 3, 4 respectively. DNS/connect may block only the **modem
worker**, with a 3-second socket connect timeout; the Z80 CPU and existing
console remain responsive. Wi-Fi must already be configured for EBTKS.

Telnet negotiation is stripped from the receive stream; basic
WILL/WONT/DO/DONT are handled for ECHO, SGA and BINARY, with support for
80x24 NAWS and ANSI terminal type. IAC bytes in remote payload are
unescaped; outbound literal 0xFF is escaped. NVT CR is sent as CR NUL
unless binary mode is negotiated. The exact negotiation behavior with
individual modern BBS servers still needs **real-hardware testing**.
Plaintext Telnet is unencrypted. Do not reuse important passwords.

## Building and testing on HP-85

1. Wait for native + ESP32 headless + ST7796 builds to be green in
   GitHub Actions for the latest feature-branch commit.
2. Pull ESP32 `feature/cpm-wifi-modem`, build
   `pio run -e esp32dev-tft`, and OTA flash the generated
   `.pio/build/esp32dev-tft/firmware.bin` through existing Web UI.
   **No Teensy or HP CPM85 LOADBIN changes**.
3. Create a stand-alone CP/M-80 COM file on the host:
   `python3 tools/build_modemterm.py -o MODEM.COM`.
   This produces the same small Z80 program documented in
   `tools/modemterm.asm`, without requiring an assembler.
4. Place `MODEM.COM` on a writable CP/M disk image using the
   established SD-card/image transfer workflow. Start CP/M and type
   `MODEM` at its `A>` or other CCP prompt. Physical HP keyboard
   requires the user-tested `CPM` HP command to be attached; the
   existing TFT/Web/Telnet terminals remain alternatives.
5. Type `AT` + Enter: expect OK. Then `ATI`; then try
   `ATDTvert.synchro.net:23` and look for a BBS greeting.
   These are **test instructions**, not claimed live availability.
6. Leave the BBS with its logout command, or let 1s elapse,
   type `+++`, leave another 1s, type `ATH` and Enter.
   **Ctrl+X quits MODEM** back to CP/M. **HP K8** returns keyboard
   control to HP BASIC without stopping the CP/M VM.

If MODEM is not on the disk, the Z80 modem can also be driven by
a separately prepared MEX/IMP machine overlay once port compatibility
is verified.

## Status / boundaries

- Native 8250 register semantics, Hayes command parsing, and deterministic
  MODEM image generation are regression-tested on the CI runner.
- ESP32 runtime heap allocation, UART I/O integration and firmware build are
  covered by headless and ST7796 CI builds.
- Actual outbound TCP/BBS connectivity, high-volume file transfers,
  per-server ANSI negotiation and physical device stress tests are **pending**.
- No CPU interrupt behavior, TLS, dialup speed emulation, XMODEM flow control
  or CP/M BIOS RDR:/PUN: integration yet; these are explicit future stages.
- The existing console and production main remain stable and untouched.
