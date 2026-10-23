// Stage B1 regression using the real redcode/Z80 core and a minimal fake
// BDOS return routine. No Arduino, SD hardware or guest ROM changes.
#include "../ESP32_Z80_Emulator/cpm_safe_point.h"
#include "../ESP32_Z80_Emulator/cpm_directory_service.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static bool serviceGate(void *context,uint16_t pc,const uint8_t *ram) {
  return static_cast<CpmSafePoint::Gate *>(context)->fetch(pc,ram);
}

struct Fixture {
  uint8_t ram[65536]={};
  volatile bool running=true;
  Z80Bridge bridge{};
  CpmSafePoint::Gate gate{};
  Fixture() {
    const uint8_t prompt[]={0x31,0xAB,0xD7};
    const uint8_t call[]={0xCD,0x39,0xD1};
    const uint8_t command[]={0x11,0x80,0x00};
    const uint8_t console[]={0x0E,0x0A};
    const uint8_t bdosCall[]={0xCD,0x05,0x00};
    const uint8_t next[]={0xCD,0x29,0xD1};
    memcpy(ram+0xD382,prompt,sizeof(prompt));
    memcpy(ram+0xD395,call,sizeof(call));
    memcpy(ram+0xD398,command,sizeof(command));
    memcpy(ram+0xD19C,console,sizeof(console));
    memcpy(ram+0xD1A1,bdosCall,sizeof(bdosCall));
    memcpy(ram+0xD1A4,next,sizeof(next));
    // JP D806 (the real CP/M 2.2 BDOS vector). Stub at D806 implements:
    // if(C==17) return A=0 (first entry), otherwise return A=FF (EOF).
    ram[5]=0xC3;ram[6]=0x06;ram[7]=0xD8;
    const uint8_t bdos[]={0x79,0xFE,0x11,0x28,0x03,0x3E,0xFF,0xC9,
                          0xAF,0xC9};
    memcpy(ram+0xD806,bdos,sizeof(bdos));
    ram[0xE5B1]=0x80;ram[0xE5B2]=0;
    const uint8_t entry[]={0x00,'H','E','L','L','O',' ',' ',' ',
                           'T','X','T',0,0,0,8};
    memcpy(ram+0x80,entry,sizeof(entry));
    for(int i=0;i<256;++i)ram[0xC000+i]=uint8_t(i^0x5A);
    z80BridgeInit(&bridge,ram,nullptr,nullptr,&gate,&running);
    z80BridgeColdReset(&bridge);
    bridge.service_fetch=serviceGate;
    bridge.trap_enabled=true;
    bridge.trap_address=0xE603;
    gate.reset(ram,true);
    Z80_PC(bridge.cpu)=CpmSafePoint::kCcpPrompt;
    Z80_SP(bridge.cpu)=0xBD00;
    Z80_A(bridge.cpu)=0xA5;
    Z80_BC(bridge.cpu)=0xBEEF;
  }
  void armDirectory() {
    gate.fetch(CpmSafePoint::kCcpPrompt,ram);
    gate.fetch(CpmSafePoint::kCcpConsoleInput,ram);
    assert(gate.request(CpmSafePoint::DIRECTORY));
    assert(!gate.request(CpmSafePoint::PROBE));
    assert(gate.operation.load()==CpmSafePoint::DIRECTORY);
    ram[CpmSafePoint::kCcpInputLength]=0;
    gate.fetch(CpmSafePoint::kCcpInputReturn,ram);
    gate.fetch(CpmSafePoint::kCcpCommand,ram);
    assert(z80BridgeRun(&bridge,150)==Z80_BRIDGE_SERVICE);
    assert(Z80_PC(bridge.cpu)==CpmSafePoint::kCcpPrompt);
  }
};

static void testDirectoryAndExactRestore() {
  Fixture f;
  f.armDirectory();
  uint8_t scratch[256],dma[128];
  memcpy(scratch,f.ram+0xC000,sizeof(scratch));
  memcpy(dma,f.ram+0x80,sizeof(dma));
  const Z80 cpuBefore=f.bridge.cpu;
  CpmSafePoint::Directory out;
  bool mustReset=true;
  assert(CpmDirectoryService::execute(&f.bridge,out,mustReset));
  assert(!mustReset);
  assert(out.count==1 && !out.truncated && out.error==CpmSafePoint::DIR_OK);
  assert(strcmp(out.entries[0].name,"HELLO.TXT")==0);
  assert(out.entries[0].user==0 && out.entries[0].records==8);
  assert(memcmp(f.ram+0xC000,scratch,sizeof(scratch))==0);
  assert(memcmp(f.ram+0x80,dma,sizeof(dma))==0);
  assert(memcmp(&f.bridge.cpu,&cpuBefore,sizeof(cpuBefore))==0);
  assert(f.bridge.trap_address==0xE603 && f.bridge.trap_enabled);
  assert(f.bridge.service_fetch==serviceGate && f.bridge.device==&f.gate);
  f.gate.publishDirectory(out);
  f.gate.complete(true);
  CpmSafePoint::Directory copy;
  assert(f.gate.copyDirectory(copy));
  assert(copy.count==1 && strcmp(copy.entries[0].name,"HELLO.TXT")==0);
  assert(!f.gate.request(CpmSafePoint::DIRECTORY));
}

