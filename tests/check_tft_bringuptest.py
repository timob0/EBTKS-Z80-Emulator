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
options = (root / "ESP32_Z80_Emulator/ebtks_firmware_options.h").read_text()
assert "#define EBTKS_TELNET_ENABLE 0" in options
assert "-DEBTKS_WEB_TERMINAL_ENABLE=1" in headless
assert "-DEBTKS_SPI_CACHE_EARLY_IRAM_ONLY=1" in headless
assert "${env:esp32dev.build_flags}" in display  # inheriting shared services
assert "-DEBTKS_TELNET_ENABLE=0" not in cfg  # compiler switch may opt in
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
                    "cfg.freq_write = EBTKS_TFT_SPI_HZ", "cfg.pin_miso = -1",
                    "lgfx::Panel_ST7796"]:
    assert requirement in src
assert "#define EBTKS_TFT_SPI_HZ 12000000" in src
assert "tft.setTextSize(1.0f,1.5f)" in terminal
assert "dirtyFirst[row]" in terminal and "dirtyLast[row]" in terminal
assert "tft.startWrite();" in terminal and "tft.endWrite();" in terminal
assert "tft.fillRect(x*6,y,(end-x)*6,13,bg);" in terminal
assert "rendered?1:16" in terminal
assert "#if EBTKS_TFT_TERMINAL_MIRROR" in ino
assert "EbtksTft::startTerminalMirror();" in ino
assert "EbtksTft::mirrorFeed" in (root / "ESP32_Z80_Emulator/serial.h").read_text()
assert "tft.cp437(true);" in terminal and "lgfx::utf8_switch" in terminal
assert not any(x in src for x in ["createSprite(", "malloc(", "new uint8_t[", "xTaskCreate"])
print("PASS: exactly 2 full ESP32 targets; optional TFT, default Telnet off, early IRAM cache")
