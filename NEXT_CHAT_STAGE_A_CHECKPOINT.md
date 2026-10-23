# SIMON85 / EBTKS — durable handoff checkpoint (2026-10-23, Europe/Berlin)

**Read this first if the previous chat was interrupted.** This is the current source of truth for the **CP/M CCP-safe resident service, Stage A**. Earlier entries in `NEXT_CHAT_HANDOFF.md` are chronological history and may refer to older build/test states.

## What is already stable and merged

Three repositories have fully merged, hardware-accepted `main` baselines for CPM85 **v0.04b**, image creation/listing (`CPDMK` / `CPDIMGS`), disk mount/unmount/reset, HP keyboard forwarding, ANSI TFT/Web terminal, and modem:

| Repository | Last coordinated main merge commit | Merged PR |
| --- | --- | --- |
| `timob0/EBTKS-Z80-Emulator` (ESP32) | `eca19c576f68ec8a90748356aeaa4b4ac861f092` | #7 |
| `timob0/ebtks-alt-firmware` (Teensy) | `c8617c07f06c2027ade96570386adb2ec3cc2edb` | #3 |
| `timob0/HP85-Option-Roms` (HP ROM/LOADBIN) | `691154251a6852767dbec322b19168ec2d4add41` | #1 |

The user physically tested the stable image commands and new-line behavior successfully. **Do not modify, overwrite, or automatically promote the user-maintained `recovery/` working firmware**. There is no need to rebuild or flash Teensy or HP LOADBIN for Stage A.

A fresh `feature/cpm85-management-console` branch exists in all three repositories; the current Stage A development changes are **ESP32-only**. Do not merge this new feature branch into `main` until physical acceptance.

## Precisely where development stopped

Stage A implementation of a safe CCP command-input gate and a harmless emulator-bundled Z80 probe is in the ESP32 repo. It **does not implement `CPFPUT`, `CPFGET`, BDOS file writes, hot-swapping drives, or the HP management UI yet**.

- `ESP32_Z80_Emulator/cpm_safe_point.h`: verifies native CP/M 2.2 CCP signatures, tracks actual CCP prompt execution (`D382`), interactive BDOS line input (`D19C`), BDOS return (`D1A4`) and command execution (`D398`). Rejects attempts while an application is running, refuses concurrent pending operations, uses a generation counter to cancel submission races, cancels after nonempty input and on warm boot.
- `ESP32_Z80_Emulator/z80_bridge.h`: introduces a second precise pre-instruction service trap while preserving the existing `E603` warm-boot trap.
- `ESP32_Z80_Emulator/cpm_service_probe.h`: runs a **non-BDOS**, 5-byte machine-code test (`LD A,42h; CP 42h; HALT`) from an overwritten-and-restored temporary TPA region at `C000`. The existing Z80 CPU task alone saves and restores registers, PC/SP, CPU callback state and TPA bytes before CCP resumes. Never injects from a Wi-Fi or HP keyboard worker.
- `GET /api/cpm/service/status` reports `supported`, `ccp_idle`, `mode`, `probe`, `git_commit`.
- `POST /api/cpm/service/probe` accepts only if CCP is waiting for interactive input (HTTP 202); while an app runs or the service is unsupported/busy it returns HTTP 409. **The operator must then press an EMPTY Return** to leave the non-reentrant BDOS input call, causing a one-shot, safe D382 trap. Typing a nonempty command cancels the operation; the command itself runs normally.
- `tests/cpm_safe_point_native.cpp`, `tests/run_z80_core_native.sh`, and the branch-enabled CI workflow test the real redcode/Z80 core, correct refusal of non-CCP execution, cancelled requests, actual service execution and full state restoration.

### Last blocking report and resolution

On the first attempt to build Stage A at **`e2e76c33e1b284ed41d1d5aa5f4b78e37500ad08`**, the **user's Mac** Xtensa linker reported:

```text
section `.dram0.bss' will not fit in region `dram0_0_seg'
region `dram0_0_seg' overflowed by 16 bytes
```

**This was a static internal-DRAM linker allocation issue, not a runtime free-heap issue.** The initial Linux CI had passed, making a local Mac linker check essential.

The ESP32 source was fixed **without disabling functions or changing Wi-Fi, SPI, Teensy, HP ROM, partitions or recovery firmware**:

