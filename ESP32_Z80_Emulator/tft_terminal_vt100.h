// Second-generation VT100/ANSI/CP437 terminal model. No Arduino dependencies.
// Separate from the proven HP/Soroc TFT model; no pixel framebuffer.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace EbtksVt100 {
constexpr int kCols=80, kRows=24;
constexpr uint8_t kDefaultColor=16;
constexpr uint16_t kFgMask=31, kBgMask=31<<5;
constexpr uint16_t kBold=1<<10, kUnderline=1<<11, kInverse=1<<12;
constexpr uint16_t kDefaultStyle=16|(16<<5);
struct Screen {
  uint8_t cells[kRows][kCols];
  uint16_t styles[kRows][kCols];
  bool dirty[kRows];
  uint8_t row=0,col=0,savedRow=0,savedCol=0,scrollTop=0,scrollBottom=kRows-1;
  uint16_t attr=kDefaultStyle,savedAttr=kDefaultStyle;
  enum Parse : uint8_t {NORMAL,ESCAPE,CSI,ESC_G0,ESC_G1,ESC_HASH,SOROC_ROW,SOROC_COL,HP_AMP} state=NORMAL;
  char params[48]={};
  uint8_t paramsLen=0,sorocRow=0;
  bool g0Dec=false,g1Dec=false,g0Uk=false,g1Uk=false,useG1=false;
  bool autoWrap=true,insertMode=false,originMode=false,vtDialect=false;
  Screen(){reset();}
  static int clamp(int n,int lo,int hi){return n<lo?lo:(n>hi?hi:n);}
  void touch(int r){dirty[r]=true;}
  void blankRow(int r) {
    memset(cells[r],' ',kCols);
    for(int c=0;c<kCols;++c)styles[r][c]=attr;
    touch(r);
  }
  void reset() {
    row=col=savedRow=savedCol=scrollTop=0;scrollBottom=kRows-1;
    attr=savedAttr=kDefaultStyle;state=NORMAL;paramsLen=0;
    g0Dec=g1Dec=g0Uk=g1Uk=useG1=false;
    autoWrap=true;insertMode=originMode=vtDialect=false;
    for(int r=0;r<kRows;++r)blankRow(r);
  }
  void put(int r,int c,uint8_t ch,uint16_t st) {
    if(cells[r][c]!=ch||styles[r][c]!=st) {
      cells[r][c]=ch;styles[r][c]=st;touch(r);
    }
  }
  void clearSpan(int r,int first,int last) {
    first=clamp(first,0,kCols-1);last=clamp(last,0,kCols-1);
    for(int c=first;c<=last;++c)put(r,c,' ',attr);
  }
  void scrollUp(int n,int top,int bottom) {
    if(top<0||bottom>=kRows||top>bottom)return;
    n=clamp(n,1,bottom-top+1);
    for(int r=top;r<=bottom-n;++r){
      memcpy(cells[r],cells[r+n],kCols);
      memcpy(styles[r],styles[r+n],sizeof(styles[r]));
    }
    for(int r=bottom-n+1;r<=bottom;++r)blankRow(r);
    for(int r=top;r<=bottom;++r)touch(r);
  }
  void scrollDown(int n,int top,int bottom) {
    if(top<0||bottom>=kRows||top>bottom)return;
    n=clamp(n,1,bottom-top+1);
    for(int r=bottom;r>=top+n;--r){
      memcpy(cells[r],cells[r-n],kCols);
      memcpy(styles[r],styles[r-n],sizeof(styles[r]));
    }
    for(int r=top;r<top+n;++r)blankRow(r);
    for(int r=top;r<=bottom;++r)touch(r);
  }
  void lineFeed(){
    if(row==scrollBottom&&row>=scrollTop)scrollUp(1,scrollTop,scrollBottom);
    else row=(uint8_t)clamp(row+1,0,kRows-1);
  }
  void reverseIndex(){
    if(row==scrollTop)scrollDown(1,scrollTop,scrollBottom);
    else row=(uint8_t)clamp(row-1,0,kRows-1);
  }
  void insertChars(int n){
    if(col>=kCols)return;
    n=clamp(n,1,kCols-col);
    for(int c=kCols-1;c>=col+n;--c){cells[row][c]=cells[row][c-n];styles[row][c]=styles[row][c-n];}
    clearSpan(row,col,col+n-1);touch(row);
  }
  void deleteChars(int n){
    if(col>=kCols)return;
    n=clamp(n,1,kCols-col);
    for(int c=col;c<kCols-n;++c){cells[row][c]=cells[row][c+n];styles[row][c]=styles[row][c+n];}
    clearSpan(row,kCols-n,kCols-1);touch(row);
  }
  void print(uint8_t ch) {
    if(col>=kCols){
      if(autoWrap){col=0;lineFeed();}
      else col=kCols-1;
    }
    if(insertMode)insertChars(1);
    put(row,col,ch,attr);
    if(col<kCols)++col;
  }
  void eraseLine(int mode) {
    if(mode==2)clearSpan(row,0,kCols-1);
    else if(mode==1)clearSpan(row,0,clamp(col,0,kCols-1));
    else if(col<kCols)clearSpan(row,col,kCols-1);
  }
  void eraseDisplay(int mode) {
    if(mode==2||mode==3){for(int r=0;r<kRows;++r)blankRow(r);}
    else if(mode==0) {
      eraseLine(0);for(int r=row+1;r<kRows;++r)blankRow(r);
    } else if(mode==1) {
      eraseLine(1);for(int r=0;r<row;++r)blankRow(r);
    }
  }
  int param(int ordinal,int fallback)const{
    int index=0,value=0;bool has=false;
    for(int i=0;i<=paramsLen;++i){
      char ch=i<paramsLen?params[i]:';';
      if(ch==';'){
        if(index==ordinal)return has?value:fallback;
        ++index;value=0;has=false;
      }else if(ch>='0'&&ch<='9'){
        value=clamp(value*10+(ch-'0'),0,999);has=true;
      }
    }return fallback;
  }
  int paramCount()const{
    int n=1;for(int i=0;i<paramsLen;++i)if(params[i]==';')++n;
    return n;
  }
  void sgr(){
    const int n=paramCount();
    for(int i=0;i<n;++i) {
      int p=param(i,0);
      switch(p) {
        case 0:attr=kDefaultStyle;break;
        case 1:attr|=kBold;break;
        case 4:attr|=kUnderline;break;
        case 7:attr|=kInverse;break;
        case 22:attr&=~kBold;break;
        case 24:attr&=~kUnderline;break;
        case 27:attr&=~kInverse;break;
        case 39:attr=(attr&~kFgMask)|kDefaultColor;break;
        case 49:attr=(attr&~kBgMask)|(kDefaultColor<<5);break;
        default:
          if(p>=30&&p<=37)attr=(attr&~kFgMask)|(p-30);
          else if(p>=40&&p<=47)attr=(attr&~kBgMask)|((p-40)<<5);
          else if(p>=90&&p<=97)attr=(attr&~kFgMask)|(p-90+8);
          else if(p>=100&&p<=107)attr=(attr&~kBgMask)|((p-100+8)<<5);
          else if(p==38||p==48){
            int mode=param(i+1,-1);
            if(mode==5)i+=2;else if(mode==2)i+=4;
          }
      }
    }
  }
  void save(){savedRow=row;savedCol=col;savedAttr=attr;}
  void restore(){row=savedRow;col=savedCol;attr=savedAttr;}
  void csi(uint8_t command){
    bool priv=paramsLen&&params[0]=='?';
    if(priv){
      if(command=='h'||command=='l'){
        bool on=command=='h';int option=0;
        for(int i=1;i<=paramsLen;++i){
          char ch=i<paramsLen?params[i]:';';
          if(ch==';'){
            if(option==6){originMode=on;row=on?scrollTop:0;col=0;}
            if(option==7)autoWrap=on;
            option=0;
          }else if(ch>='0'&&ch<='9')option=clamp(option*10+(ch-'0'),0,9999);
        }
      }return;
    }
    int n=param(0,1);if(n==0)n=1;
    int lo=originMode?scrollTop:0,hi=originMode?scrollBottom:kRows-1;
    switch(command){
      case 'A':row=(uint8_t)clamp(row-n,lo,hi);break;
      case 'B':row=(uint8_t)clamp(row+n,lo,hi);break;
      case 'C':col=(uint8_t)clamp(col+n,0,kCols-1);break;
      case 'D':col=(uint8_t)clamp(col-n,0,kCols-1);break;
      case 'E':row=(uint8_t)clamp(row+n,lo,hi);col=0;break;
      case 'F':row=(uint8_t)clamp(row-n,lo,hi);col=0;break;
      case 'G':case 0x60:col=(uint8_t)clamp(n-1,0,kCols-1);break;
      case 'd':row=(uint8_t)clamp(n-1+(originMode?scrollTop:0),lo,hi);break;
      case 'H':case 'f':
        row=(uint8_t)clamp(n-1+(originMode?scrollTop:0),lo,hi);
        col=(uint8_t)clamp(param(1,1)-1,0,kCols-1);break;
      case 'J':eraseDisplay(param(0,0));break;
      case 'K':eraseLine(param(0,0));break;
      case '@':insertChars(n);break;
      case 'P':deleteChars(n);break;
      case 'X':if(col<kCols)clearSpan(row,col,col+n-1);break;
      case 'L':if(row>=scrollTop&&row<=scrollBottom)scrollDown(n,row,scrollBottom);break;
      case 'M':if(row>=scrollTop&&row<=scrollBottom)scrollUp(n,row,scrollBottom);break;
      case 'S':scrollUp(n,scrollTop,scrollBottom);break;
      case 'T':scrollDown(n,scrollTop,scrollBottom);break;
      case 'r':{
        int first=clamp(param(0,1)-1,0,kRows-1);
        int last=clamp(param(1,kRows)-1,0,kRows-1);
        if(first>=last){first=0;last=kRows-1;}
        scrollTop=(uint8_t)first;scrollBottom=(uint8_t)last;
        row=originMode?scrollTop:0;col=0;break;
      }
      case 's':save();break;case 'u':restore();break;
      case 'm':sgr();break;
      case 'h':case 'l':if(param(0,0)==4)insertMode=(command=='h');break;
      // DSR/DA requests are ignored: only the WebSocket/Telnet session can reply.
      default:break;
    }
  }
  // DEC Special Graphics -> IBM code page 437, using Font0's bitmaps.
  static uint8_t decGlyph(uint8_t ch){
    switch(ch){
      case '_':return ' ';case 0x60:return 0x04;case 'a':return 0xB1;
      case 'b':return 'T';case 'c':return 'F';case 'd':return 'R';
      case 'e':return 'L';case 'f':return 0xF8;case 'g':return 0xF1;
      case 'h':case 'i':return '#';
      case 'j':return 0xD9;case 'k':return 0xBF;case 'l':return 0xDA;
      case 'm':return 0xC0;case 'n':return 0xC5;
      case 'o':case 'p':case 'q':case 'r':case 's':return 0xC4;
      case 't':return 0xC3;case 'u':return 0xB4;
      case 'v':return 0xC1;case 'w':return 0xC2;case 'x':return 0xB3;
      case 'y':return 0xF3;case 'z':return 0xF2;case '{':return 0xE3;
      case '|':return '!';case '}':return 0x9C;case '~':return 0xFA;
      default:return ch;
    }
  }
  void hpPosition(){
    if(paramsLen<3||params[0]!='a')return;
    int i=1;
    for(int k=0;k<2&&i<paramsLen;++k){
      bool neg=false;if(params[i]=='+'||params[i]=='-'){neg=params[i]=='-';++i;}
      int value=0;bool have=false;
      while(i<paramsLen&&params[i]>='0'&&params[i]<='9'){
        value=clamp(value*10+(params[i++]-'0'),0,999);have=true;
      }
      if(!have||i>=paramsLen)return;
      char axis=params[i++];int pos=neg?-value:value;
      if(axis=='c'||axis=='C')col=(uint8_t)clamp(pos,0,kCols-1);
      else if(axis=='r'||axis=='R'||axis=='y'||axis=='Y')
        row=(uint8_t)clamp(pos,0,kRows-1);
      else return;
    }
  }
  void feedByte(uint8_t ch){
    switch(state){
      case CSI:
        if(ch>=0x40&&ch<=0x7e){csi(ch);state=NORMAL;paramsLen=0;}
        else if(ch>=0x20&&ch<=0x3f) {
          if(paramsLen<sizeof(params)-1)params[paramsLen++]=(char)ch;
          else{state=NORMAL;paramsLen=0;}
        }else state=ch==27?ESCAPE:NORMAL;
        return;
      case ESC_G0:case ESC_G1:{
        bool dec=ch=='0',uk=ch=='A';
        if(state==ESC_G0){g0Dec=dec;g0Uk=uk;}
        else{g1Dec=dec;g1Uk=uk;}
        vtDialect=true;state=NORMAL;return;
      }
      case ESC_HASH:
        if(ch=='8'){
          for(int r=0;r<kRows;++r)
            for(int c=0;c<kCols;++c)put(r,c,'E',attr);
          row=col=0;
        }
        state=NORMAL;return;
      case SOROC_ROW:
        sorocRow=(uint8_t)clamp(ch-32,0,kRows-1);state=SOROC_COL;return;
      case SOROC_COL:
        row=sorocRow;col=(uint8_t)clamp(ch-32,0,kCols-1);state=NORMAL;return;
      case HP_AMP:
        if(paramsLen<sizeof(params)-1)params[paramsLen++]=(char)ch;
        if(ch>='A'&&ch<='Z'){hpPosition();state=NORMAL;paramsLen=0;}
        else if(paramsLen>=sizeof(params)-1){state=NORMAL;paramsLen=0;}
        return;
      case ESCAPE:
        state=NORMAL;
        switch(ch){
          case '[':paramsLen=0;state=CSI;vtDialect=true;return;
          case '(':state=ESC_G0;return;case ')':state=ESC_G1;return;
          case '#':state=ESC_HASH;return;
          case '=':state=SOROC_ROW;return; // legacy Soroc
          case '&':paramsLen=0;state=HP_AMP;return;
          case '7':save();return;case '8':restore();return;
          case 'c':reset();vtDialect=true;return;
          case 'D':if(vtDialect)lineFeed();else col=(uint8_t)clamp(col-1,0,kCols-1);return;
          case 'E':if(vtDialect){lineFeed();col=0;}else reset();return;
          case 'M':reverseIndex();vtDialect=true;return;
          case 'H':case 'h':row=col=0;return; // HP home / VT HTS ignored
          case '*':reset();return;
          case 'J':eraseDisplay(0);return;case 'K':eraseLine(0);return;
          case 'A':row=(uint8_t)clamp(row-1,0,kRows-1);return;
          case 'B':row=(uint8_t)clamp(row+1,0,kRows-1);return;
          case 'C':col=(uint8_t)clamp(col+1,0,kCols-1);return;
          default:return;
        }
      default:break;
    }
    switch(ch){
      case 27:state=ESCAPE;return;
      case 13:col=0;return;
      case 10:lineFeed();return;
      case 8:if(col>0)--col;return;
      case 9:col=(uint8_t)clamp(((int)col/8+1)*8,0,kCols);return;
      case 12:case 26:reset();return;
      case 14:useG1=true;return;case 15:useG1=false;return;
      case 0:case 7:case 127:return;
      default:
        if(ch>=32){
          if(ch<128){
            bool dec=useG1?g1Dec:g0Dec,uk=useG1?g1Uk:g0Uk;
            if(dec&&ch>=0x5f&&ch<=0x7e)ch=decGlyph(ch);
            else if(uk&&ch=='#')ch=0x9C;
          }
          print(ch); // Preserve raw IBM 8-bit glyphs, not UTF-8.
        }
    }
  }
  void feed(const uint8_t* bytes,size_t n){for(size_t i=0;i<n;++i)feedByte(bytes[i]);}
};
static_assert(sizeof(Screen)<8192,"Compact CP/M terminal must remain under 8KB");
} // namespace EbtksVt100
