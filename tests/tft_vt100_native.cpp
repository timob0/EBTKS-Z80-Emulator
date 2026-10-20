// CPU-independent ANSI/VT100 model regressions (no ESP32 headers required).
#include "../ESP32_Z80_Emulator/tft_terminal_vt100.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using EbtksVt100::Screen;
using namespace EbtksVt100;
static void feed(Screen& s,const char* str){s.feed((const uint8_t*)str,strlen(str));}
static void reset(Screen& s){s.reset();for(bool& b:s.dirty)b=false;}
int main(){
  Screen s;
  assert(sizeof(s)<8192);
  assert(s.row==0 && s.scrollBottom==23 && s.styles[0][0]==kDefaultStyle);
  // A typed glyph only invalidates its cell. A full-screen clear/scroll
  // invalidates entire rows, even when they have mixed ANSI attributes.
  reset(s);
  feed(s,"HI");
  assert(s.dirty[0] && s.dirtyFirst[0]==0 && s.dirtyLast[0]==1);
  s.dirty[0]=false; // simulate painter snapshot consuming the dirty span
  feed(s,"!");
  assert(s.dirty[0] && s.dirtyFirst[0]==2 && s.dirtyLast[0]==2);
  s.dirty[0]=false;
  feed(s,"\x1b[1;2H\x1b[7mZ");
  assert(s.dirty[0] && s.dirtyFirst[0]==1 && s.dirtyLast[0]==1);
  s.dirty[0]=false;
  feed(s,"\x1b[2J");
  for(int r=0;r<kRows;++r)
    assert(s.dirty[r] && s.dirtyFirst[r]==0 && s.dirtyLast[r]==79);
  reset(s);
  // CP/M prompt and basic VT100 cursor/direct addressing, split CSI frames.
  feed(s,"A>DIR\r\nABC");
  assert(s.cells[0][0]=='A' && s.cells[1][0]=='A');
  feed(s,"\x1b[4;");
  feed(s,"13HZ");
  assert(s.cells[3][12]=='Z' && s.col==13);
  feed(s,"\x1b[2J\x1b[H");
  assert(s.cells[3][12]==' ' && s.row==0 && s.col==0);
  feed(s,"AB\bZ");assert(s.cells[0][1]=='Z');
  // Attributes are persisted with the cell; SGR defaults, reverse, underline,
  // ANSI standard and bright colors. Clearing applies the current style.
  reset(s);
  feed(s,"\x1b[1;4;7;31;44mX");
  auto a=s.styles[0][0];
  assert(s.cells[0][0]=='X' && (a&kBold) && (a&kUnderline) && (a&kInverse));
  assert((a&kFgMask)==1 && ((a>>5)&31)==4);
  feed(s,"\x1b[22;24;27;91;104mY");
  a=s.styles[0][1];
  assert((a&(kBold|kUnderline|kInverse))==0);
  assert((a&kFgMask)==9 && ((a>>5)&31)==12);
  feed(s,"\x1b[39;49mZ");
  a=s.styles[0][2];
  assert((a&kFgMask)==kDefaultColor && ((a>>5)&31)==kDefaultColor);
  feed(s,"\x1b[0mW");
  assert(s.styles[0][3]==kDefaultStyle && s.styles[0][0]!=kDefaultStyle);
  feed(s,"\x1b[41m\x1b[2K");
  assert(s.cells[0][0]==' ' && ((s.styles[0][0]>>5)&31)==1);
  // Unsupported extended color params must not be interpreted as SGR reset.
  reset(s);
  feed(s,"\x1b[7;38;2;250;0;0;48;5;42mI");
  assert(s.styles[0][0]&kInverse);
  // CP437 bytes 128..255 must survive without conversion to question marks.
  reset(s);
  const uint8_t box[]={0xDA,0xC4,0xBF,0xB3,0xB0,0xDB};
  s.feed(box,sizeof(box));
  for(int c=0;c<6;++c)assert(s.cells[0][c]==box[c]);
  // DEC G0 Special Graphics l q q k -> CP437 corners/line.
  reset(s);feed(s,"\x1b(0lqqk\x1b(B!");
  assert(s.cells[0][0]==0xDA && s.cells[0][1]==0xC4);
  assert(s.cells[0][2]==0xC4 && s.cells[0][3]==0xBF && s.cells[0][4]=='!');
  // G1 DEC designation and SO/SI must leave G0 ordinary ASCII alone.
  reset(s);feed(s,"\x1b)0x");
  assert(s.cells[0][0]=='x');
  feed(s,"\x0e" "x" "\x0f" "x");
  assert(s.cells[0][1]==0xB3 && s.cells[0][2]=='x');
  // DEC UK character set (# -> pound), escape reset.
  reset(s);feed(s,"\x1b(A#\x1b(B#");
  assert(s.cells[0][0]==0x9C && s.cells[0][1]=='#');
  // Editing without shifting the color attributes onto the wrong character.
  reset(s);feed(s,"\x1b[31mAB\x1b[32mCD");
  feed(s,"\x1b[1;2H\x1b[2@");
  assert(s.cells[0][0]=='A' && s.cells[0][1]==' ');
  assert(s.cells[0][3]=='B' && s.cells[0][4]=='C');
  assert((s.styles[0][3]&31)==1 && (s.styles[0][4]&31)==2);
  feed(s,"\x1b[1;2H\x1b[2P");
  assert(s.cells[0][1]=='B' && s.cells[0][2]=='C');
  feed(s,"\x1b[1;2H\x1b[1X");
  assert(s.cells[0][1]==' ' && s.cells[0][2]=='C');
  // Bounded scrolling region, origin mode, index, reverse index, row shifts.
  reset(s);feed(s,"\x1b[2;5r\x1b[?6h\x1b[1;1H@");
  assert(s.row==1 && s.cells[1][0]=='@');
  feed(s,"\x1b[4;1H#");assert(s.cells[4][0]=='#');
  feed(s,"\x1b" "D"); // index at region bottom => scroll rows 1..4
  assert(s.cells[4][0]==' ' && s.cells[3][0]=='#');
  assert(s.cells[0][0]==' ' && s.scrollTop==1 && s.scrollBottom==4);
  feed(s,"\x1b[1;1H\x1b" "M"); // reverse index => insert blank line in region
  assert(s.cells[1][0]==' ' && s.cells[4][0]=='#');
  feed(s,"\x1b[?6l\x1b[r");
  assert(s.scrollTop==0 && s.scrollBottom==23 && !s.originMode);
  // Insert/delete rows and scrolling outside region must preserve other rows.
  reset(s);feed(s,"FIRST\x1b[10;1HLAST\x1b[2;6r");
  feed(s,"\x1b[3;1H\x1b[1L");
  assert(s.cells[0][0]=='F' && s.cells[9][0]=='L');
  feed(s,"\x1b[1M");
  assert(s.cells[0][0]=='F' && s.cells[9][0]=='L');
  // Legacy Soroc and HP-86 coordinates remain available as a fallback.
  reset(s);feed(s,"\x1b=");
  uint8_t location[]={32+8,32+15,'Q'};s.feed(location,sizeof(location));
  assert(s.cells[8][15]=='Q');
  reset(s);feed(s,"\x1b&a10c4RZ");
  assert(s.cells[4][10]=='Z');
  // SGR/CSI limits, codepage G0 vs 8-bit data, no runaway allocations.
  reset(s);feed(s,"\x1b[9999;9999H!");
  assert(s.cells[23][79]=='!');
  feed(s,"\x1b[?7l");assert(!s.autoWrap);
  feed(s,"\x1b[?7h");assert(s.autoWrap);
  reset(s);for(int r=0;r<24;++r)feed(s,"Q\r\n");
  assert(s.cells[0][0]=='Q' && s.cells[23][0]==' ');
  for(bool b:s.dirty)assert(b);
  std::puts("PASS: ST7796 ANSI 16-color SGR, VT100 edit/scroll, DEC/CP437, HP compatibility");
}
