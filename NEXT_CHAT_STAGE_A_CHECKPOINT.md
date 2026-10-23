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

## 2026-10-23 gzip-enabled `/z80` coldboot curl hardware experiment

User built/flashed firmware with build-time gzip Home/Updater/CRT/Media
Manager/Web Terminal, Home Page Z80 link fix, and early IRAM SPI read
cache; tested `esp32dev-tft-no-telnet` on real ESP32.
MODEM state=0, browser Web Terminal not connected.

On cold boot at uptime=48198 ms:
heap_free=67872, free_8bit=28696, largest_8bit=26612,
min_heap_free=61624, min_free_8bit=22448, modem_state=0.
Then ran curl HTTP-only gzip-request:

```sh
curl -sS --compressed -D - -o /dev/null http://192.168.178.197/z80
```

Received HTTP/1.1 200 OK; `Content-Encoding: gzip`,
`Vary: Accept-Encoding`, `Cache-Control: no-store`,
`content-length: 6969`, `text/html; charset=utf-8`.
Thus compression and HTTP negotiation confirmed on physical device.

After request at uptime=80458 ms: heap_free=68904,
free_8bit=29728, largest_8bit=27636,
min_heap_free=50212, min_free_8bit=11036, modem_state=0.
Current free8 actually increased by 1032B across snapshots;
no persistent allocation by gzip page fetch. Historical minimum
fell **11412B** during the interval, so HTTP handling still
imposes a significant transient pressure even for 6969B body.

Compare earlier fresh-boot uncompressed HTML-only test:
start min_free8=23948, final min_free8=9724,
drop **14224B**. New gzip min delta **11412B**:
observed improvement of **2812B (~20%)** in additional
low-water drop despite compressed body shrinking by 62.5%.
Different boot states/timing mean this is not a perfectly
controlled attribution of all allocation differences.
Post-request minimum itself is 1312B better than uncompressed
(11036 vs 9724), but new pre-request minimum started 1500B
lower (22448 vs 23948).

**Next gates:** Confirm Home Page proper button visually and
click `/z80`; browse `/cpmmedia` and `/crt` to test full
UI functionality. Measure gzip headers/body and min_free8
for Media Manager/CRT separately; reboot between tests
because low-water marks are lifetime cumulative.
If pursuing another optimization, make a separate small
bounded **8-bit accessible** HTTP response staging experiment
(not MALLOC_CAP_32BIT-only response byte buffers; AsyncTCP/lwIP
need byte addressing), and test with same gzip-only curl
protocol. No proof yet that WebSocket or modem concurrently
safe under low 8bit reserve.

No branch merge or source modification from this hardware result.

---
## 2026-10-23: Static HTML gzip + Home Page Z80 button repair

User requested build-time compression of **all embedded static Web pages**,
especially CP/M Media Manager, and repair of Home Z80 Terminal button
which displayed the broken raw-string macro as literal text.
Follow-up user asked whether ESPAsyncWebServer could allocate its
HTTP response output buffer in **32-bit-only RAM**. Answer: not
safely as drop-in replacement because the library passes the
byte-oriented buffer to AsyncTCP/lwIP, which expects 8-bit
accessible data; IRAM32 requires aligned 32-bit data access.
Using MALLOC_CAP_32BIT | MALLOC_CAP_8BIT would still consume
the same scarce 8-bit pool. Prefer gzip and, if needed later,
a bounded-size 8-bit DRAM response implementation.

**Feature branch implementation** (no main/recovery/Teensy/HP changes):

- Corrected `EBTKS_HOME_FALLBACK` adjacent C++ raw-string
  expression to use `)HTML\" EBTKS_HOME_Z80_LINK R\"HTML(`.
  This expands the actual Z80 link button in browser-enabled
  builds, or appropriate Telnet/TFT text in other variants.
- New build pre-script `tools/pio_gzip_pages.py` generates a
  `.pio/build/<env>/ebtks_static_pages_gzip.h` with gzip-compressed
  bytes in PROGMEM for **Home**, **Updater fallback**,
  **HP CRT**, **CP/M Media Manager**, **Z80 browser terminal**.
  Home gzip is correctly conditional for Web/Telnet/TFT targets.
  HTML markup and JS remain source-controlled/editable as
  the original C++ raw literals; no runtime compressor.
- New `ESP32_Z80_Emulator/ebtks_static_pages.h` serves
  compressed flash bytes when HTTP `Accept-Encoding` allows
  gzip, with `Content-Encoding:gzip` and
  `Vary:Accept-Encoding`, otherwise preserves old identity
  source as a compatibility fallback. `Cache-Control:no-store`
  remains. OTA POST, SPIFFS updater override, JSON API and
  `/ws` and `/z80ws` WebSockets left unchanged.
- HTML routes `/`, `/updater` fallback, `/crt`, `/cpmmedia`,
  `/z80` use shared gzip/identity helper; keep existing
  Web Terminal low-memory handshake and HTTP guards.
- New native regression `tests/check_html_gzip.py` runs in
  CI, checks exact gzip roundtrip, all 3 Home variants,
  corrected button and expected page APIs; other static
  source tests updated. All 4 firmware static DRAM budgets
  still stay below conservative 124544B ceiling in first
  green CI run (new no-Telnet variant remained 124464B).

Actual measured HTML gzip sizes from CI native test:

| Page | Raw bytes | Gzip bytes |
| --- | ---: | ---: |
| Home Web | 936 | 546 |
| Home Telnet | 934 | 558 |
| Home TFT | 916 | 547 |
| Updater fallback | 694 | 455 |
| HP CRT | 10357 | 3680 |
| Media Manager | 6841 | 2756 |
| Z80 Terminal | 18581 | 6969 |

**Crucial hardware A/B:** after flashing and coldboot, use
`curl -sS --compressed -D - -o /dev/null
http://192.168.178.197/z80` to request actual gzip.
Old `curl -sS -o /dev/null URL` by default sends no
`Accept-Encoding:gzip`, thus intentionally takes uncompressed
identity path and may reproduce the former ~14KiB trough.
Check `Content-Encoding: gzip` and `Vary: Accept-Encoding`,
then `/api/system/memory` lifetime minimum. Repeat optionally
for `/cpmmedia` and `/crt`; verify Home Page Z80 button
navigates, Media Manager listing, CRT, WebTerminal rendering
and HP/TFT, updater page accessibility. No BBS+active WebSocket
concurrency until transient memory safety confirmed.

**CI status at note time:** completed run 40011296447 green
(4 ESP32 builds, DRAM guard, native, feature checks),
source SHA a069c70bfdf7558245d199816a247d49d6b4bab2.
A minor subsequent correctness improvement to gzip
`Accept-Encoding` q=0 negotiation is at 540eea3...,
plus docs. Before requesting user flash, verify the
**LATEST branch head GitHub Actions green**. No user Mac
build or physical test of gzip yet.

---
## 2026-10-23 clean coldboot: HTTP-only `/z80` GET reproduces ~14KiB 8bit heap trough

User rebooted early-IRAM TFT+Web/no-Telnet firmware.
With MODEM never initialized (`modem_state=0`), captured memory
at 36695ms, then used Mac curl `curl -sS -o /dev/null
http://192.168.178.197/z80` to retrieve HTML with **no JS
execution, WebSocket upgrade or BBS TCP connection**, then
memory endpoint again at 68441ms.

| Measurement | Cold boot | After HTTP GET /z80 |
| --- | ---: | ---: |
| heap_free | 68656 | 68656 |
| free_8bit | 29480 | 29480 |
| largest_8bit | 25588 | 25588 |
| min_heap_free | 63124 | **48900** |
| min_free_8bit | 23948 | **9724** |
| modem_state | 0 | 0 |

**Strong isolated finding:** HTML-only HTTP GET introduces
at least **14224B additional transient 8bit heap pressure**,
with **0B retained current heap cost** after request completes.
Very similar to prior fresh-boot full browser Web Terminal
causing 13016B drop (min8 22172->9156). Thus merely serving
the HTML page can explain most/all of earlier transient trough;
WebSocket is NOT required. Modem is NOT required either.
Min values are lifetime lowest; do not assume any one malloc
exactly equals 14224B, or that WebSocket cannot also add peaks.

**Root-cause source inspection (ESP32Async/ESPAsyncWebServer
pinned at 3.6.0, GitHub tag v3.6.0):**
`src/WebRequest.cpp:755-756`: `beginResponse(int,const char*,
const uint8_t*,size_t,..)` creates `AsyncProgmemResponse`;
`src/WebResponses.cpp:775-791`: PROGMEM bytes copied via
`memcpy_P` into a response staging buffer.
`src/WebResponses.cpp:348-469` specifically
`AsyncAbstractResponse::_ack` picks `outLen` based on
TCP `client()->space()` and remaining response length
(lines 369-410), invokes **`malloc(outLen + headLen)`**
(line 413), fills PROGMEM content and writes to
`client()->write` (lines 439-454), then `free(buf)`
(line 464). This gives a direct plausible cause of a
large *temporary* DRAM allocation when serving the
~17KiB browser terminal page, without constructing a
persistent heap String. Other AsyncTCP/lwIP allocations
may overlap; exact allocation trace still unmeasured.

