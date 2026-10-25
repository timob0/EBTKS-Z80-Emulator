## 2026-10-25 — Post-freeze SOURCE AUDIT: concrete Capricorn register-save bug

Audit of retired HP ROM **CPM85 v0.06b** at commit
`f46b297a10cd09b739ddc9e727f5603e225382b6`
revealed a real error independent of display timing:
the native `RMHOOK` save sequence was

```asm
PUMD R10,+R6
PUMD R20,+R6
PUMD R30,+R6
PUMD R40,+R6
LDMD R14,=BINTAB
```

The Capricorn CPU multi-byte boundary rule means these save only
R10-R11 (2 B), R20-R21 (2 B), R30-R31 (2 B), R40-R47 (8 B):
**14 bytes, NOT the claimed R10..R47 context**. Subsequent
`LDMD R14,=BINTAB` overwrites **UNSAVED R14-R15** immediately;
`RMDRAW` also overwrites **unsaved R22 and R34-R35**.
The four inverse `POMD`s restore only the same partial regions,
then the original ROM361 AUXROM `RMIDLE` function is invoked
with altered CPU register state. This is **a confirmed source
defect and strong candidate for the observed hard freeze**;
there is no trace proving the exact instruction where execution
failed. This violates the project's previously learned register
width rule from CPDMNT handling. Manual: Capricorn CPU, pages
3-4 through 3-9 and 4-21,
`https://www.jeffcalc.hp41.eu/emu75/files/capricorncpu.pdf`.

**Other independent defects/risks:** MNRUN initialized
`RMDIRTY=1`, so the first RMIDLE callback immediately performed
32-char CRTBAD/CRTDAT work before ROM361 had been chained even
once; CRT output safety from EXEC RMIDLE was never verified.
The repaint interval of 256 **callbacks** is not a real-time
throttle and may overwhelm the editor if RMIDLE is frequent.
ROMJSB nested-entry and status/stack preservation are also
not validated, and KREL's ownership-mismatch logic needs
recovery handling.

**Do not revive v0.06b by editing only register pushes.**
Future new hook should be split into a *counter-only native
transparent RMIDLE chain* (independently proven; full actual
register/status preservation, no CRT), then a separate native
CRT rendering service in safe context. Prefer emulator/ROM
disassembly validation before another risky HP hardware test.

Findings recorded in
`timob0/HP85-Option-Roms/docs/CPM85_NATIVE_RMIDLE_C2B.md`
feature-branch documentation commit
`4933c61c77925e283a2eb1b98564c3604fbe9da4`.
**Active source unchanged:** HP branch remains v0.05c rolled-back;
last physical acceptance C1 v0.05a; Teensy/ESP32 unchanged;
all `main` baselines pristine.

## CRITICAL C2b HP-85 HARDWARE FAILURE (2026-10-25)

**Do not run the experimental v0.06b `CMENU` binary again.**
The user tested the HP-native RMIDLE/CRT experiment:
**no counter displayed; HP-85 froze; K8 had no effect; pressing RESET
recovered through a warm boot.** C2b is NOT physically accepted.
The preceding successful native assembler CI was only a build/static
check and cannot establish the safety of the RMIDLE hook.

The C2b code installed a wrapper over the existing EBTKS AUXROM 361
RMIDLE slot and called direct HP CRT status/address/data I/O from
that wrapper. `RMDIRTY=1` meant it could attempt a CRT write on
the very first RMIDLE invocation, **before displaying any counter**.
No visible count does not establish that RMIDLE was inactive.
Potential causes include the RMIDLE trampoline, EXEC/ROMJSB calling
or return conventions, CRT activity from the callback, or interacting
with native keyboard hook/stack state. No root cause is confirmed.
Avoid rerunning the same composite experiment.

**Repository rollback completed:** HP Option ROM
`feature/cpm85-hpib-output` source `src/cpm85.asm` and original
tests/workflow returned to pre-C2b commit
`72d389ae33a77c6b239ca09116a609a2942a1ddb`
(experimental CPM85 v0.05c with C2 foreground probe disabled;
physically accepted C1 CPUI and legacy CPM source intact).
The C2b-specific source-presence regression was deleted.
Teensy/ESP32 code unchanged; all main branches untouched.
Use physically accepted **C1 v0.05a** at HP Option ROM commit
`13655f2d8bd0889b91aa4d0a47c8936bcb429d0c`
for subsequent real hardware work.

