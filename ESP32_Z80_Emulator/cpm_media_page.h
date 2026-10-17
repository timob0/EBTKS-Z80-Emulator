#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "cpm_media.h"

static const char CPM_MEDIA_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EBTKS CP/M Media Manager</title>
<style>
body{font-family:Verdana,sans-serif;background:#202124;color:#e8eaed;margin:0;padding:1rem}main{max-width:1100px;margin:auto}
h2,h3{margin:.5rem 0 1rem}.bar{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin:.8rem 0}
button{border:0;border-radius:.3rem;background:#1fa3ec;color:#fff;padding:.55rem .85rem;cursor:pointer}button:hover{background:#0e70a4}button.warn{background:#b45f06}button.danger{background:#9d2b2b}button:disabled{opacity:.45;cursor:default}
select{background:#303134;color:#e8eaed;border:1px solid #5f6368;border-radius:.25rem;padding:.45rem;max-width:100%;min-width:260px}
table{border-collapse:collapse;width:100%;margin:1rem 0;background:#292a2d}th,td{border-bottom:1px solid #444;padding:.55rem;text-align:left;vertical-align:top}th{background:#303134}.muted{color:#aaa}.ok{color:#9ef59e}.pending{color:#ffd166}.bad{color:#ff8a80}.mono{font-family:monospace;word-break:break-all}.small{font-size:.85rem}.drive{font-weight:bold;font-family:monospace;font-size:1.1rem}.card{background:#292a2d;border-radius:.4rem;padding:1rem;margin:1rem 0}.status{min-height:1.3rem}
@media(max-width:760px){table,thead,tbody,tr,th,td{display:block}thead{display:none}tr{border-bottom:2px solid #555;padding:.4rem}td{border:0;padding:.3rem}.drive{font-size:1.3rem}}
</style></head><body><main>
<h2>CP/M Media Manager</h2>
<div class="card"><div id="summary" class="status">Loading...</div>
<div class="bar"><button onclick="refreshAll()">Refresh</button><button class="warn" id="apply" onclick="applyReset()">Apply changes / Reset CP/M</button><button onclick="location.href='/z80'">Z80 Terminal</button><button onclick="location.href='/'">EBTKS Home</button></div>
<div class="small muted">Media files stay on the Teensy SD card under <span class="mono">/cpm/disks</span>. Mount changes are saved immediately but do not replace media underneath a running CP/M session; use Apply / Reset CP/M to activate them.</div></div>

<h3>Drive assignments</h3>
<table><thead><tr><th>Drive</th><th>Active</th><th>Next reset</th><th>Mount image</th><th>Actions</th></tr></thead><tbody id="drives"></tbody></table>

<h3>Available SD-card media</h3>
<table><thead><tr><th>Image</th><th>Type</th><th>Size</th><th>Access</th><th>Mounted</th></tr></thead><tbody id="media"></tbody></table>
<div id="message" class="status"></div>
</main><script>
let state=null;
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function fmtSize(n){if(n>=1048576)return (n/1048576).toFixed(n%1048576?2:0)+' MiB';if(n>=1024)return (n/1024).toFixed(n%1024?1:0)+' KiB';return n+' B'}
function mountText(m){if(!m||!m.mounted)return '<span class="muted">unmounted</span>';return `<div>${esc(m.label)}</div><div class="mono small">${esc(m.path)}</div><div class="small ${m.readonly?'pending':'ok'}">${m.readonly?'R/O':'R/W'}</div>`}
function render(){
 const sum=document.getElementById('summary');
 if(!state){sum.textContent='No data';return}
 sum.innerHTML=state.pending?'<span class="pending">Mount table has unapplied changes.</span>':'<span class="ok">Active mounts match the saved table.</span>';
 document.getElementById('apply').disabled=!state.pending;
 const supported=(state.media||[]).filter(m=>m.supported);
 let rows='';
 for(const d of state.drives||[]){
   const selected=d.desired&&d.desired.mounted?d.desired.path:'';
   let opts='<option value="">-- choose image --</option>';
   for(const m of supported)opts+=`<option value="${esc(m.path)}" ${m.path===selected?'selected':''}>${esc(m.name)} — ${esc(m.label)}${m.readonly?' [R/O]':''}</option>`;
   rows+=`<tr><td class="drive">${esc(d.drive)}:</td><td>${mountText(d.active)}</td><td>${mountText(d.desired)}</td><td><select id="sel-${esc(d.drive)}">${opts}</select></td><td><div class="bar"><button onclick="mountDrive('${esc(d.drive)}')">Mount</button><button class="danger" onclick="unmountDrive('${esc(d.drive)}')">Unmount</button></div></td></tr>`;
 }
 document.getElementById('drives').innerHTML=rows;
 let mediaRows='';
 for(const m of state.media||[]){
   mediaRows+=`<tr><td><div>${esc(m.name)}</div><div class="mono small muted">${esc(m.path)}</div></td><td class="${m.supported?'':'bad'}">${esc(m.label)}</td><td>${fmtSize(m.size)}</td><td>${m.readonly?'R/O':'R/W'}</td><td>${esc(m.mounted_on||'')}</td></tr>`;
 }
 document.getElementById('media').innerHTML=mediaRows||'<tr><td colspan="5" class="muted">No files found.</td></tr>';
}
async function refreshAll(){
 const msg=document.getElementById('message');msg.textContent='Refreshing...';
 try{const r=await fetch('/api/cpm/media',{cache:'no-store'});if(!r.ok)throw new Error(await r.text());state=await r.json();render();msg.textContent=''}catch(e){msg.innerHTML='<span class="bad">'+esc(e.message||e)+'</span>'}
}
async function mountDrive(d){
 const path=document.getElementById('sel-'+d).value;if(!path){document.getElementById('message').textContent='Choose an image first.';return}
 try{const r=await fetch('/api/cpm/mount?drive='+encodeURIComponent(d)+'&path='+encodeURIComponent(path),{method:'POST'});if(!r.ok)throw new Error(await r.text());await refreshAll()}catch(e){document.getElementById('message').innerHTML='<span class="bad">'+esc(e.message||e)+'</span>'}
}
async function unmountDrive(d){
 try{const r=await fetch('/api/cpm/unmount?drive='+encodeURIComponent(d),{method:'POST'});if(!r.ok)throw new Error(await r.text());await refreshAll()}catch(e){document.getElementById('message').innerHTML='<span class="bad">'+esc(e.message||e)+'</span>'}
}
async function applyReset(){
 if(!confirm('Reset the Z80/CP/M system now and activate the saved drive assignments?'))return;
 const msg=document.getElementById('message');msg.textContent='Resetting CP/M...';
 try{const r=await fetch('/api/cpm/apply',{method:'POST'});if(!r.ok)throw new Error(await r.text());setTimeout(refreshAll,1200)}catch(e){msg.innerHTML='<span class="bad">'+esc(e.message||e)+'</span>'}
}
refreshAll();
</script></body></html>)HTML";

inline int cpmParseDriveParam(AsyncWebServerRequest *request)
{
  if (!request->hasParam("drive")) return -1;
  String text = request->getParam("drive")->value();
  text.trim();
  if (text.length() != 1) return -1;
  char c = toupper((unsigned char)text[0]);
  return (c >= 'A' && c <= 'P') ? c - 'A' : -1;
}

inline void cpmMediaRegister(AsyncWebServer &server)
{
  server.on("/cpmmedia", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", CPM_MEDIA_PAGE);
  });

  server.on("/api/cpm/media", HTTP_GET, [](AsyncWebServerRequest *request) {
    String reply;
    if (!cpmBuildMediaStatusJson(reply)) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"Unable to read CP/M media state\"}");
      return;
    }
    request->send(200, "application/json", reply);
  });

  server.on("/api/cpm/mount", HTTP_POST, [](AsyncWebServerRequest *request) {
    int drive = cpmParseDriveParam(request);
    if (drive < 0 || !request->hasParam("path")) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"drive and path are required\"}");
      return;
    }
    String path = request->getParam("path")->value();
    if (!cpmMountDesired((uint8_t)drive, path.c_str())) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Image is missing or not a supported CP/M media type\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true,\"reset_required\":true}");
  });

  server.on("/api/cpm/unmount", HTTP_POST, [](AsyncWebServerRequest *request) {
    int drive = cpmParseDriveParam(request);
    if (drive < 0) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"valid drive A-P required\"}");
      return;
    }
    if (!cpmUnmountDesired((uint8_t)drive)) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"Unable to save mount table\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true,\"reset_required\":true}");
  });

  server.on("/api/cpm/apply", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Same virtual-machine reset mechanism as /z80/reset.  The HP-85 side,
    // Wi-Fi and web services stay up; loop() reloads CP/M and activates the
    // newly saved mount table.
    RUN = false;
    request->send(200, "application/json", "{\"ok\":true}");
  });
}
