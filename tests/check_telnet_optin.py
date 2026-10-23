#!/usr/bin/env python3
"""Check compile-time Telnet switch on both existing full-firmware targets.

CI runs this only after rebuilding esp32dev and esp32dev-tft with
-D EBTKS_TELNET_ENABLE=1, without adding a third named PIO environment.
Default binaries were tested with Telnet off earlier in the workflow.
"""
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[1]
targets=sys.argv[1:] or ("esp32dev", "esp32dev-tft")
assert all(t in ("esp32dev","esp32dev-tft") for t in targets)
for target in targets:
    image=(root/".pio"/"build"/target/"firmware.bin").read_bytes()
    for symbol in (b"Telnet port 23 listener started",
                   b"New Telnet client @",
                   b"Telnet TX %s; dropping client"):
        assert symbol in image, f"Telnet opt-in missing in {target}: {symbol!r}"
    for symbol in (b"/z80ws",b"/ws",b"/cpmmedia",
                   b"/api/system/memory",b"EBTKS Z80 Terminal"):
        assert symbol in image, f"Shared service missing in {target}: {symbol!r}"
    tft_marker=b"TFT ST7796 terminal mirror starting"
    assert (tft_marker in image)==(target=="esp32dev-tft"), target
print("PASS: Telnet opt-in compiled in both existing bundles, TFT and Web intact")
