#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Small in-memory diagnostic log for Z80/CP/M services. Diagnostics must never
// be written to an ESP32 hardware UART: the EBTKS inter-processor serial link is
// protocol-only. The rolling buffer is exposed through /z80debug and the web UI.
//
// Keep this deliberately small: ESP32 internal DRAM is already tight in this
// firmware (Z80 RAM, network stacks, web server, etc.).
static const size_t Z80_DEBUG_CAPACITY = 2048;
static char z80DebugBuffer[Z80_DEBUG_CAPACITY];
static size_t z80DebugLength = 0;
static portMUX_TYPE z80DebugMux = portMUX_INITIALIZER_UNLOCKED;

inline void z80DebugAppend(const uint8_t *data, size_t n)
{
  if (!data || !n) return;

  portENTER_CRITICAL(&z80DebugMux);
  if (n >= Z80_DEBUG_CAPACITY) {
    const size_t keep = Z80_DEBUG_CAPACITY - 1;
    memcpy(z80DebugBuffer, data + n - keep, keep);
    z80DebugLength = keep;
  } else {
    size_t needed = z80DebugLength + n;
    if (needed >= Z80_DEBUG_CAPACITY) {
      size_t drop = needed - (Z80_DEBUG_CAPACITY - 1);
      if (drop > z80DebugLength) drop = z80DebugLength;
      memmove(z80DebugBuffer, z80DebugBuffer + drop, z80DebugLength - drop);
      z80DebugLength -= drop;
    }
    memcpy(z80DebugBuffer + z80DebugLength, data, n);
    z80DebugLength += n;
  }
  z80DebugBuffer[z80DebugLength] = 0;
  portEXIT_CRITICAL(&z80DebugMux);
}

inline void z80DebugLog(const char *fmt, ...)
{
  char line[320];
  va_list ap;
  va_start(ap, fmt);
  int count = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  if (count < 0) return;
  size_t n = (size_t)count;
  if (n >= sizeof(line)) n = sizeof(line) - 1;
  z80DebugAppend(reinterpret_cast<const uint8_t *>(line), n);
}

// Compatibility sink for legacy Serial.print/printf calls in the emulator
// sources. The main sketch aliases Serial to this object after all framework
// headers are included. It intentionally implements no hardware UART.
class EBTKSDebugPrint : public Print
{
public:
  using Print::write;

  size_t write(uint8_t c) override
  {
    z80DebugAppend(&c, 1);
    return 1;
  }

  size_t write(const uint8_t *buffer, size_t size) override
  {
    z80DebugAppend(buffer, size);
    return size;
  }

  void flush() {}
  operator bool() const { return true; }
};

static EBTKSDebugPrint EbtksDebugSerial;

inline String z80DebugSnapshot()
{
  size_t len;
  portENTER_CRITICAL(&z80DebugMux);
  len = z80DebugLength;
  portEXIT_CRITICAL(&z80DebugMux);

  char *copy = (char *)malloc(len + 1);
  if (!copy) return String("diagnostic log allocation failed\n");

  portENTER_CRITICAL(&z80DebugMux);
  size_t actual = z80DebugLength;
  if (actual > len) actual = len;
  memcpy(copy, z80DebugBuffer, actual);
  copy[actual] = 0;
  portEXIT_CRITICAL(&z80DebugMux);

  String out(copy);
  free(copy);
  return out;
}

inline void z80DebugClear()
{
  portENTER_CRITICAL(&z80DebugMux);
  z80DebugLength = 0;
  z80DebugBuffer[0] = 0;
  portEXIT_CRITICAL(&z80DebugMux);
}
