#include "serial.h"
#include <stdio.h>
#include <string.h>
#pragma once
#include "globals.h"
#include "ebtks_storage.h"

void loadBasic(void);
void bootstrap(void);
bool diskRead(void);
bool diskWrite(void);
void SDfileOpen(void);
bool SDfileRead(void);
bool SDprintDir(void);
bool SDsetPath(void);
void FileToRAM(char[], uint16_t);
bool installHpLifDriveB(void);
bool mapHpLifDriveBSector(uint32_t biosSector, uint32_t &rawSector);

// Stage-1 HP 82900A compatibility experiment.
//
// The existing EBTKS CP/M BIOS uses 512-byte host sectors and a single native
// 8 MiB DPB for all drives. Original HP 82900A 5.25-inch CP/M media use:
//
//   32 x 128-byte CP/M records/track
//   1 KiB allocation blocks
//   244 allocation blocks
//   128 directory entries
//   3 reserved tracks
//
// In the HP LIF images examined so far the complete CP/M block device occupies
// exactly the first 256 KiB of the LIF image. The BIOS still computes a host
// LBA using its native 32 x 512-byte sectors/track arithmetic, so drive B reads
// are translated below from:
//
//   biosSector = track * 32 + hostSector(0..7)
//
// to the raw LIF-backed 512-byte sector:
//
//   rawSector  = track * 8 + hostSector
//
// Drive B is intentionally write-protected in this first stage.
static const uint8_t  HP_LIF_DRIVE_B = 1;
static const uint32_t HP_LIF_RAW_SECTORS = 512; // 256 KiB / 512

// Addresses in the currently supplied EBTKS cbios.rom (loaded at E600h).
// The BIOS has an unused filler area FD87h..FFE7h, so we can install an HP DPB
// and checksum vector there without rebuilding the Z80 BIOS for this first
// experiment. B:'s DPH is the second 16-byte DPH entry at E643h.
static const uint16_t HP_DPH_B_DPB_PTR = 0xE64D;
static const uint16_t HP_DPH_B_CSV_PTR = 0xE64F;
static const uint16_t HP_DPB_ADDR       = 0xFD87;
static const uint16_t HP_CSV_ADDR       = 0xFD96;
static const size_t   HP_CSV_LEN        = 32;

//*********************************************************************************************
//****             Install HP 82900A 5.25-inch DPB for read-only drive B                  ****
//*********************************************************************************************
bool installHpLifDriveB(void) {
  // Guard against silently patching an unrelated/rebuilt BIOS. In the current
  // cbios.rom, B:'s DPB pointer is the common native DPB at E733h and its CSV
  // pointer is null.
  if (RAM[HP_DPH_B_DPB_PTR] != 0x33 ||
      RAM[HP_DPH_B_DPB_PTR + 1] != 0xE7 ||
      RAM[HP_DPH_B_CSV_PTR] != 0x00 ||
      RAM[HP_DPH_B_CSV_PTR + 1] != 0x00) {
    Serial.println("HP LIF B: patch skipped: unexpected CBIOS DPH layout");
    return false;
  }

  // HP 82900A 5.25-inch CP/M DPB:
  // SPT=32, BSH=3, BLM=7, EXM=0, DSM=243, DRM=127,
  // AL0=F0h, AL1=00h, CKS=32, OFF=3.
  static const uint8_t hpDpb[] = {
    0x20, 0x00,             // SPT  = 32
    0x03,                   // BSH  = 3
    0x07,                   // BLM  = 7
    0x00,                   // EXM  = 0
    0xF3, 0x00,             // DSM  = 243
    0x7F, 0x00,             // DRM  = 127
    0xF0,                   // AL0
    0x00,                   // AL1
    0x20, 0x00,             // CKS  = 32
    0x03, 0x00              // OFF  = 3
  };

  memcpy(&RAM[HP_DPB_ADDR], hpDpb, sizeof(hpDpb));
  memset(&RAM[HP_CSV_ADDR], 0, HP_CSV_LEN);

  // Repoint only B:'s DPB and checksum-vector fields. Its directory buffer and
  // allocation vector remain the existing BIOS allocations.
  RAM[HP_DPH_B_DPB_PTR]     = (uint8_t)(HP_DPB_ADDR & 0xFF);
  RAM[HP_DPH_B_DPB_PTR + 1] = (uint8_t)(HP_DPB_ADDR >> 8);
  RAM[HP_DPH_B_CSV_PTR]     = (uint8_t)(HP_CSV_ADDR & 0xFF);
  RAM[HP_DPH_B_CSV_PTR + 1] = (uint8_t)(HP_CSV_ADDR >> 8);

  Serial.println("HP 82900A LIF compatibility: B: = 256 KiB read-only HP CP/M disk");
  return true;
}

//*********************************************************************************************
//****      Translate current native-BIOS host LBA to raw HP LIF 512-byte sector          ****
//*********************************************************************************************
bool mapHpLifDriveBSector(uint32_t biosSector, uint32_t &rawSector) {
  uint32_t track = biosSector >> 5;          // native BIOS encoded *32
  uint32_t hostSector = biosSector & 0x1F;

  // With the HP DPB there are only 32 CP/M records/track = eight 512-byte
  // host sectors. Anything 8..31 means the expected BIOS/DPB relationship
  // has been violated, so fail rather than reading the wrong part of the LIF.
  if (hostSector >= 8) return false;

  rawSector = (track << 3) | hostSector;      // track * 8 + sector
  return rawSector < HP_LIF_RAW_SECTORS;
}

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
    return;
  }

  installHpLifDriveB();
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

  // Stage 1 deliberately protects the original HP LIF media. The current
  // generic BIOS has no error-status return channel for host-sector writes,
  // so CP/M may believe a rejected write completed. Do not use B: as a
  // destination yet; this guard exists to preserve the image during testing.
  if (vdrive == HP_LIF_DRIVE_B) {
    Serial.printf("HP LIF B: write rejected (read-only), BIOS sector %lu\n",
                  (unsigned long)sector);
    return false;
  }

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

  uint32_t storageSector = sector;
  if (vdrive == HP_LIF_DRIVE_B) {
    if (!mapHpLifDriveBSector(sector, storageSector)) {
      Serial.printf("HP LIF B: invalid BIOS sector %lu\n", (unsigned long)sector);
      return false;
    }
  }

  bool ok = ebtksDiskReadSector((uint8_t)vdrive, storageSector, &RAM[address]);
  if (!ok) Serial.printf("Virtual disk read failed: drive %d sector %lu (storage %lu)\n",
                         vdrive, (unsigned long)sector, (unsigned long)storageSector);
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