**Next technical investigation:** read the exact HP-85 EXEC/ROMJSB
call and return contract (ideally emulator testing) before installing
another live RMIDLE hook. Split proof into **counter-only, no CRT**
and **native display** as separate isolated tests; actual printer
service is deferred. The native UI must remain source-free without
Teensy mirroring. No new hardware binary is offered at this point.
Detailed incident: `HP85-Option-Roms/docs/CPM85_NATIVE_RMIDLE_C2B.md`.

## 2026-10-25 — C2b HP-native CMENU implementation, hardware acceptance PENDING

The user explicitly requested the implementation after identifying the
physical `RMIDLE` hook bytes. This is a **new HP-only executable
prototype** (no Teensy or ESP32 source changes) on
`timob0/HP85-Option-Roms`, `feature/cpm85-hpib-output`.

New **CPM85 v0.06b**, keyword **`CMENU`**, extends rather than
replaces the previously hardware-accepted `CPM`/C1 code. It uses
the hardware-proven `CHIDLE` + K8 release path and immediately
returns from its LOADBIN runtime to HP BASIC's normal EXEC,
no BASIC source program or foreground busy spin needed.

The `RMIDLE` hook at octal `102434` is only installed over the
seven *exact* bytes measured on the physical HP:
`316,376,011,063,147,361,236` octal. That sequence represents
`JSB =ROMJSB; DEF 063463; BYT 361; RTN`. It is compared byte-for-byte
and copied into private RAM before installing a 4-byte
`JSB <relocated native RMHOOK>; RTN` entry. The callback preserves
R10–R47 registers on R6, counts visits in three ASCII decimal
digits modulo 1000, and paints one native 32-character HP-85 CRT
alpha row with explicit CRTBAD/CRTDAT I/O. CRT repaint occurs only
on each 256th callback or after a K1–K4 state change, each CRTBUSY
test has a bounded loop, and CRTBAD is restored from `CRTBYT`.
**The exact original AUXROM361 ROMJSB descriptor is called LAST
after register restore** so its returned condition/status remains
authoritative. No calls into EBTKS CRT DMA or `screenEmu`.

K1/K2/K3 set local page 1/2/3; K4 toggles simulated Control,
with no keyboard forwarding to the ESP32. K8 calls the tested
`EOJ2` release sequence: `KREL` restores the prior RMIDLE
7-byte image only if the live hook still belongs to this module,
then restores the CHIDLE image. CPOFF/LOADBIN unload also run KREL.
The old crashed CPKTEST remains **disabled**. HP-IB and printer
output/ready are not implemented and remain disabled.

**CI successful:** HP native CAPASM/CAPLIF and source regressions
in GitHub Actions run **40206116757** with head
`f46b297a10cd09b739ddc9e727f5603e225382b6`.
No physical C2b hardware test has occurred yet. Screen overlay
restoration after K8 is NOT complete (BASIC program is untouched
but the native 32-char row might remain until refreshed).
Hook callback/CRT interactions still require careful hardware
validation. Test first using a disposable HP BASIC edit session,
with C1 v0.05a rollback image handy. Build instructions and physical
acceptance procedure:
`HP85-Option-Roms/docs/CPM85_NATIVE_RMIDLE_C2B.md`.
Do not test until both `CHIDLE` and `RMIDLE` ownership conditions
are known. **All repository main branches untouched**.

## 2026-10-25 — Physical HP-85 RMIDLE bytes IDENTIFIED

User read native RAM at `RMIDLE` decimal 34076..34082:

`206 254 9 51 103 241 158` (decimal).

Exact decoder: `JSB =ROMJSB` opcode 316 oct + pointer
`04776` oct, then `DEF 063463` oct, ROM byte `361` oct,
`RTN` opcode 236 oct. This is the **installed AUXROM 361
RMIDLE handler** in the documented 7-byte ROMJSB format.
See the HP assembler ROM manual Section 3; EBTKS identifies
ROM361 as its primary AUXROM.

**Critical implication:** RMIDLE is owned by EBTKS and is
not vacant. Correct C2 all-HP-native design must preserve and
invoke the full original seven-byte handler during every native
RMIDLE callback. Proposed: make a private 7-byte executable
RAM copy, install a 4-byte relocation-aware native RAM trampoline,
invoke the original saved handler, increment an HP-side counter,
then safely throttle actual **HP-native** CRT drawing. The
RAM/ROMJSB calling convention and safe register preservation
must be verified before requesting another physical test.
K1-K4 and K8 remain via the already proven CHIDLE hook.
**No new code, firmware flashing or HP-IB printing in this diagnostic.**

Full details: `timob0/HP85-Option-Roms`
`docs/CPM85_NATIVE_CONSOLE_RESEARCH.md` commit
`72d389ae33a77c6b239ca09116a609a2942a1ddb`.

