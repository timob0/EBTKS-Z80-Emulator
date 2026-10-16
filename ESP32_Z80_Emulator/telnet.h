#include "globals.h"
#pragma once

void TelnetTask(void *parameter) {
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Telnet task started");
  Serial.print("Use 'telnet ");
  Serial.print(WiFi.localIP());
  Serial.println(" 23' to connect");
  telnet_t = true;

  for (;;) {
    if (telnetServer.hasClient()) {
      WiFiClient incoming = telnetServer.available();
      if (!serverClient || !serverClient.connected()) {
        if (serverClient) serverClient.stop();
        serverClient = incoming;
        Serial.print("New Telnet client @ ");
        Serial.println(serverClient.remoteIP());

        // Ask the Telnet client to suppress local echo and go-ahead handling.
        // serialTask() strips all Telnet negotiation bytes before they can
        // reach the emulated Z80 UART.
        serverClient.write(255); serverClient.write(251); serverClient.write(1);  // IAC WILL ECHO
        serverClient.write(255); serverClient.write(251); serverClient.write(3);  // IAC WILL SUPPRESS-GA
        serverClient.write(255); serverClient.write(252); serverClient.write(34); // IAC WONT LINEMODE

        // A terminal attaching to the running machine must not reset CP/M.
        // The WebSocket and Telnet terminals are simply two front ends to the
        // same emulated UART.
      } else {
        incoming.stop();
      }
    }
    vTaskDelay(50);
  }
}
