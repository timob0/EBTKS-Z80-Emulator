# ESP32 CP/M firmware — two supported build targets

As of 23 October 2026, the ESP32 project uses **two** PlatformIO
environments, both containing the complete Z80/CP/M service bundle.
They differ **only** in whether physical ST7796 TFT output is compiled
in. Neither enables an inbound Telnet listener by default.

| Target | ST7796 TFT output | Browser Z80 Terminal | Inbound Telnet port 23 | Build |
| --- | --- | --- | --- | --- |
| `esp32dev` | No | Yes | Off | `pio run -e esp32dev` |
| `esp32dev-tft` | Yes, 80x24 ANSI/VT100/CP437 | Yes | Off | `pio run -e esp32dev-tft` |

**No display required:** The headless `esp32dev` firmware provides
full CP/M via `http://<ESP32-IP>/z80`. A user can add an ST7796 later
and switch to the `esp32dev-tft` firmware; the CP/M codebase, Teensy
storage and HP-85 integration are the same. Do not flash the TFT firmware
until the TFT is wired and powered correctly.

Both targets retain Wi-Fi provisioning/mDNS, `/updater` OTA,
`/crt` HP-80 Remote Display, **the original `/ws` Teensy
WebSocket**, the independent Z80 console `/z80ws`, gzip-compressed
HTML pages, CP/M Media Manager `/cpmmedia`, modem/BBS ANSI colors,
HP keyboard bridge, CCP-safe point and management APIs, read-only
diagnostics, CP/M disk mounts, and the hardware-accepted
SPI read-ahead with UART fallback and UART disk writes.
The modem's task and buffers are allocated on first guest modem
I/O and released after returning to the verified CP/M CCP prompt.

## Optional Telnet: one compile-time switch, no third target

Telnet is now **off** by default in
`ESP32_Z80_Emulator/ebtks_firmware_options.h`:

```cpp
#ifndef EBTKS_TELNET_ENABLE
#define EBTKS_TELNET_ENABLE 0
#endif
```

Advanced users may add `-DEBTKS_TELNET_ENABLE=1` to the
`build_flags` for **either** `[env:esp32dev]` or
`[env:esp32dev-tft]` in `platformio.ini` before rebuilding.
The TFT target inherits the headless target's common build flags,
so adding it to `[env:esp32dev]` enables it on both.
Alternatively, for a temporary headless one-off build without
editing the file, specify the complete common flag set:

```sh
PLATFORMIO_BUILD_FLAGS="-DEBTKS_CPM85_KEYBOARD_ENABLE=1 -DEBTKS_WEB_TERMINAL_ENABLE=1 -DEBTKS_SPI_CACHE_EARLY_IRAM_ONLY=1 -DEBTKS_TELNET_ENABLE=1" pio run -e esp32dev
```

Telnet uses the same CP/M console; the accepted default uses
`EBTKS_TELNET_SHARED_WORKER=1` (shared web accept task, not a
dedicated idle 3 KiB Telnet worker). This optional inbound
connection competes with Web Terminal, HTTP, and outgoing MODEM
for scarce 8-bit ESP32 RAM; concurrent Telnet+BBS was previously
unstable under very low memory. Treat the opt-in as an **advanced
option requiring a separate physical stress test**. Do not infer
that a successful compile proves combined-network stability.

Both default firmware binaries completely omit the TCP/23
listener and related Telnet output/input code.
`/api/system/memory` reports `telnet_enabled=false`,
`telnet_listener_mode="disabled"`, and
`telnet_connected=false` by default.

## Hardware-accepted SPI cache shared by both targets

Both `esp32dev` and `esp32dev-tft` inherit

```ini
-DEBTKS_SPI_CACHE_EARLY_IRAM_ONLY=1
```

They reserve the 8,192-byte CRC read-ahead word cache in aligned
32-bit-only executable/internal RAM **before Wi-Fi and web tasks
start**, avoiding the previously memory-constrained DRAM fallback.
If that early allocation fails, fast disk reads stay disabled and
the existing UART disk reader works instead. Disk writes remain
UART-only. The old option-specific experimental flag is now
part of the common accepted bundle.

To check on the physical device:
`GET /api/cpm/spireads` should report
`early_iram_only=true`; ordinarily
`cache_storage="iram32"`, `enabled=true`, and
`early_cache_allocator.accepted=true`. No DRAM cache fallback
is allowed. The Mac linker static DRAM guard for the TFT
remains 124,544 bytes in CI.

## Building, flashing and acceptance checks

```sh
git fetch origin
git switch feature/cpm85-management-console
git pull --ff-only

# Without display:
pio run -e esp32dev -t clean
pio run -e esp32dev

# With ST7796 display:
pio run -e esp32dev-tft -t clean
pio run -e esp32dev-tft
```

Upload `.pio/build/esp32dev/firmware.bin` or
`.pio/build/esp32dev-tft/firmware.bin` using the existing
ESP32 browser updater. **Do not flash a historical
`esp32dev-tft-no-telnet` binary for this revised two-target
configuration.** No Teensy or HP Option ROM firmware update
is required. Check the compiled revision against
`/api/system/health` `git_commit` as needed.

Hardware regression: confirm boot and CP/M `DIR`, Multiplan,
the Home Page Z80 button, browser terminal ANSI/CP437, Media
Manager drive/image listings, HP CRT and `/ws`, OTA page
availability and MODEM dial/logoff/exit/restart. On leaving
MODEM for the CP/M prompt, `modem_state` should return
to 0 and Media Manager should work without relaxing its
heap guards. Physical acceptance of the **same functionality**
was completed on the former `esp32dev-tft-no-telnet` build;
renamed target and new headless shared IRAM cache still
require their own build and boot confirmation.

CI builds exactly the two default targets, checks shared
features/TFT isolation/no Telnet, checks the Mac static DRAM
budget, and additionally smoke-builds Telnet as a compile
switch **without introducing a named third target**.

## Historical variants

`esp32dev-tft-only` (no browser terminal) and
`esp32dev-tft-no-telnet` (former accepted experimental
TFT configuration) were **removed as build environments**.
Their source-history commits and prior hardware test notes
remain in `NEXT_CHAT_STAGE_A_CHECKPOINT.md` and Git history;
they are not new recommended build flavors.

There has been **no merge to `main` or modification of the
user-maintained `recovery`** as part of this cleanup.
