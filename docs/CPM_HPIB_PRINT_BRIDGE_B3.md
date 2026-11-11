## 2026-11-11 — First physical CPLOOP regression and next diagnostic

User built/loaded CPM85 v0.07a on real HP-85:
- `CPLOOP` **does not forward ordinary keyboard entries to CP/M**.
- K8 **does** cleanly exit back to HP BASIC from foreground loop.
- Unchanged original `CPM` command still forwards keys normally.

This establishes foreground exit/key polling is possible but NOT that
ordinary key event delivery works. Source inspection: the proven `CPM`
CHIDLE hook reads `KEYHIT` at 0100671 octal, whereas the foreground
`CPLOOP` reads physical depressed-key register `KEYCOD` at 0177403
octal. The ESP32 `Cpm85Keymap` expects `KEYHIT` codes. It is an
**unverified hypothesis** that the raw hardware `KEYCOD` is distinct
from the BASIC-decoded `KEYHIT` for ordinary keys; the next test
must measure raw observed values rather than assume the mapping.

The HP85 repo now contains CPM85 **v0.07b** diagnostic draft:
- Leaves proven `CPM` unchanged;
- `CPLOOP` zeroes `KCOUNT`/`KLAST` upon start, then records
  non-K8 physical key-down edges and last `KEYCOD` value into
  those existing fields. After K8 exit, `PRINT CPCOUNT` and
  `PRINT CPLAST` report observation results (CPCOUNT wraps at 256).
- Still forwards observed raw codes, *without changing their mapping*,
  so the result can distinguish no events from wrong codes.
- Retains bounded automatic exit (iteration counter), not a guaranteed
  timing limit.
- Updates CPM85 user pocket guide and command/ABI CI.

**Next user test**: build/load feature branch 0.07b ROM only;
`CPVER` must report 0.07b; run `CPLOOP`, press individually
A/B/C then K8, `PRINT CPCOUNT` and `PRINT CPLAST`. Optionally run
another iteration pressing just A then K8 to learn raw A code; compare
the same keys in the proven `CPM` route. Do not infer from these
counters that Teensy received or ESP32 translated the key; they
measure HP foreground detections, not end-to-end transport.

No Teensy or ESP32 firmware edits; no printer bridge implemented in
this diagnostic revision. Do not merge or replace proven `CPM`.

---

## 2026-11-11 — B3a foreground keyboard experiment implementation (ROM only)

Implemented `CPLOOP` as an **EXPERIMENTAL** non-printer diagnostic command
in `timob0/HP85-Option-Roms` on this same feature branch.
CPM85 `CPVER` now reports **0.07a LOOP PROBE**. The new command:
- runs in ordinary HP BASIC foreground (not from CHIDLE);
- verifies no other hook owns CHIDLE, leaves hook bytes untouched;
- marks Teensy keyboard input active on existing 0342 HP port;
- polls physical HP key-down status (177402) and raw keycode (177403);
- forwards one key event per press through the already-proven 0341 port;
- consumes K8 locally and sets keyboard attach=0, then returns to BASIC;
- uses a bounded delay and an *approximate* safety watchdog to return
  automatically after 16384 poll cycles, even if K8 detection fails;
- does **not** yet implement HP-IB output, printer queue, online/offline
  status, K7, CPPRT or modification of the existing stable `CPM` command.

Assembly, CAPLIF packaging, static keyword ABI and the pocket guide tests
are run in the Option ROM GitHub Actions on the feature branch; hardware
test of the foreground polling behavior is still REQUIRED before proceeding.

**ROM-only hardware test:** pull only the HP85-Option-Roms feature branch,
assemble/load 0.07a CP/M binary, verify `CPVER`, run `CPLOOP` with
work saved and CP/M already available, type some keys at the CP/M prompt,
press K8 and verify HP BASIC returns, then run the proven `CPM` mode
to make sure no regressions. Auto-exit is a safety net, not a
guaranteed wall-clock timeout. Keep the prior 0.06d LOADBIN around as
a rollback.

**No ESP32 or Teensy changes or flashes are required** for B3a at this
checkpoint. Do not merge until user acceptance. Subsequent stage adds
virtual printer FIFO/status and host HP-IB printer output to the
validated foreground loop.

