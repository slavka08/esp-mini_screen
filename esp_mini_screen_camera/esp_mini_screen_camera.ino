#include <ESP8266WiFi.h>
#include <ESP8266WebServerSecure.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include <string.h>
#include <new>

#ifdef __has_include
  #if __has_include("tls_local.h")
    #include "tls_local.h"
  #else
    #error "Missing tls_local.h. Run: bash setup.sh"
  #endif
#else
  #include "tls_local.h"
#endif

// --- Frame settings ---
// Stream profiles: 30x30 (8x), 40x40 (6x), 48x48 (5x), 60x60 (4x), 80x80 (3x)
#define DISPLAY_SIZE 240
#define DEFAULT_FRAME_EDGE 48
#define DEFAULT_SCALE 5
#define MAX_FRAME_EDGE 80
#define DEFAULT_FRAME_SIZE (DEFAULT_FRAME_EDGE * DEFAULT_FRAME_EDGE * 2)
#define MAX_FRAME_SIZE (MAX_FRAME_EDGE * MAX_FRAME_EDGE * 2)
#define MAX_SCALE 8

// --- EEPROM layout (compatible with wifi sketch) ---
#define EEPROM_SIZE 96
#define SSID_ADDR   0
#define PASS_ADDR   32
#define MAX_SSID    32
#define MAX_PASS    64

const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";

TFT_eSPI tft = TFT_eSPI();
BearSSL::ESP8266WebServerSecure server(443);

// One 240xN block (RGB565) for fast vertical scaling in a single pushImage call
uint16_t scaledBlock[DISPLAY_SIZE * MAX_SCALE];

uint8_t frameEdge = DEFAULT_FRAME_EDGE;
uint8_t frameScale = DEFAULT_SCALE;
size_t frameSizeBytes = DEFAULT_FRAME_SIZE;
bool frameSmooth = false;
uint8_t* frameUploadBuffer = nullptr;
size_t frameUploadBufferCap = 0;
uint16_t smoothLineBuffer[DISPLAY_SIZE];
size_t frameUploadLen = 0;
bool frameUploadError = false;

unsigned long frameCounter = 0;

// ===================== EEPROM helpers =====================

void saveCredentials(const String& ssid, const String& pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < MAX_SSID; i++)
    EEPROM.write(SSID_ADDR + i, i < (int)ssid.length() ? ssid[i] : 0);
  for (int i = 0; i < MAX_PASS; i++)
    EEPROM.write(PASS_ADDR + i, i < (int)pass.length() ? pass[i] : 0);
  EEPROM.commit();
  EEPROM.end();
}

bool loadCredentials(String& ssid, String& pass) {
  EEPROM.begin(EEPROM_SIZE);
  char buf[MAX_PASS + 1];

  for (int i = 0; i < MAX_SSID; i++) buf[i] = EEPROM.read(SSID_ADDR + i);
  buf[MAX_SSID] = 0;
  ssid = String(buf);

  for (int i = 0; i < MAX_PASS; i++) buf[i] = EEPROM.read(PASS_ADDR + i);
  buf[MAX_PASS] = 0;
  pass = String(buf);

  EEPROM.end();
  return ssid.length() > 0 && ssid[0] != '\xff';
}

bool isSupportedEdge(uint8_t edge) {
  return edge == 30 || edge == 40 || edge == 48 || edge == 60 || edge == 80;
}

bool applyFrameProfile(uint8_t edge) {
  if (!isSupportedEdge(edge))
    return false;
  if (DISPLAY_SIZE % edge != 0)
    return false;

  uint8_t scale = DISPLAY_SIZE / edge;
  if (scale == 0 || scale > MAX_SCALE)
    return false;

  frameEdge = edge;
  frameScale = scale;
  frameSizeBytes = (size_t)edge * edge * 2;
  return true;
}

bool isSmoothAllowedForProfile() {
  return frameEdge <= 48;
}

