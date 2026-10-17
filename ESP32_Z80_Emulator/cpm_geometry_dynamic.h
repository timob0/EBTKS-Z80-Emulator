#pragma once

#include <Arduino.h>
#include "globals.h"
#include "cpm_media.h"

// Generic mixed-geometry CBIOS patcher.
//
// The stage-1 HP-LIF experiment proved that the cbios.rom actually installed
// on a machine must not be assumed to use one fixed DPH address.  Locate the
// native 8 MiB DPB and the complete 16-entry DPH table from their contents,
// restore every drive to native geometry, then patch each actively mounted
// HP 82900 floppy independently.

inline uint16_t cpmGeomRead16(uint16_t p)
{
  return (uint16_t)RAM[p] | ((uint16_t)RAM[(uint16_t)(p + 1)] << 8);
}

inline void cpmGeomWrite16(uint16_t p, uint16_t value)
{
  RAM[p] = (uint8_t)(value & 0xff);
  RAM[(uint16_t)(p + 1)] = (uint8_t)(value >> 8);
}

inline bool cpmGeomNativeDpbAt(uint16_t p)
{
  if (p < 0x0100 || p > 0xfff0) return false;
  static const uint8_t nativeDpb[15] = {
    0x80, 0x00, // SPT 128
    0x05,       // BSH 5
    0x1f,       // BLM 31
    0x01,       // EXM 1
    0xff, 0x07, // DSM 2047
    0xff, 0x01, // DRM 511
    0xf0, 0x00, // AL0/AL1
    0x00, 0x00, // CKS 0
    0x00, 0x00  // OFF 0
  };
  return !memcmp(&RAM[p], nativeDpb, sizeof(nativeDpb));
}

inline uint16_t cpmGeomFindNativeDpb()
{
  for (uint32_t p = 0xd000; p <= 0xfff0; ++p) {
    if (cpmGeomNativeDpbAt((uint16_t)p)) return (uint16_t)p;
  }
  return 0;
}

inline bool cpmGeomDphPrefixZero(uint16_t p)
{
  for (uint8_t i = 0; i < 8; ++i) if (RAM[(uint16_t)(p + i)] != 0) return false;
  return true;
}

inline uint16_t cpmGeomFindDphBase(uint16_t nativeDpb)
{
  if (!nativeDpb) return 0;

  // A valid table is 16 consecutive 16-byte DPHs.  All entries share DIRBUF;
  // their ALV buffers are distinct and laid out in increasing RAM order.  At
  // least one DPH must still reference the native DPB.  This remains true even
  // if the earlier media-manager or stage-1 code already patched another drive.
  for (uint32_t base32 = 0xd000; base32 <= 0xf000; ++base32) {
    uint16_t base = (uint16_t)base32;
    uint16_t dirbuf = cpmGeomRead16((uint16_t)(base + 8));
    if (!dirbuf || !cpmGeomDphPrefixZero(base)) continue;

    bool good = true;
    bool nativeSeen = false;
    uint16_t previousAlv = 0;

    for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
      uint32_t dph32 = base32 + (uint32_t)drive * 16UL;
      if (dph32 + 15 >= sizeof(RAM)) { good = false; break; }
      uint16_t dph = (uint16_t)dph32;
      if (!cpmGeomDphPrefixZero(dph) || cpmGeomRead16((uint16_t)(dph + 8)) != dirbuf) {
        good = false;
        break;
      }

      uint16_t dpb = cpmGeomRead16((uint16_t)(dph + 10));
      if (dpb == nativeDpb) nativeSeen = true;

      uint16_t alv = cpmGeomRead16((uint16_t)(dph + 14));
      if (!alv || (drive && alv <= previousAlv)) {
        good = false;
        break;
      }
      if (drive) {
        uint16_t delta = (uint16_t)(alv - previousAlv);
        if (delta < 160 || delta > 384) {
          good = false;
          break;
        }
      }
      previousAlv = alv;
    }

    if (good && nativeSeen) return base;
  }
  return 0;
}

