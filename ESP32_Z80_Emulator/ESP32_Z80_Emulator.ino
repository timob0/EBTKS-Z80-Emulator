#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "globals.h"
#include "ebtks_link.h"
#include "web_terminal.h"
#include "init8250.h"
#include "basic.h"
#include "disk.h"
#include "cpu.h"
#include "ota.h"
#include "serial.h"
#include "telnet.h"

void setup() {
  // EBTKS v3.x factory firmware mapping, confirmed on the installed board:
  // ESP32-D0WD-V3 rev 3.0, 40 MHz crystal, 4 MB flash.
  // UART0 diagnostics are moved to GPIO25/26; GPIO3/1 are reserved for Teensy.
  Serial.begin(115200, SERIAL_8N1, 25, 26);
  ebtksLinkBegin();
  delay(100);

  Serial.println("\nEBTKS Z80 Emulator - Milestone 1");
  Serial.println("Target: ESP32-D0WD-V3 / EBTKS v3.x");
  for(int i=0;i<11;++i)Serial.println(banner[i]);

  xTaskCreatePinnedToCore(OTAtask,"OTAtask",4096,NULL,1,&Task5,0);
  while(!ota_t)vTaskDelay(10);

  webTerminalBegin();

  RUN=false;
  xTaskCreatePinnedToCore(CPUTask,"CPUTask",4096,NULL,4,&Task1,1);
  while(!cpu_t)vTaskDelay(10);

  xTaskCreatePinnedToCore(TelnetTask,"TelnetTask",3072,NULL,2,&Task3,0);
  while(!telnet_t)vTaskDelay(10);

  xTaskCreatePinnedToCore(serialTask,"ConsoleTask",3072,NULL,3,&Task2,0);
  while(!serial_t)vTaskDelay(10);

  Serial.println("Milestone 1 services started");
  Serial.print("Browser terminal: http://");Serial.println(WiFi.localIP());
}

void loop() {
  // With no ESP-attached SD card, bootstrap() falls back to loadBasic().
  bootstrap();
  PC=0;
  SingleStep=false;
  bpOn=false;
  RUN=true;
  Serial.println("Starting Z80");
  while(RUN)vTaskDelay(100);
  Serial.printf("Z80 halted @ %04X - restarting virtual machine\n",PC?PC-1:0);
  vTaskDelay(500);
}
