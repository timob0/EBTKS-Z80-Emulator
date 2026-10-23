# Stage B1 HP BASIC CPDCAT and automatic CCP safe point

**Status:** experimental three-firmware feature; NOT hardware-accepted.
Previously accepted ESP32 Stage B1 directory read (manual blank Return) was
user-tested on real hardware, followed by successful DIR, Multiplan and Media
Manager tests. These new automatic-return / HP catalog changes have NOT yet
been flashed or regression-tested physically.


## HW root cause isolated: invalid directory filename, Z80 reset policy corrected (2026-10-24)

After running `CPDCAT "A:"` under diagnostic ESP32 firmware
`cc574613a87049c36e17216b8eec50fd3c078d24`, the operator read:

```json
{
  "supported":true,"ccp_idle":true,"mode":"ccp_input",
  "probe":"never","operation":"directory","checkpoint":"idle",
  "directory_error":"none","last_directory_state":"failed",
  "last_directory_error":"invalid_directory_filename",
  "last_directory_checkpoint":"bdos_search_failed",
  "last_directory_forced_z80_reset":true
}
```

This proves native BDOS returned a result which the ESP32 8.3 filename
decoder rejected; it does **not** prove why the raw byte is noncanonical.
The old host `execute()` treated EVERY failure, including host-side
filename validation AFTER a completed BDOS call, as a reason to force a
Z80 reboot. This is overly broad and explains the reset.

### Corrective ESP32-only changes

- Decode each name without inventing a filename. If a byte is invalid
  (after stripping CP/M 8.3 attribute bits), **skip that entry**, continue
  bounded BDOS Search Next, return `passed` with a visible warning.
- Record `skipped_malformed_entries`, `first_bad_search_index`,
  `first_bad_name_offset`, and `first_bad_name_byte` in the directory
  GET response. Anomalous entries are not silently presented as files.
- `last_directory_error:"invalid_directory_filename"` is retained as a
  **warning** even when `last_directory_state:"passed"`. A warning is
  not a BDOS execution failure.
- Only interrupted BDOS execution or unexpected mutation of the CP/M
  DMA pointer requests a Z80-only restart. Other metadata failures may
  fail the catalog but do not reboot the guest.
- HP `CPDCAT` renders actual line feeds (previously source string
  literals contained doubled backslashes), bounded 32-entry listing,
  extent count/truncation notice, and `N BAD NAME(S) SKIPPED`.
- Native regression checks a deliberately malformed directory name,
  byte-level warning, exact CPU and TPA restoration, no reset; separately
  checks interrupted BDOS still requests a reset.

**Pending hardware acceptance:** latest ESP32 TFT firmware CI and
local Mac TFT link must pass before flashing. Keep HP v0.04c and Teensy
`feature/cpm85-catalog` unchanged. After CPDCAT at A> prompt, inspect
`GET /api/cpm/service/directory` for `state:"passed"`, `skipped...`
and first bad byte. Compare with CP/M `DIR`; test Multiplan and Media
Manager. No merges and no disk filesystem writes are introduced.

## Post-error hardware evidence, running ESP32 SHA 74bb45f3 (2026-10-23)

User read both endpoints after the HP error (without rearming the request):

```json
GET /api/cpm/service/status
{"supported":true,"ccp_idle":true,"mode":"ccp_input","probe":"never","operation":"directory","git_commit":"74bb45f3a6554489670746b721b2df2b47d64cf0", ...}
GET /api/cpm/service/directory
{"supported":true,"operation":"directory","state":"never"}
```

**Interpretation:** `Gate::reset()` is invoked during a full Z80 cold
rebootstrap in `ESP32_Z80_Emulator.ino`, and resets `result` to NEVER but
does not reset `operation`. The reading is consistent with a Z80-only
restart after the failed CPDCAT, and old firmware has already destroyed the
useful last BDOS error by the time HTTP was read. `ccp_idle:true` only means
CCP has become idle again after reboot. It does NOT prove the catalog ran.

