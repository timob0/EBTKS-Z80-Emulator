#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// EBTKS v3.x factory wiring: UART1 RX=GPIO3, TX=GPIO1.
// All Teensy traffic is serialized through ebtksLinkTransaction().
HardwareSerial TeensySerial(1);
SemaphoreHandle_t ebtksLinkMutex = nullptr;

inline uint16_t ebtksCrc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xffffU;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i) {
      if (crc & 1U) crc = (crc >> 1) ^ 0xA001U;
      else crc >>= 1;
    }
  }
  return crc;
}

inline bool ebtksDecodeReplyEnvelope(String line, String &jsonReply)
{
  line.trim();
  int comma = line.lastIndexOf(',');
  if (comma <= 0) return false;

  String json = line.substring(0, comma);
  String crcText = line.substring(comma + 1);
  crcText.trim();
  if (!crcText.length()) return false;

  uint16_t received = (uint16_t)strtoul(crcText.c_str(), nullptr, 10);
  uint16_t calculated = ebtksCrc16((const uint8_t *)json.c_str(), json.length());
  if (received != calculated) {
    Serial.printf("EBTKS UART CRC mismatch: received=%u calculated=%u\n", received, calculated);
    return false;
  }

  jsonReply = json;
  return true;
}

inline void ebtksLinkBegin()
{
  TeensySerial.setRxBufferSize(2048);
  TeensySerial.begin(115200, SERIAL_8N1, 3, 1);
  if (!ebtksLinkMutex) ebtksLinkMutex = xSemaphoreCreateMutex();
}

inline bool ebtksLinkTransaction(const String &request, String &jsonReply, uint32_t timeoutMs = 3000)
{
  if (!ebtksLinkMutex) return false;
  if (xSemaphoreTake(ebtksLinkMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;

  // A complete request/reply pair owns UART1.  Discard any stale bytes left
  // from a previous aborted transaction before sending the next request.
  while (TeensySerial.available()) TeensySerial.read();

  String req = request;
  req.trim();
  TeensySerial.print(req);
  TeensySerial.write('\r');

  String line;
  line.reserve(1200);
  bool complete = false;
  uint32_t started = millis();

  while ((millis() - started) < timeoutMs) {
    while (TeensySerial.available()) {
      char c = (char)TeensySerial.read();
      if (c == '\n') {
        complete = true;
        break;
      }
      if (line.length() < 1199) line += c;
      else {
        Serial.println("EBTKS UART reply overflow");
        line = "";
        complete = false;
        break;
      }
    }
    if (complete) break;
    vTaskDelay(1);
  }

  bool ok = complete && ebtksDecodeReplyEnvelope(line, jsonReply);
  if (!ok && !complete) Serial.println("EBTKS UART transaction timeout");

  xSemaphoreGive(ebtksLinkMutex);
  return ok;
}
