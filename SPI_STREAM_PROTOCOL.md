# Generic CN3 SPI framed streaming — read-only SD producer, v1

**Status:** Both feature-branch source trees contain this opt-in implementation; **NOT yet locally compiled, flashed or hardware-validated**. Retain known-good binaries for rollback. Working HP-85, SIMON85, Wi-Fi, Web UI, Z80/CP-M and the current UART/JSON/Base64 CP/M disk service are unaffected. No merge into `main`.

## Reliability repair after first hardware runs (2026-10-18)

**Hardware report before repair:** User found that the batched read-only stream **usually passed** but after repeated tests sometimes returned `Stream prepare UART JSON malformed`, then `Could not launch SPI stream worker`. Subsequently the ESP32 web pages (`/z80`, CP/M Media Manager) became unavailable. Reported runs 19 and 20 were 16-sector tests that failed **before any SPI data was sent**: `stream_frames_received=0`, `completed_sectors=0`, `needs_reboot=false`. The successful batch timings and exact sector counts were not provided. **Do not use the earlier batched build for further stress testing.**

**Likely root cause found in source:** The original `spiTestStreamWorker()` was dynamically created for every test and called FreeRTOS `vTaskDelete(nullptr)` directly while its function-scope Arduino `String` and `JsonDocument` variables were alive. Task deletion does not perform normal C++ stack unwinding, so their heap allocations can leak on both normal and early-return paths. Repeated runs can deplete/fragment heap, explaining both JSON parse errors and eventual FreeRTOS task creation failures; this is a strong code-level diagnosis, **not yet verified as the only runtime cause**.

**ESP32-only repair committed; requires local rebuild/test:**

- One persistent `SpiBatch` worker, allocated at most once, sleeping on a FreeRTOS task notification between sessions. The body now runs as an ordinary C++ function that **returns normally**, so local `String` and ArduinoJson destructors execute.
- Worker stack increased from 6,144 to **8,192 bytes** (allocated once); task stack high-water mark and ESP heap telemetry published.
- Stream starts are **refused before task creation** if free heap or largest contiguous block is too small, and likewise refused when subsequent run memory is critically low, preserving resources for Wi-Fi and HTTP. These low-memory refusals set `needs_reboot=true`.
- A malformed or missing `spi_stream_prepare` UART response can be retried **once**, and only **before any SPI DMA is queued**. Raw decoded reply is captured as a bounded, printable sample (`stream_prepare_reply_sample`) with error category (`stream_prepare_error`) and cumulative counter; no stream-send phase is retried automatically.
- Keep legacy CP/M UART transport, the old one-sector SD test and synthetic SPI test unchanged. Teensy firmware, SD images, Wi-Fi and OTA routes are unchanged by this repair.

### Memory and stability regression (run this BEFORE 128 sectors)

Power-cycle or reset the ESP32 to clear any heap already lost under the old firmware, then flash the **updated ESP32-only** firmware via `/updater`. There is no need to rebuild/flash Teensy since its framing and handler code did not change. Retain the previous stable ESP32 binary in case a rollback is needed.

1. Verify `/z80`, `/cpmmedia`, `/updater`, and CP/M `DIR` on a fresh boot.
2. Run existing **read-only SD sector test (4 MHz)** for **1 sector**, verifying the established baseline.
3. Run new **batched framed SD stream** for **1 sector**, once. Save JSON including `heap_before_stream`, `heap_after_stream`, `heap_free_now`, `heap_min_free`, `heap_largest_free_block`, `stream_worker_stack_high_water`, `stream_worker_launch_failures` and `stream_prepare_parse_failures`.
4. Repeat the **1-sector batch 20–30 times** without restarting ESP32, checking that `heap_after_stream` does not monotonically decline, the web UI keeps loading, and `stream_worker_launch_failures=0`. Initial allocation of the persistent worker should lower free heap **once**, not once per batch. The min-free value is a historic low-water mark, not the current heap.
5. If heap remains stable, run **16 sectors** a few times, then **128 sectors** with HP-85 idle. Only then repeat 128 with SIMON85 graphics active. Stop at any fail or web-service issue.
6. If PREPARE JSON still fails, send complete `/api/spitest` status plus `/z80debug` while reachable; the new bounded `stream_prepare_reply_sample` and `stream_prepare_error` should distinguish malformed UART frames from heap failures.

