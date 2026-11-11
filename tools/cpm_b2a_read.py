#!/usr/bin/env python3
"""Read-only B2a: export up to 64 native CP/M records over the CCP service API.

The firmware limits B2a to records 0..63. This script only publishes the
output after a verified EOF and CRC on every 128-byte record; no CP/M writes.
"""
import argparse
import binascii
import json
import os
import pathlib
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

def request_json(url, method="GET"):
    req = urllib.request.Request(url, data=b"" if method == "POST" else None,
                                 method=method)
    with urllib.request.urlopen(req, timeout=8) as response:
        return json.load(response)

def main():
    parser = argparse.ArgumentParser(description="Export a small CP/M file through B2a BDOS")
    parser.add_argument("base", help="ESP32 URL e.g. http://192.168.1.50")
    parser.add_argument("source", help="CP/M 8.3 file, e.g. A:STAT.COM")
    parser.add_argument("destination", type=pathlib.Path, help="New Mac output path")
    args=parser.parse_args()
    base=args.base.rstrip("/")
    if not base.startswith("http://"):
        parser.error("Use an explicit http:// ESP32 LAN URL")
    if args.destination.exists():
        parser.error("Destination exists: refusing overwrite")
    parts=[]
    max_records=64
    try:
        for record in range(max_records):
            deadline=time.monotonic()+25
            while True:
                st=request_json(base+"/api/cpm/service/status")
                if st.get("ccp_idle"):
                    break
                if time.monotonic()>deadline:
                    raise RuntimeError("CP/M not at verified CCP idle prompt")
                time.sleep(0.25)
            params=urllib.parse.urlencode({"name":args.source,"record":record})
            result=request_json(base+"/api/cpm/service/file?"+params, "POST")
            if not result.get("accepted"):
                raise RuntimeError(f"request rejected for record {record}: {result}")
            deadline=time.monotonic()+30
            while True:
                result=request_json(base+"/api/cpm/service/file")
                state=result.get("state")
                if state in ("passed","failed","cancelled_nonempty_command",
                             "cancelled_warm_boot"):
                    break
                if time.monotonic()>deadline:
                    raise RuntimeError(f"read timeout at record {record}: {result}")
                time.sleep(0.25)
            if state!="passed" or result.get("file_error")!="none":
                raise RuntimeError(f"BDOS error record {record}: {result}")
            if result.get("eof"):
                print(f"EOF after {record} record(s) / {len(parts)*128} bytes")
                break
            if result.get("encoding")!="hex" or not result.get("has_data"):
                raise RuntimeError(f"no binary record returned: {result}")
            payload=bytes.fromhex(result["data"])
            if len(payload)!=128 or result.get("length")!=128:
                raise RuntimeError("expected exactly 128 record bytes")
            crc=binascii.crc_hqx(payload,0xFFFF)
            if crc!=result["crc16_ccitt_false"]:
                raise RuntimeError(f"CRC mismatch record {record}: {crc:#06x}")
            parts.append(payload)
            print(f"record {record:2d} OK  crc={crc:04X}")
        else:
            raise RuntimeError("B2a diagnostic limit hit (64 records); no complete export")
        args.destination.parent.mkdir(parents=True,exist_ok=True)
        fd,tmp=tempfile.mkstemp(prefix=".cpm-b2a-",dir=str(args.destination.parent))
        try:
            with os.fdopen(fd,"wb") as out:
                for block in parts:
                    out.write(block)
                out.flush()
                os.fsync(out.fileno())
            if args.destination.exists():
                raise FileExistsError("Destination appeared while reading")
            os.link(tmp,args.destination)  # atomic no-overwrite publication
        finally:
            os.unlink(tmp)
        print(f"Exported {len(parts)*128} CP/M logical bytes to {args.destination}")
        print("Note: CP/M 2.2 has 128-byte records; exact historical host byte length unknown")
    except (urllib.error.URLError,ValueError,RuntimeError,OSError) as exc:
        parser.exit(1,f"B2a aborted without publishing output: {exc}\n")

if __name__=="__main__":
    main()
