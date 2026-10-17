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
#include "z80_debug.h"

// The EBTKS serial connection is protocol-only.  Legacy emulator sources have
// many Serial.print/printf diagnostics; redirect every one of them to the
// in-memory /z80debug sink rather than any ESP32 hardware UART.  The actual
// Teensy link is the explicit TeensySerial object in ebtks_link.h.
#define Serial EbtksDebugSerial

#include "ebtks_link.h"
#include "web_terminal_v2.h"
#include "ebtks_web.h"
#include "ebtks_storage.h"
#include "init8250.h"
#include "basic.h"
#include "disk.h"
#include "cpm_mount_store.h"
#include "cpm_geometry_dynamic.h"
#include "cpm_warmboot.h"
#include "cpu.h"
#include "serial.h"
#include "telnet.h"

void setup() {
  // Do not initialise or write a diagnostic hardware UART.  GPIO3/1 are used
  // exclusively by TeensySerial for the framed ESP32 <-> Teensy protocol.
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

  // Milestone 2: verify that the Teensy SD card contains /cpm/boot.txt. All
  // actual file and disk traffic is serviced over TeensySerial by the Teensy.
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
  // With Teensy storage available, /cpm/boot.txt selects CP/M or another
  // remote boot image. If storage is absent, bootstrap() uses built-in BASIC.
  bootstrap();

  // Make the Media Manager's verified persistent table authoritative and then
  // patch every mounted HP 82900 floppy using dynamic CBIOS discovery.
  if (sdfound) {
    cpmStoreActivateMounts();
    cpmApplyMountedGeometryDynamic();
  }

  // Capture a pristine native CCP+BDOS image before starting the Z80.  Large
  // CP/M applications may legally reclaim the CCP and expect BIOS WBOOT to
  // reload it.  Arm an internal breakpoint at E603 so we can restore D000-E5FF
  // before the historical CBIOS warm-boot code executes.
  cpmWarmBootCapture();

  PC=0;
  SingleStep=false;
  cpmWarmBootArmTrap();
  RUN=true;
  Serial.println("Starting Z80");

  // A stop at the reserved WBOOT breakpoint is not a virtual-machine halt.
  // Restore the resident CP/M image and resume exactly at WBOOT.  HALT, the
  // web Reset Z80 action, and any other stop still fall through to the normal
  // full virtual-machine restart below.
  while (true) {
    while(RUN) vTaskDelay(100);

    if (cpmWarmBootHandleTrap()) {
      RUN = true;
      continue;
    }
    break;
  }

  bpOn=false;
  Serial.printf("Z80 halted @ %04X - restarting virtual machine\n",PC?PC-1:0);
  vTaskDelay(500);
}
