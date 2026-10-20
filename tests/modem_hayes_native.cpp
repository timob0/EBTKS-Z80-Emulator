#include "../ESP32_Z80_Emulator/ebtks_modem_hayes.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using EbtksModem::Hayes;
using EbtksModem::HayesEvent;
static HayesEvent cmd(Hayes& h,const char* s) {
  HayesEvent e={HayesEvent::NONE,{0},0,false};
  while(*s)e=h.feed(static_cast<unsigned char>(*s++));
  return e;
}
int main(){
  Hayes h;
  assert(cmd(h,"AT\r").kind==HayesEvent::OK);
  assert(cmd(h,"ate0v1\r").kind==HayesEvent::OK && !h.echo());
  assert(cmd(h,"ATDTvert.synchro.net:23\r").kind==HayesEvent::DIAL);
  auto d=cmd(h,"ATDexample.org:2323\r");
  assert(d.kind==HayesEvent::DIAL && d.port==2323);
  assert(std::strcmp(d.host,"example.org")==0);
  d=cmd(h,"ATDP192.0.2.1\r");
  assert(d.kind==HayesEvent::DIAL && d.port==23);
  assert(cmd(h,"ATD\r").kind==HayesEvent::ERROR);
  assert(cmd(h,"ATDfoo:0\r").kind==HayesEvent::ERROR);
  assert(cmd(h,"ATDfoo:70000\r").kind==HayesEvent::ERROR);
  assert(cmd(h,"ATDfoo:22x\r").kind==HayesEvent::ERROR);
  assert(cmd(h,"ATDfoo/bar\r").kind==HayesEvent::ERROR);
  assert(cmd(h,"ATQ1\r").kind==HayesEvent::OK && h.quiet());
  assert(cmd(h,"ATQ0\r").kind==HayesEvent::OK && !h.quiet());
  assert(cmd(h,"ATV0\r").kind==HayesEvent::OK && !h.verbose());
  assert(cmd(h,"ATV1\r").kind==HayesEvent::OK && h.verbose());
  assert(cmd(h,"ATO\r").kind==HayesEvent::ONLINE);
  assert(cmd(h,"ATI\r").kind==HayesEvent::INFO);
  assert(cmd(h,"AT&F\r").kind==HayesEvent::OK && h.echo());
  assert(cmd(h,"ATX9\r").kind==HayesEvent::ERROR);
  assert(cmd(h,"ATH\r").kind==HayesEvent::HANGUP);
  assert(cmd(h,"ATZ\r").kind==HayesEvent::HANGUP);
  assert(cmd(h,"ATH\r").kind==HayesEvent::HANGUP);
  // Deleting a typed character must not corrupt the command.
  assert(cmd(h,"ATxx\b\b\r").kind==HayesEvent::OK);
  std::puts("PASS: bounded Hayes parser AT, dialing, options, errors, reset");
}
