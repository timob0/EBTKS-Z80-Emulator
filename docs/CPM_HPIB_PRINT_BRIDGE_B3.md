# B3 — CP/M LST: → HP-85 HP-IB printer bridge (design and acceptance plan)

Status: **design-only proposal, not implemented**, 2026-11-11.
Baseline: B2d merged to main; CPM85 v0.06d, matching ESP32/Teensy firmware.
Related repositories: `timob0/EBTKS-Z80-Emulator`, `timob0/ebtks-alt-firmware`, `timob0/HP85-Option-Roms`.
Working branch in each: `feature/cpm-hpib-print-bridge`.

## User intent and scope

Allow native CP/M software using its standard `LST:` device to print via the physical
HP-85's existing HP-IB interface to a configured listener such as device `701`
(select 7, address 01). Preserve bytes **exactly**: ESC, CR, LF, FF, NUL, high-bit
values and any binary printer control streams. No ANSI stripping, CP/M text
conversion, line wrapping or automatic formfeed. Reuse the HP-85's proven
HP-IB host/controller output path, not an ESP32-to-printer external bridge.

Initial BASIC-facing proposed commands (names subject to source/lexical audit):

- `CPPRT 701`: validate/store HP-85 output-device select code. Configuration
  only; does not turn on redirection or print by itself.
- `CPPRUN`: run the blocking HP-85 relay, forwarding exact queued CP/M
  `LST:` bytes to the configured HP-IB device. K8 ends the relay cleanly,
  restoring HP BASIC. This does not stop the Z80 or cancel CP/M.
- Optional follow-up `CPPRSTAT`: counters/status (queue depth, bytes sent,
  timeouts, last HP-IB error), only if needed after first implementation.
- Optional `CPPRT 0` disables a configured target only if valid per HP-85
  parser conventions. Do not finalize before testing.

A blocking BASIC command is the **simplest safe initial model**: HP's CPU
executes real HP-IB output and cannot simultaneously run other BASIC
statements. CP/M itself continues on the ESP32 independently. To drive CP/M
while `CPPRUN` occupies the HP-85 console, use the ESP32 TFT, web terminal
or Telnet console. A later combined `CPM`+printer relay (HP keyboard to CP/M,
printer to HP-IB) is possible but should be deliberately designed separately:
the existing `CPM` keyboard hook requires the HP BASIC editor to be idle.
Never promise keyboard focus and BASIC command blocking coexist without
hardware evidence.

## Source review — important existing capabilities

- `CPM Source/cbios.asm` has an actual CP/M BIOS `list` entry (BIOS jump
  table function 5) and `listst` (function 15). Currently `list`
  examines bits 6..7 of `(iobyte)` and sends the character through the
  UART console output function; `listst` unconditionally returns 0xFF.
  Route `LST:` through a new emulated device *only when redirector armed*;
  leave legacy UART route intact when it is not.
- `src/printer85_rom.asm` demonstrates the proven HP-85 driverless HP-IB
  transport path: `HPIB7`, `HPCTRL`, `HPADRO`, `HPOUT`, and `HPSNDC`.
  The printer's model-specific ThinkJet/SII formatting routines should
  **not** be used by this raw redirector. Take inspiration from open/write/
  UNLISTEN close semantics, but do not share PRINTER85 mutable profile.
- B2d's Teensy HP-facing mailbox and ESP UART transport are already proven.
  Add an **independent raw-byte ring**, not a variation of the CPIMPORT/
  CPEXPORT transaction mailbox. New HP I/O ports must not overlap keyboard
  (`0341`/`0342`) or drive control (`0343`–`0346`).
- The project's ESP32 internal DRAM is constrained; use small bounded
  ring sizes with explicit backpressure. Avoid unbounded `std::vector`,
  per-byte JSON command allocations or blocking network work inside
  the Z80 OUT/IN callbacks.

## Proposed byte path

```text
CP/M program -> BDOS LIST / BIOS list(C)
      -> dedicated virtual Z80 printer DATA/STATUS ports
      -> bounded ESP32 raw TX queue (zero text conversion)
      -> batched ESP32-to-Teensy framed binary payload + credits/ACK
      -> bounded Teensy printer FIFO
      -> HP-85 CPPRUN polling FIFO/status through HP I/O ports
      -> HP-85 native HPIB7 / HPCTRL / HPADRO / HPOUT
      -> actual HP-IB printer at CPPRT's selected address
```

