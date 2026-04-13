#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include <string.h>

// --- Display frame settings ---
#define DISPLAY_SIZE 240
#define LINE_BYTES (DISPLAY_SIZE * 2)
#define FRAME_BYTES (DISPLAY_SIZE * DISPLAY_SIZE * 2)

// --- EEPROM layout (compatible with other sketches) ---
#define EEPROM_SIZE 96
#define SSID_ADDR   0
#define PASS_ADDR   32
#define MAX_SSID    32
#define MAX_PASS    64

const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";

TFT_eSPI tft = TFT_eSPI();
ESP8266WebServer server(80);

String scannedNetworks = "";
unsigned long imageCounter = 0;

union {
  uint8_t bytes[LINE_BYTES];
  uint16_t pixels[DISPLAY_SIZE];
} lineBuffer;

size_t uploadByteCount = 0;
size_t lineFill = 0;
uint16_t currentLine = 0;
bool uploadFailed = false;
bool uploadDrawing = false;
bool uploadSeen = false;

void startServer();
void startAPMode();

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
  displayCentered("Image Ready", 10, 2, TFT_GREEN);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 45);
  tft.print("WiFi: " + WiFi.SSID());

  tft.setCursor(10, 65);
  tft.print("Open in browser:");

  String ip = WiFi.localIP().toString();
  displayCentered("http://" + ip, 90, 1, TFT_CYAN);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  displayCentered("Upload image -> fit -> send", 200, 1, TFT_DARKGREY);
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

void finishUploadDraw() {
  if (uploadDrawing) {
    tft.setSwapBytes(false);
    tft.endWrite();
    uploadDrawing = false;
  }
}

// ===================== Web pages =====================

