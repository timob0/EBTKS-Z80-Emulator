#include "globals.h"
#pragma once

void serialTask(void *parameter) {
  Serial.println("Console router task started");
  serial_t = true;
  uint8_t chunk[128];

  // Minimal Telnet input parser.  Telnet option negotiation arrives on the
  // same TCP stream as terminal characters; those IAC command bytes must not
  // be passed to the emulated Z80 UART.
  enum TelnetRxState : uint8_t { TEL_DATA, TEL_IAC, TEL_IAC_OPTION, TEL_SB, TEL_SB_IAC };
  TelnetRxState telState = TEL_DATA;

  for (;;) {
    size_t n = 0;
    while (txOutPtr != txInPtr && n < sizeof(chunk)) {
      chunk[n++] = txBuf[txOutPtr];
      uint16_t next = txOutPtr + 1;
      if (next >= sizeof(txBuf)) next = 0;
      txOutPtr = next;
    }

    if (n) {
      if (serverClient.connected()) {
        // WiFiClient::write() may accept less than the requested length.
        // Do not discard the tail of a CP/M output burst if that happens.
        size_t sent = 0;
        while (sent < n && serverClient.connected()) {
          size_t wrote = serverClient.write(chunk + sent, n - sent);
          if (wrote == 0) {
            vTaskDelay(1);
            continue;
          }
          sent += wrote;
        }
      }
      webTerminalWrite(chunk, n);
    }

    while (serverClient.connected() && serverClient.available()) {
      uint8_t c = (uint8_t)serverClient.read();

      switch (telState) {
        case TEL_DATA:
          if (c == 255) {                    // IAC
            telState = TEL_IAC;
          } else {
            if (c == 127) c = 8;
            consoleRxEnqueue(c);
          }
          break;

        case TEL_IAC:
          if (c == 255) {                    // Escaped literal 0xFF
            consoleRxEnqueue(255);
            telState = TEL_DATA;
          } else if (c == 250) {             // SB - subnegotiation
            telState = TEL_SB;
          } else if (c >= 251 && c <= 254) { // WILL/WONT/DO/DONT + option byte
            telState = TEL_IAC_OPTION;
          } else {
            telState = TEL_DATA;             // Other 2-byte Telnet command
          }
          break;

        case TEL_IAC_OPTION:
          // Consume option byte following WILL/WONT/DO/DONT.
          telState = TEL_DATA;
          break;

        case TEL_SB:
          if (c == 255) telState = TEL_SB_IAC;
          break;

        case TEL_SB_IAC:
          if (c == 240) telState = TEL_DATA; // IAC SE
          else if (c != 255) telState = TEL_SB;
          break;
      }
    }

    if (rxOutPtr != rxInPtr && bitRead(pIn[UART_LSR], 0) == 0) {
      pIn[UART_PORT] = rxBuf[rxOutPtr];
      uint16_t next = rxOutPtr + 1;
      if (next >= sizeof(rxBuf)) next = 0;
      rxOutPtr = next;
      bitWrite(pIn[UART_LSR], 0, 1);
    }
    vTaskDelay(1);
  }
}

void outString(char buf[]) {
  int i = 0;
  while (buf[i] > 0) {
    uint16_t next = txInPtr + 1;
    if (next >= sizeof(txBuf)) next = 0;
    if (next == txOutPtr) break;
    txBuf[txInPtr] = (uint8_t)buf[i++];
    txInPtr = next;
  }
}
