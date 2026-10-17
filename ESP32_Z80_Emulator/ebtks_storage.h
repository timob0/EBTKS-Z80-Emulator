#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include "ebtks_link.h"
#include "z80_debug.h"

static const size_t EBTKS_READ_CHUNK  = 256;
// The replacement Base64 decoder in the current Teensy firmware reliably
// decodes the first 64 bytes of each request. Split larger writes at this
// boundary; successive SD writes advance the same open file normally.
static const size_t EBTKS_WRITE_CHUNK = 64;
static const char *EBTKS_CPM_ROOT = "/cpm";

inline bool ebtksStorageExchange(JsonDocument &request, JsonDocument &reply, uint32_t timeoutMs = 3000)
{
  String tx;
  serializeJson(request, tx);
  String rx;
  if (!ebtksLinkTransaction(tx, rx, timeoutMs)) return false;

  DeserializationError err = deserializeJson(reply, rx);
  if (err) {
    Serial.print("EBTKS storage JSON error: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

inline bool ebtksStorageOpen(const char *path, bool writable, int &fd)
{
  JsonDocument request;
  JsonObject open = request["open"].to<JsonObject>();
  open["path"] = path;
  // SdFat access-mode flags used by the Teensy are POSIX-style:
  // O_RDONLY=0, O_WRONLY=1, O_RDWR=2 (O_ACCMODE=3).  Sending 3 selects
  // the invalid O_ACCMODE bit pattern and makes SD.open() fail.
  // Read-only opens can use the Teensy-side default. Existing disk images
  // are opened read/write without create/truncate semantics.
  static const int EBTKS_TEENSY_O_RDWR = 2;
  if (writable) open["opts"] = EBTKS_TEENSY_O_RDWR;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) {
    if (writable) z80DebugLog("EBTKS WRITE open transport failure path=%s opts=%d\n",
                              path, EBTKS_TEENSY_O_RDWR);
    return false;
  }
  fd = reply["open"]["fd"] | -1;
  if (writable) {
    z80DebugLog("EBTKS WRITE open path=%s opts=%d -> fd=%d\n",
                path, EBTKS_TEENSY_O_RDWR, fd);
  }
  return fd >= 0;
}

inline void ebtksStorageClose(int fd)
{
  if (fd < 0) return;
  JsonDocument request;
  request["close"]["fd"] = fd;
  JsonDocument reply;
  ebtksStorageExchange(request, reply);
}

inline bool ebtksStorageCloseChecked(int fd)
{
  if (fd < 0) return false;
  JsonDocument request;
  request["close"]["fd"] = fd;
  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) {
    z80DebugLog("EBTKS WRITE close transport failure fd=%d\n", fd);
    return false;
  }
  int returnedFd = reply["close"]["fd"] | -999;
  bool ok = (returnedFd == -1);
  z80DebugLog("EBTKS WRITE close fd=%d -> returnedFd=%d %s\n",
              fd, returnedFd, ok ? "OK" : "FAIL");
  return ok;
}

inline bool ebtksStorageSeek(int fd, uint32_t offset)
{
  JsonDocument request;
  JsonObject seek = request["lseek"].to<JsonObject>();
  seek["fd"] = fd;
  seek["offset"] = offset;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) return false;
  int result = reply["lseek"]["offset"] | 0;
  // The current Teensy service returns FsFile::seek()'s success value rather
  // than the resulting byte offset, so any non-zero value means success.
  return result != 0;
}

inline bool ebtksStorageSeekChecked(int fd, uint32_t offset)
{
  JsonDocument request;
  JsonObject seek = request["lseek"].to<JsonObject>();
  seek["fd"] = fd;
  seek["offset"] = offset;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) {
    z80DebugLog("EBTKS WRITE seek transport failure fd=%d offset=%lu\n",
                fd, (unsigned long)offset);
    return false;
  }
  int result = reply["lseek"]["offset"] | 0;
  bool ok = result != 0;
  z80DebugLog("EBTKS WRITE seek fd=%d offset=%lu -> result=%d %s\n",
              fd, (unsigned long)offset, result, ok ? "OK" : "FAIL");
  return ok;
}

inline int ebtksStorageReadChunk(int fd, uint8_t *dst, size_t wanted)
{
  if (!dst || wanted == 0) return 0;
  if (wanted > EBTKS_READ_CHUNK) wanted = EBTKS_READ_CHUNK;

  JsonDocument request;
  JsonObject read = request["read"].to<JsonObject>();
  read["fd"] = fd;
  read["len"] = wanted;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) return -1;

  int reported = reply["read"]["len"] | -1;
  if (reported <= 0) return reported;

  const char *encoded = reply["read"]["data"] | "";
  size_t decoded = 0;
  int rc = mbedtls_base64_decode(dst, wanted, &decoded,
                                 (const unsigned char *)encoded, strlen(encoded));
  if (rc != 0 || decoded != (size_t)reported) {
    Serial.printf("EBTKS storage Base64 decode error rc=%d reported=%d decoded=%u\n",
                  rc, reported, (unsigned)decoded);
    return -1;
  }
  return reported;
}

