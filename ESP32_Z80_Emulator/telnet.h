#pragma once
#include "globals.h"

#if EBTKS_TELNET_ENABLE

// Preserve the legacy dedicated TelnetTask path for controlled A/B testing.
// Default: the existing EBTKSWebTask accepts incoming clients, keeping the
// TCP port 23 listener available WITHOUT allocating an extra 3072-byte task
// stack. The same ConsoleTask owns the active Telnet I/O as before.
#ifndef EBTKS_TELNET_SHARED_WORKER
#define EBTKS_TELNET_SHARED_WORKER 1
#endif
#if EBTKS_TELNET_SHARED_WORKER != 0 && EBTKS_TELNET_SHARED_WORKER != 1
#error "EBTKS_TELNET_SHARED_WORKER must be 0 or 1"
#endif

inline void telnetBegin(){
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Telnet port 23 listener started");
  Serial.print("Use 'telnet ");
  Serial.print(WiFi.localIP());
  Serial.println(" 23' to connect");
  telnet_t=true;
}

inline void telnetPoll(){
  if(!telnetServer.hasClient())return;
  WiFiClient incoming=telnetServer.available();
  if(!incoming)return;
  if(!serverClient || !serverClient.connected()){
    if(serverClient)serverClient.stop();
    serverClient=incoming;
    Serial.print("New Telnet client @ ");
    Serial.println(serverClient.remoteIP());
    // Same original negotiation; consoleTask still handles IAC on receive.
    serverClient.write(255); serverClient.write(251); serverClient.write(1);
    serverClient.write(255); serverClient.write(251); serverClient.write(3);
    serverClient.write(255); serverClient.write(252); serverClient.write(34);
  }else incoming.stop();
}

// Legacy independent worker can be restored with the compile flag
// -DEBTKS_TELNET_SHARED_WORKER=0. Do not compile this loop otherwise.
#if !EBTKS_TELNET_SHARED_WORKER
inline void TelnetTask(void*){
  telnetBegin();
  for(;;){
    telnetPoll();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#endif

#endif // EBTKS_TELNET_ENABLE
