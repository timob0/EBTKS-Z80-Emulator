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
// Stage A supports a benign non-BDOS probe; Stage B1 adds an explicitly
// read-only BDOS directory search at that same D382 prompt boundary,
// provided the previous input line was EMPTY. No surprise queued execution
// following a command, and no Z80 register/memory access from the web task.
namespace CpmSafePoint {
enum Mode : uint8_t { UNKNOWN=0, BUSY=1, CCP_INPUT=2, PROBE_RUNNING=3 };
enum Result : uint8_t { NEVER=0, WAIT_EMPTY_LINE=1, RUNNING=2,
                        PASSED=3, INPUT_NOT_EMPTY=4, FAILED=5,
                        INVALIDATED=6 };
enum Operation : uint8_t { PROBE=0, DIRECTORY=1 };
static constexpr uint8_t kDirectoryMax=32;
// Directory entries are raw CP/M directory extents, not logical files/sizes.
struct DirectoryEntry {
  char name[13]={};
  uint8_t user=0;
  uint8_t extent=0;
  uint8_t records=0;
};
struct Directory {
  uint8_t count=0;
  bool truncated=false;
  char drive='?';
  DirectoryEntry entries[kDirectoryMax]{};
};
static constexpr uint16_t kCcpPrompt = 0xD382;
static constexpr uint16_t kCcpCommand = 0xD398;
static constexpr uint16_t kCcpConsoleInput = 0xD19C;
static constexpr uint16_t kCcpInputReturn = 0xD1A4;
static constexpr uint16_t kCcpInputLength = 0xD007;
// CP/M 2.2's native BDOS buffered-input loop, observed before PUSH BC.
// At this PC, register B holds the true number of characters on the line.
static constexpr uint16_t kBdosLineLoop = 0xD9EF;
static constexpr uint16_t kBiosConinPollA = 0xE826;
static constexpr uint16_t kBiosConinPollB = 0xE831;

// Cheap, no memory allocation, no FreeRTOS work, no hardware communication.
class Gate {
public:
  std::atomic<uint8_t> mode{UNKNOWN};
  std::atomic<uint8_t> result{NEVER};
  std::atomic<bool> compatible{false};
  std::atomic<uint8_t> operation{PROBE};
  // 0=current drive, 1..16=explicit A:..P: FCB drive selector.
  std::atomic<uint8_t> requestedDrive{0};
  std::atomic<bool> ccpLineKnown{false};
  std::atomic<bool> ccpLineEmpty{false};
  bool autoReturnSent=false; // CPU-only, one synthetic CR per request
  bool biosConinPoll=false; // CPU-only: last fetched opcode is BIOS CONIN IN A,(85h)
  // Only successful native CCP safe-point requests can consume this gate.
  std::atomic_flag requestBusy=ATOMIC_FLAG_INIT;
  std::atomic_flag directoryBusy=ATOMIC_FLAG_INIT;
  Directory directory{};
  void publishDirectory(const Directory &snapshot) {
    while(directoryBusy.test_and_set(std::memory_order_acquire)) {}
    directory=snapshot;
    directoryBusy.clear(std::memory_order_release);
  }
  bool copyDirectory(Directory &out) {
    // Never block an AsyncTCP callback behind an active Z80 operation.
    if(directoryBusy.test_and_set(std::memory_order_acquire))return false;
    out=directory;
    directoryBusy.clear(std::memory_order_release);
    return true;
  }
  // Generation changes at BOTH entry to and exit from the input wait.
  // A request racing with a submitted command cannot remain armed.
  std::atomic<uint32_t> inputGeneration{0};
  // Only Z80 CPU task changes this flag. It is consumed at the next CCP entry.
  bool blankInputReturned=false;
  bool insideVerifiedCcpPrompt=false; // CPU-only; set by D382 before D19C

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
    insideVerifiedCcpPrompt=false;
    ccpLineKnown.store(false,std::memory_order_release);
    ccpLineEmpty.store(false,std::memory_order_release);
    autoReturnSent=false;
    biosConinPoll=false;
    inputGeneration.fetch_add(1,std::memory_order_acq_rel);
    mode.store(UNKNOWN,std::memory_order_release);
    result.store(NEVER,std::memory_order_release);
    compatible.store(nativeCpm && signature(ram),std::memory_order_release);
  }

  void onWarmBoot() {
    blankInputReturned=false;
    insideVerifiedCcpPrompt=false;
    ccpLineKnown.store(false,std::memory_order_release);
    ccpLineEmpty.store(false,std::memory_order_release);
    autoReturnSent=false;
    biosConinPoll=false;
    mode.store(BUSY,std::memory_order_release);
    inputGeneration.fetch_add(1,std::memory_order_acq_rel);
    if(result.load(std::memory_order_acquire)==WAIT_EMPTY_LINE)
      result.store(INVALIDATED,std::memory_order_release);
  }

  // Called from the CPU fetch callback at native BDOS's RDBUF1 entry.
  // Never infer an empty line merely from the 0080 DMA or console ring.
  void observeFetchedOpcode(uint16_t pc) {
    biosConinPoll=(pc==kBiosConinPollA || pc==kBiosConinPollB);
  }

  void observeLineCount(uint8_t count) {
    if(mode.load(std::memory_order_acquire)!=CCP_INPUT)return;
    ccpLineEmpty.store(count==0,std::memory_order_release);
    ccpLineKnown.store(true,std::memory_order_release);
    // Never leave an automatic directory request queued behind a typed line.
    // A pending request is cancelled as soon as BDOS observes characters.
    if(count!=0 && operation.load(std::memory_order_acquire)==DIRECTORY) {
      uint8_t expected=WAIT_EMPTY_LINE;
      result.compare_exchange_strong(expected,INPUT_NOT_EMPTY,
                                     std::memory_order_acq_rel);
    }
  }