1. Removed the permanent `static CpmSafePoint::Gate` from scarce `.dram0.bss`.
2. The existing Z80 CPU worker allocates the small gate **once**, with normal `new (std::nothrow)`, and disables this optional service if allocation fails. No early allocation and no special IRAM/EXEC allocation.
3. Reuses existing `ebtksCpuBridge.device` for the gate pointer (EBTKS I/O callbacks do not use that field), and uses a nullable `service_fetch` pointer; removed the redundant permanent service context and enable flag.
4. Added `tests/check_esp32_dram_budget.py`, run **after linking the TFT firmware** in CI; maximum allowed static TFT DRAM is **124,544 bytes** (the previous hardware-accepted main baseline was **124,528 bytes**).

**Verified latest code and docs checkpoint BEFORE this handoff update:** branch source commit `82d1215b919cbbd8f9838de5db0c2ce4d795d267`, GitHub Actions **run 39862436831**, both jobs **SUCCESS**:
https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39862436831

The full CI log confirms:
```text
TFT static DRAM: 124,536 bytes (.data 28,400 + .bss 96,136);
stable 124,528, guard ceiling 124,544
PASS: TFT DRAM budget remains below known macOS overflow
PASS: both ESP32 images contain full Git build ID
82d1215b919cbbd8f9838de5db0c2ce4d795d267
```

Thus the adjusted code saves exactly **16 bytes** of permanent TFT DRAM compared with the first failed-on-Mac version (124,552 -> 124,536). **The user has NOT YET confirmed that the corrected version links on macOS or works on hardware.** GitHub CI passing does not prove either. Commits to handoff files after `82d1215b` change Git HEAD and hence the build-embedded SHA, but do not change this tested application source.

## Immediate steps for the next conversation

**First verify local compile before any OTA flashing** in the local ESP32 repository:

```bash
git fetch origin
git switch feature/cpm85-management-console
git pull --ff-only
git rev-parse HEAD
pio run -e esp32dev-tft -t clean
pio run -e esp32dev-tft
```

If there is a Mac linker error, **do not flash** and inspect its exact `.dram0` overrun. Never work around it by reducing the stable TFT/terminal/keyboard functionality, changing partition layout, or reverting all features without a reason.

If build links, keep the known-working recovery binary on hand, flash the local `.pio/build/esp32dev-tft/firmware.bin` through the established Web UI, and verify `http://<ESP32-IP>/api/system/health` embeds the SHA printed by local `git rev-parse HEAD`. Continue the non-destructive hardware acceptance outlined below:

1. At an empty CP/M `A>` prompt, GET `http://<ESP32-IP>/api/cpm/service/status`. Expect `supported=true`, `ccp_idle=true`, `mode="ccp_input"`.
2. From the Mac terminal: `curl -i -X POST http://<ESP32-IP>/api/cpm/service/probe`. Expect HTTP 202 and `accepted=true`; status is `waiting_for_empty_return`.
3. Press **blank Return** on the actual CP/M terminal. Check GET status: `probe="passed"`, and verify CCP still accepts `DIR`.
4. Arm again and instead type `DIR`: expect `cancelled_nonempty_command`, with `DIR` functioning normally.
5. Start `MODEM.COM` (or another CP/M foreground application); GET should report `ccp_idle=false`, POST should refuse with HTTP 409. Exit normally (MODEM Ctrl-X) and verify the idle indication returns.
6. Regress HP K8 keyboard, TFT and Web/Telnet CP/M consoles and ordinary mounted disk access; do not start filesystem-changing Stage B before Stage A succeeds.

**Further design after Stage A acceptance:** implement CPFPUT and CPFGET through a small firmware-bundled Z80 service that calls the existing **non-reentrant BDOS** only from a CCP safe point, initially single-file transfers without overwrite, then wildcard preflight/batch. Later allow hot mount/unmount using carefully ordered CBIOS dirty-cache flush, mount-table update, ESP32 SPI-cache invalidation and BDOS function 37 selective drive logoff. Future HP management console is paged using PRINTER85 KEY LABEL save/restore and PREV/NEXT logic: disk page, file transfer page, device mappings page. No source for these later stages has yet been implemented.

## 2026-10-23 later Media Manager retest — HTTP responsiveness still BROKEN after Web Terminal/BBS

