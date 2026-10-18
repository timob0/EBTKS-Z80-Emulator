# Generic CN3 SPI framed streaming — read-only SD producer, v1

**Status:** Both feature-branch source trees contain this opt-in implementation; **NOT yet locally compiled, flashed or hardware-validated**. Retain known-good binaries for rollback. Working HP-85, SIMON85, Wi-Fi, Web UI, Z80/CP-M and the current UART/JSON/Base64 CP/M disk service are unaffected. No merge into `main`.

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
