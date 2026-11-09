#!/usr/bin/env python3
"""Static B2c diagnostic safety gates; hardware execution is still required."""
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "ESP32_Z80_Emulator"
web = (root / "ebtks_web.h").read_text()
ino = (root / "ESP32_Z80_Emulator.ino").read_text()
cpu = (root / "cpu_redcode.h").read_text()
gate = (root / "cpm_safe_point.h").read_text()
source = (root / "ebtks_cpm_sd_import.h").read_text()
bdos = (root / "cpm_file_write_service.h").read_text()
rtc = (root / "cpm_import_diag.h").read_text()

# Only the pinned CPU worker handles Teensy SD/BDOS; the AsyncTCP handler arms it.
assert '"/api/cpm/service/source",HTTP_POST' in web
assert '"/api/cpm/service/source",HTTP_GET' in web
assert '"/api/cpm/service/diag",HTTP_GET' in web
http = web.split('"/api/cpm/service/source",HTTP_POST', 1)[1].split(
    '"/api/cpm/service/source",HTTP_GET', 1)[0]
assert "gate->request(CpmSafePoint::PREFLIGHT_SD" in http
assert "ebtksStorageExchange" not in http
assert "CpmFileWriteService::execute" not in http
assert "recover_requires_READ_ONLY_SESSION" in http
assert '"/api/cpm/service/import",HTTP_POST' in web

# Preflight must close a READ-ONLY Teensy session and never enter native BDOS.
preflight = source.split("inline bool preflight(", 1)[1].split("inline void traceBdos", 1)[0]
assert "begin(source,session)" in preflight
assert "release(session.token)" in preflight
assert "CpmFileWriteService::execute" not in preflight
assert "CpmDirectoryService::invoke" not in preflight
assert 'cmd["cpm_import"]["op"]="release"' in source
assert 'cmd["cpm_import"]["op"]="recover"' in source
assert "gate.importRecoverRequested.load" in preflight
assert "requested==PREFLIGHT_SD && recoverOrphan" in gate
assert 'fcbName?fcbName[i]:\' \' ' in gate

# Preserve the Teensy reason instead of collapsing all source failures.
assert 'session.sourceIssue=reasonCode(r["reason"] | "")' in source
assert '"source_error"' in web and '"last_source_error"' in web
assert "lastImportSourceIssue.store(" in gate

# The native BDOS implementation only acquires an optional trace callback;
# existing native test fixtures retain the old arguments.
assert "TraceCallback trace=nullptr" in bdos
assert "if(trace)trace(TRACE_MAKE,0)" in bdos
assert "if(trace)trace(TRACE_WRITE,index)" in bdos
assert "if(trace)trace(TRACE_VERIFY_READ,index)" in bdos
assert "CpmSdImport::preflight(*gate)" in cpu
assert "CpmSdImport::execute(&ebtksCpuBridge" in cpu

# No new FreeRTOS task or ordinary heap allocation for the reset trace.
assert "RTC_NOINIT_ATTR static volatile Trace active" in rtc
assert "RTC_NOINIT_ATTR static volatile Trace previous" in rtc
assert "CpmImportDiag::onBoot();" in ino
assert "uxTaskGetStackHighWaterMark(nullptr)" in rtc
assert "xTaskCreate" not in rtc and "malloc(" not in rtc
print("PASS: B2c preflight/recovery/RTC/BDOS diagnostic wiring")