User ran BBS via **Web Terminal** (not TFT/Telnet); Media Manager took
a long time to update, then two curl attempts to
`http://esp32_ebtks.local/api/cpm/media` failed:
`curl (52) Empty reply from server`, **HTTP 000, zero bytes**, 6.182236 s;
then `curl (28) Operation timed out after 15002 milliseconds with 0 bytes
received`, **HTTP 000**. No headers on second request.
Subsequent `python3 -m json.tool /tmp/cpm-media.json` reported JSON VALID
but the file may be **STALE from the earlier 5079-byte HTTP 200 success**
because failed curl may not overwrite `-o` output. This is not proof
that any new JSON arrived. Correct testing must remove output first.

Source diagnosis: `GET /api/cpm/media` still synchronously called
`cpmStoreBuildMediaStatusJson` in AsyncTCP's request callback, including
`cpmStoreFetchDiskDirectory` -> serialized `ebtksLinkTransaction` with
up to 8 s Teensy UART wait + heap-intensive ArduinoJson construction.
This blocking callback can stall network request processing, and in low
RAM can result in aborted HTTP responses. The exact root cause of
transport failures remains a hypothesis (AsyncTCP task occupancy plus
low memory); no ESP32 memory snapshot was supplied for this run.

**New proposed source design on this feature branch; hardware UNTESTED:**
`cpm_media_page.h`: requests now return immediate HTTP 202
`{ok:true,pending:true,retry_ms:750}` and enqueue a single listing job;
the existing `EBTKSWebTask` (`ebtks_web.h`) runs
`CpmMediaAsync::service()` and performs Teensy UART + JSON serialization
outside AsyncTCP's callback. The browser `refreshAll()` polls 202
responses until it receives HTTP 200 with the complete JSON or an
explicit HTTP 503 error. No extra worker task stack. Work is admitted
only if `heap_free >=56 KiB`, `free_8bit >=24 KiB`, and largest8
`>=16 KiB`, verified again by the worker. This conservatively refuses
Media Manager refresh during modem/Telnet low-memory loads, protecting
network operations at cost of unavailable listing under high load.
At completion the worker owns an `AsyncResponseStream` and atomically
transfers it to the next HTTP GET; an unclaimed response is deleted
after 15 s to avoid permanently retaining ~5 KiB. No direct storage
operation or dynamic media response tree in the HTTP callback.
Existing GET route behavior changes: a one-shot curl may see HTTP 202;
the browser handles it automatically. For manual testing, poll GET until
the status becomes HTTP 200 or 503 (remove old /tmp output before each
curl); verify JSON only if that request produced bytes.

Important follow-up: while the background worker spends ~6–8 s in the
Teensy query, it cannot itself accept NEW Telnet sessions or service
the legacy Teensy browser bridge, but AsyncTCP HTTP callbacks and
active Telnet ConsoleTask remain schedulable. This is a stability
trade-off and should be profiled after basic hardware validation.
Check the macOS TFT link and CI static DRAM guard before flashing;
no baseline merge; no Teensy/HP/recovery changes.

---
## 2026-10-23 17:20 Europe/Berlin — new Media Manager JSON response hardware PASS (load not yet verified)

User pulled, built and flashed the new Media Manager streamed-response fix.
On physical ESP32 at `http://esp32_ebtks.local/api/cpm/media`, the same
read-only Mac curl diagnostic now returned **HTTP 200**,
`Content-Length: 5079`, downloaded **5079 bytes** in **6.146087 s**,
`Content-Type: application/json`, `Cache-Control: no-store`, and
`python3 -m json.tool` reported **JSON VALID**. This is a decisive
improvement from pre-fix HTTP 200 + 0 bytes. Exact firmware SHA was not
printed in this report. This single test does NOT state that modem/Telnet
were active; do not claim loaded-system acceptance.

**Next test:** verify the browser Media Manager actually populates
drives/media. Then repeat the same curl + Python JSON validation
after one ordinary modem session alongside Telnet, capturing
`/api/system/memory` before and after, especially `free_8bit`,
`largest_8bit`, `min_free_8bit`, and `modem_state`. Under overload
accept an explicit JSON 503, but never a 200/zero-body response.
Keep Stage A wider regression open, no merge yet. Response latency
~6.15 s is still unexplained and may be caused by synchronous
Teensy directory transaction in the AsyncWebServer callback;
investigate separately once correctness and stability are proven.

