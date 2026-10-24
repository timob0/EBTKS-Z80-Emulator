# Stage C proposal — CP/M LST: to HP-85-configured printer

**Status 2026-10-24:** **Stage C0 physical HP-85 test PASSED.** The user
confirmed the standalone `tests/CPMCONSOLE.bas` foreground management
console prototype works. It uses AUXROM keyboard buffering and HP CRT
`DISP`, with K1-K3 management pages, K4 simulated Control/UI toggle,
and K8 exit/cleanup. No actual printer data or CP/M forwarding is
implemented by C0. All three `feature/cpm85-hpib-output` branches were
created from the accepted `main` baselines. ESP32/Teensy code remains
unchanged. Do not merge to `main` until complete C-stage hardware tests.

## **CRITICAL Stage C2 crash — C2 NOT ACCEPTED; recovery done (2026-10-24)**

User reports **physical HP-85 crash when invoking `CPKTEST`**
from experimental CPM85 v0.05b. Do **not** recommend rerunning that
binary. This overrides the earlier C2 CI/static-test success.
C0 and C1 are still the last accepted physical console milestones.

Source review found **`JSB =K2KEY` used for a relocatable local
LOADBIN function**. Proven local call convention is
`JSB X14,K2KEY` with the BINTAB relocation base. This is a concrete
possible crash mechanism when the first `KEYHIT` value changed.
Separately, a `KEYHIT` busy spin without a verified HP firmware
event-pumping contract is unsafe and could starve native services;
the CPU-counter timeout was not an independent wall-clock watchdog.
Actual physical root cause remains unconfirmed.

**Containment completed on HP ROM `feature/cpm85-hpib-output`:**
version **CPM85 v0.05c C2 DISABLED** removed native C2 poll/dispatch
routines and turned `CPKTEST` into a notice-and-return command:
`CPKTEST DISABLED - SEE C2 DOC`. The keyword table retains
`CPKCOUNT` and `CPKLAST` as zero-valued compatibility diagnostics.
C1 `CPUI` and original proven `CPM` source path are preserved;
Teensy and ESP32 code not changed. CAPASM/CAPLIF and source regression
on the quarantined code **passed** at
`c3f070931136133ad7f78697b3d38e37b052b87c`,
run `40136810621`. The subsequent document-only build is redundant.
No `main` branches modified. v0.05c hardware validation NOT claimed.

**Safe tested rollback:** HP ROM GitHub commit
`13655f2d8bd0889b91aa4d0a47c8936bcb429d0c` holds the
physically accepted C1 source (v0.05a). Rebuild/LOADBIN that revision
or use the last successfully loaded C1 image after power-cycle/reset.
See `HP85-Option-Roms/docs/CPM85_CONSOLE_C2.md` for safety guidance.

**C2 next design:** discover/verify a documented native HP keyboard
event or cooperative interpreter entry, or keep the proven C1 BASIC
foreground key pump. Do NOT start another `KEYHIT` native tight
loop on physical hardware without a hardware-safe interrupt-aware
design and early exits. No printer ready, CP/M LIST or HPIB output
work has started.

## Stage C2 historical v0.05b prototype: retired native keyboard hardware probe (2026-10-24)

The next HP ROM experiment is committed on
`timob0/HP85-Option-Roms`, branch `feature/cpm85-hpib-output`
as **CPM85 v0.05b**, with no ESP32 or Teensy source changes.
The isolated new `CPKTEST` command executes a fully **native HP
foreground loop**, reading the HP keyboard's `KEYHIT` memory location
(octal `100671`), and shows native Drives/Devices/System pages and
simulated Management/Control mode on changes to K1-K4. A K8 event exits;
an inactivity timeout forces return to HP BASIC if no input occurs.
`CPKCOUNT` and `CPKLAST` numeric diagnostics reveal how many
different key values were seen and which code was last recorded.
This does **not** interfere with the accepted `CPM` keyboard hook or
C1 `CPUI`; no printer ready, HPIB, disk or transport changes.

