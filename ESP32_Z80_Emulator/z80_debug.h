#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Small in-memory diagnostic log for Z80/CP/M services.  It mirrors messages
// to the remapped ESP32 diagnostic UART, but also makes them available to the
// /z80 web UI so no physical serial adapter is required.
static const size_t Z80_DEBUG_CAPACITY = 8192;
static char z80DebugBuffer[Z80_DEBUG_CAPACITY];
static size_t z80DebugLength = 0;
static portMUX_TYPE z80DebugMux = portMUX_INITIALIZER_UNLOCKED;

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
  line[n] = 0;

  // Keep the existing physical diagnostic output as well.
  Serial.print(line);

  portENTER_CRITICAL(&z80DebugMux);
  if (n >= Z80_DEBUG_CAPACITY) {
    memcpy(z80DebugBuffer, line + n - (Z80_DEBUG_CAPACITY - 1), Z80_DEBUG_CAPACITY - 1);
    z80DebugLength = Z80_DEBUG_CAPACITY - 1;
  } else {
    size_t needed = z80DebugLength + n;
    if (needed >= Z80_DEBUG_CAPACITY) {
      size_t drop = needed - (Z80_DEBUG_CAPACITY - 1);
      memmove(z80DebugBuffer, z80DebugBuffer + drop, z80DebugLength - drop);
      z80DebugLength -= drop;
    }
    memcpy(z80DebugBuffer + z80DebugLength, line, n);
    z80DebugLength += n;
  }
  z80DebugBuffer[z80DebugLength] = 0;
  portEXIT_CRITICAL(&z80DebugMux);
}

inline String z80DebugSnapshot()
{
  size_t len;
  portENTER_CRITICAL(&z80DebugMux);
  len = z80DebugLength;
  portEXIT_CRITICAL(&z80DebugMux);

  char *copy = (char *)malloc(len + 1);
  if (!copy) return String("diagnostic log allocation failed\n");

  portENTER_CRITICAL(&z80DebugMux);
  // Length may have changed since the first read; clamp to the allocation.
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
