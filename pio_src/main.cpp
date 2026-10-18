// PlatformIO wrapper for the original Arduino sketch.
//
// Keeping the implementation in ESP32_Z80_Emulator/ means the project can
// still be opened directly in Arduino IDE, while PlatformIO builds the same
// source through this translation unit.

// PlatformIO Library Dependency Finder scans pio_src directly, but the
// original .ino lives outside src_dir. Give LDF the Arduino framework
// libraries explicitly instead of relying on transitive discovery (which
// failed on fresh GitHub runners with "SPI.h: No such file or directory").
// The Arduino IDE sketch remains unchanged.
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>

#include "../ESP32_Z80_Emulator/ESP32_Z80_Emulator.ino"
