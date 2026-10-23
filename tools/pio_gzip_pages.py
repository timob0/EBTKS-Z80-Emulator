"""Build deterministic gzip copies of the five embedded HTML pages.

The editable HTML continues to live in the existing C++ raw literals. At
PlatformIO build time, gzip-compress those exact bytes into PROGMEM arrays.
Retain the original literals for HTTP clients without Accept-Encoding: gzip.

This module is also importable by the native asset regression test.
"""
from pathlib import Path
import ast
import gzip
import re


PAGES = (
    ("EBTKS_UPDATER_FALLBACK", "ESP32_Z80_Emulator/ebtks_web.h"),
    ("EBTKS_CRT_DISPLAY_PAGE", "ESP32_Z80_Emulator/crt_display_page.h"),
    ("CPM_MEDIA_PAGE", "ESP32_Z80_Emulator/cpm_media_page.h"),
    ("EBTKS_TERM_PAGE", "ESP32_Z80_Emulator/web_terminal_v2.h"),
)


def raw_literal(source, symbol):
    pattern = (
        r"static const char\s+" + re.escape(symbol)
        + r"\[\]\s+PROGMEM\s*=\s*R\"HTML\((.*?)\)HTML\";"
    )
    result = re.search(pattern, source, re.S)
    if not result:
        raise ValueError("Missing or modified raw HTML page " + symbol)
    return result.group(1).encode("utf-8")


def home_variants(source):
    # The Home link is intentionally made from adjacent C++ string literals.
    # Reconstruct exactly the bytes selected by each compile-time flag.
    pattern = (
        r"static const char\s+EBTKS_HOME_FALLBACK\[\]\s+PROGMEM\s*="
        r"\s*R\"HTML\((.*?)\)HTML\"\s+EBTKS_HOME_Z80_LINK"
        r"\s+R\"HTML\((.*?)\)HTML\";"
    )
    match = re.search(pattern, source, re.S)
    if not match:
        raise ValueError("Home link is not a valid adjacent raw-string expression")
    macros = re.findall(
        r'^#define EBTKS_HOME_Z80_LINK\s+("(?:\\.|[^"\\])*")',
        source, re.M,
    )
    if len(macros) != 3:
        raise ValueError("Expected Web, Telnet and physical-TFT Home links")
    before, after = match.groups()
    variants = []
    for macro in macros:
        link = ast.literal_eval(macro)
        variants.append((before + link + after).encode("utf-8"))
    return tuple(variants)


def html_pages(project):
    project = Path(project)
    home_source = (project / "ESP32_Z80_Emulator/ebtks_web.h").read_text(
        encoding="utf-8"
    )
    result = {
        "EBTKS_HOME_WEB": home_variants(home_source)[0],
        "EBTKS_HOME_TELNET": home_variants(home_source)[1],
        "EBTKS_HOME_TFT": home_variants(home_source)[2],
    }
    for symbol, relative in PAGES:
        source = (project / relative).read_text(encoding="utf-8")
        result[symbol] = raw_literal(source, symbol)
    return result


def c_array(name, compressed):
    rows = [
        "  " + ", ".join(f"0x{item:02x}" for item in compressed[i:i + 16]) + ","
        for i in range(0, len(compressed), 16)
    ]
    return (
        "// " + name + ": " + str(len(compressed)) + " bytes gzip\n"
        "static const uint8_t " + name + "_GZIP[] PROGMEM = {\n"
        + "\n".join(rows) + "\n};\n"
    )


def render_header(project):
    pages = html_pages(project)
    contents = [
        "// Automatically generated at build time from embedded C++ HTML.\n",
        "#pragma once\n",
        "#include <stdint.h>\n",
    ]
    for index, (variant, directive) in enumerate((
        ("EBTKS_HOME_WEB", "#if EBTKS_WEB_TERMINAL_ENABLE"),
        ("EBTKS_HOME_TELNET", "#elif EBTKS_TELNET_ENABLE"),
        ("EBTKS_HOME_TFT", "#else"),
    )):
        contents.append(directive + "\n")
        compressed = gzip.compress(pages[variant], compresslevel=9, mtime=0)
        # All modes reference one logical constant.
        contents.append(c_array("EBTKS_HOME", compressed))
    contents.append("#endif\n")
    for name, _ in PAGES:
        if name == "EBTKS_TERM_PAGE":
            contents.append("#if EBTKS_WEB_TERMINAL_ENABLE\n")
        compressed = gzip.compress(pages[name], compresslevel=9, mtime=0)
        contents.append(c_array(name, compressed))
        if name == "EBTKS_TERM_PAGE":
            contents.append("#endif\n")
    return "".join(contents)


def generate(project, build_directory):
    target = Path(build_directory) / "ebtks_static_pages_gzip.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    output = render_header(project)
    if not target.exists() or target.read_text(encoding="ascii") != output:
        target.write_text(output, encoding="ascii")
    pages = html_pages(project)
    for symbol, raw in pages.items():
        packed = gzip.compress(raw, compresslevel=9, mtime=0)
        print(f"EBTKS gzip {symbol}: {len(raw)} -> {len(packed)} bytes")
    return target


# PlatformIO pre-build context supplies Import(), not ordinary Python.
if "Import" in globals():
    Import("env")
    project = env.subst("$PROJECT_DIR")
    build_directory = env.subst("$BUILD_DIR")
    generated = generate(project, build_directory)
    env.Append(CPPPATH=[str(generated.parent)])
