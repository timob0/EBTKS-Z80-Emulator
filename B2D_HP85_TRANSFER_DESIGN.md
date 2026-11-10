## 2026-11-10 — Hardware validation and prompt-on-known-busy B2d correction

User confirmed CPM85 v0.05b C/A key detection now works while Multiplan is
running: Abort cancelled the pending import correctly and a later normal
import at the native A> prompt succeeded. The remaining delay before the
prompt (user estimate ~30 seconds) is inappropriate when the Z80 is already
known to be executing a program.

Fix: ESP32 `Cpm85DriveControl::transferReply()` now distinguishes the
existing native `Gate::mode` BUSY or PROBE_RUNNING state from UNKNOWN
before claiming that CP/M is not at CCP. It sends a new **advisory only**
transfer phase `B` ("CP/M RUNNING PROGRAM - RETURN TO A>") when a
running program is known. This is not authorization to run BDOS, and
changes no native safe-point criteria.

Teensy relays that phase as `B|...`. CPM85 **v0.05c** detects the phase
and prints the familiar C=CONTINUE A=ABORT prompt immediately at the
next poll. If Continue is selected it allows an additional bounded
40-poll interval, maintaining the original transfer ID, before another
prompt. UNKNOWN and WAIT_EMPTY_LINE still use bounded polling; active
BDOS still cannot be aborted. A=Abort retains verified remote cancellation.

Hardware user additionally reported `NOFILE.COM` as an import source
being rejected for "incorrect 8.3 naming", while `A:STAT.COM` worked.
This is consistent with the earlier logical drive/FCB source parsing on
an ESP32 firmware older than the literal SD-name change. The current
B2d source code explicitly accepts literal `NOFILE.COM` under
`/cpm/transfers` (then fails **file not found**) and rejects
`A:STAT.COM` as an SD source; the user may be running a mixed build.
**Do not assume that cause is verified.** The next test must deploy
a matched **ESP32 + Teensy + CPM85 v0.05c triplet** and confirm
`CPIMPORT "NOFILE.COM","F:ABRTST.COM"` reaches the
`source_not_found` check when CCP is idle. Keep files absent and
back up any writable CP/M disk before testing writes.

CI: CAPASM/CPMLIF/keyword regression is green for v0.05c; native ESP
and Teensy regressions green. ESP32 firmware (both variants and
static DRAM guard) and latest Teensy firmware build must be confirmed
green before recommending flashing. No main merge approved.

---

## 2026-11-10 — Busy prompt hardware failure, B2d v0.05b correction

User hardware observation:
- CPIMPORT busy wait took approximately **30 seconds** to show the C/A prompt.
- Pressing **A** did NOT cause the command to abort; HP RESET was used.
- Later, after CP/M returned to its prompt, retry of CPIMPORT initially
  reported `TRANSFER CANCELLED BEFORE BDOS START` after several seconds,
  followed by a successful further retry.

Do NOT interpret that stale cancellation report as proof that HP RESET
properly cancelled the requested transfer. The original transfer likely
remained owned by Teensy/ESP32 until a later status poll or the server's
60-second pre-start watchdog cancelled it. This was a serious usability and
state clarity defect.

The **root cause on HP** is likely KEYHIT being a single pending system
service slot: the earlier v0.05a prompt compared KEYHIT values, but while
the binary BASIC statement is running SVCWRD can retain a pending key and
KEYHIT need not change upon another keypress. System keyboard controller
documentation confirms KEYSTS read bit 1 indicates key pressed and KEYCOD
reads the raw key code. v0.05b switches to physical release/press detection
using the previously hardware-proven PRINTER85 KEYSTS LRB/JEV pattern,
then KEYCOD, without installing a new CHIDLE/KYIDLE hook.

v0.05b also:
- Adjusts the uncalibrated wait counter from 100 to 40 polls, nominal
  target ~12 seconds assuming ~30 s for 100 hardware polls; measure in
  hardware rather than claiming an exact 10-second timeout.
- Adds BASIC `CPXSTAT` for querying the original transaction after RESET,
  and `CPABORT` for an explicit remotely acknowledged pre-start
  cancellation of that same transaction. If BDOS has already started,
  `CPABORT` **does not stop or undo it**.
- Teensy rejects a second CPIMPORT/CPEXPORT START while a transfer remains
  owned with an explicit `PREVIOUS TRANSFER UNRESOLVED` error, instead
  of silently ignoring it and allowing a confusing query of an old ID.
- Preserves the original transfer ID across local HP mailbox timeout;
  a delayed or lost ACK is not grounds for retransmitting the write.

Next hardware sequence, **without any CP/M file write**, only after matched
firmware and CPM85 v0.05b assembly/CI:
1. With CP/M running a deliberately busy, non-writing program,
   `CPIMPORT "A_STAT.COM","F:ABRTST.COM"` (verify destination absent).
   Wait for prompt and press A. Expect `TRANSFER CANCELLED BEFORE BDOS START`
   promptly, and HP BASIC returns.
