# Stage B2a — native CP/M BDOS 128-byte read-only file service

**Experimental / physical hardware acceptance pending, 2026-10-25.**
ESP32 only, `feature/cpm-file-transfer-b2a`, created from the **accepted
main** containing merged Stage B1 directory service. No HP Option ROM or
Teensy firmware modification. No RMIDLE/CHIDLE hook change, no printer,
disk image overwrite or CP/M filesystem write.

## Why this is safe to introduce incrementally

The accepted CCP `D382h` safe-point runs BDOS from the pinned Z80 CPU
task *only* after native CCP BDOS 10 has received a **verified empty**
command line. The existing auto-Return mechanism completes that native
BDOS input frame before the new operation starts. Nonempty commands,
foreground CP/M programs, unexpected CCP signatures and warm-boot
invalidate/reject requests. The normal `CPDCAT` directory operation
continues unchanged.

**New `READ_RECORD` operation:** strict drive-qualified CP/M 8.3
filename (A:–P:), current CP/M user area, a **zero-based 128-byte
record index 0–63**, and a read-only BDOS 15 OPEN → BDOS 20 READ
SEQUENTIAL (up to requested index) → BDOS 16 CLOSE. All BDOS
invocations use the previously accepted `CpmDirectoryService::invoke`
trampoline and trap. A CP/M EOF return (A=1) reports `eof=true`
and no payload. No file deletion, MAKE or WRITE operation exists.

The target FCB at `C020h` is **retained through every sequential
BDOS call in this single service execution**. Afterward, the Z80 CPU,
C000h–C0FFh scratch/FCB/stack, default DMA 0080h–00FFh, and
emulator trap/callback state are restored. BDOS internal state may
change as normal OPEN/READ/CLOSE side effects; this is not a
bitwise rollback of the entire BDOS workspace.

Result is exactly one 128-byte record (if present) or EOF. The result
snapshot is in the existing heap-allocated safe-point Gate, **not
another permanent `.bss` buffer**. The web task never reads guest
Z80 memory or invokes BDOS. Its read-only GET copies that Gate
snapshot and returns uppercase hexadecimal byte data, plus CRC-16/
CCITT-FALSE (poly 1021h, init FFFFh) for integrity checks.

A service request returning `202 Accepted` is **not a successful
read**; it is only armed. A later GET must report `state=passed`,
`file_error=none` and either 128 data bytes or `eof=true`.
If a BDOS call fails to return, the service restores its scratch
and requests a **Z80-only reset**, retaining the last failure and
forced-reset flag in the in-memory Gate for diagnostics.

## Manual test with curl

Back up your writable CP/M disk images, leave CP/M at a native
`A>` prompt, and substitute your ESP32's local IP:

```bash
curl -s http://<ESP32-IP>/api/cpm/service/status
curl -s -X POST 'http://<ESP32-IP>/api/cpm/service/file?name=A%3ASTAT.COM&record=0'
curl -s http://<ESP32-IP>/api/cpm/service/file
```

The POST should return `accepted:true`, HTTP 202; it will not
block. The empty-Return handshake is automatic when no user key or
terminal input is queued. Repeat the GET until `state=passed`
or a terminal failure. Successful read:

```json
{
  "supported":true,
  "operation":"file_read",
  "state":"passed",
  "checkpoint":"bdos_search_completed",
  "file_error":"none",
  "record_max":63,
  "record":0,
  "has_data":true,
  "eof":false,
  "length":128,
  "encoding":"hex",
  "crc16_ccitt_false":12345,
  "data":"<256 hexadecimal characters>"
}
```

`data` and CRC above are **examples**, not claimed physical output.
`record=1` requests the next record; a record at or beyond EOF
returns `has_data=false,eof=true,length=0`. `record=64` returns
HTTP 400. Missing files produce `file_not_found`, not a reset.
The exact filename must be present on the selected CP/M drive
and in the *current user area*. The filename parser refuses wildcards
and path separators. The diagnostic is limited to the first 64
records (8,192 bytes) **by design** to limit repeated scan work;
it is not yet a scalable transfer service.

## Mac-side diagnostic read

With a small known existing file such as `STAT.COM` in A:, run:

```bash
python3 tools/cpm_b2a_read.py http://<ESP32-IP> A:STAT.COM /tmp/STAT-B2A.COM
```

The stdlib-only script issues one authenticated-by-safe-point,
read-only POST/GET cycle per record, validates all 128 bytes and
CRC16, stops at CP/M EOF and atomically publishes a **new** local
file (never overwrites an existing destination). If it does not
find EOF before record 64, it aborts without publishing any file.
No part of this utility writes to CP/M, ESP32 storage, the Teensy SD
card or an HP-IB destination. A complete export contains **CP/M
logical record bytes**, which may include 1Ah padding; no host
source-byte-length metadata exists to strip it safely.

The diagnostic endpoint presently follows existing LAN API access
controls and is intended **only for trusted local networks**;
it exposes file contents. Do not expose the ESP32 Web UI to the
public Internet.

## Test progression and regressions

1. Native PC/CI tests: exact safe-point arming, 8.3 normalization,
   cancel after a nonempty command, BDOS OPEN/read/close synthetic
   Z80 code, CRC, EOF at the third record, missing file, bad DMA,
   interrupted BDOS requiring reset and byte-perfect CPU/TPA/DMA
   restoration. Both ESP32 firmware variants + TFT static DRAM guard
   must pass.
2. Mac linker of both targets with local PlatformIO before flashing
   ESP32. The accepted headless/tft bundles have tight RAM limits.
   Keep a known-good ESP32 recovery image and avoid recompiling
   Teensy or HP LOADBIN.
3. Physical read-only test: CP/M at `A>`, request one record
   of `A:STAT.COM`, verify plausible data and CRC. Check
   missing file `A:NOTHERE.TST` reports failed/not-found with no
   reboot, and record 64 is rejected before touching the Z80.
4. Read a second and later record, a file EOF, then export a small
   complete file using the Mac tool and compare it to a known
   reference (e.g. CP/M-side checksum/dump of the same logical
   records). Any discrepancy stops the milestone.
5. Run CP/M DIR, `CPDCAT "A:"` from HP BASIC, MODEM,
   Multiplan, Web/TFT/Telnet, Media Manager and
   `/api/system/health`/memory. Try a POST while an application
   runs (must reject), then a nonempty command (must cancel).
6. If a BDOS operation causes an unexpected Z80-only reset, capture
   `/api/cpm/service/file` sticky last-error and
   `/api/system/health`. Do **not** continue to writes.
   Never change the recovery binary on the accepted main branch.

**Not yet implemented:** arbitrary file sizes, efficient persistent
read handles/chunked streaming, exact original host-byte length,
Teensy SD staging, live CP/M file create/write/delete, HP `CPMCP`
or HP-IB transfer. Those are later B2 milestones, dependent on
successful physical B2a acceptance.
