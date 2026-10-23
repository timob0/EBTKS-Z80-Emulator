# ESP32 CP/M firmware variants — development branch

One **shared** ESP32/Teensy/HP-85 CP/M codebase builds three independent
display configurations. No separate forks and no firmware changes on the
Teensy or HP LOADBIN are necessary.

| PlatformIO environment | Physical ST7796 480x320 TFT | Browser Z80 terminal (`/z80`, `/z80ws`) | Telnet port 23 |
| --- | --- | --- | --- |
| `esp32dev` | No | Yes | Yes |
| `esp32dev-tft-only` | Yes | No | Yes |
| `esp32dev-tft` | Yes | Yes | Yes |

The existing `esp32dev` and `esp32dev-tft` environment names remain
backward compatible. No TFT is initialized in the headless build;
its real hardware display will remain dark. In TFT-only, the home page
shows that the Z80 console is accessible by TFT or Telnet instead of
offering a dead browser-terminal link.

**All variants preserve:** Wi-Fi provisioning and mDNS, firmware OTA via
`/updater`, HP CRT mirroring via `/crt` and the **original** Teensy JSON
WebSocket `/ws`, CP/M Media Manager `/cpmmedia` and
`/api/cpm/media`, HP-85 CPM85 keyboard and management commands,
CCP safe-point/CPRESET, Z80 diagnostic/status APIs, Telnet, virtual
Wi-Fi modem, SPI disk read-ahead and UART write fallback.

`EBTKS_WEB_TERMINAL_ENABLE=0` compiles out the **entire** `/z80` HTML,
the independent `/z80ws` WebSocket object/handler, buffering/flow
control and client-cleanup code. The shared CRT and Media Manager
were deliberately retained in the normal web-server registration
path. `EBTKS_TFT_TERMINAL_MIRROR=0` uses the existing no-op TFT mirror
implementation and never creates the LovyanGFX panel or its task.
`/api/system/memory` exposes `web_terminal_enabled` and
`tft_terminal_enabled` as explicit runtime capability flags.

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

pio run -e esp32dev-tft-only -t clean
pio run -e esp32dev-tft-only
```

The binary for that target is
`.pio/build/esp32dev-tft-only/firmware.bin`.
To build the Web Terminal-only target, replace the target with
`esp32dev`. For the complete TFT + Web Terminal build, use
`esp32dev-tft`; the respective binaries use those directory names.
**Always select the matching firmware.bin**; the OTA upload page alone
cannot distinguish which variant the user intended.

The Git SHA embedded in every firmware can be checked via
`/api/system/health`; run `git rev-parse HEAD` locally and compare.

## Physical regression and measurement

Test **one variant at a time**; cold-boot between tests. Begin with
TFT-only on hardware with an attached screen, then WebTerm-only, then
the full bundle. On each variant:

1. After reboot, inspect `/api/system/memory`. Record
   `heap_free`, `free_8bit`, `largest_8bit`,
   `min_free_8bit`, the firmware feature booleans and stack values.
   Use the ESP32 numeric IP to avoid the Mac's observed 5-second
   `esp32_ebtks.local` mDNS resolution delay.
2. Verify normal CP/M commands `DIR`, Multiplan, and `CPSTATUS` /
   `CPDIMGS` on the HP-85. Check `/crt`, Media Manager, the firmware
   updater page and Telnet port 23. Verify that the disk mounts and
   images are unchanged after OTA.
3. In **TFT-only**, check ANSI color/CP437 scrolling on TFT;
   `/z80` should not exist and `/z80ws` should not be registered.
   The HP CRT page `/crt` must still work. In **WebTerm-only**,
   confirm browser colors/key input and that no TFT task is running.
   In **full**, check both displays.
4. Run an ordinary MODEM/BBS session and return to CP/M without
   rebooting. Inspect `/api/system/memory` again and verify
   Telnet/CRT/UI remain responsive. Test a second BBS session only
   after confirming the first left sufficient RAM. Never stress the
   complete configuration into a known near-OOM condition to gather data.
5. The Media Manager's asynchronous `/api/cpm/media` may initially
   return HTTP 202, then HTTP 200 valid JSON. Under memory pressure
   an explicit JSON HTTP 503 is expected and safer than a timeout.
   The full firmware's prior simultaneous-load issue remains OPEN.

Static linked DRAM is audited by the CI job and
`tests/check_esp32_dram_budget.py` on **both** TFT targets;
the limit is 124,544 bytes, based on the Mac toolchain's prior
linker overflow. This is *not* runtime free memory. CI also checks
that the TFT-only binary omits `/z80ws` and the large terminal page,
and that all variants retain the common server routes.

## Deferred

Do not implement a Tang Nano/FPGA backend or modify the management
file-transfer `CPFPUT`/`CPFGET` service as part of this change.
Those follow firmware variant regression and memory profiling.
