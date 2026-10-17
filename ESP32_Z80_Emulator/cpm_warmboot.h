#pragma once

#include <Arduino.h>
#include <stdlib.h>
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
// Keep a pristine host-side shadow of D000-E5FF.  Allocate it from the ESP32
// heap rather than as a static array: this firmware is already very close to
// the linker-visible DRAM/BSS limit, while the ESP32 heap has enough runtime
// capacity for this 5632-byte snapshot.
//
// The CPU emulator already has a single breakpoint facility, so reserve it
// internally while native CP/M is running.  We first stop at CCP entry D000
// after cold/warm boot to install a tiny BDOS vector shim, then arm the
// breakpoint at BIOS WBOOT entry E603.
//
// The shim lives in the intentionally unused CBIOS hole FFEB-FFFD:
//   LD A,C / OR A / JP Z,E603 / JP D806
// Thus ordinary CALL 0005 BDOS requests pass through unchanged, while BDOS
// function 0 reaches WBOOT even if an HP-oriented application has overwritten
// part of our lower-addressed BDOS before terminating.

static constexpr uint16_t CPM_SHADOW_START = 0xD000;
static constexpr size_t   CPM_SHADOW_SIZE  = 0x1600;  // D000-E5FF inclusive
static constexpr uint16_t CPM_CCP_ENTRY    = 0xD000;
static constexpr uint16_t CPM_BDOS_ENTRY   = 0xD806;
static constexpr uint16_t CPM_BIOS_ENTRY   = 0xE600;
static constexpr uint16_t CPM_WBOOT_ENTRY  = 0xE603;
static constexpr uint16_t CPM_BDOS_VECTOR  = 0x0005;
static constexpr uint16_t CPM_BDOS_SHIM    = 0xFFEB;

static uint8_t *cpmResidentShadow = nullptr;
static bool cpmResidentShadowValid = false;
static uint32_t cpmWarmBootCount = 0;

inline void cpmWarmBootWriteShim()
{
  // FFEB: LD A,C
  RAM[CPM_BDOS_SHIM + 0] = 0x79;
  // FFEC: OR A
  RAM[CPM_BDOS_SHIM + 1] = 0xB7;
  // FFED: JP Z,E603
  RAM[CPM_BDOS_SHIM + 2] = 0xCA;
  RAM[CPM_BDOS_SHIM + 3] = (uint8_t)(CPM_WBOOT_ENTRY & 0xFF);
  RAM[CPM_BDOS_SHIM + 4] = (uint8_t)(CPM_WBOOT_ENTRY >> 8);
  // FFF0: JP D806
  RAM[CPM_BDOS_SHIM + 5] = 0xC3;
  RAM[CPM_BDOS_SHIM + 6] = (uint8_t)(CPM_BDOS_ENTRY & 0xFF);
  RAM[CPM_BDOS_SHIM + 7] = (uint8_t)(CPM_BDOS_ENTRY >> 8);
}

inline void cpmWarmBootPatchBdosVector()
{
  // 0005: JP FFEB
  RAM[CPM_BDOS_VECTOR + 0] = 0xC3;
  RAM[CPM_BDOS_VECTOR + 1] = (uint8_t)(CPM_BDOS_SHIM & 0xFF);
  RAM[CPM_BDOS_VECTOR + 2] = (uint8_t)(CPM_BDOS_SHIM >> 8);
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

  // These JP opcodes identify the native CP/M image we currently support.
  // If boot.txt selects BASIC or some other image, leave the trap disabled.
  if (RAM[CPM_SHADOW_START] != 0xC3 ||
      RAM[CPM_BDOS_ENTRY]   != 0xC3 ||
      RAM[CPM_BIOS_ENTRY]   != 0xC3 ||
      RAM[CPM_WBOOT_ENTRY]  != 0xC3) {
    Serial.println("CP/M warm boot shadow: native CP/M image not detected; disabled");
    return false;
  }

  if (!cpmWarmBootEnsureShadow()) return false;

  memcpy(cpmResidentShadow, &RAM[CPM_SHADOW_START], CPM_SHADOW_SIZE);
  cpmWarmBootWriteShim();
  cpmResidentShadowValid = true;
  Serial.printf("CP/M warm boot shadow: captured %u bytes D000-E5FF\n",
                (unsigned)CPM_SHADOW_SIZE);
  return true;
}

inline void cpmWarmBootArmInitialTrap()
{
  if (cpmResidentShadowValid) {
    // Cold boot must first run the real BIOS so it can initialise page zero.
    // Stop at the first CCP entry, patch the BDOS vector, then switch the trap
    // over to WBOOT for normal operation.
    BP = CPM_CCP_ENTRY;
    bpOn = true;
  } else {
    BP = 0xFFFF;
    bpOn = false;
  }
}

inline bool cpmWarmBootHandleTrap()
{
  if (!cpmResidentShadowValid || !cpmResidentShadow) return false;

  if (PC == CPM_CCP_ENTRY) {
    // BOOT/WBOOT has just rebuilt page zero, so install our function-0 shim.
    cpmWarmBootWriteShim();
    cpmWarmBootPatchBdosVector();
    BP = CPM_WBOOT_ENTRY;
    bpOn = true;
    Serial.println("CP/M warm boot: BDOS function-0 shim active");
    return true;
  }

  if (PC == CPM_WBOOT_ENTRY) {
    // Restore the pristine CCP+BDOS before allowing the real CBIOS WBOOT code
    // to run.  WBOOT will reset its stack/buffer state and rebuild page zero.
    memcpy(&RAM[CPM_SHADOW_START], cpmResidentShadow, CPM_SHADOW_SIZE);
    cpmWarmBootWriteShim();
    ++cpmWarmBootCount;

    // WBOOT rewrites the BDOS vector at 0005, so catch its eventual JP D000
    // once and reinstall the shim before the CCP starts executing.
    BP = CPM_CCP_ENTRY;
    bpOn = true;

    Serial.printf("CP/M warm boot: restored D000-E5FF (count=%lu)\n",
                  (unsigned long)cpmWarmBootCount);
    return true;
  }

  return false;
}
