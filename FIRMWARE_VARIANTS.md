# ESP32 CP/M firmware variants — development branch

One **shared** ESP32/Teensy/HP-85 CP/M codebase builds four independent
display configurations. No separate forks and no firmware changes on the
Teensy or HP LOADBIN are necessary.

| PlatformIO environment | Physical ST7796 480x320 TFT | Browser Z80 terminal (`/z80`, `/z80ws`) | Telnet port 23 |
| --- | --- | --- | --- |
| `esp32dev` | No | Yes | Yes |
| `esp32dev-tft-only` | Yes | No | Yes |
| `esp32dev-tft` | Yes | Yes | Yes |
| `esp32dev-tft-no-telnet` | Yes | Yes | **No** |

The existing `esp32dev` and `esp32dev-tft` environment names remain
backward compatible. No TFT is initialized in the headless build;
its real hardware display will remain dark. In TFT-only, the home page
shows that the Z80 console is accessible by TFT or Telnet instead of
offering a dead browser-terminal link.

**All variants preserve:** Wi-Fi provisioning and mDNS, firmware OTA via
`/updater`, HP CRT mirroring via `/crt` and the **original** Teensy JSON
WebSocket `/ws`, CP/M Media Manager `/cpmmedia` and
`/api/cpm/media`, HP-85 CPM85 keyboard and management commands,
CCP safe-point/CPRESET, Z80 diagnostic/status APIs, virtual
Wi-Fi modem, SPI disk read-ahead and UART write fallback.

`EBTKS_WEB_TERMINAL_ENABLE=0` compiles out the **entire** `/z80` HTML,
the independent `/z80ws` WebSocket object/handler, buffering/flow
control and client-cleanup code. The shared CRT and Media Manager
were deliberately retained in the normal web-server registration
path. `EBTKS_TELNET_ENABLE=0` compiles out the port-23 listener, TCP client,
nonblocking 1-KiB Telnet transmission queue and the Telnet input parser.
It does **not** remove the Wi-Fi modem's separate outgoing TCP connection
or the HTTP/WebSocket server. The new `telnet_enabled=false`,
`telnet_listener_mode="disabled"` and `telnet_connected=false` values
are exposed on `/api/system/memory` for this build.
`EBTKS_TFT_TERMINAL_MIRROR=0` uses the existing no-op TFT mirror
implementation and never creates the LovyanGFX panel or its task.
`/api/system/memory` exposes `web_terminal_enabled` and
`tft_terminal_enabled` as explicit runtime capability flags.

## Build-time gzip static Web UI pages and repaired Home link

All five firmware-embedded HTML sources have **deterministic
gzip copies generated at PlatformIO build time**:
Home (variant-aware Web Terminal/Telnet/TFT link),
updater **fallback** page, HP CRT (`/crt`),
CP/M Media Manager (`/cpmmedia`), and Z80 Terminal (`/z80`).
Editable JavaScript and markup stay in existing C++ source
HTML literals; `tools/pio_gzip_pages.py` extracts those bytes
and creates build-directory `ebtks_static_pages_gzip.h`.
Source files and generated gzip arrays are both in flash:
no runtime compression, no large HTML heap String, and
no SPIFFS change. HTTP `Accept-Encoding: gzip` selects
the compressed bytes, `Content-Encoding: gzip` and
`Vary: Accept-Encoding`; otherwise the unchanged identity
HTML is served. OTA POST uploads and all JSON/WebSocket
routes stay untouched. Existing SPIFFS `/updater.html`
overrides the fallback exactly as before; `/style.css`
is likewise still served from SPIFFS, not repackaged.

| Source | Original HTML bytes | Gzip bytes |
| --- | ---: | ---: |
| Home, Web Terminal variant | 936 | 546 |
| Updater fallback | 694 | 455 |
| HP-80 CRT | 10357 | 3680 |
| CP/M Media Manager | 6841 | 2756 |
| Z80 Terminal | 18581 | 6969 |

The Home Page markup defect had literal text
`\") EBTKS_HOME_Z80_LINK R\"HTML(` from an improperly
closed raw string. Repaired with adjacent C++ raw/string
literals around the macro, so the Home Page now includes a
real working Z80 Terminal button in browser-enabled builds,
or TFT/Telnet explanatory text in the relevant other targets.
`tests/check_html_gzip.py` validates decompression equivalence,
all variants, correct link and static page contents.

