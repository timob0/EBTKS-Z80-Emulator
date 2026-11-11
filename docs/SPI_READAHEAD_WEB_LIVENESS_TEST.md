# SPI-enabled web liveness diagnostic (2026-10-18)

**Branch:** feature/spi-readahead-liveness-diagnostics, from feature/media-manager-api-cleanup. **ESP32 physically built, flashed, and tested successfully by user on 2026-10-18.**

## Hardware regression PASSED — SPI read-ahead plus cross-page navigation (2026-10-18)

The user built/flashed this exact feature branch and reported **working Z80 Terminal, Remote Display, Media Manager, and Telnet** before and after enabling SPI read-ahead, performing CP/M DIR through Telnet, and navigating back and forth between the pages. The previous ~20-second outage/Telnet disconnect **did not recur**. ESP32 uptime increased continuously (22.9s → 56.4s → 95.0s → 152.6s), so **no ESP32 reset occurred during this test**. The returned reset_reason was **3 (ESP_RST_SW, prior software reset)**, not a new crash.

| Measurement | SPI OFF (boot) | SPI ON, before DIR | SPI ON, after DIR | After page navigation |
| --- | ---: | ---: | ---: | ---: |
| uptime_ms (seconds) | 22.9 | 56.4 | 95.0 | 152.6 |
| heap_free (bytes) | 101192 | 92828 | 80160 | 79460 |
| heap_min_free (bytes) | 80324 | 80324 | 71576 | 56532 |
| largest_free_8bit_block (bytes) | 49140 | 40948 | 29684 | 28660 |
| spi_cache_allocated | false | true | true | true |
| spi_worker_created | false | false | true | true |
| spi_dma_initialized | false | false | true | true |

Interpretation: enabling mode retains an ~8 KiB cache (8364 free-heap reduction); performing DIR allocates/initializes worker and SPI DMA (~12668 B net reduction); repeated HTTP navigation leaves free heap nearly steady (only 700 B below after-DIR observation). The **minimum** heap did fall by ~15 KiB transiently during navigation (71576 → 56532), so it is worthwhile to retain the telemetry and avoid claiming there is no runtime pressure. The post-navigation **largest allocatable 8-bit block 28,660 B** remains above the former full-page allocation sizes; direct flash response avoids those allocations. This strongly supports heap-heavy AsyncBasicResponse as the earlier navigation trigger, but does not prove whether the original failure was heap exhaustion, fragmentation, exception, or watchdog.

**Decision:** preserve this feature branch as **the new hardware-tested web-liveness checkpoint**; no additional SPI/Teensy modifications. The unfinished SPI-backed HTTP image stream remains disabled. Next development milestone is the separately gated, ACK-controlled binary download transport outlined in `MEDIA_MANAGER_API_REDESIGN.md` (inherited from cleanup branch), starting with RAM-only tests before any live SPI. The stable `main` branch has not been changed.

## User's new observation

- On power-up all pages, Z80 terminal, and CRT function.
- Enabling SPI read-ahead while on Media Manager improves live CP/M Telnet directory speed, as expected.
- Only when navigating away from Media Manager to a different page does web service fail, with Telnet disconnected. After ~20 seconds the ESP resumes. This suggests a reboot rather than persistent deadlock, but reset cause/uptime have not yet been measured.
- This failure occurred even after removing the experimental SPI-backed HTTP download worker and route. Do not resurrect that route.

## Suspect: response memory pressure

- Media Manager already serves its large HTML directly from flash using AsyncProgmemResponse and remains responsive after switching SPI mode.
- Z80 Terminal and CRT still used AsyncBasicResponse, which copies their ~15.7 KiB and ~10.4 KiB HTML bodies into a heap String and allocates substrings as it transmits them. Home/updater fallbacks likewise used that method.
- Enabling SPI allocates a retained 8 KiB cache. First live CP/M SPI read creates an additional 8 KiB-stack FreeRTOS worker and initializes SPI DMA buffers. The resulting heap pressure/fragmentation could make large AsyncBasicResponse operations fail when the user navigates away from Media Manager. This remains a hypothesis; the failure could instead be stack overflow, panic, watchdog, or another resource problem.

## Diagnostic branch changes

1. CRT, Z80 Terminal, Home, and updater fallback now serve static HTML directly from flash with length-based AsyncProgmemResponse. Media Manager already uses the same strategy.
2. Added read-only RAM-only GET /api/system/health endpoint with uptime, numeric esp_reset_reason plus watchdog/panic/brownout flags, free/min heap, largest 8-bit allocatable block, internal heap, and live SPI-cache, worker, DMA state indicators.
3. Media Manager shows that health summary on page load, after changing SPI mode, and via a Refresh system health control.
4. No SPI worker, DMA, frame protocol, 4 MHz timing, Wi-Fi configuration, Teensy firmware, or HP interrupt changes. Compatibility download and verified upload preserved. The SPI-backed HTTP image stream remains quarantined.
5. Browser JavaScript parsed successfully and modified C++ source balance checked, but not yet compiled or tested on hardware.

## One controlled hardware test

1. Pull/build/flash only this ESP32 feature branch. Keep CP/M idle and open /cpmmedia. Record initial System memory and last reset lines (or GET /api/system/health).
2. Click Enable SPI read-ahead, stay on the Media Manager, and record the updated memory/worker/DMA state.
3. Perform a normal CP/M directory listing through an existing Telnet session. Back in the same Media Manager tab, click Refresh system health, record heap and largest block. Note that the first SPI disk read may create the worker/DMA buffers only now.
4. Navigate once to /z80, then /crt; check both pages and Telnet remain responsive. Return to Media Manager if they do.
5. If the ESP becomes unresponsive, do NOT immediately power-cycle. After it resumes (~20s), open GET /api/system/health and send complete JSON. Reset reason and uptime distinguish reboot/panic/watchdog from a stuck TCP task. After recovery, minimum heap will reflect a new boot; preserve the preceding heap readings.
6. If no problem, confirm /, /cpmmedia, /cpmdiag, and /api/cpm/media still function. Do not try 8 MiB transfers until liveness is confirmed.

Do not claim heap exhaustion or a watchdog as proven without reset evidence. Avoid serial logging on ESP32 GPIO1/3, reserved for Teensy UART.
