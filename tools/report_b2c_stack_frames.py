#!/usr/bin/env python3
"""Extract Xtensa function-frame sizes from an already-built ESP32 ELF.

This tool DOES NOT build or flash firmware. It identifies the Xtensa "entry
a1, N" prologue of emitted functions, including binary framework libraries.
It is a frame inventory, NOT a call-chain maximum: inlining, indirect calls,
non-entry prologues and ISR/library frames require manual examination.
"""
import argparse
import re
import shutil
import subprocess
from pathlib import Path

NAMES = (
    "CPUTask", "CpmSdImport", "CpmFileWriteService",
    "CpmDirectoryService", "z80BridgeRun", "z80_run",
    "z80BridgeOut", "z80BridgeOpcode", "ebtksBridgePortOut",
    "portOut", "diskWrite", "cpmMountedDiskWrite",
    "mbedtls_base64_encode", "ebtksStorageExchange",
    "ebtksLinkTransaction", "serializeJson", "deserializeJson",
    "HardwareSerial::print", "HardwareSerial::write",
    "uxTaskGetStackHighWaterMark",
)
HEADER = re.compile(r"^\s*([0-9a-fA-F]+)\s+<(.+)>:\s*$")
ENTRY = re.compile(r"\bentry\s+a1,\s*(0x[0-9a-fA-F]+|\d+)\b")


def parse_disassembly(lines):
    result = []
    current = None
    count = 0
    for line in lines:
        heading = HEADER.match(line)
        if heading:
            if current:
                result.append(current)
            current = {"address": heading.group(1), "symbol": heading.group(2),
                       "frame": None}
            count = 0
            continue
        if current is not None and count < 24:
            count += 1
            match = ENTRY.search(line)
            if match and current["frame"] is None:
                value = match.group(1)
                current["frame"] = int(value, 16 if value.startswith("0x") else 10)
    if current:
        result.append(current)
    return result


def selftest():
    sample = [
        "400d0123 <CPUTask(void*)>:",
        "  400d0123: 008136 entry a1, 128",
        "  400d0126: 012345 call8 400d9999 <z80_run>",
        "400d0200 <mbedtls_base64_encode>:",
        "  400d0200: 004136 entry a1, 64",
        "400d0300 <small_function>:",
        "  400d0300: 004136 entry a1, 0x20",
    ]
    functions = parse_disassembly(sample)
    assert [x["frame"] for x in functions] == [128, 64, 32]
    assert functions[0]["symbol"] == "CPUTask(void*)"
    print("PASS: Xtensa function-frame disassembly parser")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="?", default=".pio/build/esp32dev-tft/firmware.elf",
                        help="Existing compiled ELF (no build/flash)")
    parser.add_argument("--objdump", default=None,
                        help="Explicit xtensa-esp32-elf-objdump path")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return
    elf = Path(args.elf).expanduser()
    if not elf.is_file():
        parser.error(f"ELF not found: {elf}")
    objdump = args.objdump or shutil.which("xtensa-esp32-elf-objdump")
    if not objdump:
        candidate = (Path.home() / ".platformio/packages/toolchain-xtensa-esp32/"
                     "bin/xtensa-esp32-elf-objdump")
        if candidate.is_file():
            objdump = str(candidate)
    if not objdump:
        parser.error("xtensa-esp32-elf-objdump unavailable; pass --objdump PATH")
    run = subprocess.run([str(objdump), "-Cd", str(elf)], text=True,
                         capture_output=True, check=False)
    if run.returncode:
        parser.error("objdump failed: " + run.stderr[-800:])
    functions = parse_disassembly(run.stdout.splitlines())
    print(f"ELF: {elf}\nFunctions disassembled: {len(functions)}")
    print("Frame bytes are compiler-emitted Xtensa entry allocations.\n")
    for term in NAMES:
        found = [f for f in functions if term in f["symbol"]]
        if not found:
            print(f"{term:34s} NOT A STANDALONE SYMBOL (possibly inlined)")
            continue
        found.sort(key=lambda f: (f["frame"] or -1), reverse=True)
        for f in found[:4]:
            frame = f["frame"]
            print(f"{term:34s} {str(frame)+' B' if frame is not None else 'unknown':>9s}"
                  f"  0x{f['address']}  {f['symbol'][:110]}")
        if len(found)>4:
            print(f"  (+{len(found)-4} more matches)")
    print("\nLIMITATION: Do NOT add these figures blindly. Live stack usage equals")
    print("the maximum of simultaneous frames on an ACTUAL execution path;")
    print("inlined functions, indirect callbacks, and external library calls")
    print("need disassembly/call-graph review. A high-water mark is historical.")
    print("No firmware change, build or flash was performed.")


if __name__ == "__main__":
    main()
