// Compact, allocation-free 80x24 CP/M console model for the ST7796 mirror.
// Text and a small HP-86/Soroc + ANSI cursor/erase subset, not full VT100.
// No Arduino/FreeRTOS dependencies: native host regressions exercise parser.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace EbtksTerminal {
constexpr int kCols = 80;
constexpr int kRows = 24;

struct Screen {
  char cells[kRows][kCols];
  bool dirty[kRows];
  uint8_t row = 0, col = 0, savedRow = 0, savedCol = 0;
  enum Parse : uint8_t { NORMAL, ESCAPE, CSI, SOROC_ROW, SOROC_COL, HP_AMP } state = NORMAL;
  char params[24] = {};
  uint8_t paramsLen = 0, sorocRow = 0;
  bool changed = false;

  Screen() { reset(); }
  void touch(int r) { dirty[r] = true; changed = true; }
  void reset() {
    memset(cells, ' ', sizeof(cells));
    for (int r=0; r<kRows; ++r) dirty[r]=true;
    row=col=savedRow=savedCol=0;
    state=NORMAL; paramsLen=0; changed=true;
  }
  void lineFeed() {
    if (row < kRows-1) { ++row; return; }
    memmove(cells[0],cells[1],(kRows-1)*kCols);
    memset(cells[kRows-1],' ',kCols);
    for (int r=0;r<kRows;++r) touch(r);
  }
  void print(uint8_t c) {
    if (col >= kCols) { col=0; lineFeed(); }
    if (cells[row][col] != (char)c) { cells[row][col]=(char)c; touch(row); }
    if (col < kCols) ++col;
  }
  void clearLine(int start,int end) {
    if (start < 0) start=0;
    if (end >= kCols) end=kCols-1;
    for(int c=start;c<=end;++c) cells[row][c]=' ';
    touch(row);
  }
  void eraseDisplay(int mode) {
    if (mode==2 || mode==3) {
      for(int r=0;r<kRows;++r){ memset(cells[r],' ',kCols);touch(r); }
    } else if(mode==0) {
      clearLine(col,kCols-1);
      for(int r=row+1;r<kRows;++r){memset(cells[r],' ',kCols);touch(r);}
    } else if(mode==1) {
      clearLine(0,col);
      for(int r=0;r<row;++r){memset(cells[r],' ',kCols);touch(r);}
    }
  }
  void eraseLine(int mode) {
    if (mode==2) clearLine(0,kCols-1);
    else if(mode==1) clearLine(0,col);
    else clearLine(col,kCols-1);
  }
  static int clamp(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
  // First and second numeric parameters of common CSI controls.
  int parameter(int ordinal,int fallback) const {
    int at=0,v=0;bool have=false;
    for(int i=0;i<=paramsLen;++i){
      const char ch=i<paramsLen?params[i]:';';
      if(ch==';'){
        if(at==ordinal)return have?v:fallback;
        ++at;v=0;have=false;
      } else if(ch>='0' && ch<='9') {
        v=v*10+(ch-'0');if(v>999)v=999;have=true;
      }
    }
    return fallback;
  }
  void csi(uint8_t command) {
    int n=parameter(0,1);if(n==0)n=1;
    switch(command){
      case 'H':case 'f':
        row=(uint8_t)clamp(parameter(0,1)-1,0,kRows-1);
        col=(uint8_t)clamp(parameter(1,1)-1,0,kCols-1);
        break;
      case 'A':row=(uint8_t)clamp(row-n,0,kRows-1);break;
      case 'B':row=(uint8_t)clamp(row+n,0,kRows-1);break;
      case 'C':col=(uint8_t)clamp(col+n,0,kCols-1);break;
      case 'D':col=(uint8_t)clamp(col-n,0,kCols-1);break;
      case 'G':col=(uint8_t)clamp(n-1,0,kCols-1);break;
      case 'd':row=(uint8_t)clamp(n-1,0,kRows-1);break;
      case 'J':eraseDisplay(parameter(0,0));break;
      case 'K':eraseLine(parameter(0,0));break;
      case 's':savedRow=row;savedCol=col;break;
      case 'u':row=savedRow;col=savedCol;break;
      // SGR styling, scroll regions, insert/delete and VT line graphics are
      // not implemented in this first mirror. Consume them, don't print them.
      default:break;
    }
  }
  // HP terminal ESC &a column/row positioning, e.g. ESC &a10c4R.
  // Interpret the terminal's 0-based positions like the browser's HP mode.
  void hpPosition() {
    if(paramsLen<3 || params[0]!='a')return;
    int pos=1;
    for(int field=0;field<2 && pos<paramsLen;++field) {
      bool neg=false;
      if(params[pos]=='+' || params[pos]=='-') {
        neg=params[pos]=='-';++pos;
      }
      int value=0;bool digits=false;
      while(pos<paramsLen && params[pos]>='0' && params[pos]<='9') {
        value=value*10+(params[pos++]-'0');
        if(value>999)value=999;
        digits=true;
      }
      if(pos>=paramsLen || !digits)return;
      const char dimension=params[pos++];
      const int v=neg?-value:value;
      if(dimension=='c'||dimension=='C')
        col=(uint8_t)clamp(v,0,kCols-1);
      else if(dimension=='r'||dimension=='R'||dimension=='y'||dimension=='Y')
        row=(uint8_t)clamp(v,0,kRows-1);
      else return;
    }
  }
  void feedByte(uint8_t ch) {
    if (state==CSI) {
      if(ch>=0x40 && ch<=0x7e){csi(ch);state=NORMAL;paramsLen=0;}
      else if(ch>=0x20 && ch<=0x3f) {
        if(paramsLen<sizeof(params)-1)params[paramsLen++]=(char)ch;
        else state=NORMAL;
      } else if(ch==27){state=ESCAPE;}
      else state=NORMAL;
      return;
    }
    if (state==SOROC_ROW) {
      sorocRow=(uint8_t)clamp((int)ch-32,0,kRows-1);
      state=SOROC_COL;return;
    }
    if (state==SOROC_COL) {
      row=sorocRow;
      col=(uint8_t)clamp((int)ch-32,0,kCols-1);
      state=NORMAL;return;
    }
    if (state==HP_AMP) {
      // Consume the complete HP escape command; never spill its parameters
      // into the visible screen as printable text.
      if(paramsLen < sizeof(params)-1) params[paramsLen++]=(char)ch;
      if(ch>='A' && ch<='Z') {
        hpPosition();
        paramsLen=0;
        state=NORMAL;
      } else if(paramsLen >= sizeof(params)-1) {
        paramsLen=0;
        state=NORMAL;
      }
      return;
    }
    if (state==ESCAPE) {
      state=NORMAL;
      switch(ch){
        case '[':state=CSI;paramsLen=0;return;
        case '=':state=SOROC_ROW;return; // Soroc: row+32, col+32
        case '&':paramsLen=0;state=HP_AMP;return;
        case 'H':case 'h':row=col=0;return;
        case 'E':case '*':reset();return; // HP-86 erase whole screen
        case 'J':eraseDisplay(0);return;
        case 'K':eraseLine(0);return;
        case 'A':row=(uint8_t)clamp(row-1,0,kRows-1);return;
        case 'B':row=(uint8_t)clamp(row+1,0,kRows-1);return;
        case 'C':col=(uint8_t)clamp(col+1,0,kCols-1);return;
        case 'D':col=(uint8_t)clamp(col-1,0,kCols-1);return;
        case '7':savedRow=row;savedCol=col;return;
        case '8':row=savedRow;col=savedCol;return;
        default:return;
      }
    }
    switch(ch) {
      case 27:state=ESCAPE;return;
      case 13:col=0;return;
      case 10:lineFeed();return;
      case 8: if(col>0)--col;return;
      case 9: col=(uint8_t)clamp(((int)col/8+1)*8,0,kCols);return;
      case 12:case 26:reset();return;
      case 0:case 7:case 127:return;
      default:if(ch>=32)print(ch < 127?ch:'?');return;
    }
  }
  void feed(const uint8_t* data,size_t n) {
    for(size_t i=0;i<n;++i) feedByte(data[i]);
  }
};
} // namespace EbtksTerminal
