#!/usr/bin/env node
// Browser terminal regression without a browser, DOM library or network.
// Execute the actual JavaScript embedded in the flash-backed /z80 page.
'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const src=fs.readFileSync(path.join(__dirname,'../ESP32_Z80_Emulator/web_terminal_v2.h'),'utf8');
const page=src.match(/R"HTML\(([\s\S]*?)\)HTML"/);
assert(page,'Z80 terminal PROGMEM HTML missing');
const match=page[1].match(/<script>([\s\S]*?)<\/script>/);
assert(match,'inline Web terminal JavaScript missing');
const elements=new Map(),handlers=new Map(),pendingTimers=[];
function element(id){
  if(!elements.has(id)){
    const e={id,style:{display:'none'},value:'',textContent:'',innerHTML:'',
      scrollTop:0,scrollHeight:0,
      focus(){},addEventListener(type,fn){handlers.set(id+':'+type,fn)}};
    elements.set(id,e);
  }
  return elements.get(id);
}
const document={getElementById:element,addEventListener(){}};
class FakeWebSocket{
  constructor(url){this.url=url;this.readyState=1;this.sent=[];}
  send(value){this.sent.push(value)}
}
FakeWebSocket.OPEN=1;FakeWebSocket.CONNECTING=0;
const store=new Map();
const ctx=vm.createContext({document,WebSocket:FakeWebSocket,
  localStorage:{getItem:k=>store.get(k)||null,setItem:(k,v)=>store.set(k,v)},
  location:{host:'localhost:80'},requestAnimationFrame:fn=>fn(),
  setTimeout:(fn,ms)=>{pendingTimers.push({fn,ms});return pendingTimers.length},setInterval:()=>0,clearInterval:()=>0,
  Uint8Array,console});
vm.runInContext(match[1],ctx,{filename:'inline-z80-terminal.js'});
function evalTerm(expr){return vm.runInContext(expr,ctx)}
function feedString(str){ctx.feed(Uint8Array.from(str, c=>c.charCodeAt(0)&255))}
function feedBytes(...bytes){ctx.feed(Uint8Array.from(bytes))}
function reset(){ctx.setMode('vt100')}
const ESC=String.fromCharCode(27);
const CSI=ESC+'[';

// CP437 0x80..0xFF exactly; raw bytes must render as Unicode glyphs.
assert.equal(evalTerm('cp437High.length'),128);
for(const [b,ch] of [[0x80,'Ç'],[0x9c,'£'],[0xb0,'░'],[0xb3,'│'],
  [0xc4,'─'],[0xda,'┌'],[0xdb,'█'],[0xe3,'π'],[0xf3,'≤'],[0xff,'\u00a0']]){
  assert.equal(evalTerm('cp437('+b+')'),ch);
}
reset();
feedBytes(0xda,0xc4,0xbf,0xb3,0xdb,0x80);
assert.equal(evalTerm('scr[0].slice(0,6).map(x=>x.ch).join("")'),'┌─┐│█Ç');

// Palette indexes must track TFT regular and bright SGR colors.
reset();
feedString(CSI+'31;44mA'+CSI+'1mB'+CSI+'22;91;104mC'+CSI+'39;49mD');
assert.equal(evalTerm('scr[0][0].fg'),1);
assert.equal(evalTerm('scr[0][0].bg'),4);
assert.equal(evalTerm('scr[0][1].bd'),true);
assert.equal(evalTerm('scr[0][2].fg'),9);
assert.equal(evalTerm('scr[0][2].bg'),12);
assert.equal(evalTerm('scr[0][2].bd'),false);
assert.equal(evalTerm('scr[0][3].fg'),16);
assert.equal(evalTerm('scr[0][3].bg'),16);
assert(element('term').innerHTML.includes('#ad0000'),'normal ANSI red missing');
assert(element('term').innerHTML.includes('#0000ad'),'normal ANSI blue missing');
assert(element('term').innerHTML.includes('#ff0000'),'bold ANSI bright red missing');
assert(element('term').innerHTML.includes('#0000ff'),'bright blue missing');

// Clearing the screen uses active cell attributes (as on the TFT).
reset();
feedString(CSI+'42m'+CSI+'2K');
assert.equal(evalTerm('scr[0][0].bg'),2);
assert(element('term').innerHTML.includes('background-color:#00aa00'));

