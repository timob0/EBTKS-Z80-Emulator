// ESP32 redcode/Z80 replacement engine adapter.
// Existing machine I/O is intentionally retained; only CPU instruction
// execution/flags/registers move to the pinned upstream C library.
// The old cpu.h remains selectable with -DEBTKS_Z80_LEGACY_CPU.
#pragma once
#include "disk.h"
#include "z80_bridge.h"
#include "cpm_service_probe.h"
#include <new>  // tiny standard-heap allocation, not 32-bit/EXEC IRAM

static Z80Bridge ebtksCpuBridge;

static bool ebtksCcpServiceFetch(void *context,uint16_t pc,const uint8_t *ram) {
  return static_cast<CpmSafePoint::Gate *>(context)->fetch(pc,ram);
}

void portOut(uint8_t p, uint8_t v) {
  int i;  
  if (CpmWifiModem::isPort(p)) {CpmWifiModem::writePort(p,v);return;}
  pOut[p] = v;                     //Save a copy of byte sent to IO Port
  switch (p) {
    case GPP:                                 //Write to GPIO Port
      pIn[GPP] = v;                           //Copy to read buffer so it can be read.     
      for(i = 0; i<8; i++){
        if(PortA[i] > -1) digitalWrite(PortA[i], bitRead(v, i));        
      }
      break;
    case GPP+1:                               //GPIO Direction Port
      pIn[GPP + 1] = v;                       //Copy to read buffer so it can be read.
      for(i = 0; i<8; i++){
        if(PortA[i] > -1) {
          if (bitRead(v, 0) == 1) pinMode(PortA[i], OUTPUT); else pinMode(PortA[i], INPUT_PULLUP);
        }       
      }
      
      break;
    case GPP+2:
      pIn[GPP + 2] = v;                       //Copy to read buffer so it can be read.
      for(i = 0; i<8; i++){
        if(PortB[i] > -1) digitalWrite(PortB[i], bitRead(v, i));        
      }      
      break;
    case GPP+3:                               //GPP Direction Port
      pIn[GPP + 3] = v;                       //Copy to read buffer so it can be read.
           
      for(i = 0; i<8; i++){
        if(PortB[i] > -1) {
          if (bitRead(v, 0) == 1) pinMode(PortB[i], OUTPUT); else pinMode(PortB[i], INPUT_PULLUP);
        }       
      }
      break;
    case UART_PORT: {                         // UART write
      // The Z80 can produce output much faster than Wi-Fi can deliver it.
      // This is a single-producer/single-consumer ring (CPU -> ConsoleTask).
      // Never overwrite unread bytes when a browser-only session is busy:
      // give the console task time to drain the ring.  A connected Telnet
      // client used to provide that pacing accidentally through TCP writes.
      uint16_t next = txInPtr + 1;
      if (next >= sizeof(txBuf)) next = 0;
      while (next == txOutPtr && RUN) vTaskDelay(1);
      if (next != txOutPtr) {
        txBuf[txInPtr] = v;
        txInPtr = next;
      }
      bitWrite(pIn[UART_LSR], 6, 1);          // Transmitter ready
      break;
    }

    case DCMD:
      pIn[p] = v;
      if (sdfound == true) {
        switch (v) {
          case 1: diskRead();         pIn[DCMD] = 0; break;
          case 2: diskWrite();        pIn[DCMD] = 0; break;
          case 4: SDfileOpen();       pIn[DCMD] = 0; break;
          case 5: SDfileRead();       pIn[DCMD] = 0; break;
          case 6: SDprintDir();       pIn[DCMD] = 0; break;
          case 7: SDsetPath();        pIn[DCMD] = 0; break;
          case 8: RUN = false; PC = 0; pIn[DCMD] = 0; break;  // request cold restart from main loop; no bootstrap inside opcode
          default: Serial.printf("Unknown Disk Command: 0x%.2X\n\r", v); break;
        }
      }
      break;
    case DPARM:   break;
    case DPARM+1: break;
    case DPARM+2: break;
    case DPARM+3: break;
    case DPARM+4: break;
    case DPARM+5: break;
    default:                                    //Just print data
      //Serial.printf("Port Out: %.2X  Val: %.2X PC: %.4X\n\r", p, v, PC);
      break;
  }
}


