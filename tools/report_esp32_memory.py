#!/usr/bin/env python3
"""Static ESP32 RAM audit from a PlatformIO firmware.elf image.

Usage from repository root:
  python3 tools/report_esp32_memory.py .pio/build/esp32dev-tft/firmware.elf

This measures ELF-linked static RAM, NOT Wi-Fi/FreeRTOS/task/driver heap.
For runtime allocator statistics use /api/system/memory/pools.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def find_tool(name):
    local = (Path.home() / ".platformio" / "packages" /
             "toolchain-xtensa-esp32" / "bin" / ("xtensa-esp32-elf-" + name))
    return str(local) if local.is_file() else shutil.which("xtensa-esp32-elf-" + name)


def output(cmd):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--top", type=int, default=40)
    args = parser.parse_args()
    if not args.elf.is_file():
        parser.error(f"ELF does not exist: {args.elf}")
    nm = find_tool("nm")
    size = find_tool("size")
    if not nm or not size:
        parser.error("Xtensa nm/size not found in ~/.platformio/packages/toolchain-xtensa-esp32/bin")
    sections = output([size, "-A", str(args.elf)])
    symbols = output([nm, "--print-size", "--defined-only",
                      "--radix=x", "-C", str(args.elf)])
    print("EBTKS ESP32 static RAM ELF audit")
    print("ELF:", args.elf)
    print("Note: heap allocations (Wi-Fi, FreeRTOS stacks, DMA, CP/M shadow)")
    print("are not included in ELF static symbol sizes.")
    print("\nRAM linker sections (bytes):")
    for line in sections.splitlines():
        if re.match(r"\s*\.(?:dram|iram|rtc|bss|data)", line, re.I):
            print(" ", line.strip())
    pattern = re.compile(r"^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([bBdDcCsS])\s+(.+)$")
    hits = []
    for line in symbols.splitlines():
        m = pattern.match(line)
        if m:
            hits.append((int(m.group(2), 16), m.group(3), m.group(4)))
    print(f"\nLargest defined BSS/DATA symbols (top {args.top}):")
    for length, kind, name in sorted(hits, reverse=True)[:args.top]:
        print(f"{length:>8} bytes [{kind}] {name}")
    print(f"\nParsed {len(hits)} defined BSS/DATA symbols.")
    if not hits:
        parser.error("No symbols parsed: check toolchain nm output format")


if __name__ == "__main__":
    main()
