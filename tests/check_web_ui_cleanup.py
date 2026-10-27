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

# Large media listing must never run the up-to-8-second Teensy query in
# AsyncTCP's callback. Instead queue it to the existing web background task,
# returning 202, later 200 JSON, or explicit 503 when memory is insufficient.
store = (ROOT / "cpm_mount_store.h").read_text(encoding="utf-8")
assert 'inline bool cpmStoreBuildMediaStatusJson(Print &out)' in store
assert 'if (result.overflowed()) return false;' in store
assert 'const size_t expected = measureJson(result);' in store
assert 'return serializeJson(result, out) == expected;' in store
assert 'Teensy directory/listing heap released before response serialization' in store
assert 'AsyncResponseStream* response=' in media_page
assert 'new (std::nothrow) AsyncResponseStream("application/json",512)' in media_page
assert 'if(!response || !cpmStoreBuildMediaStatusJson(*response))' in media_page
assert 'CpmMediaAsync::init();' in media_page
assert 'CpmMediaAsync::service();' in web
assert 'compare_exchange_strong(queued,2' in media_page
assert 'compare_exchange_strong(ready,5' in media_page
assert 'response.exchange(nullptr' in media_page
assert 'request->send(202,"application/json"' in media_page
assert 'request->send(503,"application/json"' in media_page
assert 'if(r.status===202)' in media_page
assert 'Media Manager unavailable: ESP32 memory low' in media_page
assert 'return ESP.getFreeHeap() >= 56U*1024U' in media_page
assert 'heap_caps_get_free_size(MALLOC_CAP_8BIT) >= 24U*1024U' in media_page
assert 'request->send(200, "application/json", reply);' not in media_page
handler=media_page[media_page.index('server.on("/api/cpm/media"'):media_page.index('server.on("/api/cpm/mount"')]
assert 'cpmStoreBuildMediaStatusJson' not in handler
assert 'cpmStoreFetchDiskDirectory' not in handler
assert 'ebtksStorageExchange' not in handler

# The retired /sdtest page has no residual entry point.
assert not (ROOT / "sd_test_page.h").exists()
assert "/sdtest" not in web
assert "sd_test_page.h" not in web
assert "ebtksServeSdTest" not in web

