# CN3 SPI full-duplex diagnostic — build and first hardware test

**Status: source committed on 2026-10-18; requires local PlatformIO builds, flashes, and hardware validation.** Both repos must be on `feature/cn3-spi-read-test`. The previous one-way Teensy MOSI → ESP32 DMA RX test is already confirmed on hardware at 1–8 MHz and up to 128 sectors. This extension adds the *unverified* ESP32 DMA TX → Teensy MISO receive path.

## Design and safety

- Full-duplex **512-byte mode-0 SPI transaction**: Teensy SPI0 master clocks MOSI data and simultaneously receives MISO data; ESP32 SPI2 slave RX DMA receives MOSI and TX DMA sends a different deterministic pattern on MISO.
- Both receivers check **every byte** against a locally generated expected pattern, calculate **CRC-16/Modbus**, and compare CRCs over the existing serialized UART `spi_probe` status reply. The Teensy reports its **first MISO mismatch index**. ESP32 reports its **first MOSI mismatch index**.
- The optional request flag `"bidi":true` enables the MISO test; omitted or `false` keeps the original MOSI-only operation. The browser has a direction selector.
- For each 512-byte transaction, ESP32 fills aligned `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` **TX and RX** buffers before queuing its transaction. Its post-setup ISR signals ESP_READY high only when DMA is armed; Teensy waits for READY, then clocks bytes. The next transaction is queued only after the previous one is completed and acknowledged.
- If DMA completion is uncertain, the ESP32 requires a **reboot** before retrying to avoid reusing still-owned DMA memory. All HP-bus interrupts stay enabled.
- **No disk image is read or written; CP/M storage stays on its original UART/Base64 protocol.** Wi-Fi, HP-85 bus service, WebSocket, OTA, SD services and the existing `main` baseline are not modified. Do not merge this experimental branch to `main` pending testing.

The MISO byte stream is intentionally different from the simultaneous MOSI stream, *including* for constant 00/FF pattern selections, so a short/echo/loopback cannot falsely pass the entire sector:

```cpp
miso[i] = mosi_pattern(i, seq ^ 0xA7, block, pattern)
        ^ uint8_t(0x5A + i * 11 + block * 7);
```

The pattern function is independently implemented on each CPU and was statically checked for parity across the repositories. The original MOSI pattern/CRC result for sequence 13 and block 0 is still **59244**.

## Pull, build and flash

Keep known-good binaries accessible for rollback. Reuse **the same five already-verified CN3 jumpers and 10 kΩ external CS pull-up**; power down before inspecting physical wiring. No rewiring is necessary if the existing MISO jumper was installed.

Teensy (via your normal USB flashing procedure):

```sh
cd ebtks-alt-firmware
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e teensy41
pio run -e teensy41 -t upload
```

ESP32 (via the working EBTKS OTA browser updater):

```sh
cd EBTKS-Z80-Emulator
git fetch origin
git switch feature/cn3-spi-read-test
git pull --ff-only
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Upload only `.pio/build/esp32dev/firmware.bin` in `/updater`. No ESP32 flash erase or Teensy SD replacement required. Teensy must have `"CRTRemote": true` in SD-root `CONFIG.TXT`, as for the previous diagnostic.

## First tests

With CP/M idle at the `A>` prompt and HP-85 normal, open `http://<EBTKS-IP>/spitest`.

1. **MOSI only, 1 MHz, 1 sector, sequence/ramp** — regression; should match previous successful logic.
2. **Full duplex, 1 MHz, 1 sector, sequence/ramp** — first MISO signal test. If it fails, capture the JSON and `/z80debug` before trying other rates.
3. **Full duplex, 2 MHz, then 4 MHz, then 8 MHz, one sector each** — increase only after the prior rate passes.
4. **Full duplex, 4 MHz, 128 sectors, Cycle five patterns** — stress 64 KiB in each direction.
5. **Full duplex, 8 MHz, 128 sectors, Cycle five patterns** — final 64 KiB each way, if 8 MHz single-sector passes.
6. Optionally run 16/128 sectors with fixed 00, FF, 55/AA or walking-one; confirm the HP-85 remains responsive throughout.

At 8 MHz, MOSI input timing was previously reliable, but **MISO output timing is a different path** and may show failures at higher clocks. A high-speed MISO failure is not evidence of broken CP/M disks or a defective encoding library; record the *first* failing byte and clock.

On a successful bidirectional 128-sector test, expect:

```json
{
  "state": "passed",
  "bidirectional": true,
  "bytes": 65536,
  "completed_sectors": 128,
  "completed_bytes": 65536,
  "completed_bytes_total": 131072,
  "first_mismatch_byte": -1,
  "first_mismatch_miso": -1,
  "miso_crc_esp_tx": 0,
  "miso_crc_teensy_rx": 0,
  "needs_reboot": false
}
```

**The two shown MISO CRC zeroes are placeholders in this example**; real reported CRCs depend on run number, pattern and last sector. The two actual CRC fields should be equal. Likewise `crc_esp` must equal `crc_teensy` for MOSI. The aggregate MISO CRC is calculated locally on ESP transmit bytes, whereas actual received MISO CRC is compared on every sector.

`spi_kib_s` / `end_to_end_kib_s` remain **per-direction** throughput, and `full_duplex_total_kib_s` is **total bytes in both directions divided by summed SPI transfer-loop time**, not the end-to-end rate. The Teensy loop now includes incoming-byte CRC and expected-data comparisons, so a direct speed comparison with the older one-way test must account for extra software work.

A complete failed/incomplete DMA transaction requires ESP32 reboot if `needs_reboot=true`. After obtaining the test results, check normal CP/M `DIR`, browser-only VT100, Telnet, `/ws`, updater, Teensy SD features and HP-85 operation. The next milestone is a lower-overhead binary disk sector protocol, **not** an automatic modification of the current storage path.
