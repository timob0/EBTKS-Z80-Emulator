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
assert '"/api/system/memory/pools"' in web
assert 'EbtksMemory::appendHeapPools(doc)' in web
# ESPAsyncWebServer routes match child paths; first matching callback wins.
# Preserve this ordering or /memory shadows /memory/pools on hardware.
assert web.index('ebtksServer.on("/api/system/memory/pools"') < (
    web.index('ebtksServer.on("/api/system/memory"'))
assert 'doc["capability_rows_overlap"]=true' in web
assert 'doc["min_free_8bit"]' in web
assert 'heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)' in web
memory = (ROOT / "ebtks_memory_profile.h").read_text(encoding="utf-8")
assert 'ARDUINO_SETUP_ENTRY' in memory
assert 'CPM_BOOT_READY' in memory
assert 'heap_caps_get_info(&info,pool.caps)' in memory
assert 'MALLOC_CAP_32BIT' in memory
assert 'EbtksMemory::capture(EbtksMemory::ARDUINO_SETUP_ENTRY)' in ino
assert 'EbtksMemory::capture(EbtksMemory::CPM_BOOT_READY)' in ino
assert (ROOT.parent / "tools/report_esp32_memory.py").is_file()
assert "spiDiskCacheStoreSector(block,data);" in spi
assert "spiDiskCacheLoadSector" in spi
assert "MALLOC_CAP_32BIT|MALLOC_CAP_EXEC|MALLOC_CAP_INTERNAL" in spi
assert "free8After>=free8Before" in spi
assert "EBTKS_SPI_CACHE_WORD32" in spi
assert 'doc["cache_storage"] = spiDiskCacheStorage();' in spi
assert 'v["spi_cache_storage"]=spiDiskCacheStorage();' in web
assert 'v["spi_cache_allocated"]=spiDiskCacheAllocated();' in web
word_cache = (ROOT / "spi_cache_words.h").read_text(encoding="utf-8")
assert "words[i]=value" in word_cache
assert "const uint32_t value=words[i]" in word_cache
assert "memcpy" not in word_cache.replace("memcpy/memset", "")
# The failed Wi-Fi health instrumentation is completely removed.
# Only the build-generated commit SHA is permitted in the health API.
console = (ROOT / "serial.h").read_text(encoding="utf-8")
web_terminal = (ROOT / "web_terminal_v2.h").read_text(encoding="utf-8")
assert not (ROOT / "ebtks_network_diag.h").exists()
for src in (web, console, web_terminal, ino):
    assert "EbtksNetDiag" not in src
    assert "ebtks_network_diag.h" not in src
    assert "WiFi.onEvent(" not in src
assert 'WiFi.mode(WIFI_STA);' in web
assert 'pdMS_TO_TICKS(10000)' in web_terminal
assert 'z80WebSocket.closeAll();' in web_terminal
assert 'serverClient.write(chunk + sent, n - sent)' in console
assert 'vTaskDelay(1);' in console
assert 'CpmWifiModem::service();' in web
assert "network_counters_available" not in web
assert 'v["wifi_rssi_dbm"]' not in web
assert 'v["websocket_queue_wait_events"]' not in web
assert 'v["telnet_write_stall_events"]' not in web

# Firmware identity is the exact compiled SHA. No startup hooks/heap needed.
pio = (ROOT.parent / "platformio.ini").read_text(encoding="utf-8")
revision = (ROOT.parent / "tools/pio_git_revision.py").read_text(encoding="utf-8")
assert "extra_scripts = pre:tools/pio_git_revision.py" in pio
assert 'git("rev-parse", "--verify", "HEAD")' in revision
assert "EBTKS_BUILD_GIT_COMMIT" in revision
assert "EBTKS_BUILD_GIT_DIRTY" not in revision
assert '#include "ebtks_build_revision.h"' in web
assert 'v["git_commit"]=EBTKS_BUILD_GIT_COMMIT' in web
assert 'v["git_dirty"]' not in web
assert (ROOT.parent / "tests/check_build_revision.py").is_file()
print("PASS: known-good boot/network paths; only compile-time Git SHA added")

print("PASS: Web, modem, heap capability inventory, shared Telnet, IRAM32 SPI cache, SPI safeguards")
