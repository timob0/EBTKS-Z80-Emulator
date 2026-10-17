#include "serial.h"
#include <stdio.h>
#pragma once
#include "globals.h"
#include "ebtks_storage.h"

void loadBasic(void);
void bootstrap(void);
bool diskRead(void);
bool diskWrite(void);
void SDfileOpen(void);
bool SDfileRead(void);
void SDprintDir(void);
bool SDsetPath(void);
void FileToRAM(char[], uint16_t);

//*********************************************************************************************
//****                       Load bootstrap binaries from flash                            ****
//*********************************************************************************************
void loadBasic(void) {
  Serial.println("\n\rBooting built-in Z80 BASIC fallback:");
  Serial.printf("Loading: BIOS  at 0x%04x ", 0);
  for (int i = 0; i < (int)sizeof(init8250); i++) RAM[i] = init8250[i];
  Serial.printf("%5d bytes... Done\n\r", sizeof(init8250));
  Serial.printf("Loading: Basic at 0x%04x ", 0x150);
  for (int i = 0; i < (int)sizeof(basic); i++) RAM[i + 0x150] = basic[i];
  Serial.printf("%5d bytes... Done\n\r", sizeof(basic));
}

//*********************************************************************************************
//****         Load /cpm/boot.txt and bootstrap binaries from the Teensy SD card           ****
//*********************************************************************************************
void bootstrap(void) {
  if (!sdfound) {
    loadBasic();
    return;
  }

  char bootText[512];
  size_t bootLen = 0;
  if (!ebtksStorageReadText("/cpm/boot.txt", bootText, sizeof(bootText), bootLen)) {
    Serial.println("\n\rTeensy storage: /cpm/boot.txt not found; using BASIC fallback");
    loadBasic();
    return;
  }

  Serial.println("\n\rBooting CP/M from Teensy SD card /cpm:");
  int loaded = 0;
  char *save = nullptr;
  char *line = strtok_r(bootText, "\n", &save);

  while (line) {
    while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
    size_t ll = strlen(line);
    while (ll && (line[ll - 1] == '\r' || line[ll - 1] == ' ' || line[ll - 1] == '\t')) line[--ll] = 0;

    if (line[0] == '/') {
      char fileName[64] = {};
      unsigned int address = 0;
      if (sscanf(line, "%63s %x", fileName, &address) == 2 && address < sizeof(RAM)) {
        size_t actual = 0;
        Serial.printf("Loading: %-20s at 0x%04X ", fileName, address);
        if (ebtksStorageReadFileToRam(fileName, (uint16_t)address, actual)) {
          Serial.printf("%5u bytes... Done\n\r", (unsigned)actual);
          ++loaded;
        } else {
          Serial.println("FAILED");
        }
      }
    }

    line = strtok_r(nullptr, "\n", &save);
  }

  if (!loaded) {
    Serial.println("No boot images loaded; using BASIC fallback");
    loadBasic();
  }
}

//*********************************************************************************************
//****                 Directory helper used by the upstream monitor tools                 ****
//*********************************************************************************************
void SDprintDir(void) {
  // The CP/M boot path and virtual disks do not require directory enumeration.
  // Keep the command explicit rather than touching ESP32-local storage.
  outString((char *)"Directory listing is not implemented for EBTKS storage yet.\n\r");
}

//*********************************************************************************************
//****                    Set logical download path for sdcopy.com                         ****
//*********************************************************************************************
bool SDsetPath(void) {
  int a = pOut[DPARM + 1] << 8 | pOut[DPARM];
  int l = RAM[a];
  if (l >= (int)sizeof(sddir)) l = sizeof(sddir) - 1;

  int j = 0;
  for (int i = 1; i <= l && j < (int)sizeof(sddir) - 1; i++) {
    char c = (char)RAM[a + i];
    if (c != 0x20 || j != 0) sddir[j++] = c;
  }
  sddir[j] = 0;

  char buf[96];
  sprintf(buf, "Setting EBTKS storage path to: %s\n\r", sddir);
  outString(buf);
  return true;
}

//*********************************************************************************************
//****                   Open remote file and report 128-byte block count                  ****
//*********************************************************************************************
void SDfileOpen(void) {
  uint16_t sdbuffer = pOut[DPARM + 1] << 8 | pOut[DPARM];

  size_t baseLen = strnlen(sddir, sizeof(sddir));
  snprintf(sdfile, sizeof(sdfile), "%s%s", sddir, (baseLen && sddir[baseLen - 1] == '/') ? "" : "/");
  size_t used = strlen(sdfile);

  for (int n = 0; n < 12 && used < sizeof(sdfile) - 1; ++n) {
    char c = (char)pOut[DPARM + n + 2];
    if (!c || c == ' ') break;
    sdfile[used++] = c;
  }
  sdfile[used] = 0;

  uint32_t bytes = 0;
  unsigned int blocks = 0;
  if (ebtksStorageFileSize(sdfile, bytes)) {
    blocks = (bytes + 127U) / 128U;
    Serial.printf("open: %s %lu bytes %u block(s)\n", sdfile, (unsigned long)bytes, blocks);
  } else {
    Serial.printf("open failed: %s\n", sdfile);
  }

  RAM[sdbuffer + 1] = (blocks >> 8) & 0xff;
  RAM[sdbuffer] = blocks & 0xff;
}

//*********************************************************************************************
//****                  Read a 128-byte file block for sdcopy.com                         ****
//*********************************************************************************************
bool SDfileRead(void) {
  uint32_t block = pOut[DPARM + 3] << 8 | pOut[DPARM + 2];
  uint16_t address = pOut[DPARM + 1] << 8 | pOut[DPARM];
  return ebtksStorageReadRange(sdfile, block * 128UL, &RAM[address], 128);
}

//*********************************************************************************************
//****                         Z80 virtual disk write                                      ****
//*********************************************************************************************
bool diskWrite(void) {
  uint32_t packed = pOut[DPARM + 4] << 16 | pOut[DPARM + 3] << 8 | pOut[DPARM + 2];
  uint16_t address = pOut[DPARM + 1] << 8 | pOut[DPARM];
  vdrive = packed >> 14;
  uint32_t sector = packed & 0x3fff;

  bool ok = ebtksDiskWriteSector((uint8_t)vdrive, sector, &RAM[address]);
  if (!ok) Serial.printf("Virtual disk write failed: drive %d sector %lu\n",
                         vdrive, (unsigned long)sector);
  return ok;
}

//*********************************************************************************************
//****                         Z80 virtual disk read                                       ****
//*********************************************************************************************
bool diskRead(void) {
  uint32_t packed = pOut[DPARM + 4] << 16 | pOut[DPARM + 3] << 8 | pOut[DPARM + 2];
  uint16_t address = pOut[DPARM + 1] << 8 | pOut[DPARM];
  vdrive = packed >> 14;
  uint32_t sector = packed & 0x3fff;

  bool ok = ebtksDiskReadSector((uint8_t)vdrive, sector, &RAM[address]);
  if (!ok) Serial.printf("Virtual disk read failed: drive %d sector %lu\n",
                         vdrive, (unsigned long)sector);
  return ok;
}

//*********************************************************************************************
//****                     Load remote file into Z80 RAM                                  ****
//*********************************************************************************************
void FileToRAM(char c[], uint16_t address) {
  size_t actual = 0;
  Serial.printf("Loading: %s at 0x%04x ", c, address);
  if (ebtksStorageReadFileToRam(c, address, actual)) Serial.printf("%5u bytes... Done\n\r", (unsigned)actual);
  else Serial.println("FAILED");
}
