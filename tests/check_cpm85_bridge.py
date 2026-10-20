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
for code in ['hp == 0222', 'hp == 0217',
             'case 0234:', 'case 0235:', 'case 0241:', 'case 0242:',
             'case 0245:', 'case 0243:', 'case 0244:',
             'case 0236:', 'case 0237:', 'case 0200:', 'case 0206:',
             'hp == 0231', 'hp == 0232', 'hp == 0233', 'hp <= 0176']:
    assert code in keymap
assert 'code!=0207' in a
assert 'case 0255:' not in keymap
assert 'Cpm85Input::drain();' in b
assert '#include "ebtks_cpm85_keyboard.h"' in main
assert 'Cpm85Input::start();' in main
assert pio.count('-DEBTKS_CPM85_KEYBOARD_ENABLE=1')==2
assert '[env:esp32dev]' in pio and '[env:esp32dev-tft]' in pio
assert 'TeensySerial.write(' not in a and 'SPI.transfer(' not in a
print('PASS: development-only CP/M keyboard bridge, no unsolicited UART/SPI')
