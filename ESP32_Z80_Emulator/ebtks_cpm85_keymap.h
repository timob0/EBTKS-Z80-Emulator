#pragma once
#include <stddef.h>
#include <stdint.h>

// Physical HP-85A/B KEYHIT -> CP/M console input. Codes below are OCTAL;
// see the EBTKS "Octal Keycodes for Special Keys on HP 85 A/B Keyboard" table.
// Ordinary Ctrl characters (0..31), printable ASCII, END LINE and Backspace
// retain the exact user-hardware-tested mappings. As CP/M-only convenience
// keys, CLEAR (0222 octal) sends ESC and SCRATCH (0255 octal) sends TAB.
// -LINE (0240 octal) stays unmapped; these keys retain their HP BASIC
// behavior whenever the CPM85 keyboard hook is detached.
//
// All special key input uses 7-bit ANSI/VT100-compatible sequences. The
// original VT100 defined normal (ESC [ A/B/C/D) and application cursor
// (ESC O A/B/C/D) modes. Home/Insert/Delete/Page/Fn are common xterm-style
// additions; applications must be configured to expect this input dialect.
// HP K8 (0207) is always intercepted on the HP-85 and NEVER sent to CP/M.
namespace Cpm85Keymap {
static constexpr uint8_t kMaxInputBytes=5;
struct InputBytes {
  uint8_t len;
  uint8_t bytes[kMaxInputBytes];
};

// Preserved single-byte path, including NUL (Ctrl+@); -1 means that the
// key needs an escape sequence or is an unmapped HP control key.
constexpr int toConsoleByte(uint8_t hp) {
  return (hp == 0231 || hp == 0233 || hp == 0177) ? 0x08
       : (hp == 0232) ? 0x0D
       : (hp == 0222) ? 0x1B  // CLEAR / clear line -> ESC
       : (hp == 0255) ? 0x09  // SCRATCH -> TAB
       : (hp <= 0176) ? hp
       : -1;
}

inline InputBytes toInputBytes(uint8_t hp,bool appCursor=false) {
  const int plain=toConsoleByte(hp);
  if(plain>=0)return {1,{static_cast<uint8_t>(plain)}};
  const uint8_t esc=0x1B;
  switch(hp) {
    // HP-85 physical cursor keys, normal/application VT100 mode.
    case 0241: return {3,{esc,static_cast<uint8_t>(appCursor?'O':'['),'A'}}; // UP
    case 0242: return {3,{esc,static_cast<uint8_t>(appCursor?'O':'['),'B'}}; // DOWN
    case 0235: return {3,{esc,static_cast<uint8_t>(appCursor?'O':'['),'C'}}; // RIGHT
    case 0234: return {3,{esc,static_cast<uint8_t>(appCursor?'O':'['),'D'}}; // LEFT
    // Widely-supported ANSI/xterm input keys, not original VT100 hardware.
    case 0245: return {3,{esc,static_cast<uint8_t>(appCursor?'O':'['),'H'}}; // HOME
    case 0243: return {4,{esc,'[','2','~'}}; // INS/RPL -> Insert
    case 0244: return {4,{esc,'[','3','~'}}; // -CHR -> Delete character
    case 0236: return {4,{esc,'[','5','~'}}; // ROLL UP -> Page Up
    case 0237: return {4,{esc,'[','6','~'}}; // ROLL DN -> Page Down
    // HP softkeys K1..K7. K8 is RESERVED for physical detach.
    case 0200: return {3,{esc,'O','P'}}; // F1
    case 0201: return {3,{esc,'O','Q'}}; // F2
    case 0202: return {3,{esc,'O','R'}}; // F3
    case 0203: return {3,{esc,'O','S'}}; // F4
    case 0204: return {5,{esc,'[','1','5','~'}}; // F5
    case 0205: return {5,{esc,'[','1','7','~'}}; // F6
    case 0206: return {5,{esc,'[','1','8','~'}}; // F7
    default:   return {0,{0}}; // HP system keys, -LINE, COPY etc ignored.
  }
}

// Small, passive DECCKM tracker for outgoing Z80 UART bytes. ANSI applications
// switch to ESC O <arrow> by sending CSI ? 1 h and restore ESC [ <arrow>
// with CSI ? 1 l. CSI parameters may be split across console output chunks.
// No TFT dependency, heap, I/O, or changes to console output bytes.
class CursorModeTracker {
  enum State : uint8_t {NORMAL, AFTER_ESC, CSI, PRIVATE_CSI};
  State state_=NORMAL;
  bool application_=false;
  bool hasNum_=false, sawMode1_=false;
  uint16_t num_=0;
  void endParam() {
    if(hasNum_ && num_==1)sawMode1_=true;
    num_=0; hasNum_=false;
  }
public:
  bool applicationCursor()const{return application_;}
  void feedByte(uint8_t c) {
    if(c==0x1b) {state_=AFTER_ESC;return;}
    switch(state_) {
      case NORMAL: break;
      case AFTER_ESC:
        if(c=='[')state_=CSI;
        else {if(c=='c')application_=false;state_=NORMAL;} // VT100 RIS
        break;
      case CSI:
        if(c=='?') {
          state_=PRIVATE_CSI;hasNum_=sawMode1_=false;num_=0;
        } else if(c>=0x40 && c<=0x7e)state_=NORMAL;
        // Ignore all other CSI parameters; no private mode requested.
        break;
      case PRIVATE_CSI:
        if(c>='0' && c<='9') {
          hasNum_=true;
          num_=(num_<1000)?static_cast<uint16_t>(num_*10+c-'0'):10000;
        } else if(c==';')endParam();
        else if(c>=0x40 && c<=0x7e) {
          endParam();
          if(sawMode1_ && (c=='h'||c=='l'))application_=(c=='h');
          state_=NORMAL;
        } else if(c<0x20)state_=NORMAL;
        break;
    }
  }
  void feed(const uint8_t* bytes,size_t n) {
    for(size_t i=0;i<n;++i)feedByte(bytes[i]);
  }
};
} // namespace Cpm85Keymap
