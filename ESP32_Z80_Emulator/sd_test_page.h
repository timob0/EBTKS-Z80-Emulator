#pragma once

#include <ESPAsyncWebServer.h>

// Browser-driven diagnostic for the existing ESP32 <-> Teensy JSON file
// service.  The Teensy firmware is intentionally left unchanged so this page
// tests the exact file-service implementation used by CP/M storage.
//
// SdFat on the ARM Teensy uses the POSIX-style flags:
//   O_RDWR  = 0x002
//   O_CREAT = 0x200
//   O_TRUNC = 0x400
// Therefore create/truncate/read-write is 0x602 (1538 decimal).
//
// The current Teensy protocol has a historical case mismatch: it dispatches
// on jdoc["write"] but reads the Base64 payload from jdoc["Write"]["data"].
// The test deliberately sends both keys, exactly like ebtks_storage.h.
static const char EBTKS_SD_TEST_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS SD File Service Test</title>
<style>
body{font-family:Verdana,sans-serif;background:#252525;color:#eaeaea;margin:0;padding:1rem}
main{max-width:820px;margin:1.5rem auto}
h2{margin-bottom:.3rem}
p{line-height:1.45}
button{border:0;border-radius:.3rem;background:#1fa3ec;color:white;padding:.65rem 1rem;font-size:1rem;margin:.35rem .35rem .35rem 0;cursor:pointer}
button:hover{background:#0e70a4}button:disabled{background:#666;cursor:default}
pre{background:#111;color:#d8ffd8;border:1px solid #555;border-radius:.3rem;padding:1rem;min-height:22rem;white-space:pre-wrap;word-break:break-word;overflow:auto}
.small{font-size:.85rem;color:#aaa}.pass{color:#7dff7d}.fail{color:#ff8080}
</style>
</head>
<body>
<main>
<h2>Teensy SD File Service Test</h2>
<p>This exercises the existing Teensy <code>open/read/write/lseek/close</code> JSON services through the ESP32 UART bridge. It creates and overwrites only <code>/cpm/ESPTEST.BIN</code>.</p>
<button id="run">Run Test</button>
<button onclick="location.href='/'">Back</button>
<p class="small">The final 8 KiB test uses multiple 192-byte writes and 256-byte reads to exercise packet boundaries as well as SD persistence.</p>
<pre id="log">Ready.</pre>
</main>
<script>
'use strict';
const logEl=document.getElementById('log');
const runEl=document.getElementById('run');
let ws=null;
let pending=null;
let timer=null;

function line(s=''){logEl.textContent+=s+'\n';logEl.scrollTop=logEl.scrollHeight;}
function resetLog(){logEl.textContent='';}
function hex(n){return '0x'+Number(n).toString(16).toUpperCase();}
function bytesEqual(a,b){if(a.length!==b.length)return false;for(let i=0;i<a.length;i++)if(a[i]!==b[i])return false;return true;}
function toB64(bytes){let s='';for(let i=0;i<bytes.length;i++)s+=String.fromCharCode(bytes[i]);return btoa(s);}
function fromB64(s){const raw=atob(s||'');const out=new Uint8Array(raw.length);for(let i=0;i<raw.length;i++)out[i]=raw.charCodeAt(i);return out;}
function makePattern(len,seed){const a=new Uint8Array(len);let x=seed>>>0;for(let i=0;i<len;i++){x=(x*1664525+1013904223)>>>0;a[i]=(x>>>24)&255;}return a;}

function connect(){
  return new Promise((resolve,reject)=>{
    if(ws&&ws.readyState===WebSocket.OPEN){resolve();return;}
    const proto=location.protocol==='https:'?'wss://':'ws://';
    ws=new WebSocket(proto+location.host+'/ws');
    const t=setTimeout(()=>reject(new Error('WebSocket connection timeout')),5000);
    ws.onopen=()=>{clearTimeout(t);resolve();};
    ws.onerror=()=>{clearTimeout(t);reject(new Error('WebSocket connection failed'));};
    ws.onclose=()=>{if(pending){pending.reject(new Error('WebSocket closed'));pending=null;}ws=null;};
    ws.onmessage=(ev)=>{
      if(!pending)return;
      let obj;
      try{obj=JSON.parse(ev.data);}catch(e){return;}
      clearTimeout(timer);timer=null;
      const p=pending;pending=null;p.resolve(obj);
    };
  });
}

function request(obj){
  return new Promise((resolve,reject)=>{
    if(!ws||ws.readyState!==WebSocket.OPEN){reject(new Error('WebSocket is not connected'));return;}
    if(pending){reject(new Error('Internal error: request already pending'));return;}
    pending={resolve,reject};
    timer=setTimeout(()=>{if(pending){pending=null;reject(new Error('Teensy reply timeout'));}},5000);
    ws.send(JSON.stringify(obj));
  });
}

async function openFile(path,opts){
  const q={open:{path:path}};
  if(opts!==undefined)q.open.opts=opts;
  const r=await request(q);
  return Number(r.open&&r.open.fd!==undefined?r.open.fd:-1);
}
async function closeFile(fd){const r=await request({close:{fd:fd}});return Number(r.close&&r.close.fd!==undefined?r.close.fd:-999);}
async function seekFile(fd,offset){const r=await request({lseek:{fd:fd,offset:offset}});return Number(r.lseek&&r.lseek.offset!==undefined?r.lseek.offset:0);}
async function readChunk(fd,len){
  const r=await request({read:{fd:fd,len:len}});
  const n=Number(r.read&&r.read.len!==undefined?r.read.len:-1);
  if(n<0)return {n:n,data:new Uint8Array(0)};
  const data=fromB64(r.read&&r.read.data?r.read.data:'');
  return {n:n,data:data};
}
async function writeChunk(fd,data){
  const b64=toB64(data);
  const r=await request({write:{fd:fd,len:data.length},Write:{data:b64}});
  return Number(r.write&&r.write.len!==undefined?r.write.len:-1);
}
async function readExact(fd,total){
  const out=new Uint8Array(total);let done=0;
  while(done<total){const want=Math.min(256,total-done);const r=await readChunk(fd,want);if(r.n<=0)throw new Error('read returned '+r.n+' at offset '+done);if(r.data.length!==r.n)throw new Error('Base64 length mismatch: reply '+r.n+', decoded '+r.data.length);out.set(r.data.subarray(0,r.n),done);done+=r.n;}
  return out;
}
async function writeAll(fd,data){
  let done=0;
  while(done<data.length){const n=Math.min(192,data.length-done);const chunk=data.subarray(done,done+n);const wrote=await writeChunk(fd,chunk);if(wrote!==n)throw new Error('write returned '+wrote+' for '+n+' bytes at offset '+done);done+=n;}
  return done;
}
function step(name,ok,detail=''){line(name.padEnd(39,'.')+' '+(ok?'PASS':'FAIL')+(detail?'  '+detail:''));if(!ok)throw new Error(name+(detail?': '+detail:''));}

async function runTest(){
  runEl.disabled=true;resetLog();line('Teensy SD File Service Test');line('===========================');line();
  let fd=-1;
  try{
    await connect();line('WebSocket /ws connected.');line();

    fd=await openFile('/cpm/boot.txt');step('Open existing /cpm/boot.txt',fd>=0,'fd='+fd);
    let r=await readChunk(fd,64);step('Read existing file',r.n>0,r.n+' bytes');
    let c=await closeFile(fd);fd=-1;step('Close existing file',c===-1,'returned fd='+c);
    line();

    const block1=makePattern(128,0x12345678);
    fd=await openFile('/cpm/ESPTEST.BIN',1538);step('Create/truncate ESPTEST.BIN',fd>=0,'fd='+fd+' opts=1538');
    let n=await writeChunk(fd,block1);step('Write block 1',n===block1.length,n+'/'+block1.length+' bytes');
    c=await closeFile(fd);fd=-1;step('Close created file',c===-1,'returned fd='+c);

    fd=await openFile('/cpm/ESPTEST.BIN');step('Reopen created file',fd>=0,'fd='+fd);
    let got=await readExact(fd,block1.length);step('Read back block 1',got.length===block1.length,got.length+' bytes');
    step('Compare block 1',bytesEqual(got,block1));
    c=await closeFile(fd);fd=-1;step('Close after readback',c===-1,'returned fd='+c);
    line();

    const block2=makePattern(128,0x89ABCDEF);
    fd=await openFile('/cpm/ESPTEST.BIN',2);step('Open existing read/write',fd>=0,'fd='+fd+' opts=2');
    let sk=await seekFile(fd,128);step('Seek to offset 128',sk!==0,'Teensy seek result='+sk);
    n=await writeChunk(fd,block2);step('Write block 2',n===block2.length,n+'/'+block2.length+' bytes');
    c=await closeFile(fd);fd=-1;step('Close after block 2',c===-1,'returned fd='+c);

    const combined=new Uint8Array(256);combined.set(block1,0);combined.set(block2,128);
    fd=await openFile('/cpm/ESPTEST.BIN');step('Reopen two-block file',fd>=0,'fd='+fd);
    got=await readExact(fd,combined.length);step('Read combined file',got.length===combined.length,got.length+' bytes');
    step('Compare combined file',bytesEqual(got,combined));
    c=await closeFile(fd);fd=-1;step('Close two-block file',c===-1,'returned fd='+c);
    line();

    const big=makePattern(8192,0xC0FFEE01);
    fd=await openFile('/cpm/ESPTEST.BIN',1538);step('Create/truncate 8 KiB test',fd>=0,'fd='+fd);
    n=await writeAll(fd,big);step('Write 8 KiB in 192-byte chunks',n===big.length,n+' bytes');
    c=await closeFile(fd);fd=-1;step('Close 8 KiB file',c===-1,'returned fd='+c);
    fd=await openFile('/cpm/ESPTEST.BIN');step('Reopen 8 KiB file',fd>=0,'fd='+fd);
    got=await readExact(fd,big.length);step('Read 8 KiB in 256-byte chunks',got.length===big.length,got.length+' bytes');
    step('Compare full 8 KiB',bytesEqual(got,big));
    c=await closeFile(fd);fd=-1;step('Final close',c===-1,'returned fd='+c);

    line();line('RESULT: PASS');
  }catch(e){
    line();line('RESULT: FAIL');line(String(e&&e.message?e.message:e));
    if(fd>=0){try{await closeFile(fd);}catch(ignore){}}
  }finally{runEl.disabled=false;}
}

runEl.addEventListener('click',runTest);
</script>
</body>
</html>
)HTML";

inline void ebtksServeSdTest(AsyncWebServerRequest *request)
{
  request->send(200, "text/html", EBTKS_SD_TEST_PAGE);
}
