# CN3 SPI0 → SPI2 first communication test (FEATURE BRANCH)

**Experimental — not yet compiled or hardware-tested. Do not merge to main.**

Both repositories need the same feature branch `feature/cn3-spi-read-test`:

- ESP32: https://github.com/timob0/EBTKS-Z80-Emulator/tree/feature/cn3-spi-read-test
- Teensy: https://github.com/timob0/ebtks-alt-firmware/tree/feature/cn3-spi-read-test

This is the first **512-byte test pattern from Teensy SPI0 master to ESP32 SPI2 slave**, at 1 MHz. Nothing reads or writes the SD card or CP/M images. CP/M disk I/O remains on the existing UART/Base64 protocol. There is **no automatic transfer at startup**. The diagnostic is initiated manually via a browser page. Keep the last working `main` images backed up.

## Prerequisites and CN3 wiring

Confirm the five jumpers and 10k external CS pull-up from [CN3_SPI_PROPOSAL.md](CN3_SPI_PROPOSAL.md):

| Signal | Teensy 4.1 CN3 | ESP32 CN3 | Notes |
|---|---|---|---|
| MOSI | 33 — T11 | 21 — E22/GPIO22 | Teensy → ESP |
| MISO | 29 — T12 | 13 — E17/GPIO17 | Reserved for future reverse traffic; not validated by this test |
| SCK | 24 — T13 | 14 — E16/GPIO16 | Teensy supplies clock |
| CS_N | 30 — T26 | 4 — E34/GPIO35 | Active LOW. 10k resistor CN3-4 ↔ CN3-36 (3.3 V) |
| ESP_READY | 26 — T06 | 8 — E27/GPIO27 | ESP output HIGH when RX DMA armed; Teensy input |

Power down before checking jumpers. Check for shorts and physical CN3 pin-1 orientation. Both boards already share ground and operate at 3.3 V. **Never short CN3 pin 4 directly to 3.3 V**; use the resistor. `GPIO35` has no internal pull-up.

Teensy firmware currently requires `"CRTRemote": true` in the SD root `CONFIG.TXT` to service ESP UART messages. Do not change this while testing.

## Build / flash order

First build and install the **Teensy** feature firmware via the existing Teensy 4.1 USB flashing procedure:

```sh
cd ebtks-alt-firmware
git fetch origin
git switch feature/cn3-spi-read-test
pio run -e teensy41
# On your established Teensy USB upload setup:
pio run -e teensy41 -t upload
```

Next build the **ESP32** feature firmware and update through the existing EBTKS `/updater` web page:

```sh
cd EBTKS-Z80-Emulator
git fetch origin
git switch feature/cn3-spi-read-test
pio run -e esp32dev -t clean
pio run -e esp32dev
```

Select `.pio/build/esp32dev/firmware.bin` in the browser updater. This OTA image is application-only, **not** a blank-ESP32 factory image.

Both firmwares must be on their respective feature branches before attempting the SPI test. The Teensy adds a single new optional JSON command `spi_probe`; the ESP32 adds the page and peripheral driver. Stable CP/M features remain enabled.

## Run the diagnostic

After both updates, reboot and confirm HP-85 operation and a normal CP/M prompt in `/z80`. Then open:

```text
http://<EBTKS-IP>/spitest
```

Click **Run 512-byte test** once. The page polls `/api/spitest` and should report:

```json
{
  "state": "passed",
  "detail": "512 bytes verified; no CP/M disk accessed",
  "needs_reboot": false,
  "bytes": 512,
  "clock_hz": 1000000,
  "first_mismatch_byte": -1
}
```

There are additional numeric fields for sequence/run count, the CRC reported by Teensy and independently calculated by ESP, and the Teensy's elapsed transfer-loop time in microseconds.

**Expected /z80debug record:**

```text
SPI test: ESP32 SPI2 slave initialized (GPIO22/17/16, CS=35, READY=27)
SPI test PASS seq=1 512 bytes CRC=.... Teensy measured=.... us
```

If unsuccessful, copy the full `/api/spitest` JSON and the relevant `/z80debug` lines back to the development chat. The page deliberately prevents repeated attempts after a timed-out/incomplete DMA transfer until the ESP32 reboots. This avoids freeing or reusing a still-active peripheral transaction.

### What the test actually measures

The Teensy generates a deterministic byte pattern `0xA5 XOR low8(i*37 + seq)` and clocks 512 bytes as SPI mode 0 using standard `SPI.transfer()` calls. The ESP32 has first queued a word-aligned DMA buffer, and its post-setup ISR callback sets `ESP_READY=HIGH`. The Teensy observes READY, asserts CS_N, sends exactly 512 bytes, and deasserts CS_N. UART carries only the test command and a small status/CRC reply. The ESP32 checks the actual DMA length, every byte, its CRC-16/Modbus (poly A001, initial FFFF), the Teensy CRC, and the sequence number.

`teensy_transfer_us` measures the full loop, **including per-byte SPI.transfer() and software CRC**, not a pure wire-level transfer benchmark. At 1 MHz a 512-byte wire transfer alone is 4096 microseconds. Software and HP interrupt scheduling will add overhead.

This first read-only test does **not** establish 4 MHz reliability, reverse-direction ESP→Teensy MISO traffic, clock pausing during HP-bus service, sustained throughput, or disk-speed improvements. Those are subsequent experiments.

## Safety and fallback

- SPI pins are initialized only after the user presses **Run**. The Teensy maintains CS_N inactive (HIGH) when not transferring.
- The original Teensy ESP serial protocol on GPIO3/GPIO1 remains unchanged, including the existing CRC envelope and mutex.
- The HP-85 bus ISR is not modified. The Teensy's synchronous test transfer is temporary and has interrupts enabled; **bus timing while transferring still needs actual hardware validation**.
- The existing CP/M disk-service path remains unchanged and should continue functioning without SPI success.
- For a failed build or unwanted behavior, reflash the last known-good `main` image(s). No SD-card image change is required.

**No TFT wiring or display firmware is included.**