---
## 2026-10-23 Media Manager root-cause evidence and bounded-memory fix (hardware UNTESTED)

User tested `curl -sS --max-time 15 -D ... -o ... -w ...
http://esp32_ebtks.local/api/cpm/media` AFTER two BBS sessions.
Response: **HTTP 200**, **Content-Length: 0**, **zero body bytes**,
completed in **5.387394 seconds**. Python `json.tool` rejected the empty
body (`Expecting value: line 1 column 1`). This conclusively explains the
Media Manager's blank tables, but does not alone distinguish which of
the two string allocations failed. Device still otherwise responsive.

Source inspection of pinned ESPAsyncWebServer **3.6.0** shows
`AsyncBasicResponse::AsyncBasicResponse(...,const char* content)`
does `_content = content;` (another large dynamic String copy). In
`cpm_media_page.h`, `request->send(200,"application/json",reply)`
would use that constructor even if body construction succeeded;
allocation failure can yield an HTTP 200 with `Content-Length: 0`.
Meanwhile `cpmStoreBuildMediaStatusJson` built a large JsonDocument
plus Teensy SD JsonDocument then serialized into a String, and returned
`true` unconditionally without checking overflow, write count, or
String allocation. Under the 268-byte historic 8-bit minimum this
is a credible explanation of the observed empty body. The exact
individual failing allocation is unconfirmed.

**Source fix on feature branch, not yet physically verified:**
`cpmStoreBuildMediaStatusJson(Print &out)` directly serializes the
JSON response to a provided stream, scopes the Teensy directory
JsonDocument so it is freed before output, checks
`result.overflowed()`, `measureJson(result)`, and that the
number of bytes emitted matches exactly. `cpm_media_page.h` now
uses `AsyncResponseStream` (initial 512 B buffer) so no extra body
copy through `AsyncBasicResponse`; if a low-heap preflight fails
(free8 <12 KiB or largest8 <8 KiB), or the JSON writer fails,
returns an explicit HTTP 503 JSON error instead of HTTP 200 empty
body. Native static regression tests protect against regression.
**Remaining risk**: `/api/cpm/media` still calls the Teensy SD query
with up to 8 s timeout from the async HTTP callback. This is not
addressed in the small fix and deserves architectural follow-up if
latency/availability remain problematic.

**Next**: check full CI including TFT static DRAM budget, require
Mac TFT link before OTA; verify SHA and retest `/api/cpm/media`
with curl `HTTP`, `bytes`, `time`, and `python3 -m json.tool` first
at idle, then after an ordinary MODEM session (avoid forced stress).
Expect either valid nonempty HTTP 200 JSON or explicit 503 JSON
rather than HTTP 200/0. Keep accepted recovery firmware untouched;
no Stage A merge until hardware regression accepted.

---
## 2026-10-23 hardware retest after bounded Telnet TX fix — TWO BBS sessions, no freeze, UI degraded

User reflashed the subsequent firmware with 2 s bounded Telnet TX writes,
then cold booted and tested DIR/Multiplan, Telnet, MODEM and two BBS sessions.
Git SHA of actually flashed binary was **not supplied**; do not assume
HEAD just from successful build. HP/TFT/CP/M and Telnet stayed responsive
through both sessions, so the prior total freeze did not recur in this run.
No hardware proof yet attributes this improvement specifically to the
Telnet timeout versus changed allocation timing or other factors.

| Phase | heap_free | free_8bit | largest_8bit | min_free_8bit |
| --- | ---: | ---: | ---: | ---: |
| Boot + CP/M DIR/Multiplan | 66,816 | 27,640 | 25,588 | 23,960 |
| Telnet connected | 64,224 | 25,048 | 24,564 | 20,240 |
| DIR and Multiplan via Telnet | 64,232 | 25,056 | 24,564 | 13,036 |
| MODEM initialized, no dial | 55,404 | 16,228 | 13,300 | 13,036 |
| BBS session #1 active | 54,100 | 14,924 | 13,300 | 1,564 |
| After BBS #1 | 56,380 | 17,204 | 13,300 | **268** |
| BBS #2 active | 54,176 | 15,000 | 13,300 | **268** |
| After BBS #2 | 55,888 | 16,712 | 13,300 | **268** |
| After Web UI browsing | 55,956 | 16,780 | 13,300 | **268** |