**Most promising targeted code experiment** (not yet
implemented or requested): gzip-compress terminal HTML
at development/build time, store compressed bytes in
flash/PROGMEM, serve with `Content-Encoding: gzip`,
correct Content-Length and type text/html. This caps
this AsyncWebServer response's staging allocation to
compressed bytes (potentially far below ~17KiB),
reduces TCP payload and may improve low-water headroom.
A runtime gzip compressor would be counterproductive;
do NOT compress at runtime. Verify Content-Encoding
works with browser, no extra `.dram0.bss`, `curl -i`
headers, content rendering, WebSocket handshake after
page load, and same coldboot HTTP-only free8 minima.
An alternative is a bounded-size custom response or
switching to filesystem serving if proven to use smaller
chunks; default `beginChunkedResponse` in same library
also mallocs outLen based on TCP space, so is NOT
automatically a bounded-memory fix.

All previous BBS, physical TFT, Web Terminal, disk
and OTA paths remain as before. No firmware change
or main/recovery merge as part of these observations.

---
## 2026-10-23 clean coldboot / browser terminal / leave: isolated transient heap use without MODEM

User ran new clean-boot A/B on early IRAM-only
`esp32dev-tft-no-telnet` firmware, MODEM never started
(`modem_state=0` in all snapshots), Telnet disabled,
Web Terminal+TFT compiled in:

| Phase | uptime_ms | heap_free | free_8bit | largest_8bit | min_heap_free | min_free_8bit |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CP/M cold boot | 22482 | 69100 | 29924 | 28660 | 61348 | 22172 |
| Browser `/z80` open/in use | 63052 | 66816 | 27640 | 25588 | 48332 | **9156** |
| Navigated away | 95399 | 68712 | 29536 | 27636 | 48332 | **9156** |

Web Terminal session steady delta from boot: -2284B in free8;
after browser leaves, +1896B free8 is reclaimed,
leaving just -388B from initial coldboot and -1024B in
largest8. No substantial sustained heap leak detected in
this single browser cycle.

**Critical evidence**: lifetime min_free8 fell **13016B**
from 22172B to 9156B after Web Terminal opened and used.
min_heap_free fell exactly 13016B. This happens with
`modem_state=0` and no previous BBS connection, so the
earlier BBS session is NOT a prerequisite for transient
pressure. The 2436B minimum in the prior WebTerminal-after-BBS
test is plausibly combined resident MODEM heap plus similar
browser load, but timing, workloads differ: do not attribute
precise cause to one allocation or claim exact 13KiB websocket
allocation. Current firmware serves `/z80` terminal HTML directly
from PROGMEM (~17KiB, no intermediary heap String), and
WebSocket handshake occurs on `/z80ws`; TCP send buffering
and AsyncWebSocket's per-frame queue are plausible contributors.
Min values are whole-system cumulative, so not event-specific.

Source review: `web_terminal_v2.h`
`webTerminalRegister()` uses `beginResponse(200, ... const
uint8_t* EBTKS_TERM_PAGE, length)` for HTML, not String,
and memory guards HTTP GET and websocket upgrade;
`webTerminalWrite()` queues websocket binary frames and
waits for queue capacity, `z80WebSocket.cleanupClients()`
is run periodically by web worker in `ebtks_web.h`.

Suggested **code-free next isolation** if desired: cold boot,
capture `/api/system/memory`, then retrieve page using
`curl -sS -o /dev/null http://192.168.178.197/z80`
(HTTP HTML only, curl does NOT execute JavaScript or open WS),
then capture memory endpoint again. This isolates the HTTP
page delivery's peak from the full browser+WebSocket path.
Follow with controlled actual browser test on a **separate
fresh boot** for comparable lifetime min baseline. Do NOT
run active Web Terminal concurrently with BBS yet, as combined
peak may again leave only ~2KiB 8bit heap available.
Next source change only if requested: tiny staged heap samples
around `/z80` GET and WS accept/output paths, and then
targeted allocation optimization. Do not increase static DRAM
because strict linker ceiling 124544B.

No ESP32 firmware source changes or main/recovery merge
resulted from this diagnostic run.

---
## 2026-10-23 active browser Web Terminal disconnected: full heap recovery

User **navigated away** from browser Web Terminal `/z80`,
after testing it in CP/M on the new early-IRAM TFT+Web/no-Telnet
firmware. `/api/system/memory` at uptime 792493ms:
`heap_free=60704`, `free_8bit=21528`,
`largest_8bit=18420`, `min_heap_free=41612`,
`min_free_8bit=2436`, modem_state=2,
Telnet disabled, Web Terminal compiled in, TFT enabled.

| Sample | heap_free | free8 | largest8 | min_free8 |
| --- | ---: | ---: | ---: | ---: |
| CP/M before browser terminal | 60140 | 20964 | 17396 | 8860 |
| Browser terminal active | 59032 | 19856 | 18420 | 2436 |
| Browser navigated away | **60704** | **21528** | **18420** | **2436** |

After browser close, current free8 **+1672 bytes** vs
terminal-active and **+564 bytes** vs earlier CP/M baseline;
largest 8-bit block **+1024 bytes** vs earlier baseline.
Thus no substantial retained heap leak is visible in this
single open/use/close cycle; the network client and associated
session resources appear reclaimed. Navigating away is not
independent proof of WebSocket disconnect; if needed add
session count diagnostics. Historical low remains 2436B
because it is monotonic until reboot.

**Outstanding caution:** 2436B transient historical min
appeared during browser active phase, down from 8860B before
opening. Cause cannot be attributed specifically to handshake,
HTTP page delivery, WebSocket output or other allocator traffic
from endpoint snapshots alone; do not use live Web Terminal
concurrently with BBS yet. Next sensible A/B isolation:
fresh cold boot, no BBS, idle CP/M, capture memory before
opening `/z80`, while active, and after closing. If transient
trough repeats, instrument specific WebSocket connect/output
allocation paths with tiny stage captures rather than relying
only on lifetime min_free8. Verify DIR/Multiplan separately
if user has not confirmed them on this experimental image.

No firmware changes from the browser-close observation; no
main/recovery merge.

---
## 2026-10-23 early-IRAM fourth target: active browser Web Terminal memory observation

After completing and logging out of a physical TFT/HP BBS session,
user opened browser Z80 Web Terminal `/z80` in CP/M, with Telnet
compiled out and no BBS connected. Browser terminal reported
"open and in use"; no explicit DIR or Multiplan repeat has yet
been reported for this step.

At uptime_ms 734628, `/api/system/memory`:
heap_free=59032, free_8bit=19856, largest_8bit=18420,
min_heap_free=41612, **min_free_8bit=2436**, modem_state=2
(worker resident, NOT evidence BBS still connected),
telnet_enabled=false, telnet_connected=false,
web_terminal_enabled=true, tft_terminal_enabled=true.
Stack unused Web2728, CPU2880, console1280, keyboard2132,
SPI6320, modem3320, TFT2980.

Compared to prior CP/M-only before browser Web Terminal open
(heap=60140, free8=20964, largest8=17396,
min_heap=48036, min_free8=8860): current heap -1108,
free8 -1108, largest8 +1024, historical min8 **-6424**
to only **2436** (and min total -6424). Thus modest ~1.1KiB
persistent net delta between these two samples but at least
6.4KiB further transient memory pressure after prior snapshot.
Unclear which specific event caused trough (initial HTTP page,
WebSocket handshake, terminal output bursts, HTTP memory
diagnostics, other concurrent task); do NOT assert exact source
from high-water mark alone. Web Terminal live operation appears
functionally usable; no user-reported fault.

**Next controlled gate**: close `/z80` browser window
(disconnect WebSocket), leave modem TCP idle, allow allocations
to settle, and capture `/api/system/memory` once more.
Low-water mark is lifetime cumulative and won't rise after
close until ESP reboot; compare current free8/heap/largest.
Do NOT attempt concurrent live BBS/WebSocket until transient
headroom issue is investigated. It may be useful to instrument
WebSocket handshake, terminal output, and dynamic HTTP
allocation peaks separately in followup build, without
consuming constrained static DRAM. Early cache allocation
`iram32` and no-Telnet decision remain successful.

---
## 2026-10-23 early-IRAM fourth target: BBS logout and return to CP/M complete

User continued single BBS session on physical TFT and HP keyboard using
`esp32dev-tft-no-telnet` with early 8192-byte SPI cache in `iram32`,
browser Web Terminal compiled in but not actively connected and Telnet disabled.
Successfully **logged off the BBS and returned to CP/M without reset**.
User has not yet explicitly repeated DIR/Multiplan for this session.

| Phase | uptime_ms | heap_free | free_8bit | largest_8bit | min_free_8bit |
| --- | ---: | ---: | ---: | ---: | ---: |
| MODEM ready | 232449 | 60408 | 21232 | 18420 | 20120 |
| BBS active | 404483 | 60024 | 20848 | 17396 | 8860 |
| Logged off | 457229 | 61152 | 21976 | 19444 | 8860 |
| Back in CP/M | 503310 | 60140 | 20964 | 17396 | 8860 |

