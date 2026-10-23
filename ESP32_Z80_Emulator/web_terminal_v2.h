#pragma once
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include "z80_debug.h"
#include "crt_display_page.h"
#include "cpm_media_page.h"
#include "ebtks_firmware_options.h"

// Z80 browser terminal.  Telnet remains a raw byte stream; browser terminal
// emulation is deliberately client-side so CP/M and BIOS console behaviour are
// unchanged.
#if EBTKS_WEB_TERMINAL_ENABLE
AsyncWebSocket z80WebSocket("/z80ws");

// Reserve contiguous 8-bit RAM for AsyncTCP/Wi-Fi and existing clients.
// The modem already guards TCP dial; protect the reverse startup order too,
// BEFORE the WebSocket handshake allocates its new client.
// Observed: Telnet connected has 63 KiB free / 16 KiB largest8;
// modem started first has 55 KiB free / 11 KiB largest8.
inline bool webTerminalHasMemoryReserve() {
  return ESP.getFreeHeap() >= 56U * 1024U &&
         heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= 14U * 1024U;
}

static const char EBTKS_TERM_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS Z80 Terminal</title><style>
body{background:#202124;color:#e8eaed;font-family:monospace;margin:0;padding:1rem}.toolbar{display:flex;align-items:center;gap:.45rem;flex-wrap:wrap;margin-bottom:.65rem}button,select{font:inherit}.spacer{flex:1}.hint{color:#9aa0a6;font-size:.9em}#term{background:#000;color:#a5ff31;padding:12px;min-height:24em;white-space:pre;overflow:auto;outline:none;line-height:1.16;letter-spacing:0;font-family:"DejaVu Sans Mono","Noto Sans Mono","Liberation Mono",monospace}#term .rv{background:#a5ff31;color:#000}#term .ul{text-decoration:underline}#term .bd{font-weight:bold}#term .cursor{outline:1px solid currentColor;animation:blink 1.1s step-end infinite}@keyframes blink{50%{outline-color:transparent}}#diag{background:#111;color:#ddd;padding:12px;min-height:10em;max-height:20em;overflow:auto;white-space:pre-wrap;display:none}#diagControls{display:none;margin:.5rem 0}#status2{color:#fbbc04}
</style></head><body>
<div class="toolbar"><b>EBTKS Z80 / CP/M terminal</b><label>Mode <select id="termMode"><option value="raw">Raw</option><option value="vt100">VT100</option><option value="hp86">HP-86 / 82900</option></select></label><button onclick="clearTerm()">Clear</button><button onclick="resetZ80()">Reset Z80</button><button onclick="toggleDiag()">Diagnostics</button><button onclick="retryTerminalConnection()">Reconnect</button><button onclick="location.href='/cpmmedia'">CP/M Media</button><button onclick="location.href='/crt'">HP-85 CRT</button><button onclick="location.href='/'">EBTKS Home</button><span class="spacer"></span><span id="state">connecting...</span></div>
<div class="hint">The ESP32 console answers VT100 DA/DSR probes for both TFT and Web (external Telnet clients answer for themselves). VT100 supports the ST7796 TFT's 16 ANSI colors, CP437 characters and DEC line graphics at 80x24. HP-86 retains original HP CP/M keys and Soroc/HP controls. Telnet remains raw.</div>
<pre id="term" tabindex="0"></pre><div id="status2"></div><div id="diagControls"><button onclick="refreshDiag()">Refresh diagnostics</button><button onclick="clearDiag()">Clear diagnostics</button></div><pre id="diag"></pre><script>
const term=document.getElementById('term'),state=document.getElementById('state'),diag=document.getElementById('diag'),diagControls=document.getElementById('diagControls'),modeSel=document.getElementById('termMode'),status2=document.getElementById('status2');
let ws,diagTimer=null,wsFailures=0,retryTimer=null,mode=localStorage.getItem('ebtksTermMode')||'vt100';const W=80,H=24,ESC=27;
let scr,row,col,savedRow,savedCol,savedAttr,scrollTop,scrollBottom,attr,insertMode,cursorVisible,autoWrap,originMode,g0,g1,gl,parserState,seq,charTarget,hpSeq,hpAddr,hpDialect,rawText='',unsupported=new Set();
// RGB equivalents of the physical TFT's RGB565 16-color palette.
// Index 16 means TFT default foreground #a5ff31 or black background.
const ansiPalette=['#000000','#ad0000','#00aa00','#adaa00',
  '#0000ad','#ad00ad','#00aaad','#adaaad','#525552','#ff0000',
  '#00ff00','#ffff00','#0000ff','#ff00ff','#00ffff','#ffffff'];
const cp437High='ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒ'+
  'áíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐'+
  '└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀'+
  'αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
function cp437(b){return b>=128?cp437High[b-128]:String.fromCharCode(b)}
function defaultAttr(){return {inv:false,ul:false,bd:false,fg:16,bg:16}}
function copyAttr(x){return {inv:x.inv,ul:x.ul,bd:x.bd,fg:x.fg,bg:x.bg}}
function cell(ch=' '){return {ch,...copyAttr(attr)}}function blankRow(){return Array.from({length:W},()=>cell())}
function resetModel(clear=true){attr=defaultAttr();savedAttr=defaultAttr();if(clear||!scr)scr=Array.from({length:H},blankRow);row=0;col=0;savedRow=0;savedCol=0;scrollTop=0;scrollBottom=H-1;insertMode=false;cursorVisible=true;autoWrap=true;originMode=false;g0='ascii';g1='ascii';gl=0;parserState='normal';seq='';charTarget=0;hpSeq='';hpAddr=[];hpDialect='auto'}
function clearTerm(){rawText='';resetModel(true);unsupported.clear();status2.textContent='';render()}function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function cellStyle(x,cur){
  let fg=x.fg,bg=x.bg;
  if(x.bd){if(fg<8)fg+=8;else if(fg===16)fg=10}
  let front=fg===16?'#a5ff31':ansiPalette[fg&15];
  let back=bg===16?'#000000':ansiPalette[bg&15];
  if(x.inv){const temp=front;front=back;back=temp}
  const classes=(x.ul?'ul ':'')+(x.bd?'bd ':'')+(cur?'cursor':'');
  const styled=classes!==''||front!=='#a5ff31'||back!=='#000000';
  return {key:classes+'|'+front+'|'+back,classes,front,back,styled};
}
function render(){
  if(mode==='raw'){term.textContent=rawText;term.scrollTop=term.scrollHeight;return}
  let out='';
  for(let r=0;r<H;r++){
    let group='',last=null;
    const flush=()=>{
      if(!group)return;
      if(last.styled)
        out+='<span class="'+last.classes.trim()+'" style="color:'+last.front+
          ';background-color:'+last.back+'">'+escHtml(group)+'</span>';
      else out+=escHtml(group);
      group='';
    };
    for(let c=0;c<W;c++){
      const x=scr[r][c],cur=cursorVisible&&r===row&&c===Math.min(col,W-1);
      const next=cellStyle(x,cur);
      if(last&&last.key!==next.key)flush();
      group+=x.ch;last=next;
    }
    flush();if(r!==H-1)out+='\n';
  }
  term.innerHTML=out;
}
function hex(b){return b.toString(16).toUpperCase().padStart(2,'0')}function safeText(s){let o='';for(let i=0;i<s.length;i++){const b=s.charCodeAt(i)&255;o+=(b>=32&&b<127)?String.fromCharCode(b):'\\x'+hex(b)}return o}
function trace(tag,s){const text=tag+': '+safeText(s);if(unsupported.has(text))return;unsupported.add(text);status2.textContent='Unsupported terminal sequence(s): '+unsupported.size+' (see Diagnostics)';if(ws&&ws.readyState===WebSocket.OPEN)ws.send('TERMTRACE '+text.slice(0,180))}
function clamp(){row=Math.max(originMode?scrollTop:0,Math.min(originMode?scrollBottom:H-1,row));col=Math.max(0,Math.min(W,col))}function scrollUp(n=1){while(n-->0){scr.splice(scrollTop,1);scr.splice(scrollBottom,0,blankRow())}}function scrollDown(n=1){while(n-->0){scr.splice(scrollBottom,1);scr.splice(scrollTop,0,blankRow())}}function lineFeed(){if(row===scrollBottom)scrollUp();else row=Math.min(H-1,row+1)}function reverseIndex(){if(row===scrollTop)scrollDown();else row=Math.max(0,row-1)}
function putChar(ch){if(col>=W){if(autoWrap){col=0;lineFeed()}else col=W-1}if(insertMode){scr[row].splice(col,0,cell());scr[row].length=W}scr[row][col]=cell(ch);if(col<W)col++}
function ctl(b,hp=false){if(b===0||b===7)return true;if(b===8){col=Math.max(0,col-1);return true}if(b===9){col=Math.min(W,(Math.floor(col/8)+1)*8);return true}if(b===10){lineFeed();return true}if(b===13){col=0;return true}if(!hp&&b===14){gl=1;return true}if(!hp&&b===15){gl=0;return true}if(hp&&b===11){row=Math.max(0,row-1);return true}if(hp&&b===12){col=Math.min(W,col+1);return true}return false}
function eraseDisplay(p){if(p===2||p===3){for(let r=0;r<H;r++)scr[r]=blankRow();return}if(p===0){eraseLine(0);for(let r=row+1;r<H;r++)scr[r]=blankRow();return}if(p===1){eraseLine(1);for(let r=0;r<row;r++)scr[r]=blankRow()}}function eraseLine(p){if(p===2){scr[row]=blankRow();return}if(p===0){for(let c=Math.min(col,W-1);c<W;c++)scr[row][c]=cell();return}if(p===1){for(let c=0;c<=Math.min(col,W-1);c++)scr[row][c]=cell()}}function eraseChars(n){for(let i=0;i<n&&col+i<W;i++)scr[row][col+i]=cell()}function insChars(n){n=Math.min(n,W-col);while(n-->0){scr[row].splice(col,0,cell());scr[row].pop()}}function delChars(n){n=Math.min(n,W-col);while(n-->0){scr[row].splice(col,1);scr[row].push(cell())}}function insLines(n){if(row<scrollTop||row>scrollBottom)return;while(n-->0){scr.splice(row,0,blankRow());scr.splice(scrollBottom+1,1)}}function delLines(n){if(row<scrollTop||row>scrollBottom)return;while(n-->0){scr.splice(row,1);scr.splice(scrollBottom,0,blankRow())}}
// DEC Special Graphics mapping follows EbtksVt100::Screen::decGlyph().
const lineMap={'_':' ','`':'♦','a':'▒','b':'T','c':'F','d':'R',
  'e':'L','f':'°','g':'±','h':'#','i':'#','j':'┘','k':'┐',
  'l':'┌','m':'└','n':'┼','o':'─','p':'─','q':'─','r':'─',
  's':'─','t':'├','u':'┤','v':'┴','w':'┬','x':'│',
  'y':'≤','z':'≥','{':'π','|':'!','}':'£','~':'·'};
function vtPrintable(b){let ch=cp437(b),cs=gl?g1:g0;
  if(b<128&&cs==='line'&&Object.prototype.hasOwnProperty.call(lineMap,String.fromCharCode(b)))
    ch=lineMap[String.fromCharCode(b)];
  else if(b<128&&cs==='uk'&&b===35)ch='£';
  putChar(ch)
}function csiNums(body){let t=body;if(t[0]==='?'||t[0]==='>')t=t.slice(1);if(t==='')return[];return t.split(';').map(x=>x===''?0:(parseInt(x,10)||0))}
function csiHandle(body,fin){const priv=body.startsWith('?'),sec=body.startsWith('>'),p=csiNums(body),one=(i=0,d=1)=>(p[i]||d);switch(fin){case'A':row-=one();break;case'B':row+=one();break;case'C':col+=one();break;case'D':col-=one();break;case'E':row+=one();col=0;break;case'F':row-=one();col=0;break;case'G':case'`':col=one(0,1)-1;break;case'd':row=(originMode?scrollTop:0)+one(0,1)-1;break;case'H':case'f':row=(originMode?scrollTop:0)+one(0,1)-1;col=one(1,1)-1;break;case'J':eraseDisplay(p[0]||0);break;case'K':eraseLine(p[0]||0);break;case'@':insChars(one());break;case'P':delChars(one());break;case'X':eraseChars(one());break;case'L':insLines(one());break;case'M':delLines(one());break;case'S':scrollUp(one());break;case'T':scrollDown(one());break;case'r':scrollTop=Math.max(0,one(0,1)-1);scrollBottom=Math.min(H-1,one(1,H)-1);if(scrollBottom<=scrollTop){scrollTop=0;scrollBottom=H-1}row=originMode?scrollTop:0;col=0;break;case's':savedRow=row;savedCol=col;savedAttr=copyAttr(attr);break;case'u':row=savedRow;col=savedCol;attr=copyAttr(savedAttr);break;case'm':{
  const a=p.length?p:[0];
  for(let i=0;i<a.length;i++){
    const x=a[i];
    if(x===0)attr=defaultAttr();
    else if(x===1)attr.bd=true;
    else if(x===4)attr.ul=true;
    else if(x===7)attr.inv=true;
    else if(x===22)attr.bd=false;
    else if(x===24)attr.ul=false;
    else if(x===27)attr.inv=false;
    else if(x===39)attr.fg=16;
    else if(x===49)attr.bg=16;
    else if(x>=30&&x<=37)attr.fg=x-30;
    else if(x>=40&&x<=47)attr.bg=x-40;
    else if(x>=90&&x<=97)attr.fg=x-90+8;
    else if(x>=100&&x<=107)attr.bg=x-100+8;
    else if(x===38||x===48){
      // Match TFT behavior: skip unsupported 256-color / truecolor groups.
      if(a[i+1]===5)i+=2;
      else if(a[i+1]===2)i+=4;
    }
  }
  break}case'h':case'l':{const on=fin==='h';for(const x of p){if(!priv&&x===4)insertMode=on;else if(priv&&x===25)cursorVisible=on;else if(priv&&x===7)autoWrap=on;else if(priv&&x===6){originMode=on;row=on?scrollTop:0;col=0}}break}case'n':case'c':break;default:trace('VT100','CSI '+body+fin);break}clamp()}
function processVT(b){if(parserState==='normal'){if(b===ESC){parserState='esc';return}if(ctl(b))return;if(b>=32&&b!==127)vtPrintable(b);return}if(parserState==='esc'){parserState='normal';if(b===91){seq='';parserState='csi';return}if(b===55){savedRow=row;savedCol=col;savedAttr=copyAttr(attr);return}if(b===56){row=savedRow;col=savedCol;attr=copyAttr(savedAttr);clamp();return}if(b===68){lineFeed();return}if(b===77){reverseIndex();return}if(b===69){lineFeed();col=0;return}if(b===99){resetModel(true);return}if(b===90)return; /* The ESP32 console router owns DECID. */if(b===40||b===41){charTarget=b===41?1:0;parserState='charset';return}if(b===35){parserState='lineattr';return}if(b===60||b===61||b===62)return;trace('VT100','ESC '+String.fromCharCode(b));return}if(parserState==='charset'){parserState='normal';let cs=null;if(b===48)cs='line';else if(b===66)cs='ascii';else if(b===65)cs='uk';else if(b===49||b===50)cs='ascii';else{trace('VT100','charset '+(charTarget?'G1 ':'G0 ')+String.fromCharCode(b));return}if(charTarget)g1=cs;else g0=cs;return}if(parserState==='lineattr'){parserState='normal';if(b===51||b===52||b===53||b===54)return;if(b===56){for(let r=0;r<H;r++)for(let c=0;c<W;c++)scr[r][c]=cell('E');row=0;col=0;return}trace('VT100','ESC # '+String.fromCharCode(b));return}if(parserState==='csi'){if(b>=0x40&&b<=0x7e){const body=seq;seq='';parserState='normal';csiHandle(body,String.fromCharCode(b));return}if(seq.length<48)seq+=String.fromCharCode(b);else{trace('VT100','overlong CSI');parserState='normal';seq=''}}}
function hpEnhanced(s){let m=s.match(/^&a([+-]?\d+)([cC])([+-]?\d+)([rRyY])$/);if(m){col=parseInt(m[1],10);row=parseInt(m[3],10);clamp();return true}m=s.match(/^&a([+-]?\d+)([rRyY])([+-]?\d+)([cC])$/);if(m){row=parseInt(m[1],10);col=parseInt(m[3],10);clamp();return true}m=s.match(/^&a([+-]?\d+)([cC])$/);if(m){col=parseInt(m[1],10);clamp();return true}m=s.match(/^&a([+-]?\d+)([rRyY])$/);if(m){row=parseInt(m[1],10);clamp();return true}return false}
function hpEscByte(b){const ch=String.fromCharCode(b);if(hpSeq){hpSeq+=ch;if(hpSeq.length>32){trace('HP86','ESC '+hpSeq);hpSeq='';parserState='normal';return}if(/[A-Z]/.test(ch)){const s=hpSeq;hpSeq='';parserState='normal';if(!hpEnhanced(s))trace('HP86','ESC '+s);return}return}parserState='normal';switch(ch){case'A':row--;break;case'B':row++;break;case'C':col++;break;case'D':col--;break;case'H':case'h':row=0;col=0;break;case'F':case'f':row=H-1;col=0;break;case'S':hpDialect='hp262';scrollUp();break;case'T':if(hpDialect==='soroc')eraseLine(0);else{hpDialect='hp262';scrollDown()}break;case'J':hpDialect='hp262';eraseDisplay(0);break;case'K':hpDialect='hp262';eraseLine(0);break;case'P':hpDialect='hp262';delChars(1);break;case'Q':hpDialect='hp262';insertMode=true;break;case'R':hpDialect='hp262';insertMode=false;break;case'E':hpDialect='hp262';for(let r=0;r<H;r++)scr[r]=blankRow();row=0;col=0;break;case'Y':if(hpDialect==='soroc')eraseDisplay(0);return;case'Z':return;case'*':hpDialect='soroc';for(let r=0;r<H;r++)scr[r]=blankRow();row=0;col=0;break;case'=':hpDialect='soroc';parserState='hpaddr1';hpAddr=[];return;case'&':hpDialect='hp262';hpSeq='&';parserState='hpesc';return;default:trace('HP86','ESC '+ch);return}clamp()}
function processHP(b){if(parserState==='normal'){if(b===ESC){parserState='hpesc';hpSeq='';return}if(ctl(b,true))return;if(b>=32&&b!==127)putChar(cp437(b));return}if(parserState==='hpaddr1'){hpAddr=[b];parserState='hpaddr2';return}if(parserState==='hpaddr2'){hpAddr.push(b);row=Math.max(0,Math.min(H-1,hpAddr[0]-32));col=Math.max(0,Math.min(W-1,hpAddr[1]-32));parserState='normal';return}if(parserState==='hpesc')hpEscByte(b)}
function putRaw(b){if(b===13)return;if(b===10)rawText+='\n';else if(b===8||b===127)rawText=rawText.slice(0,-1);else if(b===12||b===26)rawText='';else if(b>=32||b===9)rawText+=cp437(b);if(rawText.length>60000)rawText=rawText.slice(-40000)}let renderPending=false;function scheduleRender(){if(renderPending)return;renderPending=true;requestAnimationFrame(()=>{renderPending=false;render()})}function feed(data){for(const b of data){if(mode==='raw')putRaw(b);else if(mode==='vt100')processVT(b);else processHP(b)}scheduleRender()}function setMode(m){mode=m;localStorage.setItem('ebtksTermMode',mode);modeSel.value=mode;rawText='';resetModel(true);unsupported.clear();status2.textContent='';render();term.focus()}modeSel.addEventListener('change',()=>setMode(modeSel.value));
async function resetZ80(){state.textContent='resetting...';try{const r=await fetch('/z80/reset',{method:'POST'});if(!r.ok)throw new Error();clearTerm();state.textContent='reset requested';setTimeout(()=>term.focus(),250)}catch(e){state.textContent='reset error'}}async function refreshDiag(){try{const r=await fetch('/z80debug',{cache:'no-store'});diag.textContent=await r.text();diag.scrollTop=diag.scrollHeight}catch(e){diag.textContent='diagnostic fetch failed'}}async function clearDiag(){try{await fetch('/z80debug-clear',{method:'POST'});await refreshDiag()}catch(e){diag.textContent='diagnostic clear failed'}}function toggleDiag(){const show=diag.style.display==='none';diag.style.display=show?'block':'none';diagControls.style.display=show?'block':'none';if(show){refreshDiag();diagTimer=setInterval(refreshDiag,1000)}else{if(diagTimer){clearInterval(diagTimer);diagTimer=null}term.focus()}}
function sendBytes(a){if(ws&&ws.readyState===WebSocket.OPEN)ws.send(new Uint8Array(a))}function sendText(s){sendBytes(Array.from(s,x=>x.charCodeAt(0)&255))}function retryTerminalConnection(){if(retryTimer!==null){clearTimeout(retryTimer);retryTimer=null}if(ws&&(ws.readyState===WebSocket.OPEN||ws.readyState===WebSocket.CONNECTING))return;wsFailures=0;connect()}
function connect(){retryTimer=null;ws=new WebSocket(`ws://${location.host}/z80ws`);ws.binaryType='arraybuffer';ws.onopen=()=>{wsFailures=0;state.textContent='connected';term.focus()};ws.onclose=()=>{if(++wsFailures<3){state.textContent='disconnected; retrying...';retryTimer=setTimeout(connect,5000*wsFailures)}else state.textContent='Terminal unavailable (possibly low ESP32 memory). Close unused sessions and click Reconnect.'};ws.onerror=()=>state.textContent='connection error';ws.onmessage=e=>{if(typeof e.data==='string')feed(Uint8Array.from(e.data,x=>x.charCodeAt(0)&255));else feed(new Uint8Array(e.data))}}
const hpKey={ArrowLeft:0x9F,ArrowRight:0xAA,ArrowUp:0xA3,ArrowDown:0xA4,Delete:0x88,Insert:0x9E,PageUp:0x91,PageDown:0xA9,F1:0x80,F2:0x81,F3:0x82,F4:0x83,F5:0xA1,F6:0xA2,F7:0x9C,F8:0x84,F9:0x85,F10:0x86,F11:0x87,F12:0xA5,F13:0xAC,F14:0x93};
term.addEventListener('keydown',e=>{if(!ws||ws.readyState!==WebSocket.OPEN)return;let s=null,a=null;if(e.key==='Enter')a=[13];else if(e.key==='Backspace')a=[8];else if(e.key==='Tab')a=[9];else if(e.key==='Escape')a=[27];else if(mode==='hp86'&&hpKey[e.key]!==undefined)a=[hpKey[e.key]];else if(mode!=='hp86'&&e.key==='ArrowUp')s='\x1b[A';else if(mode!=='hp86'&&e.key==='ArrowDown')s='\x1b[B';else if(mode!=='hp86'&&e.key==='ArrowRight')s='\x1b[C';else if(mode!=='hp86'&&e.key==='ArrowLeft')s='\x1b[D';else if(mode!=='hp86'&&e.key==='Home')s='\x1b[H';else if(mode!=='hp86'&&e.key==='Delete')s='\x1b[3~';else if(e.ctrlKey&&!e.altKey&&e.key.length===1){const c=e.key.toUpperCase().charCodeAt(0);if(c>=64&&c<=95)a=[c&31]}else if(e.key.length===1)a=[e.key.charCodeAt(0)&255];if(s!==null){sendText(s);e.preventDefault()}else if(a){sendBytes(a);e.preventDefault()}});document.addEventListener('click',e=>{if(e.target===term)term.focus()});modeSel.value=mode;resetModel(true);render();connect();
</script></body></html>)HTML";

