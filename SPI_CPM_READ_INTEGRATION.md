# CP/M read-only SPI read-ahead trial — Multiplan benchmark

**Status (2026-10-18): Source committed on ESP32 `feature/cn3-spi-read-test`, NOT yet locally compiled, flashed, or hardware-tested as CP/M transport.** The Teensy `poll_low_v2` framed stream sender has already passed the dedicated SD→SPI diagnostics and **does not change in this milestone**. Keep your known-good ESP32 and Teensy binaries, and finish your UART Multiplan baseline measurements **before flashing**.

## Design and safety

This is an *opt-in* replacement for **CP/M virtual disk reads only**. The established `diskRead()` → `cpmMapMountedSector()` → `cpmMountedDiskRead()` mapping and per-drive mounted media table remain the same. SPI is used **only** when enabled at `/cpmmedia` (or via `POST /api/cpm/spireads?enabled=1`). Otherwise the original UART/JSON/Base64 path is used. Default after every ESP32 boot: **UART**.

### Read-ahead, protocol and correctness

- On an SPI cache miss, fetch an **aligned 16 × 512-byte window** (8 KiB) from the correctly mounted image file using the hardware-verified **4 MHz SPI MODE0 / `poll_low_v2`** framed protocol. If the requested sector is too close to the exposed medium's end for a full window, fetch one sector instead.
- The disk source is the **actual mounted image path**, not an assumed `A.dsk` filename. Native 8 MiB media are bounded by 16,384 × 512-byte sectors; HP 82900 LIF CP/M media by 512 × 512-byte sectors. Existing BIOS-to-file-sector mapping remains authoritative.
- The ESP32 creates one persistent core-0 `SpiDisk` worker when first needed; the Z80 core-1 CPU task waits for the result. Only the core-0 worker accesses ESP32 SPI2 slave DMA. No Teensy GPIO IRQ is attached: the HP-85 bus still owns `IRQ_GPIO6789`.
- Each 528-byte SPI frame is checked for protocol version, source type, session, sequence, payload length, header CRC and data CRC. The final Teensy UART reply's aggregate payload CRC and frame count must also match.
- **The cache window becomes valid only after the entire batch passes.** A CRC error, DMA short transfer/timeout, missing final UART reply or incompatible Teensy firmware causes the SPI path to disable itself for the remainder of that ESP32 boot. A fallback to the **unchanged UART/Base64 CP/M read** is attempted; a bad partial SPI frame is never exposed to the Z80.
- **All writes remain on the existing UART/Base64 `disk_write` service.** Cache is invalidated **before every CP/M disk write**, successful or not, and on CP/M Z80 bootstrap/reset or transport-mode change. This prevents stale prefetched sectors after installer writes. Mount changes still take effect only on the existing virtual Z80 reset.
- SPI diagnostics at `/spitest` are rejected while live CP/M SPI reads are enabled (and vice versa), preventing DMA ownership conflicts.
- For uncertain DMA ownership, set `needs_reboot=true` and do not retry SPI until ESP32 reboot. A failing UART fallback may still expose the historical CBIOS's weak disk error reporting; **back up disk images before using the Multiplan installer**. Nothing in this first version changes the CBIOS or claims write durability beyond the existing UART baseline.

### Why batching matters

The validated standalone batch test transferred 128 × 512 bytes in ~881 ms (72.61 KiB/s). A single framed 512-byte fetch still needs control-plane UART exchanges, so naively replacing one CP/M read with one SPI sector would not guarantee any speed improvement. The **16-sector read-ahead** design amortizes those exchanges; cache hits do not touch UART or SPI. Results depend on Multiplan's actual disk access locality and write/read interleaving. Installation may benefit less than ordinary executable loading because **writes stay on UART and invalidate the read cache**.

## Rebuild

**Teensy:** Stay on the existing, hardware-tested `feature/cn3-spi-read-test` revision using `poll_low_v2`. **No new Teensy build/flash is required for this milestone.**

**ESP32 only** (after finishing the UART baseline):

