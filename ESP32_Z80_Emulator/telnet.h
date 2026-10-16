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
        serverClient.write(255); serverClient.write(251); serverClient.write(1);
        serverClient.write(255); serverClient.write(251); serverClient.write(3);
        serverClient.write(255); serverClient.write(252); serverClient.write(34);
        while (serverClient.available()) serverClient.read();
        RUN = false;
      } else {
        incoming.stop();
      }
    }
    vTaskDelay(50);
  }
}
