# Pinned third-party Z80 CPU library

The files `Z80.c` and `Z80.h` in this directory are unmodified copies of
[redcode/Z80](https://github.com/redcode/Z80) by Manuel Sainz de Baranda y Goñi,
pinned at upstream Git commit:

`ccc8f22aa96c1f01df767b0ed2fffacbc6e779b8`

Upstream Z80 CPU emulator license: **LGPL-3.0-or-later**.
Copyright (C) 1999–2026 Manuel Sainz de Baranda y Goñi.
The verbatim upstream `COPYING` and `COPYING.LESSER` files are included here.

`ebtks_zeta_compat.h` is an EBTKS-specific portability adapter created for
the little-endian 32-bit/64-bit hosts and ESP32 target; it replaces the optional
Zeta headers via the upstream-supported `Z80_EXTERNAL_HEADER` mechanism.
It is *not* a replacement for the upstream library's opcode implementation.

The prototype links the library statically into firmware. Before distributing
combined firmware releases, confirm full LGPL-3.0-or-later compliance, including
the appropriate notices, corresponding library source and users' ability to
rebuild/relink a modified library. No LGPL compatibility or compiled footprint
claim has yet been verified by a firmware build.

To compile native tests: `bash tests/run_z80_core_native.sh`.
To restore the original CPU for A/B comparison on the ESP32 fix branch, add
`-DEBTKS_Z80_LEGACY_CPU` to the PlatformIO environment's `build_flags`.
The user-tested main firmware has not been changed.
