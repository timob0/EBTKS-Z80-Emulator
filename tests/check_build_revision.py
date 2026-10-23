#!/usr/bin/env python3
"""Check that a PlatformIO firmware image contains its exact Git HEAD SHA.

Called from CI after both ESP32 targets are compiled. It detects cases where
an incremental build reuses a stale object/header and would misidentify an OTA.
"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
sha = subprocess.check_output(
    ["git", "rev-parse", "--verify", "HEAD"], cwd=root, text=True
).strip()
assert re.fullmatch(r"[0-9a-f]{40}", sha), sha

for target in ("esp32dev", "esp32dev-tft", "esp32dev-tft-only"):
    directory = root / ".pio" / "build" / target
    header = (directory / "ebtks_build_revision.h").read_text(encoding="utf-8")
    assert f'#define EBTKS_BUILD_GIT_COMMIT "{sha}"' in header, target
    assert "EBTKS_BUILD_GIT_DIRTY" not in header, target
    binary = (directory / "firmware.bin").read_bytes()
    assert sha.encode("ascii") in binary, f"commit not embedded in {target} firmware.bin"

print(f"PASS: all three ESP32 images contain full Git build ID {sha}")