**Critical unverified assumption:** KEYHIT is proven in the existing
CHIDLE handler but may NOT update while a foreground LOADBIN command
runs. Identical consecutive keys cannot be reliably recognized by a
change-only scheme. Therefore this is a **hardware feasibility test,
NOT a claim that C2 native keyboard input works**. Its timeout avoids
an indefinitely stuck HP BASIC session when no events arrive.
Please test per `HP85-Option-Roms/docs/CPM85_CONSOLE_C2.md`.
If key detection fails, research a documented native keyboard queue
entry point or consider a small HP/Teensy event mailbox; do not wire
live CP/M keyboard/printer traffic into this raw sample scheme.

The HP feature-branch CAPASM/CAPLIF CI and static guards passed
at `af4691288f4ef2c9cc52f70efe4c6d5baad201a9`,
run `40135165957`. The later documentation-only build is
pending. Previous C0 and C1 stages passed physical HP-85 testing;
C2 physical acceptance is pending. All repository `main` baselines
remain untouched.

## Stage C1 hardware accepted (2026-10-24)

User confirmed physical HP-85 validation of C1: **"Tested and works."**
The independent `CPUI` native CRT renderer and native management/
simulated-Control mode/page state work with the foreground BASIC
`KBDBUFFER/KBDISKEY/KBDKEY` keycode pump
(`HP85-Option-Roms/tests/CPMCONSOLE_C1.bas`).
The prior HP feature-head build passed CI: `d2a02e9a6ebe78b0e67fb6b3c5c0a681b7caf201`,
run `40133163983`.

**Important distinction:** C1 is accepted only as the **hybrid native
UI + BASIC foreground input loop**. Neither all-native input, CP/M
keyboard forwarding from `CPUI`, nor HPIB/`LST:` printer reception is
implemented/tested in C1. Continue on `feature/cpm85-hpib-output`;
all three repository main branches stay unchanged. **Next: C2 native
foreground input integration**, then mode/session-ready signaling
before real printer byte routing.

## Stage C1 implemented — native CPUI renderer and mode state (2026-10-24)

HP ROM repo `feature/cpm85-hpib-output` now has an isolated
`CPM85 v0.05a` **experimental LOADBIN** extension,
`src/cpm85.asm` keyword `CPUI <numeric-keycode>`.
`CPUI 0` initializes the console; K1-K3 (128-130) navigate
Drives/Devices/System, K4 (131) changes Management/Control **simulation**,
and K8 (135) terminates its native UI session. Every page/heading is
rendered by native HP firmware `OUTSTR`, never BASIC `PRINT`,
`PRINTER IS` or new HP-IB controller code. No printer data nor
keyboard forwarding is activated; no peripheral ready advertised.

Native keyboard polling is *still unverified* for a foreground LOADBIN
statement. The C1 key-event pump `tests/CPMCONSOLE_C1.bas` uses the
previously verified AUXROM `KBDBUFFER/KBDISKEY/KBDKEY` service and
passes keycodes to native `CPUI`. This is a deliberate **hybrid
intermediate implementation**: it proves native menu state/output but
does not replace the polling loop entirely in machine code. Full
native foreground loop is the remaining C2/input-service feasibility
gate, not something proven by C1.

HP repo docs `docs/CPM85_CONSOLE_C1.md` include local assembly and
HP-85 hardware test procedure. Native parser follows tested PRINTER85
`NUMVP/ONER/CONINT` ABI. Original `CPM`, K8, `CPDCAT`,
Teensy and ESP32 remain unchanged. Latest HP feature CI run
`40133075200` completed **success** including CAPASM 1.0.1,
CAPLIF packaging and static tests. **Physical C1 acceptance pending**.
Old C0 physical acceptance remains passed.

## Stage C0 implemented: HP BASIC foreground-loop hardware probe (2026-10-24)

