#!/usr/bin/env python3
"""Verify compiled display isolation, not just PlatformIO flag names."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
names = ("esp32dev", "esp32dev-tft", "esp32dev-tft-only", "esp32dev-tft-no-telnet")
images = {name: (root / ".pio" / "build" / name / "firmware.bin").read_bytes()
          for name in names}
for name, image in images.items():
    for feature in (b"/crt", b"/cpmmedia", b"/api/system/memory", b"/updater",
                    b"/ws", b"/z80/status", b"/api/cpm/media"):
        assert feature in image, f"{feature!r} missing from {name}"
for name in ("esp32dev", "esp32dev-tft", "esp32dev-tft-no-telnet"):
    assert b"/z80ws" in images[name], f"Web Terminal socket missing from {name}"
    assert b"EBTKS Z80 Terminal" in images[name], f"Web Terminal HTML missing from {name}"
assert b"/z80ws" not in images["esp32dev-tft-only"], "Browser socket retained in TFT-only"
assert b"EBTKS Z80 Terminal" not in images["esp32dev-tft-only"], "Browser HTML retained in TFT-only"
assert b"TFT ST7796 terminal mirror starting" not in images["esp32dev"], "TFT enabled in headless"
for name in ("esp32dev-tft", "esp32dev-tft-only", "esp32dev-tft-no-telnet"):
    assert b"TFT ST7796 terminal mirror starting" in images[name], f"TFT missing from {name}"
# Verify actual compiled binaries exclude the TCP/23 listener and TX code,
# not merely that PlatformIO carries a Telnet=0 preprocessor flag.
assert b"Telnet port 23 listener started" not in images["esp32dev-tft-no-telnet"]
assert b"New Telnet client @" not in images["esp32dev-tft-no-telnet"]
assert b"Telnet TX %s; dropping client" not in images["esp32dev-tft-no-telnet"]
for name in ("esp32dev", "esp32dev-tft", "esp32dev-tft-only"):
    assert b"Telnet port 23 listener started" in images[name], f"Telnet missing from {name}"
print("PASS: four linked firmware images isolate TFT/Web/Telnet and retain shared services")
