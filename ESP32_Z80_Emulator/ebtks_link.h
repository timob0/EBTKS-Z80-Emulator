#pragma once
#include <HardwareSerial.h>

// EBTKS v3.x factory wiring: UART1 RX=GPIO3, TX=GPIO1.
// Milestone 1 reserves the link but sends no CP/M console data over it.
HardwareSerial TeensySerial(1);

inline void ebtksLinkBegin() {
  TeensySerial.setRxBufferSize(1024);
  TeensySerial.begin(115200, SERIAL_8N1, 3, 1);
}