The **first testable console skeleton** is now on the HP Option ROM
repository's `feature/cpm85-hpib-output` branch:

- Source: `tests/CPMCONSOLE.bas`
- Hardware guide: `docs/CPM85_CONSOLE_SKELETON.md`
- Static regression: `tests/check_cpm_console_skeleton.py`
- HP branch GitHub Actions builds old CPM85 LOADBIN unchanged and runs
  the new regression.

This is a **standalone RUN-ning HP BASIC prototype**, intentionally
using the *previously physically tested* EBTKS AUXROM keyboard-buffer
functions `KBDBUFFER`, `KBDISKEY` and `KBDKEY` as a proof of the
foreground event loop. K1/K2/K3 choose placeholder pages, K4 toggles
UI vs **simulated** CP/M Control, K8 exits and disables the buffer.
It uses **DISP exclusively**, since on HP-85 BASIC `PRINT` follows
`PRINTER IS` and would otherwise send menu text to the selected
printer. Leave `CRT IS 1` for the test.

**No CP/M key forwarding, no actual LST or printer output, no EBTKS
I/O writes and no change to the working CPM85 command**. The user
needs to verify real keyboard polling, mode toggle, and K8 exit on
hardware; then we can implement a native foreground LOADBIN console
with a real multiline CRT renderer. Do not treat C0 as the finished
console or advertise ready status. The ESP32 and Teensy feature branches
are unchanged apart from this design document; all mains untouched.

## Updated end-state architecture: foreground CPM85 management console

**Design direction (2026-10-24):** the user wants this to become the
full integrated system's local management console, not a printer-only
background patch. There must be a **main processing loop** to receive
events, update the HP-85 CRT/menu, manage disk/printer/device settings
and operate CP/M keyboard forwarding and printer reception. An explicit
mode switch pauses **both** forwarding and printer reception while
interacting with the management UI.

This design supersedes the earlier preference for a resident background
printer hook below. It is a proposal, not yet implemented/verified on HP
hardware. Preserve the old working CPM85 command until a replacement is
physically tested.

### Modes and visible behavior

| Mode | HP keyboard | CP/M output to LST: | HP display |
| --- | --- | --- | --- |
| UI / MANAGEMENT | Owned by HP console, **not forwarded** | **Not ready**; do not receive a new print chunk | Drive/device/media/status pages |
| CONTROL / CP/M | Forwarded to CP/M, except local mode/exit keys | Ready only when the printer can accept data and transport has credit | Compact attached/target/status indication as appropriate; Z80 screen stays on TFT/Web |
| PAUSING / TRANSITION | Local only, no forwarding | Not ready while the existing in-flight chunk is resolved | Enter/leave status |
| ERROR / PRINTER OFFLINE | Local controls available | Not ready; explicit recovery/cancel | Error/status guidance |
| EXIT | Returned to ordinary HP BASIC | Not ready; discard session credits, never replay stale chunks | HP BASIC restored |

A freshly entered `CPM` could open the management menu and switch into
Control on an explicit menu action; to preserve the existing convenient
workflow, allowing `CPM` to enter Control directly is another option.
Exact entry/default selection and which function keys toggle the mode
must be settled with physical UX tests. **Existing K8 exit behavior
must not silently change.** A K4/K8 PREV/NEXT menu convention is
available from PRINTER85, but it conflicts with K8 exit if reused
unconditionally; key assignments are provisional.

### Foreground service loop (HP-85)

Illustrative pseudo-code (not HP assembly syntax):

```
CPM [optional printer device]
    save HP CRT / keyboard / printer context as needed
    choose initial local UI or CP/M Control mode
    session_generation += 1
    while console_active:
        scan_or_receive_keyboard_events()    // source must be hardware-proven
        process_local_ui_keys_or_forward_cp_m_keys()
        if mode == CONTROL:
            inspect_printer_readiness_and_link_health()
            receive_at_most_one_bounded_LST_chunk()
            send_bytes_using_native_HP_printer_service()
            acknowledge_bytes_only_after_safe_acceptance()
        update_UI_or_status_if_changed()
        service_HP_runtime_without_starving_other_firmware()
    set_ready(false)
    close_and_invalidate_session()
    restore_HP_context()
    return_to_HP_BASIC
```