inline bool cpmApplyMountedGeometryDynamic()
{
  uint16_t nativeDpb = cpmGeomFindNativeDpb();
  uint16_t dphBase = cpmGeomFindDphBase(nativeDpb);
  if (!nativeDpb || !dphBase) {
    Serial.printf("CP/M media: dynamic CBIOS geometry discovery failed (DPB=%04X DPH=%04X)\n",
                  nativeDpb, dphBase);
    return false;
  }

  static const uint8_t hpDpb[15] = {
    0x20, 0x00,             // SPT  = 32
    0x03,                   // BSH  = 3
    0x07,                   // BLM  = 7
    0x00,                   // EXM  = 0
    0xf3, 0x00,             // DSM  = 243
    0x7f, 0x00,             // DRM  = 127
    0xf0,                   // AL0
    0x00,                   // AL1
    0x20, 0x00,             // CKS  = 32
    0x03, 0x00              // OFF  = 3
  };

  // First return all sixteen DPHs to the native hard-disk geometry.  This is
  // essential when a drive that used to contain an HP floppy is remounted as
  // an 8 MiB disk or unmounted.
  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    uint16_t dph = (uint16_t)(dphBase + (uint16_t)drive * 16U);
    cpmGeomWrite16((uint16_t)(dph + 10), nativeDpb);
    cpmGeomWrite16((uint16_t)(dph + 12), 0);
  }

  bool ok = true;
  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    if (!cpmActiveMounts[drive].mounted ||
        cpmActiveMounts[drive].profile != CPM_MEDIA_HP82900_LIF) continue;

    uint16_t dph = (uint16_t)(dphBase + (uint16_t)drive * 16U);
    uint16_t alv = cpmGeomRead16((uint16_t)(dph + 14));
    uint32_t hpDpbAddr32 = (uint32_t)alv + 64UL;
    uint32_t hpCsvAddr32 = (uint32_t)alv + 96UL;
    uint32_t slotEnd = 0x10000UL;
    if (drive + 1 < CPM_DRIVE_COUNT) {
      uint16_t nextDph = (uint16_t)(dph + 16);
      slotEnd = cpmGeomRead16((uint16_t)(nextDph + 14));
    }

    if (!alv || hpCsvAddr32 + 32UL > slotEnd || hpCsvAddr32 + 32UL > 0x10000UL) {
      Serial.printf("CP/M media: %c: ALV slot too small for HP DPB/CSV (ALV=%04X end=%04lX)\n",
                    'A' + drive, alv, (unsigned long)slotEnd);
      cpmClearMount(cpmActiveMounts[drive]);
      ok = false;
      continue;
    }

    uint16_t hpDpbAddr = (uint16_t)hpDpbAddr32;
    uint16_t hpCsvAddr = (uint16_t)hpCsvAddr32;
    memcpy(&RAM[hpDpbAddr], hpDpb, sizeof(hpDpb));
    memset(&RAM[hpCsvAddr], 0, 32);
    cpmGeomWrite16((uint16_t)(dph + 10), hpDpbAddr);
    cpmGeomWrite16((uint16_t)(dph + 12), hpCsvAddr);

    bool verified = cpmGeomRead16((uint16_t)(dph + 10)) == hpDpbAddr &&
                    cpmGeomRead16((uint16_t)(dph + 12)) == hpCsvAddr &&
                    RAM[hpDpbAddr] == 0x20 && RAM[hpDpbAddr + 2] == 0x03 &&
                    RAM[hpDpbAddr + 5] == 0xf3;
    if (!verified) {
      Serial.printf("CP/M media: %c: HP geometry verification failed\n", 'A' + drive);
      cpmClearMount(cpmActiveMounts[drive]);
      ok = false;
      continue;
    }

    Serial.printf("CP/M media: %c: HP geometry DPH=%04X DPB=%04X CSV=%04X\n",
                  'A' + drive, dph, hpDpbAddr, hpCsvAddr);
  }

  Serial.printf("CP/M media: dynamic CBIOS geometry base=%04X nativeDPB=%04X\n",
                dphBase, nativeDpb);
  return ok;
}
