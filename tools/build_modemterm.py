#!/usr/bin/env python3
"""Create the stand-alone CP/M 2.2 MODEM.COM (8.3-safe), no assembler required.

The matching Z80 assembly source is tools/modemterm.asm.
--check runs self-tests without writing to disk.
"""
import argparse
from pathlib import Path

ORG = 0x0100


def program():
    out = bytearray()
    labels = {}
    fixups = []

    def label(name):
        assert name not in labels
        labels[name] = ORG + len(out)

    def emit(*items):
        out.extend(items)

    def absolute(opcode, name):
        emit(opcode, 0, 0)
        fixups.append((len(out) - 2, name))

    label("start")
    absolute(0x11, "hello")  # LD DE, greeting
    emit(0x0E, 0x09, 0xCD, 0x05, 0x00)  # BDOS fn 9
    label("loop")
    emit(0x1E, 0xFF, 0x0E, 0x06, 0xCD, 0x05, 0x00, 0xB7)  # BDOS6, OR A
    absolute(0xCA, "checkrx")  # JP Z
    emit(0xFE, 0x18)  # Ctrl+X
    absolute(0xCA, "quit")
    emit(0x47)  # LD B,A
    label("txwait")
    emit(0xDB, 0x8D, 0xE6, 0x20)  # IN LSR; AND THRE
    absolute(0xCA, "txwait")
    emit(0x78, 0xD3, 0x88)  # LD A,B; OUT THR
    label("checkrx")
    emit(0xDB, 0x8D, 0xE6, 0x01)  # RX data ready?
    absolute(0xCA, "loop")
    emit(0xDB, 0x88, 0x5F, 0x0E, 0x02, 0xCD, 0x05, 0x00)  # BDOS output
    absolute(0xC3, "loop")
    label("quit")
    emit(0xC9)
    label("hello")
    out.extend(
        b"\r\nMODEMTERM - CP/M Wi-Fi modem at 88h\r\n"
        b"Type AT or ATDTvert.synchro.net:23\r\n"
        b"Ctrl+X exits; +++ (guard time) enters command mode.\r\n$"
    )
    for offset, name in fixups:
        target = labels[name]
        out[offset] = target & 0xFF
        out[offset + 1] = target >> 8
    return bytes(out), labels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default="MODEM.COM")
    ap.add_argument("--check", action="store_true", help="verify image only")
    opts = ap.parse_args()
    payload, labels = program()
    assert payload[:1] == b"\x11"  # LD DE, text address
    assert payload[labels["quit"] - ORG] == 0xC9
    assert payload[labels["hello"] - ORG:] .endswith(b"$")
    assert payload.count(b"\xDB\x8D") == 2
    assert b"\xD3\x88" in payload and b"\xDB\x88" in payload
    assert len(payload) < 512
    if not opts.check:
        Path(opts.output).write_bytes(payload)
        print(f"Wrote {opts.output}: {len(payload)} bytes")
    else:
        print(f"PASS: MODEM.COM {len(payload)} bytes, valid entry/BIOS-independent I/O")


if __name__ == "__main__":
    main()
