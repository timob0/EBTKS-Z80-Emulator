#include "../ESP32_Z80_Emulator/cpm_file_read_service.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

struct Fixture {
  uint8_t memory[65536]{};
  volatile bool running=true;
  Z80Bridge bridge{};
  Fixture() {
    // Normal CP/M vector and DMA.
    memory[5]=0xC3;memory[6]=0x06;memory[7]=0xD8;
    memory[0xE5B1]=0x80;memory[0xE5B2]=0;
    for(unsigned i=0;i<256;++i)memory[0xC000+i]=uint8_t(i^0x5A);
    for(unsigned i=0;i<128;++i)memory[0x80+i]=uint8_t(i^0xE5);
    memory[0xC040]=0;
    z80BridgeInit(&bridge,memory,nullptr,nullptr,nullptr,&running);
    z80BridgeColdReset(&bridge);
    Z80_PC(bridge.cpu)=0xD382;
    Z80_SP(bridge.cpu)=0xBD00;
    Z80_A(bridge.cpu)=0x5A;
    Z80_BC(bridge.cpu)=0x0B0B;
    // Minimal executable BDOS mock in *real* Z80 opcodes:
    // C=15 OPEN -> 00; C=16 CLOSE -> 00;
    // C=20 READ -> 2 records then EOF=1, stores record index at DMA[0].
    std::vector<uint8_t> code;
    auto byte=[&](uint8_t b){code.push_back(b);};
    auto branch=[&](uint8_t target) {
      byte(0x28);byte(target); // JR Z,+offset patched below
    };
    byte(0x79);                    // LD A,C
    byte(0xFE);byte(15);branch(0); // CP 15 / JR Z open
    const size_t openRel=code.size()-1;
    byte(0xFE);byte(16);branch(0); // CP 16 / JR Z close
    const size_t closeRel=code.size()-1;
    byte(0xFE);byte(20);branch(0); // CP 20 / JR Z read
    const size_t readRel=code.size()-1;
    byte(0x3E);byte(0xFF);byte(0xC9); // error
    const size_t open=code.size();
    byte(0xAF);byte(0xC9);  // open: XOR A / RET
    const size_t close=code.size();
    byte(0xAF);byte(0xC9);  // close: XOR A / RET
    const size_t read=code.size();
    byte(0x3A);byte(0x40);byte(0xC0); // LD A,(C040) current record
    byte(0xFE);byte(2);    // CP 2
    byte(0x30);byte(9);    // JR NC,+9 -> EOF
    byte(0x32);byte(0x80);byte(0x00); // LD (0080),A
    byte(0x3C);             // INC A
    byte(0x32);byte(0x40);byte(0xC0); // LD (C040),A
    byte(0xAF);byte(0xC9); // XOR A; RET
    byte(0x3E);byte(1);byte(0xC9); // EOF: LD A,1; RET
    code[openRel]=uint8_t(open-openRel-1);
    code[closeRel]=uint8_t(close-closeRel-1);
    code[readRel]=uint8_t(read-readRel-1);
    memcpy(memory+0xD806,code.data(),code.size());
  }
};