With the installed ESPAsyncWebServer 3.6.0, a flash page
response still uses a temporary 8-bit DRAM output buffer
in `AsyncAbstractResponse::_ack`. Serving fewer compressed
bytes bounds this allocation substantially without changing
the library or handing byte-oriented TCP buffers to unsafe
32-bit-only IRAM. If transient RAM remains too low, evaluate
a *bounded-size 8-bit buffer* as a separate experiment rather
than using `MALLOC_CAP_32BIT` alone for AsyncTCP/lwIP.

**Hardware A/B:** cold boot, snapshot `/api/system/memory`,
fetch `curl -sS --compressed -D - -o /dev/null
http://192.168.178.197/z80`, snapshot again.
`--compressed` sends `Accept-Encoding` and transparently
supports gzip; headers must show `Content-Encoding: gzip`
and `Vary: Accept-Encoding`. Repeat for `/cpmmedia` and
`/crt` only if coldboot memory margins allow. Then test
browser Home link, CRT, media listing and Web Terminal
input/output on physical hardware. Old command `curl -sS
-o /dev/null URL` by default does **not** advertise gzip
and intentionally receives original identity HTML, so
it is **not** the right A/B comparison for the new change.

---
## Before flashing

The source is on branch `feature/cpm85-management-console`; **do not**
merge into main until hardware regression passes. Keep a copy of the
stable firmware available. Use the existing OTA updater on the **ESP32**
only, not the Teensy or HP LOADBIN. Pull and clean-build the desired
target on the user's Mac:

```sh
git fetch origin
git switch feature/cpm85-management-console
git pull --ff-only

pio run -e esp32dev-tft-no-telnet -t clean
pio run -e esp32dev-tft-no-telnet
```

The binary for that target is
`.pio/build/esp32dev-tft-no-telnet/firmware.bin`.
To build TFT without the browser Z80 Terminal but **with** Telnet,
use `esp32dev-tft-only`. For the Web Terminal-only/headless target, use
`esp32dev`. For the complete TFT + Web Terminal **with** Telnet, use
`esp32dev-tft`; the binaries use their environment directory names.
**Always select the matching firmware.bin**; the OTA upload page alone
cannot distinguish which variant the user intended.

The Git SHA embedded in every firmware can be checked via
`/api/system/health`; run `git rev-parse HEAD` locally and compare.

## Early IRAM-only SPI cache experiment (fourth target only)

The `esp32dev-tft-no-telnet` environment additionally defines
`EBTKS_SPI_CACHE_EARLY_IRAM_ONLY=1`. This reserves the 8,192-byte
SPI read-ahead word cache **after Teensy UART initialization but
before Wi-Fi/Web service startup**, using the same existing
`MALLOC_CAP_32BIT|MALLOC_CAP_EXEC|MALLOC_CAP_INTERNAL` request.
The endpoint `GET /api/cpm/spireads` includes `early_iram_only=true` and
`early_cache_allocator` containing `accepted`, `free8_before`,
`free8_after`, `exec32_free_before`, `exec32_free_after`,
`exec32_largest_before`, and `exec32_largest_after`. The
diagnostic is stored on a very small heap allocation after the cache
reservation and survives the 2KiB rolling debug-log buffer.

Boot debug `/z80debug` now logs
`CP/M SPI early IRAM32 reserve: iram32` or `unavailable`,
with the free-8bit and exact intersection of executable/32-bit
allocator free/largest values before and after allocation.

If an IRAM allocation fails or would reduce ordinary 8-bit RAM,
this variant leaves its SPI read-ahead **off for that boot** and
uses the original Teensy UART disk reads. It does **not** fall back
to allocating an 8 KiB cache from normal DRAM, even if the Web UI
asks to re-enable SPI. UART disk **writes are unchanged**.
Other three PlatformIO environments retain their current SPI
cache behavior. No Teensy firmware or HP LOADBIN changes.

### First hardware gate: no MODEM/BBS until boot is verified

