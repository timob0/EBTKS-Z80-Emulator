// Native HP-85A/B -> VT100/ANSI CP/M keyboard regression, no ESP32 headers.
// g++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic
//     tests/cpm85_keymap_native.cpp -o /tmp/cpm85_keymap_native
#include "../ESP32_Z80_Emulator/ebtks_cpm85_keymap.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using Cpm85Keymap::InputBytes;
using Cpm85Keymap::toConsoleByte;
using Cpm85Keymap::toInputBytes;
using Cpm85Keymap::CursorModeTracker;

static void expect(uint8_t key,bool app,const char* sequence) {
  const InputBytes b=toInputBytes(key,app);
  assert(b.len==std::strlen(sequence));
  assert(std::memcmp(b.bytes,sequence,b.len)==0);
}
static void expectIgnored(uint8_t key) {
  assert(toConsoleByte(key)==-1);
  assert(toInputBytes(key,false).len==0);
  assert(toInputBytes(key,true).len==0);
}
static void output(CursorModeTracker& m,const char* str) {
  m.feed(reinterpret_cast<const uint8_t*>(str),std::strlen(str));
}
int main() {
  // Every CTRL key (0..31) and printable ASCII byte is unchanged. NUL is a
  // *valid key byte*, not the unmapped sentinel; K8 must remain local.
  for(int key=0;key<127;++key) {
    const InputBytes a=toInputBytes(static_cast<uint8_t>(key));
    assert(toConsoleByte(static_cast<uint8_t>(key))==key);
    assert(a.len==1 && a.bytes[0]==key);
  }
  assert(toConsoleByte(0)==0);
  assert(toConsoleByte(3)==3);   // CTRL+C
  assert(toConsoleByte(8)==8);   // CTRL+H
  assert(toConsoleByte(17)==17); // CTRL+Q
  assert(toConsoleByte(19)==19); // CTRL+S
  assert(toConsoleByte(26)==26); // CTRL+Z
  assert(toConsoleByte(27)==27); // CTRL+[
  for(bool app : {false,true}) {
    expect(0177,app,"\x08"); // ASCII DEL
    expect(0231,app,"\x08"); // HP BACK SPACE
    expect(0233,app,"\x08"); // HP FAST BACK SPACE
    expect(0232,app,"\x0d"); // HP END LINE
    expect(0240,app,"\x1b"); // HP LINE (unshifted) -> ESC
    expect(0217,app,"\x09"); // HP CONT (unshifted) -> TAB
  }

  // Normal mode: VT100 ANSI cursor keys; optional application cursor mode
  // is SS3 and is selected by the CP/M application's own DECCKM commands.
  expect(0241,false,"\x1b[A"); expect(0242,false,"\x1b[B");
  expect(0235,false,"\x1b[C"); expect(0234,false,"\x1b[D");
  expect(0241,true, "\x1bOA"); expect(0242,true, "\x1bOB");
  expect(0235,true, "\x1bOC"); expect(0234,true, "\x1bOD");
  // Home/Insert/Delete/Page are conventional ANSI/xterm input extensions.
  expect(0245,false,"\x1b[H"); expect(0245,true,"\x1bOH");
  expect(0243,false,"\x1b[2~"); // INS/RPL
  expect(0244,false,"\x1b[3~"); // -CHR
  expect(0236,false,"\x1b[5~"); // ROLL UP -> PgUp
  expect(0237,false,"\x1b[6~"); // ROLL DN -> PgDn
  for(uint8_t k : {0243,0244,0236,0237}) {
    const InputBytes a=toInputBytes(k,false),b=toInputBytes(k,true);
    assert(a.len==b.len && std::memcmp(a.bytes,b.bytes,a.len)==0);
  }
  // Physical K1..K7 map to F1..F7, K8 (0207) is the detach key.
  expect(0200,false,"\x1bOP");expect(0201,false,"\x1bOQ");
  expect(0202,false,"\x1bOR");expect(0203,false,"\x1bOS");
  expect(0204,false,"\x1b[15~");expect(0205,false,"\x1b[17~");
  expect(0206,false,"\x1b[18~");
  expectIgnored(0207);
  expectIgnored(0222); // SHIFT+LINE = CLEAR; no longer emits ESC.
  expectIgnored(0255); // SCRATCH is SHIFT+CONT; not the TAB key.
  for(int code=0200;code<0400;++code) {
    const bool special=(code>=0200 && code<=0206)
      ||code==0240||code==0217||code==0231||code==0232||code==0233||code==0234
      ||code==0235||code==0236||code==0237||code==0241
      ||code==0242||code==0243||code==0244||code==0245;
    if(!special)expectIgnored(static_cast<uint8_t>(code));
  }
  // Mixed keys must preserve complete 3- and 5-byte escape sequences, Ctrl,
  // zero byte, Backspace, and filtered K8 in the emitted byte stream.
  const uint8_t input[]={'D','I','R',0232,0241,0235,0245,0244,
                         0206,0240,0217,0222,0255,0,3,0231,0207};
  const char expected[]="DIR\r\x1b[A\x1b[C\x1b[H\x1b[3~\x1b[18~\x1b\x09";
  std::vector<uint8_t> bytes;
  for(uint8_t hp:input) {
    const InputBytes b=toInputBytes(hp);
    assert(b.len<=Cpm85Keymap::kMaxInputBytes);
    for(int i=0;i<b.len;++i)bytes.push_back(b.bytes[i]);
  }
  const size_t textLen=sizeof(expected)-1;
  assert(bytes.size()==textLen+3);
  for(size_t i=0;i<textLen;++i)assert(bytes[i]==static_cast<uint8_t>(expected[i]));
  assert(bytes[textLen]==0 && bytes[textLen+1]==3 && bytes[textLen+2]==8);

  // Passive outgoing UART tracker: split CSI frames, multiple private modes,
  // other text/SGR and RIS reset must not corrupt DECCKM state.
  CursorModeTracker t;
  assert(!t.applicationCursor());
  output(t,"Hello\x1b[31mWorld\x1b[?");
  assert(!t.applicationCursor());
  output(t,"1h");
  assert(t.applicationCursor());
  expect(0241,t.applicationCursor(),"\x1bOA");
  output(t,"\x1b[?7l");
  assert(t.applicationCursor());
  output(t,"\x1b[?6;1l");
  assert(!t.applicationCursor());
  output(t,"\x1b[?1;7h");
  assert(t.applicationCursor());
  output(t,"\x1b" "c");
  assert(!t.applicationCursor());
  output(t,"\x1b[?1h");
  assert(t.applicationCursor());
  output(t,"\x1b[?1l");
  assert(!t.applicationCursor());

  std::puts("PASS: CPM85 ANSI/VT100, LINE->ESC, CONT->TAB, shifted CLEAR/SCRATCH ignored, K8");
}