Post-CP/M vs MODEM-ready free8 **-268 bytes**, heap_free **-268**
and largest8 -1024; **no indication of substantial retained
per-session heap loss**. Historical min remained 8860B between
BBS, logout and CP/M snapshots, unlike prior dram8 test dropping
from 1836B to 476B on logout. `modem_state=2` remains resident;
`tcp_closed` stage captured total62604/free8=23428/largest8=14836
at TCP closure; these stage values are not synchronized with later
HTTP snapshots. Web/TFT flags true, all Telnet flags disabled.
Console unused stack 1280B and modem 3320B remain stable.

Old full-minus-Telnet **dram8** cache test after CP/M:
free8=11344, largest8=9716, historical min=476.
New early IRAM result after CP/M: **+9620 free8**,
**+7680 largest8**, **+8384 in min_free8**. Earlier TFT-only
(WebTerm compiled OUT, Telnet disconnected) after CP/M:
free8=20752, largest8=19444, historical min=4212;
new full early-IRAM after CP/M has +212 free8, -2048 largest8,
historical min +4648. These are single-run, workload-dependent.

**Conclusion:** first full physical TFT/HP BBS cycle with full
browser-terminal firmware compiled IN and Telnet disabled passes
login, logout, CP/M return, and memory recovery. Idle WebTerminal
not materially costly under this test. Concurrent **active**
browser Z80 WebSocket plus BBS remains UNTESTED, as do long repeated
sessions. Previous CBIOS ROM timeout did not reproduce on early
IRAM coldboot; continue to monitor. Suggested next cautious gate:
DIR and Multiplan after return to CP/M, then optionally open
`/z80` browser terminal in **CP/M idle**, one client only, compare
memory before/open/after close, test shared ANSI/keys/TFT with
no concurrent BBS; stop if heap becomes tight. Only later consider
active browser+BBS concurrency, with explicit memory headroom.

No firmware changes from these results, no main or recovery merge.

---
## 2026-10-23 early-IRAM full TFT/Web no Telnet: BBS connected with improved heap margin

User dialed BBS on physical TFT+HP keyboard with early IRAM-only
`esp32dev-tft-no-telnet` firmware. Browser Web Terminal compiled
IN but not connected, Telnet disabled.
At uptime_ms 404483, `/api/system/memory`: heap_free=60024,
free_8bit=20848, largest_8bit=17396, min_heap_free=48036,
min_free_8bit=8860, modem_state=2,
modem_dial_refused_low_heap=0. Console unused stack 1280B,
modem 3320B, web 2728B, z80_cpu 2880B, HP keyboard 2132B,
SPI 6320B, TFT 2980B.

Dial stage pre-connect total 64096, free8 24920, largest8 20468;
post-connect total 62964, free8 23788, largest8 19444.
Dial guard conditions satisfied comfortably.

| Hardware BBS condition | heap_free | free8 | largest8 | min_free8 |
| --- | ---: | ---: | ---: | ---: |
| Previous full, no Telnet, DRAM8 SPI cache | 57952 | 10568 | 8692 | 1836 |
| Earlier TFT-only, IRAM32 SPI cache, Telnet disconnected | 56812 | 17636 | 16372 | 8108 |
| **New full, no Telnet, early IRAM32 SPI cache** | **60024** | **20848** | **17396** | **8860** |

Vs previous full DRAM8 cache BBS sample: +2072 total heap,
**+10280 free8**, **+8704 largest8**, +7024 in min_free8.
Vs TFT-only previous BBS sample: +3212 free8 and +1024 largest8,
despite Web Terminal code compiled IN. Minima are historical
within a boot and workload-dependent, not a uniform per-operation
benchmark. Caution: 8860B transient minimum shows TCP/terminal
activity still adds temporary memory pressure. No evidence yet
on *concurrent active* Z80 browser WebSocket and BBS.

**Next test gate:** preserve the single BBS connection, verify
normal physical TFT ANSI colors, scrolling and HP keyboard;
then log out normally, return to CP/M and record memory
at logout and after CP/M, including historical minimum.
No second BBS/WebTerminal concurrency until first recovery passes.
Earlier this boot verified `cache_storage=iram32`, early allocation
accepted with zero free8 reduction, and all ROMs including
CBIOS loaded `Done` without Teensy UART timeouts.
No source modification or merge as result of this snapshot.

---
## 2026-10-23 early-IRAM fourth target: MODEM initialization memory PASSED

Physical HP/TFT user started MODEM on the early-IRAM-only
`esp32dev-tft-no-telnet` firmware, no BBS dial yet.
`/api/system/memory` at uptime_ms=232449:
`heap_free=60408`, `free_8bit=21232`, `largest_8bit=18420`,
`min_heap_free=59296`, `min_free_8bit=20120`, `modem_state=2`,
`telnet_enabled=false`, `telnet_connected=false`,
`web_terminal_enabled=true`, `tft_terminal_enabled=true`,
`modem_dial_refused_low_heap=0`. Modem worker stack unused 4440 B,
console 2128 B, web 2728 B, TFT 2980 B, SPI 6320 B.
MODEM_READY staged sample: total=64096, free8=24920, largest8=20468.

Compare earlier full TFT+Web with disabled Telnet but **dram8 cache**
at MODEM: heap=59308, free8=11944, largest8=10740, min8=10852.
New early IRAM improves heap +1100 B, **free8 +9288 B**,
**largest8 +7680 B**, min8 +9268 B (historical minima are
timing-dependent and not identical workloads).

Compare earlier **TFT-only/Web Terminal compiled OUT** control
at MODEM (heap=60044, free8=20868, largest8=18420, min8=13040):
new full+WebTerminal-enabled/no-Telnet has **+364 B free8**
and **same 18420 B largest8**. This reinforces that the dramatic
previous ~8KiB loss arose from SPI cache allocation in normal
8bit DRAM, not from compiling Web Terminal in.

Both dial guards currently satisfied: heap 60408 > 49152 and
largest8 18420 > 12288. Boot earlier confirmed cache iram32
and cboot/cpm22/cbios all loaded Done, no UART timeout observed.
Recommended: one **short, controlled** physical TFT/HP keyboard
BBS dial; keep browser `/z80` and extra Web UI pages closed,
use `/api/system/memory` only for observation. Snapshot once
BBS logged in and again after normal logout and return to CP/M.
Stop if terminal/network becomes sluggish or low-water free8
approaches a few KiB. Do not claim stability until BBS test
completes. No source change or merge.

---
## 2026-10-23 early-IRAM experiment: first hardware coldboot PASSED

User built/flashed early IRAM-only `esp32dev-tft-no-telnet`
(source revision 639261d377508a1c0b9245f960e22b99a4b218ba).
At uptime 34411ms, `/api/cpm/spireads` reported:

```json
{"enabled":true,"boot_disabled":false,"cache_storage":"iram32",
 "early_iram_only":true,"cache_bytes":8192,
 "early_cache_allocator":{"accepted":true,
 "free8_before":165676,"free8_after":165676,
 "exec32_free_before":174984,"exec32_free_after":166776,
 "exec32_largest_before":110580,"exec32_largest_after":110580},
 "cache_hits":30,"cache_misses":2,"batch_reads":2,
 "frames":32,"uart_fallbacks":0,"spi_failures":0}
```

**Confirmed** early IRAM allocation accepted. Exactly zero 8-bit
RAM consumed by reservation; exact-capability free decreased 8208B
for 8192B word cache plus allocator overhead. SPI read-ahead functioning,
no failures. Fourth variant has telnet_enabled=false,
web_terminal_enabled=true, tft_terminal_enabled=true.

`/api/system/memory` coldboot: uptime_ms=34411,
heap_free=69204, free_8bit=30028, largest_8bit=26612,
min_heap_free=61288, min_free_8bit=22112, modem_state=0.
Compared with previous fourth variant with cache `dram8` at coldboot
(heap=68168, free8=20784, largest8=18420, min_free8=14244):
+1036 heap, **+9244 free8**, **+8192 largest8**,
+7868 minimum free8. Compared with earlier TFT-only WebTerm-off
coldboot free8=28808, new full WebTerm-on no-Telnet free8=30028;
Web Terminal compiled-in idle does not appear to cost 8KiB.

Stages of new boot: web total112820/free8=73644; keyboard
100684/61508; SPI stage 99644/60468; TFT 87932/48756;
CP/M boot 81608/42432. `stack_unused_bytes`: Web2984,
Z80 CPU2880, console2128, HP keyboard2164, SPI6320, TFT2980.
Compare old fourth variant SPI stage free8=52252, confirming
the previous ordinary-DRAM cache was the important difference.

`/z80debug` on new coldboot: `/cpm/cboot.rom` 3B Done,
`/cpm/cpm22.rom` 5683B Done, `/cpm/cbios.rom` 6656B Done.
**No EBTKS UART transaction timeout visible**; previous CBIOS
loader failure did not reproduce on this boot. Do not claim
intermittent UART defect resolved permanently.

**Next hardware gate:** start MODEM from physical HP/TFT, keep
Telnet disabled and browser `/z80` unopened; capture
`/api/system/memory` before dialing. Compare old TFT-only MODEM
free8=20868/largest8=18420/min_free8=13040, previous fourth
variant `dram8` MODEM free8=11944/largest8=10740/min_free8=10852.
If memory margin healthy and CP/M remains responsive, only then
one short BBS test, with snapshots and normal logout. Avoid
multiple simultaneous browser/Telnet sockets in this gate.
No firmware changes made from these first hardware findings;
feature branch remains unmerged; do not touch main/recovery.

