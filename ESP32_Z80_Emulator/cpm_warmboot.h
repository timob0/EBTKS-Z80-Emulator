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
// The original HP 82900A BIOS also has a useful non-standard CONIN convention
// for special HP-86/87 keys.  The raw 8-bit keycode is returned in B while A
// contains keycode & 7Fh.  Appendix D of the HP reference manual documents the
// high-bit cursor/function key values, and Microsoft Multiplan's HP-86 driver
// relies on this behavior.  Our native CBIOS originally returned the byte only
// in A, so special keys such as 9Fh (left arrow) were not recognized properly.
// Patch the two native UART CONIN tails through a second tiny shim in the same
// unused high-memory hole.
//
// FFEB-FFF2: BDOS function-0 shim
//   LD   A,C
//   OR   A
//   JP   Z,E603       ; BIOS WBOOT
//   JP   D811         ; original BDOS FBASE1 body
//
// FFF3-FFF9: HP-compatible CONIN shim
//   IN   A,(80h)
//   LD   B,A          ; preserve complete HP keycode
//   AND  7Fh          ; standard 7-bit CP/M character result
//   POP  HL           ; matches native coninA/coninB stack frame
//   RET

static constexpr uint16_t CPM_SHADOW_START = 0xD000;
static constexpr size_t   CPM_SHADOW_SIZE  = 0x1600;  // D000-E5FF inclusive
static constexpr uint16_t CPM_BDOS_ENTRY   = 0xD806;
static constexpr uint16_t CPM_BDOS_BODY    = 0xD811;
static constexpr uint16_t CPM_BIOS_ENTRY   = 0xE600;
static constexpr uint16_t CPM_WBOOT_ENTRY  = 0xE603;

static constexpr uint16_t CPM_BDOS_SHIM    = 0xFFEB;
static constexpr uint16_t CPM_HPKEY_SHIM   = 0xFFF3;

// Native CBIOS 1.1 addresses from the checked-in cbios.lst.  At both sites the
// original bytes are DB 80 E1 C9: IN A,(80h), POP HL, RET.
static constexpr uint16_t CPM_CONIN_A_READ = 0xE82C;
static constexpr uint16_t CPM_CONIN_B_READ = 0xE837;

static uint8_t *cpmResidentShadow = nullptr;
static bool cpmResidentShadowValid = false;
static bool cpmHpConinCompatActive = false;
static uint32_t cpmWarmBootCount = 0;

inline void cpmWarmBootWriteShims()
{
  // FFEB: LD A,C
  RAM[CPM_BDOS_SHIM + 0] = 0x79;
  // FFEC: OR A
  RAM[CPM_BDOS_SHIM + 1] = 0xB7;
  // FFED: JP Z,E603
  RAM[CPM_BDOS_SHIM + 2] = 0xCA;
  RAM[CPM_BDOS_SHIM + 3] = (uint8_t)(CPM_WBOOT_ENTRY & 0xFF);
  RAM[CPM_BDOS_SHIM + 4] = (uint8_t)(CPM_WBOOT_ENTRY >> 8);
  // FFF0: JP D811
  RAM[CPM_BDOS_SHIM + 5] = 0xC3;
  RAM[CPM_BDOS_SHIM + 6] = (uint8_t)(CPM_BDOS_BODY & 0xFF);
  RAM[CPM_BDOS_SHIM + 7] = (uint8_t)(CPM_BDOS_BODY >> 8);

  // FFF3: IN A,(80h)
  RAM[CPM_HPKEY_SHIM + 0] = 0xDB;
  RAM[CPM_HPKEY_SHIM + 1] = 0x80;
  // FFF5: LD B,A
  RAM[CPM_HPKEY_SHIM + 2] = 0x47;
  // FFF6: AND 7Fh
  RAM[CPM_HPKEY_SHIM + 3] = 0xE6;
  RAM[CPM_HPKEY_SHIM + 4] = 0x7F;
  // FFF8: POP HL / RET
  RAM[CPM_HPKEY_SHIM + 5] = 0xE1;
  RAM[CPM_HPKEY_SHIM + 6] = 0xC9;
}

