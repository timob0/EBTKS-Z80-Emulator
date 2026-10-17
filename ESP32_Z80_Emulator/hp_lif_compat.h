#pragma once

#include <Arduino.h>
#include "globals.h"

// Stage-1 HP 82900A CP/M compatibility helper.
//
// Locate the current EBTKS CBIOS DPH table dynamically, rather than relying on
// fixed addresses from one cbios.rom build.  Drive B keeps its existing ALV
// allocation, but its unused tail is used to hold the HP 5.25-inch DPB and CSV.
// This is installed after bootstrap() has loaded the BIOS and before PC is
// released, so BDOS sees the HP geometry the first time B: is selected.

static bool hpLifBCompatReady = false;

static inline uint16_t hpRead16(uint16_t p)
{
  return (uint16_t)RAM[p] | ((uint16_t)RAM[(uint16_t)(p + 1)] << 8);
}

static inline void hpWrite16(uint16_t p, uint16_t v)
{
  RAM[p] = (uint8_t)(v & 0xff);
  RAM[(uint16_t)(p + 1)] = (uint8_t)(v >> 8);
}

static inline bool hpNativeDpbLooksRight(uint16_t p)
{
  // Native EBTKS DPB: SPT=128, BSH=5, BLM=31, EXM=1,
  // DSM=2047, DRM=511, AL0=F0, AL1=00, CKS=0, OFF=0.
  if (p < 0x0100 || p > 0xfff0) return false;
  return RAM[p + 0]  == 0x80 && RAM[p + 1]  == 0x00 &&
         RAM[p + 2]  == 0x05 && RAM[p + 3]  == 0x1f &&
         RAM[p + 4]  == 0x01 &&
         RAM[p + 5]  == 0xff && RAM[p + 6]  == 0x07 &&
         RAM[p + 7]  == 0xff && RAM[p + 8]  == 0x01 &&
         RAM[p + 9]  == 0xf0 && RAM[p + 10] == 0x00 &&
         RAM[p + 11] == 0x00 && RAM[p + 12] == 0x00 &&
         RAM[p + 13] == 0x00 && RAM[p + 14] == 0x00;
}

static inline bool installHpLifDriveBCompat()
{
  hpLifBCompatReady = false;

  // DPH entry layout (16 bytes):
  //   +0 XLT, +2/+4/+6 scratch, +8 DIRBUF, +10 DPB, +12 CSV, +14 ALV
  // Find drive A from its native DPB signature and validate that the following
  // entry looks like drive B from the same DPH table.
  uint16_t dphA = 0;
  for (uint32_t p = 0xd000; p <= 0xf800; ++p) {
    bool firstEightZero = true;
    for (int i = 0; i < 8; ++i) {
      if (RAM[p + i] != 0) {
        firstEightZero = false;
        break;
      }
    }
    if (!firstEightZero) continue;

    uint16_t nativeDpb = hpRead16((uint16_t)(p + 10));
    if (!hpNativeDpbLooksRight(nativeDpb)) continue;

    uint32_t b = p + 16;
    if (b + 15 >= sizeof(RAM)) continue;
    bool bFirstEightZero = true;
    for (int i = 0; i < 8; ++i) {
      if (RAM[b + i] != 0) {
        bFirstEightZero = false;
        break;
      }
    }
    if (!bFirstEightZero) continue;

    // A and B should share the directory buffer.  Their allocation vectors
    // must be distinct and non-zero.
    if (hpRead16((uint16_t)(p + 8)) != hpRead16((uint16_t)(b + 8))) continue;
    uint16_t alvA = hpRead16((uint16_t)(p + 14));
    uint16_t alvB = hpRead16((uint16_t)(b + 14));
    if (!alvA || !alvB || alvA == alvB) continue;

    dphA = (uint16_t)p;
    break;
  }

  if (!dphA) {
    Serial.println("HP LIF B: could not locate native CBIOS DPH table; B: compatibility disabled");
    return false;
  }

  uint16_t dphB = (uint16_t)(dphA + 16);
  uint16_t nativeDpb = hpRead16((uint16_t)(dphA + 10));
  uint16_t alvB = hpRead16((uint16_t)(dphB + 14));
  uint16_t alvC = hpRead16((uint16_t)(dphB + 16 + 14));

  // Native DSM=2047 requires 257 ALV bytes per drive in this BIOS.  Verify
  // there is plenty of room in B's allocation-vector slot before using its
  // tail for our much smaller HP structures.  The HP filesystem itself needs
  // only 31 allocation-vector bytes (244 blocks).
  if (alvC <= alvB || (uint16_t)(alvC - alvB) < 160) {
    Serial.printf("HP LIF B: unexpected ALV layout B=%04X C=%04X; compatibility disabled\n",
                  alvB, alvC);
    return false;
  }

  const uint16_t hpDpb = (uint16_t)(alvB + 64);
  const uint16_t hpCsv = (uint16_t)(alvB + 96);

  static const uint8_t hpDpbBytes[15] = {
    0x20, 0x00,             // SPT = 32 x 128-byte records/track
    0x03,                   // BSH = 3 (1 KiB allocation block)
    0x07,                   // BLM = 7
    0x00,                   // EXM = 0
    0xF3, 0x00,             // DSM = 243 (244 blocks)
    0x7F, 0x00,             // DRM = 127 (128 directory entries)
    0xF0,                   // AL0
    0x00,                   // AL1
    0x20, 0x00,             // CKS = 32
    0x03, 0x00              // OFF = 3 reserved tracks
  };

  memcpy(&RAM[hpDpb], hpDpbBytes, sizeof(hpDpbBytes));
  memset(&RAM[hpCsv], 0, 32);
  hpWrite16((uint16_t)(dphB + 10), hpDpb);
  hpWrite16((uint16_t)(dphB + 12), hpCsv);

  // Read back the pointers/signature.  This makes failure explicit instead of
  // allowing CP/M to scan a LIF image with the native 8 MiB geometry.
  hpLifBCompatReady =
      hpRead16((uint16_t)(dphB + 10)) == hpDpb &&
      hpRead16((uint16_t)(dphB + 12)) == hpCsv &&
      RAM[hpDpb] == 0x20 && RAM[hpDpb + 2] == 0x03 && RAM[hpDpb + 5] == 0xf3;

  if (hpLifBCompatReady) {
    Serial.printf("HP LIF B: DPH=%04X nativeDPB=%04X HP-DPB=%04X CSV=%04X ALV=%04X\n",
                  dphB, nativeDpb, hpDpb, hpCsv, alvB);
    Serial.println("HP LIF B: geometry installed: 244 KiB, 32 records/track, 3 reserved tracks, read-only");
  } else {
    Serial.println("HP LIF B: DPB readback verification failed; compatibility disabled");
  }

  return hpLifBCompatReady;
}
