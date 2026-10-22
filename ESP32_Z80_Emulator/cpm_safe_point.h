#pragma once
#include <atomic>
#include <stdint.h>
#include <string.h>

// CP/M 2.2 safety gate: verified on the checked-in cpm22.lst.
//
// The user is at a real CCP console prompt ONLY after the native CCP has
// reached the interactive BDOS function-10 entry at D19C.  An application
// may have overwritten the CCP; reject requests unless the ROM signatures
// are intact and cpmWarmBootCapture() has recognized the native image.
//
// This first milestone never calls BDOS or changes a mounted drive. A
// benign, non-BDOS Z80 probe can run at the NEXT D382 prompt boundary,
// provided the previous input line was EMPTY. No surprise queued execution
// following a command, and no Z80 register/memory access from the web task.
namespace CpmSafePoint {
enum Mode : uint8_t { UNKNOWN=0, BUSY=1, CCP_INPUT=2, PROBE_RUNNING=3 };
enum Result : uint8_t { NEVER=0, WAIT_EMPTY_LINE=1, RUNNING=2,
                        PASSED=3, INPUT_NOT_EMPTY=4, FAILED=5,
                        INVALIDATED=6 };
static constexpr uint16_t kCcpPrompt = 0xD382;
static constexpr uint16_t kCcpCommand = 0xD398;
static constexpr uint16_t kCcpConsoleInput = 0xD19C;
static constexpr uint16_t kCcpInputReturn = 0xD1A4;
static constexpr uint16_t kCcpInputLength = 0xD007;

// Cheap, no memory allocation, no FreeRTOS work, no hardware communication.
class Gate {
public:
  std::atomic<uint8_t> mode{UNKNOWN};
  std::atomic<uint8_t> result{NEVER};
  std::atomic<bool> compatible{false};
  // Only Z80 CPU task changes this flag. It is consumed at the next CCP entry.
  bool blankInputReturned=false;

  static bool signature(const uint8_t *ram) {
    return ram &&
       ram[0xD382]==0x31 && ram[0xD383]==0xAB && ram[0xD384]==0xD7 &&
       ram[0xD395]==0xCD && ram[0xD396]==0x39 && ram[0xD397]==0xD1 &&
       ram[0xD398]==0x11 && ram[0xD399]==0x80 && ram[0xD39A]==0x00 &&
       ram[0xD19C]==0x0E && ram[0xD19D]==0x0A &&
       ram[0xD1A1]==0xCD && ram[0xD1A2]==0x05 && ram[0xD1A3]==0x00 &&
       ram[0xD1A4]==0xCD && ram[0xD1A5]==0x29 && ram[0xD1A6]==0xD1;
  }

  void reset(const uint8_t *ram,bool nativeCpm) {
    blankInputReturned=false;
    mode.store(UNKNOWN,std::memory_order_release);
    result.store(NEVER,std::memory_order_release);
    compatible.store(nativeCpm && signature(ram),std::memory_order_release);
  }

  void onWarmBoot() {
    blankInputReturned=false;
    mode.store(BUSY,std::memory_order_release);
    if(result.load(std::memory_order_acquire)==WAIT_EMPTY_LINE)
      result.store(INVALIDATED,std::memory_order_release);
  }

  // Called only on these exact four Z80 instruction addresses, by CPU task.
  // Return true to trap just before executing the original CCP D382 opcode.
  bool fetch(uint16_t pc,const uint8_t *ram) {
    if(!compatible.load(std::memory_order_acquire))return false;
    if(pc==kCcpConsoleInput) {
      mode.store(CCP_INPUT,std::memory_order_release);
      return false;
    }
    if(pc==kCcpInputReturn) {
      mode.store(BUSY,std::memory_order_release);
      blankInputReturned=false;
      if(result.load(std::memory_order_acquire)==WAIT_EMPTY_LINE) {
        if(ram[kCcpInputLength]==0)blankInputReturned=true;
        else result.store(INPUT_NOT_EMPTY,std::memory_order_release);
      }
      return false;
    }
    if(pc==kCcpCommand) {
      mode.store(BUSY,std::memory_order_release);
      // A command is underway, not a verified empty-line safe point.
      blankInputReturned=false;
      return false;
    }
    if(pc==kCcpPrompt) {
      mode.store(BUSY,std::memory_order_release);
      if(blankInputReturned &&
         result.load(std::memory_order_acquire)==WAIT_EMPTY_LINE) {
        blankInputReturned=false;
        result.store(RUNNING,std::memory_order_release);
        mode.store(PROBE_RUNNING,std::memory_order_release);
        return true;
      }
      blankInputReturned=false;
    }
    return false;
  }

  bool request() {
    if(!compatible.load(std::memory_order_acquire) ||
       mode.load(std::memory_order_acquire)!=CCP_INPUT)return false;
    // Only one request can be pending. A completed one may be re-armed.
    uint8_t previous=result.load(std::memory_order_acquire);
    while(previous!=WAIT_EMPTY_LINE && previous!=RUNNING) {
      if(result.compare_exchange_weak(previous,WAIT_EMPTY_LINE,
                                      std::memory_order_acq_rel))return true;
    }
    return false;
  }

  void complete(bool passed) {
    // The original CCP instruction will now execute; it must reach the
    // console input marker before a new management operation is accepted.
    mode.store(BUSY,std::memory_order_release);
    result.store(passed?PASSED:FAILED,std::memory_order_release);
  }
};
inline const char *modeText(uint8_t m) {
  switch(m) {
    case BUSY:return "busy";
    case CCP_INPUT:return "ccp_input";
    case PROBE_RUNNING:return "probe_running";
    default:return "unknown";
  }
}
inline const char *resultText(uint8_t r) {
  switch(r) {
    case WAIT_EMPTY_LINE:return "waiting_for_empty_return";
    case RUNNING:return "running";
    case PASSED:return "passed";
    case INPUT_NOT_EMPTY:return "cancelled_nonempty_command";
    case FAILED:return "failed";
    case INVALIDATED:return "cancelled_warm_boot";
    default:return "never";
  }
}
} // namespace CpmSafePoint
