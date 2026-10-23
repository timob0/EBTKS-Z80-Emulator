#!/usr/bin/env python3
"""Verify gzip HTML bytes, navigation and all three Home-page variants.

This test runs on a host without PlatformIO or embedded libraries and checks
the exact compressed sources the build script will put in flash.
"""
from pathlib import Path
import gzip
import sys

project=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(project / "tools"))
from pio_gzip_pages import html_pages, render_header

pages=html_pages(project)
header=render_header(project)
assert len(pages)==7, pages.keys()
assert header.count("static const uint8_t EBTKS_HOME_GZIP[]") == 3
assert "#if EBTKS_WEB_TERMINAL_ENABLE" in header
assert "#elif EBTKS_TELNET_ENABLE" in header
assert "#else" in header
assert "#if EBTKS_WEB_TERMINAL_ENABLE\n" in header
assert "EBTKS_TERM_PAGE_GZIP[]" in header
for name, plain in pages.items():
    zipped=gzip.compress(plain,compresslevel=9,mtime=0)
    assert gzip.decompress(zipped)==plain
    assert zipped[:2]==b"\x1f\x8b"
    assert len(zipped)<len(plain), (name,len(plain),len(zipped))
    if name in ("CPM_MEDIA_PAGE","EBTKS_TERM_PAGE","EBTKS_CRT_DISPLAY_PAGE"):
        assert len(zipped)<len(plain)*0.7, (name,len(plain),len(zipped))
    print(f"PASS: {name}: {len(plain)} => {len(zipped)} bytes ({len(zipped)*100//len(plain)}%)")

home=pages["EBTKS_HOME_WEB"]
assert b"Z80 / CP/M Terminal</button>" in home
assert b"location.href='/z80'" in home
assert b"CP/M Media Manager" in home
assert b"Firmware Updater" in home
assert b'EBTKS_HOME_Z80_LINK' not in home
assert b')HTML"' not in home
assert b"Telnet port 23" in pages["EBTKS_HOME_TELNET"]
assert b"Z80 / CP/M Terminal</button>" not in pages["EBTKS_HOME_TFT"]
assert b"physical TFT" in pages["EBTKS_HOME_TFT"]
assert b"/ws" in pages["EBTKS_CRT_DISPLAY_PAGE"]
assert b"/api/cpm/media" in pages["CPM_MEDIA_PAGE"]
assert b"/z80ws" in pages["EBTKS_TERM_PAGE"]
assert b'action="/updater"' in pages["EBTKS_UPDATER_FALLBACK"]
assert "0x1f, 0x8b" in header
print("PASS: all seven static HTML variants decompress exactly; Home link syntax repaired")
