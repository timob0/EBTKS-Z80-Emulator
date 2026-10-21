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
.toolbar{display:flex;align-items:center;gap:.45rem;flex-wrap:wrap;margin-bottom:.65rem}
button,select{font:inherit}.spacer{flex:1}.hint{color:#9aa0a6;font-size:.9em}
#term{background:#000;color:#9ef59e;padding:12px;min-height:24em;white-space:pre;overflow:auto;outline:none;line-height:1.16;letter-spacing:0}
#term .rv{background:#9ef59e;color:#000}#term .ul{text-decoration:underline}#term .bd{font-weight:bold}
#term .cursor{outline:1px solid currentColor;animation:blink 1.1s step-end infinite}@keyframes blink{50%{outline-color:transparent}}
#diag{background:#111;color:#ddd;padding:12px;min-height:10em;max-height:20em;overflow:auto;white-space:pre-wrap;display:none}
#diagControls{display:none;margin:.5rem 0}#status2{color:#fbbc04}
</style></head><body>
<div class="toolbar"><b>EBTKS Z80 / CP/M terminal</b>
<label>Mode <select id="termMode"><option value="raw">Raw</option><option value="vt100">VT100</option><option value="hp86">HP-86 / 82900</option></select></label>
<button onclick="clearTerm()">Clear</button><button onclick="resetZ80()">Reset Z80</button><button onclick="toggleDiag()">Diagnostics</button><button onclick="location.href='/cpmmedia'">CP/M Media</button><button onclick="location.href='/crt'">HP-85 CRT</button><button onclick="location.href='/'">EBTKS Home</button>
<span class="spacer"></span><span id="state">connecting...</span></div>
<div class="hint">Structured modes emulate an 80x24 terminal. HP-86 mode accepts the HP 262x/Soroc control subset used by HP's CP/M environment. Telnet remains raw.</div>
<pre id="term" tabindex="0"></pre><div id="status2"></div>
<div id="diagControls"><button onclick="refreshDiag()">Refresh diagnostics</button><button onclick="clearDiag()">Clear diagnostics</button></div>
<pre id="diag"></pre><script>
const term=document.getElementById('term'),state=document.getElementById('state'),diag=document.getElementById('diag'),diagControls=document.getElementById('diagControls'),modeSel=document.getElementById('termMode'),status2=document.getElementById('status2');
let ws,diagTimer=null,mode=localStorage.getItem('ebtksTermMode')||'vt100';
const W=80,H=24,ESC=27;
let scr,row,col,savedRow,savedCol,scrollTop,scrollBottom,attr,insertMode,cursorVisible,g0,parserState,seq,hpSeq,hpAddr,hpDialect,rawText='',unsupported=new Set();
function cell(){return{ch:' ',inv:false,ul:false,bd:false}}
function resetModel(clear=true){
  if(clear||!scr)scr=Array.from({length:H},()=>Array.from({length:W},cell));
  row=0;col=0;savedRow=0;savedCol=0;scrollTop=0;scrollBottom=H-1;attr={inv:false,ul:false,bd:false};insertMode=false;cursorVisible=true;g0='ascii';parserState='normal';seq='';hpSeq='';hpAddr=[];hpDialect='auto';
}
function clearTerm(){rawText='';resetModel(true);unsupported.clear();status2.textContent='';render()}
function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function render(){
  if(mode==='raw'){term.textContent=rawText;term.scrollTop=term.scrollHeight;return}
  let out='';
  for(let r=0;r<H;r++){
    let plain=''; const flush=()=>{if(plain){out+=escHtml(plain);plain=''}};
    for(let c=0;c<W;c++){
      const x=scr[r][c],cur=cursorVisible&&r===row&&c===Math.min(col,W-1);
      if(!x.inv&&!x.ul&&!x.bd&&!cur){plain+=x.ch;continue}
      flush();let cls=[];if(x.inv)cls.push('rv');if(x.ul)cls.push('ul');if(x.bd)cls.push('bd');if(cur)cls.push('cursor');out+='<span class="'+cls.join(' ')+'">'+escHtml(x.ch)+'</span>';
    }
    flush();if(r!==H-1)out+='\n';
  }
  term.innerHTML=out;
}
function trace(tag,s){
  const text=tag+': '+s;if(unsupported.has(text))return;unsupported.add(text);status2.textContent='Unsupported terminal sequence(s): '+unsupported.size+' (see Diagnostics)';
  if(ws&&ws.readyState===WebSocket.OPEN)ws.send('TERMTRACE '+text.slice(0,180));
}
function clamp(){row=Math.max(0,Math.min(H-1,row));col=Math.max(0,Math.min(W,col))}
function blankRow(){return Array.from({length:W},cell)}
function scrollUp(n=1){while(n-->0){scr.splice(scrollTop,1);scr.splice(scrollBottom,0,blankRow())}}
function scrollDown(n=1){while(n-->0){scr.splice(scrollBottom,1);scr.splice(scrollTop,0,blankRow())}}
function lineFeed(){if(row===scrollBottom)scrollUp();else row=Math.min(H-1,row+1)}
function reverseIndex(){if(row===scrollTop)scrollDown();else row=Math.max(0,row-1)}
function putChar(ch){
  if(col>=W){col=0;lineFeed()}
  if(insertMode){scr[row].splice(col,0,cell());scr[row].length=W}
  scr[row][col]={ch,inv:attr.inv,ul:attr.ul,bd:attr.bd};col++;
}
function ctl(b,hp=false){
  if(b===0||b===7)return true;
  if(b===8){col=Math.max(0,col-1);return true}
  if(b===9){col=Math.min(W,((Math.floor(col/8)+1)*8));return true}
  if(b===10){lineFeed();return true}
  if(b===13){col=0;return true}
  if(hp&&b===11){row=Math.max(0,row-1);return true}
  if(hp&&b===12){col=Math.min(W,col+1);return true}
  return false;
}
function eraseDisplay(p){
  if(p===2||p===3){for(let r=0;r<H;r++)scr[r]=blankRow();if(p===2){row=0;col=0}return}
  if(p===0){eraseLine(0);for(let r=row+1;r<H;r++)scr[r]=blankRow();return}
  if(p===1){eraseLine(1);for(let r=0;r<row;r++)scr[r]=blankRow()}
}
function eraseLine(p){
  if(p===2){scr[row]=blankRow();return}
  if(p===0){for(let c=Math.min(col,W-1);c<W;c++)scr[row][c]=cell();return}
  if(p===1){for(let c=0;c<=Math.min(col,W-1);c++)scr[row][c]=cell()}
}
function eraseChars(n){for(let i=0;i<n&&col+i<W;i++)scr[row][col+i]=cell()}
function insChars(n){n=Math.min(n,W-col);while(n-->0){scr[row].splice(col,0,cell());scr[row].pop()}}
function delChars(n){n=Math.min(n,W-col);while(n-->0){scr[row].splice(col,1);scr[row].push(cell())}}
function insLines(n){if(row<scrollTop||row>scrollBottom)return;while(n-->0){scr.splice(row,0,blankRow());scr.splice(scrollBottom+1,1)}}
function delLines(n){if(row<scrollTop||row>scrollBottom)return;while(n-->0){scr.splice(row,1);scr.splice(scrollBottom,0,blankRow())}}
const lineMap={'j':'┘','k':'┐','l':'┌','m':'└','n':'┼','q':'─','t':'├','u':'┤','v':'┴','w':'┬','x':'│'};
function vtPrintable(b){let ch=String.fromCharCode(b);if(g0==='line'&&lineMap[ch])ch=lineMap[ch];putChar(ch)}
function csiNums(body){if(body.startsWith('?'))body=body.slice(1);if(body==='')return[];return body.split(';').map(x=>x===''?0:(parseInt(x,10)||0))}
function csiHandle(body,fin){
  const priv=body.startsWith('?'),p=csiNums(body),one=(i=0,d=1)=>(p[i]||d);
  switch(fin){
    case'A':row-=one();break;case'B':row+=one();break;case'C':col+=one();break;case'D':col-=one();break;
    case'E':row+=one();col=0;break;case'F':row-=one();col=0;break;case'G':case'`':col=one(0,1)-1;break;case'd':row=one(0,1)-1;break;
    case'H':case'f':row=one(0,1)-1;col=one(1,1)-1;break;
    case'J':eraseDisplay(p[0]||0);break;case'K':eraseLine(p[0]||0);break;
    case'@':insChars(one());break;case'P':delChars(one());break;case'X':eraseChars(one());break;case'L':insLines(one());break;case'M':delLines(one());break;
    case'S':scrollUp(one());break;case'T':scrollDown(one());break;
    case'r':scrollTop=Math.max(0,one(0,1)-1);scrollBottom=Math.min(H-1,one(1,H)-1);if(scrollBottom<=scrollTop){scrollTop=0;scrollBottom=H-1}row=scrollTop;col=0;break;
    case's':savedRow=row;savedCol=col;break;case'u':row=savedRow;col=savedCol;break;
    case'm':{
      const a=p.length?p:[0];for(const x of a){if(x===0){attr={inv:false,ul:false,bd:false}}else if(x===1)attr.bd=true;else if(x===4)attr.ul=true;else if(x===7)attr.inv=true;else if(x===22)attr.bd=false;else if(x===24)attr.ul=false;else if(x===27)attr.inv=false;}break;
    }
    case'h':case'l':{
      const on=fin==='h';for(const x of p){if(!priv&&x===4)insertMode=on;else if(priv&&x===25)cursorVisible=on;}break;
    }
    case'n':if(ws&&ws.readyState===WebSocket.OPEN){if(p[0]===5)sendText('\x1b[0n');else if(p[0]===6)sendText(`\x1b[${row+1};${Math.min(col,W-1)+1}R`)}break;
    case'c':if(ws&&ws.readyState===WebSocket.OPEN)sendText('\x1b[?1;0c');break;
    default:trace('VT100','CSI '+body+fin);break;
  }
  clamp();
}
function processVT(b){
  if(parserState==='normal'){
    if(b===ESC){parserState='esc';return}if(ctl(b))return;if(b>=32&&b!==127)vtPrintable(b);return;
  }
  if(parserState==='esc'){
    parserState='normal';
    if(b===91){seq='';parserState='csi';return}if(b===55){savedRow=row;savedCol=col;return}if(b===56){row=savedRow;col=savedCol;clamp();return}
    if(b===68){lineFeed();return}if(b===77){reverseIndex();return}if(b===69){lineFeed();col=0;return}if(b===99){resetModel(true);return}
    if(b===40||b===41){parserState='charset';return}if(b===61||b===62)return;
    trace('VT100','ESC '+String.fromCharCode(b));return;
  }
  if(parserState==='charset'){parserState='normal';if(b===48)g0='line';else if(b===66)g0='ascii';else trace('VT100','charset '+String.fromCharCode(b));return}
  if(parserState==='csi'){
    if(b>=0x40&&b<=0x7e){const body=seq;seq='';parserState='normal';csiHandle(body,String.fromCharCode(b));return}
    if(seq.length<40)seq+=String.fromCharCode(b);else{trace('VT100','overlong CSI');parserState='normal';seq=''}
  }
}
function hpEnhanced(s){
  // HP 262x screen-relative addressing: ESC & a <column> c <row> Y,
  // plus accepted row-first forms. Coordinates are zero based.
  let m=s.match(/^&a([+-]?\d+)([cC])([+-]?\d+)([rRyY])$/);
  if(m){col=parseInt(m[1],10);row=parseInt(m[3],10);clamp();return true}
  m=s.match(/^&a([+-]?\d+)([rRyY])([+-]?\d+)([cC])$/);
  if(m){row=parseInt(m[1],10);col=parseInt(m[3],10);clamp();return true}
  m=s.match(/^&a([+-]?\d+)([cC])$/);if(m){col=parseInt(m[1],10);clamp();return true}
  m=s.match(/^&a([+-]?\d+)([rRyY])$/);if(m){row=parseInt(m[1],10);clamp();return true}
  return false;
}
function hpEscByte(b){
  const ch=String.fromCharCode(b);
  if(hpSeq){
    hpSeq+=ch;
    if(hpSeq.length>32){trace('HP86','ESC '+hpSeq);hpSeq='';parserState='normal';return}
    if(/[A-Z]/.test(ch)){
      const s=hpSeq;hpSeq='';parserState='normal';if(!hpEnhanced(s))trace('HP86','ESC '+s);return;
    }
    return;
  }
  parserState='normal';
  switch(ch){
    case'A':row--;break;case'B':row++;break;case'C':col++;break;case'D':col--;break;case'H':case'h':row=0;col=0;break;
    case'F':case'f':row=H-1;col=0;break;case'S':hpDialect='hp262';scrollUp();break;case'T':if(hpDialect==='soroc')eraseLine(0);else{hpDialect='hp262';scrollDown()}break;
    case'J':hpDialect='hp262';eraseDisplay(0);break;case'K':hpDialect='hp262';eraseLine(0);break;case'P':hpDialect='hp262';delChars(1);break;case'Q':hpDialect='hp262';insertMode=true;break;case'R':hpDialect='hp262';insertMode=false;break;
    case'E':hpDialect='hp262';resetModel(true);break;case'Y':if(hpDialect==='soroc')eraseDisplay(0);return;case'Z':return;
    case'*':hpDialect='soroc';resetModel(true);hpDialect='soroc';break;
    case'=':hpDialect='soroc';parserState='hpaddr1';hpAddr=[];return;
    case'&':hpDialect='hp262';hpSeq='&';parserState='hpesc';return;
    default:trace('HP86','ESC '+ch);return;
  }
  clamp();
}
function processHP(b){
  if(parserState==='normal'){
    if(b===ESC){parserState='hpesc';hpSeq='';return}if(ctl(b,true))return;if(b>=32&&b!==127)putChar(String.fromCharCode(b));return;
  }
  if(parserState==='hpaddr1'){hpAddr=[b];parserState='hpaddr2';return}
  if(parserState==='hpaddr2'){hpAddr.push(b);row=Math.max(0,Math.min(H-1,hpAddr[0]-32));col=Math.max(0,Math.min(W-1,hpAddr[1]-32));parserState='normal';return}
  if(parserState==='hpesc'){hpEscByte(b)}
}
function putRaw(b){
  if(b===13)return;if(b===10)rawText+='\n';else if(b===8||b===127)rawText=rawText.slice(0,-1);else if(b===12||b===26)rawText='';else if(b>=32||b===9)rawText+=String.fromCharCode(b);if(rawText.length>60000)rawText=rawText.slice(-40000)
}
function feed(data){for(const b of data){if(mode==='raw')putRaw(b);else if(mode==='vt100')processVT(b);else processHP(b)}render()}
function setMode(m){mode=m;localStorage.setItem('ebtksTermMode',mode);modeSel.value=mode;rawText='';resetModel(true);unsupported.clear();status2.textContent='';render();term.focus()}
modeSel.addEventListener('change',()=>setMode(modeSel.value));
async function resetZ80(){state.textContent='resetting...';try{const r=await fetch('/z80/reset',{method:'POST'});if(!r.ok)throw new Error();clearTerm();state.textContent='reset requested';setTimeout(()=>term.focus(),250)}catch(e){state.textContent='reset error'}}
async function refreshDiag(){try{const r=await fetch('/z80debug',{cache:'no-store'});diag.textContent=await r.text();diag.scrollTop=diag.scrollHeight}catch(e){diag.textContent='diagnostic fetch failed'}}
async function clearDiag(){try{await fetch('/z80debug-clear',{method:'POST'});await refreshDiag()}catch(e){diag.textContent='diagnostic clear failed'}}
function toggleDiag(){const show=diag.style.display==='none';diag.style.display=show?'block':'none';diagControls.style.display=show?'block':'none';if(show){refreshDiag();diagTimer=setInterval(refreshDiag,1000)}else{if(diagTimer){clearInterval(diagTimer);diagTimer=null}term.focus()}}
function sendBytes(a){if(ws&&ws.readyState===WebSocket.OPEN)ws.send(new Uint8Array(a))}
function sendText(s){sendBytes(Array.from(s,x=>x.charCodeAt(0)&255))}
function connect(){ws=new WebSocket(`ws://${location.host}/z80ws`);ws.binaryType='arraybuffer';ws.onopen=()=>{state.textContent='connected';term.focus()};ws.onclose=()=>{state.textContent='disconnected';setTimeout(connect,1000)};ws.onerror=()=>state.textContent='error';ws.onmessage=e=>{if(typeof e.data==='string')feed(Uint8Array.from(e.data,x=>x.charCodeAt(0)&255));else feed(new Uint8Array(e.data))}}
term.addEventListener('keydown',e=>{
  if(!ws||ws.readyState!==WebSocket.OPEN)return;let s=null,a=null;
  if(e.key==='Enter')a=[13];else if(e.key==='Backspace')a=[8];else if(e.key==='Tab')a=[9];
  else if(e.key==='ArrowUp')s=mode==='hp86'?'\x1bA':'\x1b[A';else if(e.key==='ArrowDown')s=mode==='hp86'?'\x1bB':'\x1b[B';else if(e.key==='ArrowRight')s=mode==='hp86'?'\x1bC':'\x1b[C';else if(e.key==='ArrowLeft')s=mode==='hp86'?'\x1bD':'\x1b[D';
  else if(e.key==='Home')s=mode==='hp86'?'\x1bH':'\x1b[H';else if(e.key==='Delete')s=mode==='hp86'?'\x1bP':'\x1b[3~';else if(e.key==='Escape')a=[27];else if(e.key.length===1)a=[e.key.charCodeAt(0)&255];
  if(s!==null){sendText(s);e.preventDefault()}else if(a){sendBytes(a);e.preventDefault()}
});
document.addEventListener('click',e=>{if(e.target===term)term.focus()});modeSel.value=mode;resetModel(true);render();connect();
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
    if (!info || info->index != 0 || info->len != len || !info->final) return;

    if (info->opcode == WS_TEXT) {
      // Browser-side terminal emulation reports unsupported control sequences
      // as text frames. Keep them in the in-memory web diagnostic log only;
      // they must never enter the Z80 input stream or a hardware UART.
      if (len >= 10 && len <= 220 && !memcmp(data, "TERMTRACE ", 10)) {
        char msg[232];
        memcpy(msg, data, len);
        msg[len] = 0;
        z80DebugLog("Web terminal %s\n", msg + 10);
      }
      return;
    }

    if (info->opcode != WS_BINARY) return;
    for (size_t i=0;i<len;++i) {
      uint8_t c=data[i];
      if(c==127)c=8;
      consoleRxEnqueue(c);
    }
  });

  server.on("/z80", HTTP_GET, [](AsyncWebServerRequest *r){
    // Serve the ~17-KiB terminal HTML from flash, not a heap String.
    // In low heap, an AsyncBasicResponse may fail or reset TCP before
    // replying; CRT and Media Manager already use this safe overload.
    AsyncWebServerResponse *response=r->beginResponse(
      200,"text/html; charset=utf-8",
      reinterpret_cast<const uint8_t *>(EBTKS_TERM_PAGE),
      sizeof(EBTKS_TERM_PAGE)-1U);
    if(!response){
      r->send(503,"text/plain","Z80 terminal response unavailable");
      return;
    }
    response->addHeader("Cache-Control","no-store");
    r->send(response);
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
