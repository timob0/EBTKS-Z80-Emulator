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
#include "ebtks_tft_test.h" // normal build: inert; ST7796 VSPI panel config
#include "ebtks_tft_terminal.h" // optional ANSI/VT100 TFT mirror (headless no-op)

// The EBTKS serial connection is protocol-only.  Legacy emulator sources have
// many Serial.print/printf diagnostics; redirect every one of them to the
// in-memory /z80debug sink rather than any ESP32 hardware UART.  The actual
// Teensy link is the explicit TeensySerial object in ebtks_link.h.
#define Serial EbtksDebugSerial

#include "ebtks_link.h"
#include "spi_link_test.h"
#include "spi_disk_read.h" // default-on (build-configurable) 4 MHz CP/M reads; all writes remain UART
#include "web_terminal_v2.h"
#include "ebtks_web.h"
#include "ebtks_storage.h"
#include "ebtks_cpm85_keyboard.h" // before disk.h transitively includes serial.h
#include "ebtks_modem.h" // CPU I/O handlers + core-0 Wi-Fi modem worker
#include "init8250.h"
#include "basic.h"
#include "disk.h"
#include "cpm_mount_store.h"
#include "cpm_geometry_dynamic.h"
#include "cpm_warmboot.h"
// Keep last user-tested decoder available for A/B regression builds.
#ifdef EBTKS_Z80_LEGACY_CPU
#include "cpu.h"
#else
#include "cpu_redcode.h"
#endif
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

  // HP-85 keyboard router: low-priority control-link poll,
  // no extra UART, no SPI bus overlap, no Z80 reset or CP/M bootstrap.
  Cpm85Input::start();
  // Independent Hayes/Telnet Wi-Fi modem on 88h-8Fh, not console 80h.
  CpmWifiModem::start();

  // Start the CRC-checked 16-sector SPI cache after Wi-Fi/web and worker
  // initialization but BEFORE CP/M bootstrap/read traffic. If Teensy storage
  // is unavailable or the heap guard refuses the 8 KiB cache, stay on UART.
  // Do not initialize DMA or create the core-0 SPI worker until the first
  // actual read; the existing SPI implementation owns error handling.
#if EBTKS_SPI_READAHEAD_DEFAULT_ON
  if (sdfound) {
    const bool enabled = spiDiskSetEnabled(true);
    Serial.printf("CP/M SPI read-ahead boot default: %s\n",
                  enabled ? "enabled (4 MHz, 16-sector cache)" : "UART fallback");
    if (!enabled && spiDiskLastError[0])
      Serial.printf("CP/M SPI read-ahead unavailable: %s\n", spiDiskLastError);
  } else {
    Serial.println("CP/M SPI read-ahead: no Teensy storage; staying on UART");
  }
#else
  Serial.println("CP/M disk reads: UART startup default (compile-time override)");
#endif

#if EBTKS_TFT_TERMINAL_MIRROR
  // Create a low-priority TFT worker, never do SPI drawing inside the UART
  // console task. 80x24 character storage: 1,920 bytes, no pixel framebuffer.
  Serial.println("TFT ST7796 terminal mirror starting");
  EbtksTft::startTerminalMirror();
#elif EBTKS_TFT_ENABLE
  // Preserve the already-tested single-draw color-bar bring-up image.
  Serial.println("TFT ST7796 one-shot boot pattern starting");
  EbtksTft::drawBootPattern();
#endif

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

  // Capture a pristine native CCP+BDOS image before starting the Z80. Large
  // CP/M applications may legally reclaim this area and expect BIOS WBOOT to
  // reload it. The helper keeps page zero standard (0005 -> D806) and patches
  // only the BDOS entry itself to recognize function 0 safely.
  cpmWarmBootCapture();

#ifdef EBTKS_Z80_LEGACY_CPU
  PC=0;
#else
  ebtksCpuColdReset(); // reset CPU registers after each outer-loop cold boot
#endif
  SingleStep=false;
  cpmWarmBootArmTrap();
  RUN=true;
  Serial.println("Starting Z80");

  // A stop at the reserved WBOOT breakpoint is not a virtual-machine halt.
  // Restore the resident CP/M image and resume exactly at WBOOT. HALT, the
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