Telnet remained connected (`telnet_connected=1`) and modem worker
initialized (`modem_state=2`) throughout both BBS sessions. Modem
refusal counter stayed 0. Web Terminal correctly returned HTTP 503
low-8-bit-RAM warning after modem initialization. BBS sessions were
completed and the CP/M/TFT and network stayed responsive.

At BBS #2 hangup the captured `tcp_closed` stage had
free_8bit=11180, largest_8bit=5876, versus current free_8bit=16712
and largest_8bit=13300 after the session: a significant *transient*
post-close memory pressure, not proof of retained leak. At #1 close
the capture showed free_8bit=14428, largest_8bit=11764. The cumulative
268 B minimum occurred by the end of #1 and remained unchanged during #2.
Modem startup consumes roughly 8–11 KiB measured at these checkpoints.
Console task stack unused declined as low as 932 bytes (not yet
a stack overflow or confirmed liveness issue).

**Outstanding issue:** HP CRT remote display and simple Web API endpoints
remain responsive but CP/M Media Manager UI stays at `Loading...`,
drive/media tables blank, with browser error `The string did not match
the expected pattern.` The error is not proven to be a JSON parse error;
it could reflect network/fetch failure. In current source the
`/api/cpm/media` handler calls `cpmStoreBuildMediaStatusJson(reply)`
**synchronously in AsyncWebServer callback**, constructs two
`ArduinoJson::JsonDocument` allocations (`result` containing all
16 drives and media list, plus Teensy SD `listing`), serializes
into a dynamic `String`, and then forms an HTTP response. It calls
`cpmStoreFetchDiskDirectory` (Teensy UART with up to 8 s timeout).
This is an inherently high peak-memory/blocking path in the same
resource-stressed AsyncTCP environment; **hypothesis**, exact
failure not yet proven. No SD/NVS corruption indicated.

**Next priority:** inspect the *actual HTTP response* without triggering
a mount change. From Mac request
`curl -i --max-time 15 http://<ESP32-IP>/api/cpm/media`
and if possible save to file / validate JSON. If 200 malformed or
connection prematurely closed, focus on allocation/response limits.
If 500 or 503, use status details. If curl JSON is valid, investigate
Safari fetch handling/URL rather than assuming ESP32 JSON failure.
Keep Stage A general regression OPEN, no merge yet. Do not repeat
unsafe combined load just to force another freeze.

Potential engineering response (not implemented): preflight /api/cpm/media
heap guard with explicit JSON 503, catch ArduinoJson overflowed()
and String reserve/serialize failure, make Media Manager show an
unambiguous recoverable status; longer-term reduce peak JSON
allocation and avoid 8 s blocking UART transaction from AsyncTCP
callback. Compare the planned 3 firmware flavours after this.

---
## 2026-10-23 further hardware observation — CPRESET did not recover hang

User adds that, during the same network/console freeze reported below,
the HP `CPRESET` command returned `Z80 RESET REQUESTED`, but there was
no observable Z80 restart; the unit required power-off. This is an
important distinction between a management ACK and actual CPU reset.
`cpm85_drive_control.h::execute(op=4)` calls
`cpmStoreVerifyDesired()`, sets `RUN=false`, then immediately returns
`Z80 RESET REQUESTED` without synchronously waiting for CPU-task ACK.
The CPU task only stops after it returns from `z80BridgeRun`; it can
still be blocked in an I/O callback, console ring pressure, or other
work while `RUN` has been set false. The UI ACK alone does NOT establish
that a Z80 reset took place. The simultaneous HTTP unresponsiveness
also leaves network starvation/heap exhaustion as an open possibility.

After this report, checked GitHub Actions run 39934832703 for source at
`b0160b72926f97025a7598795ca437dd235b44f1`: both native and
ESP32/headless/TFT build jobs completed **SUCCESS**, including
static TFT DRAM check. This proves compile/static tests, NOT hardware
liveness or recovery. The next untested code includes only bounded
Telnet TX loop (2s per 256-byte chunk) plus the already-tested Web
Terminal admission guard. User powered off unit and will flash only
once a new version is ready. Preserve `recovery/` and do not merge.