---
## 2026-10-23 Fourth-target experimental fix: reserve IRAM cache before Web/Wi-Fi

Hardware baseline: full TFT+Web **without Telnet** previously used
8192-byte `dram8` SPI cache, left just 10568B current free8 while
BBS connected and 476B historical free8 minimum by logout.
CBIOS `/cpm/cbios.rom` boot also emitted two EBTKS UART timeouts
and `FAILED`, although subsequent DIR/Multiplan ran.
User requested next experiment to move SPI cache into early IRAM.

Changes on feature branch only, no main/recovery/Teensy/HP changes:

- New flag **`EBTKS_SPI_CACHE_EARLY_IRAM_ONLY=1`** only in
  `esp32dev-tft-no-telnet` PlatformIO environment. Other three
  configurations retain their original cache/telnet behavior.
- Before Wi-Fi/Web startup, directly after `ebtksLinkBegin()`,
  `setup()` calls `spiDiskReserveCacheEarly()`.
- Attempts the existing 8192B cache from exact capabilities
  `MALLOC_CAP_32BIT|MALLOC_CAP_EXEC|MALLOC_CAP_INTERNAL`, and
  rejects/reclaims it if free8 drops; does **not** initialize SPI
  DMA or its disk worker at this stage.
- New compile-time early-IRAM-only branch in `spiDiskEnsureCache()`
  **returns false before `MALLOC_CAP_8BIT` fallback**. On failure,
  marks SPI boot disabled with informative last_error and leaves
  all CP/M disk reads on existing Teensy UART transport; writes
  always UART. Runtime Media Manager cannot bypass this boot guard.
- Logs early exact-cap free/largest and 8bit before/after.
  Because `/z80debug` is a 2KiB rolling log, preserves those
  numbers in `GET /api/cpm/spireads` under `early_cache_allocator`:
  accepted, free8_before/after, exec32_free_before/after,
  exec32_largest_before/after. Uses one small dynamically
  allocated diagnostic object, no 8KiB static DRAM array.
- Extends native source regression tests to ensure exact variant
  flag, earliest boot placement, sticky UART fallback,
  and durable diagnostics; `FIRMWARE_VARIANTS.md` has new
  boot/hardware-gate instructions.

**Post-flash hardware acceptance order:** coldboot with no open
Web Terminal or Telnet; collect `/api/cpm/spireads` and
`/api/system/memory` first. Should show cache_storage=`iram32`
and accepted=true, with free8_after>=free8_before. If none,
UART mode is intended safe fallback; **dram8 never acceptable**
in this new build. Verify `/z80debug` CBIOS load `Done` rather
than prior `FAILED` before MODEM/BBS. If CBIOS fails, stop
BBS test and investigate Teensy UART. If boot clean, compare
coldboot and MODEM-start memory with prior TFT-only control;
only if safe margin, later consider one short TFT/HP BBS dial
without opening `/z80` WebSocket. User expects explicit
pull/build instructions after green CI. No claim of hardware
success before testing.

---
## 2026-10-23 ESP32 allocator pools after full-no-Telnet test: 47KiB IRAM-capable block remains

User captured `/api/system/memory/pools` at uptime 509827ms
after BBS logout and successful CP/M DIR/Multiplan in
`esp32dev-tft-no-telnet` with SPI cache allocated in `dram8`.
Rows overlap and must NOT be summed:

| Capability | Free | Largest | Min free |
| --- | ---: | ---: | ---: |
| internal | 58944 | 47092 | 39652 |
| byte_8bit | 11524 | 10740 | 476 |
| internal_byte_8bit | 11488 | 10740 | 476 |
| word_32bit | 58828 | 47092 | 39652 |
| internal_word_32bit | 58792 | 47092 | 39652 |
| executable | 58732 | 47092 | 39636 |
| dma | 11328 | 10740 | 476 |

At measurement time, a ~47KiB **contiguous 32-bit/executable**
pool remains while only ~11.5KiB ordinary byte-addressable DRAM
does. This is compatible with IRAM still being available for the
8KiB SPI cache even though its **earlier allocation** fell back
to DRAM8. However each row is a capability union; these are
not direct measurements of
`MALLOC_CAP_32BIT|MALLOC_CAP_EXEC|MALLOC_CAP_INTERNAL`
intersection at boot, so cannot yet assert why exact malloc
failed or was rejected by `free8After>=free8Before` guard.
Per Espressif docs ESP32 supports 32-bit-aligned accesses to IRAM
and D/IRAM aliases; an allocation can affect both the 8bit and
executable pools. Source `spi_disk_read.h::spiDiskEnsureCache()`
already checks for 8bit drain and releases the speculative word
cache before taking 8bit DRAM fallback.

**Engineering recommendation**: instrument exact-cap intersection
largest/free and free8 deltas at attempted word-cache allocation,
and test reserving IRAM word cache *before* Web/Wi-Fi/task
allocation. For fourth experimental target, avoid unexpected
8KiB byte-DRAM fallback: if IRAM word cache cannot be secured,
leave SPI read-ahead disabled and use existing UART fallback.
Preserve alignment-only word-store/load implementation and do
not touch actual Teensy disk operations. Verify cold boot
`cache_storage=iram32`, free8 near prior TFT-only baseline,
no CBIOS load failures, and controlled MODEM/BBS minimum
before declaring success. Nothing merged to main.

---
## 2026-10-23 full no-Telnet BBS successful logout, DIR/Multiplan pass; CRITICAL min free8 476B

User completed no-Telnet TFT/HP keyboard BBS session in
`esp32dev-tft-no-telnet`, logged off normally, returned to CP/M,
and confirmed **DIR and Multiplan both work**.

| Phase | uptime_ms | heap_free | free_8bit | largest_8bit | min_free_8bit |
| --- | ---: | ---: | ---: | ---: | ---: |
| MODEM started | 117444 | 59308 | 11944 | 10740 | 10852 |
| BBS active | 291937 | 57952 | 10568 | 8692 | 1836 |
| Logged off | 390137 | 60052 | 12668 | 11764 | **476** |
| Back in CP/M | 449122 | 58728 | 11344 | 9716 | **476** |

At logout all Telnet flags still disabled; Web Terminal=true, TFT=true,
MODEM worker remains state=2. Lowest transient 8-bit RAM **476B**
after BBS active (even worse than 644B when previous Telnet+BBS
failed). This PROVES removing Telnet alone does not guarantee safe
memory margin when SPI cache is in DRAM8. Runtime currently free8
11344B and largest8 9716B, under current dial guard of 12288B.
Do not repeat dial or open browser `/z80` WebSocket concurrently
until cache placement issue fixed.

From BBS active to logged out, free8 recovered +2100B and largest
+3072B. Logged off to back in CP/M free8 changed -1324B, largest
-2048B; may be secondary allocations or fragmentation. No evidence
of continuously growing leak from two observations alone.

`/api/cpm/spireads` after CP/M: still `cache_storage=dram8`
(8192B), enabled, 22 successful batches, 352 sectors,
187 hits, 22 misses, zero SPI failures, zero UART fallback;
cache_first=16 and cache_count=16. This substantiates continuing
SPI functionality alongside DIR and Multiplan. Previous
CBIOS load emitted **two Teensy UART timeouts and FAILED**;
DIR and Multiplan functioning does not prove full pristine
CBIOS image loaded at boot. Investigate boot integrity independently.

For next firmware revision prioritize IRAM32 cache reservation
early enough to avoid DRAM8 fallback; consider IRAM-only policy
and UART read fallback if reservation fails to protect networking.
Need `GET /api/system/memory/pools` to understand IRAM/EXEC
allocator availability; that endpoint was requested but user
provided SPI cache endpoint instead. Once documented, controlled
short BBS test after cache change, not on current image.
No firmware source changes or main/recovery merge from this data.

---
## 2026-10-23 full TFT + Web without Telnet: BBS connected but 1836B free8 minimum

User continued the physical TFT + HP keyboard BBS experiment with
`esp32dev-tft-no-telnet`, even though MODEM-start free8/largest8 were
11944/10740. At uptime 291937 ms, BBS dialed and session in progress.
HTTP memory endpoint still responsive; `telnet_enabled=false`,
`telnet_listener_mode=disabled`, `telnet_connected=false`,
`web_terminal_enabled=true`, `tft_terminal_enabled=true`,
`modem_state=2`, `modem_dial_refused_low_heap=0`.

Current heap_free=57952, free_8bit=10568, largest_8bit=8692,
**min_free_8bit=1836**, min_heap_free=41012. Stack unused bytes
console=1380, Web=2728, modem=3312, HP keyboard=2132, SPI=6324,
TFT=2976, CPU=2656. This is a *very thin* transient memory
reserve: no more load or concurrency should be added.

Dial guard triggered at exact `before_tcp_connect` capture:
heap=64216, free8=16832, largest8=12788. These numbers SATISFY
thresholds 49152 total and 12288 largest8 even though prior
snapshot had only 10740 largest8; allocations changed between
samples. `after_tcp_connect` captured 63084 total, 15700 free8,
largest8 12788. Do not claim dial guard was bypassed.