After flashing, cold boot and request `GET /api/cpm/spireads`
and `GET /api/system/memory`. Confirm:

- preferred outcome: `cache_storage="iram32"`,
  `early_iram_only=true`, `enabled=true`, and no significant
  8KiB reduction of free_8bit across the SPI cache stage
  (Web start now occurs **after** early reservation).
- alternate safe outcome: `cache_storage="none"`,
  `early_iram_only=true`, `boot_disabled=true`, `enabled=false`,
  and UART fallback reads work; report the boot diagnostic log
  before further changes.
- **never acceptable for the experiment**:
  `cache_storage="dram8"`.

**Critical independent gate:** prior full-minus-Telnet hardware
boot printed two `EBTKS UART transaction timeout` messages and
`FAILED` for `/cpm/cbios.rom`. Carefully examine `/z80debug` on
the new boot. If CBIOS loading reports `FAILED`, **do not
start a BBS stress test**; first provide debug output so the
Teensy UART/CPM loader issue can be isolated. A working `DIR`
or Multiplan does not negate a ROM-load failure.

If boot, CP/M and IRAM cache are healthy, capture cold-boot
`/api/system/memory`, then start MODEM using TFT/HP keyboard
without opening `/z80` or a Telnet client. Capture after MODEM
and **do not dial** until free8/largest8 margins look healthy;
compare with prior no-Telnet TFT-only configuration.

---
## Physical regression and measurement

For the new **full-minus-Telnet** variant, start with a cold boot
and only **one browser accessing `/api/system/memory`**; avoid opening
`/z80`, `/crt`, `/cpmmedia` or `/ws` initially. This matches the
control observation: during the successful prior TFT/HP BBS session the
user accessed the memory endpoint only. Record
`heap_free`, `free_8bit`, `largest_8bit`, `min_free_8bit` and the
`stack_unused_bytes` fields immediately after boot, once MODEM is
ready and after logging into the BBS on the **physical TFT / HP keyboard**.

Verify `telnet_enabled=false`, `telnet_listener_mode="disabled"`,
`telnet_connected=false`, `web_terminal_enabled=true` and
`tft_terminal_enabled=true`. From the Mac, a TCP/23 connect should
fail/refuse, e.g. `nc -G 2 -vz <ESP32_NUMERIC_IP> 23` (on macOS).
Successful `/z80` in the browser and physical TFT rendering confirm
both backends are retained, but opening a WebSocket allocates additional
runtime heap; **do not open the browser Z80 Terminal until after the
memory-endpoint-only BBS control test**.

After one normal BBS login/logout and return to CP/M, capture recovery
memory. Only if free memory and responsiveness remain adequate, open
`/z80` and test simple CP/M console input/output alongside TFT, then
repeat MODEM/BBS cautiously if needed. Stop immediately when the UI
becomes unresponsive or 8-bit memory approaches the prior hazardous
minimum. This experiment isolates the *Telnet implementation and its
extra TCP connection*, not the whole Web UI; a browser-terminal WebSocket
would itself constitute a second active networking session.

Regression criteria: HP keyboard, ANSI colors and scrolling remain
functional, `/crt`, Media Manager and OTA are reachable when system
memory allows, and the memory endpoint remains responsive. Do not
expect Media Manager to list images under its current 24 KiB
free-8bit admission guard during MODEM/BBS operation. A 503 is
controlled refusal, not a disk image failure.

For all existing targets, use the same procedure documented in the
previous acceptance plan. **Concurrent Telnet+BBS was observed to
consume memory to 644 bytes minimum and is still classified unstable.**

Static linked DRAM is audited by the CI job and
`tests/check_esp32_dram_budget.py` on **all three** TFT targets;
the limit is 124,544 bytes, based on the Mac toolchain's prior
linker overflow. This is *not* runtime free memory. CI also checks
that TFT-only omits `/z80ws` and the large terminal page, and
that full-minus-Telnet lacks the Telnet listener and console send path,
and that all variants retain the common server routes.

## Deferred

Do not implement a Tang Nano/FPGA backend or modify the management
file-transfer `CPFPUT`/`CPFGET` service as part of this change.
Those follow firmware variant regression and memory profiling.