inline int ebtksStorageWriteChunk(int fd, const uint8_t *src, size_t len)
{
  if (!src || len == 0) return 0;
  if (len > EBTKS_WRITE_CHUNK) len = EBTKS_WRITE_CHUNK;

  unsigned char encoded[4 * ((EBTKS_WRITE_CHUNK + 2) / 3) + 1];
  size_t encodedLen = 0;
  int rc = mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen, src, len);
  if (rc != 0) {
    z80DebugLog("EBTKS storage Base64 encode error rc=%d input=%u required=%u\n",
                rc, (unsigned)len, (unsigned)encodedLen);
    return -1;
  }
  encoded[encodedLen] = 0;

  JsonDocument request;
  JsonObject write = request["write"].to<JsonObject>();
  write["fd"] = fd;
  write["len"] = len;

  // The current Teensy firmware checks jdoc["write"] but reads the Base64
  // payload from jdoc["Write"]["data"]. Supplying both keys preserves
  // compatibility without requiring a Teensy-side protocol change.
  request["Write"]["data"] = (const char *)encoded;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) {
    z80DebugLog("EBTKS WRITE chunk transport failure fd=%d requested=%u\n",
                fd, (unsigned)len);
    return -1;
  }
  int written = reply["write"]["len"] | -1;
  z80DebugLog("EBTKS WRITE chunk fd=%d requested=%u -> written=%d %s\n",
              fd, (unsigned)len, written,
              written == (int)len ? "OK" : "FAIL");
  return written;
}

inline bool ebtksStorageProbe()
{
  int fd = -1;
  bool ok = ebtksStorageOpen("/cpm/boot.txt", false, fd);
  if (ok) ebtksStorageClose(fd);
  return ok;
}

inline bool ebtksStorageReadRange(const char *path, uint32_t offset, uint8_t *dst, size_t len)
{
  int fd = -1;
  if (!ebtksStorageOpen(path, false, fd)) return false;

  bool ok = (offset == 0) || ebtksStorageSeek(fd, offset);
  size_t done = 0;

  while (ok && done < len) {
    int n = ebtksStorageReadChunk(fd, dst + done, len - done);
    if (n < 0) ok = false;
    else if (n == 0) break;
    else done += (size_t)n;
  }

  ebtksStorageClose(fd);
  return ok && done == len;
}

inline bool ebtksStorageWriteRange(const char *path, uint32_t offset, const uint8_t *src, size_t len)
{
  z80DebugLog("EBTKS WRITE begin path=%s offset=%lu len=%u\n",
              path, (unsigned long)offset, (unsigned)len);

  int fd = -1;
  if (!ebtksStorageOpen(path, true, fd)) {
    z80DebugLog("EBTKS WRITE abort: open failed path=%s\n", path);
    return false;
  }

  bool ok = (offset == 0) || ebtksStorageSeekChecked(fd, offset);
  if (offset == 0) {
    z80DebugLog("EBTKS WRITE seek skipped fd=%d offset=0\n", fd);
  }
  size_t done = 0;

  while (ok && done < len) {
    size_t part = len - done;
    if (part > EBTKS_WRITE_CHUNK) part = EBTKS_WRITE_CHUNK;
    int n = ebtksStorageWriteChunk(fd, src + done, part);
    if (n != (int)part) ok = false;
    else done += part;
  }

  bool closeOk = ebtksStorageCloseChecked(fd);
  ok = ok && closeOk;
  z80DebugLog("EBTKS WRITE end path=%s done=%u/%u result=%s\n",
              path, (unsigned)done, (unsigned)len, ok ? "OK" : "FAIL");
  return ok && done == len;
}

inline bool ebtksStorageReadText(const char *path, char *dst, size_t capacity, size_t &actual)
{
  actual = 0;
  if (!dst || capacity < 2) return false;

  int fd = -1;
  if (!ebtksStorageOpen(path, false, fd)) return false;

  bool ok = true;
  while (actual < capacity - 1) {
    size_t room = capacity - 1 - actual;
    int n = ebtksStorageReadChunk(fd, (uint8_t *)dst + actual, room);
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    actual += (size_t)n;
  }

  dst[actual] = 0;
  ebtksStorageClose(fd);
  return ok;
}

inline bool ebtksStorageReadFileToRam(const char *path, uint16_t address, size_t &actual)
{
  actual = 0;
  int fd = -1;
  if (!ebtksStorageOpen(path, false, fd)) return false;

  bool ok = true;
  while (ok && ((uint32_t)address + actual) < sizeof(RAM)) {
    size_t room = sizeof(RAM) - ((size_t)address + actual);
    size_t wanted = room > EBTKS_READ_CHUNK ? EBTKS_READ_CHUNK : room;
    int n = ebtksStorageReadChunk(fd, &RAM[address + actual], wanted);
    if (n < 0) ok = false;
    else if (n == 0) break;
    else actual += (size_t)n;
  }

  ebtksStorageClose(fd);
  return ok;
}

inline bool ebtksStorageFileSize(const char *path, uint32_t &size)
{
  size = 0;
  int fd = -1;
  if (!ebtksStorageOpen(path, false, fd)) return false;

  uint8_t scratch[EBTKS_READ_CHUNK];
  bool ok = true;
  for (;;) {
    int n = ebtksStorageReadChunk(fd, scratch, sizeof(scratch));
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    size += (uint32_t)n;
  }

  ebtksStorageClose(fd);
  return ok;
}
