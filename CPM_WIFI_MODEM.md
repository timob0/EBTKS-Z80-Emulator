# CPM Wi-Fi modem -- development milestone (2026-10-21)

The ESP32 now contains a **separate, polling 8250-compatible virtual modem**
at Z80 I/O ports **88h..8Fh**, alongside the existing CP/M console at
80h/85h. This is a new **development branch**, not part of production main:
`feature/cpm-wifi-modem`.

## Build validation (source commit `38bb93ea`)

GitHub Actions run [39518316821](https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39518316821)
has **passed** native Z80/8250/Hayes tests, MODEMTERM.COM generator check,
both headless and ST7796 builds, and headless SPI-off regression.

| Build | Flash | OTA limit | Static RAM |
| --- | ---: | ---: | ---: |
| ESP32 headless | 1,129,165 B | 1,310,720 B | 123,280 B |
| ESP32 ST7796 TFT | 1,191,033 B | 1,310,720 B | 124,504 B |

The TFT image has **119,687 bytes available** in the OTA slot.
Static DRAM was very close to its linker limit: the modem's UART, Hayes,
socket and network FIFOs were dynamically allocated as a result.
**Actual runtime heap margin and live BBS use still require HP-85 hardware
acceptance**. Save the previously verified firmware for rollback.

## Architecture

- The Z80 port I/O dispatch calls **only** `CpmWifiModem::readPort/writePort`,
  a short FreeRTOS critical section around heap-free UART register logic;
  DNS, sockets, TCP and any blocking network work **never run in CPU code**.
- One low-priority core-0 worker owns a single `WiFiClient`, the Hayes
  parser, protocol state and network send queues.
- Modem receive and transmit FIFO buffers (512 each), plus a 512-byte TCP
  transmit queue and 256-byte response queue, are allocated on the ESP32
  runtime heap **at startup**. Allocation failure leaves the device inactive
  and records a message in `/z80debug`. They are not statically allocated
  in the scarce linker `.dram0.bss` section; the WiFiClient and
  Hayes parser instances are likewise allocated on the runtime heap.
- Existing HP-85/Teensy keyboard capture, CP/M boot/media, ESP32 OTA,
  WebSocket/Telnet server, and TFT routing are unchanged.

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
Do **not** type them at the plain CP/M CCP prompt; use the MODEMTERM utility
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
   `python3 tools/build_modemterm.py -o MODEMTERM.COM`.
   This produces the same small Z80 program documented in
   `tools/modemterm.asm`, without requiring an assembler.
4. Place `MODEMTERM.COM` on a writable CP/M disk image using the
   established SD-card/image transfer workflow. Start CP/M and type
   `MODEMTERM` at its `A>` or other CCP prompt. Physical HP keyboard
   requires the user-tested `CPM` HP command to be attached; the
   existing TFT/Web/Telnet terminals remain alternatives.
5. Type `AT` + Enter: expect OK. Then `ATI`; then try
   `ATDTvert.synchro.net:23` and look for a BBS greeting.
   These are **test instructions**, not claimed live availability.
6. Leave the BBS with its logout command, or let 1s elapse,
   type `+++`, leave another 1s, type `ATH` and Enter.
   **Ctrl+X quits MODEMTERM** back to CP/M. **HP K8** returns keyboard
   control to HP BASIC without stopping the CP/M VM.

If MODEMTERM is not on the disk, the Z80 modem can also be driven by
a separately prepared MEX/IMP machine overlay once port compatibility
is verified.

## Status / boundaries

- Native 8250 register semantics, Hayes command parsing, and deterministic
  MODEMTERM image generation are regression-tested on the CI runner.
- ESP32 runtime heap allocation, UART I/O integration and firmware build are
  covered by headless and ST7796 CI builds.
- Actual outbound TCP/BBS connectivity, high-volume file transfers,
  per-server ANSI negotiation and physical device stress tests are **pending**.
- No CPU interrupt behavior, TLS, dialup speed emulation, XMODEM flow control
  or CP/M BIOS RDR:/PUN: integration yet; these are explicit future stages.
- The existing console and production main remain stable and untouched.