void testParser() {
  uint8_t drive=0,name[11]{};
  assert(CpmFileReadService::parseName("a:stat.com",drive,name));
  assert(drive==1 && memcmp(name,"STAT    COM",11)==0);
  assert(CpmFileReadService::parseName("P:HELLO",drive,name));
  assert(drive==16 && memcmp(name,"HELLO      ",11)==0);
  for(const char *bad:{"Q:STAT.COM","STAT.COM","A:FOO.BARX",
                        "A:TOOLONGGG.TXT","A:*.COM","A:HI/FOO","A:..",
                        "A:FOO.","A:.COM","A:FOO..COM","A:TEST\r.COM"}) {
    assert(!CpmFileReadService::parseName(bad,drive,name));
  }
}
void testRecord(uint16_t index,bool eof) {
  Fixture f;
  uint8_t fcb[11]{};
  uint8_t drive=0;
  assert(CpmFileReadService::parseName("A:STAT.COM",drive,fcb));
  const Z80 originalCpu=f.bridge.cpu;
  uint8_t scratch[256],dma[128];
  memcpy(scratch,f.memory+0xC000,256);
  memcpy(dma,f.memory+0x80,128);
  CpmSafePoint::FileRecord out;
  bool reset=true;
  assert(CpmFileReadService::execute(&f.bridge,fcb,drive,index,out,reset));
  assert(!reset && out.error==CpmSafePoint::FILE_OK);
  assert(out.record==index && out.eof==eof && out.hasData==!eof);
  if(!eof) {
    assert(out.data[0]==index);
    assert(out.data[1]==uint8_t(1^0xE5));
    assert(out.crc16==CpmSafePoint::fileRecordCrc(out.data,128));
  }
  assert(memcmp(scratch,f.memory+0xC000,256)==0);
  assert(memcmp(dma,f.memory+0x80,128)==0);
  assert(memcmp(&originalCpu,&f.bridge.cpu,sizeof(originalCpu))==0);
}
void testPreflightAndMissing() {
  Fixture f;
  uint8_t fcb[11]{};
  uint8_t drive=0;assert(CpmFileReadService::parseName("A:STAT.COM",drive,fcb));
  CpmSafePoint::FileRecord out;
  bool reset=true;
  f.memory[0xE5B1]=0x40;
  assert(!CpmFileReadService::execute(&f.bridge,fcb,drive,0,out,reset));
  assert(!reset && out.error==CpmSafePoint::FILE_BAD_DMA);
  f.memory[0xE5B1]=0x80;
  f.memory[0xD806]=0x3E;f.memory[0xD807]=0xFF;f.memory[0xD808]=0xC9;
  assert(!CpmFileReadService::execute(&f.bridge,fcb,drive,0,out,reset));
  assert(!reset && out.error==CpmSafePoint::FILE_OPEN_NOT_FOUND);
  assert(!CpmFileReadService::execute(&f.bridge,fcb,drive,64,out,reset));
  assert(!reset && out.error==CpmSafePoint::FILE_BAD_REQUEST);
}
void testInterrupted() {
  Fixture f;uint8_t fcb[11]={'T','E','S','T',' ',' ',' ',' ','C','O','M'};
  f.memory[0xD806]=0x76; // HALT before returning to trampoline
  CpmSafePoint::FileRecord out;bool reset=false;
  assert(!CpmFileReadService::execute(&f.bridge,fcb,1,0,out,reset));
  assert(reset && out.error==CpmSafePoint::FILE_BDOS_INTERRUPTED);
}
static void testGateSafePointAndCancellation() {
  Fixture f;
  // Checked-in native CCP opcode signatures, not arbitrary application PCs.
  const uint8_t sigPrompt[]={0x31,0xAB,0xD7};
  const uint8_t sigCall[]={0xCD,0x39,0xD1};
  const uint8_t sigCmd[]={0x11,0x80,0x00};
  const uint8_t sigCon[]={0x0E,0x0A};
  const uint8_t sigBdos[]={0xCD,0x05,0x00};
  const uint8_t sigReturn[]={0xCD,0x29,0xD1};
  memcpy(f.memory+0xD382,sigPrompt,3);
  memcpy(f.memory+0xD395,sigCall,3);
  memcpy(f.memory+0xD398,sigCmd,3);
  memcpy(f.memory+0xD19C,sigCon,2);
  memcpy(f.memory+0xD1A1,sigBdos,3);
  memcpy(f.memory+0xD1A4,sigReturn,3);
  CpmSafePoint::Gate gate;
  gate.reset(f.memory,true);
  assert(gate.compatible.load());
  gate.fetch(CpmSafePoint::kCcpPrompt,f.memory);
  gate.fetch(CpmSafePoint::kCcpConsoleInput,f.memory);
  gate.observeLineCount(0);
  uint8_t drive=0,fcb[11]{};
  assert(CpmFileReadService::parseName("A:STAT.COM",drive,fcb));
  assert(gate.request(CpmSafePoint::READ_RECORD,drive,fcb,1));
  assert(!gate.request(CpmSafePoint::DIRECTORY));
  gate.observeFetchedOpcode(CpmSafePoint::kBiosConinPollA);
  assert(gate.takeAutomaticReturn());
  assert(!gate.takeAutomaticReturn());
  f.memory[CpmSafePoint::kCcpInputLength]=0;
  gate.fetch(CpmSafePoint::kCcpInputReturn,f.memory);
  gate.fetch(CpmSafePoint::kCcpCommand,f.memory);
  assert(gate.fetch(CpmSafePoint::kCcpPrompt,f.memory));
  assert(gate.checkpoint.load()==CpmSafePoint::BDOS_ENTER);
  CpmSafePoint::FileRecord record{};
  record.hasData=true;
  record.record=1;
  record.data[0]=0xE5;
  gate.publishFile(record);
  gate.fileError.store(CpmSafePoint::FILE_OK);
  gate.complete(true);
  CpmSafePoint::FileRecord copied{};
  assert(gate.copyFile(copied));
  assert(copied.hasData && copied.record==1 && copied.data[0]==0xE5);
  assert(gate.lastFileResult.load()==CpmSafePoint::PASSED);
  assert(gate.lastFileCheckpoint.load()==CpmSafePoint::BDOS_OK);
  gate.reset(f.memory,true);
  assert(gate.result.load()==CpmSafePoint::NEVER);
  assert(gate.lastFileResult.load()==CpmSafePoint::PASSED);

  CpmSafePoint::Gate cancelled;
  cancelled.reset(f.memory,true);
  cancelled.fetch(CpmSafePoint::kCcpPrompt,f.memory);
  cancelled.fetch(CpmSafePoint::kCcpConsoleInput,f.memory);
  cancelled.observeLineCount(0);
  assert(cancelled.request(CpmSafePoint::READ_RECORD,drive,fcb,0));
  cancelled.observeLineCount(2);
  assert(cancelled.result.load()==CpmSafePoint::INPUT_NOT_EMPTY);
  assert(cancelled.lastFileResult.load()==CpmSafePoint::INPUT_NOT_EMPTY);
  assert(!cancelled.takeAutomaticReturn());
}

