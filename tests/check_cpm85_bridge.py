#!/usr/bin/env python3
"""Static regressions for the development-only CPM85 keyboard bridge."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
a=(root/"ESP32_Z80_Emulator/ebtks_cpm85_keyboard.h").read_text()
b=(root/"ESP32_Z80_Emulator/serial.h").read_text()
main=(root/"ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino").read_text()
pio=(root/"platformio.ini").read_text()
assert 'ebtksLinkTransaction(' in a and 'cpm_keys' in a
assert 'xQueueSend(keyQueue' in a and 'xQueueReceive(keyQueue' in a
keymap=(root/"ESP32_Z80_Emulator/ebtks_cpm85_keymap.h").read_text()
assert 'ebtks_cpm85_keymap.h' in a and 'toInputBytes(' in a
assert 'uxQueueSpacesAvailable(keyQueue)' in a
assert 'Cpm85Input::observeConsoleOutput(chunk,n);' in b
assert 'CursorModeTracker' in keymap
for code in ['hp == 0240', 'hp == 0217',
             'case 0234:', 'case 0235:', 'case 0241:', 'case 0242:',
             'case 0245:', 'case 0243:', 'case 0244:',
             'case 0236:', 'case 0237:', 'case 0200:', 'case 0206:',
             'hp == 0231', 'hp == 0232', 'hp == 0233', 'hp <= 0176']:
    assert code in keymap
assert 'code!=0207' in a
assert 'case 0255:' not in keymap
assert 'case 0222:' not in keymap
assert 'Cpm85Input::drain();' in b
assert '#include "ebtks_cpm85_keyboard.h"' in main
assert 'Cpm85Input::start();' in main
# Keyboard support is common to both published targets through inheritance.
assert pio.count('-DEBTKS_CPM85_KEYBOARD_ENABLE=1')==1
assert all(f'[env:{name}]' in pio for name in ('esp32dev', 'esp32dev-tft'))
assert 'build_flags =\n    ${env:esp32dev.build_flags}' in pio
assert 'TeensySerial.write(' not in a and 'SPI.transfer(' not in a
print('PASS: development-only CP/M keyboard bridge, no unsolicited UART/SPI')

# CPDMNT and CPDUMNT execute synchronously in the 3584-byte keyboard task,
# unlike Web Media Manager (which uses AsyncTCP). Each CpmMountEntry is ~99
# bytes, so the old two 16-drive stack arrays in cpmStoreSaveDesired took
# >3 KiB and could overflow the worker, resetting the entire ESP32/Z80.
store=(root/"ESP32_Z80_Emulator/cpm_mount_store.h").read_text()
assert 'xTaskCreatePinnedToCore(worker,"CPM85Keys",3584' in a
save=store.split("inline bool cpmStoreSaveDesired()",1)[1].split("inline bool cpmStoreFetchDiskDirectory(",1)[0]
verify=store.split("inline bool cpmStoreVerifyDesired()",1)[1].split("inline bool cpmStoreActivateMounts()",1)[0]
prefs=store.split("inline bool cpmStoreVerifyPrefs(",1)[1].split("inline bool cpmStoreSaveDesired()",1)[0]
assert "CpmMountEntry expected[CPM_DRIVE_COUNT]" not in save
assert "CpmMountEntry check[CPM_DRIVE_COUNT]" not in save
assert "CpmMountEntry check[CPM_DRIVE_COUNT]" not in verify
assert "cpmStoreWritePrefs(cpmDesiredMounts)" in save
assert "cpmStoreVerifyPrefs(cpmDesiredMounts)" in save
assert "cpmStoreVerifyPrefs(cpmDesiredMounts)" in verify
assert 'prefs.getUInt("magic", 0) == CPM_PREF_MAGIC' in prefs
assert 'prefs.getString(key, "")' in prefs
assert '"%s|%s|%s"' in prefs
assert 'actual.equals(canonical)' in prefs
assert 'if (!expected[i].mounted)' in prefs
assert 'for (uint8_t i = 0; i < CPM_DRIVE_COUNT && ok; ++i)' in prefs
print("PASS: HP drive-control NVS save/reset avoids worker stack overflow")

# New CPDIMGS/CPDMK: bounded HP mailbox, no ESP-side disk-image streaming.
control=(root/"ESP32_Z80_Emulator/cpm85_drive_control.h").read_text()
assert "if(op==5){images();return;}" in control
assert "if(op==6){makeImage(args);return;}" in control
# Validator must admit every operation that execute() dispatches. A stale
# opcode ceiling of 4 rejected both commands on physical HP-85 despite green CI.
import re
admitted=re.search(r'if\s*\(\s*op\s*<\s*1\s*\|\|\s*op\s*>\s*(\d+)\s*\|\|',control)
assert admitted, "missing command validator"
max_op=int(admitted.group(1))
dispatched=[int(op) for op in re.findall(r'if\s*\(\s*op\s*==\s*(\d+)\s*\)',control)]
assert dispatched and max_op==max(dispatched)==7, (max_op,dispatched)
assert 'setResult(false,"INVALID CP/M REQUEST")' in control
assert 'request["cpm_image"]["op"] = "begin"' in control
assert 'statusReq["cpm_image"]["op"] = "status"' in control
assert "CPM_NATIVE_BYTES" in control
assert 'ebtksStorageExchange(statusReq, statusReply, 3000)' in control
assert "IMAGE CREATE PENDING; CHECK CPDIMGS" in control
assert 'strcasecmp(dot, ".dsk")' in control
assert "LIST TRUNCATED" in control
# Ignore AppleDouble/hidden dot-files, including "._DISK.DSK".
assert "if (name[0] == '.') continue;" in control
assert control.index("if (name[0] == '.') continue;") < control.index("const char *dot = strrchr(name, '.');")
assert "memset(" not in control
print("PASS: CPDIMGS/CPDMK image commands use bounded directory/status messages")

# Opcode 7 returns read-only CP/M directory extents through the safe-point.
assert 'if(op==7){catalog(args);return;}' in control
assert 'gate->request(CpmSafePoint::DIRECTORY,drive+1)' in control
assert 'CPDCAT: CP/M BUSY OR COMMAND LINE NOT EMPTY' in control
assert 'snapshot.truncated' in control
assert '#include "cpm_catalog_format.h"' in control
assert 'for(uint8_t i=0;formatted && i<snapshot.count;i+=2)' in control
assert 'CpmCatalogFormat::formatRow(' in control
assert 'CpmCatalogFormat::kMaxRowBytes+51>=kTextMax' in control
assert 'EX%u' not in control.split('inline void catalog(',1)[1].split('inline void execute(',1)[0]
# 2026-10-24 hardware regression: the original catalog formatter compiled
# cleanly but emitted the two printable ASCII characters '\\' and 'n'
# instead of an actual ASCII LF. The HP CPM85 ROM translates LF to an HP
# native line break, so validate the DECODED C/C++ string literal, not just
# the presence of a backslash before 'n' in the source.
import ast
catalog=control.split("inline void catalog(",1)[1].split("inline void execute(",1)[0]
cpp_strings=re.findall(r'"(?:\\.|[^"\\])*"', catalog)
expected_lines=(
    "CP/M %c: DIRECTORY",
    "%u EXTENTS%s", "%u BAD NAME(S) SKIPPED",
)
for prefix in expected_lines:
    matched=[ast.literal_eval(x) for x in cpp_strings if prefix in x]
    assert len(matched)==1, (prefix,matched)
    assert matched[0].endswith(chr(10)), (
        "CPDCAT catalog line must end with a REAL LF, not literal \\\\n",prefix,matched
    )
    assert (chr(92)+"n") not in matched[0], (prefix,matched)
print("PASS: HP CPDCAT sends real ASCII LF in header, entries and footers")
