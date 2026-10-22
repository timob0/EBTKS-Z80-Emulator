// Allocation-free ANSI DA/DSR responder used by TFT + Web CP/M console.
#include "../ESP32_Z80_Emulator/console_ansi_probe.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

static std::string take(EbtksAnsiProbe::Terminal &terminal, const std::string &input) {
  std::string response;
  auto emit = [&](const char *bytes, size_t n) {
    response.append(bytes, n);
  };
  terminal.feed(reinterpret_cast<const uint8_t *>(input.data()), input.size(), emit);
  return response;
}
static void expect(EbtksAnsiProbe::Terminal &t, const std::string &in,
                   const std::string &expected) {
  const auto got = take(t, in);
  if (got != expected) {
    std::cerr << "ANSI reply mismatch: got " << got.size() << " bytes, expected "
              << expected.size() << " bytes\n";
    assert(false);
  }
}

int main() {
  using EbtksAnsiProbe::Terminal;
  static_assert(sizeof(Terminal) < 96, "probe must fit in console task stack");

  Terminal t;
  const std::string esc(1, '\x1b');
  // DA1, legacy DECID, and DSR 5n/6n. Exact Web terminal reply compatibility.
  expect(t, esc + "[c", esc + "[?1;0c");
  expect(t, esc + "[0c", esc + "[?1;0c");
  expect(t, esc + "Z", esc + "[?1;0c");
  expect(t, esc + "[5n", esc + "[0n");
  expect(t, esc + "[6n", esc + "[1;1R");

  // Query split across UART chunks; no premature response.
  expect(t, esc + "[", "");
  expect(t, "6", "");
  expect(t, "n", esc + "[1;1R");

  // Cursor tracking includes ordinary text, CR/LF and ANSI positioning.
  expect(t, "Hello", "");
  expect(t, esc + "[6n", esc + "[1;6R");
  expect(t, "\r\n", "");
  expect(t, esc + "[6n", esc + "[2;1R");
  expect(t, esc + "[12;40H" + esc + "[6n", esc + "[12;40R");
  expect(t, esc + "[2D" + esc + "[6n", esc + "[12;38R");
  expect(t, esc + "[31;44m" + esc + "[6n", esc + "[12;38R");

  // DEC save/restore and ordinary scrolling region.
  expect(t, esc + "7" + esc + "[23;79H", "");
  expect(t, esc + "8" + esc + "[6n", esc + "[12;38R");
  expect(t, esc + "[2;20r" + esc + "[?6h" + esc + "[6n", esc + "[2;1R");
  expect(t, esc + "[?6l" + esc + "[r", "");

  // Last-column response clamps to 80, then next character wraps.
  t.reset();
  expect(t, std::string(80, 'X') + esc + "[6n", esc + "[1;80R");
  expect(t, "Z" + esc + "[6n", esc + "[2;2R");

  // A BBS detection query must never be mistaken for color/control traffic.
  t.reset();
  expect(t, esc + "[38;5;196m" + esc + "[48;2;255;0;0m", "");
  expect(t, esc + "[?25l" + esc + "[?1h" + esc + "[6n", esc + "[1;1R");
  expect(t, esc + "[?6n" + esc + "[>c", ""); // unsupported variants

  // HP/Soroc legacy screen address and escape parsing stay nonresponsive.
  t.reset();
  expect(t, esc + "=" + std::string(1, 32+3) + std::string(1, 32+12) +
                esc + "[6n", esc + "[4;13R");
  expect(t, esc + "(0" + esc + ")B", "");

  // Correctly process mixed queries in one chunk without losing sequencing.
  t.reset();
  expect(t, esc + "[c" + esc + "[5n" + esc + "[6n",
         esc + "[?1;0c" + esc + "[0n" + esc + "[1;1R");
  std::cout << "PASS: centralized TFT/Web ANSI DA, DSR, DECID and cursor tracking\n";
}
