#pragma once
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Keep the original EBTKS websocket at /ws for HP/Teensy traffic.
// The Z80 terminal has its own independent websocket endpoint.
AsyncWebSocket z80WebSocket("/z80ws");

static const char EBTKS_TERM_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS Z80 Terminal</title><style>
body{background:#202124;color:#e8eaed;font-family:monospace;margin:0;padding:1rem}
#term{background:#000;color:#9ef59e;padding:12px;min-height:25em;white-space:pre-wrap;outline:none}
button{margin-right:.5rem}</style></head><body>
<div><b>EBTKS Z80 / CP/M terminal</b> <button onclick="clearTerm()">Clear</button> <button onclick="location.href='/'">EBTKS Home</button> <span id="state">connecting...</span></div>
<pre id="term" tabindex="0"></pre><script>
const term=document.getElementById('term'),state=document.getElementById('state');let ws;
function clearTerm(){term.textContent=''}
function putByte(c){if(c===13)return;if(c===10)term.textContent+='\n';else if(c===8||c===127)term.textContent=term.textContent.slice(0,-1);else if(c===12||c===26)clearTerm();else if(c>=32||c===9)term.textContent+=String.fromCharCode(c);if(term.textContent.length>60000)term.textContent=term.textContent.slice(-40000);window.scrollTo(0,document.body.scrollHeight)}
function connect(){ws=new WebSocket(`ws://${location.host}/z80ws`);ws.binaryType='arraybuffer';ws.onopen=()=>{state.textContent='connected';term.focus()};ws.onclose=()=>{state.textContent='disconnected';setTimeout(connect,1000)};ws.onerror=()=>state.textContent='error';ws.onmessage=e=>{if(typeof e.data==='string'){for(let i=0;i<e.data.length;i++)putByte(e.data.charCodeAt(i)&255)}else{for(const c of new Uint8Array(e.data))putByte(c)}}}
term.addEventListener('keydown',e=>{if(!ws||ws.readyState!==WebSocket.OPEN)return;let b=null;if(e.key==='Enter')b=[13];else if(e.key==='Backspace')b=[8];else if(e.key==='Tab')b=[9];else if(e.key.length===1)b=[e.key.charCodeAt(0)&255];if(b){ws.send(new Uint8Array(b));e.preventDefault()}});document.addEventListener('click',()=>term.focus());connect();
</script></body></html>)HTML";

inline void consoleRxEnqueue(uint8_t c) {
  uint16_t next = rxInPtr + 1;
  if (next >= sizeof(rxBuf)) next = 0;
  if (next == rxOutPtr) return;
  rxBuf[rxInPtr] = c;
  rxInPtr = next;
}

inline void webTerminalWrite(const uint8_t *data, size_t len) {
  if (len && z80WebSocket.count()) z80WebSocket.binaryAll(data, len);
}

inline void webTerminalRegister(AsyncWebServer &server) {
  z80WebSocket.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type != WS_EVT_DATA) return;
    AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
    if (!info || info->opcode != WS_BINARY) return;
    for (size_t i=0;i<len;++i) {
      uint8_t c=data[i];
      if(c==127)c=8;
      consoleRxEnqueue(c);
    }
  });

  server.on("/z80", HTTP_GET, [](AsyncWebServerRequest *r){
    r->send(200,"text/html",EBTKS_TERM_PAGE);
  });

  server.on("/z80/status", HTTP_GET, [](AsyncWebServerRequest *r){
    String s="{\"target\":\"ESP32-D0WD-V3\",\"milestone\":1,\"z80_running\":";
    s+=RUN?"true":"false";
    s+="}";
    r->send(200,"application/json",s);
  });

  server.addHandler(&z80WebSocket);
}