The ongoing CPU activity exists on the **HP-85 side**; the ESP32 Z80
and Teensy are independent and continue executing in both modes.
The existing keyboard hook is *not* a printer service context. We must
prove a suitable native keyboard polling/event API for the foreground
program before replacing the accepted CHIDLE-only flow; CHIDLE may be
tied to the BASIC editor idle path and cannot be assumed to run while
the command is blocking. Input handling must not require extra keypresses
to advance printing. Never invoke blocking printer output from a
keyboard ISR or HP bus ISR.

### Mode transitions and correct ready semantics

- **On UI entry:** withdraw LST readiness *before* stopping receiver
  consumption and before reassigning HP keyboard ownership. Resolve the
  one already granted frame: either finish and acknowledge, or retain
  it unacknowledged within its current session for later retry. Never
  acknowledge a byte merely because it reached the Teensy or HP bus.
- **On Control entry:** verify real printer selection, working HP-side
  output, receiver capacity, fresh transport state and matching session
  generation. Only then advertise readiness. If validation fails,
  keep Control keyboard handling usable but show printer offline.
- **Printer unavailable:** withdraw readiness, preserve undelivered
  buffered bytes, and let operator retry or explicitly cancel. Do not
  block the *whole console UI* indefinitely on a missing printer.
- **K8 / CPOFF / RESET / ROM unload:** revoke readiness, close the
  session, restore HP state, discard stale credits and de-duplicate
  retransmissions. Any intentionally discarded print data must be
  acknowledged as cancelled, not as printed.
- **Guest BIOS LIST vs LISTST:** LISTST returning not ready is useful
  for programs that check it, but an application may call LIST without
  checking. A finite FIFO with bounded backpressure/cancel behavior
  is required for correctness and recoverable UX. Do not silently
  drop bytes or block the HP bus ISR.

### Printer destination and system UI

- `CPM` defaults to whatever printer the HP-85's `PRINTER IS`
  configuration already selects. Printing runs through the native HP
  printer output API, not a duplicated ESP32 IEEE-488 controller.
- `CPM 705` proposes an explicit per-session printer device override;
  validate numeric parser and native device-selection semantics before
  promising this syntax. Save/restore any affected global printer
  selection and use native error status where supported.
- A future management screen will include **Drives**, **Devices**
  (printer/target, readiness and mapping), and **System / terminal**.
  File-transfer controls can be a later page. Only the selected mode
  owns HP keyboard focus and printer drain.
- Avoid taking ownership of PRINTER85's CHIDLE or KEY LABEL resources
  without a conflict/restore policy; use its proven page/save-restore
  patterns if suitable. The ultimate console is a **single owner** of
  local UI keys and session state.

### Implementation sequence adjusted for the end-state console

1. Build a minimal foreground HP console loop with local key handling,
   mode switching, visible UI, clean K8 exit, no printer traffic and
   no changes to ESP32 or Teensy. Verify no loss of current HP BASIC,
   CRT, keyboard, or PRINTER85 behavior.
2. Add explicit HP/Teensy/ESP32 mode and heartbeat/credit status; test
   local UI == LISTST not ready and Control == potentially ready while
   CP/M and TFT remain independent.
3. Add read-only synthetic LST data frames and native HP printer
   output, controlled by the foreground loop; validate byte order,
   CR/LF/FF, error propagation and cancellation.
4. Intercept actual CP/M BIOS LIST/LISTST (without disturbing console
   UART), then use existing HP printer destination, and only afterward
   the optional `CPM 705` override.