static void testB2cGateImportOwnership() {
  Fixture f;
  const uint8_t sigPrompt[]={0x31,0xAB,0xD7};
  const uint8_t sigCall[]={0xCD,0x39,0xD1};
  const uint8_t sigCmd[]={0x11,0x80,0x00};
  const uint8_t sigCon[]={0x0E,0x0A};
  const uint8_t sigBdos[]={0xCD,0x05,0x00};
  const uint8_t sigReturn[]={0xCD,0x29,0xD1};
  memcpy(f.memory+0xD382,sigPrompt,3);
  memcpy(f.memory+0xD395,sigCall,3);
  memcpy(f.memory+0xD398,sigCmd,3);
  memcpy(f.memory+0xD19C,sigCon,2);
  memcpy(f.memory+0xD1A1,sigBdos,3);
  memcpy(f.memory+0xD1A4,sigReturn,3);
  CpmSafePoint::Gate gate;
  gate.reset(f.memory,true);
  gate.fetch(CpmSafePoint::kCcpPrompt,f.memory);
  gate.fetch(CpmSafePoint::kCcpConsoleInput,f.memory);
  gate.observeLineCount(0);
  uint8_t src[11]{},dst[11]{},srcDrive=0,dstDrive=0;
  assert(CpmFileReadService::parseName("A:STAT.COM",srcDrive,src));
  assert(CpmFileReadService::parseName("B:IMPTST.COM",dstDrive,dst));
  assert(!gate.request(CpmSafePoint::IMPORT_SD,dstDrive,dst));
  assert(!gate.request(CpmSafePoint::IMPORT_SD,dstDrive,dst,0,src,17));
  assert(gate.request(CpmSafePoint::IMPORT_SD,dstDrive,dst,0,src,srcDrive));
  assert(gate.requestedSourceDrive.load()==1);
  assert(gate.requestedDrive.load()==2);
  assert(gate.requestedSourceFile[0].load()=='S');
  assert(gate.requestedFile[0].load()=='I');
  assert(!gate.request(CpmSafePoint::READ_RECORD,dstDrive,dst,0));
  gate.observeLineCount(3);
  assert(gate.result.load()==CpmSafePoint::INPUT_NOT_EMPTY);
  assert(gate.lastFileResult.load()==CpmSafePoint::INPUT_NOT_EMPTY);
  assert(!gate.takeAutomaticReturn());
}

int main() {
  testGateSafePointAndCancellation();
  testB2cGateImportOwnership();
  testParser();
  testRecord(0,false);
  testRecord(1,false);
  testRecord(2,true);
  testPreflightAndMissing();
  testInterrupted();
  std::puts("PASS: CP/M file record BDOS15/20/16, CRC, EOF, exact CPU/DMA/TPA restoration");
}