The source change has been inspected for basic protocol compatibility, but **this revised ESP32 firmware has not been compiled or exercised on hardware**. Do not treat the resource leak as fully fixed until repeated runs and web regressions pass.

## Why this is generic

The Teensy 4.1 remains the **SPI master**, the ESP32 SPI2 its **slave**. UART carries stream *control* and completion status; SPI carries opaque binary frames. The binary framing layer knows nothing about SD files or CRT layout. A source adapter on the Teensy supplies 512-byte payloads. The first `kind=1` producer reads CP/M image sectors **O_RDONLY**; a future `kind=2` producer could supply an HP-85 text/graphics screen dump (split into padded 512-byte blocks with logical length in stream metadata). That CRT producer is **not implemented in this build**, and no existing screen-copy command has been altered.

The same unchanged version-1 frame codec exists in `EBTKS_SPI_Stream_Frame.h` (Teensy) and `spi_stream_frame.h` (ESP32). Both independently implement CRC-16/Modbus, little-endian fields, header generation/checking and packet type/sequence validation.

### Frame format (528 bytes per transaction)

| Offset | Bytes | Description |
|---|---:|---|
| 0–1 | 2 | Magic `85 53` |
| 2 | 1 | Protocol version `1` |
| 3 | 1 | Producer kind (`1` SD read; `2` reserved for CRT) |
| 4–5 | 2 | Stream session ID (little-endian) |
| 6–7 | 2 | Zero-based frame number within session |
| 8–9 | 2 | Payload size, currently exactly 512 |
| 10–11 | 2 | CRC-16/Modbus of 512-byte payload |
| 12–13 | 2 | Flags; bit 0 means source read error |
| 14–15 | 2 | CRC-16/Modbus of first 14 header bytes |
| 16–527 | 512 | Unencoded binary data payload |

Headers and payloads are CRC-checked separately. The ESP32 rejects wrong session IDs, frame indices, kinds and packet lengths. A failed SD read produces an error-flagged frame rather than silently substituting valid data. The receiver **drains remaining frames even on recoverable CRC/metadata errors** to keep DMA ownership safe; the test ultimately fails, with `stream_first_error` and `failed_sector`. A missing DMA completion forces `needs_reboot=true`.

### Control flow — two UART round trips per *batch*, not per sector

1. ESP32 sends `spi_stream_prepare` with a path, starting 512-byte sector, count and session ID. Teensy validates a direct `/cpm/disks/<filename>` path, opens it `O_RDONLY`, checks that **the entire requested range fits in the file**, closes it and responds. **On invalid path/range no DMA is queued.**
2. ESP32 takes the existing serialized Teensy-UART mutex, queues its first DMA RX descriptor (528 bytes) and sends **one** `spi_stream_send` UART command for the whole session.
3. Teensy opens/positions the file once, sequentially reads one 512-byte sector at a time, prepares a CRC-protected frame, waits for ESP32 `READY`, then clocks it over **SPI mode 0 at 4 MHz**, with HP-bus interrupts enabled. ESP32 reaps each DMA transaction, validates the frame, and queues the next descriptor. The Teensy does not send UART acknowledgments per sector.
4. After transmitting the last frame, Teensy returns **one** UART reply with completed frame count, aggregate payload CRC, SD-read time, SPI loop time and min/max frame time. ESP32 verifies all frames and compares aggregate CRCs before claiming success; the mutex is then released.
5. The original `spi_sd_prepare` / `spi_sd_send` **two-UART-round-trip-per-sector** diagnostic remains unchanged as a baseline, as does synthetic full-duplex `/spitest`.

**Note:** Batching reduces *control-message overhead*, but it still uses one ESP32 slave DMA descriptor and one CS pulse per 528-byte frame. This is not a single continuous 64 KiB SPI transfer. We have not yet implemented retries or production-grade fallback; the batched path is opt-in diagnostic-only.

### Concurrency and safety

