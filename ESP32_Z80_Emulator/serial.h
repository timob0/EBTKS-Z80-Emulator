#include "globals.h"
#include "console_ansi_probe.h"
#include "ebtks_network_diag.h"
#pragma once

void serialTask(void *parameter) {
  Serial.println("Console router task started");
  serial_t = true;
  // Fewer, larger WebSocket frames reduce queue pressure during screen redraws.
  uint8_t chunk[256];
  // One allocation-free virtual terminal probe shared by TFT and Web.
  // Process bytes in console order, even across 256-byte output chunks.
  EbtksAnsiProbe::Terminal ansiProbe;
  auto replyToGuest = [](const char *reply, size_t len) {
    // An external Telnet terminal normally supplies its own DA/DSR replies.
    // Do not inject a competing answer when one is connected.
    if (serverClient.connected()) return;
    // The 1024-byte input ring is shared with real keyboard input.
    // Never inject a partial ANSI response when the ring is full.
    const size_t cap = sizeof(rxBuf);
    const size_t freeSlots = (rxOutPtr + cap - rxInPtr - 1) % cap;
    if (len > freeSlots) return;
    for (size_t i = 0; i < len; ++i)
      consoleRxEnqueue((uint8_t)reply[i]);
  };

  // Minimal Telnet input parser.  Telnet option negotiation arrives on the
  // same TCP stream as terminal characters; those IAC command bytes must not
  // be passed to the emulated Z80 UART.
  enum TelnetRxState : uint8_t { TEL_DATA, TEL_IAC, TEL_IAC_OPTION, TEL_SB, TEL_SB_IAC };
  TelnetRxState telState = TEL_DATA;

  // In Telnet NVT mode, a carriage return is normally sent as CR NUL
  // (literal carriage return) or CR LF (new line).  CP/M wants a single CR
  // for Enter, so remember a received CR and discard a following NUL/LF.
  bool telnetAfterCr = false;

  for (;;) {
    size_t n = 0;
    while (txOutPtr != txInPtr && n < sizeof(chunk)) {
      chunk[n++] = txBuf[txOutPtr];
      uint16_t next = txOutPtr + 1;
      if (next >= sizeof(txBuf)) next = 0;
      txOutPtr = next;
    }

    if (n) {
      // Passive observation of CP/M's DECCKM ?1h/?1l. This updates the
      // physical HP-85 keyboard's VT100 cursor mode without changing bytes
      // sent to Telnet, WebSocket or the ST7796 terminal.
      Cpm85Input::observeConsoleOutput(chunk,n);
      if (serverClient.connected()) {
        // WiFiClient::write() may accept less than the requested length.
        // Do not discard the tail of a CP/M output burst if that happens.
        size_t sent = 0;
        bool writeStalled = false;
        uint32_t stallStartMs = 0;
        while (sent < n && serverClient.connected()) {
          size_t wrote = serverClient.write(chunk + sent, n - sent);
          if (wrote == 0) {
            EbtksNetDiag::telnetWriteZero(!writeStalled);
            if (!writeStalled) {
              stallStartMs = millis();
              writeStalled = true;
            }
            vTaskDelay(1);
            continue;
          }
          if (writeStalled) {
            EbtksNetDiag::telnetStallEnd(millis() - stallStartMs);
            writeStalled = false;
          }
          sent += wrote;
        }
        if (writeStalled)
          EbtksNetDiag::telnetStallEnd(millis() - stallStartMs);
      }
      // Mirror the same CP/M UART bytes as WebSocket/Telnet without doing
      // any TFT SPI I/O on this latency-sensitive console router task.
      EbtksTft::mirrorFeed(chunk, n);
      // The BBS can probe ANSI support while the physical TFT is the ONLY
      // active terminal. Replies enter CP/M CON: (so MODEM.COM sends them
      // onward through the emulated 8250), not the BBS TCP socket directly.
      // Browser JavaScript no longer sends duplicate responses.
      ansiProbe.feed(chunk, n, replyToGuest);
      webTerminalWrite(chunk, n);
    }

    while (serverClient.connected() && serverClient.available()) {
      uint8_t c = (uint8_t)serverClient.read();

      switch (telState) {
        case TEL_DATA:
          // Normalize Telnet's CR NUL / CR LF convention to one CP/M CR.
          // The CR itself was already queued on the previous byte.
          if (telnetAfterCr) {
            telnetAfterCr = false;
            if (c == 0 || c == 10) break;
          }

          if (c == 255) {                    // IAC
            telState = TEL_IAC;
          } else {
            if (c == 127) c = 8;
            consoleRxEnqueue(c);
            if (c == 13) telnetAfterCr = true;
          }
          break;

        case TEL_IAC:
          if (c == 255) {                    // Escaped literal 0xFF
            consoleRxEnqueue(255);
            telState = TEL_DATA;
          } else if (c == 250) {             // SB - subnegotiation
            telState = TEL_SB;
          } else if (c >= 251 && c <= 254) { // WILL/WONT/DO/DONT + option byte
            telState = TEL_IAC_OPTION;
          } else {
            telState = TEL_DATA;             // Other 2-byte Telnet command
          }
          break;

        case TEL_IAC_OPTION:
          // Consume option byte following WILL/WONT/DO/DONT.
          telState = TEL_DATA;
          break;

        case TEL_SB:
          if (c == 255) telState = TEL_SB_IAC;
          break;

        case TEL_SB_IAC:
          if (c == 240) telState = TEL_DATA; // IAC SE
          else if (c != 255) telState = TEL_SB;
          break;
      }
    }

    // Keys from the physical HP-85 are staged by the low-priority
    // Teensy UART polling worker; only this console task feeds the
    // virtual Z80 UART so it retains Web/Telnet ordering semantics.
    Cpm85Input::drain();
    if (rxOutPtr != rxInPtr && bitRead(pIn[UART_LSR], 0) == 0) {
      pIn[UART_PORT] = rxBuf[rxOutPtr];
      uint16_t next = rxOutPtr + 1;
      if (next >= sizeof(rxBuf)) next = 0;
      rxOutPtr = next;
      bitWrite(pIn[UART_LSR], 0, 1);
    }
    vTaskDelay(1);
  }
}

void outString(char buf[]) {
  int i = 0;
  while (buf[i] > 0) {
    uint16_t next = txInPtr + 1;
    if (next >= sizeof(txBuf)) next = 0;
    // outString shares the console ring with Z80 OUT (80h); don't silently
    // truncate diagnostic/utility strings when the output queue is full.
    if (next == txOutPtr) {
      if (!RUN) break;  // Z80 is being reset/stopped.
      vTaskDelay(1);
      continue;
    }
    txBuf[txInPtr] = (uint8_t)buf[i++];
    txInPtr = next;
  }
}
