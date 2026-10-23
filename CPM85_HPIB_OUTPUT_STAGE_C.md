# Stage C proposal — CP/M LST: to HP-85-configured printer

**Status 2026-10-24:** design/branch setup ONLY. No printer routing code implemented or hardware tested in this stage. Created three fresh `feature/cpm85-hpib-output` branches at the current accepted `main` of ESP32, Teensy and HP Option ROM repositories. Do not merge before complete physical HP-85 acceptance.

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