inline void cpmWarmBootPatchBdosEntry()
{
  // Preserve the standard page-zero vector (0005 -> D806). Only replace the
  // original D806 "JP D811" with "JP FFEB".
  RAM[CPM_BDOS_ENTRY + 0] = 0xC3;
  RAM[CPM_BDOS_ENTRY + 1] = (uint8_t)(CPM_BDOS_SHIM & 0xFF);
  RAM[CPM_BDOS_ENTRY + 2] = (uint8_t)(CPM_BDOS_SHIM >> 8);
}

inline bool cpmHpConinLayoutMatches()
{
  return RAM[CPM_CONIN_A_READ + 0] == 0xDB &&
         RAM[CPM_CONIN_A_READ + 1] == 0x80 &&
         RAM[CPM_CONIN_A_READ + 2] == 0xE1 &&
         RAM[CPM_CONIN_A_READ + 3] == 0xC9 &&
         RAM[CPM_CONIN_B_READ + 0] == 0xDB &&
         RAM[CPM_CONIN_B_READ + 1] == 0x80 &&
         RAM[CPM_CONIN_B_READ + 2] == 0xE1 &&
         RAM[CPM_CONIN_B_READ + 3] == 0xC9;
}

inline void cpmPatchHpConinCompat()
{
  // Replace IN A,(80h) + POP HL with JP FFF3.  The shim performs the original
  // input, supplies HP's B-register keycode convention, pops HL and returns;
  // therefore the old RET byte immediately following each patched site is
  // deliberately never executed.
  RAM[CPM_CONIN_A_READ + 0] = 0xC3;
  RAM[CPM_CONIN_A_READ + 1] = (uint8_t)(CPM_HPKEY_SHIM & 0xFF);
  RAM[CPM_CONIN_A_READ + 2] = (uint8_t)(CPM_HPKEY_SHIM >> 8);

  RAM[CPM_CONIN_B_READ + 0] = 0xC3;
  RAM[CPM_CONIN_B_READ + 1] = (uint8_t)(CPM_HPKEY_SHIM & 0xFF);
  RAM[CPM_CONIN_B_READ + 2] = (uint8_t)(CPM_HPKEY_SHIM >> 8);
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
  cpmHpConinCompatActive = false;

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

  const bool hpConinLayoutOk = cpmHpConinLayoutMatches();
  cpmWarmBootWriteShims();
  cpmWarmBootPatchBdosEntry();

  if (hpConinLayoutOk) {
    cpmPatchHpConinCompat();
    cpmHpConinCompatActive = true;
  }

  cpmResidentShadowValid = true;

  Serial.printf("CP/M warm boot shadow: captured %u bytes D000-E5FF\n",
                (unsigned)CPM_SHADOW_SIZE);
  Serial.println("CP/M warm boot: BDOS base preserved at D806; function-0 shim active");
  if (cpmHpConinCompatActive)
    Serial.println("CP/M console: HP 82900 CONIN keycode compatibility active");
  else
    Serial.println("CP/M console: native CONIN layout changed; HP keycode compatibility disabled");
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
  // its normal JP D806 BDOS vector. The BIOS itself is outside the restored
  // range, but rewrite the shims/CONIN patch as a cheap consistency safeguard.
  memcpy(&RAM[CPM_SHADOW_START], cpmResidentShadow, CPM_SHADOW_SIZE);
  cpmWarmBootWriteShims();
  cpmWarmBootPatchBdosEntry();
  if (cpmHpConinCompatActive) cpmPatchHpConinCompat();
  ++cpmWarmBootCount;

  BP = CPM_WBOOT_ENTRY;
  bpOn = true;

  Serial.printf("CP/M warm boot: restored D000-E5FF (count=%lu)\n",
                (unsigned long)cpmWarmBootCount);
  return true;
}