## CURRENT CORRECTION (2026-10-25): native-only HP console, erroneous C2a retracted

The user correctly rejected Teensy-side RMIDLE instrumentation,
Teensy CRT DMA drawing and any use of EBTKS `screenEmu` for the
HP-85 management console. **Only HP-85 native LOADBIN code is
responsible for keyboard events, RMIDLE service count and CRT
rendering.** The Teensy and ESP32 remain unchanged transport and
CP/M peripherals for this stage.

The mislayered C2a hardware test `CMENU` showed no counter or
K1–K4 response, although K8 exited and preserved the current HP
BASIC program. The absence of a count was caused by the fact that
the EBTKS CRT probe deliberately returned immediately when the
optional `screenEmu` setting was false. This is **not evidence**
that HP's RMIDLE stopped running.

**Source recovery complete:**
- `timob0/ebtks-alt-firmware`
  `feature/cpm85-hpib-output`: all five modified original files
  restored to byte-for-byte `main` content; the three new
  probe header/source/test files deleted. No experimental Teensy
  firmware remains on the branch.
- `timob0/HP85-Option-Roms`
  `feature/cpm85-hpib-output`: deleted the HP-source
  `CMENU` probe and reverted HP ASM, keyword tests and CI
  to pre-C2a, **CPM85 v0.05c** (quarantined crashed native
  `CPKTEST`, accepted C1 `CPUI` and legacy `CPM` intact).
  The old build/test guide now carries a prominent retraction.
- `main` branches unchanged; ESP32 feature source unchanged.

**Correct C2 plan:** inspect octal `RMIDLE=102434` (decimal 34076),
whose seven-byte slot already contains the AUXROM/EBTKS handler.
Implement a checked native hook chain that calls the saved
original EBTKS hook, increments an HP RAM counter and restores the
prior hook bytes on K8, CPOFF/unload/reset. Independently confirm a
safe native CRT overlay primitive (not redirectable BASIC PRINT,
not EBTKS DMA, not blindly invoked from the RMIDLE callback).
The preexisting native `CHIDLE` keyboard callback remains the
accepted source-free K8 handler. No code that touches RMIDLE
is being offered for physical testing until these contracts are
understood; no printer or CP/M LST readiness enabled.

Detailed revised research is in
`HP85-Option-Roms/docs/CPM85_NATIVE_CONSOLE_RESEARCH.md`,
HP feature commit `b3f7f692d0633127e5f2f9cb8ce6a3aca2630ad9`.

# Stage C proposal — CP/M LST: to HP-85-configured printer

**Status 2026-10-24:** **Stage C0 physical HP-85 test PASSED.** The user
confirmed the standalone `tests/CPMCONSOLE.bas` foreground management
console prototype works. It uses AUXROM keyboard buffering and HP CRT
`DISP`, with K1-K3 management pages, K4 simulated Control/UI toggle,
and K8 exit/cleanup. No actual printer data or CP/M forwarding is
implemented by C0. All three `feature/cpm85-hpib-output` branches were
created from the accepted `main` baselines. ESP32/Teensy code remains
unchanged. Do not merge to `main` until complete C-stage hardware tests.

## 2026-10-25 — C2a RMIDLE console hardware probe ready (UNTESTED ON HP)

**Implemented on the feature branches, not yet physically validated:**

- `timob0/HP85-Option-Roms`, `feature/cpm85-hpib-output`:
  `src/cpm85.asm` **CPM85 v0.06a** exposes new **`CMENU`**
  LOADBIN statement, intentionally alphabetic/unique to avoid
  HP BASIC prefix ambiguity with existing `CPM`. It duplicates
  the *proven* `CHIDLE` stub/save/restore logic, sets `KHACT=2`
  and writes `CPCTL=2` instead of the old `CPM` attach=1.
  It immediately returns to BASIC editor processing; no foreground
  busy loop and no BASIC application/source needed. K1-K3 pages,
  K4 mode, K8 exits via old local hook. All non-K8 keys are delivered
  to Teensy `CPKEY` but feature Teensy consumes them locally; no
  forwarding to Z80 in test mode. `CPKTEST` remains disabled.
  Hardware test guide: `docs/CPM85_RMIDLE_CONSOLE_C2A.md`.
