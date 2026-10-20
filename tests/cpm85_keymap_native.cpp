// Regression tests for raw HP-85A/B KEYHIT -> CP/M keyboard translation.
// Build: g++ -std=c++11 -Wall -Wextra -Werror -pedantic
//        tests/cpm85_keymap_native.cpp -o /tmp/cpm85_keymap_native
#include "../ESP32_Z80_Emulator/ebtks_cpm85_keymap.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
  using Cpm85Keymap::toConsoleByte;
  // Every ASCII control sequence must be forwarded without mangling: e.g.
  // CTRL+C=ETX, CTRL+S=DC3, CTRL+Q=DC1, CTRL+Z=SUB, CTRL+[=ESC.
  for (int ch = 0; ch < 32; ++ch) assert(toConsoleByte(ch) == ch);
  for (int ch = 32; ch <= 126; ++ch) assert(toConsoleByte(ch) == ch);
  assert(toConsoleByte(0) == 0);     // CTRL+@ (NUL) is not "no event".
  assert(toConsoleByte(3) == 3);     // CTRL+C interrupts CP/M programs.
  assert(toConsoleByte(8) == 8);     // CTRL+H remains backspace.
  assert(toConsoleByte(13) == 13);   // CTRL+M remains CR.
  assert(toConsoleByte(17) == 17);   // CTRL+Q.
  assert(toConsoleByte(19) == 19);   // CTRL+S.
  assert(toConsoleByte(26) == 26);   // CTRL+Z.
  assert(toConsoleByte(27) == 27);   // CTRL+[.
  assert(toConsoleByte(31) == 31);   // CTRL+_.
  assert(toConsoleByte(0177) == 8); // ASCII DEL matches existing Telnet path.
  assert(toConsoleByte(0231) == 8); // Physical BACK SPACE.
  assert(toConsoleByte(0233) == 8); // Physical FAST BACK SPACE.
  assert(toConsoleByte(0232) == 13);// Physical END LINE.
  assert(toConsoleByte(0207) == -1);// K8 must never reach CP/M.
  for (int ch = 0200; ch < 0400; ++ch)
    if (ch != 0231 && ch != 0232 && ch != 0233)
      assert(toConsoleByte(ch) == -1);
  // Test mixed physical typing with controls/edits and no phantom key bytes.
  const uint8_t keys[] = {'D','I','X',0231,'R',0232,3,19,17,26,0207,0233};
  const uint8_t expected[] = {'D','I','X','\b','R','\r',3,19,17,26,'\b'};
  std::vector<uint8_t> delivered;
  for (uint8_t hp : keys) {
    const int mapped = toConsoleByte(hp);
    if (mapped >= 0) delivered.push_back(static_cast<uint8_t>(mapped));
  }
  assert(delivered == std::vector<uint8_t>(expected, expected + sizeof(expected)));
  std::puts("PASS: CPM85 physical BACKSPACE/FAST BACKSPACE, END LINE, CTRL 0..31, K8 isolation");
}
