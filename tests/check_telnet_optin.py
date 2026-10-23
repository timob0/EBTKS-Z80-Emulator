#!/usr/bin/env python3
"""After a compile-time override, verify optional TCP/23 was truly linked.

Run only after the CI smoke-build of env:esp32dev with
PLATFORMIO_BUILD_FLAGS containing -DEBTKS_TELNET_ENABLE=1.
The default two-target binary validation runs BEFORE this smoke test.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
image=(root/".pio"/"build"/"esp32dev"/"firmware.bin").read_bytes()
for symbol in (b"Telnet port 23 listener started",
               b"New Telnet client @",
               b"Telnet TX %s; dropping client"):
    assert symbol in image, f"Telnet opt-in missing linked symbol {symbol!r}"
for symbol in (b"/z80ws",b"/ws",b"/cpmmedia",
               b"/api/system/memory",b"EBTKS Z80 Terminal"):
    assert symbol in image, f"Shared service missing with Telnet opt-in: {symbol!r}"
assert b"TFT ST7796 terminal mirror starting" not in image
print("PASS: Telnet opt-in linked in existing esp32dev without losing Web/CPM")
