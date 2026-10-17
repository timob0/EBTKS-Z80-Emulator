#pragma once

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

// Native CP/M layout used by the EBTKS Z80 build:
//   D000-D7FF  CCP
//   D806-E5FF  BDOS and resident CP/M state
//   E600-FFFF  CBIOS
//
// CP/M applications may reclaim the CCP before terminating. A normal BIOS
// warm boot reloads the resident CCP/BDOS image before returning to the CCP;
// the historical CBIOS used by this emulator only jumps back to the in-memory
// copy. Large applications such as Microsoft INSTALL/Multiplan can therefore
// leave resident CP/M damaged at exit.
//
// Keep a pristine host-side shadow of D000-E5FF in heap memory and reserve the
// emulator's existing breakpoint at BIOS WBOOT entry E603. When WBOOT is
// reached, restore CCP+BDOS, then resume at the real CBIOS WBOOT routine so it
// can reset its stack, disk buffers and page-zero vectors normally.
//
// BDOS function 0 is special: if an application has already overwritten part
// of BDOS, it must still be able to request WBOOT. Do NOT move the standard
// page-zero BDOS vector at 0005 away from D806: many CP/M programs inspect
// 0006/0007 to discover the BDOS base / top of TPA. Instead patch the first
// instruction at D806 to jump through a small shim in the intentionally unused
// CBIOS hole FFEB-FFFD.
//
// Shim at FFEB (AF preserved for ordinary BDOS calls):
//   PUSH AF
//   LD   A,C
//   OR   A
//   JR   Z,warm
//   POP  AF
//   JP   D811        ; original FBASE1 target
// warm:
//   POP  AF
//   JP   E603        ; BIOS WBOOT
//
// Page zero therefore remains exactly the standard JP D806 layout.

static constexpr uint16_t CPM_SHADOW_START = 0xD000;
static constexpr size_t   CPM_SHADOW_SIZE  = 0x1600;  // D000-E5FF inclusive
static constexpr uint16_t CPM_BDOS_ENTRY   = 0xD806;
static constexpr uint16_t CPM_BDOS_BODY    = 0xD811;
static constexpr uint16_t CPM_BIOS_ENTRY   = 0xE600;
static constexpr uint16_t CPM_WBOOT_ENTRY  = 0xE603;
static constexpr uint16_t CPM_BDOS_SHIM    = 0xFFEB;

static uint8_t *cpmResidentShadow = nullptr;
static bool cpmResidentShadowValid = false;
static uint32_t cpmWarmBootCount = 0;

inline void cpmWarmBootWriteShim()
{
  // FFEB: PUSH AF
  RAM[CPM_BDOS_SHIM + 0] = 0xF5;
  // FFEC: LD A,C
  RAM[CPM_BDOS_SHIM + 1] = 0x79;
  // FFED: OR A
  RAM[CPM_BDOS_SHIM + 2] = 0xB7;
  // FFEE: JR Z,FFF4  (+4 from FFF0)
  RAM[CPM_BDOS_SHIM + 3] = 0x28;
  RAM[CPM_BDOS_SHIM + 4] = 0x04;
  // FFF0: POP AF
  RAM[CPM_BDOS_SHIM + 5] = 0xF1;
  // FFF1: JP D811
  RAM[CPM_BDOS_SHIM + 6] = 0xC3;
  RAM[CPM_BDOS_SHIM + 7] = (uint8_t)(CPM_BDOS_BODY & 0xFF);
  RAM[CPM_BDOS_SHIM + 8] = (uint8_t)(CPM_BDOS_BODY >> 8);
  // FFF4: POP AF
  RAM[CPM_BDOS_SHIM + 9] = 0xF1;
  // FFF5: JP E603
  RAM[CPM_BDOS_SHIM + 10] = 0xC3;
  RAM[CPM_BDOS_SHIM + 11] = (uint8_t)(CPM_WBOOT_ENTRY & 0xFF);
  RAM[CPM_BDOS_SHIM + 12] = (uint8_t)(CPM_WBOOT_ENTRY >> 8);
}

inline void cpmWarmBootPatchBdosEntry()
{
  // Preserve the standard page-zero vector (0005 -> D806). Only replace the
  // original D806 "JP D811" with "JP FFEB".
  RAM[CPM_BDOS_ENTRY + 0] = 0xC3;
  RAM[CPM_BDOS_ENTRY + 1] = (uint8_t)(CPM_BDOS_SHIM & 0xFF);
  RAM[CPM_BDOS_ENTRY + 2] = (uint8_t)(CPM_BDOS_SHIM >> 8);
}

inline bool cpmWarmBootEnsureShadow()
{
  if (cpmResidentShadow) return true;

  cpmResidentShadow = static_cast<uint8_t *>(malloc(CPM_SHADOW_SIZE));
  if (!cpmResidentShadow) {
    Serial.printf("CP/M warm boot shadow: heap allocation of %u bytes failed; disabled\n",
                  (unsigned)CPM_SHADOW_SIZE);
    return false;
  }

  return true;
}

inline bool cpmWarmBootCapture()
{
  cpmResidentShadowValid = false;

  // Identify the native CP/M image before patching anything. In particular,
  // D806 must still contain its original JP D811 instruction here.
  if (RAM[CPM_SHADOW_START] != 0xC3 ||
      RAM[CPM_BDOS_ENTRY]   != 0xC3 ||
      RAM[CPM_BDOS_ENTRY + 1] != (uint8_t)(CPM_BDOS_BODY & 0xFF) ||
      RAM[CPM_BDOS_ENTRY + 2] != (uint8_t)(CPM_BDOS_BODY >> 8) ||
      RAM[CPM_BIOS_ENTRY]   != 0xC3 ||
      RAM[CPM_WBOOT_ENTRY]  != 0xC3) {
    Serial.println("CP/M warm boot shadow: native CP/M image not detected; disabled");
    return false;
  }

  if (!cpmWarmBootEnsureShadow()) return false;

  memcpy(cpmResidentShadow, &RAM[CPM_SHADOW_START], CPM_SHADOW_SIZE);
  cpmWarmBootWriteShim();
  cpmWarmBootPatchBdosEntry();
  cpmResidentShadowValid = true;

  Serial.printf("CP/M warm boot shadow: captured %u bytes D000-E5FF\n",
                (unsigned)CPM_SHADOW_SIZE);
  Serial.println("CP/M warm boot: BDOS base preserved at D806; function-0 shim active");
  return true;
}

inline void cpmWarmBootArmTrap()
{
  if (cpmResidentShadowValid) {
    BP = CPM_WBOOT_ENTRY;
    bpOn = true;
  } else {
    BP = 0xFFFF;
    bpOn = false;
  }
}

inline bool cpmWarmBootHandleTrap()
{
  if (!cpmResidentShadowValid || !cpmResidentShadow || PC != CPM_WBOOT_ENTRY)
    return false;

  // Restore pristine CCP+BDOS, then immediately reinstall the D806 shim before
  // allowing the real WBOOT code to execute. WBOOT will rebuild page zero with
  // its normal JP D806 BDOS vector, so no further CCP-entry trap is required.
  memcpy(&RAM[CPM_SHADOW_START], cpmResidentShadow, CPM_SHADOW_SIZE);
  cpmWarmBootWriteShim();
  cpmWarmBootPatchBdosEntry();
  ++cpmWarmBootCount;

  BP = CPM_WBOOT_ENTRY;
  bpOn = true;

  Serial.printf("CP/M warm boot: restored D000-E5FF (count=%lu)\n",
                (unsigned long)cpmWarmBootCount);
  return true;
}
