// Native CPU-independent correctness tests for the 80x24 TFT CP/M model.
#include "../ESP32_Z80_Emulator/tft_terminal_model.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using EbtksTerminal::Screen;
static void feed(Screen& s,const char* t){s.feed((const uint8_t*)t,strlen(t));}
int main(){
  Screen s;
  feed(s,"A>DIR\r\nHELLO");
  assert(s.cells[0][0]=='A' && s.cells[0][1]=='>');
  assert(s.cells[1][0]=='H' && s.cells[1][4]=='O');
  assert(s.row==1 && s.col==5);
  feed(s,"\x1b[4;12H!");
  assert(s.cells[3][11]=='!' && s.row==3);
  feed(s,"\x1b[2J\x1b[H");
  assert(s.cells[3][11]==' ' && s.row==0 && s.col==0);
  feed(s,"XY\bZ");
  assert(s.cells[0][0]=='X' && s.cells[0][1]=='Z');
  feed(s,"\x1b=");
  uint8_t cursor[]={32+12,32+34,'Q'};
  s.feed(cursor,sizeof(cursor));
  assert(s.cells[12][34]=='Q');
  feed(s,"\x1b" "E");
  assert(s.row==0 && s.col==0 && s.cells[12][34]==' ');
  feed(s,"ABC\rXY\x1b[K");
  assert(s.cells[0][0]=='X' && s.cells[0][1]=='Y' && s.cells[0][2]==' ');
  s.reset();
  for(int row=0;row<24;++row){s.feed((const uint8_t*)"Q\r\n",3);}
  assert(s.cells[0][0]=='Q' && s.cells[23][0]==' ');
  // ANSI: bold/inverse is ignored but its payload must never appear.
  s.reset();feed(s,"\x1b[7mFOO\x1b[0m");
  assert(s.cells[0][0]=='F' && s.cells[0][2]=='O');
  std::puts("PASS: TFT 80x24 terminal parser, VT100 and HP/Soroc controls");
  return 0;
}
