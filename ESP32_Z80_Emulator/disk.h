#include "serial.h"
#include <stdio.h>
#include <string.h>
#pragma once
#include "globals.h"
#include "ebtks_storage.h"
#include "cpm_media.h"

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
  // Z80 cold/reset bootstrap may reactivate the same image paths after
  // external media changes. Never reuse read-ahead across a Z80 reset.
  spiDiskInvalidateCache();
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
    return;
  }

  // Load the persistent A:-P: media table and patch each DPH/DPB before the
  // Z80 starts.  This replaces the earlier one-off B: HP-LIF experiment.
  cpmActivateMounts();
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
  uint32_t biosSector = packed & 0x3fff;

  if (!cpmValidDrive(vdrive)) return false;
  const CpmMountEntry &mount = cpmActiveMounts[vdrive];
  if (!mount.mounted) {
    Serial.printf("CP/M write rejected: drive %c: is unmounted\n", 'A' + vdrive);
    return false;
  }
  if (mount.readOnly) {
    Serial.printf("CP/M write rejected: drive %c: is read-only (%s)\n",
                  'A' + vdrive, mount.path);
    return false;
  }

  uint32_t storageSector = 0;
  if (!cpmMapMountedSector((uint8_t)vdrive, biosSector, storageSector)) {
    Serial.printf("CP/M write mapping failed: drive %c: BIOS sector %lu\n",
                  'A' + vdrive, (unsigned long)biosSector);
    return false;
  }

  bool ok = cpmMountedDiskWrite((uint8_t)vdrive, storageSector, &RAM[address]);
  if (!ok) Serial.printf("Virtual disk write failed: drive %c: sector %lu path=%s\n",
                         'A' + vdrive, (unsigned long)storageSector, mount.path);
  return ok;
}

//*********************************************************************************************
//****                         Z80 virtual disk read                                       ****
//*********************************************************************************************
bool diskRead(void) {
  uint32_t packed = pOut[DPARM + 4] << 16 | pOut[DPARM + 3] << 8 | pOut[DPARM + 2];
  uint16_t address = pOut[DPARM + 1] << 8 | pOut[DPARM];
  vdrive = packed >> 14;
  uint32_t biosSector = packed & 0x3fff;

  if (!cpmValidDrive(vdrive)) return false;
  const CpmMountEntry &mount = cpmActiveMounts[vdrive];

  // The historical CBIOS does not propagate host read errors cleanly to BDOS.
  // For an unmounted drive return a deterministic erased/empty sector rather
  // than exposing stale data from the previous host buffer.
  if (!mount.mounted) {
    memset(&RAM[address], 0xE5, EBTKS_DISK_SECTOR_SIZE);
    return true;
  }

  uint32_t storageSector = 0;
  if (!cpmMapMountedSector((uint8_t)vdrive, biosSector, storageSector)) {
    Serial.printf("CP/M read mapping failed: drive %c: BIOS sector %lu\n",
                  'A' + vdrive, (unsigned long)biosSector);
    memset(&RAM[address], 0xE5, EBTKS_DISK_SECTOR_SIZE);
    return false;
  }

  bool ok = cpmMountedDiskRead((uint8_t)vdrive, storageSector, &RAM[address]);
  if (!ok) {
    Serial.printf("Virtual disk read failed: drive %c: sector %lu path=%s\n",
                  'A' + vdrive, (unsigned long)storageSector, mount.path);
    memset(&RAM[address], 0xE5, EBTKS_DISK_SECTOR_SIZE);
  }
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