**Fix committed on ESP32 feature branch only:** add lastDirectoryResult,
lastDirectoryError, lastDirectoryCheckpoint and lastDirectoryReset to the
existing heap Gate. Store final error after BDOS service and record forced
Z80 reset **before** setting RUN=false; leave sticky diagnostics unchanged
across Gate::reset while resetting normal live state. Both HTTP endpoints
now expose `last_directory_state`, `last_directory_error`,
`last_directory_checkpoint`, `last_directory_forced_z80_reset`. Native
regression simulates failed BDOS then a Z80-only cold boot and verifies
retention. These sticky values do NOT survive an ESP32 MCU power cycle.

**The underlying BDOS failure reason is not yet known.** Do not assert
that the explicit A-drive FCB, the expanded 32-entry search, or an input
race is the cause without a diagnostic result. First test from empty A>
prompt, then inspect the sticky last_directory fields. If the HP catalog
fails again, compare with the Web directory POST (current-drive selector
0) to separate explicit drive selection from common BDOS traversal.
Avoid new CP/M request until the last-directory diagnostics are recorded.
No HP/Teensy firmware changes; main and recovery untouched.

## Second physical CPDCAT result: generic cancellation/failure (2026-10-23)

After the ESP32 fetch-filter fix, the HP command no longer timed out.
With CP/M at an apparently idle prompt, the operator got
`CPDCAT: CANCELLED OR DIRECTORY READ FAILED`. The existing HP message
collapses `INPUT_NOT_EMPTY`, `FAILED`, `INVALIDATED` and other outcomes.
**Exact failure cause is unknown until the HTTP service state is read.**
Without starting a new directory request, inspect:

```sh
curl -s http://192.168.178.197/api/cpm/service/status
curl -s http://192.168.178.197/api/cpm/service/directory
```

On the updated **ESP32-only** diagnostic branch, both HTTP endpoints now
include an atomic `checkpoint` and precise `directory_error` value;
HP `CPDCAT` now prints distinct labels for nonempty input, warm boot,
and native BDOS errors. The pinned Z80 task records errors for vector
validation, DMA preflight, interrupted BDOS, invalid search slot, invalid
user/name and DMA corruption. Native regressions assert checkpoint
transitions and explicit-drive behavior. Do not conflate this with a
confirmed BDOS defect: first distinguish `cancelled_nonempty_command`
from `failed`. **All diagnostic changes are still awaiting physical
hardware acceptance**; retain existing Teensy/HP firmware, and only flash
the ESP32 after current branch-tip CI and macOS TFT build succeed.


## First hardware CPDCAT timeout and corrective fix (2026-10-23)

User reported: `CPDCAT` printed **TIMEOUT; CHECK CP/M SERVICE STATUS**.
Root cause identified by source inspection: `z80BridgeOpcode()` filtered
`service_fetch` at only four original CCP PCs (D19C/D1A4/D382/D398).
The new line-input observer at **D9EF** and BIOS CONIN poll observer at
**E826/E831** were therefore **never invoked** by the actual redcode CPU.
The synthetic CR could never be generated and the HP worker timed out.
This was a bug in the **ESP32** firmware, not evidence of a Teensy or HP
mailbox error.

Fix: forward those three precise native PCs in
`ESP32_Z80_Emulator/z80_bridge.h`, while retaining the four original
CCP locations and avoiding ordinary BIOS console-OUTPUT polling at E863.
Added native regression **testRealBridgeForwardsBdosAndBiosInputFetches**
that executes real redcode Z80 code at D9EF / E826 and checks an
injected one-shot CR; separate test ensures E863 cannot trigger it.
**Do not flash before the corrected ESP32 CI and local macOS TFT build
both pass.** HP and Teensy feature branches are unchanged. New hardware
acceptance of automatic CPDCAT remains pending.

## Source branches (do not merge or touch recovery)

- ESP32: `timob0/EBTKS-Z80-Emulator`, `feature/cpm85-file-transfer-hpib-ui`
- Teensy: `timob0/ebtks-alt-firmware`, `feature/cpm85-catalog`
- HP LOADBIN: `timob0/HP85-Option-Roms`, `feature/cpm85-catalog`
  (`src/cpm85.asm`, CPM85 **0.04c**)
