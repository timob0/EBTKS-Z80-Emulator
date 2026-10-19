#!/usr/bin/env python3
"""Static regressions for retired UI diagnostic and hidden media filtering.

No browser or Teensy hardware is exercised by this test.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "ESP32_Z80_Emulator"
media_page = (ROOT / "cpm_media_page.h").read_text(encoding="utf-8")
media_api = (ROOT / "cpm_media.h").read_text(encoding="utf-8")
web = (ROOT / "ebtks_web.h").read_text(encoding="utf-8")
ino = (ROOT / "ESP32_Z80_Emulator.ino").read_text(encoding="utf-8")
spi = (ROOT / "spi_disk_read.h").read_text(encoding="utf-8")

# The requested surrounding Media Manager sections are still present and ordered.
assert media_page.index("<h2>CP/M Media Manager") < media_page.index("<h3>Drive assignments")
assert media_page.index("<h3>Drive assignments") < media_page.index("<h3>Available SD-card media")
assert "CP/M disk-read transport" not in media_page
assert "refreshDiskTransport" not in media_page
assert "setDiskTransport" not in media_page
assert "spiDiskStatus" not in media_page

# Hide dot files in BOTH the API, before classification, and the HTML rendering.
assert "name[0] == '.'" in media_api
assert "m.name.startsWith('.')" in media_page
assert "const supported=visibleMedia.filter(m=>m.supported)" in media_page
assert "for(const m of visibleMedia)" in media_page

# The retired /sdtest page has no residual entry point.
assert not (ROOT / "sd_test_page.h").exists()
assert "/sdtest" not in web
assert "sd_test_page.h" not in web
assert "ebtksServeSdTest" not in web

# Preserve the critical Teensy JSON bridge, SPI read-ahead, fallback and OTA.
assert 'AsyncWebSocket ebtksWebSocket("/ws")' in web
assert "spiTestRegister(ebtksServer)" in web
assert "spiDiskRegister(ebtksServer)" in web
assert '"/api/system/health"' in web
assert '"/updater"' in web
assert "spiDiskSetEnabled(true)" in ino
assert "#ifndef EBTKS_SPI_READAHEAD_DEFAULT_ON" in spi
assert 'server.on("/api/cpm/spireads"' in spi
assert "spiDiskReadCached(" in media_api
assert "spiDiskInvalidateCache()" in media_api
print("PASS: Media Manager, dot-file filtering, /sdtest retirement, SPI safeguards")
