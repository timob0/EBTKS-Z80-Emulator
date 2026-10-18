// Standalone host regression for the platform-neutral SPI frame codec.
// Does not require Arduino/PlatformIO or access to EBTKS hardware.
// g++ -std=c++11 -Wall -Wextra -pedantic tests/spi_stream_frame_selftest.cpp -o /tmp/ebtks_spi_frame_test && /tmp/ebtks_spi_frame_test
#include "../ESP32_Z80_Emulator/spi_stream_frame.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
  using namespace EbtksStream;
  static_assert(FRAME_BYTES == 528, "Expected 16+512 aligned DMA frame");
  static_assert((FRAME_BYTES % 4) == 0, "DMA transaction must be aligned");
  const uint8_t known[] = {'1','2','3','4','5','6','7','8','9'};
  assert(crc16(known, sizeof(known)) == 0x4B37); // CRC-16/Modbus

  alignas(4) uint8_t frame[FRAME_BYTES] = {};
  for (size_t i = 0; i < PAYLOAD_BYTES; ++i)
    frame[HEADER_BYTES+i] = (uint8_t)(i * 37U + (i >> 3U));
  stamp(frame, KIND_SD_READ, 0xA135, 17, 0);
  assert(check(frame, KIND_SD_READ, 0xA135, 17) == Check::OK);
  assert(check(frame, KIND_SD_READ, 0xA134, 17) == Check::BAD_SESSION);
  assert(check(frame, KIND_SD_READ, 0xA135, 18) == Check::BAD_BLOCK);
  assert(check(frame, KIND_CRT, 0xA135, 17) == Check::BAD_KIND);

  frame[17] ^= 0x80;
  assert(check(frame, KIND_SD_READ, 0xA135, 17) == Check::BAD_PAYLOAD_CRC);
  frame[17] ^= 0x80;
  frame[14] ^= 0x01;
  assert(check(frame, KIND_SD_READ, 0xA135, 17) == Check::BAD_HEADER_CRC);
  frame[14] ^= 0x01;

  stamp(frame, KIND_SD_READ, 0xA135, 17, FLAG_SOURCE_ERROR);
  assert(check(frame, KIND_SD_READ, 0xA135, 17) == Check::SOURCE_ERROR);
  stamp(frame, KIND_CRT, 0xA135, 17, 0);
  assert(check(frame, KIND_CRT, 0xA135, 17) == Check::OK);
  std::puts("SPI stream frame self-test PASS");
}
