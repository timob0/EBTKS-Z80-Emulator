# CPM85 management service — Stage A: CCP-safe injection probe

**Date:** 2026-10-22  
**Branch:** `feature/cpm85-management-console`  
**Status:** Source/native CI implementation. Awaiting physical HP-85/TFT/Web acceptance.

## Goal and safety boundary

This is **not yet CPFPUT/CPFGET or hot disk switching**. It proves that ESP32
firmware can accept a remote request only when the native CP/M 2.2 CCP is in
its interactive command-input loop, execute a tiny non-BDOS Z80 service at a
verified CCP instruction boundary, then restore *all* Z80 CPU and temporary
memory state before continuing the untouched CCP.

- No CP/M BIOS, BDOS, resident image or Teensy SD code is changed.
- No new HP LOADBIN is required; CPM85 v0.04b is unchanged.
- No 8 MiB image data or filesystem writes.
- No interrupt-time BDOS call; no injection from the Web/keyboard tasks.
- All execution/injection/restoration happens exclusively on the existing
  pinned Z80 CPU task. The web task only changes small atomic request state.
- Existing E603 warm-boot trap remains intact and has precedence.
- If the loaded CCP does not match the compiled native byte signatures, no
  service can be armed. Legacy-CPU firmware explicitly disables the feature.

## CCP execution gate

From the checked-in `CPM Source/cpm22.lst` (native CP/M 2.2 at D000h):

| Z80 PC | Meaning |
|---|---|
| `D19C` | CCP about to invoke interactive BDOS function 10 — *CCP input idle* |
| `D1A4` | BDOS line-input call returned — *no longer idle* |
| `D398` | CCP processing the returned command line |
| `D382` | CCP entering next command-prompt loop (safe injection boundary) |

A request is accepted only in `ccp_input` mode. The operator must press
an **empty Return** at the CP/M terminal. The gate checks the BDOS line length
at `D007` and will arm the service at the next `D382` fetch **only when the
line is empty**. A nonempty command line cancels the request immediately.
A warm boot or Z80 reset cancels it. A generation counter also rejects any
request that races with line submission. No request is queued to execute
after an application terminates.

This deliberate empty-Return handshake avoids calling BDOS from inside its
own console-input routine (which would violate non-reentrancy). The future
CPFPUT/CPFGET service will use the same D382 trap and *then* invoke BDOS only
from outside the BDOS call stack.

## Harmless service probe

At the D382 fetch, the CPU worker snapshots the full `Z80` state and the
5-byte scratch region at `C000`. It copies this minimal ROM-bundled machine
code into the scratch region:

```asm
LD A,42H
CP 42H
HALT
```

No port operations, memory writes, SD, network calls or BDOS operations are
performed by the guest code. HALT ends the bounded probe under emulator
control; `A=42h` and the expected halt location are verified. The CPU state
and scratch bytes are then restored byte-for-byte. The original D382 opcode
executes normally, leading back to the CP/M prompt.

The portable gate and the actual redcode/Z80 probe are covered by
`tests/cpm_safe_point_native.cpp` (native fixtures with native CCP opcode
signatures, busy rejection, nonempty-input cancellation, warm-boot cancel,
real opcode-trap, state restoration, and rearm gating).

## Physical hardware test (ESP32 firmware only)

Keep the verified stable `recovery/` binary unchanged. Flash the
`esp32dev-tft` build of this feature branch, then visit:

1. `http://<ESP32-IP>/api/system/health` — verify the new firmware Git SHA.
2. `http://<ESP32-IP>/api/cpm/service/status` — when A> awaits input:
   `supported=true`, `ccp_idle=true`, `mode="ccp_input"`.
3. From macOS terminal, with the CP/M terminal at an empty A> prompt:
   `curl -X POST http://<ESP32-IP>/api/cpm/service/probe`.
   Expected HTTP **202**, `accepted=true`, `probe="waiting_for_empty_return"`.
4. Press **Return on the actual CP/M terminal with no command entered**.
5. GET the status URL again. Expected `probe="passed"`, prompt working.
6. Repeat with a **nonempty command** such as `DIR`: pending probe should
   change to `cancelled_nonempty_command`, and no injection should run.
7. Start a foreground CP/M application (MODEM.COM or another known program)
   and GET status: `ccp_idle=false`. POST should return **409** with
   `accepted=false`. Exit the program and verify CCP input is recognized.
8. Check CP/M DIR, G: mount, keyboard, TFT and Web/Telnet still behave as
   before. An error or mismatch means do NOT merge/advance to file writes.

**Phase A limitations:** The first injection requires the user to press an
empty Return. It is intentionally **not** an asynchronous takeover of a
live BDOS input frame. It offers no filesystem manipulation, transfers,
disk switching or sidecar metadata. Once physical behavior is confirmed,
we can implement the CP/M BDOS file-transfer service and make the UI/page
control call its guarded API.
