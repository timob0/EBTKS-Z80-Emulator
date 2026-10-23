#!/usr/bin/env python3
"""Guard the ESP32 TFT static DRAM budget across toolchain/build differences.

This classic ESP32 build is unusually near dram0_0_seg's linker limit.  The
macOS Xtensa linker failed with a 16-byte overflow when the same TFT firmware
linked under GitHub CI.  PlatformIO's 327,680-byte "RAM" total includes areas
that cannot all hold static .dram0 data; it is NOT a safe static-BSS budget.

Baseline on last physically verified main ESP32 source eca19c576f68:
  .dram0.data = 28,400 bytes
  .dram0.bss  = 96,128 bytes  (total = 124,528)
Stage A first (failed on Mac despite CI success): total = 124,552.
Repaired Stage A (Gate allocated once on normal heap, reduced bridge state):
  total = 124,536.

Require no more than 16 bytes added over stable main, leaving a small buffer
below the Mac-failing size. For future development, reduce static allocation
or explicitly review and revise this baseline; never delete this check just
to force a feature through.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STABLE_STATIC_DRAM_BYTES = 124_528
MAX_NEW_STATIC_DRAM_BYTES = 16
LIMIT = STABLE_STATIC_DRAM_BYTES + MAX_NEW_STATIC_DRAM_BYTES

size = Path.home() / ".platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size"
if not size.is_file():
    raise SystemExit(f"Missing pinned Xtensa size tool: {size}")

# Independently enforce the Mac link-time limit for both TFT configurations.
# The Web Terminal is excluded only in tft-only; neither variant may
# silently grow scarce classic ESP32 .dram0 static storage.
for target in ("esp32dev-tft", "esp32dev-tft-only"):
    elf = ROOT / ".pio" / "build" / target / "firmware.elf"
    if not elf.is_file():
        raise SystemExit(f"Missing TFT ELF: {elf}")
    report = subprocess.check_output([str(size), "-A", str(elf)], text=True)
    sections = {}
    for line in report.splitlines():
        m = re.match(r"^\\s*(\\.dram0\\.(?:data|bss))\\s+(\\d+)\\s+", line)
        if m:
            sections[m.group(1)] = int(m.group(2))
    if set(sections) != {".dram0.data", ".dram0.bss"}:
        raise SystemExit(f"Can't read {target} dram0 sections: {sections!r}")
    actual = sum(sections.values())
    print(
        f"{target} static DRAM: {actual:,} bytes "
        f"(.data {sections['.dram0.data']:,} + .bss {sections['.dram0.bss']:,}); "
        f"stable {STABLE_STATIC_DRAM_BYTES:,}, guard ceiling {LIMIT:,}"
    )
    if actual > LIMIT:
        print(f"FAIL: {target} static DRAM exceeds Mac-link guard", file=sys.stderr)
        sys.exit(1)
print("PASS: both TFT DRAM budgets remain below known macOS linker overflow")
