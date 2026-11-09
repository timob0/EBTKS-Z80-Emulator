# B2c diagnostics — first hardware test (2026-11-09)

## Why this exists

Hardware-accepted B2b copied CP/M A:STAT.COM -> Teensy SD
`/cpm/transfers/A_STAT.COM` (42 records, 5,376 bytes, CRC16 54346).
The first B2c SD -> CP/M import intermittently coincided with loss of the
ESP32 Wi-Fi association / apparent full ESP32 reboot. On later attempts
`teensy_sd_source_failed` was reported without its underlying reason.
The CP/M destination A:IMPTST.COM was not found.

No root cause has been established. In particular, 4 KiB CPU-task stack
exhaustion, ESP32 EN reset, and orphaned Teensy read session are hypotheses,
not findings.

## Safety / scope

- Changes only on ESP32 and Teensy `feature/cpm-file-transfer-b2c`.
  No `main` merge and no HP ROM change.
- Diagnostic `POST /api/cpm/service/source?name=A%3ASTAT.COM` requests a
  verified native CCP blank-line safe point. The pinned CPU task then asks
  Teensy to open, scan, CRC and **release** the published read-only SD source.
  There are **no additional CP/M filesystem BDOS calls** in this preflight (ordinary CCP console input still runs).
  The source is not modified.
- The HTTP handler never blocks on Teensy UART, calls BDOS or touches disk.
- A source session abandoned by an ESP32-only reset may be explicitly
  released by `recover=READ_ONLY_SESSION`, but ONLY if source name matches.
  Recovery is never automatic and operates on the Teensy's read-only descriptor.
- The pre-existing import (CP/M WRITE) is deliberately **not** part of
  the first hardware test. Keep a backup of writable A.dsk for later tests.
- RTC_NOINIT stage tracing consumes no regular DRAM/BSS or extra task.
  A preserved trace is helpful, but an EN reset or real power loss may erase it.

## Build gate (both repositories)

Teensy (`timob0/ebtks-alt-firmware`):
```sh
git fetch origin
git switch feature/cpm-file-transfer-b2c
git pull --ff-only
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Itest/stubs -Iinclude test/cpm85_sd_import_native.cpp -o /tmp/cpm85_sd_import_native
/tmp/cpm85_sd_import_native
pio run -e teensy41
```

ESP32 (`timob0/EBTKS-Z80-Emulator`):
```sh
git fetch origin
git switch feature/cpm-file-transfer-b2c
git pull --ff-only
python3 tests/check_b2c_diagnostics.py
python3 tests/check_web_ui_cleanup.py
bash tests/run_z80_core_native.sh
pio run -e esp32dev
pio run -e esp32dev-tft
python3 tests/check_esp32_dram_budget.py
```

Check both GitHub Actions workflows, especially native tests and PlatformIO
TFT static DRAM limits. These source changes have not yet been confirmed with
local Mac builds or physical hardware. **Do not flash any failing build.**
Preserve known-good recovery binaries. Once all builds pass, flash the
Teensy B2c firmware first, then ESP32 B2c TFT firmware using the normal
known-working update procedure. An ESP32-only OTA leaves the previous Teensy
UART protocol incompatible with the new `release` request.

## Read-only first hardware test (do not run IMPORT)

Leave native CP/M idle at `A>`. Do not type DIR or start an application
during the preflight. Replace IP if DHCP changed:

```sh
ESP32_URL=http://192.168.178.197
curl -sS "$ESP32_URL/api/system/health"
curl -sS "$ESP32_URL/api/cpm/service/diag"
curl -sS "$ESP32_URL/api/cpm/service/status"

curl -i -sS -X POST \
  "$ESP32_URL/api/cpm/service/source?name=A%3ASTAT.COM"
curl -sS "$ESP32_URL/api/cpm/service/source"
curl -sS "$ESP32_URL/api/cpm/service/diag"
curl -sS "$ESP32_URL/api/system/health"
```

POST HTTP 202 is only **accepted**, not completed. Poll GET until
`state=passed` or `state=failed`.

Expected preflight success:
`operation=sd_source_preflight`, `state=passed`, `source_error=none`,
`file_error=none`, `expected_records=42`,
`expected_crc16_ccitt_false=54346`; no new CP/M file,
no CP/M disk modification, no processor reset.

A failed preflight should now expose a specific `source_error`, e.g.
`source_not_found`, `source_must_be_1_to_63_records`,
`import_in_progress`, or `uart_transaction_failed`.
`bdos_search_failed` is a legacy **generic** failure checkpoint string;
it does not mean BDOS ran during source preflight.

Only if `source_error=import_in_progress` and no import remains active,
an explicit recovery can close the matching read-only session and
immediately rerun preflight:
```sh
curl -i -sS -X POST \
  "$ESP32_URL/api/cpm/service/source?name=A%3ASTAT.COM&recover=READ_ONLY_SESSION"
curl -sS "$ESP32_URL/api/cpm/service/source"
```
Do not use recovery speculatively or during any running transfer.

If the ESP32 restarts, record Wi-Fi disappearance, time, and the
`/api/system/health` and `/api/cpm/service/diag` output after reconnect.
`before_last_esp_boot.valid=false` means the RTC snapshot was not
retained, **not** that the import was never entered. A reset reason of
`ESP_RST_POWERON` is compatible with a genuine power-on or EN reset.

Stop and share the preflight results **before** starting the writable B2c
import or merging any branch into main.