- `timob0/ebtks-alt-firmware`, same feature branch:
  `-DEBTKS_CPM85_RMIDLE_PROBE_ENABLE=1` enabled only in the
  development `platformio.ini`. An opt-in probe counts **actual
  EBTKS AUXROM_RMIDLE() invocations**, does not replace/chain the
  hook or alter mailbox processing, and throttles HP-85 CRT overlay
  painting from **normal Teensy main loop** using the existing
  `Write_on_CRT_Alpha` DMA path (~4 fps). Status includes numeric
  invocation count, `ACTIVE` / `STALLED` (no callbacks for 2 s),
  selected page, and simulated UI vs Control mode. All CRT DMA is
  excluded from bus ISR, HP keyboard hook and AUXROM handler.
  On K8 Teensy attempts to restore the captured CRT image;
  on HP RESET `CPCTL=4` requests exit without replaying a
  pre-reset snapshot. Legacy control 0/1 still works.
- ESP32 branch: **no code modifications**. No CP/M LST, printer,
  mode-ready or HP-IB worker behavior is enabled.

**CI:** HP native CAPASM/CAPLIF + regressions passed in HP
`feature/cpm85-hpib-output` run `40195681837` (source includes
documentation commit); Teensy firmware + native tests passed
in Teensy `feature/cpm85-hpib-output` run `40195573628`.
No physical firmware test yet. Two firmware assets need rebuilding:
**Teensy and HP LOADBIN**, not ESP32.

**C2a acceptance:** with a disposable BASIC program and `CRT IS 1`,
execute `CMENU` as an immediate command, observe `RMIDLE` counter
advancing **while no keys are pressed**; K2/K3/K1 change pages,
K4 toggles simulated modes without stopping callbacks, K8 restores
HP keyboard/screen, then regress legacy `CPM` forwarding/K8.
`RMIDLE` count may stall, which is a useful negative result.
The CRT capture/restore use is experimental; preserve a backup of
important BASIC program edits before this hardware experiment.
No `main` merge until accepted.

## 2026-10-25 — HP timer and nonblocking LST output research

HP-85 **does** have three system-backed programmable timers and
millisecond system clock. However the `ON TIMER#` callback mechanism
branches into an HP BASIC program, and the HP Advanced Programming
manual says it is associated with the program that declared it, rather
than a free-standing native LOADBIN service. It is not an appropriate
way to interrupt another user's BASIC program/edit session.

The HP Assembler ROM Manual confirms:
- **`IRQ20` is an I/O interrupt vector; it does NOT mean a 20 ms
  timer or periodic clock callback.**
- Clock interrupts normally use the ON TIMER machinery.
- The HP executive loop normally tests `CSTAT`, tests `XCOM`,
  then enters **`RMIDLE`**. This is the promising cooperative
  no-key-required service point, but it is not a guaranteed
  millisecond-periodic callback.
- EBTKS AUXROM/Teensy already owns `RMIDLE`, dispatching its
  autostart/batch/Wi-Fi key injection. Do not commandeer it.

**Architectural update:** keep a resident native LOADBIN console
(`CHIDLE`/possibly `KYIDLE` for keys) plus a **bounded, cooperative
printer worker serviced through an explicitly verified RMIDLE
extension/dispatch path**. An idle invocation may schedule/check data
and handle only a small tentative chunk (16–32 bytes initially), but
actual HP-native `PRINTER IS`/HPIB output must run only after a
safe calling context and offline/timeouts are proven. Never run HP-IB
transactions in raw IRQ/clock/keyboard hooks or the Teensy bus ISR.
Short chunks reduce ordinary busy periods but **do not protect
against even a single indefinite HP-IB handshake**.

CP/M `LISTST` readiness and buffer credits follow confirmed printer
service availability, not just an active `CPM` session or a timer
tick. UI entry drops readiness; CP/M may continue executing.
If EBTKS RMIDLE is not reached during a given HP operation,
printing must pause/backpressure rather than fake readiness.

Research is documented in
`HP85-Option-Roms/docs/CPM85_NATIVE_CONSOLE_RESEARCH.md`,
HP feature-branch commit
`4b7fdb2bef05f9e9a44aabd49472fa2ee58e7649`.
**Documentation only; no firmware changed or new hardware test.**

## 2026-10-25 research decision — fully self-contained native LOADBIN console

The user clarified a fundamental product constraint: **do not require
loading an HP BASIC source program to use the management console**.
It must launch with the existing `CPM` LOADBIN keyword even when the
user is editing another HP BASIC program, preserving that program and
editing state. The prior proposal to retain C1's BASIC event-pump in
the final product is therefore **REJECTED for the finished console**.
C1 remains useful solely as the hardware-tested page/render state
prototype and regression reference.