SPI endpoint still reports enabled=true, cache_storage=dram8,
cache_bytes=8192, batches=6, frames=96, spi_failures=0.
Previous TFT-only no-Telnet BBS login was free8=17636,
largest8=16372, min_free8=8108; new run has -7068 free8,
-7680 largest, -6272 min. Fits 8KiB DRAM cache allocation
penalty. Not proof of exact WebTerminal static/dynamic overhead.

Boot debug still contains two EBTKS UART transaction timeouts
and `FAILED` at cbios.rom; no new UART failure shown during
this session. This remains a *boot integrity concern*.
After session: recommend normal BBS logout; if healthy, capture
`/api/system/memory` and `/api/system/memory/pools`,
avoid opening `/z80` concurrently or any additional BBS dial.
Prioritize deterministic SPI read-cache placement and investigation
of CBIOS load timeout before another hardware stress trial.
No firmware changes from these measurements; no merge.

---
## Fourth build hardware check: DRAM cache fallback confirmed

MODEM initialized without dialing. Current free8 11944, largest8
10740, heap_free 59308, min_free8 10852, modem_state 2.
Telnet disabled; browser and TFT terminal enabled. Largest free8
is below the 12288-byte dial guard. Do not dial on this boot.

User's SPI endpoint reported cache_storage=dram8, cache_bytes=8192,
word_cache_default=true, enabled=true, 6 successful SPI batches,
no failures. This confirms the 8KiB free8 loss at SPI cache setup:
the IRAM32 allocation preference fell back to normal DRAM.
Determine why IRAM32 was unavailable with memory/pools diagnostics
before changing allocations.

Boot log also reports two Teensy UART transaction timeouts and FAILED
during /cpm/cbios.rom loading, despite later Starting Z80 and MODEM
initialization. Investigate this independent boot reliability concern.
Do not assume the CBIOS image loaded properly on this boot.

No firmware source changes. Stage A remains unmerged.

---
## 2026-10-23 20:00 Berlin — Fourth build cold boot: suspect 8KiB SPI cache DRAM fallback

User flashed full TFT+Web without Telnet (`esp32dev-tft-no-telnet`).
Cold-boot uptime=35768 ms: heap_free=68168, free8=20784,
largest8=18420, min_free8=14244,
`telnet_enabled=false`, listener mode disabled, connected=false,
`web_terminal_enabled=true`, `tft_terminal_enabled=true`, modem_state=0.
Web worker stack unused=3016, cpu=2656, console=2128, keyboard=2132,
SPI=6324, TFT=2976. All expected capability flags correct.

Compared with prior **TFT-only, no connected Telnet** coldboot
(67984 heap, 28808 free8, 27636 largest8), the new build
shows **+184 bytes total free heap**, but **-8024 free8** and
**-9216 largest 8bit**. An initialization-stage comparison is
almost definitive that the difference emerges at SPI disk-cache
allocation, not at Web startup or Z80 console:

| Stage | TFT-only free8 | No-Telnet full free8 |
| --- | ---: | ---: |
| web | 73544 | 73684 |
| keyboard | 61380 | 61548 |
| spi_disk_cache | 60368 | **52252** |
| tft | 48652 | 40584 |
| cpm_boot_ready | 43360 | 34260 |

`spi_disk_read.h::spiDiskEnsureCache()` first tries an **8192-byte**
`heap_caps_malloc(..., MALLOC_CAP_32BIT|MALLOC_CAP_EXEC|MALLOC_CAP_INTERNAL)`
and accepts it only if `free8After >= free8Before` (storage `iram32`).
If it cannot allocate IRAM or consumes 8bit RAM, it frees and
**falls back to 8192-byte MALLOC_CAP_8BIT DRAM** (`dram8`).
The new build's 8K step is highly consistent with **dram8 fallback**,
and the prior TFT-only boot used IRAM32, but verify direct telemetry
**before concluding**. Existing endpoint
`GET /api/cpm/spireads` has `cache_storage`, `cache_allocated`,
`word_cache_default`, `enabled`, `last_error` fields.
Also `GET /api/system/health` includes `spi_cache_storage`, and
`/api/system/memory/pools` reports allocator-capability inventory.
Use numeric IP; no need to reboot/flash. Ask for these BEFORE BBS
stress; with just 20784 free8 at coldboot, MODEM+BBS may approach
the dangerous previous 644B low water or refuse the modem dial.

**Caution:** The apparent 8KiB 'Web Terminal cost' is confounded
by cache allocation placement and should not be attributed to
browser WebTerminal HTML/WS until controlled comparison establishes it.
If confirmed, prioritize deterministic IRAM-only cache policy or
startup allocation order, balanced against performance of UART
fallback if no IRAM available. No source change made from this
observation. Keep four target source on feature branch only.

---
## 2026-10-23 Four firmware configurations: successful GitHub CI, ready for Mac/hardware testing

Fourth target implemented and CI **GREEN** at source commit
`e355175d59587014936fb2977320615698404b23`,
run: https://github.com/timob0/EBTKS-Z80-Emulator/actions/runs/39994629516.
Both jobs passed: native Z80/bridge regressions and four linked
ESP32 PlatformIO firmware images, plus firmware feature isolation
(no Telnet listener, no Telnet console TX path in new binary),
exact embedded SHA and Mac-derived static DRAM guard.
Linked TFT static DRAM: full 124536 bytes, TFT-only 124432,
**TFT + Web without Telnet 124464**, below conservative guard
124544. New target static DRAM is 72B lower than full;
this does **not** guarantee comparable runtime heap reduction.
An initial CI failure from the guard around shared serialTask
`if (n)` scope was fixed before green run. No additional source
changes since green run.

Build on Mac: `git fetch origin && git switch feature/cpm85-management-console
&& git pull --ff-only`; `pio run -e esp32dev-tft-no-telnet -t clean`
then `pio run -e esp32dev-tft-no-telnet`; flash
`.pio/build/esp32dev-tft-no-telnet/firmware.bin` by ESP32 `/updater`.
After boot memory endpoint must show `telnet_enabled=false`,
`telnet_listener_mode="disabled"`, `telnet_connected=false`,
`web_terminal_enabled=true`, `tft_terminal_enabled=true`.
TCP/23 should refuse connections. Initially use **only**
`/api/system/memory`, physical TFT+HP keyboard, and MODEM BBS
to compare to previous TFT-only/no-Telnet control; log cold boot,
MODEM initialized, BBS logged in, logged off, back in CP/M.
After this, optionally open `/z80` WebSocket as second socket
and cautiously observe memory, if system remains healthy.
**Hardware NOT yet tested.** No Teensy/HP LOADBIN changes;
main/recovery untouched, do not merge without user approval.

---
## 2026-10-23 four firmware target experiment: full Web + TFT, Telnet compiled OUT

User confirms that during no-Telnet physical TFT/HP-only BBS session
ANSI **colors**, scrolling, HP keyboard and overall terminal behavior
were **normal**. In that session user accessed **only** the HTTP
`/api/system/memory` endpoint; no other Web UI pages/sockets.
This reinforces that earlier 8-KiB improvement at BBS login versus
concurrent Telnet was not caused by turning off all Web UI pages,
rather by the absence of Telnet connection and/or browser Z80 Web
Terminal implementation. The no-Telnet isolation variant will help
separate those factors.

**New fourth PlatformIO environment:** `esp32dev-tft-no-telnet`.
Extends full `esp32dev-tft`: TFT=1, mirror=1, HP keyboard=1,
Web Terminal=1, **`EBTKS_TELNET_ENABLE=0`**. Removes Telnet
port-23 listener/client global objects, TelnetTask, shared Web
Telnet polling, ConsoleTask Telnet 1024-byte TX FIFO/send path
and Telnet input parser using compile-time guards. Leaves
browser `/z80`, `/z80ws`, TFT, HP keyboard, CP/M Wi-Fi MODEM
(a separate outgoing TCP connection), Web UI `/ws`, `/crt`,
`/api/system/memory`, Media Manager and OTA. API new explicit
`telnet_enabled` boolean; in fourth variant `telnet_connected=false`,
`telnet_listener_mode="disabled"`. Existing three build targets
stay the same. Update docs `FIRMWARE_VARIANTS.md` with Mac build
commands and controlled first-pass test using **only memory endpoint**.
Tests/CI extended for fourth binary and Mac static DRAM safety.

**Proposed hardware order:** after CI and Mac build, flash
`.pio/build/esp32dev-tft-no-telnet/firmware.bin`, verify port 23
refused, TFT works, WebTerminal available but NOT yet connected;
capture cold-boot, MODEM-ready and BBS login/logout memory with
only memory endpoint open. Once this control passes, optionally
connect browser Z80 Web Terminal and compare active concurrent
WebSocket use, monitoring 8-bit free/largest low water. Do not
automatically open WebTerminal while running the first controlled
single-socket BBS run. Never merge feature branch into main
without user approval.

---
## 2026-10-23 Physical TFT/HP-only BBS logout, return to CP/M: no sustained heap loss

User completed controlled no-Telnet BBS session on physical TFT/HP keyboard
and provided two subsequent `/api/system/memory` samples.