Priority when retesting: Mac link, verify embedded SHA, begin with
cold boot/CP/M CCP & CPRESET normal-operation check, then a single
Telnet/large-output session. Avoid reproducing repeated modem+Web
stress before isolating the deadlock and establishing low-heap safe
admission. If another freeze appears, record pre-failure memory,
uptime and reset reason when obtainable; don't infer a hardware
reset reason where power-off was needed.

---
## 2026-10-23 11:47 Europe/Berlin — physical combined-network regression FAILED

User tested the newly committed Web Terminal low-heap admission guard (post
`c70b55d`, with native/TFT GitHub CI passing). Exact running SHA was not
supplied for this test, so do not assert one. Test A: after cold boot,
MODEM active, Telnet connected: heap_free=56884, free_8bit=17708,
largest_8bit=10740, min_free_8bit=2548, modem_state=2.
Opening `/z80` correctly produced HTTP 503 text:
`Web Terminal unavailable: ESP32 8-bit RAM is low. Exit MODEM or close
other sessions, then reload.` This confirms the *page guard* works.

Test B: cold boot, Web Terminal connected first: heap_free=63760,
free_8bit=24584, largest_8bit=20468, min_free_8bit=5952, modem_state=0.
Starting MODEM and connecting to a BBS dropped the Web Terminal mid-banner
with its bounded-retry error message. At uptime 89036:
heap_free=55520, free_8bit=16344, largest_8bit=10740,
min_free_8bit=3996, modem_state=2. BBS session continued successfully
on physical TFT, CCP returned, Multiplan launched successfully.
At uptime 345135 after BBS session: heap_free=57620,
free_8bit=18444, largest_8bit=13300, **min_free_8bit=388 bytes**
(historical low water!), modem_state=2 (worker persisted, no proof of leak).
Opening `/z80` was safely refused with HTTP 503.

After that the user connected via Telnet and launched a second MODEM
session, which **froze CP/M and made all Web pages including the memory
API unreachable**; disconnecting Telnet did not recover; hardware reboot
was necessary. Do NOT mark Stage A as generally accepted and do NOT merge.
No evidence yet identifies whether the final hang was a network OOM,
blocking Telnet write, task/watchdog issue, or another interaction.
Original modem dial threshold (heap >=49152, largest8 >=12288) has only
~1 KiB margin vs 13300-byte largest block in post-BBS snapshot; actual
values at the *second dial attempt* are unknown. Do not relax guards.

**Source inspection:** `serial.h::serialTask` has an unbounded
`while(sent<n && serverClient.connected())` loop and keeps retrying
`WiFiClient::write()` after zero-byte writes without a deadline.
A congested-but-reported-connected Telnet stream could block console
routing indefinitely; this is a plausible secondary liveness flaw, NOT
by itself proof of whole HTTP stack deadlock. `ebtks_modem.h::worker`
retains 512-byte net TX, 256-byte response FIFO, virtual 8250, Hayes
parser, socket and 5120-byte modem FreeRTOS task after hangup.
Web Terminal handshake guard cannot prevent memory dips from an
*already connected* client amid BBS bursts. `min_free_8bit` is
a cumulative minimum and should not be confused with current free.

**Follow-up source change, hardware UNTESTED:** Added a bounded 2-second
retry window around ConsoleTask Telnet output in `serial.h`; if a
connected TCP client fails to accept a 256-byte chunk within that window,
only its Telnet connection is closed so the console task can continue
forwarding output to TFT and Web Terminal and servicing HP input.
No modem dial thresholds or TCP API were changed; separate static
regression guard added to `tests/check_web_ui_cleanup.py`.
This may prevent one plausible ConsoleTask hang but does **not** prove or
repair the independent HTTP/server freeze due to the 388-byte historical
8-bit heap minimum. Await CI, Mac TFT link, and careful physical testing.
**Next:** Freeze feature merge and keep known-good recovery image;
isolate Telnet write boundedness and dynamic socket memory pressure.
Prefer a small separately verifiable liveness fix and safer dial
admission, with native+TFT DRAM CI and local Mac link before OTA.
A/B run without Web Terminal, then with Telnet, to isolate repeated
MODEM TCP session pressure only after protective fixes. Subsequently
the user's agreed roadmap is three PlatformIO firmware environments:
(1) TFT excluding Web Terminal, (2) headless with Web Terminal,
(3) full combination. After regression stability, complete Stage B
CPFPUT/CPFGET and HP management UI, then a fourth optional Tang Nano
FPGA display backend. Do not modify Teensy/HP or recovery binaries.