---

## 2026-11-11 — Virtual printer readiness and operator control (confirmed direction)

**Requirements from the user:** CP/M virtual printer starts **OFFLINE**;
`CPPRT 701` configures its destination but must not enable it.
Entering the integrated `CPM` console session with a configured destination
enables the virtual printer automatically. Leaving with K8, or an
unexpected HP-85 disconnect/reset, takes the virtual printer **OFFLINE**.
Provide a local HP-85 function key to toggle ONLINE/OFFLINE without leaving
the interactive CP/M console (proposed **K7**, pending hardware keycode and
key-label conflict checks). A temporary offline switch must be useful for
paper changes or runaway jobs.

CP/M BIOS `listst` currently always returns 0xFF, so replace it with real
virtual-printer status when redirect mode is selected: `0xFF` only if printer
session ONLINE, no transport fault and sufficient queue capacity; return
`0x00` when OFFLINE, faulted or queue full. When redirect mode is
*disabled/unconfigured*, choose explicit compatibility behavior, not
accidental duplication to the UART.

CP/M BIOS `list` has no return code: if OFFLINE or full, it **must not
accept-and-discard** a byte. A synchronous direct `LIST` caller may wait
for readiness (using cooperative Z80 I/O status polling), even if it does
not test `LISTST`. The separate physical HP-85 loop must remain responsive
to K7/K8 while guest CP/M is waiting; toggling ONLINE must let it proceed.
Do not turn a slow/blocked printer into a tight uncontrolled CPU spin.

Recommended state model:
- **UNCONFIGURED/OFFLINE**: after CP/M boot, no valid `CPPRT` target;
- **CONFIGURED/OFFLINE**: `CPPRT 701` completed, but `CPM` not attached;
- **ONLINE**: integrated `CPM` session active and printer toggled on;
- **PAUSED/OFFLINE**: local function key toggled offline during `CPM`;
- **FAULT/OFFLINE**: HP-IB failure, lost host/heartbeat, or reset.
A separately sampled queue-capacity condition temporarily makes
`LISTST` not-ready without changing the user-visible ONLINE state.
Hardware HP-IB listener readiness may not be directly detectable;
`ONLINE` means the bridge can accept data, not guaranteed paper/ink state.

**When K7 switches OFFLINE:** immediately stop sending new HP-IB data and
advertise not-ready to CP/M; retain accepted bytes in FIFO (and remember
which bytes were acknowledged) so they can be resumed by toggling ONLINE.
No discard, replay or auto-formfeed. K8 disconnect sets OFFLINE; preserve
an explicit policy for pending buffered data across sessions before the
first hardware test (either retain and resume with clear accounting or
explicitly report undrained data). If a runaway CP/M job must be cancelled,
a separate deliberate flush/cancel UI can be considered later; toggling
offline is **pause**, not cancel.

Safety: ONLINE/OFFLINE control messages must be out-of-band, not mixed into
the raw printer byte stream. Session state and host-presence watchdogs
should default to OFFLINE on missed heartbeat, RESET and transport restart.
Verify timing of offlining during a partly sent HP-IB chunk; bounded chunks
cannot be preempted byte-for-byte, so choose a small maximum chunk and
clearly limit pause latency. Do not insert slow HP-IB I/O in `CHIDLE`;
the integrated `CPM` foreground loop must be validated separately.

Hardware regression targets:
1. Boot to A> with no CPPRT/CPM: LISTST not-ready, no printer output.
2. CPPRT 701 alone: still not-ready.
3. CPM starts: ready, raw binary LIST prints; keyboard remains responsive.
4. K7 offline: LISTST goes not-ready, pending FIFO remains intact, no
   additional printer bytes beyond bounded already-in-flight chunk.
5. K7 online: printing resumes in original order, no missing/duplicate bytes.
6. K8 exit: virtual printer not-ready, HP BASIC responsive, CP/M continues.
7. Unexpected HP reset/link loss: fails closed to not-ready with no stale
   online state.
8. Offline LIST call that skips LISTST blocks/retries safely and unblocks on
   the online transition.
