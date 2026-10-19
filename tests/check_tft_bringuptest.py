#!/usr/bin/env python3
"""Build-time boundaries of the supported optional ST7796 CP/M terminal."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = (root / "ESP32_Z80_Emulator/ebtks_tft_test.h").read_text()
terminal = (root / "ESP32_Z80_Emulator/ebtks_tft_terminal.h").read_text()
ino = (root / "ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino").read_text()
cfg = (root / "platformio.ini").read_text()

envs = re.findall(r"^\[env:([^]]+)\]", cfg, re.M)
assert envs == ["esp32dev", "esp32dev-tft"], envs
headless, display = cfg.split("[env:esp32dev-tft]")
assert "lovyan03/LovyanGFX" not in headless
assert "EBTKS_TFT_ENABLE=1" not in headless
assert "lovyan03/LovyanGFX @ 1.2.7" in display
for flag in ["EBTKS_TFT_ENABLE=1", "EBTKS_TFT_TERMINAL_MIRROR=1",
             "EBTKS_TFT_VT100=1"]:
    assert flag in display
assert "#define EBTKS_TFT_ENABLE 0" in src
for label, pin in [("kTftSclk", 14), ("kTftMosi", 23),
                   ("kTftCs", 13), ("kTftDc", 33), ("kTftRst", 19)]:
    assert re.search(r"constexpr int\s+" + label + r"\s*=\s*" + str(pin), src)
for requirement in ["cfg.spi_host = VSPI_HOST", "cfg.dma_channel = 0",
                    "cfg.freq_write = 4000000", "cfg.pin_miso = -1",
                    "lgfx::Panel_ST7796"]:
    assert requirement in src
assert "#if EBTKS_TFT_TERMINAL_MIRROR" in ino
assert "EbtksTft::startTerminalMirror();" in ino
assert "EbtksTft::mirrorFeed" in (root / "ESP32_Z80_Emulator/serial.h").read_text()
assert "tft.cp437(true);" in terminal and "lgfx::utf8_switch" in terminal
assert not any(x in src for x in ["createSprite(", "malloc(", "new uint8_t[", "xTaskCreate"])
print("PASS: exactly 2 ESP32 targets; TFT optional, VSPI isolated and ANSI/CP437 enabled")