Two separate groups of ports are needed and must be inventoried before
assignment: guest Z80 I/O ports for LIST/ready, and host HP-85/Teensy
peripheral ports for FIFO read/status/control.

## Flow control and race invariants

1. A byte is accepted by the Z80 printer-port callback **only when free
   queue space is available**. BIOS `list` must poll ready/capacity and
   wait cooperatively if full. Never silently drop or overwrite a byte.
2. `listst` returns ready only when a printing byte can be accepted.
   If redirector is not enabled, preserve the previous `LST:` behavior;
   if enabled and queue temporarily full, report not ready.
3. A batched transport frame has sequence number, length and byte payload;
   ACKs grant credits only after Teensy has reserved/accepted the bytes.
   Explicit length makes binary 0x00 legal. Lost/duplicate frame must
   never print twice. Retry behavior and sequence wrap must be tested.
4. HP-85 `CPPRUN` may fetch chunks and send them using `HPOUT` with
   a byte count; it must fully acknowledge only bytes actually sent.
   The HP-85 is the **only HP-IB controller**. Do not print from Teensy
   bus emulation or from ESP32.
5. HP-IB bus opening and UNLISTEN closing are distinct from raw payload
   bytes. Prefer a sustained addressed-listener session or explicit bounded
   idle close as supported by the proven HP driver, not per-character
   addressing. Neither operation inserts bytes into the data payload.
6. K8 requests graceful shutdown: drain already accepted pending bytes
   before returning, or report an explicit "not drained" status and
   preserve them for recovery. Never silently discard pending payload.
7. Error/timeout and reset behavior is explicit: no indefinite HP BASIC
   hang; no false success or data loss on a full queue, link drop,
   HP RESET, printer offline or detached host.
8. Normal `CPM` capture, PRINTER85's `PRTKEYS` and the relay must
   not claim the same keyboard hook concurrently. First implementation
   may use direct key polling inside the blocking `CPPRUN` call.
9. Other HP BASIC graphics/printing facilities are unaffected while
   `CPPRUN` is not running.

## Recommended incremental acceptance sequence

### B3a — raw path, no physical printer

- Add guest LIST emulation and a bounded ring, route only LIST bytes.
- Native tests: ASCII, ESC sequences, high-bit bytes and 0x00, queue
  capacity/backpressure, high throughput, no duplicate frames,
  LISTST ready semantics and unchanged console UART behavior.
- Add transport to Teensy RAM ring with observable counters; host-side
  HP BASIC test routine drains bytes to a diagnostic capture buffer
  or a deliberately instrumented endpoint rather than HP-IB.

### B3b — HP-85 controller and BASIC service

- Add `CPPRT` target device parser/profile and `CPPRUN` service.
- Read raw byte chunks on the physical HP-85 and send via `HPOUT` to
  `701` (or the user's actual printer address); K8 exits.
- Verify byte-for-byte stream with a trusted HP-IB listener/capture
  before testing printer-specific jobs.

### B3c — CP/M applications and failure handling

- CP/M `PIP LST:=A:README.TXT` (or equivalent syntax already known
  to work on this CP/M system), printer output via CP/M application,
  ESC/CR/LF/FF preserved, zero-width payload frames accepted.
- Confirm queue backpressure on slow/unready HP-IB listener;
  no missing/reordered/doubled bytes.
- K8/RESET recovery, ESP32/Teensy restart, printer disconnected,
  zero bytes printed when redirector is disabled, etc.
- Update CPM85 Pocket Guide, matching source command list CI, full
  handoff docs and compile/firmware workflow tests as part of each stage.

## Licensing caveat

The checked-in `CPM Source/cbios.asm` begins with a Grant Searle
non-commercial-use notice. The overall project/manual license does NOT
automatically relicense this third-party BIOS source. Preserve its
attribution and license requirements; review provenance before
redistribution or any license-header rewrite.

## Not decided yet

- Final names of guest Z80 and host HP ports (must audit current maps).
- Whether `CPPRUN` also forwards HP keyboard to the CP/M console,
  or stays printer-only and requires TFT/web/telnet input in phase one.
- Whether frame transport can reuse an existing binary framing layer
  instead of introducing a new ESP/Teensy protocol.
- Session-termination and explicit error returns on printer offline.

**Implementation is not started merely by this document.** No changes
to stable firmware or ROM files have been made for B3 yet.
