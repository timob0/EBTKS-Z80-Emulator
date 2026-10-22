// Passive VT100/ANSI terminal-capability and cursor-state tracker.
//
// The Web terminal used to reply to DA/DSR directly in JavaScript, so an
// unattended physical TFT could never answer an ANSI-detect probe from a BBS.
// The console router now owns one responder for the *shared* 80x24 CP/M
// console. No framebuffer, Arduino allocation, task or network I/O here.
// An external Telnet client may answer itself: the router suppresses replies
// while such a client is connected to avoid duplicate input.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace EbtksAnsiProbe {
class Terminal {
  enum State : uint8_t { NORMAL, ESC, CSI, CHARSET, HASH, SOROC_ROW,
                         SOROC_COL, HP_AMP };
  State state_ = NORMAL;
  uint8_t row_ = 0, col_ = 0, savedRow_ = 0, savedCol_ = 0;
  uint8_t scrollTop_ = 0, scrollBottom_ = 23, sorocRow_ = 0;
  uint8_t paramsLen_ = 0;
  char params_[48] = {};
  bool origin_ = false, autoWrap_ = true, vtDialect_ = false;

  static int clamp(int n, int lo, int hi) {
    return n < lo ? lo : (n > hi ? hi : n);
  }
  void lineFeed() {
    // Scrolling does not change the cursor's screen-relative row.
    if (row_ != scrollBottom_ || row_ < scrollTop_)
      row_ = (uint8_t)clamp(row_ + 1, 0, 23);
  }
  void reverseIndex() {
    if (row_ != scrollTop_)
      row_ = (uint8_t)clamp(row_ - 1, 0, 23);
  }
  void print() {
    if (col_ >= 80) {
      if (autoWrap_) { col_ = 0; lineFeed(); }
      else col_ = 79;
    }
    if (col_ < 80) ++col_;
  }
  int param(int ordinal, int fallback) const {
    int index = 0, value = 0;
    bool has = false;
    for (int i = 0; i <= paramsLen_; ++i) {
      const char c = i < paramsLen_ ? params_[i] : ';';
      if (c == ';') {
        if (index == ordinal) return has ? value : fallback;
        ++index; value = 0; has = false;
      } else if (c >= '0' && c <= '9') {
        value = clamp(value * 10 + c - '0', 0, 999);
        has = true;
      }
    }
    return fallback;
  }
  template <class Emit> void csi(uint8_t finalByte, Emit emit) {
    const bool priv = paramsLen_ && params_[0] == '?';
    const bool secondary = paramsLen_ && params_[0] == '>';
    // DA and DSR replies travel back through the guest CON: keyboard input,
    // so MODEM.COM can forward them to the BBS over its 8250 serial port.
    if (finalByte == 'c' && !priv && !secondary) {
      emit("\x1b[?1;0c", 7);
      return;
    }
    if (finalByte == 'n' && !priv && !secondary) {
      if (param(0, 0) == 5) emit("\x1b[0n", 4);
      else if (param(0, 0) == 6) {
        char reply[18];
        const int n = snprintf(reply, sizeof(reply), "\x1b[%u;%uR",
                               (unsigned)row_ + 1,
                               (unsigned)clamp(col_, 0, 79) + 1);
        if (n > 0 && n < (int)sizeof(reply)) emit(reply, (size_t)n);
      }
      return;
    }
    if (priv) {
      if (finalByte == 'h' || finalByte == 'l') {
        const bool on = finalByte == 'h';
        int option = 0;
        for (int i = 1; i <= paramsLen_; ++i) {
          const char ch = i < paramsLen_ ? params_[i] : ';';
          if (ch == ';') {
            if (option == 6) {
              origin_ = on;
              row_ = on ? scrollTop_ : 0; col_ = 0;
            }
            if (option == 7) autoWrap_ = on;
            option = 0;
          } else if (ch >= '0' && ch <= '9') {
            option = clamp(option * 10 + ch - '0', 0, 9999);
          }
        }
      }
      return;
    }
    if (secondary) return;
    int n = param(0, 1);
    if (n == 0) n = 1;
    const int lo = origin_ ? scrollTop_ : 0;
    const int hi = origin_ ? scrollBottom_ : 23;
    switch (finalByte) {
      case 'A': row_ = (uint8_t)clamp(row_ - n, lo, hi); break;
      case 'B': row_ = (uint8_t)clamp(row_ + n, lo, hi); break;
      case 'C': col_ = (uint8_t)clamp(col_ + n, 0, 79); break;
      case 'D': col_ = (uint8_t)clamp(col_ - n, 0, 79); break;
      case 'E': row_ = (uint8_t)clamp(row_ + n, lo, hi); col_ = 0; break;
      case 'F': row_ = (uint8_t)clamp(row_ - n, lo, hi); col_ = 0; break;
      case 'G': case '`': col_ = (uint8_t)clamp(n - 1, 0, 79); break;
      case 'd': row_ = (uint8_t)clamp(n - 1 + (origin_ ? scrollTop_ : 0), lo, hi); break;
      case 'H': case 'f':
        row_ = (uint8_t)clamp(n - 1 + (origin_ ? scrollTop_ : 0), lo, hi);
        col_ = (uint8_t)clamp(param(1, 1) - 1, 0, 79); break;
      case 'r': {
        int top = clamp(param(0, 1) - 1, 0, 23);
        int bottom = clamp(param(1, 24) - 1, 0, 23);
        if (top >= bottom) { top = 0; bottom = 23; }
        scrollTop_ = (uint8_t)top; scrollBottom_ = (uint8_t)bottom;
        row_ = origin_ ? scrollTop_ : 0; col_ = 0;
        break;
      }
      case 's': savedRow_ = row_; savedCol_ = col_; break;
      case 'u': row_ = savedRow_; col_ = savedCol_; break;
      default: break; // SGR, erase, insertion etc. do not move the cursor.
    }
  }
 public:
  uint8_t row() const { return row_; }
  uint8_t col() const { return col_; }
  void reset() {
    row_ = col_ = savedRow_ = savedCol_ = scrollTop_ = 0;
    scrollBottom_ = 23;
    state_ = NORMAL; paramsLen_ = 0;
    origin_ = false; autoWrap_ = true; vtDialect_ = false;
  }
  // emit(bytes, length): receives complete short reply. It must enqueue
  // atomically or drop the full reply if the guest input buffer is full.
  template <class Emit> void feed(const uint8_t *bytes, size_t count, Emit emit) {
    for (size_t i = 0; i < count; ++i) {
      const uint8_t c = bytes[i];
      switch (state_) {
        case CSI:
          if (c >= 0x40 && c <= 0x7e) {
            csi(c, emit);
            state_ = NORMAL; paramsLen_ = 0;
          } else if (c == 27) {
            state_ = ESC; paramsLen_ = 0;
          } else if (paramsLen_ < sizeof(params_) - 1) {
            params_[paramsLen_++] = (char)c;
          } else { state_ = NORMAL; paramsLen_ = 0; }
          break;
        case CHARSET: case HASH:
          state_ = NORMAL; break;
        case SOROC_ROW:
          sorocRow_ = (uint8_t)clamp((int)c - 32, 0, 23);
          state_ = SOROC_COL; break;
        case SOROC_COL:
          row_ = sorocRow_; col_ = (uint8_t)clamp((int)c - 32, 0, 79);
          state_ = NORMAL; break;
        case HP_AMP:
          if (c >= 'A' && c <= 'Z') state_ = NORMAL;
          break;
        case ESC:
          state_ = NORMAL;
          switch (c) {
            case '[': state_ = CSI; paramsLen_ = 0; vtDialect_ = true; break;
            case 'Z': emit("\x1b[?1;0c", 7); break; // VT100 DECID
            case '(': case ')': state_ = CHARSET; break;
            case '#': state_ = HASH; break;
            case '=': state_ = SOROC_ROW; break;
            case '&': state_ = HP_AMP; break;
            case '7': savedRow_ = row_; savedCol_ = col_; break;
            case '8': row_ = savedRow_; col_ = savedCol_; break;
            case 'c': reset(); vtDialect_ = true; break;
            case 'D': if (vtDialect_) lineFeed();
                      else col_ = (uint8_t)clamp(col_ - 1, 0, 79); break;
            case 'E': if (vtDialect_) { lineFeed(); col_ = 0; }
                      else reset(); break;
            case 'M': reverseIndex(); vtDialect_ = true; break;
            case 'H': case 'h': row_ = col_ = 0; break;
            case '*': reset(); break;
            case 'A': row_ = (uint8_t)clamp(row_ - 1, 0, 23); break;
            case 'B': row_ = (uint8_t)clamp(row_ + 1, 0, 23); break;
            case 'C': col_ = (uint8_t)clamp(col_ + 1, 0, 79); break;
            default: break;
          }
          break;
        case NORMAL:
          switch (c) {
            case 27: state_ = ESC; break;
            case 13: col_ = 0; break;
            case 10: lineFeed(); break;
            case 8: if (col_) --col_; break;
            case 9: col_ = (uint8_t)clamp(((int)col_ / 8 + 1) * 8, 0, 80); break;
            case 12: case 26: reset(); break;
            default: if (c >= 32 && c != 127) print(); break;
          }
          break;
      }
    }
  }
};
} // namespace EbtksAnsiProbe