9. Test printer-disconnected/HP-IB errors, queue-full flow control and
   repeated toggles while printing.

The key assignment **K7 is proposed**, not yet confirmed by HP keycode
instrumentation or user testing; K8 remains reserved for console exit.

---

# B3 — CP/M LST: → HP-85 HP-IB printer bridge (design and acceptance plan)

Status: **design-only proposal, not implemented**, 2026-11-11.
Baseline: B2d merged to main; CPM85 v0.06d, matching ESP32/Teensy firmware.
Related repositories: `timob0/EBTKS-Z80-Emulator`, `timob0/ebtks-alt-firmware`, `timob0/HP85-Option-Roms`.
Working branch in each: `feature/cpm-hpib-print-bridge`.

## 2026-11-11 design correction — combine console and printer in CPM

**Confirmed user requirement:** printer forwarding must work concurrently with
HP keyboard interaction in the **same `CPM` session**. A standalone blocking
`CPPRUN` would force the user to exit CP/M to print and thus defeats
interactive programs that print while awaiting further input. **Drop CPPRUN
from the intended interface.**

Desired user interface:

```basic
CPPRT 701
CPM
```

While the `CPM` connection is active, K8 still detaches and returns to BASIC,
and CP/M itself keeps running. CP/M BIOS `LST:` output flows without byte
transformation to the configured HP-IB device as it arrives, *while the HP-85
keyboard continues to control CP/M*. `CPPRT` is only a setup command;
unconfigured printer behavior must be explicit (prefer unchanged legacy
LST route rather than silent loss). Printer setup is retained for the next
`CPM` session unless explicitly changed. No extra action is required to
enable printing during `CPM`.

**Architectural blocker / required research before implementation:** The
hardware-verified CPM85 v0.06d does NOT occupy BASIC foreground after
`CPM` executes. It installs a `CHIDLE` keyboard hook then returns to BASIC's
editor. The `KHOOK` routine uses `EOJ2` and the special R6 frame unwind,
and intentionally sends only a single byte over a Teensy ISR-safe HP I/O
port; it explicitly says *do not add I/O/display/SPI operations here*.
Long HP-IB `HPOUT` calls or waits inside `CHIDLE` risk freezes, stack
corruption, or unsafe returns. **Do not add printer draining in KHOOK.**

Investigate these two alternatives and select based on a real-HP85 probe:

1. **Preferred if experimentally safe: foreground cooperative `CPM` session
   loop** using direct HP key status/code polling, forwarding keys to the
   existing 0341 port and draining queued printer data in bounded HP-IB
   chunks, all in *normal BASIC statement runtime* rather than `CHIDLE`.
   K8 returns cleanly. Prior experiments with an HP CRT foreground counter
   encountered freezes; use a minimal instrumentation-only prototype
   before replacing the proven `CHIDLE` path. The keyboard scanner, STOP/
   RESET, hook ownership and CPU responsiveness all require acceptance.
2. **If a safe supported periodic BASIC/HP-IB service callback exists:**
   leave `CPM` as a keyboard hook and schedule printer draining exclusively
   in a documented normal-context service routine, not from an interrupt/
   keyboard hook. Prove it runs while awaiting keys and will not race
   PRINTER85 or BASIC's HP-IB state. Merely speculating about RMIDLE/
   CHIDLE is insufficient.

Whichever approach passes, **no blocking printer service may prevent
ongoing keyboard forwarding**. Keep the existing `CPM` key semantics,
especially K8 detach, and preserve the old console path when printer
redirection is disabled/unconfigured. A slow printer may legitimately
backpressure `LST:` but must not discard queued data; a CP/M program
blocked in a synchronous `LIST` write may itself stop responding to input
until the physical printer drains—that behavior is not caused by an HP-85
keyboard disconnect and must be documented.

## User intent and scope

Allow native CP/M software using its standard `LST:` device to print via the physical
HP-85's existing HP-IB interface to a configured listener such as device `701`
(select 7, address 01). Preserve bytes **exactly**: ESC, CR, LF, FF, NUL, high-bit
values and any binary printer control streams. No ANSI stripping, CP/M text
conversion, line wrapping or automatic formfeed. Reuse the HP-85's proven
HP-IB host/controller output path, not an ESP32-to-printer external bridge.