5. Extend UI pages to disk/device management; add file-transfer panel
   and BDOS file operations later, after separate safety regression.

**Unverified HP firmware details to investigate:** correct nonblocking
foreground keyboard input; invoking the native printer API while a
LOADBIN BASIC statement is active; printer ready/offline/error reporting;
whether printer destination selection can be overridden and restored
without changing the BASIC user's `PRINTER IS`; KEY LABEL ownership
and refresh while in a foreground assembly routine. These are
implementation gates, not assumptions.

## Agreed user direction

- Reuse HP-85 native `PRINTER IS` / printer output facilities rather than reimplement HP-IB protocol on ESP32 or Teensy.
- Capture CP/M `LST:` stream on the ESP32, carry it over the already established ESP32-to-Teensy poll/mailbox channel, and deliver the bytes to an HP-side receiver associated with the CPM85 LOADBIN `CPM` command.
- Plain `CPM` selects the user's existing `PRINTER IS` target. Optional numeric `CPM 705` would override the target for that CPM attachment (interface 7, device 05), without inadvertently changing unrelated HP BASIC printer use. Exact implementation of the override depends on the HP printer ROM/output API and requires checking.
- If no HP receiver is running, if the printer backend fails, or if output cannot be accepted without byte loss, CP/M `LISTST` must report not ready. Detach/K8/ROM unload/HP reset must revoke ready; reconnection must start a clean session.

## Clarification: CPM stays active for the user, but its entry routine returns

User observes (correctly) that invoking `CPM` keeps the HP-85 in
CP/M keyboard-control mode until K8, with BASIC keystrokes intercepted.
Source in HP Option ROM `src/cpm85.asm` shows that **the CPMRUN entry
routine itself does not spin in a foreground loop**: after installing
`CHIDLE`, setting `KHACT=1` and writing `CPCTL=1`, it executes
`CPMRET: BIN / PAD / RTN`. Thereafter HP BASIC remains in its keyboard
wait/editor machinery, while `KHOOK` intercepts and consumes keys.
`KHOOK` returns via `EOJ2` / `RTN`, and K8 runs `KREL`.

Thus **active control mode != continuously executing CPMRUN**. The user
cannot type into BASIC during capture, which gives the strong appearance
of a modal foreground program and is fully consistent with how it
behaves on physical hardware. We should keep this user experience.

There are two candidate HP-side printer receivers:
- **Cooperative resident receiver** executed in a verified safe BASIC
  idle/service context while the keyboard hook remains active.
- **Foreground CPM loop** redesigned to poll printer data and process it
  while arranging keyboard dispatch/K8 correctly. This is a meaningful
  change to the accepted keyboard integration, so first prove that HP
  keyboard interception and native printer calls remain functional.

Do NOT assume the `CHIDLE` hook runs every timer tick or is a safe
printer-calling context merely because keyboard capture persists. A
printer service must not rely on additional keystrokes to make progress.

## What already exists and the important architectural gap

- The current HP-85 `CPM` statement installs a `CHIDLE` keyboard hook and **returns to BASIC**; it does *not* remain in a foreground BASIC receiver loop. K8 releases the hook through `KREL` and writes Teensy control port `0177742`.
- Current `CHIDLE` hook is a keyboard-event path. It deliberately performs at most one EBTKS I/O write per key. Do **not** print, poll UART, use HP-IB, or block inside that hook or any timing-sensitive Teensy HP-bus ISR.
- HP printing from a resident service requires a separately verified, safe cooperative HP idle/service context (or an explicit foreground receiver design). Before changing CPM85, prove the service executes without keystrokes, can call the native HP configured-printer output function, can handle an unavailable device, and does not break BASIC / PRINTER85 hooks.
- CP/M 2.2 CBIOS in the checked-in `CPM Source/cbios.lst`: `LIST` at `E83B` currently redirects the character through ordinary virtual UART A/B based on IOBYTE; `LISTST` at `E87D` unconditionally returns `A=FF` (ready). We must distinguish `LST:` from normal CONOUT so console/TFT/Web remain untouched. Any guest hook/patch must preserve register and stack semantics and survive guest warm/cold restart correctly.
- The existing Teensy-initiated UART control poll carries keyboard input and the 0343-0346 HP drive-control command mailbox. Printer payload traffic requires a separate bounded, typed channel with explicit framing/sequence/byte length; do not pass raw print data through the existing NUL-terminated text response field.

