#pragma once
#include <stdint.h>

// HP-85A/B KEYHIT -> one CP/M console input byte. Pure, allocation-free
// mapping so it can be regression-tested without Arduino/FreeRTOS.
// HP-85 special codes from the EBTKS / HP-85 keycode reference are OCTAL:
//   0231 = BACK SPACE, 0232 = END LINE, 0233 = FAST BACK SPACE.
// Ordinary CTRL+key combinations already arrive as ASCII 000..037 octal.
// Unknown HP-specific command/function/navigation keys are not injected into
// CP/M. K8 (0207) is always consumed locally by the CPM85 LOADBIN.
namespace Cpm85Keymap {
constexpr int toConsoleByte(uint8_t hp) {
  return (hp == 0231 || hp == 0233 || hp == 0177) ? 0x08
       : (hp == 0232) ? 0x0D
       : (hp <= 0176) ? hp  // Includes NUL and *all* CTRL codes 0..31.
       : -1;                // No guessed escape sequences for special keys.
}
} // namespace Cpm85Keymap