- **SPI mode 0, 4,000,000 Hz** fixed for this pilot (8 MHz mode 1 previously failed; 8 MHz mode 0 passed hardware stress but has margin concerns with Dupont jumpers).
- The shared Teensy UART mutex remains locked throughout the burst, preventing CP/M UART requests from interleaving with control responses. Leave the Z80 at its command prompt. HP-85 bus **interrupts remain enabled** during SD reads and SPI loops.
- SD access is strictly **read-only** and constrained to named files directly under `/cpm/disks/`. There is no modification of CP/M mounted drives, SD images or live CP/M disk reads/writes.
- Do not attempt a new SPI test while a previous one runs. If JSON says `needs_reboot=true`, reboot the ESP32 before retrying. Preserve the proven binaries.
- Tested board MISO jumper: **Teensy CN3-29 ↔ ESP32 CN3-17** (physically verified WROOM GPIO17/pad 28). The published EBTKS V3 schematic is incorrect on this mapping.

## Optional frame-only host regression

The ESP32 feature branch includes `tests/spi_stream_frame_selftest.cpp`, which has no Arduino dependencies. On a development machine with `g++` installed, run this before flashing:

```sh
g++ -std=c++11 -Wall -Wextra -pedantic tests/spi_stream_frame_selftest.cpp -o /tmp/ebtks_spi_frame_test
/tmp/ebtks_spi_frame_test
```

The self-test checks the known CRC-16/Modbus vector `123456789` (expected `0x4B37`), successful SD/CRT typed frames, wrong session/frame/type, header corruption, payload corruption and source-error flags. This only checks the portable frame codec; **it does not replace PlatformIO compilation or physical SPI tests.**

## Pull, compile, flash

Use **both** repositories' `feature/cn3-spi-read-test` branches.

Teensy:

```sh
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e teensy41
pio run -e teensy41 -t upload
```

ESP32:

```sh
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Upload only `.pio/build/esp32dev/firmware.bin` via the existing `/updater` page. No flash erase or SD formatting.

### First-run test order

1. Confirm HP-85 operation, the Z80 CP/M prompt, and `/spitest`. Run the **existing synthetic full-duplex 4 MHz mode 0, one sector** test.
2. Run the **existing "Read SD sectors over SPI (4 MHz)"** diagnostic, using `/cpm/disks/A.dsk` if that file is present, sector 0 and count 1, to verify the previous baseline.
3. With the same file and inputs, click **"Batched framed SD stream (4 MHz, read-only)"**, initially for **1 sector**. Inspect `state`, `test_kind=sd_stream_framed`, `stream_bad_frames`, `stream_crc_esp`, `stream_crc_teensy`, `stream_frames_received`, and `needs_reboot`.
4. If step 3 passes, try **16 sectors**, then **128 sectors** with the HP-85 idle. Record `wall_us` and compare with the previous SD test's idle results: approximately **0.961 s for 16 sectors** and **7.710 s for 128 sectors**.
5. Only then repeat **128 sectors while a SIMON85 HP BASIC graphics program draws/fills ellipses**, checking graphics responsiveness and sector CRCs.
6. Finally retest Z80 CP/M `DIR`, Telnet, WebSocket terminal, main EBTKS Web UI and updater. The live disk path must still work on UART/Base64.

REST endpoints:

```sh
curl -X POST 'http://<EBTKS-IP>/api/spitest/stream?path=%2Fcpm%2Fdisks%2FA.dsk&first=0&sectors=1'
curl 'http://<EBTKS-IP>/api/spitest'
```

When `state=passed`, `stream_frames_received=sectors`, `completed_sectors=sectors`, `stream_bad_frames=0`, `stream_crc_esp=stream_crc_teensy`, and `sd_crc_match=true` should all hold. Each 512-byte payload receives a protected 16-byte header: 128 frames = **67,584 wire bytes** carrying **65,536 payload bytes** (3.125% packet-header overhead, before CS/delays).

**Request/response examples** and a future CRT producer should reuse the framing layer, but the image-specific semantics (pixel format, resolution, logical length, capture synchronization) belong in separate metadata and source adapters, not in this SPI code.

### Next milestone after physical passes

Only after 1/16/128-sector tests succeed should we consider opt-in **read-only CP/M disk reads over the framed SPI transport**, with end-to-end CRCs, bounded retry, a UART/Base64 fallback, safe disk-image selection and no changes to writes until separately tested. Generalize the service further for CRT framebuffer uploads/downloads after defining ownership, buffer format and HP-85 screen-capture timing.
