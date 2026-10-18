# Z80 opcode compatibility audit — ESP32 core

**Status: source audit only, 2026-10-19. No emulator instruction changes, new firmware build, or hardware validation yet.**

**Repository/branch:** [timob0/EBTKS-Z80-Emulator, `fix/z80-cpu-compatibility`](https://github.com/timob0/EBTKS-Z80-Emulator/tree/fix/z80-cpu-compatibility), created **with explicit user approval** from user-tested `main` commit `49e6eef341df4a768c9af28d09949e6e36ef6127`. The unrelated Teensy 4.1 repository remains on `main` and must not be modified.

## Hardware reproduction (historical, user-provided)

- Turbo Pascal 3.01A: `Unknown OP-Code ED 52 at 231B` (SBC HL,DE), `Unknown OP-Code DD 2A at 172B` (LD IX,(nn)), then `Z80 halted @ D001 - restarting virtual machine`; Z80 cold bootstrap, not an observed ESP32 reset.
- TINST: `Unknown OP-Code ED 42 at 04E1` (SBC HL,BC), `Unknown OP-Code DD E9 at 054C` (JP (IX)), then CP/M WBOOT rather than cold restart. Terminal configuration of original Turbo is a separate Microbee-vs-VT100 issue.
- `GET /z80debug` contains CPU logs because the application aliases `Serial` to `EbtksDebugSerial`.

## Static coverage census from `ESP32_Z80_Emulator/cpu.h` on current main

The census counts **explicit switch cases, not correctly executed instructions**:

| Decoder | Explicit cases | Meaning |
|---|---:|---|
| Base unprefixed | 256/256 | All byte values dispatched, **not** verified correct (e.g. arithmetic/flags contain errors) |
| CB | 35/256 | 213 of 248 officially documented CB encodings absent (other 8 are undocumented SLL/SLS) |
| ED | 19/256 | 18 of 58 distinct canonical documented ED encodings present; 40 canonical documented ED instructions absent, plus aliases/undocumented behavior. One implemented encoding (`ED 71`) is normally undocumented. |
| DD (IX) | 6 explicit cases | `21,23,36,77,E1,E5` only |
| FD (IY) | 3 explicit cases | `7E,E1,E5` only |
| DD CB / FD CB | 0 | No indexed rotate/shift/bit/reset/set decoding |

**Important:** DD/FD prefixes frequently inherit ordinary opcodes when no IX/IY substitution is needed. Therefore missing 250 DD or 253 FD explicit cases is **not** the number of unique missing instructions; proper prefix processing must support substitution, ignored-prefix opcodes, displacement, and multi-prefix behavior.

### Canonical missing ED opcode groups

| Family | Missing canonical encodings |
|---|---|
| `IN r,(C)` | `40,48,50,58,60,68` (only `78` handled) |
| `OUT (C),r` | None of 7 canonical forms (all implemented; `71` additionally implemented) |
| `SBC HL,rr` | `42,52,62,72` (all four missing) |
| `ADC HL,rr` | `4A,6A,7A` (only `5A` handled) |
| `LD (nn),rr` | `63,73` (43,53 handled) |
| `LD rr,(nn)` | `4B,6B,7B` (5B handled) |
| `NEG` / `RETI` | `44,4D` (`RETN 45` handled) |
| Interrupt modes | `46` IM0, `5E` IM2 (only IM1 56 handled; interrupts themselves need separate semantics review) |
| I and R register transfers | `47,4F,57,5F` (all missing) |
| `RRD` | `67` (RLD 6F handled) |
| Block move/compare/I/O | `A0,A2,A3,A8,A9,AA,AB,B2,B3,B8,B9,BA,BB` (only A1 CPI, B0 LDIR, B1 CPIR handled) |

**Separate alternate ED encodings** (e.g. repeated NEG/RETN/interrupt-mode opcodes) require a deliberate policy. The program-compatible documented forms above are the first target.

### Missing CB / indexed examples

- CB includes all `RES b,r` and `SET b,r` opcodes (`80`–`FF`), many BIT encodings, and shifts/rotates not listed in its 35 cases.
- IX/IY pair operations absent: `LD IX/IY,(nn)` (`DD/FD 2A`), `LD (nn),IX/IY` (`DD/FD 22`), `JP (IX/IY)` (`DD/FD E9`), `LD SP,IX/IY` (`DD/FD F9`), pair arithmetic, `DEC`, indexed register/memory arithmetic and memory loads. `FD 21` (LD IY,nn) is also absent.
- Indexed bit operations `DD/FD CB d op` are completely unimplemented. The `d` byte must be consumed as a signed displacement.
- Unknown DD/FD forms currently just log and leave instruction operands unconsumed, potentially desynchronizing PC and causing a virtual-machine halt.

### Confirmed non-coverage correctness defects

- `ED B0 LDIR`: final HL/DE reconstruct with `/16` and `0x0f` rather than high/low **bytes** (`>>8`, `&0xff`); P/V should be **clear** when BC becomes zero.
- `ED B1 CPIR`: `Zf == false;` is a no-op; final H assigned `V16/16`, not `>>8`; search and flags need independent correctness tests.
- `CPU_DD` / `CPU_FD` signed displacement: `0xFF` (−1) incorrectly addresses `IX+127` / `IY+127` instead of minus one.
- `ADD8`: `Pf = bitRead(ci,1)` reads bit 1 from a value holding only 0/1; computed signed-overflow P/V is wrong. Carry and half-carry paths merit exhaustive vectors.
- Many `Nf - false;` and `Hf == false;` expressions do not modify flags; 16-bit `ADC HL,DE` overflow logic incorrectly conflates carry and overflow.
- Current code is not cycle-accurate and leaves interrupts partially stubbed; complete fidelity is a distinct scope from CP/M application compatibility.

## Proposed safe implementation/test order

1. Build a **host-native automated CPU test harness** independent of ESP32 Wi-Fi/Teensy/SD, with deterministic RAM, PC, registers and flags; identify current behavior before editing the decoder. Record opcode coverage (including CB, ED, DD/FD and DD/FD CB) and known-invalid streams. Keep architecture and productive SPI read-ahead untouched.
2. Implement missing ED `SBC HL,rr`, missing IX/IY instructions encountered in Turbo/TINST, and correct operand consumption / signed displacements. Add exact register/flags/PC/memory regression vectors.
3. Expand documented CB, ED and IX/IY coverage, including indexed CB and prefix fallback, while fixing arithmetic/flag and LDIR/CPIR bugs with reference vectors (zero BC, carry/borrow, overflow, wraparound).
4. Local PlatformIO compile (ESP32 only), then user HP-85 regression: CP/M boots, Turbo/TINST, Multiplan, Zork, WBOOT, disk write/readback, SPI read-ahead, Web UI and Telnet. **Do not claim fixed until tested on user's hardware.**

**Reference:** Zilog Z80 CPU User Manual UM0080 (https://www.zilog.com/docs/z80/um0080.pdf). Avoid conflating absent unassigned ED bytes with missing documented operations or counting DD/FD ignored-prefix variants as distinct instructions.

**Branch policy:** This branch was explicitly authorized by the user for ESP32 CPU repair; ask again before creating any *additional* branches. Keep Teensy `main` untouched, and merge only with the user's hardware verification.

## External core replacement assessment (2026-10-19) — user-suggested picoZ80

**User suggestion:** [picoZ80 tagged project link](https://git.eaw.app/eaw/pico/src/tag/BaseZ80-v2.676/projects/Z80). The picoZ80 project's published README instructs developers to clone the **separate** CPU library [Manuel Sainz de Baranda's `redcode/Z80`](https://github.com/redcode/Z80) into `projects/Z80`. The specific tag page was not accessible to the research browser, so the exact revision of library underlying the tag is **not independently confirmed**. We inspected the upstream library directly instead (current `master` commit `ccc8f22aa96c1f01df767b0ed2fffacbc6e779b8`, at time of evaluation); **pin an immutable reviewed commit** for any actual integration rather than following moving `master`.

**Decision:** Replacing just the old ESP32 instruction engine is preferable to hand-implementing hundreds of missing Z80 opcodes. The upstream core is portable ANSI C, full documented/undocumented Z80 coverage is claimed, and the README documents passing the principal Z80 instruction-exerciser suites. This is a strong candidate, not yet an actual ESP32 compile/footprint/CP/M compatibility test.

**API verified in `redcode/Z80/API/Z80.h` and `redcode/Z80/sources/Z80.c`:**
- `struct Z80` contains CPU state, callback pointers and a context. It has `fetch_opcode`, `fetch`, `read`, `write`, `in`, `out`, optional `halt`, `hook` and other notification callbacks; `Z80Read/Z80Write` signatures pass a context and **16-bit** address/port.
- `z80_run(&cpu, cycles)` provides bounded execution and actual cycle count; `z80_break(&cpu)` stops execution after the current emulation step when called inside a callback. `z80_power`, `z80_instant_reset`, `z80_int`, `z80_nmi` provide reset/interrupt controls. `Z80_PC(cpu)`, `Z80_SP(cpu)`, `Z80_IX(cpu)` etc. expose register pairs; flags are an 8-bit F byte.
- Actual source is one portable C implementation (about 132 KiB of *source text*, NOT a measured runtime RAM cost) plus public header; depends by default on the header-only Zeta types/macros but can use `Z80_EXTERNAL_HEADER` adapter. For PlatformIO, compile the C source as C and expose the API to Arduino C++; don't copy the Pico RP2350 PIO, memory-management or ESP32-S3 architecture.
- **Memory mapping:** all fetch/read/write callbacks use the existing ESP32 `RAM[65536]`; no additional 64 KiB RAM map is required. Existing `portIn(uint8_t)`, `portOut(uint8_t,uint8_t)` and disk controller/UART service remain, adapting upstream 16-bit Z80 port addresses to the currently emulated low 8 bits where appropriate.
- **Integration risks requiring tests:** the existing `cpu.h` FreeRTOS task, `RUN` stop/reset, `SingleStep`, `BP` breakpoints, CP/M WBOOT stop **before** the BIOS WBOOT instruction at `CPM_WBOOT_ENTRY`, `HALT`-triggered VM restart vs the upstream core's normal HALT state, reset to PC=0 with coherent registers and IFF state, `portOut` DCMD 8 bootstrap/PC reset, sufficient yields during disk I/O, UART console pacing and BIOS/BDOS resident shadow. A fetched instruction must not run unintentionally across WBOOT or Reset because `z80_break` ends after the current step. Prefer a tested bounded execution scheduler and explicit trap interception.
- Keep current ESP32 SPI read-ahead, Teensy UART link, SD mounts, Wi-Fi/OTA and HP-85 hardware ISR **untouched**. The Teensy repo is not involved.
- **Licensing:** the original `redcode/Z80` implementation states **LGPL v3 or later** (file `COPYING.LESSER`), whereas the broader picoZ80 firmware has its own **GPLv3** license / separate noncommercial statements. Respect the *specific third-party component's* license and obligations; do not assume wholesale copying of picoZ80 firmware is licensed identically. The existing EBTKS repo has no obvious top-level LICENSE. Resolve notices, license inclusion and corresponding-source/relinking requirements before distributing a combined firmware.

**Gate for implementation:** on the already user-approved ESP32 `fix/z80-cpu-compatibility` branch, bring in a pinned upstream Z80 library as a separate component, first compile/run a host-only CPU test using actual memory/I/O callbacks, then build ESP32 with PlatformIO and measure DRAM, largest allocatable heap, binary size, stack and execution throughput. Prove the four Turbo/TINST opcodes, WBOOT, reset, HALT, and representative CP/M programs. The user must flash/test the ESP32 before merging. Keep the user-tested ESP32 `main` unchanged until then, and **do not create another branch without explicit approval**.