bool setFrameUploadBufferSize(size_t bytes) {
  if (bytes == 0)
    return false;
  if (bytes > MAX_FRAME_SIZE)
    return false;
  if (bytes == frameUploadBufferCap && frameUploadBuffer != nullptr)
    return true;

  uint8_t* newBuf = new (std::nothrow) uint8_t[bytes];
  if (!newBuf)
    return false;

  if (frameUploadBuffer) {
    delete[] frameUploadBuffer;
  }
  frameUploadBuffer = newBuf;
  frameUploadBufferCap = bytes;
  return true;
}

// ===================== Display helpers =====================

void displayCentered(const String& text, int y, int size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, TFT_BLACK);
  int16_t tw = tft.textWidth(text);
  tft.setCursor((DISPLAY_SIZE - tw) / 2, y);
  tft.print(text);
}

void showConnectedInfo() {
  tft.fillScreen(TFT_BLACK);
  displayCentered("Camera Ready", 10, 2, TFT_GREEN);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 45);
  tft.print("WiFi: " + WiFi.SSID());

  tft.setCursor(10, 65);
  tft.print("Open in browser (HTTPS):");

  String ip = WiFi.localIP().toString();
  displayCentered("https://" + ip, 90, 1, TFT_CYAN);

  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 120);
  tft.print("Accept the certificate");
  tft.setCursor(10, 135);
  tft.print("warning in browser!");

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  displayCentered("Heap: " + String(ESP.getFreeHeap()) + " bytes", 175, 1, TFT_DARKGREY);
  displayCentered("RGB565 via HTTPS", 200, 1, TFT_DARKGREY);
}

void showAPInfo() {
  tft.fillScreen(TFT_BLACK);
  displayCentered("WiFi Setup", 10, 2, TFT_YELLOW);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 50);
  tft.print("Connect to WiFi:");
  tft.setCursor(10, 70);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(AP_SSID);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 100);
  tft.print("Password:");
  tft.setCursor(10, 120);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(AP_PASS);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 155);
  tft.print("Then open:");
  tft.setCursor(10, 175);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print("192.168.4.1");
}

void showConnecting(const String& ssid) {
  tft.fillScreen(TFT_BLACK);
  displayCentered("Connecting...", 80, 2, TFT_YELLOW);
  displayCentered(ssid, 120, 1, TFT_WHITE);
}

// ===================== Frame display =====================

