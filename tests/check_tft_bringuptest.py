#!/usr/bin/env python3
"""Build-time boundaries for the first ESP32 ST7796 trial."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/"ESP32_Z80_Emulator/ebtks_tft_test.h").read_text()
ino=(root/"ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino").read_text()
cfg=(root/"platformio.ini").read_text()
assert "#define EBTKS_TFT_ENABLE 0" in src
assert "constexpr int kTftSclk = 14;" in src
assert "constexpr int kTftMosi = 23;" in src
assert "constexpr int kTftCs   = 13;" in src
assert "constexpr int kTftDc   = 33;" in src
assert "constexpr int kTftRst  = 19;" in src
assert "cfg.spi_host = VSPI_HOST" in src
assert "cfg.dma_channel = 0" in src
assert "cfg.freq_write = 4000000" in src
assert "cfg.pin_miso = -1" in src
assert "lgfx::Panel_ST7796" in src
assert "EbtksTft::drawBootPattern();" in ino
assert "pio run -e esp32dev-tft" in cfg
assert "lovyan03/LovyanGFX @ 1.2.7" in cfg
assert "EBTKS_TFT_ENABLE=1" in cfg
assert not any(x in src for x in ["createSprite(", "malloc(", "new uint8_t[", "xTaskCreate"])
print("PASS: ST7796 opt-in, VSPI GPIO isolation and framebuffer-free boot pattern")