- `main` and `recovery/` in every repository remain unchanged.

## User operation

On the HP-85 BASIC side, load the new standalone CPM85 module and run:

```basic
LOADBIN "CPM85"
CPVER
CPDCAT "A:"
CPDCAT "B:"
```

The HP keyboard must be in BASIC mode (press the usual **K8** to detach it
from CP/M if necessary). CP/M itself must be at a native CCP prompt, e.g.
`A>` or `B>`, not inside an application.

CPDCAT checks the requested active mount, submits mailbox opcode **7**
through the already-established 0343..0346 HP/Teensy ports and `cpm_keys`
UART control link, then waits with a bounded timeout for the ESP32 service.

The ESP32 verifies the native CCP prompt and actual BDOS buffered input
line count, checks empty real-keyboard/virtual-UART receive queues, and
synthesizes ONE blank CR **only at the native BIOS console INPUT status
instruction** (CBIOS E826 or E831). A typed, nonempty line is refused;
applications and incompatible CP/M images are refused. This completes
the EXISTING BDOS function 10 first. Only at the subsequently validated
CCP D382 prompt boundary does it run BDOS functions 17/18. No reentrant
BDOS entry, keyboard ISR waiting, HP-side artificial keystroke, or data
file write.

The service uses a 1-based explicit-drive FCB selector (1=A: .. 16=P:),
without changing the CCP's default drive, and the CP/M current user area.
Results contain up to **32 directory extents**, formatted for the HP-85
32-column CRT. Duplicate filename extents are displayed individually.
If more extents exist, the footer reads **MORE NOT SHOWN**. This is not
yet a paginated, complete directory listing on very full disks, and no
file sizes are calculated. Future improvement: paginated, deduplicated
catalogs after this architectural bridge is hardware accepted.

On the Web API, `POST /api/cpm/service/directory` also triggers automatically
on a verified empty input line. GET of the same URL returns the last
snapshot, including selected `drive`; the original Stage A `probe`
still requires the operator's blank Return.

## Mandatory build and test gates

1. Await green GitHub Actions on all three feature branches.
2. Rebuild and flash **Teensy feature branch firmware** first (adds opcode
   7; no HP I/O ISR disk/UART work).
3. Rebuild the **ESP32** firmware locally on the macOS host. Build both
   `esp32dev` and `esp32dev-tft` as relevant. Confirm successful **macOS TFT
   linking** as well as the static DRAM budget / Git SHA checks, then flash
   the ESP32 experimental firmware.
4. Assemble HP `src/cpm85.asm` with CAPASM using the HP branch. Load the
   experimental `CPM85.dat` on HP only after new Teensy and ESP32 builds
   are running; `CPVER` should show **0.04c**.
5. Before HP testing, while CP/M sits at an **empty** CCP prompt, send
   `POST /api/cpm/service/directory` and poll GET. No manual Return
   should be needed. The guest will see a harmless empty line/prompt cycle.
6. From HP BASIC run `CPDCAT "A:"`. Expect real filename extents, an
   explicit truncation footer if needed, and a normal HP BASIC input line.
   Optionally test `B:` if active. Repeat; verify CP/M `DIR`, Multiplan,
   disk operations, Web Terminal, TFT terminal and Media Manager afterwards.
7. Type a **partial command without Enter** in a Web CP/M terminal, then run
   HP `CPDCAT "A:"`. It must refuse as **CP/M BUSY OR COMMAND LINE NOT EMPTY**
   and must NOT submit or change the partial command. Finish/cancel the line
   manually. Repeat with CP/M running MODEM: refuse rather than steal input.
8. Check `GET /api/system/health` for running ESP32 Git SHA and memory
   baselines. Stop on any unexpected reset; keep the proven old firmware
   and recovery image intact.

The ESP32 `CPM85Keys` worker has only 3584 bytes of stack; the 32-entry
directory snapshot lives on the existing heap Gate and is formatted under
its short existing lock, **not copied onto the HP command worker stack**.
No new tasks or permanent static TFT DRAM arrays were added.