# Preserve the critical Teensy JSON bridge, SPI read-ahead, fallback and OTA.
assert 'AsyncWebSocket ebtksWebSocket("/ws")' in web
assert "spiTestRegister(ebtksServer)" in web
assert "spiDiskRegister(ebtksServer)" in web
assert "webTerminalCleanupClients();" in web
assert "EBTKS_HOME_Z80_LINK" in web
assert ')HTML" EBTKS_HOME_Z80_LINK R"HTML(' in web
assert ')" EBTKS_HOME_Z80_LINK R"HTML(' not in web
assert '"ebtks_static_pages.h"' in web
assert '"Content-Encoding","gzip"' in (ROOT / "ebtks_static_pages.h").read_text()
assert 'ebtksServeStaticHtml(request' in web
assert 'ebtksServeStaticHtml(r' in (ROOT / "web_terminal_v2.h").read_text()
assert 'ebtksServeStaticHtml(request' in media_page
assert 'ebtksServeStaticHtml(request' in (ROOT / "crt_display_page.h").read_text()
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
assert 'CpmWifiModem::requestReleaseAtCcp();' in web
assert 'gate->compatible.load(std::memory_order_acquire)' in web
assert 'gate->mode.load(std::memory_order_acquire)==CpmSafePoint::CCP_INPUT' in web
assert web.index('CpmWifiModem::requestReleaseAtCcp();') < web.index('CpmWifiModem::service();')
assert 'inline void requestReleaseAtCcp()' in modem
assert 'startState.compare_exchange_strong(running,6' in modem
assert 'if (status()==6)' in modem
assert 'socketClient->stop();' in modem
assert 'delete oldUart;' in modem
assert 'delete socketClient;socketClient=nullptr;' in modem
assert 'delete hayes;hayes=nullptr;' in modem
assert 'free(netTx);netTx=nullptr;' in modem
assert 'free(response);response=nullptr;' in modem
assert 'startState.store(7,std::memory_order_release);' in modem
assert 'vTaskSuspend(nullptr);' in modem
assert 'eTaskGetState(workerHandle)==eSuspended' in modem
assert 'vTaskDelete(finished);' in modem
assert 'startState.store(0,std::memory_order_release);' in modem
assert 'netIn=netOut=responseIn=responseOut=0;' in modem
assert modem.index('inline void requestReleaseAtCcp()') > modem.index('static void hangup(bool respond)')

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
assert '#if EBTKS_TELNET_ENABLE && EBTKS_TELNET_SHARED_WORKER' in web
assert 'doc["telnet_enabled"]=(bool)EBTKS_TELNET_ENABLE;' in web
assert 'doc["telnet_listener_mode"]="disabled";' in web
assert 'doc["telnet_connected"]=false;' in web
assert '#if EBTKS_TELNET_ENABLE' in telnet
assert "telnetBegin();" in web
assert '"free_8bit"' in web
assert '"telnet_listener_mode"' in web
assert "#if EBTKS_TELNET_ENABLE && !EBTKS_TELNET_SHARED_WORKER" in ino
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
# Early cache experiment isolates IRAM exhaustion without consuming an
# additional 8KiB of scarce normal DRAM. Boot reserve precedes Wi-Fi.
assert "#define EBTKS_SPI_CACHE_EARLY_IRAM_ONLY 0" in spi
assert "#if EBTKS_SPI_CACHE_EARLY_IRAM_ONLY && !EBTKS_SPI_CACHE_WORD32" in spi
assert "spiDiskReserveCacheEarly();" in ino
assert ino.index("spiDiskReserveCacheEarly();") < ino.index("ebtksWebBegin()")
assert 'heap_caps_get_free_size(caps)' in spi
assert 'heap_caps_get_largest_free_block(caps)' in spi
assert 'heap_caps_get_free_size(MALLOC_CAP_8BIT)' in spi
assert "if (!allocated) {" in spi
assert "spiDiskBootDisabled=true;" in spi
assert '"Early IRAM32 cache unavailable; UART disk reads only"' in spi
assert 'doc["early_iram_only"] = true;' in spi
assert 'doc["early_cache_allocator"].to<JsonObject>()' in spi
assert 'early["free8_before"]' in spi
assert 'early["free8_after"]' in spi
assert 'early["exec32_free_before"]' in spi
assert 'early["exec32_largest_before"]' in spi
assert 'spiDiskEarlyDiag->accepted=(uint32_t)allocated;' in spi
fallback_guard=spi.split("  // Safe fallback to the proven DRAM cache",1)[0]
assert "#if EBTKS_SPI_CACHE_EARLY_IRAM_ONLY\n" in fallback_guard
assert "  return false;\n#endif" in fallback_guard
assert spi.index("#if EBTKS_SPI_CACHE_EARLY_IRAM_ONLY\n  // Never consume") < spi.index("heap_caps_malloc(SPI_DISK_CACHE_BYTES, MALLOC_CAP_8BIT)")

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
assert '#if EBTKS_WEB_TERMINAL_ENABLE' in web_terminal
assert 'inline void webTerminalWrite(const uint8_t*,size_t) {}' in web_terminal
assert 'webTerminalCleanupClients()' in web_terminal
assert 'cpmMediaRegister(server);' in web_terminal
# Arduino-ESP32 2.0.17 WiFiClient::write internally waits up to 10 s.
# The ConsoleTask cannot safely call it when the peer is slow or the TCP
# send buffers are exhausted. Never stall TFT/CP/M behind Telnet retry.
assert 'serverClient.write(chunk + sent, n - sent)' not in console
assert '::send(fd,telnetTx+telnetStart,telnetEnd-telnetStart,MSG_DONTWAIT)' in console
assert 'kTelnetTxCapacity=1024' in console
assert "#if EBTKS_TELNET_ENABLE" in console
assert "#if EBTKS_TELNET_ENABLE" in (ROOT / "globals.h").read_text()
assert 'malloc(kTelnetTxCapacity)' in console
assert 'free(telnetTx)' in console
assert 'telnetEnd+n>kTelnetTxCapacity' in console
assert 'memmove(telnetTx,telnetTx+telnetStart,left)' in console
assert 'telnetStart+=(size_t)wrote;' in console
assert 'errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR' in console
assert 'pdMS_TO_TICKS(2000)' in console
assert 'dropSlowTelnet("buffer full")' in console
assert 'dropSlowTelnet("no progress for 2 s")' in console
assert 'Cpm85Input::observeConsoleOutput(chunk,n);' in console
assert console.index('EbtksTft::mirrorFeed(chunk,n)') < console.index('::send(fd,')
assert console.index('webTerminalWrite(chunk,n)') < console.index('::send(fd,')
assert 'vTaskDelay(1);' in console
assert 'CpmWifiModem::service();' in web
assert "network_counters_available" not in web
assert 'v["wifi_rssi_dbm"]' not in web
assert 'v["websocket_queue_wait_events"]' not in web
assert 'v["telnet_write_stall_events"]' not in web