static void testExplicitRequestedDrive() {
  Fixture f;
  f.armDirectory();
  CpmSafePoint::Directory out;
  bool reset=false;
  assert(CpmDirectoryService::execute(&f.bridge,out,reset,1));
  assert(!reset && out.count==1 && out.error==CpmSafePoint::DIR_OK);
  assert(out.drive=='A');
  // Executing the explicit-drive FCB must not overwrite guest TPA scratch.
  assert(f.ram[CpmDirectoryService::kFcb]!=1);
}

static void testFailClosedBeforeBdos() {
  Fixture f;
  f.armDirectory();
  f.ram[0xE5B1]=0x40; // nonstandard DMA pointer: refuse to touch BDOS
  const Z80 original=f.bridge.cpu;
  CpmSafePoint::Directory out;
  bool mustReset=true;
  assert(!CpmDirectoryService::execute(&f.bridge,out,mustReset));
  assert(out.error==CpmSafePoint::DIR_BAD_DMA);
  assert(!mustReset);
  assert(memcmp(&f.bridge.cpu,&original,sizeof(original))==0);
}

// The real CP/M image has shown noncanonical filename data. BDOS itself
// returned safely. The host may omit malformed metadata, but MUST NOT force
// a Z80 restart or risk running its native CCP with altered registers/RAM.
static void testNoncanonicalFilenameIsWarningNotGuestReset() {
  Fixture f;
  f.armDirectory();
  f.ram[0x81]=0x00; // first byte of returned 8.3 base name is invalid
  const Z80 saved=f.bridge.cpu;
  uint8_t scratch[256],dma[128];
  memcpy(scratch,f.ram+0xC000,sizeof(scratch));
  memcpy(dma,f.ram+0x80,sizeof(dma));
  CpmSafePoint::Directory out;
  bool reset=true;
  assert(CpmDirectoryService::execute(&f.bridge,out,reset,1));
  assert(!reset);
  assert(out.drive=='A');
  assert(out.count==0 && out.skipped==1);
  assert(out.error==CpmSafePoint::DIR_INVALID_NAME);
  assert(out.firstBadIndex==0 && out.firstBadOffset==1 && out.firstBadByte==0);
  assert(memcmp(&f.bridge.cpu,&saved,sizeof(saved))==0);
  assert(memcmp(scratch,f.ram+0xC000,sizeof(scratch))==0);
  assert(memcmp(dma,f.ram+0x80,sizeof(dma))==0);

  // CP/M may report a bad directory slot/user after returning normally:
  // fail the request, but this alone never justifies guest restart.
  Fixture user;
  user.armDirectory();
  user.ram[0x80]=16;
  bool resetUser=true;
  assert(!CpmDirectoryService::execute(&user.bridge,out,resetUser,1));
  assert(!resetUser && out.error==CpmSafePoint::DIR_INVALID_USER);
}

static void testInterruptedBdosStillRequestsGuestReset() {
  Fixture f;
  f.armDirectory();
  // Emulated BDOS halts before returning to the service trampoline.
  // This is an UNSAFE guest execution failure, unlike a bad name.
  f.ram[0xD806]=0x76; // HALT
  CpmSafePoint::Directory out;
  bool reset=false;
  assert(!CpmDirectoryService::execute(&f.bridge,out,reset,1));
  assert(reset);
  assert(out.error==CpmSafePoint::DIR_BDOS_INTERRUPTED);
}

static void testEmptyDirectoryAndBoundedOutput() {
  Fixture f;
  f.armDirectory();
  // Immediate EOF for BDOS first/next.
  f.ram[0xD806]=0x3E;f.ram[0xD807]=0xFF;f.ram[0xD808]=0xC9;
  CpmSafePoint::Directory out;
  bool reset=false;
  assert(CpmDirectoryService::execute(&f.bridge,out,reset));
  assert(out.count==0 && !out.truncated && !reset);
  assert(out.error==CpmSafePoint::DIR_OK);

  Fixture many;
  many.armDirectory();
  // Every search returns the same entry; cap the count and signal truncation.
  many.ram[0xD806]=0xAF;many.ram[0xD807]=0xC9;
  assert(CpmDirectoryService::execute(&many.bridge,out,reset));
  assert(out.count==CpmSafePoint::kDirectoryMax);
  assert(out.truncated && !reset && out.error==CpmSafePoint::DIR_OK);
}

int main() {
  testDirectoryAndExactRestore();
  testExplicitRequestedDrive();
  testFailClosedBeforeBdos();
  testNoncanonicalFilenameIsWarningNotGuestReset();
  testInterruptedBdosStillRequestsGuestReset();
  testEmptyDirectoryAndBoundedOutput();
  std::puts("PASS: BDOS directory search, bounded entries, safe rejection and exact Z80 restore");
}