2. Return to native CP/M prompt; confirm F:ABRTST.COM was **not created**.
   Use `CPXSTAT` to display terminal state if needed.
3. Separately test `C` at the busy prompt and confirm it continues the
   **same** transfer without duplication. To avoid a write in this stage,
   use an intentionally nonexistent SD source filename.
4. To test interrupted BASIC state, while CP/M busy, interrupt the HP
   transfer with RESET. **Do not immediately retry CPIMPORT**; use
   `CPXSTAT` followed by `CPABORT` if still pending. A clear
   terminal/aborted message should precede any new START.
5. If A still does not register, do not proceed with transfer writes;
   log key behavior and revert to accepted working B2d import/export
   build. No `main` merge yet.

Hardware acceptance of the C/A path remains **pending** until user
confirms. Previous successful import/export round trip is unaffected
by the revised HP prompt logic.

---

## 2026-11-10 — Physical hardware export/import round trip

User confirmed on actual HP-85 / EBTKS / ESP32 / native CP/M hardware:
1. `F:MODEM.COM` exported from CP/M to Teensy SD by `CPEXPORT`.
2. The exported file was imported back into CP/M as `A:MODEM.COM` by `CPIMPORT`.
3. The resulting `A:MODEM.COM` program **ran successfully**.

This is a successful functional executable round trip across both transfer directions and two CP/M drives, strong practical evidence of preserved file integrity. An independently measured byte comparison, CRC values and transfer record counts were **not supplied**; do not claim they were separately verified.

Previous user tests also confirmed: read-only B: refused an import with an error, new A:TEST.COM imported successfully, F: import succeeded, and repeating a destination name on F: was correctly refused (no overwrite).

Outstanding hardware test: CP/M-busy wait, C=Continue / A=Abort, cancellation before native CCP safe-point activation, and no delayed write upon return to prompt. No main-branch merge approval.

---

## 2026-11-10 — User physical-hardware CPIMPORT regression results

User exercised the B2d HP BASIC `CPIMPORT` command on actual HP-85/EBTKS/CP/M hardware:

- B: drive, mounted as read-only floppy: import **failed**, and `CPIMPORT` reported the problem. Expected protection.
- A: system drive, new destination `TEST.COM`: import **succeeded**.
- F: other hard disk, new destination: import **succeeded**.
- F: retry using the **same destination filename**: import **failed**, with error reported. Expected CREATE-NEW/no-overwrite protection.

These observations validate important BASIC → Teensy → ESP32 → native BDOS import paths and refusal cases across different drives. They do **not** independently establish exact CRC, record count or binary readback from this message, nor confirm `CPEXPORT`, the busy CCP wait, C/A interaction, or abort-vs-start races on physical hardware. Do not mark those additional tests as passed until separately reported. No `main` merge authorization.

---

## 2026-11-10 — B2d source-argument correction (user-requested)

The **first CPIMPORT argument is a literal SD filename or optional
relative subpath under `/cpm/transfers/`**, and must NOT be rewritten
using a CP/M source-drive prefix. For example:

```basic
CPIMPORT "A_STAT.COM","B:STAT.COM"
```

reads `/cpm/transfers/A_STAT.COM` on Teensy SD and creates
`B:STAT.COM` in CP/M (assuming it does not already exist).
A hypothetical `CPIMPORT "PACK/HELLO.COM","B:HELLO.COM"`
reads `/cpm/transfers/PACK/HELLO.COM`. There is no `A:` on the SD
side. A literal relative slash-separated path is accepted but always
sandboxed below `/cpm/transfers`; absolute paths, parent traversal,
dot-prefixed components, colons, backslashes and control chars are
rejected. No arbitrary SD path access.

The pre-existing B2c **web API** still accepts its historical
`A:STAT.COM` logical source and maps it to `A_STAT.COM`; this
compatibility is intentional and independent of the new BASIC syntax.
CP/M destination names remain strict A:–P: 8.3, CREATE-NEW.
**Use B: for the first writable hardware trial and back up the B:
disk image first**, not system disk A:.

The import change touches ESP32 gate/controller, its pinned CPU SD
transport, Teensy read-only source validator and native/static
regression checks. Coordinated firmware validation must complete
again before flashing these new commits.

---

# B2d — HP-85 blocking CP/M file transfers (implementation candidate)

Updated 2026-11-10. Branch in all repositories:
`feature/cpm-file-transfer-b2d`.

**This is not yet hardware accepted. Do not merge into `main`.**
The original B2c 8192-byte Z80 CPU-task stack and B2b/B2c native BDOS
engines remain unchanged.

## Implemented

- CPM85 v0.05a adds BASIC `CPEXPORT "A:STAT.COM"` and
  `CPIMPORT "A_STAT.COM","B:STAT.COM"`, using native string
  parser ABI and the existing four-port Teensy mailbox.
- Separate START (9 export/10 import), STATUS (11) and remotely
  confirmed pre-start CANCEL (12) opcodes. All opcode constants
  in Capricorn source are **octal** (`=11`, `=12`, `=13`, `=14`).
