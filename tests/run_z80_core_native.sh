#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -c pio_src/z80_redcode.c -o "$BUILD_DIR/z80_redcode.o"
g++ -std=c++17 -O2 -Wall -Wextra tests/z80_core_native.cpp "$BUILD_DIR/z80_redcode.o" -o "$BUILD_DIR/z80_test"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  tests/cpm_safe_point_native.cpp "$BUILD_DIR/z80_redcode.o" \
  -o "$BUILD_DIR/cpm_safe_point_test"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  tests/cpm_directory_native.cpp "$BUILD_DIR/z80_redcode.o" \
  -o "$BUILD_DIR/cpm_directory_test"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  tests/cpm_file_read_native.cpp "$BUILD_DIR/z80_redcode.o" \
  -o g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \\
  tests/cpm_file_write_native.cpp "$BUILD_DIR/z80_redcode.o" \\
  -o "$BUILD_DIR/cpm_file_write_test"
"$BUILD_DIR/cpm_file_read_test"
"$BUILD_DIR/cpm_file_write_test"
"$BUILD_DIR/cpm_safe_point_test"
"$BUILD_DIR/cpm_directory_test"
"$BUILD_DIR/cpm_file_read_test"
"$BUILD_DIR/z80_test"