| Phase | uptime_ms | heap_free | free_8bit | largest_8bit | min_free_8bit | telnet | modem |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| MODEM before BBS | 96286 | 60044 | 20868 | 18420 | 13040 | 0 | 2 |
| Logged into BBS | 155720 | 56812 | 17636 | 16372 | 8108 | 0 | 2 |
| Logged off BBS | 213815 | 59940 | 20764 | 19444 | 8108 | 0 | 2 |
| Back in CP/M | 256095 | 59928 | 20752 | 19444 | **4212** | 0 | 2 |

Comparison of MODEM-before-BBS to CP/M after: free8 only 116B lower;
heap_free only 116B lower, largest8 1024B HIGHER, consistent with
most per-connection memory reclaimed. No evidence from these snapshots
of substantial retained heap loss after logout. The modem background
worker persists at state 2, explaining ~8KiB persistent footprint
vs cold boot (free8 28808 to 20752). HP keyboard/TFT and CP/M continue.

**Important transient warning:** `min_free_8bit` dropped from 8108
at logout to 4212 at return to CP/M although current free8 changed
only -12B. Some intervening event briefly used at least ~3.9KiB
more 8-bit RAM and later released it. The source is unknown: do
NOT ascribe it to a particular service without targeted diagnostic.
Lowest free8 is still far above the 644B transient during simultaneous
BBS+Telnet run, but not comfortably high for unchecked new allocations.

Test outcome: single physical TFT+HP BBS path **logged in, logged off,
returned to CP/M, stable heap after logout**. Sustained long-session
responsiveness, ANSI colors, Media Manager availability and repeated
BBS dials remain unverified. This is NOT acceptance of concurrent
Telnet+BBS stress, which previously FAILED. User has not yet explicitly
reported subjective TFT rendering/keyboard/UI feel for this run.
Keep variants on feature branch; no source changes or merge.

---
## 2026-10-23 Physical TFT-only BBS logged in with no Telnet: substantial heap improvement

Follow-up to cold-boot baseline. Physical HP keyboard + TFT CP/M MODEM COM
logged into BBS **without any Telnet client**. User has not yet reported
subjective GUI responsiveness, long-session stability, or successful hangup.

| Snapshot | uptime_ms | heap_free | free_8bit | largest_8bit | min_free_8bit | telnet | modem |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold boot | 35636 | 67984 | 28808 | 27636 | 21280 | 0 | 0 |
| MODEM initialized / user says not dialed | 96286 | 60044 | 20868 | 18420 | 13040 | 0 | 2 |
| Logged into BBS | 155720 | 56812 | 17636 | 16372 | 8108 | 0 | 2 |

**Hardware comparison** with earlier TFT-only *simultaneous*
BBS+Telnet session at login: heap/free8 48072/8896 then vs
56812/17636 now = **+8740 B free8**, largest8 7924 vs 16372
= **+8448 B**, min_free_8bit 644 vs 8108 = **+7464 B**.
This strongly suggests a concurrent Telnet TCP stream greatly contributes
to resource pressure; differences in traffic/rendering and sampling
mean it does NOT precisely measure Telnet socket's sole incremental cost.
Current console stack unused 1480B, modem 3312B, web 2556B,
TFT 2980B. No dial refused, modem state=2, telnet_connected=0.

**Notable inconsistency to follow up only if needed:** user's first
'MODEM initialized, not dialed yet' sample already contains valid
`before_tcp_connect` and `after_tcp_connect` stage captures
(64096 free heap/24920 free8/21492 largest then
62380/23204/21492); in `ebtks_modem.h::handleHayes` those are
recorded only on a DIAL event. So a DIAL event had in fact been
processed before that sample; do not treat it as a guaranteed
never-dialed control sample. At login stage samples were refreshed:
before 63824/24648/21492, after 61852/22676/21492.

Still current free8=17636 < Media Manager 24KiB read admission,
so a controlled 503 is expected even in TFT-only BBS session.
Next: ask how TFT responsiveness/colors and Web UI behave during BBS;
if healthy, log out normally and capture memory afterward. Stop on
unresponsiveness. Do NOT claim Stage A accepted or merge main.

---
## 2026-10-23 physical TFT + HP keyboard, no-Telnet control test: cold-boot baseline

User initiated controlled comparison without Telnet to isolate the
second TCP client. Cold boot measurement at uptime=35636 ms:
`heap_free=67984`, `free_8bit=28808`, `largest_8bit=27636`,
`min_heap_free=60456`, `min_free_8bit=21280`,
`telnet_connected=0`, `modem_state=0`, `modem_dial_refused_low_heap=0`,
feature flags `web_terminal_enabled=false`, `tft_terminal_enabled=true`.
Boot-ready snapshot free8=43360, largest8=38900; worker stacks unused
Web 2556, CPU 2660, console 2084, HP keyboard 2136, SPI disk 6328,
TFT 2980. Compared with previous TFT-only boot (free8=27488,
largest8=25588), this current sample is +1320 free8 and
+2048 largest8. Minimum 21280 is a **historical low-water mark**
since this boot, not the currently available 8-bit memory.

Next: launch MODEM from physical HP keyboard/TFT with Telnet still
disconnected, record snapshot before dialing, then one short BBS dial
only if dial guards satisfied and UI remains responsive. Abort
on signs of severe memory pressure. This is an in-progress
**isolation test**, not a stable firmware acceptance.

---
## 2026-10-23 Telnet drop diagnosis confirmed: full 1KiB queue, free block only 1012B

User captured full recent `/z80debug` in the failing TFT-only
nonblocking-Telnet firmware after BBS session. Final relevant lines:

```
New Telnet client @ 192.168.178.52
CP/M modem: virtual UART 88h-8Fh online
EBTKS UART transaction timeout
Telnet TX buffer full; dropping client (queued=1023, free8=5284, largest8=1012)
```

**Direct cause of disconnection confirmed**: CP/M ConsoleTask's
bounded 1024-byte Telnet output queue reached 1023 queued bytes;
the intentionally fail-fast `dropSlowTelnet("buffer full")` ran.
The actual underlying bottleneck is still unproven: heavy CP/M output
burst may outrun one nonblocking send per loop, TCP window may be
congested, or network `pbuf` allocation might fail under 8-bit
heap pressure. Do NOT claim that EAGAIN or socket error was
observed; buffer-full log does not distinguish these.

**Severe transient memory pressure at exact drop:** 5284 total
free MALLOC_CAP_8BIT, but **largest contiguous free8 block only 1012B**.
The recorded low water of **644B** was even worse at a different
instant. Simply increasing Telnet queue size or its retry timeout
would consume more scarce allocatable heap and is not a root fix.
After Telnet/BBS closed, current free8 recovered to 19132/19928,
largest8 to 13300, TFT+HP keyboard stayed responsive and Web UI
recovered. MODEM worker stayed allocated (state 2).

`EBTKS UART transaction timeout` source location:
`ESP32_Z80_Emulator/ebtks_link.h::ebtksLinkTransaction()` logs
this if no Teensy UART reply newline arrives within caller timeout.
HP keyboard worker in `ebtks_cpm85_keyboard.h` polls at 350ms
and serializes via UART mutex; Web `/ws` and Media Manager
also use same transaction machinery. The timeout could be
a missed delayed poll or another transaction, not enough
evidence to assign it specifically to the keyboard or to OOM.
No proof Z80 emulator halted or SD images corrupted.

**Next suggested controlled isolation:** no more simultaneous
Telnet+BBS stress on this firmware. If hardware is responsive
and user wishes to isolate the second network socket, conduct
one cautious BBS dial using **only TFT/HP physical terminal
(no TCP Telnet client)**, with free8/largest8 and `/z80debug`
monitoring. Abort immediately if Web UI becomes unresponsive.
That establishes whether the concurrent Telnet connection
accounts for dangerous transient allocation. Alternatively
measure `/api/system/memory/pools` at idle and after MODEM
to identify available capability pools. Future engineering
should reduce simultaneous TCP/AsyncWeb allocations, not
merely grow buffers. Stage A remains OPEN and unmerged.

---
## 2026-10-23 post-Telnet-drop recovery: TFT/HP continues, HTTP responsive

At uptime **438102 ms** (after Telnet session dropped and BBS TCP
already closed), physical TFT console + HP-85 keyboard continued
functioning and user observed improved Web UI responsiveness.
Memory: heap_free **58308**, free_8bit **19132**,
largest_8bit **13300**, min_free_8bit **644** (historical low water,
not current), telnet_connected **0**, modem_state **2** (worker retained).
Console stack unused 1132 B; modem 3312, Web 2320, TFT 2984.
Compared with immediately after Telnet loss (59104/19928/13300),
current total and free8 are each 796 B lower; no evidence from these
two samples of a continuing major leak. Compared with BBS login,
free8 recovered 10236 B. Modem still allocated after TCP close.

Network resource pressure and disconnect reason remain unresolved:
**obtain `/z80debug` NOW** before its 2KiB log rolls; look for new
`Telnet TX ... dropping client (queued=..., free8=..., largest8=...)`.
Do not perform further BBS stress without diagnosis; do not merge.
No firmware source changes from this observation.

---
## 2026-10-23 TFT-only nonblocking Telnet: live BBS/Telnet load FAIL