Research checked HP-85 global symbols (CAPASM and EBTKS headers),
original PRINTER85/CPM85 hook code, and the Teensy EBTKS AUXROM
`AUXROM_RMIDLE` implementation. HP-85 OCTAL intercept addresses:
`CHIDLE=102416` (character editor), `KYIDLE=102425` (keyboard
intercept), `RMIDLE=102434` (EXEC intercept). A published HP
Assembler manual labels `KYIDLE` as polled upon keypress. Actual
HP-85 callback ABI is still to be established. EBTKS already uses
`RMIDLE` for autostart/batch keystroke injection; its source
describes codes `2` and `3` for taking/releasing keyboard ownership
but explicitly calls that feature untested on the Teensy.

**Preferred direction:** A source-free **resident native, event-driven
console**: a LOADBIN command initializes and draws menu, installs a
verified hook and returns control to the normal HP firmware keyboard
event path. The user remains in modal Management/Control mode until K8,
but **no BASIC program has to be loaded** and **no native tight loop
blocks HP keyboard processing**. Reuse the already accepted CPM85
`CHIDLE` event handling / exit path as the first C2a keyboard
checkpoint, lifting the C1 menu logic into a callback-safe routine
that does not assume BASIC statement parser/stack state. Research
`KYIDLE` as a potentially better future ownership hook; do not
blindly overwrite CHIDLE when PRINTER85 owns it.

A **second, no-key-required** HP service for eventual `LST:`
printer reception must be designed using a *cooperative, confirmed*
interpreter idle hook. The EBTKS `RMIDLE` hook is already occupied:
cooperate/chaining is required. Validate actual no-key callback
behavior first with a harmless event counter before modifying any
ESP32/Teensy firmware or claiming printer ready. Do not assume the
HP runs a service callback continuously during arbitrary BASIC code.

The detailed research report and incremental hardware acceptance
gates are in
`timob0/HP85-Option-Roms/docs/CPM85_NATIVE_CONSOLE_RESEARCH.md`
on `feature/cpm85-hpib-output`, commit
`4429f1409b8ef8b6ab8049b227ead7208f02b0be`.
All three `main` branches unchanged. **No executable code changed
in this research checkpoint**; C2 unsafe `CPKTEST` stays quarantined.

## C2 crash clarification and revised next step (2026-10-24)

Follow-up from physical HP-85 testing: `CPKTEST` **printed its
initial heading**, then **did not respond to any keys**, then after
a short delay the HP CRT became **blank with a cursor**, visually like
RESET. No confirmed machine reset cause was measured.

This suggests that `KEYHIT` was not delivering usable events while
the native LOADBIN foreground command was running. The incorrect
indirect `JSB =K2KEY` is a proven source defect but is only invoked
after a detected change, making it **less convincing as the specific
trigger** of the reported failure than originally believed. The
tight firmware-unaware spin, counter expiry and return path remain
possible contributors; root cause is **not conclusively identified**.
The unsafe native C2 experiment stays quarantined in v0.05c,
with C1 v0.05a the last physically accepted source baseline.

**Recommended architecture:** retain C1's **verified BASIC
`KBDBUFFER/KBDISKEY/KBDKEY` foreground event pump**, with **native
`CPUI` handling UI state and CRT rendering**. Extend it with a
short, bounded, promptly returning **native service/poll operation
`CPUPOLL`** callable from the BASIC loop even when no key arrives.
This provides an execution context for later HP-native printer
operations and mode/readiness handling while avoiding unproven
`KEYHIT` busy polling. The user-facing console remains modal,
and CP/M keeps running independently. In Management mode both
keyboard forwarding and LST receiver pause; only Control mode may
eventually advertise LST readiness after real target/backpressure
checks. First CPUPOLL prototype should avoid all peripheral output.

No new implementation in this follow-up: this records the diagnosis
and route forward. See `HP85-Option-Roms/docs/CPM85_CONSOLE_C2.md`
for the detailed physical sequence.

## **CRITICAL Stage C2 crash — C2 NOT ACCEPTED; recovery done (2026-10-24)**

User reports **physical HP-85 crash when invoking `CPKTEST`**
from experimental CPM85 v0.05b. Do **not** recommend rerunning that
binary. This overrides the earlier C2 CI/static-test success.
C0 and C1 are still the last accepted physical console milestones.

Source review found **`JSB =K2KEY` used for a relocatable local
LOADBIN function**. Proven local call convention is
`JSB X14,K2KEY` with the BINTAB relocation base. This is a concrete
unsafe instruction, although it is not established to have executed
on the observed heading-then-no-keys path. Separately, a `KEYHIT` busy spin without a verified HP firmware
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