---
## 2026-10-23 hardware follow-up — safe point PASS, Web/Modem RAM regression remains

ESP32 `2fd4f856d9aeb95b8468183caad233e7b6735208` ran on physical HP-85/EBTKS/TFT hardware.
Stage A native CCP safe-point accepted a POST with HTTP 202; pressing an empty
Return yielded `probe=passed`. Arming then typing `DIR` yielded
`cancelled_nonempty_command`. While `MODEM.COM` ran, `mode=busy`,
`ccp_idle=false`, and POST refused with HTTP 409 / `accepted=false`.
Exiting MODEM with Ctrl-X restored `ccp_idle=true`, `mode=ccp_input`.
Thus Stage A safe-point functionality PASSED hardware tests, but the wider
system regression has NOT passed and the branch must NOT merge.

User observed that opening the Web Terminal sometimes did not respond;
reopening it could freeze the ESP32 and disconnect Telnet. After reboot:

| State | heap_free | free_8bit | largest_8bit |
| --- | ---: | ---: | ---: |
| Cold boot | 64,740 | 25,564 | 24,564 |
| Telnet connected | 62,956 | 23,780 | 16,372 |
| MODEM initialized | 55,476 | 16,300 | 11,252 |
| Later after CPDIMGS | 54,552 | 15,376 | 11,252 |

`min_free_8bit=1932` was a historical minimum, not current free RAM.
Modem refuses dial because `largest_8bit=11252` falls below its existing
`12288`-byte dial guard, even though `heap_free` exceeds 49,152.
The user suspects reversed service startup order, which is plausible but
not yet isolated as the definitive cause. Media Manager also failed
to populate once with a browser parsing error in this low-memory state;
do not infer corruption of SD/NVS from this message.

**Remediation implemented on this feature branch after 2fd4f856:**
`web_terminal_v2.h` rejects a new Web Terminal page with descriptive HTTP
503 when total free heap is <56 KiB or largest free 8-bit block <14 KiB;
the pinned ESPAsyncWebServer 3.6.0 `handleHandshake` hook applies the same
guard *before* client allocation for direct WebSocket upgrades (its own
reject response is HTTP 401). The browser performs at most three sequential
connect attempts with 5/10-second backoff, then stops until manual Reconnect.
New static/browser tests enforce these constraints. Existing modem startup
and dial thresholds, 10-second WebSocket console flow-control timeout,
partitioning, CP/M, Teensy/HP LOADBIN, and `recovery/` are unchanged.
Thresholds are conservative initial values based on supplied hardware
snapshots and require explicit hardware acceptance. Do not describe this as
proven crash resolution before both startup orders are retested.

**Next steps:** Wait for latest branch CI native+TFT success and `check_esp32_dram_budget.py`;
then do a **local Mac `pio run -e esp32dev-tft -t clean && pio run -e esp32dev-tft`**
before OTA. Verify running SHA from `/api/system/health`. Test modem-first
then `/z80` (expect safe HTTP 503 or safe WebSocket rejection, not crash),
and terminal-first then modem (expect modem dial guard if low). Reboot and
check Web/Telnet/TFT responsiveness, `DIR`, `CPDIMGS`, `/cpmmedia`, actual
dial if sufficient RAM; collect `/api/system/memory` after each step.
Do not merge until overall regression is accepted.

---
## Related documents

- `NEXT_CHAT_QUICKSTART.md` — condensed historical development guide
- `NEXT_CHAT_HANDOFF.md` — detailed chronological record (very long)
- `CPM85_SAFEPOINT_SERVICE.md` — Stage A architecture and hardware tests
- `ESP32_RECOVERY_USB.md` — recovery when Web updater is unavailable
- `CPM85_DRIVE_CONTROL.md` — accepted v0.04b image/drive integration

**Important:** All merge/main activity for the previously accepted drive-control feature is complete; Stage A stays on its own `feature/cpm85-management-console` branch. The user is waiting to report the local Mac build result.