User's actual TFT-only branch firmware with new nonblocking Telnet TX reached
MODEM prompt without the earlier startup freeze; Telnet stayed connected
through dial and BBS login. However terminal was **sluggish**, ANSI
**colors appeared lost**, Web UI became less responsive, and eventually
**Telnet disconnected**. No precise disconnect reason yet: **retrieve
`/z80debug` on physical machine BEFORE any additional load**. The new
ConsoleTask logs `Telnet TX buffer full`, `no progress for 2 s`, or
`socket error`, including `queued`, `free8`, `largest8`.

| Physical sample | heap_free | free_8bit | largest_8bit | min_free_8bit | telnet_connected | modem_state |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Prior to dialing; uptime 222752ms | 56624 | 17448 | 13300 | 4212 | 1 | 2 |
| Logged in to BBS; uptime 304259ms | 48072 | 8896 | 7924 | **644** | 1 | 2 |
| After Telnet disconnect; uptime 372499ms | 59104 | 19928 | 13300 | 644 | 0 | 2 |

Modem `dial_refused_low_heap=0` because its **before TCP connect**
sample had heap 60360 and largest8 13300, satisfying 48KiB/12KiB
preflight. **After connect** sampled 58028/18852/13300.
The `tcp_closed` sample at final snapshot was heap=61124, free8=21948,
largest8=13300: both BBS TCP and Telnet had become disconnected by the
later observation. This is significant transient allocator exhaustion
(minimum available 8-bit RAM **644 bytes**, vs 268 bytes in prior
full-bundle regression), not proof of a permanent heap leak.
Despite TFT-only excluding Web Terminal and ConsoleTask nonblocking
send, simultaneous BBS + Telnet + TFT has FAILED hardware acceptance.

Observed console task unused stack: ~1484B during login and ~1132B
after disconnect; modem unused ~3312B, web ~2320B, TFT ~2984B.
No measured stack overflow, but transient network memory pressure
remains critical. The Telnet ring may shed connection by buffer-full
or TX timeout; cannot distinguish without `/z80debug`.
Do NOT claim lost ANSI colors proves a parser bug: content may have
been missed/truncated, terminal settings may have changed, or the
remote BBS emitted something unexpected. Investigate with logs.

**Next actions:** avoid another BBS stress test now; request
`curl -sS --max-time 5 http://192.168.178.197/z80debug`
(numeric IP may differ) to identify latest Telnet TX disconnect reason.
Check whether Web UI/CP/M/TFT have recovered and whether reconnect
to Telnet works at idle. Do not deploy another unverified firmware
until root and memory allocation strategy are revisited; keep Stage A
open, main/recovery untouched.

---
## 2026-10-23 TFT-only nonblocking Telnet firmware hardware retest: MODEM START PASS

User flashed latest TFT-only branch with ConsoleTask nonblocking Telnet TX (exact flashed SHA not supplied),
then captured three memory JSON records on physical hardware.
**Cold boot:** uptime 18461ms, heap_free=66696, free_8bit=27488, largest_8bit=25588,
min_free_8bit=26496, telnet=0, modem_state=0; unused console stack=2088.
**Telnet connected:** uptime 55758ms, heap_free=64264, free_8bit=25088,
largest_8bit=24564, min_free_8bit=21228, telnet=1, modem_state=0;
console unused stack=1528.
**After MODEM launch without BBS dial:** uptime 85916ms,
heap_free=56836, free_8bit=17660, largest_8bit=13300,
min_heap_free=47952, min_free_8bit=8776,
telnet=1, modem_state=2, modem_dial_refused_low_heap=0;
console unused stack=1528, modem unused stack=4432, web=2320.
Flags confirm `web_terminal_enabled=false`, `tft_terminal_enabled=true`.
The memory API responded after MODEM start and Telnet remained connected;
the previous TFT-only freeze + forced Telnet disconnect was **NOT** reproduced.
The observed modem-start footprint vs before MODEM is
7428 bytes less current free heap and free 8-bit RAM, and 11264 bytes
less in largest available 8-bit block; console stack watermark unchanged.

**Caution:** Dial guard checks ESP.getFreeHeap >=49152 and largest8>=12288.
After MODEM, heap is 7684 bytes above that free-heap threshold, largest8
just 1012 bytes above the threshold; earlier BBS sessions reached extremely
low min_free_8bit. This startup PASS is NOT evidence a sustained BBS TCP
session is stable. Suggested next step: one short controlled BBS call,
capture current/min free8 and largest8 during and after, whether Telnet
stays connected and HTTP is responsive; fetch `/z80debug` immediately
if any stall/disconnection. No code change to main/recovery.

---
## 2026-10-23 ~18:30 Europe/Berlin — debug CONFIRMS slow-Telnet watchdog; targeted TX redesign

User retrieved `/z80debug` from real `esp32dev-tft-only` firmware
after failed Telnet/MODEM initialization. Log showed:
  `New Telnet client @ 192.168.178.52`
  `CP/M modem: virtual UART 88h-8Fh online`
  `Telnet TX stalled for 2 s; disconnecting slow client`

The last line proves the **ConsoleTask Telnet-output retry watchdog**
intentionally stopped the Telnet connection. It does NOT establish
why the socket could not accept writes (backpressure, lwIP, memory,
peer state). The modem worker did start; BBS wasn't dialed.

Source review of pinned Arduino-ESP32 framework **2.0.17**
(`espressif32@7.1.3`) found `WiFiClient::write()` itself loops
up to `WIFI_CLIENT_MAX_WRITE_RETRY=10` with
`WIFI_CLIENT_SELECT_TIMEOUT_US=1000000` per retry, doing a
`send(...,MSG_DONTWAIT)` only when select says writable. Thus
the old surrounding 2-second ConsoleTask retry check cannot
interrupt a single library call and may lead to prolonged stalls.

**New experimental firmware change, hardware NOT tested**:
`ESP32_Z80_Emulator/serial.h` now routes data to TFT, ANSI device
probe and browser terminal BEFORE Telnet transport, then uses a
single lwIP `::send(..., MSG_DONTWAIT)` per ConsoleTask iteration
with exact partial-write accounting, a per-connection **1024-byte
heap** output queue (not static BSS or new task), reset on Telnet
socket FD change, and buffer-full/2-second no-progress/socket-error
client shedding instead of blocking ConsoleTask. `/z80debug`
now logs exact cause + queued bytes, free8 and largest8 at drop.
Active slow Telnet peers can still disconnect after buffer pressure;
this patch prioritizes CP/M/TFT/HTTP liveness, not yet guaranteed
lossless Telnet sessions under network congestion. Native
`tests/check_web_ui_cleanup.py` enforces nonblocking calls and
correct routing order. Three firmware variants stay unchanged;
do not merge into main. User must not flash until source-version
CI is green and Mac local clean TFT-only link succeeds.

**Next physical test:** compile/flash *only* TFT-only variant, use
numeric IP for health/heap, coldboot verify DIR/Multiplan and
Telnet input/output. Then launch MODEM (no BBS dial yet); confirm
HTTP, TFT/HP CP/M and Telnet remain responsive and inspect
`/z80debug`. If Telnet drops, log now distinguishes buffer full,
socket error, or 2s zero progress; capture memory again. Do not
repeat BBS load until MODEM startup passes. Preserve recovery/main.

---
## 2026-10-23 ~18:25 Europe/Berlin — TFT-only hardware MODEM START stalls Telnet (FAILED)

User built/flashed new **esp32dev-tft-only** variant, confirmed
shared Web UI works immediately post-reboot. CP/M,TFT were running.
Beginning Telnet+MODEM test yielded an unexpected *temporary freeze*
of CP/M and unresponsive `/api/system/memory`; after a delay the
Telnet session disconnected. HP/TFT eventually recovered and memory
endpoint worked, but no BBS dial was reached. Do NOT assume modem/BBS
stability or propose promoting/merging the variant. User describes
this as worse than prior full-bundle run.

| Sample | heap_free | free_8bit | largest_8bit | min_free_8bit | modem | Telnet |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| TFT-only after boot, uptime 120875 ms | 67664 | 28488 | 26612 | 7092 | 0 | 0 |
| TFT-only Telnet, uptime 154055 ms | 65372 | 26196 | 25588 | 7092 | 0 | 1 |
| After trying MODEM, uptime 318189 ms | 59572 | 20396 | 14836 | 5388 | 2 | 0 |

Feature flags confirmed by firmware: `web_terminal_enabled=false`,
`tft_terminal_enabled=true`. Stage `cpm_boot_ready` snapshot:
`total=81544, free8=42368, largest8=34804`, significantly better
than prior full-bundle stage `total=69576, free8=30400,
largest8=23540` (+11968 free8). But live post-boot heap
was only ~848 bytes ahead of prior full firmware (`28488 vs 27640`);
other runtime allocations/active browser traffic/Media Manager
operation may have obscured the startup saving; not demonstrated
which allocations yet. Startup-only snapshots are not identical
conditions. Prior full with Telnet had `free8=25048`; TFT-only
has `26196` (+1148). Prior full after MODEM had `free8=16228`
with Telnet still connected; TFT-only had `20396` after Telnet
had disconnected, so cannot compare directly.

