#pragma once

#include <Arduino.h>
#include <string.h>

// Native CP/M layout used by the EBTKS Z80 build:
//   D000-D7FF  CCP
//   D806-E5FF  BDOS and resident CP/M state
//   E600-FFFF  CBIOS
//
// CP/M applications are allowed to use/overwrite the CCP before terminating;
// a real BIOS warm boot reloads the resident CP/M image before returning to
// the command processor.  The historical CBIOS used by this emulator jumps
// straight back to the in-memory CCP instead, which fails for large programs
// (notably Microsoft INSTALL/Multiplan) that reclaim high memory.
//
// Keep a pristine host-side shadow of D000-E5FF and reserve the emulator's
// existing single breakpoint as an internal WBOOT trap at E603.  The CPU task
// stops *before* executing WBOOT; the main task restores CCP+BDOS and resumes
// at E603 so the real CBIOS WBOOT routine can reset its own stack, disk buffer
// state and low-memory vectors in the normal way.

static constexpr uint16_t CPM_SHADOW_START = 0xD000;
static constexpr size_t   CPM_SHADOW_SIZE  = 0x1600;  // D000-E5FF inclusive
static constexpr uint16_t CPM_BDOS_ENTRY   = 0xD806;
static constexpr uint16_t CPM_BIOS_ENTRY   = 0xE600;
static constexpr uint16_t CPM_WBOOT_ENTRY  = 0xE603;

static uint8_t cpmResidentShadow[CPM_SHADOW_SIZE];
static bool cpmResidentShadowValid = false;
static uint32_t cpmWarmBootCount = 0;

inline bool cpmWarmBootCapture()
{
  cpmResidentShadowValid = false;

  // These JP opcodes identify the native CP/M image we currently support.
  // If boot.txt selects BASIC or some other image, leave the trap disabled.
  if (RAM[CPM_SHADOW_START] != 0xC3 ||
      RAM[CPM_BDOS_ENTRY]   != 0xC3 ||
      RAM[CPM_BIOS_ENTRY]   != 0xC3 ||
      RAM[CPM_WBOOT_ENTRY]  != 0xC3) {
    Serial.println("CP/M warm boot shadow: native CP/M image not detected; disabled");
    return false;
  }

  memcpy(cpmResidentShadow, &RAM[CPM_SHADOW_START], CPM_SHADOW_SIZE);
  cpmResidentShadowValid = true;
  Serial.printf("CP/M warm boot shadow: captured %u bytes D000-E5FF\n",
                (unsigned)CPM_SHADOW_SIZE);
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
  if (!cpmResidentShadowValid || PC != CPM_WBOOT_ENTRY) return false;

  memcpy(&RAM[CPM_SHADOW_START], cpmResidentShadow, CPM_SHADOW_SIZE);
  ++cpmWarmBootCount;
  Serial.printf("CP/M warm boot: restored D000-E5FF (count=%lu)\n",
                (unsigned long)cpmWarmBootCount);
  return true;
}