```sh
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Flash `.pio/build/esp32dev/firmware.bin` using the existing `/updater`. Do not erase SPIFFS or NVS mount settings. Keep the working firmware image for rollback.

## First-run test sequence

1. Keep the Multiplan baseline timings and a **backup** of all writable CP/M disk images. Start the new ESP32 firmware with SPI still **OFF** by default. Verify HP BASIC/SIMON85, CP/M terminal, CP/M `DIR`, and ordinary CP/M disk writes (e.g., copy a disposable file); the UART code path should behave as before.
2. With CP/M **idle at its prompt**, open `/cpmmedia`, look for **CP/M disk-read transport (experimental)**, and click **Enable SPI read-ahead (4 MHz)**. The selector applies immediately; no Z80 reset is required. If you prefer REST: `POST /api/cpm/spireads?enabled=1`.
3. Run `DIR` on the mounted A: drive and on another mounted drive. Check `GET /api/cpm/spireads` for increasing `batch_reads`, `frames` and `cache_hits`; `spi_failures` and `uart_fallbacks` should remain 0. Watch HP-85 responsiveness.
4. Run a simple CP/M copy to a **disposable** destination file/image and verify it afterward; all writes are still UART and cache invalidation must make subsequent reads coherent. Avoid the Multiplan installer until this passes.
5. Only then compare **Multiplan executable startup** against your UART timing. For comparable cold reads, go back to UART and then re-enable SPI between benchmark runs (both mode switches invalidate the cache). Keep the same image and filesystem state where practical.
6. After startup tests pass, exercise the Multiplan installer using **backed-up images**. Because it writes, don't assume the same speedup as read-heavy startup. Check actual results.
7. Optionally run SIMON85 graphics while CP/M starts Multiplan, as in the previous proven SD/SPI stress tests. Do not run the standalone `/spitest` diagnostics while live SPI disk mode is enabled.

Switch back at any time **when CP/M is idle** using the **Use UART (baseline)** button, or `POST /api/cpm/spireads?enabled=0`. This clears the cache. On any SPI failure, fallback occurs automatically and SPI stays disabled until the next ESP32 reboot; `/api/cpm/spireads` reports the reason. If the HP-85 bus becomes unresponsive at all, stop and power-cycle EBTKS and revert to the known-good binary.

### Status / instrumentation

```sh
curl 'http://<EBTKS-IP>/api/cpm/spireads'
curl -X POST 'http://<EBTKS-IP>/api/cpm/spireads?enabled=1'
curl -X POST 'http://<EBTKS-IP>/api/cpm/spireads?enabled=0'
```

Important JSON fields:

| Key | Interpretation |
|---|---|
| `enabled` | Active opt-in SPI read mode (default false) |
| `boot_disabled`, `needs_reboot` | SPI disabled due to error, especially uncertain DMA ownership |
| `cache_hits`, `cache_misses` | How often CP/M reads were served from read-ahead or needed a bulk fetch |
| `batch_reads`, `frames` | Successful physical 1-/16-frame transfers |
| `uart_fallbacks` | Failed SPI requests serviced via existing UART read |
| `spi_failures`, `last_error` | Details of failures preventing further SPI requests |
| `last_batch_us` | Elapsed time for most recent complete SPI batch |
| `cache_invalidations` | Increases on writes, resets, mode changes and miss replacement |
| `writes` | Always `UART` in this milestone |

**Meaningful comparison:** note Multiplan startup duration on UART, same duration with SPI enabled, and a snapshot of cache hits/misses and fallback count. The batch's measured standalone speedup (~8.75× throughput) is **not a prediction of total application launch speedup**.

## Next milestones

If the Multiplan startup and installation regressions pass, profile read locality, evaluate larger/pipelined read-ahead or adaptive cache policy, and only then consider SPI disk writes with separate integrity/durability testing. Separately, a `KIND_CRT` adapter may supply HP-85 screen dumps on the same generic SPI framing transport; it is **not implemented here**.

The ESP32 firmware **main branch remains unchanged**. Never add normal Arduino GPIO interrupts to the Teensy HP-85 bus IRQ group.
