#include "globals.h"
#pragma once

void serialTask(void *parameter) {
  Serial.println("Console router task started");
  serial_t = true;
  uint8_t chunk[128];

  for (;;) {
    size_t n = 0;
    while (txOutPtr != txInPtr && n < sizeof(chunk)) {
      chunk[n++] = txBuf[txOutPtr];
      uint16_t next = txOutPtr + 1;
      if (next >= sizeof(txBuf)) next = 0;
      txOutPtr = next;
    }
    if (n) {
      if (serverClient.connected()) serverClient.write(chunk, n);
      webTerminalWrite(chunk, n);
    }

    while (serverClient.connected() && serverClient.available()) {
      uint8_t c = serverClient.read();
      if (c == 127) c = 8;
      consoleRxEnqueue(c);
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
