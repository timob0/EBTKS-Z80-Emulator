#pragma once
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "z80_debug.h"
#include "crt_display_page.h"
#include "cpm_media_page.h"

// Keep the original EBTKS websocket at /ws for HP/Teensy traffic.
// The Z80 terminal has its own independent websocket endpoint.
AsyncWebSocket z80WebSocket("/z80ws");

static const char EBTKS_TERM_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS Z80 Terminal</title><style>
body{background:#202124;color:#e8eaed;font-family:monospace;margin:0;padding:1rem}
#term{background:#000;color:#9ef59e;padding:12px;min-height:25em;white-space:pre-wrap;outline:none}
#diag{background:#111;color:#ddd;padding:12px;min-height:10em;max-height:20em;overflow:auto;white-space:pre-wrap;display:none}
button{margin-right:.5rem}</style></head><body>
<div><b>EBTKS Z80 / CP/M terminal</b> <button onclick="clearTerm()">Clear</button> <button onclick="resetZ80()">Reset Z80</button> <button onclick="toggleDiag()">Diagnostics</button> <button onclick="location.href='/cpmmedia'">CP/M Media</button> <button onclick="location.href='/crt'">HP-85 CRT</button> <button onclick="location.href='/'">EBTKS Home</button> <span id="state">connecting...</span></div>
<pre id="term" tabindex="0"></pre>
<div id="diagControls" style="display:none"><button onclick="refreshDiag()">Refresh diagnostics</button><button onclick="clearDiag()">Clear diagnostics</button></div>
<pre id="diag"></pre><script>
const term=document.getElementById('term'),state=document.getElementById('state'),diag=document.getElementById('diag'),diagControls=document.getElementById('diagControls');let ws,diagTimer=null;
function clearTerm(){term.textContent=''}
async function resetZ80(){
  state.textContent='resetting...';
  try{
    const r=await fetch('/z80/reset',{method:'POST'});
    if(!r.ok)throw new Error('reset failed');
    term.textContent='';
    state.textContent='reset requested';
    setTimeout(()=>term.focus(),250);
  }catch(e){state.textContent='reset error'}
}
async function refreshDiag(){try{const r=await fetch('/z80debug',{cache:'no-store'});diag.textContent=await r.text();diag.scrollTop=diag.scrollHeight}catch(e){diag.textContent='diagnostic fetch failed'}}
async function clearDiag(){try{await fetch('/z80debug-clear',{method:'POST'});await refreshDiag()}catch(e){diag.textContent='diagnostic clear failed'}}
function toggleDiag(){const show=diag.style.display==='none';diag.style.display=show?'block':'none';diagControls.style.display=show?'block':'none';if(show){refreshDiag();diagTimer=setInterval(refreshDiag,1000)}else{if(diagTimer){clearInterval(diagTimer);diagTimer=null}term.focus()}}
function putByte(c){if(c===13)return;if(c===10)term.textContent+='\n';else if(c===8||c===127)term.textContent=term.textContent.slice(0,-1);else if(c===12||c===26)clearTerm();else if(c>=32||c===9)term.textContent+=String.fromCharCode(c);if(term.textContent.length>60000)term.textContent=term.textContent.slice(-40000);window.scrollTo(0,document.body.scrollHeight)}
function connect(){ws=new WebSocket(`ws://${location.host}/z80ws`);ws.binaryType='arraybuffer';ws.onopen=()=>{state.textContent='connected';term.focus()};ws.onclose=()=>{state.textContent='disconnected';setTimeout(connect,1000)};ws.onerror=()=>state.textContent='error';ws.onmessage=e=>{if(typeof e.data==='string'){for(let i=0;i<e.data.length;i++)putByte(e.data.charCodeAt(i)&255)}else{for(const c of new Uint8Array(e.data))putByte(c)}}}
term.addEventListener('keydown',e=>{if(!ws||ws.readyState!==WebSocket.OPEN)return;let b=null;if(e.key==='Enter')b=[13];else if(e.key==='Backspace')b=[8];else if(e.key==='Tab')b=[9];else if(e.key.length===1)b=[e.key.charCodeAt(0)&255];if(b){ws.send(new Uint8Array(b));e.preventDefault()}});document.addEventListener('click',e=>{if(e.target===term)term.focus()});connect();
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

  server.on("/crt", HTTP_GET, ebtksServeCrtDisplay);

  server.on("/z80/reset", HTTP_POST, [](AsyncWebServerRequest *r){
    // The main loop treats RUN=false as a virtual machine stop, reloads the
    // configured boot images and restarts the Z80 without resetting the ESP32
    // or disturbing the HP-85 side of EBTKS.
    RUN = false;
    r->send(200,"application/json","{\"ok\":true}");
  });

  // Keep diagnostics on top-level paths so older ESPAsyncWebServer variants
  // cannot confuse them with the /z80 terminal handler.
  server.on("/z80debug", HTTP_GET, [](AsyncWebServerRequest *r){
    r->send(200,"text/plain",z80DebugSnapshot());
  });

  server.on("/z80debug-clear", HTTP_POST, [](AsyncWebServerRequest *r){
    z80DebugClear();
    r->send(200,"application/json","{\"ok\":true}");
  });

  server.on("/z80/status", HTTP_GET, [](AsyncWebServerRequest *r){
    String s="{\"target\":\"ESP32-D0WD-V3\",\"milestone\":2,\"z80_running\":";
    s+=RUN?"true":"false";
    s+=",\"teensy_storage\":";
    s+=sdfound?"true":"false";
    s+="}";
    r->send(200,"application/json",s);
  });

  cpmMediaRegister(server);
  server.addHandler(&z80WebSocket);
}