//*********************************************************************************************
//****                             Z80 input port handler                                  ****
//*********************************************************************************************
uint8_t portIn(uint8_t p) {
  if (CpmWifiModem::isPort(p))return CpmWifiModem::readPort(p);
  switch (p) {
    case GPP:        //Read GPIO port if Direction be is 0 (Input)     
      for(int i = 0; i <8; i++){
        if (PortA[i] > -1 && bitRead(pOut[GPP + 1], 0) == 0) bitWrite(pIn[GPP], 0, digitalRead(PortA[i]));        
      }
      break;
    case GPP+2:      //Read GPIO port if Direction be is 0 (Input)
      for(int i = 0; i <8; i++){
        if (PortB[i] > -1 && bitRead(pOut[GPP + 2], 0) == 0) bitWrite(pIn[GPP], 0, digitalRead(PortB[i]));        
      }
      break;

    case UART_PORT:   //Read virtual 8250 UART LSR
      bitWrite(pIn[UART_LSR], 0, 0);  //clear bit to say char has been read
      break;

    case UART_LSR:    //Check for received char
      //Nothing to do here as this is handled by the serial task
      break;
      
    case DCMD:
      break;

    default:
      Serial.printf("Port In: %.2X  Val: %.2X PC: %.4X\n\r", p, pIn[p], PC);
      break;
  }
  return (pIn[p]);                            //Return buffered port input
}



static uint8_t ebtksBridgePortIn(void *,uint16_t port) {
  // EBTKS exposes 8-bit virtual peripheral ports (DCMD, UART, GPIO).
  // Physical Z80 addresses are 16 bit; high bits are not decoded here.
  return portIn((uint8_t)port);
}
static void ebtksBridgePortOut(void *,uint16_t port,uint8_t value) {
  portOut((uint8_t)port,value);
}

inline void ebtksCpuColdReset() {
  // Called by Arduino loop while RUN is false, after bootstrap/mount setup.
  z80BridgeColdReset(&ebtksCpuBridge);
  PC=0;
}

void CPUTask(void *) {
  // Initialize the same emulated PIO/UART registers as the old CPU task.
  portOut(GPP+1,255);
  portOut(GPP,0);
  portOut(GPP+3,255);
  portOut(GPP+2,0);
  pIn[UART_LSR]=0x40;
  // No ESP32-local SD exists in EBTKS: storage belongs to Teensy.
  z80BridgeInit(&ebtksCpuBridge,RAM,ebtksBridgePortIn,
                ebtksBridgePortOut,nullptr,&RUN);
  // One tiny Gate on the ordinary heap, allocated ONCE in the existing CPU
  // task (after Wi-Fi starts). Bridge.device is free: both EBTKS port I/O
  // callbacks ignore it. No new TFT BSS, early startup hook, or IRAM allocator.
  auto *gate=new (std::nothrow) CpmSafePoint::Gate();
  if(gate) {
    ebtksCpuBridge.device=gate;
    ebtksCpuBridge.service_fetch=ebtksCcpServiceFetch;
  } else {
    Serial.println("CP/M safe-point probe disabled: gate allocation failed");
  }
  cpu_t=true;
  Serial.println("redcode/Z80 CPU task ready (Teensy storage unaffected)");

  for (;;) {
    if (!RUN) { vTaskDelay(1); continue; }
    ebtksCpuBridge.trap_enabled=bpOn;
    ebtksCpuBridge.trap_address=BP;
    // A small bounded slice prevents SPI/UART/Web UI starvation. SingleStep
    // executes exactly one whole instruction, not one T-state.
    Z80BridgeStop reason=z80BridgeRun(&ebtksCpuBridge,SingleStep?1:12000);
    PC=Z80_PC(ebtksCpuBridge.cpu); // shared diagnostics/warm-boot reader
    if(reason==Z80_BRIDGE_SERVICE) {
      // This runs ONLY on the Z80 task at the verified CCP D382 opcode,
      // after a blank Enter. No other task touches Z80 registers or RAM.
      const bool ok=CpmServiceProbe::execute(&ebtksCpuBridge);
      if (auto *gate=static_cast<CpmSafePoint::Gate *>(ebtksCpuBridge.device))
        gate->complete(ok);
      PC=Z80_PC(ebtksCpuBridge.cpu);
    } else if(reason==Z80_BRIDGE_TRAP || reason==Z80_BRIDGE_HALT) {
      RUN=false;
    } else if(reason==Z80_BRIDGE_EXTERNAL_STOP) {
      RUN=false;
    }
    if(SingleStep)RUN=false;
    taskYIELD();
  }
}
