#pragma once

#include <ESPAsyncWebServer.h>

// Browser front end for the original EBTKS /ws CRT service. /ws is the JSON
// transport endpoint; /crt is the human-facing HP-85 display.
static const char EBTKS_CRT_DISPLAY_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS HP-85 Remote CRT</title>
<style>
body{margin:0;padding:1rem;background:#252525;color:#eaeaea;text-align:center;font-family:Verdana,sans-serif}
main{max-width:760px;margin:0 auto}
#bezel{display:inline-block;background:#111;border:12px solid #444;border-radius:12px;padding:10px;box-shadow:0 0 18px #000}
#screen{display:block;background:#000;width:512px;height:384px;max-width:100%;image-rendering:pixelated}
.controls{margin:1rem auto;display:flex;flex-wrap:wrap;gap:.5rem;justify-content:center;align-items:center}
button,select{border:0;border-radius:.3rem;background:#1fa3ec;color:#fff;padding:.55rem .8rem;font-size:.95rem;cursor:pointer}
select{background:#444}.state{font-family:monospace;margin:.5rem}.ok{color:#7dff7d}.bad{color:#ff8080}
.small{font-size:.82rem;color:#aaa;line-height:1.4}
</style>
</head>
<body><main>
<h2>HP-85 Remote CRT</h2>
<div id="bezel"><canvas id="screen" width="512" height="384"></canvas></div>
<div id="state" class="state">connecting...</div>
<div class="controls">
<button onclick="location.href='/'">EBTKS Home</button>
<button onclick="location.href='/z80'">Z80 / CP/M Terminal</button>
<button onclick="location.href='/updater'">Firmware Updater</button>
<label>Refresh <select id="rate"><option value="250">250 ms</option><option value="500" selected>500 ms</option><option value="1000">1 s</option><option value="2000">2 s</option></select></label>
</div>
<p class="small">This page uses the original EBTKS <code>/ws</code> JSON service. The current restoration mirrors the HP-85 32 x 16 alpha display; graphics mode remains disabled until its larger framebuffer transport is bounded safely.</p>
</main>
<script>
'use strict';
const canvas=document.getElementById('screen');
const ctx=canvas.getContext('2d');
const state=document.getElementById('state');
const rate=document.getElementById('rate');
let ws=null,waiting=false,timer=null;

function setState(text,ok){state.textContent=text;state.className='state '+(ok===true?'ok':ok===false?'bad':'');}
function schedule(){clearTimeout(timer);timer=setTimeout(requestFrame,Number(rate.value)||500);}
function requestFrame(){
  if(!ws||ws.readyState!==WebSocket.OPEN||waiting){schedule();return;}
  waiting=true;
  ws.send(JSON.stringify({get_alpha:{}}));
}
function decodeBase64(s){
  const b=atob(s||'');
  const out=new Uint8Array(b.length);
  for(let i=0;i<b.length;i++)out[i]=b.charCodeAt(i)&255;
  return out;
}
function render(bytes){
  ctx.fillStyle='#000';ctx.fillRect(0,0,512,384);
  ctx.fillStyle='#fff';ctx.font='20px monospace';ctx.textBaseline='top';
  const cw=16,ch=24;
  for(let row=0;row<16;row++){
    for(let col=0;col<32;col++){
      const v=bytes[row*32+col]||0;
      const code=v&0x7f;
      const x=col*cw,y=row*ch;
      if(code>=32&&code<=126)ctx.fillText(String.fromCharCode(code),x+2,y+1);
      if(v&0x80){ctx.fillRect(x+1,y+19,13,2);}
    }
  }
}
function connect(){
  const proto=location.protocol==='https:'?'wss://':'ws://';
  ws=new WebSocket(proto+location.host+'/ws');
  ws.onopen=()=>{waiting=false;setState('connected',true);requestFrame();};
  ws.onclose=()=>{waiting=false;setState('disconnected - reconnecting...',false);clearTimeout(timer);setTimeout(connect,1000);};
  ws.onerror=()=>setState('websocket error',false);
  ws.onmessage=(ev)=>{
    let obj;try{obj=JSON.parse(ev.data);}catch(e){return;}
    if(!obj.hpText)return; // /ws is shared; ignore replies requested by other pages.
    waiting=false;
    try{
      const bytes=decodeBase64(obj.hpText.image);
      if(bytes.length!==512)throw new Error('expected 512 bytes, got '+bytes.length);
      render(bytes);
      setState('connected - HP-85 alpha display',true);
    }catch(e){setState('display decode error: '+e.message,false);}
    schedule();
  };
}
rate.addEventListener('change',()=>{if(!waiting)schedule();});
render(new Uint8Array(512));
connect();
</script>
</body></html>
)HTML";

inline void ebtksServeCrtDisplay(AsyncWebServerRequest *request)
{
  request->send(200, "text/html", EBTKS_CRT_DISPLAY_PAGE);
}
