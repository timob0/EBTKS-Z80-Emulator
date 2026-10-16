#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <Update.h>
#include <ArduinoJson.h>

#include "globals.h"
#include "ebtks_link.h"
#include "web_terminal.h"
#include "ebtks_web.h"
#include "ebtks_storage.h"
#include "init8250.h"
#include "basic.h"
#include "disk.h"
#include "cpu.h"
#include "serial.h"
#include "telnet.h"

void setup() {
  // EBTKS v3.x factory firmware mapping, confirmed on the installed board:
  // ESP32-D0WD-V3 rev 3.0, 40 MHz crystal, 4 MB flash.
  // UART0 diagnostics are moved to GPIO25/26; GPIO3/1 are reserved for Teensy.
  Serial.begin(115200, SERIAL_8N1, 25, 26);
  ebtksLinkBegin();
  delay(100);

  Serial.println("\nEBTKS Z80 Emulator - Milestone 2");
  Serial.println("Target: ESP32-D0WD-V3 / EBTKS v3.x");
  for(int i=0;i<11;++i) Serial.println(banner[i]);

  // Preserve the existing EBTKS Wi-Fi provisioning, mDNS, /ws Teensy bridge,
  // SPIFFS web assets, and browser firmware updater.  The Z80 terminal is
  // added at /z80 with its own websocket /z80ws.
  if (!ebtksWebBegin()) {
    Serial.println("EBTKS web services failed to start; rebooting in 3 seconds");
    delay(3000);
    ESP.restart();
  }

  // Milestone 2: verify that the Teensy SD card contains /boot.txt.  The
  // upstream CPU task still performs its virtual disk-controller init, but
  // all actual file traffic is now serviced over UART1 by the Teensy.
  Serial.print("Probing Teensy-backed Z80 storage... ");
  sdfound = ebtksStorageProbe();
  Serial.println(sdfound ? "ready" : "not available (BASIC fallback)");

  RUN=false;
  xTaskCreatePinnedToCore(CPUTask,"CPUTask",4096,NULL,4,&Task1,1);
  while(!cpu_t) vTaskDelay(10);

  xTaskCreatePinnedToCore(TelnetTask,"TelnetTask",3072,NULL,2,&Task3,0);
  while(!telnet_t) vTaskDelay(10);

  xTaskCreatePinnedToCore(serialTask,"ConsoleTask",3072,NULL,3,&Task2,0);
  while(!serial_t) vTaskDelay(10);

  Serial.println("Milestone 2 services started");
  Serial.print("EBTKS home: http://"); Serial.println(WiFi.localIP());
  Serial.print("Z80 terminal: http://"); Serial.print(WiFi.localIP()); Serial.println("/z80");
}

void loop() {
  // With Teensy storage available, /boot.txt selects CP/M or another remote
  // boot image.  If storage is absent, bootstrap() uses the built-in BASIC.
  bootstrap();
  PC=0;
  SingleStep=false;
  bpOn=false;
  RUN=true;
  Serial.println("Starting Z80");
  while(RUN) vTaskDelay(100);
  Serial.printf("Z80 halted @ %04X - restarting virtual machine\n",PC?PC-1:0);
  vTaskDelay(500);
}