# Protect Web Terminal BEFORE websocket client allocation, without relaxing
# modem dial guards or introducing any new permanently allocated data.
assert "inline bool webTerminalHasMemoryReserve()" in web_terminal
assert "ESP.getFreeHeap() >= 56U * 1024U" in web_terminal
assert "heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= 14U * 1024U" in web_terminal
assert "z80WebSocket.handleHandshake(" in web_terminal
assert "return webTerminalHasMemoryReserve();" in web_terminal
assert web_terminal.index("z80WebSocket.handleHandshake(") < web_terminal.index("server.addHandler(&z80WebSocket)")
assert web_terminal.index("if (!webTerminalHasMemoryReserve())") < web_terminal.index("reinterpret_cast<const uint8_t *>(EBTKS_TERM_PAGE)")
assert "r->send(503,\"text/plain\"" in web_terminal
assert "if(++wsFailures<3)" in web_terminal
assert "retryTerminalConnection()" in web_terminal
assert "setTimeout(connect,1000)" not in web_terminal

# Firmware identity is the exact compiled SHA. No startup hooks/heap needed.
pio = (ROOT.parent / "platformio.ini").read_text(encoding="utf-8")
revision = (ROOT.parent / "tools/pio_git_revision.py").read_text(encoding="utf-8")
assert "pre:tools/pio_git_revision.py" in pio
assert "pre:tools/pio_gzip_pages.py" in pio
assert 'git("rev-parse", "--verify", "HEAD")' in revision
assert "EBTKS_BUILD_GIT_COMMIT" in revision
assert "EBTKS_BUILD_GIT_DIRTY" not in revision
assert '#include "ebtks_build_revision.h"' in web
assert 'v["git_commit"]=EBTKS_BUILD_GIT_COMMIT' in web
assert 'v["git_dirty"]' not in web
assert (ROOT.parent / "tests/check_build_revision.py").is_file()
print("PASS: known-good boot/network paths; only compile-time Git SHA added")

# OTA regression (B2b exposed an older incomplete-session problem):
# interrupted HTTP uploads must not leave UpdateClass running; only successful
# begin may be followed by writes, and user-visible diagnostics identify where
# an initial transfer stopped. No new permanent TFT DRAM buffers.
assert "static EbtksOtaSession* ebtksOtaSession=nullptr;" in web
assert "new(std::nothrow) EbtksOtaSession{request,0,0}" in web
assert "request->onDisconnect([request]()" in web
assert 'ebtksOtaTrace("client disconnected; aborting"' in web
assert "if(Update.isRunning()) Update.abort();" in web
assert "if(!limit || !Update.begin(limit))" in web
assert "if(!s || s->owner!=request || s->state!=0) return;" in web
assert "if(written!=len)" in web
assert 'ebtksOtaTrace("progress",s->received);' in web
assert 'ebtksOtaTrace("verified",s->received);' in web
assert "esp_ota_get_next_update_partition(nullptr)" in web
assert "Update.end(true)" in web
assert "ebtksRestartRequired=true;" in web
print("PASS: OTA upload owner/disconnect cleanup and bounded progress tracing")

print("PASS: Web, modem, heap capability inventory, shared Telnet, IRAM32 SPI cache, SPI safeguards")
