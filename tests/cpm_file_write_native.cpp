#include "../ESP32_Z80_Emulator/cpm_file_write_service.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <utility>

struct Fixture {
  uint8_t memory[65536]{};
  volatile bool running=true;
  Z80Bridge bridge{};
  struct Asm {
    std::vector<uint8_t> code;
    std::map<std::string,size_t> labels;
    std::vector<std::pair<size_t,std::string>> fixups;
    void b(uint8_t x){code.push_back(x);}
    void w(uint16_t x){b(uint8_t(x));b(uint8_t(x>>8));}
    void lab(const char *label){labels[label]=code.size();}
    void jpz(const char *label) {
      b(0xCA);fixups.emplace_back(code.size(),label);w(0);
    }
    void ldA(uint8_t n){b(0x3E);b(n);}
    void ldAFrom(uint16_t addr){b(0x3A);w(addr);}
    void ldToA(uint16_t addr){b(0x32);w(addr);}
    void ret(){b(0xC9);}
    void xorA(){b(0xAF);}
    void compare(uint8_t n){b(0xFE);b(n);}
    void transfer(uint16_t from,uint16_t to) {
      b(0x21);w(from); // LD HL,source
      b(0x11);w(to); // LD DE,dest
      b(0x01);w(128); // LD BC,128
      b(0xED);b(0xB0); // LDIR
    }
    void patch(uint8_t *ram) {
      for(const auto &f:fixups) {
        assert(labels.count(f.second));
        const uint16_t dest=uint16_t(0xD806+labels.at(f.second));
        code[f.first]=uint8_t(dest);
        code[f.first+1]=uint8_t(dest>>8);
      }
      assert(code.size()<0x200);
      memcpy(ram+0xD806,code.data(),code.size());
    }
  };
  Fixture() {
    memory[5]=0xC3;memory[6]=0x06;memory[7]=0xD8;
    memory[0xE5B1]=0x80;memory[0xE5B2]=0;
    for(unsigned i=0;i<256;++i)memory[0xC000+i]=uint8_t(0x78^i);
    for(unsigned i=0;i<128;++i)memory[0x80+i]=uint8_t(0xE3^i);
    memory[0xE000]=0xFF; // mock CP/M name is initially absent
    z80BridgeInit(&bridge,memory,nullptr,nullptr,nullptr,&running);
    z80BridgeColdReset(&bridge);
    Z80_PC(bridge.cpu)=0xD382;
    Z80_SP(bridge.cpu)=0xBD00;
    Z80_A(bridge.cpu)=0x39;
    Z80_BC(bridge.cpu)=0x0B0B;
    Asm a;
    a.b(0x79); // LD A,C; dispatch native BDOS call in a real Z80 emulator
    for(const auto &fn:std::vector<std::pair<uint8_t,const char *>>{
         {17,"search"},{22,"make"},{21,"write"},{16,"close"},
         {15,"open"},{20,"read"}}) {
      a.compare(fn.first);a.jpz(fn.second);
    }
    a.ldA(0xFE);a.ret();
    a.lab("search");
    a.ldAFrom(0xE000);a.ret();
    a.lab("make");
    a.ldAFrom(0xE004);a.compare(1);a.jpz("makefail");
    a.xorA();a.ldToA(0xE000);a.ldToA(0xE001);a.ldToA(0xE002);
    a.ret();
    a.lab("makefail");a.ldA(0xFF);a.ret();
    a.lab("write");
    a.ldAFrom(0xE004);a.compare(2);a.jpz("writefail");
    a.ldAFrom(0xE001);a.compare(0);a.jpz("write0");
    a.compare(1);a.jpz("write1");
    a.lab("writefail");a.ldA(2);a.ret();
    a.lab("write0");a.transfer(0x0080,0xE100);
    a.ldA(1);a.ldToA(0xE001);a.xorA();a.ret();
    a.lab("write1");a.transfer(0x0080,0xE180);
    a.ldA(2);a.ldToA(0xE001);a.xorA();a.ret();
    a.lab("close");a.xorA();a.ret();
    a.lab("open");a.ldAFrom(0xE000);a.ret();
    a.lab("read");
    a.ldAFrom(0xE004);a.compare(3);a.jpz("verifyfail");
    a.ldAFrom(0xE002);a.compare(0);a.jpz("read0");
    a.compare(1);a.jpz("read1");
    a.ldA(1);a.ret();
    a.lab("verifyfail");a.ldA(1);a.ret();
    a.lab("read0");a.transfer(0xE100,0x0080);
    a.ldA(1);a.ldToA(0xE002);a.xorA();a.ret();
    a.lab("read1");a.transfer(0xE180,0x0080);
    a.ldA(2);a.ldToA(0xE002);a.xorA();a.ret();
    a.patch(memory);
  }
};
struct Data {int reads=0; int failAt=-1;};
bool readSource(void *ctx,uint16_t rec,uint8_t *data,uint16_t &crc) {
  Data &source=*static_cast<Data*>(ctx);
  if(source.failAt==(int)rec)return false;
  ++source.reads;
  for(unsigned i=0;i<128;++i)data[i]=uint8_t(i^(rec==0?0xA6:0x53));
  crc=CpmSafePoint::fileRecordCrc(data,128);
  return true;
}
uint16_t expectedCrc() {
  Data ctx;uint8_t b[128];uint16_t recordCrc=0,crc=0xFFFF;
  for(unsigned i=0;i<2;++i){
    assert(readSource(&ctx,i,b,recordCrc));
    crc=CpmFileWriteService::extendCrc(crc,b,128);
  }
  return crc;
}
void runCase(bool collision,int fault,int failAt,uint8_t expectedError,
             bool expectPartial) {
  Fixture f;
  f.memory[0xE000]=collision?0:0xFF;
  f.memory[0xE004]=uint8_t(fault);
  const Z80 initial=f.bridge.cpu;
  uint8_t original[256],dma[128];
  memcpy(original,f.memory+0xC000,256);
  memcpy(dma,f.memory+0x80,128);
  uint8_t drive=0,fcb[11]{};
  assert(CpmFileReadService::parseName("A:IMPTST.COM",drive,fcb));
  Data context;context.failAt=failAt;
  CpmFileWriteService::Outcome out;
  bool reset=true;
  bool ok=CpmFileWriteService::execute(
    &f.bridge,fcb,drive,2,expectedCrc(),
    &readSource,&context,out,reset);
  assert(!reset);
  assert(out.error==expectedError);
  assert(out.partialPossible==expectPartial);
  assert(ok==(expectedError==CpmSafePoint::FILE_OK));
  assert(memcmp(original,f.memory+0xC000,256)==0);
  assert(memcmp(dma,f.memory+0x80,128)==0);
  assert(memcmp(&initial,&f.bridge.cpu,sizeof(initial))==0);
  if(ok) {
    assert(out.written==2 && context.reads==2);
    assert(out.crc==expectedCrc());
    assert(f.memory[0xE000]==0 && f.memory[0xE001]==2);
  } else if(collision) {
    assert(out.written==0 && context.reads==0);
    assert(f.memory[0xE001]==0); // no MAKE or WRITE
  }
}
int main() {
  runCase(false,0,-1,CpmSafePoint::FILE_OK,false);
  runCase(true,0,-1,CpmSafePoint::FILE_TARGET_EXISTS,false);
  runCase(false,1,-1,CpmSafePoint::FILE_MAKE_FAILED,true);
  runCase(false,2,-1,CpmSafePoint::FILE_WRITE_RECORD_FAILED,true);
  runCase(false,3,-1,CpmSafePoint::FILE_IMPORT_VERIFY_FAILED,true);
  runCase(false,0,1,CpmSafePoint::FILE_SD_SOURCE_FAILED,true);
  Fixture f;
  f.memory[0xD806]=0x76; // BDOS never reaches trampoline return
  uint8_t drive=0,fcb[11]{};
  assert(CpmFileReadService::parseName("A:IMPTST.COM",drive,fcb));
  Data context;
  CpmFileWriteService::Outcome out;
  bool reset=false;
  assert(!CpmFileWriteService::execute(&f.bridge,fcb,drive,2,expectedCrc(),
                                     &readSource,&context,out,reset));
  assert(reset && out.error==CpmSafePoint::FILE_BDOS_INTERRUPTED);
  assert(out.written==0 && context.reads==0);
  std::puts("PASS: native Z80 B2c MAKE/WRITE/CLOSE+OPEN/READ, no overwrite, CRC, partial files and unsafe halt");
}
