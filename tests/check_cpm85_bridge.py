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
assert pio.count('-DEBTKS_CPM85_KEYBOARD_ENABLE=1')==2
assert '[env:esp32dev]' in pio and '[env:esp32dev-tft]' in pio
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
