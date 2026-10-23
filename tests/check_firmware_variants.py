#!/usr/bin/env python3
"""Verify compiled display isolation, not just PlatformIO flag names."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
names = ("esp32dev", "esp32dev-tft", "esp32dev-tft-only")
images = {name: (root / ".pio" / "build" / name / "firmware.bin").read_bytes()
          for name in names}
for name, image in images.items():
    for feature in (b"/crt", b"/cpmmedia", b"/api/system/memory", b"/updater",
                    b"/ws", b"/z80/status", b"/api/cpm/media"):
        assert feature in image, f"{feature!r} missing from {name}"
for name in ("esp32dev", "esp32dev-tft"):
    assert b"/z80ws" in images[name], f"Web Terminal socket missing from {name}"
    assert b"EBTKS Z80 Terminal" in images[name], f"Web Terminal HTML missing from {name}"
assert b"/z80ws" not in images["esp32dev-tft-only"], "Browser socket retained in TFT-only"
assert b"EBTKS Z80 Terminal" not in images["esp32dev-tft-only"], "Browser HTML retained in TFT-only"
assert b"TFT ST7796 terminal mirror starting" not in images["esp32dev"], "TFT enabled in headless"
for name in ("esp32dev-tft", "esp32dev-tft-only"):
    assert b"TFT ST7796 terminal mirror starting" in images[name], f"TFT missing from {name}"
print("PASS: three linked firmware images isolate Web/TFT and retain shared services")