Initial BASIC-facing commands (names subject to parser/ROM ABI verification):

- `CPPRT 701`: validate/store HP-85 HP-IB destination (select 7, device 01).
  Configuration only; does not print or connect keyboard by itself.
- `CPM`: **existing command enhanced** to forward keys and drain the LST
  printer FIFO concurrently. K8 detaches from both services and returns to
  HP BASIC. CP/M continues running.
- An optional explicit diagnostic/status command may be added later if
  essential (FIFO depth, bytes sent, HP-IB failure).
- Potential `CPPRT 0` disable semantics require an HP BASIC parser check.

`CPPRUN` is explicitly **out of scope and must not be implemented**. The
whole point is one interactive console session with transparent printing.

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
      -> HP-85 CPM session service polling FIFO/status through HP I/O ports
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
4. HP-85 `CPM` session service may fetch chunks and send them using `HPOUT` with
   a byte count; it must fully acknowledge only bytes actually sent.
   The HP-85 is the **only HP-IB controller**. Do not print from Teensy
   bus emulation or from ESP32.
5. HP-IB bus opening and UNLISTEN closing are distinct from raw payload
   bytes. Prefer a sustained addressed-listener session or explicit bounded
   idle close as supported by the proven HP driver, not per-character
   addressing. Neither operation inserts bytes into the data payload.
6. K8 detaches console and ends host printer service. Define graceful shutdown: drain already accepted pending bytes
   before returning, or report an explicit "not drained" status and
   preserve them for recovery. Never silently discard pending payload.
7. Error/timeout and reset behavior is explicit: no indefinite HP BASIC
   hang; no false success or data loss on a full queue, link drop,
   HP RESET, printer offline or detached host.
8. `CPM` console capture, PRINTER85's `PRTKEYS`, and the printer-service
   execution model must not claim the same hook concurrently. Foreground
   `CPM` may use direct key polling only after hardware-safe validation.
9. Other HP BASIC graphics/printing facilities are unaffected when
   `CPM` is not connected and the printer service is inactive.

## Recommended incremental acceptance sequence

### B3a — raw path, no physical printer

- Add guest LIST emulation and a bounded ring, route only LIST bytes.
- Native tests: ASCII, ESC sequences, high-bit bytes and 0x00, queue
  capacity/backpressure, high throughput, no duplicate frames,
  LISTST ready semantics and unchanged console UART behavior.
- Add transport to Teensy RAM ring with observable counters; host-side
  HP BASIC test routine drains bytes to a diagnostic capture buffer
  or a deliberately instrumented endpoint rather than HP-IB.

### B3b — Integrated HP-85 console/print service

- First prove safe concurrent HP keyboard polling and HP-IB sends in a
  minimal real-machine prototype, without modifying stable `CPM`.
- Add `CPPRT` target parser/profile and enhance `CPM` itself to
  forward keys while servicing printer queue.
- Read raw byte chunks on the physical HP-85 and send via `HPOUT` to
  `701` (or configured address), with HP keyboard interaction remaining
  available; K8 exits the entire console/print session.
- Verify byte-for-byte stream with a trusted HP-IB listener/capture
  before testing printer-specific jobs.

### B3c — CP/M applications and failure handling

- CP/M `PIP LST:=A:README.TXT` (or equivalent syntax already known
  to work on this CP/M system), printer output via CP/M application,
  ESC/CR/LF/FF preserved, zero-width payload frames accepted.
- Confirm queue backpressure on slow/unready HP-IB listener;
  no missing/reordered/doubled bytes.
- Interaction during printing (prompt responses and user keystrokes),
  K8/RESET recovery, ESP32/Teensy restart, printer disconnected,
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
- Which normal-context execution strategy can safely preserve HP keyboard
  capture AND native HP-IB output in one `CPM` session on real hardware.
- Whether frame transport can reuse an existing binary framing layer
  instead of introducing a new ESP/Teensy protocol.
- Session-termination and explicit error returns on printer offline.

**Implementation is not started merely by this document.** No changes
to stable firmware or ROM files have been made for B3 yet.