Task unused stack after attempted MODEM: web 1104, z80_cpu 2888,
console **988**, hp_keyboard 2124, spi_disk 6104, modem 4428,
tft 2984. Console high-water nearly 1 KiB but no confirmed overflow.
Low-water `min_free_8bit` was **7092 even before MODEM** and 5388
after; unlike prior 268-B low-water, this does NOT itself prove
global RAM exhaustion as the cause of this particular stall.
MODEM task state=2 and no dial refusal means startup completed.

**Important diagnostic available WITHOUT a reflash:** TFT-only build
preserves `/z80debug`. The console routing code in `serial.h` emits
`Telnet TX stalled for 2 s; disconnecting slow client` into that
rolling 2048-byte log when its 2s Telnet send guard fires.
Mac: `curl -sS --max-time 5 http://192.168.178.197/z80debug`
(adjust ESP32 numeric IP) ASAP before the log rolls. If it contains
that line, the Telnet disconnect was intentional after a TX stall.
Also inspect for `CP/M modem: virtual UART 88h-8Fh online` and
web-service failures. Absence is not decisive because rolling log
can truncate. Web worker handles Telnet *accept* (not active data),
UART modem startup and Media Manager background listing, so a busy
web worker can impact new connections. Investigate overlapping
network/socket operations and synchronous `WiFiClient::write` under
memory pressure before escalating modem guards. Consider measuring
heap snapshots from cold boot **without browsing Media Manager**
to isolate background/HTTP allocations.

Media Manager after MODEM displayed blank tables, `Loading...`,
and a controlled low-8-bit-memory 503 message. That response is
expected under the current 24KiB free8 preflight and is not
a separate directory or disk failure. But UI still has an obsolete
`Z80 Terminal` navigation button in TFT-only and `Loading...`
does not clear on 503; small UX issues to fix in next source revision.

**Status:** three-variant static/CI regression compiled, but
TFT-only practical hardware regression FAIL, no further OTA
until modem/Telnet stability is better understood. Preserve main,
recovery firmware and CP/M disk images. Exact ESP32 flashed SHA
was not captured in this report.

---
## 2026-10-23 — Three firmware flavours implemented on feature branch (hardware UNTESTED)

User explicitly approved proceeding with three compile-time PlatformIO
firmware options, keeping the existing HP CRT/management console and common
CP/M codebase. Added `FIRMWARE_VARIANTS.md` with complete build and
physical acceptance procedure.

| PlatformIO env | ST7796 TFT | Browser /z80 & /z80ws | Common web services /ws, /crt, /cpmmedia, OTA | Telnet |
| --- | --- | --- | --- | --- |
| `esp32dev` | No | Yes | Yes | Yes |
| `esp32dev-tft-only` | Yes | No | Yes | Yes |
| `esp32dev-tft` | Yes | Yes | Yes | Yes |

Existing `esp32dev` and `esp32dev-tft` targets are intentionally
retained for backward compatibility; the sole new PlatformIO environment
is `esp32dev-tft-only`. Independent
`EBTKS_WEB_TERMINAL_ENABLE=0/1` flag omits Web-Terminal HTML, socket object,
and queue/cleanup processing in TFT-only; **do not accidentally omit**
`/crt`, `/ws`, Media Manager, modem, OTA, CP/M disk, HP keyboard, or
generic Z80 status/reset endpoints. Web server home page substitutes a
TFT/Telnet notice for the absent browser terminal button.

Static regressions `tests/check_firmware_variants.py` examine the actual
linked firmware binaries for route/page inclusion, not merely header
defines; the Git SHA build check now covers three images, and both TFT
ELFs are checked against the 124,544-byte static DRAM ceiling.
CI `.github/workflows/z80-core-native.yml` builds all three, reports
static memory for each, and retains the legacy headless SPI-read-ahead-off
regression test.

Initial full native+three-build CI at source commit
`2d446fdc2780264ea9bc83aa7db855610006c1c2`: GitHub Actions
run **39982662950** completed SUCCESS, including headless/full/TFT-only
links and binary feature isolation; full TFT DRAM was 124,536 bytes.
Subsequently CI was extended to check the TFT-only DRAM budget too,
and a user-facing variant guide was added. **Verify newest CI run and
Mac local clean-link before asking for a flash.** CI alone cannot
prove OTA/actual runtime memory improvement or physical display
stability. No merge to main and no Teensy/HP source or recovery-image
changes.

Next steps: macOS `git fetch && git switch feature/cpm85-management-console
&& git pull --ff-only`, then clean-build `pio run -e esp32dev-tft-only`,
compare local Git SHA with `/api/system/health` after OTA,
measure `/api/system/memory` feature booleans and heap before modem,
after ordinary BBS, and confirm Web Terminal absent while /crt,
Media Manager and OTA still accessible. Then headless `esp32dev`
(no physical TFT) and finally full `esp32dev-tft` (existing full
behaviour). Keep Stage A overall acceptance OPEN until all
hardware runs pass; defer CPFPUT/CPFGET and external FPGA.

---

## 2026-10-23 mDNS latency proven; GUI low-memory failure confirmed

After modem via Telnet, user opened the browser Media Manager: the
UI remained at `Loading...` with empty drive/media tables and displayed
`Media Manager unavailable: ESP32 memory low; close terminal or modem
connection and retry`. This confirms that the 112-byte HTTP 503
response truly was the explicit low-heap admission guard, and that
the browser correctly displays the error without crashing.

User ran the same GET to `/api/system/health` using curl timing
metrics with mDNS hostname vs numeric IPv4:

| Address | DNS (s) | TCP connect time (s) | First byte (s) | Total (s) |
| --- | ---: | ---: | ---: | ---: |
| esp32_ebtks.local | 5.005906 | 5.080385 | 5.097292 | 5.097528 |
| 192.168.178.197 | 0.001506 | 0.101995 | 0.116745 | 0.116817 |

Thus ~5.006 s was **macOS mDNS lookup latency**, NOT server HTTP
handler/Teensy processing in this health test. Following connect,
health endpoint first byte arrived in ~17 ms using `.local`, ~15 ms
using numeric IP. Ping `esp32_ebtks.local` resolved to 192.168.178.197
and received all four packets (33–92 ms). User also reported one
Wi-Fi association failure that required reboot; not necessarily
related to mDNS and not root-caused. Prefer using numeric IP for
next test, or DHCP address reservation if persistent.

Do not interpret previous ~5-6 s wall-clock curl totals as evidence
of a ~6 s server-side Media Manager function; measure the individual
`time_namelookup`, `time_connect`, `time_starttransfer` before
making latency claims. A large file JSON build/Teensy SD query
still needs performance measurement separately.

Media listing under modem+Web/Telnet load is still deliberately
unavailable because the small 8-bit memory reserve is exhausted.
Testing outcome: safe rejection and browser rendering CONFIRMED;
simultaneous full operation / modem memory pressure not solved.
Do not merge Stage A yet. Avoid unnecessary code changes or more
risky stress tests. Planned next phase remains the three compile-time
firmware variants (TFT-only, WebTerm/headless, full), eventually
complete file-transfer management console and optional FPGA variant.

---
## 2026-10-23 asynchronous Media Manager hardware report — 202/200 and low-memory 503 WORK

User flashed the feature branch version with `CpmMediaAsync` background job.
After reboot, three successive
`curl http://esp32_ebtks.local/api/cpm/media` requests:
1. HTTP **202**, **41 bytes**, **5.046570 s**
2. HTTP **202**, **41 bytes**, **5.112937 s**
3. HTTP **200**, **5079 bytes**, **5.887209 s**;
   headers `Content-Length: 5079`, `Cache-Control: no-store`,
   `Content-Type: application/json`; Python JSON validator **VALID**.
Thus asynchronous request/poll/result handoff succeeds on physical ESP32.

After an ordinary BBS MODEM session **via Telnet**, another GET returned
HTTP **503**, **112 bytes**, **5.070838 s** (no empty reply or timeout).
The 112-byte length **exactly matches** the source's low-memory preflight
error `{\"ok\":false,\"error\":\"Media Manager unavailable: ESP32 memory low; close terminal or modem connection and retry\"}`;
the refusal is deliberate, not a broken 200/empty response.
The preceding `JSON VALID` command was run before the failed/load
curl and tested a prior `/tmp/cpm-media.json`, not this 503.
Actual HTTP 503 response body not pasted, but exact 112-byte match
identifies the low-memory guard with high confidence.

**New timing clue:** All five observed responses (two 202, one 200,
one 503, plus previous data) each cost approximately **5 seconds**,
including immediate 202/503 callbacks. This suggests pre-handler
DNS/mDNS resolution of `esp32_ebtks.local` or connection establishment
(hypothesis, not yet verified), rather than ~5s of synchronous API
logic. Before changing firmware measure `curl -w` timings for
`time_namelookup`, `time_connect`, `time_starttransfer`, `time_total`,
and compare `.local` URL with numeric ESP32 IP.

Status: Media Manager idle + safe-refusal hardware tests PASS; media
listing remains intentionally **unavailable** under modem memory
pressure, not accepted as simultaneous full operation. The prior
full-freeze scenarios and persistent extremely low 8-bit heap
deserve caution. No additional code changes for this report;
keep Stage A feature branch unmerged; next planned milestone is
three firmware variants once wider regression is sufficiently safe.

---
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
