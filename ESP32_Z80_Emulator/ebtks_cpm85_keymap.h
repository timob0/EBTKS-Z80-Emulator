#pragma once
#include <stdint.h>

// HP-85A/B KEYHIT -> one CP/M console input byte. Pure, allocation-free
// mapping so it can be regression-tested without Arduino/FreeRTOS.
// HP-85A/B special codes are OCTAL (EBTKS keyboard reference):
//   0231/0233 = BACK SPACE / FAST BACK SPACE; 0232 = END LINE;
//   0234/0235 = LEFT/RIGHT; 0241/0242 = UP/DOWN.
// The four cursor keys use WordStar/Turbo Pascal/Multiplan CP/M editing
// controls: left=^S, right=^D, up=^E, down=^X. A deliberate single-byte
// application-input mapping, NOT ANSI/VT100 terminal OUTPUT commands.
// Ordinary CTRL+key combinations already arrive as ASCII 000..037 octal.
// Other HP command/function keys are not injected into CP/M.
// K8 (0207) is always consumed locally by the CPM85 LOADBIN.
namespace Cpm85Keymap {
constexpr int toConsoleByte(uint8_t hp) {
  return (hp == 0231 || hp == 0233 || hp == 0177) ? 0x08
       : (hp == 0232) ? 0x0D
       : (hp == 0234) ? 0x13  // LEFT  -> ^S, Multiplan / Turbo Pascal
       : (hp == 0235) ? 0x04  // RIGHT -> ^D
       : (hp == 0241) ? 0x05  // UP    -> ^E
       : (hp == 0242) ? 0x18  // DOWN  -> ^X
       : (hp <= 0176) ? hp  // Includes NUL and all CTRL codes 0..31.
       : -1;                // Unmapped HP-specific keys stay suppressed.
}
} // namespace Cpm85Keymap
