#!/usr/bin/env python3
"""Validate the two shipped binaries: same services, only TFT differs."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
names = ("esp32dev", "esp32dev-tft")
images = {name: (root / ".pio" / "build" / name / "firmware.bin").read_bytes()
          for name in names}
for name, image in images.items():
    for feature in (b"/crt", b"/cpmmedia", b"/api/system/memory", b"/updater",
                    b"/ws", b"/z80/status", b"/api/cpm/media", b"/z80ws",
                    b"EBTKS Z80 Terminal"):
        assert feature in image, f"{feature!r} missing from {name}"
    # Telnet must be compiled OUT of both default firmware bundles. Only
    # adding EBTKS_TELNET_ENABLE=1 should enable its listener and TX path.
    for telnet in (b"Telnet port 23 listener started",
                   b"New Telnet client @",
                   b"Telnet TX %s; dropping client"):
        assert telnet not in image, f"{telnet!r} unexpectedly present in {name}"
assert b"TFT ST7796 terminal mirror starting" not in images["esp32dev"], (
    "TFT unexpectedly enabled in headless firmware"
)
assert b"TFT ST7796 terminal mirror starting" in images["esp32dev-tft"], (
    "TFT missing from display firmware"
)
print("PASS: exactly two full Web/CPM bundles; TFT isolated; default Telnet absent")
