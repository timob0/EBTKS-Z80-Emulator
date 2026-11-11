#!/usr/bin/env python3
"""Static safety gates for production B2c import; not a hardware test."""
from pathlib import Path

base = Path(__file__).resolve().parents[1]
root = base / "ESP32_Z80_Emulator"
read = lambda name: (root / name).read_text()
web = read("ebtks_web.h")
ino = read("ESP32_Z80_Emulator.ino")
cpu = read("cpu_redcode.h")
gate = read("cpm_safe_point.h")
source = read("ebtks_cpm_sd_import.h")
bdos = read("cpm_file_write_service.h")

# Preserve the physically verified stack, independent of any optional A/B override.
assert "#define EBTKS_Z80_CPU_TASK_STACK_BYTES 8192" in ino
assert 'xTaskCreatePinnedToCore(CPUTask,"CPUTask",EBTKS_Z80_CPU_TASK_STACK_BYTES' in ino

# Only the pinned CPU task can initiate native BDOS and Teensy SD exchanges.
assert '"/api/cpm/service/source",HTTP_POST' in web
assert '"/api/cpm/service/source",HTTP_GET' in web
http = web.split('"/api/cpm/service/source",HTTP_POST', 1)[1].split(
    '"/api/cpm/service/source",HTTP_GET', 1)[0]
assert "gate->request(CpmSafePoint::PREFLIGHT_SD" in http
assert "ebtksStorageExchange" not in http
assert "CpmFileWriteService::execute" not in http
assert "recover_requires_READ_ONLY_SESSION" in http
assert '"/api/cpm/service/import",HTTP_POST' in web

# Preflight reads a source, releases the read-only session and never enters BDOS.
preflight = source.split("inline bool preflight(", 1)[1].split(
    "inline bool execute(", 1)[0]
assert "begin(source,session)" in preflight
assert "release(session.token)" in preflight
assert "CpmFileWriteService::execute" not in preflight
assert "CpmDirectoryService::invoke" not in preflight
assert 'cmd["cpm_import"]["op"]="release"' in source
assert 'cmd["cpm_import"]["op"]="recover"' in source
assert "gate.importRecoverRequested.load" in preflight
assert "requested==PREFLIGHT_SD && recoverOrphan" in gate
assert "fcbName?fcbName[i]" in gate

# Source error telemetry, partial-file warnings and verified guest readback stay.
assert 'session.sourceIssue=reasonCode(r["reason"] | "")' in source
assert '"source_error"' in web and '"last_source_error"' in web
assert "lastImportSourceIssue.store(" in gate
assert "importPartialPossible.store(" in bdos
assert "FILE_IMPORT_VERIFY_FAILED" in bdos
assert "CpmSdImport::preflight(*gate)" in cpu
assert "CpmSdImport::execute(&ebtksCpuBridge" in cpu

# Crash-specific tracing is no longer compiled or exposed.
assert not (root / "cpm_import_diag.h").exists()
for name, data in {
    "ESP32_Z80_Emulator.ino": ino,
    "cpu_redcode.h": cpu,
    "ebtks_cpm_sd_import.h": source,
    "cpm_file_write_service.h": bdos,
    "ebtks_web.h": web,
}.items():
    assert "CpmImportDiag" not in data, name
    assert "traceBdos" not in data, name
assert '"/api/cpm/service/diag"' not in web
assert "TraceCallback" not in bdos
print("PASS: B2c 8KiB stack, preflight/recovery/error safeguards, no RTC tracing")