  // Called ONLY from the Z80 CPU UART status read, after checking physical
  // keyboard queue, virtual terminal ring, and UART receive latch are empty.
  // The synthetic CR completes the EXISTING BDOS 10; only then does the
  // proven D382 safe point execute BDOS directory search (never reentrant).
  bool takeAutomaticReturn() {
    if(autoReturnSent || !biosConinPoll || !insideVerifiedCcpPrompt ||
       mode.load(std::memory_order_acquire)!=CCP_INPUT ||
       result.load(std::memory_order_acquire)!=WAIT_EMPTY_LINE ||
       operation.load(std::memory_order_acquire)!=DIRECTORY ||
       !ccpLineKnown.load(std::memory_order_acquire) ||
       !ccpLineEmpty.load(std::memory_order_acquire))return false;
    autoReturnSent=true;
    return true;
  }

  // Called at native CCP/BDOS instruction addresses by the CPU task.
  // Return true to trap just before executing the original CCP D382 opcode.
  bool fetch(uint16_t pc,const uint8_t *ram) {
    if(!compatible.load(std::memory_order_acquire))return false;
    if(pc==kCcpConsoleInput) {
      // Reject a direct jump into the CCP input helper from an application.
      // Only the normal CCP D382 prompt loop can authorize this state.
      if(insideVerifiedCcpPrompt) {
        ccpLineKnown.store(false,std::memory_order_release);
        ccpLineEmpty.store(false,std::memory_order_release);
        autoReturnSent=false;
        inputGeneration.fetch_add(1,std::memory_order_acq_rel);
        mode.store(CCP_INPUT,std::memory_order_release);
      }
      return false;
    }
    if(pc==kCcpInputReturn) {
      insideVerifiedCcpPrompt=false;
      ccpLineKnown.store(false,std::memory_order_release);
      ccpLineEmpty.store(false,std::memory_order_release);
      mode.store(BUSY,std::memory_order_release);
      inputGeneration.fetch_add(1,std::memory_order_acq_rel);
      blankInputReturned=false;
      if(result.load(std::memory_order_acquire)==WAIT_EMPTY_LINE) {
        if(ram[kCcpInputLength]==0)blankInputReturned=true;
        else result.store(INPUT_NOT_EMPTY,std::memory_order_release);
      }
      return false;
    }
    if(pc==kCcpCommand) {
      mode.store(BUSY,std::memory_order_release);
      // Even an empty line passes CMMND2 before its CCP prompt re-entry.
      // Retain the one-shot marker ONLY for a verified blank input.
      if(result.load(std::memory_order_acquire)!=WAIT_EMPTY_LINE)
        blankInputReturned=false;
      return false;
    }
    if(pc==kCcpPrompt) {
      insideVerifiedCcpPrompt=true;
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

  bool request(Operation requested=PROBE,uint8_t drive=0) {
    if(requestBusy.test_and_set(std::memory_order_acquire))return false;
    const uint32_t generation=inputGeneration.load(std::memory_order_acquire);
    if(!compatible.load(std::memory_order_acquire) ||
       mode.load(std::memory_order_acquire)!=CCP_INPUT) {
      requestBusy.clear(std::memory_order_release);
      return false;
    }
    // Never overwrite the operation belonging to a pending request.
    uint8_t previous=result.load(std::memory_order_acquire);
    if(previous==WAIT_EMPTY_LINE || previous==RUNNING) {
      requestBusy.clear(std::memory_order_release);
      return false;
    }
    if(drive>16 || (requested==DIRECTORY &&
       ccpLineKnown.load(std::memory_order_acquire) &&
       !ccpLineEmpty.load(std::memory_order_acquire))) {
      requestBusy.clear(std::memory_order_release);
      return false;
    }
    requestedDrive.store(drive,std::memory_order_release);
    operation.store(requested,std::memory_order_release);
    // Only one request can be pending. A completed one may be re-armed.
    while(previous!=WAIT_EMPTY_LINE && previous!=RUNNING) {
      if(result.compare_exchange_weak(previous,WAIT_EMPTY_LINE,
                                      std::memory_order_acq_rel)) {
        // The guest may have submitted a command between checking IDLE and
        // publishing this request. Abort rather than queue behind a program.
        if(inputGeneration.load(std::memory_order_acquire)!=generation ||
           mode.load(std::memory_order_acquire)!=CCP_INPUT) {
          uint8_t waiting=WAIT_EMPTY_LINE;
          result.compare_exchange_strong(waiting,INPUT_NOT_EMPTY,
                                         std::memory_order_acq_rel);
          requestBusy.clear(std::memory_order_release);
          return false;
        }
        requestBusy.clear(std::memory_order_release);
        return true;
      }
    }
    requestBusy.clear(std::memory_order_release);
    return false;
  }

  // Safe for the HP mailbox timeout handler: a request which has NOT
  // entered BDOS may be cancelled, but an actively running BDOS call is
  // never interrupted. Also stops any later blank line from triggering it.
  bool cancelPending() {
    uint8_t expected=WAIT_EMPTY_LINE;
    return result.compare_exchange_strong(expected,INPUT_NOT_EMPTY,
                                          std::memory_order_acq_rel);
  }

  void complete(bool passed) {
    // The original CCP instruction will now execute; it must reach the
    // console input marker before a new management operation is accepted.
    mode.store(BUSY,std::memory_order_release);
    result.store(passed?PASSED:FAILED,std::memory_order_release);
  }
};
inline const char *operationText(uint8_t o) {
  return o==DIRECTORY?"directory":"probe";
}
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