- Teensy fixes transaction ID when the START opcode arrives, BEFORE
  any ACK, and retains it across local timeout (opcode 0), preventing
  lost ACKs from stranding a potentially running operation.
- ESP32 owns one serialized HP transaction. Its gate reservation
  excludes competing HTTP services; CCP start uses an atomic
  `WAIT_EMPTY_LINE` -> `RUNNING` compare-and-swap against
  pre-start cancellation. Repeated status queries cannot reissue MAKE.
- Transfer replies begin with a machine phase (`W` waiting CCP,
  `P` armed, `R` running, `S` succeeded, `F` failed,
  `X` cancelled, `E` rejected); a local UART timeout is a
  separate `U`, never proof of remote cancellation.
- HP BASIC blocks and polls status; for repeated pre-start waits it
  prompts `CP/M NOT IDLE - RETURN TO A>` and
  `C=CONTINUE A=ABORT`. The C/A keyboard path reads HP native
  `KEYHIT`; **verify on physical hardware before writable tests**.
  Initial prompt delay uses an uncalibrated bounded software counter;
  the previously discussed ~10 seconds is the target, not yet measured.
- An unacknowledged pre-start request is automatically cancelled after
  60 seconds without HP status contact. Active BDOS is never aborted
  by this lease or by the C/A request. A stopped/interrupted BASIC
  statement is not remote cancellation.
- Teensy refuses `CPRESET` and `CPREBOOT` while an unresolved
  HP transfer still owns the transaction. Existing K8 keyboard
  detach/reattach and disk/terminal paths are unchanged.
- Final report includes actual 128-byte records sent, byte count,
  cumulative CRC16, source/BDOS failure reason, and possible
  partial CP/M target warning. CREATE-NEW, 63/64-record limits and
  independent BDOS readback remain in force. No ZIP/batch yet.

## Build gate on Mac (no flash before all succeed)

ESP32:
```sh
git switch feature/cpm-file-transfer-b2d && git pull
bash tests/run_z80_core_native.sh
python3 tests/check_cpm85_bridge.py
pio run -e esp32dev
pio run -e esp32dev-tft
python3 tests/check_esp32_dram_budget.py
```

Teensy:
```sh
git switch feature/cpm-file-transfer-b2d && git pull
python3 test/check_cpm85_drive_read_abi.py
pio run -e teensy41
```
Option ROM:
```sh
git switch feature/cpm-file-transfer-b2d && git pull
python3 tests/check_cpm85_keywords.py
mkdir -p build
./py311/bin/capasm src/cpm85.asm -b build/CPM85.bin -l build/CPM85.lst -r 2 -c
./py311/bin/caplif build/CPM85.bin -l build/CPM85.dat -f CPM85
```
When the Mac build gates are green, retain tested B2c binaries and
backup mounted CP/M disk images plus the SD transfer folder.
Deployment order: matched Teensy41 firmware, ESP32
`esp32dev-tft` via the working **serial->Teensy->ESP Prog**
recovery route (browser OTA is unreliable), then CPM85.dat to SD
and `LOADBIN "CPM85"` on HP-85. Do not replace SIMON85 or PRINTER85.

## Controlled first hardware tests (use this order)

1. Verify `CPVER` reports **CPM85 0.05a**; check CPSTATUS, native
   CP/M DIR, K8 back to HP BASIC, and `CPM` re-entry.
2. First exercise the waiting/C/A path with CP/M deliberately BUSY,
   not parked at `A>` (e.g. a nonwriting program). Issue
   `CPEXPORT "A:NOFILE.COM"` from HP BASIC. Test **A** cancellation
   before returning to CCP. Observe message `TRANSFER CANCELLED
   BEFORE BDOS START`; check no surprise operation occurs when
   CP/M later reaches `A>`.
3. Repeat waiting with **C** and allow CP/M to return to A>.
   `NOFILE.COM` does not exist, so the expected result is a
   non-destructive `file_not_found` error. If C/A keyboard input
   does not work, STOP testing and do not attempt a writable import.
4. Only after the above: confirm `B:STAT.COM` is ABSENT, backup A.dsk,
   check published Teensy SD `A_STAT.COM` exists, then
   `CPIMPORT "A_STAT.COM","B:STAT.COM"`. Expect 42 records,
   5376 bytes, CRC16 54346 and clean BDOS readback. Check CP/M
   DIR/STAT and SD file integrity. Never execute copied COM until
   independently verified.
5. Test export of an existing small CP/M file whose destination
   `/cpm/transfers/A_<NAME>` is ABSENT. Do not reuse
   `A:STAT.COM` export if `A_STAT.COM` already exists.
6. After successful transfer, check ESP /api/system/memory and
   /api/system/health, especially Z80 8192-byte stack watermark,
   free8, largest8, watchdog/reset and SPI failure counters.

A failed import after MAKE may leave a partial new CP/M file; do
not auto-delete or silently retry. No extra diagnostic-only flashes.
No `main` merge or branch deletion without explicit user approval.