// Inverse, underline, reset and default bold foreground.
reset();
feedString(CSI+'1;4;7mX');
assert.equal(evalTerm('scr[0][0].ul'),true);
assert.equal(evalTerm('scr[0][0].inv'),true);
assert(element('term').innerHTML.includes('background-color:#00ff00'));
feedString(CSI+'0mY');
assert.equal(evalTerm('scr[0][1].fg'),16);
assert.equal(evalTerm('scr[0][1].inv'),false);

// Ignore 256-color and true-color SGR payloads, not accidentally reset.
reset();
feedString(CSI+'7;38;2;250;0;0;48;5;42mI');
assert.equal(evalTerm('scr[0][0].inv'),true);
assert.equal(evalTerm('scr[0][0].fg'),16);
assert.equal(evalTerm('scr[0][0].bg'),16);

// DEC Special Graphics includes the entire TFT/CP437 line-map set.
reset();
feedString(ESC+'(0lqqk'+ESC+'(B!');
assert.equal(evalTerm('scr[0].slice(0,5).map(x=>x.ch).join("")'),'┌──┐!');
reset();
feedString(ESC+')0x'+String.fromCharCode(14)+'x'+String.fromCharCode(15)+'x');
assert.equal(evalTerm('scr[0].slice(0,3).map(x=>x.ch).join("")'),'x│x');
reset();
feedString(ESC+'(A#'+ESC+'(B#');
assert.equal(evalTerm('scr[0].slice(0,2).map(x=>x.ch).join("")'),'£#');

// Cursor and style saved/restored by VT100 escape and CSI sequence.
reset();
feedString(CSI+'31m'+ESC+'7'+CSI+'32m'+ESC+'8Z');
assert.equal(evalTerm('scr[0][0].fg'),1);
reset();
feedString(CSI+'34m'+CSI+'s'+CSI+'33m'+CSI+'uQ');
assert.equal(evalTerm('scr[0][0].fg'),4);

// Escaping still prevents arbitrary HTML from terminal byte streams.
reset();
feedString('<&>');
assert(element('term').innerHTML.includes('&lt;&amp;&gt;'));
assert(!element('term').innerHTML.includes('<&>'));

// Group adjacent cells: a colorized 80-column banner must not create 80 spans.
reset();
feedString(CSI+'41m'+'X'.repeat(80));
const spanCount=(element('term').innerHTML.match(/<span\b/g)||[]).length;
assert(spanCount<8,'rendering should group adjacent identical color cells');

// HP legacy mode still works, with CP437 high glyphs supported.
ctx.setMode('hp86');
feedBytes(0xdb,0xda);
assert.equal(evalTerm('scr[0][0].ch'),'█');
assert.equal(evalTerm('scr[0][1].ch'),'┌');
assert.equal(evalTerm('mode'),'hp86');

// Raw mode remains a non-ANSI stream, but high bytes show CP437.
ctx.setMode('raw');
feedBytes(0xda,0xc4,0xbf);
assert.equal(element('term').textContent,'┌─┐');

// DA/DSR replies are now owned by ESP32's shared console router.
reset();
evalTerm('ws.sent.length=0');
feedString(CSI+'c'+CSI+'5n'+CSI+'6n'+ESC+'Z');
assert.equal(evalTerm('ws.sent.length'),0,
  'browser must not duplicate hardware console DA/DSR responses');
// Interactive Web keyboard input is still sent as before.
handlers.get('term:keydown')({key:'x',ctrlKey:false,altKey:false,
  preventDefault(){}});
assert.equal(evalTerm('ws.sent.length'),1);
assert.equal(evalTerm('ws.sent[0][0]'),120);

// A refused WebSocket upgrade must not cause an endless one-second retry loop.
// Verify three attempts, progressively spaced, then a manual recovery path.
for (let attempt=1;attempt<=3;attempt++){
  evalTerm('ws.readyState=3;ws.onclose()');
  if(attempt<3){
    const retry=pendingTimers.shift();
    assert(retry,'a bounded reconnect must be scheduled');
    assert.equal(retry.ms,5000*attempt);
    retry.fn(); // simulate the browser retry timer
  }
}
assert.equal(pendingTimers.length,0,'Web terminal should stop after three failures');
assert(element('state').textContent.includes('Reconnect'));
evalTerm('retryTerminalConnection()');
assert.equal(evalTerm('wsFailures'),0,'manual reconnect restarts the attempt budget');

// Browser-only change: preserve binary WebSocket input and status.
assert(src.includes('AsyncWebSocket z80WebSocket("/z80ws")'));
assert(src.includes('z80WebSocket.binaryAll(data, len)'));
console.log('PASS: actual browser terminal ANSI-16, CP437, DEC graphics, HP legacy, HTML escaping, span grouping');