const char IMAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Image to Screen</title>
<style>
body{font-family:sans-serif;background:#10131a;color:#e6e6e6;margin:0;padding:16px;text-align:center}
h2{margin:6px 0 14px;color:#6ee7ff}
.panel{max-width:360px;margin:0 auto}
canvas{width:240px;height:240px;background:#000;border:1px solid #2a2f3a;border-radius:10px}
input,button{width:100%;box-sizing:border-box;padding:12px;margin:8px 0;border:none;border-radius:10px;font-size:15px}
input{background:#1c2330;color:#e6e6e6}
button{background:#6ee7ff;color:#00131a;font-weight:bold;cursor:pointer}
button.alt{background:#2a3445;color:#d8e9ff}
button:disabled{opacity:.5;cursor:not-allowed}
.info{font-size:13px;color:#9fb1c7;margin:6px 0}
#status{color:#8ef58e}
</style>
</head>
<body>
<div class='panel'>
<h2>Image Upload</h2>
<input id='file' type='file' accept='image/*'>
<button id='paste' class='alt' onclick='pasteImage()'>Paste from Clipboard</button>
<canvas id='preview' width='240' height='240'></canvas>
<div class='info' id='meta'>Choose an image file or press Ctrl+V / Cmd+V</div>
<button id='send' onclick='sendImage()' disabled>Upload to ESP</button>
<div class='info' id='status'>Idle</div>
</div>
<script>
const SIZE=240;
const fileEl=document.getElementById('file');
const pasteBtn=document.getElementById('paste');
const canvas=document.getElementById('preview');
const ctx=canvas.getContext('2d',{willReadFrequently:true});
const metaEl=document.getElementById('meta');
const statusEl=document.getElementById('status');
const sendBtn=document.getElementById('send');
let prepared=false;

ctx.fillStyle='#000';
ctx.fillRect(0,0,SIZE,SIZE);
if(!navigator.clipboard || !navigator.clipboard.read){
  pasteBtn.textContent='Paste (Ctrl+V / Cmd+V)';
}

function prepareToSquare(img){
  const scale=SIZE/Math.min(img.naturalWidth,img.naturalHeight);
  const w=img.naturalWidth*scale;
  const h=img.naturalHeight*scale;
  const x=(SIZE-w)/2;
  const y=(SIZE-h)/2;

  ctx.fillStyle='#000';
  ctx.fillRect(0,0,SIZE,SIZE);
  ctx.drawImage(img,x,y,w,h);

  prepared=true;
  sendBtn.disabled=false;
  metaEl.textContent='Source: '+img.naturalWidth+'x'+img.naturalHeight+' -> 240x240 (center-crop by wide side)';
}

async function loadImageBlob(blob, sourceLabel){
  prepared=false;
  sendBtn.disabled=true;
  if(!blob){
    statusEl.textContent='No image data';
    return;
  }
  const img=new Image();
  let objectUrl='';
  try{
    objectUrl=URL.createObjectURL(blob);
    await new Promise((resolve,reject)=>{
      img.onload=resolve;
      img.onerror=reject;
      img.src=objectUrl;
    });
    prepareToSquare(img);
    statusEl.textContent='Ready ('+sourceLabel+')';
  }catch(e){
    statusEl.textContent='Image decode error';
  }finally{
    if(objectUrl) URL.revokeObjectURL(objectUrl);
  }
}

fileEl.addEventListener('change',async()=>{
  const file=fileEl.files&&fileEl.files[0];
  prepared=false;
  sendBtn.disabled=true;
  if(!file){
    metaEl.textContent='Choose an image file or press Ctrl+V / Cmd+V';
    statusEl.textContent='Idle';
    return;
  }
  await loadImageBlob(file,'file');
});

async function pasteImage(){
  if(!navigator.clipboard || !navigator.clipboard.read){
    statusEl.textContent='Clipboard API unavailable. Use Ctrl+V / Cmd+V.';
    return;
  }
  try{
    statusEl.textContent='Reading clipboard...';
    const items=await navigator.clipboard.read();
    for(const item of items){
      const type=item.types.find(t=>t.startsWith('image/'));
      if(type){
        const blob=await item.getType(type);
        await loadImageBlob(blob,'clipboard');
        return;
      }
    }
    statusEl.textContent='Clipboard has no image';
  }catch(e){
    statusEl.textContent='Clipboard read blocked. Try Ctrl+V / Cmd+V.';
  }
}

window.addEventListener('paste',async(e)=>{
  const items=e.clipboardData&&e.clipboardData.items;
  if(!items) return;
  for(let i=0;i<items.length;i++){
    const it=items[i];
    if(it.type&&it.type.startsWith('image/')){
      e.preventDefault();
      const file=it.getAsFile();
      await loadImageBlob(file,'clipboard');
      return;
    }
  }
});

function canvasToRGB565Buffer(){
  const rgba=ctx.getImageData(0,0,SIZE,SIZE).data;
  const pixels=SIZE*SIZE;
  const buf=new ArrayBuffer(pixels*2);
  const view=new DataView(buf);
  for(let i=0;i<pixels;i++){
    const r=rgba[i*4],g=rgba[i*4+1],b=rgba[i*4+2];
    const rgb565=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);
    view.setUint16(i*2,rgb565,true);
  }
  return buf;
}

async function sendImage(){
  if(!prepared){
    statusEl.textContent='Pick an image first';
    return;
  }
  try{
    sendBtn.disabled=true;
    statusEl.textContent='Converting...';
    const rgb565=canvasToRGB565Buffer();
    const formData=new FormData();
    formData.append('frame',new Blob([rgb565],{type:'application/octet-stream'}),'image.rgb565');
    statusEl.textContent='Uploading...';
    const t0=performance.now();
    const resp=await fetch('/upload',{method:'POST',body:formData});
    const text=await resp.text();
    if(!resp.ok) throw new Error(text||('HTTP '+resp.status));
    const ms=Math.round(performance.now()-t0);
    statusEl.textContent='Done in '+ms+' ms: '+text;
  }catch(e){
    statusEl.textContent='Upload failed: '+e.message;
  }finally{
    sendBtn.disabled=false;
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

// ===================== Upload handlers =====================

void handleImageUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadSeen = true;
    uploadByteCount = 0;
    lineFill = 0;
    currentLine = 0;
    uploadFailed = false;
    tft.startWrite();
    tft.setSwapBytes(true);
    uploadDrawing = true;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed) return;

    size_t i = 0;
    while (i < upload.currentSize) {
      if (uploadByteCount >= FRAME_BYTES) {
        uploadFailed = true;
        break;
      }

      size_t freeInLine = LINE_BYTES - lineFill;
      size_t chunkRemaining = upload.currentSize - i;
      size_t frameRemaining = FRAME_BYTES - uploadByteCount;
      size_t take = chunkRemaining;
      if (take > freeInLine) take = freeInLine;
      if (take > frameRemaining) take = frameRemaining;
      if (take == 0) {
        uploadFailed = true;
        break;
      }

      memcpy(lineBuffer.bytes + lineFill, upload.buf + i, take);
      lineFill += take;
      uploadByteCount += take;
      i += take;

      if (lineFill == LINE_BYTES) {
        if (currentLine >= DISPLAY_SIZE) {
          uploadFailed = true;
          break;
        }
        tft.pushImage(0, currentLine, DISPLAY_SIZE, 1, lineBuffer.pixels);
        currentLine++;
        lineFill = 0;
        if ((currentLine & 0x0F) == 0) yield();
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (lineFill != 0 || currentLine != DISPLAY_SIZE || uploadByteCount != FRAME_BYTES) {
      uploadFailed = true;
    }
    finishUploadDraw();
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadFailed = true;
    finishUploadDraw();
  }
}

void handleUploadDone() {
  finishUploadDraw();
  if (!uploadSeen) {
    server.send(400, "text/plain", "bad:nofile");
    return;
  }
  uploadSeen = false;

  if (uploadFailed) {
    server.send(400, "text/plain", "bad:upload");
    return;
  }
  if (uploadByteCount != FRAME_BYTES || currentLine != DISPLAY_SIZE || lineFill != 0) {
    server.send(400, "text/plain", "bad:size");
    return;
  }

  imageCounter++;
  server.send(200, "text/plain", "ok#" + String(imageCounter));
}

// ===================== WiFi setup handlers =====================

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
    server.send_P(200, "text/html", IMAGE_HTML);
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
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/upload", HTTP_POST, handleUploadDone, handleImageUpload);
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
