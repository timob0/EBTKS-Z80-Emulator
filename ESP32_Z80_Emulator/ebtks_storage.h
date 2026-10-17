#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include "ebtks_link.h"

static const size_t EBTKS_READ_CHUNK  = 256;
static const size_t EBTKS_WRITE_CHUNK = 192;
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
  // SdFat flags used by the Teensy: O_READ=1, O_WRITE=2, O_RDWR=3.
  // Read-only opens can use the Teensy-side default. Existing disk images
  // are opened read/write without create/truncate semantics.
  if (writable) open["opts"] = 3;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) {
    if (writable) Serial.printf("EBTKS WRITE open transport failure path=%s opts=3\n", path);
    return false;
  }
  fd = reply["open"]["fd"] | -1;
  if (writable) {
    Serial.printf("EBTKS WRITE open path=%s opts=3 -> fd=%d\n", path, fd);
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
    Serial.printf("EBTKS WRITE close transport failure fd=%d\n", fd);
    return false;
  }
  int returnedFd = reply["close"]["fd"] | -999;
  bool ok = (returnedFd == -1);
  Serial.printf("EBTKS WRITE close fd=%d -> returnedFd=%d %s\n",
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
    Serial.printf("EBTKS WRITE seek transport failure fd=%d offset=%lu\n",
                  fd, (unsigned long)offset);
    return false;
  }
  int result = reply["lseek"]["offset"] | 0;
  bool ok = result != 0;
  Serial.printf("EBTKS WRITE seek fd=%d offset=%lu -> result=%d %s\n",
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

  // mbedtls_base64_encode() requires room for the trailing NUL in addition
  // to the Base64 payload.  For a full 192-byte chunk the payload is exactly
  // 256 bytes, so passing sizeof(encoded)-1 made every full write chunk fail
  // with MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL before it ever reached Teensy.
  unsigned char encoded[4 * ((EBTKS_WRITE_CHUNK + 2) / 3) + 1];
  size_t encodedLen = 0;
  int rc = mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen, src, len);
  if (rc != 0) {
    Serial.printf("EBTKS storage Base64 encode error rc=%d input=%u required=%u\n",
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
    Serial.printf("EBTKS WRITE chunk transport failure fd=%d requested=%u\n",
                  fd, (unsigned)len);
    return -1;
  }
  int written = reply["write"]["len"] | -1;
  Serial.printf("EBTKS WRITE chunk fd=%d requested=%u -> written=%d %s\n",
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
  Serial.printf("EBTKS WRITE begin path=%s offset=%lu len=%u\n",
                path, (unsigned long)offset, (unsigned)len);

  int fd = -1;
  if (!ebtksStorageOpen(path, true, fd)) {
    Serial.printf("EBTKS WRITE abort: open failed path=%s\n", path);
    return false;
  }

  bool ok = (offset == 0) || ebtksStorageSeekChecked(fd, offset);
  if (offset == 0) {
    Serial.printf("EBTKS WRITE seek skipped fd=%d offset=0\n", fd);
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
  Serial.printf("EBTKS WRITE end path=%s done=%u/%u result=%s\n",
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
