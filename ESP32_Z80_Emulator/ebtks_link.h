#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// EBTKS v3.x factory wiring: UART1 RX=GPIO3, TX=GPIO1.
// All Teensy traffic is serialized through ebtksLinkTransaction().
HardwareSerial TeensySerial(1);
SemaphoreHandle_t ebtksLinkMutex = nullptr;

static const size_t EBTKS_LINK_MAX_REPLY = 8192;

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
  TeensySerial.setRxBufferSize(8192);
  TeensySerial.begin(115200, SERIAL_8N1, 3, 1);
  if (!ebtksLinkMutex) ebtksLinkMutex = xSemaphoreCreateMutex();
}

inline bool ebtksLinkTransaction(const String &request, String &jsonReply, uint32_t timeoutMs = 3000)
{
  if (!ebtksLinkMutex) return false;
  if (xSemaphoreTake(ebtksLinkMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;

  while (TeensySerial.available()) TeensySerial.read();

  String req = request;
  req.trim();
  TeensySerial.print(req);
  TeensySerial.write('\r');

  String line;
  line.reserve(1024);
  bool complete = false;
  bool overflow = false;
  uint32_t started = millis();

  while ((millis() - started) < timeoutMs) {
    while (TeensySerial.available()) {
      char c = (char)TeensySerial.read();
      if (c == '\n') {
        complete = true;
        break;
      }
      if (line.length() < EBTKS_LINK_MAX_REPLY - 1) line += c;
      else {
        overflow = true;
        break;
      }
    }
    if (complete || overflow) break;
    vTaskDelay(1);
  }

  bool ok = !overflow && complete && ebtksDecodeReplyEnvelope(line, jsonReply);
  if (overflow) Serial.println("EBTKS UART reply overflow");
  else if (!complete) Serial.println("EBTKS UART transaction timeout");

  xSemaphoreGive(ebtksLinkMutex);
  return ok;
}