#endif // EBTKS_WEB_TERMINAL_ENABLE: discard browser HTML and websocket

inline void consoleRxEnqueue(uint8_t c){uint16_t next=rxInPtr+1;if(next>=sizeof(rxBuf))next=0;if(next==rxOutPtr)return;rxBuf[rxInPtr]=c;rxInPtr=next;}
#if EBTKS_WEB_TERMINAL_ENABLE
inline void webTerminalWrite(const uint8_t *data, size_t len) {
  if (!len) return;

  // AsyncWebSocket::binaryAll() queues frames, it does NOT throttle the Z80.
  // Rapid DIR/Multiplan redraws can fill the library's finite queue and close
  // the browser socket, silently losing the remainder of the screen.  Wait
  // for queue space before consuming more bytes from the console TX ring.
  // The CPU's UART output ring now blocks when full, completing end-to-end
  // flow control even with no Telnet client attached.
  const TickType_t start = xTaskGetTickCount();
  while (z80WebSocket.count()) {
    if (z80WebSocket.availableForWriteAll()) {
      z80WebSocket.binaryAll(data, len);
      return;
    }

    // A dead browser must not freeze CP/M forever. TCP will normally drain
    // the queue quickly; only abandon the client after a prolonged stall.
    if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(10000)) {
      z80DebugLog("Z80 web terminal stalled for 10 s; disconnecting slow client\n");
      z80WebSocket.closeAll();
      return;
    }
    vTaskDelay(1);
  }
}
#else
inline void webTerminalWrite(const uint8_t*,size_t) {}
#endif

