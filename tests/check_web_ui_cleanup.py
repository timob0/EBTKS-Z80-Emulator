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
# Low-heap modem regression: never start another network task at ESP boot.
modem = (ROOT / "ebtks_modem.h").read_text(encoding="utf-8")
term = (ROOT / "web_terminal.h").read_text(encoding="utf-8")
assert "CpmWifiModem::start();" not in ino
assert 'CpmWifiModem::service();' in web
assert "startState.compare_exchange_strong" in modem
assert "ESP.getFreeHeap()<48U*1024U" in modem
assert "heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<8192U" in modem
assert 's==3 || s==4' in modem
assert 'return (p==kBase+5 && (s==3 || s==4))?0x60:0;' in modem
assert 'reinterpret_cast<const uint8_t *>(EBTKS_TERM_PAGE)' in term
assert 'r->send(200,"text/html",EBTKS_TERM_PAGE);' not in term
assert '"cpm_modem_state"' in web
# Hardware regression: previous open BBS connection left no large free 8-bit
# block and caused simultaneous HTTP/Telnet unavailability. Guard the dial
# BEFORE any TCP connect allocation, preserve existing Web UI and OTA.
memory = (ROOT / "ebtks_memory_profile.h").read_text(encoding="utf-8")
assert 'server.on("/api/system/memory"' not in web # the live handler uses ebtksServer
assert 'ebtksServer.on("/api/system/memory"' in web
assert 'EbtksMemory::capture(EbtksMemory::MODEM_DIAL_BEFORE)' in modem
assert modem.index('largest<kDialMinimumLargest8Bit') < modem.index('socketClient->connect(e.host')
assert 'kDialMinimumLargest8Bit=12U*1024U' in modem
assert 'MEMORY LOW - DIAL REFUSED' in modem
assert '"stack_unused_bytes"' in web
assert 'uxTaskGetStackHighWaterMark' in memory
assert 'CpmWifiModem::lowMemoryDialRefusals()' in web
assert 'EbtksMemory::capture(EbtksMemory::TFT_READY)' in ino
telnet = (ROOT / "telnet.h").read_text(encoding="utf-8")
assert "EBTKS_TELNET_SHARED_WORKER" in telnet
assert "inline void telnetPoll()" in telnet
assert "inline void telnetBegin()" in telnet
assert "telnetPoll();" in web
assert "telnetBegin();" in web
assert '"free_8bit"' in web
assert '"telnet_listener_mode"' in web
assert "#if !EBTKS_TELNET_SHARED_WORKER" in ino
assert "ESP32 Prog" not in telnet
print("PASS: Web, lazy modem, RAM telemetry/dial guard, shared Telnet listener, SPI safeguards")
