# Stage B2 — CP/M live-file transfer via validated CCP safe point

**2026-10-25 priority pivot, design only. NO firmware changes or untested image writes.**
The user prefers to implement file transfers first, deferring the source-independent HP
management console and failed RMIDLE investigation. New executable work should
branch from hardware-accepted **main** (Stage B1 CPDCAT); keep rejected HP
C2/C2b console code out of the file-transfer line.

## Accepted foundation

- Stage B1 `CPDCAT` is accepted and merged on all three repositories;
  automatic empty-Return CCP safe point and native BDOS Search First/Next
  17/18 run in the pinned ESP32 Z80 task at the verified D382h CCP point.
- CP/M DIR, Multiplan, Media Manager, HP BASIC keyboard coexistence, and
  mounted-drive behavior were hardware regression tested.
- `ESP32_Z80_Emulator/cpm_directory_service.h` already provides bounded
  Z80 BDOS subcalls, saved/restored CPU state, C000-C0FF scratch,
  0080-00FF DMA, gate cancellation and error classification.
- `ebtks_storage.h` already offers Teensy SD read/write chunks over
  existing UART/JSON/Base64. General SD file *creation/truncation*
  requires careful review; the current open helper explicitly supplies
  O_RDWR for writable access, not a creating-open flag.
- The shared file-service design lives in `CPM_FILE_API_HP_BRIDGE_DESIGN.md`.

## Proposed order

**B2a: read one CP/M file safely (read-only).** New ESP32-side
`CpmFileService`, reusing the CCP input gate, verifies native BDOS
vectors, selects a bounded normalized drive/user/8.3 FCB, calls
BDOS **15 OPEN**, **20 READ SEQUENTIAL** (128-byte record into
0080 DMA), **16 CLOSE**. Observe EOF and explicit BDOS error values.
Preserve/restorable Z80 registers, touched scratch, FCB, stack
and DMA, with BDOS internal effects called out explicitly. The
read may require a session object in ESP32 memory for FCB state;
do not simply restore an FCB after each record and lose its current
record/extent. No mutating CP/M operations yet. Expose a
*headless internal* API `readFile(target,offset/chunk)` and a
read-only diagnostic endpoint or host-side test so results can be
validated without adding HP BASIC commands immediately.

**B2b: end-to-end export to Teensy SD staging.** Move the returned
128-byte records in bounded chunks using existing proven transports,
starting with small files (one record, several records, multiple
extents). Save into an allowlisted staging namespace, not an existing
mounted disk image and never as an arbitrary unsanitized path.
Add create/truncate semantics only after proving their actual Teensy
file-service flags; use a temporary name plus explicit close/CRC
and rename/publish only if supported and proven. Avoid large
buffers, 8 MiB image downloads and the previously rejected
one-response AsyncTCP streaming path. CP/M file sizes are
in **128-byte logical records**; exact original byte length is
unknown without sidecar metadata. Do not discard 0x1A/0x00
from binary data.

**B2c: import safely onto a mounted CP/M drive.** This is separately
gated because BDOS 22 MAKE, 21 WRITE SEQUENTIAL and 16 CLOSE
**change the live filesystem**. Confirm the target does not
exist; do not delete/overwrite during the first tests.
Check every disk-full/write error; ensure FCB state, DMA and
BDOS callback completion across bounded batches. Interrupted
transfer may leave a partial named CP/M file: define cleanup,
completion marker and status policy; do not call an ordinary
BDOS sequence fully transactional without proof. Disallow the
operation unless CCP is at the verified safe point and drive is
not write-protected. Back up writable images before testing.

**B2d: HP-85 LOADBIN `CPMCP` and external HP-IB file endpoint.**
Use the same typed file API to transfer A:–P: files to/from the
configured HP mass-storage/peripheral through HP-native OS
file commands and existing Teensy gateway. The HP-85 endpoint
may use the user's proposed `D330` syntax, but its parser and
native file-mode semantics require validation. Do not make
ESP32 directly control HP-IB. Begin with one binary file and
verify readback on both sides. Later add deletion `CPMPURGE`
as a separately confirmed destructive operation and richer
`CPMCAT` pagination/extent aggregation.

## Safety and memory

- Keep installed `CPM`, `CPDCAT`, Z80 warm boot, keyboard,
  modems, cache, disk UART-write transport, SD files and TFT/Web
  accepted paths unchanged; **no console hooks, RMIDLE or timers**.
- Fixed-length 128-byte DMA records and modest bounded transfer
  buffers; no extra large ESP32 DRAM statics or long-lived request
  JSON documents. Existing ESP32 TFT memory is constrained.
- All Z80 BDOS service execution is on the Z80 CPU task at
  verified CCP D382h, not in Wi-Fi/AsyncTCP callbacks or Teensy
  HP-bus IRQ context. No BDOS call during a foreground CP/M app.
- A safe-point user request may complete an automatic empty CR,
  but a nonempty command/CCP invalidation/warm boot cancels it.
  Need an explicit transaction/session ownership and timeout
  policy before transmitting long files.
- Distinguish `record count`, `record bytes`, true host byte
  length (if known), and transport bytes; report CRC, result
  and partial errors.
- Protect against overlapping transfers and repeat requests,
  preserve per-request generation and final results, and
  require a Z80-only recovery if BDOS is interrupted mid-call.
- Do not mutate a mounted .DSK's raw sectors from ESP32/Teensy;
  drive operations must go through CP/M BDOS.
- First B2a can be **ESP32-only**. Teensy revisions are required
  only if SD staging creation/commit needs support; HP LOADBIN
  changes are deferred to B2d. Do not merge to main without
  successful physical acceptance and user approval.

## Suggested B2a acceptance

Use a known small CP/M file and an A> prompt. Check:
1. File absent → OPEN returns normal not-found, no guest reset.
2. One-record file → exactly one 128-byte payload, CRC matches
   a CP/M-side binary reference, normal DIR still works.
3. Multi-record/multi-extent file → correct record order, EOF,
   no duplicate/omitted records, no memory corruption.
4. Nonempty CCP command or MODEM/MULTIPLAN running → reject/cancel,
   don't defer a request into a future unrelated application exit.
5. Session abort/timeout, unsupported CCP signature, wrong DMA →
   explicit failure and clean prompt/recovery; no mounted disk edits.
6. CPDCAT, CPSTATUS, CP/M DIR, Multiplan, Web/TFT/Telnet and
   Media Manager regressions, system memory watermarks checked.

**Historical note:** legacy `CPM Source/sdcopy.asm` uses old
virtual SD commands and **deletes the destination before create**;
do NOT reuse it verbatim as an initial safe import. Keep its
noncommercial license/attribution if adapting it later.