static inline uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t, uint8_t denom) {
  if (t == 0 || a == b || denom == 0) {
    return a;
  }

  uint8_t wa = denom - t;
  uint8_t wb = t;

  uint16_t ar = (a >> 11) & 0x1F;
  uint16_t ag = (a >> 5) & 0x3F;
  uint16_t ab = a & 0x1F;

  uint16_t br = (b >> 11) & 0x1F;
  uint16_t bg = (b >> 5) & 0x3F;
  uint16_t bb = b & 0x1F;

  uint16_t r = (ar * wa + br * wb + (denom >> 1)) / denom;
  uint16_t g = (ag * wa + bg * wb + (denom >> 1)) / denom;
  uint16_t bl = (ab * wa + bb * wb + (denom >> 1)) / denom;

  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void buildScaledLine(const uint16_t* srcPixels, uint16_t* outLine, bool smoothHorizontal) {
  if (frameScale <= 1 || !smoothHorizontal) {
    for (int x = 0; x < frameEdge; x++) {
      uint16_t c = srcPixels[x];
      int bx = x * frameScale;
      for (int s = 0; s < frameScale; s++) {
        outLine[bx + s] = c;
      }
    }
    return;
  }

  uint8_t denom = frameScale - 1;
  for (int x = 0; x < frameEdge; x++) {
    uint16_t c = srcPixels[x];
    uint16_t cNext = (x < frameEdge - 1) ? srcPixels[x + 1] : c;
    int bx = x * frameScale;
    for (int s = 0; s < frameScale; s++) {
      outLine[bx + s] = lerp565(c, cNext, s, denom);
    }
  }
}

// Display one source row scaled horizontally, then repeat N times vertically
void displayScaledRow(int srcY, const uint16_t* srcPixels, const uint16_t* nextPixels) {
  uint16_t* firstLine = scaledBlock;
  int rowWidth = frameEdge * frameScale;

  buildScaledLine(srcPixels, firstLine, frameSmooth);
  if (frameSmooth && frameScale > 1 && srcY < frameEdge - 1) {
    buildScaledLine(nextPixels, smoothLineBuffer, true);
    uint8_t denom = frameScale - 1;
    for (int s = 1; s < frameScale; s++) {
      uint16_t* dstLine = &scaledBlock[s * rowWidth];
      for (int i = 0; i < rowWidth; i++) {
        dstLine[i] = lerp565(firstLine[i], smoothLineBuffer[i], s, denom);
      }
    }
  } else {
    for (int s = 1; s < frameScale; s++) {
      memcpy(&scaledBlock[s * rowWidth], firstLine, rowWidth * sizeof(uint16_t));
    }
  }

  int dy = srcY * frameScale;
  tft.pushImage(0, dy, rowWidth, frameScale, scaledBlock);
}

void drawFrame(const uint8_t* data) {
  tft.startWrite();
  tft.setSwapBytes(true);
  for (int y = 0; y < frameEdge; y++) {
    const uint16_t* srcRow = (const uint16_t*)(data + y * frameEdge * 2);
    const uint16_t* nextRow = (y + 1 < frameEdge)
      ? (const uint16_t*)(data + (y + 1) * frameEdge * 2)
      : srcRow;
    displayScaledRow(y, srcRow, nextRow);
    yield();
  }
  tft.setSwapBytes(false);
  tft.endWrite();
}

// ===================== Web pages =====================

const char CAMERA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Camera Stream</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:15px;text-align:center}
h2{color:#e94560;margin:10px 0}
video{width:100%;max-width:320px;border-radius:8px;background:#000}
canvas{display:none}
button{padding:14px 28px;margin:10px 5px;border:none;border-radius:8px;font-size:16px;
  font-weight:bold;cursor:pointer;color:#fff}
.start{background:#0f0;color:#000}
.stop{background:#e94560}
.info{font-size:13px;color:#888;margin:5px 0}
.fps{font-size:18px;color:#0f0;margin:10px 0}
select{padding:8px;border-radius:6px;font-size:14px;background:#16213e;color:#eee;border:1px solid #333}
.ctrl{display:flex;justify-content:center;gap:10px;flex-wrap:wrap;margin:8px 0}
.ctrl label{font-size:12px;color:#bbb;text-align:left}
.ctrl select{display:block;margin-top:4px;min-width:120px}
</style>
</head>
<body>
<h2>Camera to Display</h2>
<div class='ctrl'>
<label>Camera
<select id='cam'>
<option value='environment'>Back Camera</option>
<option value='user'>Front Camera</option>
</select>
</label>
<label>Resolution
<select id='res'>
<option value='30'>30x30 (8x)</option>
<option value='40'>40x40 (6x)</option>
<option value='48' selected>48x48 (5x)</option>
<option value='60'>60x60 (4x)</option>
<option value='80'>80x80 (3x)</option>
</select>
</label>
<label>FPS
<select id='fpsSel'>
<option value='5'>5</option>
<option value='8'>8</option>
<option value='10' selected>10</option>
</select>
</label>
<label>Smooth
<select id='smoothSel'>
<option value='1'>On</option>
<option value='0' selected>Off</option>
</select>
</label>
</div>
<video id='v' autoplay playsinline muted></video>
<canvas id='c' width='48' height='48'></canvas>
<div>
<button class='start' onclick='startStream()'>Start</button>
<button class='stop' onclick='stopStream()'>Stop</button>
</div>
<div class='fps' id='fps'>-- FPS</div>
<div class='info' id='fmt'>Resolution: 48x48 | Scale: 5x | Format: RGB565</div>
<div class='info' id='status'>Ready</div>
<div class='info' id='dbg'></div>
<script>
const DISPLAY_SIZE=240;
const video=document.getElementById('v');
const canvas=document.getElementById('c');
const ctx=canvas.getContext('2d',{willReadFrequently:true});
const camEl=document.getElementById('cam');
const resEl=document.getElementById('res');
const fpsSelEl=document.getElementById('fpsSel');
const smoothSelEl=document.getElementById('smoothSel');
const fpsEl=document.getElementById('fps');
const fmtEl=document.getElementById('fmt');
const statusEl=document.getElementById('status');
const dbgEl=document.getElementById('dbg');
let streaming=false,frameCount=0,lastFpsTime=0;
let frameEdge=48;
let targetFps=10;
let frameInterval=1000/targetFps;
let smoothEnabled=false;

function updateFormatInfo(){
 const scale=DISPLAY_SIZE/frameEdge;
 fmtEl.textContent='Resolution: '+frameEdge+'x'+frameEdge+' | Scale: '+scale+'x | Smooth: '+(smoothEnabled?'On':'Off')+' | RGB565';
}

function applyFpsFromUi(){
 targetFps=parseInt(fpsSelEl.value,10)||10;
 if(frameEdge>=80 && targetFps>5){
  targetFps=5;
  fpsSelEl.value='5';
 }else if(smoothEnabled && targetFps>5){
  targetFps=5;
  fpsSelEl.value='5';
 }else if(frameEdge===60 && targetFps>8){
  targetFps=8;
  fpsSelEl.value='8';
 }
 frameInterval=1000/targetFps;
}

async function applyResolutionFromUi(){
 const requested=parseInt(resEl.value,10)||48;
 const smooth=smoothSelEl.value==='1'?1:0;
 const resp=await fetch('/config',{
  method:'POST',
  headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'res='+encodeURIComponent(requested)+'&smooth='+smooth
 });
 const txt=await resp.text();
 if(!resp.ok){
  throw new Error(txt||('HTTP '+resp.status));
 }

 const edgeMatch=txt.match(/ok:(\d+)x\1/);
 const smoothMatch=txt.match(/smooth:(\d)/);
 frameEdge=edgeMatch?parseInt(edgeMatch[1],10):requested;
 smoothEnabled=smoothMatch?smoothMatch[1]==='1':!!smooth;
 smoothSelEl.value=smoothEnabled?'1':'0';
 resEl.value=String(frameEdge);
 if(smooth===1 && !smoothEnabled){
  dbgEl.textContent='ESP: Smooth is available up to 48x48';
 }
 canvas.width=frameEdge;
 canvas.height=frameEdge;
 updateFormatInfo();
}

fpsSelEl.addEventListener('change',applyFpsFromUi);
smoothSelEl.addEventListener('change',async()=>{
 try{
  await applyResolutionFromUi();
  applyFpsFromUi();
  if(streaming){
   statusEl.textContent='Streaming '+frameEdge+'x'+frameEdge+' @ '+targetFps+' FPS target';
  }else{
   statusEl.textContent='Ready';
  }
 }catch(e){
  statusEl.textContent='Config error: '+e.message;
 }
});
resEl.addEventListener('change',async()=>{
 try{
  await applyResolutionFromUi();
  applyFpsFromUi();
  if(streaming){
   statusEl.textContent='Streaming '+frameEdge+'x'+frameEdge+' @ '+targetFps+' FPS target';
  }else{
   statusEl.textContent='Ready';
  }
 }catch(e){
  statusEl.textContent='Config error: '+e.message;
 }
});

updateFormatInfo();
applyFpsFromUi();

async function startStream(){
 if(streaming)return;
 try{
  await applyResolutionFromUi();
  applyFpsFromUi();
  const sourceSize=Math.max(160,frameEdge*4);
  const facingMode=camEl.value;
  const stream=await navigator.mediaDevices.getUserMedia({
   video:{facingMode,width:{ideal:sourceSize},height:{ideal:sourceSize}},audio:false
  });
  video.srcObject=stream;
  await video.play();
  streaming=true;
  frameCount=0;
  lastFpsTime=performance.now();
  statusEl.textContent='Streaming '+frameEdge+'x'+frameEdge+' @ '+targetFps+' FPS target';
  sendLoop();
 }catch(e){
  statusEl.textContent='Camera/config error: '+e.message;
 }
}

function stopStream(){
 streaming=false;
 if(video.srcObject){
  video.srcObject.getTracks().forEach(t=>t.stop());
  video.srcObject=null;
 }
 fpsEl.textContent='-- FPS';
 statusEl.textContent='Stopped';
}

function captureFrame(){
 ctx.drawImage(video,0,0,frameEdge,frameEdge);
 const imgData=ctx.getImageData(0,0,frameEdge,frameEdge).data;
 const pixels=frameEdge*frameEdge;
 const buf=new ArrayBuffer(pixels*2);
 const view=new DataView(buf);
 for(let i=0;i<pixels;i++){
  const r=imgData[i*4],g=imgData[i*4+1],b=imgData[i*4+2];
  const c=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);
  view.setUint16(i*2,c,true);
 }
 return buf;
}

async function sendLoop(){
 while(streaming){
  const t0=performance.now();
  const frame=captureFrame();
  try{
   const formData=new FormData();
   formData.append('frame',new Blob([frame],{type:'application/octet-stream'}),'frame.bin');
   const resp=await fetch('/frame',{method:'POST',body:formData});
   if(!resp.ok) throw new Error('HTTP '+resp.status);
   frameCount++;
   const now=performance.now();
   if(now-lastFpsTime>=1000){
    fpsEl.textContent=frameCount+' FPS';
    dbgEl.textContent='ESP: HTTP '+resp.status+' | target '+targetFps+' FPS';
    frameCount=0;
    lastFpsTime=now;
   }
   const elapsed=now-t0;
   if(elapsed<frameInterval) await new Promise(r=>setTimeout(r,frameInterval-elapsed));
  }catch(e){
   dbgEl.textContent='Fetch error: '+e.message;
   statusEl.textContent='Send error - retrying...';
   await new Promise(r=>setTimeout(r,1000));
  }
 }
}
</script>
</body>
</html>
)rawliteral";

const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>WiFi Setup</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px}
h2{color:#e94560;text-align:center}
.net{padding:12px;margin:6px 0;background:#16213e;border-radius:8px;cursor:pointer}
.net:hover{background:#0f3460}
input,button{width:100%;padding:12px;margin:8px 0;border:none;border-radius:8px;box-sizing:border-box;font-size:16px}
input{background:#16213e;color:#eee}
button{background:#e94560;color:#fff;cursor:pointer;font-weight:bold}
</style>
</head>
<body>
<h2>WiFi Setup</h2>
<div id='nets'>%NETWORKS%</div>
<form action='/connect' method='POST'>
<input id='s' name='ssid' placeholder='SSID' required>
<input name='pass' type='password' placeholder='Password'>
<button type='submit'>Connect</button>
</form>
<button onclick="location.href='/scan'">Scan Again</button>
<script>function sel(s){document.getElementById('s').value=s}</script>
</body>
</html>
)rawliteral";

// ===================== Frame handler =====================

void handleConfig() {
  if (!server.hasArg("res")) {
    server.send(400, "text/plain", "missing:res");
    return;
  }

  uint8_t oldEdge = frameEdge;
  uint8_t oldScale = frameScale;
  size_t oldSize = frameSizeBytes;

  int requested = server.arg("res").toInt();
  if (!applyFrameProfile((uint8_t)requested)) {
    server.send(400, "text/plain", "bad:res");
    return;
  }

  if (!setFrameUploadBufferSize(frameSizeBytes)) {
    frameEdge = oldEdge;
    frameScale = oldScale;
    frameSizeBytes = oldSize;
    server.send(503, "text/plain", "bad:oom");
    return;
  }

  if (server.hasArg("smooth")) {
    String smoothArg = server.arg("smooth");
    frameSmooth = smoothArg != "0";
  }

  if (frameSmooth && !isSmoothAllowedForProfile()) {
    frameSmooth = false;
  }

  server.send(200, "text/plain",
    "ok:" + String(frameEdge) + "x" + String(frameEdge) +
    " scale:" + String(frameScale) +
    " smooth:" + String(frameSmooth ? "1" : "0"));
}

void handleFrameUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    frameUploadLen = 0;
    frameUploadError = false;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (frameUploadError)
      return;
    if (frameUploadLen + upload.currentSize > frameSizeBytes || frameUploadLen + upload.currentSize > frameUploadBufferCap) {
      frameUploadError = true;
      return;
    }
    memcpy(frameUploadBuffer + frameUploadLen, upload.buf, upload.currentSize);
    frameUploadLen += upload.currentSize;
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    frameUploadError = true;
  }
}

void handleFrame() {
  if (frameUploadBuffer == nullptr || frameUploadBufferCap < frameSizeBytes) {
    server.send(503, "text/plain", "bad:buf");
    return;
  }

  if (frameUploadError) {
    server.send(400, "text/plain", "bad:upload");
    frameUploadLen = 0;
    frameUploadError = false;
    return;
  }

  if (frameUploadLen != frameSizeBytes) {
    server.send(400, "text/plain", "bad:" + String(frameUploadLen) + "/" + String(frameSizeBytes));
    frameUploadLen = 0;
    frameUploadError = false;
    return;
  }

  drawFrame(frameUploadBuffer);

  frameCounter++;
  frameUploadLen = 0;
  frameUploadError = false;
  server.send(204, "text/plain", "");
}

// ===================== WiFi setup handlers =====================

String scannedNetworks = "";

void scanNetworks() {
  int n = WiFi.scanNetworks();
  scannedNetworks = "";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    scannedNetworks += "<div class='net' onclick=\"sel('" + ssid + "')\">";
    scannedNetworks += ssid + " (" + String(rssi) + " dBm)";
    if (WiFi.encryptionType(i) == ENC_TYPE_NONE)
      scannedNetworks += " [open]";
    scannedNetworks += "</div>";
  }
}

void handleRoot() {
  if (WiFi.status() == WL_CONNECTED) {
    server.send_P(200, "text/html", CAMERA_HTML);
  } else {
    String page = FPSTR(SETUP_HTML);
    page.replace("%NETWORKS%", scannedNetworks);
    server.send(200, "text/html", page);
  }
}

void handleScan() {
  scanNetworks();
  String page = FPSTR(SETUP_HTML);
  page.replace("%NETWORKS%", scannedNetworks);
  server.send(200, "text/html", page);
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  server.send(200, "text/html",
    "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px}</style></head>"
    "<body><h2>Connecting to " + ssid + "...</h2>"
    "<p>Check the screen for status.</p></body></html>");
  delay(500);

  saveCredentials(ssid, pass);
  server.stop();

  showConnecting(ssid);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    showConnectedInfo();
    startServer();
  } else {
    tft.fillScreen(TFT_BLACK);
    displayCentered("Failed!", 80, 2, TFT_RED);
    displayCentered("Restarting...", 120, 1, TFT_WHITE);
    delay(2000);
    ESP.restart();
  }
}

// ===================== Server setup =====================

void startServer() {
  server.getServer().setECCert(
    new BearSSL::X509List(serverCert),
    BR_KEYTYPE_EC,
    new BearSSL::PrivateKey(serverKey)
  );

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/frame", HTTP_POST, handleFrame, handleFrameUpload);
  server.begin();
}

// ===================== AP mode =====================

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  scanNetworks();
  showAPInfo();
  startServer();
}

// ===================== Main =====================

void setup() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  applyFrameProfile(DEFAULT_FRAME_EDGE);
  if (!setFrameUploadBufferSize(frameSizeBytes)) {
    tft.fillScreen(TFT_BLACK);
    displayCentered("Heap error", 90, 2, TFT_RED);
    displayCentered("Restarting...", 130, 1, TFT_WHITE);
    delay(1500);
    ESP.restart();
  }

  String ssid, pass;
  if (loadCredentials(ssid, pass)) {
    showConnecting(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      showConnectedInfo();
      startServer();
      return;
    }

    tft.fillScreen(TFT_BLACK);
    displayCentered("WiFi failed", 80, 2, TFT_RED);
    displayCentered("Starting AP...", 120, 1, TFT_WHITE);
    delay(2000);
  }

  startAPMode();
}

void loop() {
  server.handleClient();
}