inline void webTerminalCleanupClients(){
#if EBTKS_WEB_TERMINAL_ENABLE
  z80WebSocket.cleanupClients();
#endif
}

inline void webTerminalRegister(AsyncWebServer &server){
#if EBTKS_WEB_TERMINAL_ENABLE
  // ESPAsyncWebServer 3.6.0 checks this before allocating a socket client.
  // Its API returns HTTP 401 for a rejected upgrade (no new WebSocket).
  z80WebSocket.handleHandshake([](AsyncWebServerRequest*) {
    return webTerminalHasMemoryReserve();
  });
  z80WebSocket.onEvent([](AsyncWebSocket*,AsyncWebSocketClient*,AwsEventType type,void *arg,uint8_t *data,size_t len){
    if(type!=WS_EVT_DATA)return;AwsFrameInfo *info=static_cast<AwsFrameInfo*>(arg);if(!info||info->index!=0||info->len!=len||!info->final)return;
    if(info->opcode==WS_TEXT){if(len>=10&&len<=220&&!memcmp(data,"TERMTRACE ",10)){char msg[232];memcpy(msg,data,len);msg[len]=0;z80DebugLog("Web terminal %s\n",msg+10);}return;}
    if(info->opcode!=WS_BINARY)return;for(size_t i=0;i<len;++i){uint8_t c=data[i];if(c==127)c=8;consoleRxEnqueue(c);}
  });
  server.on("/z80",HTTP_GET,[](AsyncWebServerRequest *r){
    if (!webTerminalHasMemoryReserve()) {
      r->send(503,"text/plain",
              "Web Terminal unavailable: ESP32 8-bit RAM is low. "
              "Exit MODEM or close other sessions, then reload.");
      return;
    }
    // This page is ~17 KiB. Never materialize it as a heap String, which
    // becomes fragile when live SPI read-ahead retains its cache and worker.
    AsyncWebServerResponse *response=r->beginResponse(
      200,"text/html; charset=utf-8",
      reinterpret_cast<const uint8_t *>(EBTKS_TERM_PAGE),
      sizeof(EBTKS_TERM_PAGE)-1U);
    if(!response) {
      r->send(503,"text/plain","Terminal page response allocation failed");
      return;
    }
    response->addHeader("Cache-Control","no-store");
    r->send(response);
  });
#endif // EBTKS_WEB_TERMINAL_ENABLE
  // HP CRT, Media Manager, Z80 control and diagnostics are always present.
  server.on("/crt",HTTP_GET,ebtksServeCrtDisplay);
  server.on("/z80/reset",HTTP_POST,[](AsyncWebServerRequest *r){RUN=false;r->send(200,"application/json","{\"ok\":true}");});
  server.on("/z80debug",HTTP_GET,[](AsyncWebServerRequest *r){r->send(200,"text/plain",z80DebugSnapshot());});
  server.on("/z80debug-clear",HTTP_POST,[](AsyncWebServerRequest *r){z80DebugClear();r->send(200,"application/json","{\"ok\":true}");});
  server.on("/z80/status",HTTP_GET,[](AsyncWebServerRequest *r){String s="{\"target\":\"ESP32-D0WD-V3\",\"milestone\":2,\"z80_running\":";s+=RUN?"true":"false";s+=",\"teensy_storage\":";s+=sdfound?"true":"false";s+="}";r->send(200,"application/json",s);});
  cpmMediaRegister(server);
#if EBTKS_WEB_TERMINAL_ENABLE
  server.addHandler(&z80WebSocket);
#endif
}