## Proposed staged implementation and safety contract

1. **Receiver feasibility spike (HP):** Verify a safe HP cooperative entry point and native output to the current `PRINTER IS` target. Check how HP printer functions report offline/busy/errors. Demonstrate 1/16/128 ASCII characters and CR/LF/FF without repeated/newly invented line endings. Confirm internal printer (`PRINTER IS 2`) and an external HP-IB printer (`PRINTER IS 705`, when present). No Z80 changes yet.
2. **CPU-only LST capture (ESP32):** Add a bounded FIFO to the existing Z80 worker/service ownership. Route native BIOS `LIST` output into it and implement `LISTST` based on an explicitly tracked receiver session, printer health, protocol freshness and available FIFO credits. Keep normal console UART untouched; start disconnected/not ready on Z80 reboot.
3. **Transport (ESP32/Teensy):** Reuse the existing Teensy-master poll cycle. Include session ID, ordered sequence, length, byte payload and ACK/credit status. No dynamic task or large TFT `.bss` buffers; Teensy HP ISR only queues/reads a few bounded RAM bytes. At most one consumer may own the session; avoid loss, duplication and replay of printing after K8 detach.
4. **HP-side drain:** In verified safe non-ISR context, consume bounded received bytes and issue native printer output, honoring the active `PRINTER IS` setting. `CPM 705` selects an override for the active CPM attachment; restore previous HP printer state/selection on detach if a global setting must be changed. Do not reinterpret CP/M printer output as BASIC command text.
5. **Readiness:** `LISTST` ready only if an actively draining HP session has confirmed target readiness, a fresh heartbeat/ACK, and capacity for another byte. Drop readiness promptly on K8, ROM unload, timeout, printer error, cancellation, or buffer pressure. Retry only with explicit accepted acknowledgment; preserve ordering.
6. **Failure behavior:** CP/M `LIST` has **no error return code**, and some CP/M applications do not call `LISTST`; setting `LISTST=not ready` cannot by itself guarantee those programs will stop. Define an explicit, bounded no-loss strategy (backpressure/pause with operator cancel) and expose visible status before enabling the output path. Never silently discard a print byte and report success.

## Acceptance tests

- `CPM` uses existing HP `PRINTER IS` output; `CPM 705` uses the selected external address only for its session. Verify actual HP parser accepts optional numeric operand.
- CP/M `LISTST` indicates not ready when no CPM receiver is attached; a normal `LST:` printing app must not silently lose content or hang irrecoverably.
- Send a known multi-line sample to internal printer; then an external HP-IB printer; verify exact characters, ordering, CR/LF and FF treatment.
- Simulate unplugged/offline printer, queue full, ESP32/Teensy reboot, K8 detach while printing, stale reply/duplicate frame, and recovery/re-attach.
- Confirm `CP/M DIR`, CPDCAT, Multiplan, Media Manager, Web/TFT terminal, keyboard K8, basic HP printer operations, and PRINTER85 CHIDLE hook coexistence. Preserve EBTKS HP bus ISR timing and the ESP32 memory-optimized two-target builds.

## Explicitly deferred

- CP/M file transfer and mounted-image writes, HP-GL/GSX plotter support, arbitrary HP-IB bus mastering on ESP32, reimplementing the HP IEEE-488 stack, and merges to `main`.

**Design checkpoint:** The feasibility of an HP-side background receiver and its printer error handling must be validated before adding code that advertises `LISTST=ready`.
