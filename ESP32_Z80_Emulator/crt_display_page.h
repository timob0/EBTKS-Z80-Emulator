#pragma once

#include <ESPAsyncWebServer.h>

// Browser front end for the original EBTKS /ws CRT service. /ws is the JSON
// transport endpoint; /crt is the human-facing HP-80 display.
static const char EBTKS_CRT_DISPLAY_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS HP-80 Remote Display</title>
<style>
body{margin:0;padding:1rem;background:#252525;color:#eaeaea;text-align:center;font-family:Verdana,sans-serif}
main{max-width:1160px;margin:0 auto}
.displays{display:flex;flex-wrap:wrap;gap:1rem;justify-content:center;align-items:flex-start}
.panel{flex:1 1 520px;max-width:548px}
.panel h3{margin:.45rem 0}
.bezel{display:inline-block;background:#111;border:10px solid #444;border-radius:12px;padding:8px;box-shadow:0 0 18px #000;max-width:calc(100% - 36px)}
canvas{display:block;background:#000;width:512px;height:384px;max-width:100%;image-rendering:pixelated}
.controls{margin:.7rem auto;display:flex;flex-wrap:wrap;gap:.5rem;justify-content:center;align-items:center}
button,select{border:0;border-radius:.3rem;background:#1fa3ec;color:#fff;padding:.55rem .8rem;font-size:.95rem;cursor:pointer}
button:disabled{background:#555;color:#aaa;cursor:default}select{background:#444}
.state{font-family:monospace;margin:.45rem;min-height:1.2em}.ok{color:#7dff7d}.bad{color:#ff8080}
.small{font-size:.82rem;color:#aaa;line-height:1.4}
.nav{margin-top:1.1rem}
</style>
</head>
<body><main>
<h2>HP-80 Remote Display</h2>
<div class="displays">
  <section class="panel">
    <h3>Alpha Display <span class="small">(live)</span></h3>
    <div class="bezel"><canvas id="alphaScreen" width="512" height="384"></canvas></div>
    <div id="alphaState" class="state">connecting...</div>
    <div class="controls">
      <label>Refresh <select id="rate"><option value="250">250 ms</option><option value="500" selected>500 ms</option><option value="1000">1 s</option><option value="2000">2 s</option></select></label>
    </div>
  </section>

  <section class="panel">
    <h3>Graphics Snapshot</h3>
    <div class="bezel"><canvas id="graphScreen" width="512" height="384"></canvas></div>
    <div id="graphState" class="state">No snapshot yet</div>
    <div class="controls">
      <button id="captureButton" onclick="captureGraphics()">Capture Graphics</button>
      <button id="downloadButton" onclick="downloadGraphics()" disabled>Download PNG</button>
    </div>
  </section>
</div>

<div class="controls nav">
<button onclick="location.href='/'">EBTKS Home</button>
<button onclick="location.href='/z80'">Z80 / CP/M Terminal</button>
<button onclick="location.href='/updater'">Firmware Updater</button>
</div>
<p class="small">The alpha display updates continuously through the original EBTKS <code>/ws</code> service. Graphics capture is on demand: the HP-85 256 x 192 graphics RAM is frozen on the Teensy, transferred in 512-byte chunks, rendered at 2× scale, and can be saved as a PNG.</p>
</main>
<script>
'use strict';
const alphaCanvas=document.getElementById('alphaScreen');
const alphaCtx=alphaCanvas.getContext('2d');
const graphCanvas=document.getElementById('graphScreen');
const graphCtx=graphCanvas.getContext('2d');
const alphaState=document.getElementById('alphaState');
const graphState=document.getElementById('graphState');
const rate=document.getElementById('rate');
const captureButton=document.getElementById('captureButton');
const downloadButton=document.getElementById('downloadButton');

let ws=null;
let alphaWaiting=false;
let alphaTimer=null;
let capturing=false;
let graphGeneration=null;
let graphChunkCount=0;
let graphExpectedChunk=0;
let graphChunks=[];
let graphRequestCount=0;
let graphReady=false;

function setAlphaState(text,ok){alphaState.textContent=text;alphaState.className='state '+(ok===true?'ok':ok===false?'bad':'');}
function setGraphState(text,ok){graphState.textContent=text;graphState.className='state '+(ok===true?'ok':ok===false?'bad':'');}

function decodeBase64(s){
  const b=atob(s||'');
  const out=new Uint8Array(b.length);
  for(let i=0;i<b.length;i++)out[i]=b.charCodeAt(i)&255;
  return out;
}

function scheduleAlpha(){
  clearTimeout(alphaTimer);
  alphaTimer=setTimeout(requestAlpha,Number(rate.value)||500);
}

function requestAlpha(){
  if(capturing){scheduleAlpha();return;}
  if(!ws||ws.readyState!==WebSocket.OPEN||alphaWaiting){scheduleAlpha();return;}
  alphaWaiting=true;
  ws.send(JSON.stringify({get_alpha:{}}));
}

function renderAlpha(bytes){
  alphaCtx.fillStyle='#000';alphaCtx.fillRect(0,0,512,384);
  alphaCtx.fillStyle='#fff';alphaCtx.font='20px monospace';alphaCtx.textBaseline='top';
  const cw=16,ch=24;
  for(let row=0;row<16;row++){
    for(let col=0;col<32;col++){
      const v=bytes[row*32+col]||0;
      const code=v&0x7f;
      const x=col*cw,y=row*ch;
      if(code>=32&&code<=126)alphaCtx.fillText(String.fromCharCode(code),x+2,y+1);
      if(v&0x80)alphaCtx.fillRect(x+1,y+19,13,2);
    }
  }
}

function renderGraphics(bytes){
  if(bytes.length!==6144)throw new Error('expected 6144 graphics bytes, got '+bytes.length);
  const image=graphCtx.createImageData(512,384);
  const d=image.data;
  for(let y=0;y<192;y++){
    for(let xb=0;xb<32;xb++){
      const b=bytes[y*32+xb];
      for(let bit=0;bit<8;bit++){
        const on=(b&(0x80>>bit))!==0;
        const c=on?255:0;
        const x=(xb*8+bit)*2;
        const yy=y*2;
        for(let dy=0;dy<2;dy++){
          for(let dx=0;dx<2;dx++){
            const p=((yy+dy)*512+x+dx)*4;
            d[p]=c;d[p+1]=c;d[p+2]=c;d[p+3]=255;
          }
        }
      }
    }
  }
  graphCtx.putImageData(image,0,0);
}

function sendGraphRequest(){
  if(!capturing)return;
  if(!ws||ws.readyState!==WebSocket.OPEN){finishCaptureError('websocket not connected');return;}
  graphRequestCount++;
  if(graphRequestCount>36){finishCaptureError('could not synchronize graphics chunks');return;}
  ws.send(JSON.stringify({get_graph:{}}));
}

function captureGraphics(){
  if(capturing)return;
  if(!ws||ws.readyState!==WebSocket.OPEN){setGraphState('websocket not connected',false);return;}
  capturing=true;
  graphGeneration=null;
  graphChunkCount=0;
  graphExpectedChunk=0;
  graphChunks=[];
  graphRequestCount=0;
  captureButton.disabled=true;
  downloadButton.disabled=true;
  graphReady=false;
  clearTimeout(alphaTimer);
  setGraphState('Synchronizing snapshot...',null);
  sendGraphRequest();
}

function finishCaptureError(message){
  capturing=false;
  captureButton.disabled=false;
  downloadButton.disabled=!graphReady;
  setGraphState('Capture failed: '+message,false);
  scheduleAlpha();
}

function handleGraphFrame(frame){
  if(!capturing)return;
  if(frame.length!==515){finishCaptureError('expected 515-byte chunk frame, got '+frame.length);return;}

  const generation=frame[0];
  const index=frame[1];
  const count=frame[2];
  if(count!==12||index>=count){finishCaptureError('invalid chunk header');return;}

  // An aborted earlier capture may have left the Teensy midway through its
  // 12-chunk cycle. Ignore those old chunks until a new chunk 0 appears; that
  // chunk also freezes a new complete graphics snapshot on the Teensy.
  if(graphGeneration===null){
    if(index!==0){setGraphState('Synchronizing snapshot... chunk '+index+'/'+(count-1),null);sendGraphRequest();return;}
    graphGeneration=generation;
    graphChunkCount=count;
    graphExpectedChunk=0;
    graphChunks=new Array(count);
  }

  // If another client disturbed the sequence, resynchronize on its next chunk 0.
  if(generation!==graphGeneration||index!==graphExpectedChunk){
    graphGeneration=null;
    graphChunkCount=0;
    graphExpectedChunk=0;
    graphChunks=[];
    setGraphState('Resynchronizing snapshot...',null);
    if(index===0){
      graphGeneration=generation;
      graphChunkCount=count;
      graphChunks=new Array(count);
    }else{sendGraphRequest();return;}
  }

  graphChunks[index]=frame.slice(3);
  graphExpectedChunk=index+1;
  setGraphState('Capturing graphics '+graphExpectedChunk+'/'+graphChunkCount+'...',null);

  if(graphExpectedChunk<graphChunkCount){sendGraphRequest();return;}

  const raw=new Uint8Array(6144);
  for(let i=0;i<graphChunkCount;i++)raw.set(graphChunks[i],i*512);
  try{
    renderGraphics(raw);
    graphReady=true;
    setGraphState('Snapshot ready - 256 x 192 pixels',true);
  }catch(e){
    finishCaptureError(e.message);
    return;
  }
  capturing=false;
  captureButton.disabled=false;
  downloadButton.disabled=false;
  scheduleAlpha();
}

function downloadGraphics(){
  if(!graphReady)return;
  const now=new Date();
  const z=n=>String(n).padStart(2,'0');
  const name='HP85-graphics-'+now.getFullYear()+z(now.getMonth()+1)+z(now.getDate())+'-'+z(now.getHours())+z(now.getMinutes())+z(now.getSeconds())+'.png';
  graphCanvas.toBlob(blob=>{
    if(!blob)return;
    const url=URL.createObjectURL(blob);
    const a=document.createElement('a');
    a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
  },'image/png');
}

function connect(){
  const proto=location.protocol==='https:'?'wss://':'ws://';
  ws=new WebSocket(proto+location.host+'/ws');
  ws.onopen=()=>{alphaWaiting=false;setAlphaState('connected',true);requestAlpha();};
  ws.onclose=()=>{
    alphaWaiting=false;
    clearTimeout(alphaTimer);
    if(capturing){capturing=false;captureButton.disabled=false;}
    setAlphaState('disconnected - reconnecting...',false);
    setGraphState(graphReady?'Snapshot retained - websocket disconnected':'websocket disconnected',false);
    setTimeout(connect,1000);
  };
  ws.onerror=()=>setAlphaState('websocket error',false);
  ws.onmessage=(ev)=>{
    let obj;try{obj=JSON.parse(ev.data);}catch(e){return;}

    if(obj.hpText){
      alphaWaiting=false;
      try{
        const bytes=decodeBase64(obj.hpText.image);
        if(bytes.length!==512)throw new Error('expected 512 bytes, got '+bytes.length);
        renderAlpha(bytes);
        setAlphaState('connected - HP-85 alpha display',true);
      }catch(e){setAlphaState('display decode error: '+e.message,false);}
      if(!capturing)scheduleAlpha();
      return;
    }

    if(obj.hpGraph){
      let frame;
      try{frame=decodeBase64(obj.hpGraph.image);}catch(e){finishCaptureError('Base64 decode error');return;}
      handleGraphFrame(frame);
    }
  };
}

rate.addEventListener('change',()=>{if(!alphaWaiting&&!capturing)scheduleAlpha();});
renderAlpha(new Uint8Array(512));
renderGraphics(new Uint8Array(6144));
connect();
</script>
</body></html>
)HTML";

inline void ebtksServeCrtDisplay(AsyncWebServerRequest *request)
{
  request->send(200, "text/html", EBTKS_CRT_DISPLAY_PAGE);
}
