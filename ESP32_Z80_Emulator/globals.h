#pragma once
#include "ebtks_firmware_options.h"

#define EBTKS

const String banner[] = {
  " EEEEEE   SSSSSS    PPPPPP    888888    000000  ",
  "E        S      S  P      P  8      8  0     00 ",
  "E        S         P      P  8      8  0    0 0 ",
  "EEEEEE    SSSSSS   PPPPPPP    888888   0   0  0 ",
  "E               S  P         8      8  0  0   0 ",
  "E               S  P         8      8  0 0    0 ",
  " EEEEEE   SSSSSS   P          888888    000000  ",
  "                                                ",
  "       EBTKS Z80 Emulator - Milestone 2         ",
  "        based on ESP32 Z80 Emulator V2.2        ",
  "                                                "
};

#if EBTKS_TELNET_ENABLE
WiFiServer telnetServer(23);
WiFiClient serverClient;
#endif
const char *hostName = "ebtks-z80";

// The upstream CPU initialisation still calls sdSPI.begin()/SD.begin().
// On EBTKS there is deliberately no ESP32-attached SD card: those calls are
// harmless compatibility shims. Actual file and disk traffic is performed
// by ebtks_storage.h over UART1 to the Teensy SD card.
class EBTKSNullSPI {
public:
  void begin(int, int, int, int) {}
};

class EBTKSNullSD {
public:
  bool begin(int, EBTKSNullSPI &) { return true; }
  File open(const char *, const char * = FILE_READ) { return File(); }
};

EBTKSNullSPI sdSPI;
EBTKSNullSD ebtksNullSD;
#define SD ebtksNullSD
#define SS   (-1)
#define MOSI (-1)
#define MISO (-1)
#define SCK  (-1)

int PortA[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
int PortB[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

const uint8_t GPP = 0;
const uint8_t UART_PORT = 0x80;
const uint8_t UART_LSR  = UART_PORT + 5;
const uint8_t DCMD  = 0x20;
const uint8_t DPARM = 0x30;

uint8_t A=0, Fl=0, B=0, C=0, D=0, E=0, H=0, L=0;
uint16_t IX=0, IY=0, PC=0, SP=0;
uint8_t Aa=0, Fla=0, Ba=0, Ca=0, Da=0, Ea=0, Ha=0, La=0;
bool Zf=false, Cf=false, Sf=false, Hf=false, Pf=false, Nf=false;

bool RUN=false, SingleStep=false, intE=false, bpOn=false;
uint16_t BP=0xffff;
uint8_t BPmode=0, OC, JR, V8, v1, v2;
uint16_t V16;
uint16_t V16a;
uint32_t V32;
bool cfs, dled;

uint8_t RAM[65536] = {};
uint8_t pOut[256];
uint8_t pIn[256];
uint8_t rxBuf[1024];
volatile uint16_t rxInPtr=0, rxOutPtr=0;
uint8_t txBuf[1024];
volatile uint16_t txInPtr=0, txOutPtr=0;

int vdrive;
char sdfile[64] = {};
char sddir[64] = {"/cpm/download"};
bool sdfound = false;  // Milestone 2: true means Teensy-backed storage is reachable.

TaskHandle_t Task1, Task2, Task3, Task4, Task5, Task6;
SemaphoreHandle_t baton;
bool ota_t=false, cpu_t=false, serial_t=false;
#if EBTKS_TELNET_ENABLE
bool telnet_t=false;
#endif
uint32_t POP[256] = {};
uint32_t POPcb[256] = {};
