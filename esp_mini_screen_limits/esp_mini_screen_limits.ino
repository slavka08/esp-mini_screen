#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include <stdlib.h>
#include <string.h>

// --- Display size ---
#define DISPLAY_SIZE 240

// --- EEPROM layout (compatible with other sketches) ---
#define EEPROM_SIZE 96
#define SSID_ADDR   0
#define PASS_ADDR   32
#define MAX_SSID    32
#define MAX_PASS    64

// --- Limits payload sizing ---
#define METRIC_TEXT_LEN 24
#define UPDATED_AT_LEN  32

const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";

const uint16_t COLOR_BG = TFT_BLACK;
const uint16_t COLOR_CARD = 0x18C3;
const uint16_t COLOR_BORDER = 0x31C7;
const uint16_t COLOR_MUTED = 0x8410;
const uint16_t COLOR_BAR_BG = 0x2104;
const uint16_t COLOR_CODEX = 0x2E9F;
const uint16_t COLOR_CLAUDE = 0xFD20;

struct MetricState {
  char text[METRIC_TEXT_LEN];
  int percent;
  bool valid;
};

struct ProviderState {
  const char* name;
  uint16_t accent;
  MetricState daily;
  MetricState weekly;
};

TFT_eSPI tft = TFT_eSPI();
ESP8266WebServer server(80);

String scannedNetworks = "";

ProviderState codex = {"Codex", COLOR_CODEX, {"", 0, false}, {"", 0, false}};
ProviderState claude = {"Claude", COLOR_CLAUDE, {"", 0, false}, {"", 0, false}};

char updatedAt[UPDATED_AT_LEN] = "";
bool hasUploadedData = false;
unsigned long updateCounter = 0;

void startServer();
void startAPMode();
void renderDashboard();

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

// ===================== Generic helpers =====================

void copyToBuffer(char* dst, size_t dstSize, const String& src) {
  String trimmed = src;
  trimmed.trim();
  size_t n = trimmed.length();
  if (n >= dstSize) n = dstSize - 1;
  memcpy(dst, trimmed.c_str(), n);
  dst[n] = 0;
}

bool parsePercent(const String& raw, int& out) {
  String value = raw;
  value.trim();
  if (value.length() == 0)
    return false;

  char* endPtr = nullptr;
  long parsed = strtol(value.c_str(), &endPtr, 10);
  if (endPtr == value.c_str())
    return false;

  if (parsed < 0) parsed = 0;
  if (parsed > 100) parsed = 100;
  out = (int)parsed;
  return true;
}

void setMetricText(MetricState& metric, const String& text) {
  copyToBuffer(metric.text, sizeof(metric.text), text);
  metric.valid = metric.text[0] != 0;
}

bool providerHasData(const ProviderState& provider) {
  return provider.daily.valid || provider.weekly.valid;
}

String fitText(const char* text, int maxWidth) {
  String value = String(text);
  if (value.length() == 0)
    return value;
  if (tft.textWidth(value) <= maxWidth)
    return value;

  while (value.length() > 1) {
    value.remove(value.length() - 1);
    String candidate = value + "...";
    if (tft.textWidth(candidate) <= maxWidth)
      return candidate;
  }
  return "...";
}

String jsonEscape(const char* text) {
  String escaped = "";
  for (size_t i = 0; text[i] != 0; i++) {
    char c = text[i];
    if (c == '\\' || c == '"') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else {
      escaped += c;
    }
  }
  return escaped;
}

void appendMetricJson(String& json, const MetricState& metric) {
  json += "{\"text\":\"";
  json += jsonEscape(metric.valid ? metric.text : "");
  json += "\",\"percent\":";
  json += String(metric.percent);
  json += "}";
}

String buildStateJson() {
  String json = "";
  json.reserve(320);

  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  json += "{\"wifiConnected\":";
  json += ((WiFi.status() == WL_CONNECTED) ? "true" : "false");
  json += ",\"ip\":\"";
  json += jsonEscape(ip.c_str());
  json += "\",\"updatedAt\":\"";
  json += jsonEscape(updatedAt);
  json += "\",\"hasData\":";
  json += (hasUploadedData ? "true" : "false");
  json += ",\"updates\":";
  json += String(updateCounter);
  json += ",\"codex\":{\"daily\":";
  appendMetricJson(json, codex.daily);
  json += ",\"weekly\":";
  appendMetricJson(json, codex.weekly);
  json += "},\"claude\":{\"daily\":";
  appendMetricJson(json, claude.daily);
  json += ",\"weekly\":";
  appendMetricJson(json, claude.weekly);
  json += "}}";

  return json;
}

bool updateMetricFromArgs(const char* textKey, const char* percentKey, MetricState& metric) {
  bool touched = false;

  if (server.hasArg(textKey)) {
    setMetricText(metric, server.arg(textKey));
    touched = true;
  }

  if (server.hasArg(percentKey)) {
    int percent = metric.percent;
    if (parsePercent(server.arg(percentKey), percent)) {
      metric.percent = percent;
      touched = true;
      if (!metric.valid) {
        setMetricText(metric, String(percent) + "%");
      }
    }
  }

  return touched;
}

// ===================== Display helpers =====================

void displayCentered(const String& text, int y, int size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, COLOR_BG);
  int16_t tw = tft.textWidth(text);
  tft.setCursor((DISPLAY_SIZE - tw) / 2, y);
  tft.print(text);
}

void showAPInfo() {
  tft.fillScreen(COLOR_BG);
  displayCentered("WiFi Setup", 10, 2, TFT_YELLOW);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setCursor(10, 50);
  tft.print("Connect to WiFi:");
  tft.setCursor(10, 70);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, COLOR_BG);
  tft.print(AP_SSID);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setCursor(10, 100);
  tft.print("Password:");
  tft.setCursor(10, 120);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, COLOR_BG);
  tft.print(AP_PASS);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setCursor(10, 155);
  tft.print("Then open:");
  tft.setCursor(10, 175);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.print("192.168.4.1");
}

void showConnecting(const String& ssid) {
  tft.fillScreen(COLOR_BG);
  displayCentered("Connecting...", 80, 2, TFT_YELLOW);
  displayCentered(ssid, 120, 1, TFT_WHITE);
}

void showConnectionFailed() {
  tft.fillScreen(COLOR_BG);
  displayCentered("WiFi failed", 80, 2, TFT_RED);
  displayCentered("Starting AP...", 120, 1, TFT_WHITE);
  delay(2000);
}

void drawMetricRow(int x, int y, int width, const char* label, const MetricState& metric, uint16_t barColor) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_CARD);
  tft.setCursor(x, y);
  tft.print(label);

  String value = metric.valid ? fitText(metric.text, width - 44) : String("waiting");
  tft.setTextColor(metric.valid ? TFT_WHITE : COLOR_MUTED, COLOR_CARD);
  int16_t valueWidth = tft.textWidth(value);
  tft.setCursor(x + width - valueWidth, y);
  tft.print(value);

  int barY = y + 12;
  tft.fillRect(x, barY, width, 8, COLOR_BAR_BG);
  int fillWidth = 0;
  if (metric.valid && metric.percent > 0) {
    fillWidth = (width * metric.percent + 50) / 100;
  }
  if (fillWidth > 0) {
    tft.fillRect(x, barY, fillWidth, 8, barColor);
  }
  tft.drawRect(x, barY, width, 8, COLOR_BORDER);
}

void drawProviderCard(int y, const ProviderState& provider) {
  const int x = 10;
  const int width = DISPLAY_SIZE - 20;
  const int height = 84;

  tft.fillRect(x, y, width, height, COLOR_CARD);
  tft.drawRect(x, y, width, height, provider.accent);

  tft.setTextSize(2);
  tft.setTextColor(provider.accent, COLOR_CARD);
  tft.setCursor(x + 10, y + 8);
  tft.print(provider.name);

  drawMetricRow(x + 10, y + 34, width - 20, "Daily", provider.daily, provider.accent);
  drawMetricRow(x + 10, y + 58, width - 20, "Weekly", provider.weekly, provider.accent);
}

void renderDashboard() {
  tft.fillScreen(COLOR_BG);

  displayCentered("AI Limits", 8, 2, TFT_WHITE);

  tft.setTextSize(1);
  if (hasUploadedData) {
    String stamp = updatedAt[0] ? (String("Updated: ") + updatedAt) : String("Updated via /limits");
    stamp = fitText(stamp.c_str(), DISPLAY_SIZE - 20);
    tft.setTextColor(COLOR_MUTED, COLOR_BG);
    int16_t stampWidth = tft.textWidth(stamp);
    tft.setCursor((DISPLAY_SIZE - stampWidth) / 2, 32);
    tft.print(stamp);
  } else {
    displayCentered("Waiting for first upload", 32, 1, COLOR_MUTED);
  }

  drawProviderCard(46, codex);
  drawProviderCard(136, claude);

  String footer = "POST ";
  if (WiFi.status() == WL_CONNECTED) {
    footer += "http://" + WiFi.localIP().toString() + "/limits";
  } else {
    footer += "/limits after WiFi setup";
  }
  footer = fitText(footer.c_str(), DISPLAY_SIZE - 20);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  int16_t footerWidth = tft.textWidth(footer);
  tft.setCursor((DISPLAY_SIZE - footerWidth) / 2, 225);
  tft.print(footer);
}

// ===================== Web pages =====================

const char LIMITS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>AI Limits</title>
<style>
body{font-family:sans-serif;background:#11161f;color:#e8eef6;margin:0;padding:16px}
.panel{max-width:420px;margin:0 auto}
h2{margin:0 0 8px;color:#7ee7ff}
.sub{margin:0 0 16px;color:#9fb2c8;font-size:14px;line-height:1.4}
.card{background:#1a2230;border:1px solid #2b3a52;border-radius:12px;padding:14px;margin:12px 0}
.card h3{margin:0 0 10px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
label{display:block;font-size:13px;color:#b6c6da}
input{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:1px solid #31415a;border-radius:10px;background:#0f1520;color:#eef6ff}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
button{flex:1;min-width:120px;padding:12px;border:none;border-radius:10px;background:#7ee7ff;color:#07202a;font-weight:700;cursor:pointer}
button.alt{background:#28364b;color:#dce9f8}
#status{margin-top:12px;color:#9ef0a8;min-height:20px}
pre{background:#0b1018;border:1px solid #263345;border-radius:12px;padding:12px;white-space:pre-wrap;word-break:break-word;color:#a7f5ba}
</style>
</head>
<body>
<div class='panel'>
  <h2>AI Limits</h2>
  <p class='sub'>Use this page to test <code>POST /limits</code>. Empty fields are skipped, so you can update only one metric at a time.</p>
  <form id='form'>
    <label>Updated At
      <input name='updatedAt' placeholder='2026-04-13 22:15'>
    </label>

    <div class='card'>
      <h3>Codex</h3>
      <div class='grid'>
        <label>Daily text
          <input name='codexDailyText' placeholder='72% left'>
        </label>
        <label>Daily percent
          <input name='codexDailyPercent' type='number' min='0' max='100' placeholder='72'>
        </label>
        <label>Weekly text
          <input name='codexWeeklyText' placeholder='58% left'>
        </label>
        <label>Weekly percent
          <input name='codexWeeklyPercent' type='number' min='0' max='100' placeholder='58'>
        </label>
      </div>
    </div>

    <div class='card'>
      <h3>Claude</h3>
      <div class='grid'>
        <label>Daily text
          <input name='claudeDailyText' placeholder='4h 10m left'>
        </label>
        <label>Daily percent
          <input name='claudeDailyPercent' type='number' min='0' max='100' placeholder='35'>
        </label>
        <label>Weekly text
          <input name='claudeWeeklyText' placeholder='2d 03h left'>
        </label>
        <label>Weekly percent
          <input name='claudeWeeklyPercent' type='number' min='0' max='100' placeholder='61'>
        </label>
      </div>
    </div>

    <div class='actions'>
      <button type='button' class='alt' onclick='fillDemo()'>Load demo</button>
      <button type='submit'>Upload</button>
      <button type='button' class='alt' onclick='refreshState()'>Refresh state</button>
    </div>
  </form>

  <div id='status'>Idle</div>
  <pre id='state'>Loading...</pre>
</div>

<script>
const form = document.getElementById('form');
const statusEl = document.getElementById('status');
const stateEl = document.getElementById('state');

function setValue(name, value){
  const input = form.elements.namedItem(name);
  if(input) input.value = value == null ? '' : String(value);
}

function metricValue(state, providerKey, metricKey, field){
  if(!state || !state[providerKey] || !state[providerKey][metricKey]){
    return '';
  }
  const value = state[providerKey][metricKey][field];
  return value == null ? '' : value;
}

function syncForm(state){
  setValue('updatedAt', state && state.updatedAt ? state.updatedAt : '');
  setValue('codexDailyText', metricValue(state, 'codex', 'daily', 'text'));
  setValue('codexDailyPercent', metricValue(state, 'codex', 'daily', 'percent'));
  setValue('codexWeeklyText', metricValue(state, 'codex', 'weekly', 'text'));
  setValue('codexWeeklyPercent', metricValue(state, 'codex', 'weekly', 'percent'));
  setValue('claudeDailyText', metricValue(state, 'claude', 'daily', 'text'));
  setValue('claudeDailyPercent', metricValue(state, 'claude', 'daily', 'percent'));
  setValue('claudeWeeklyText', metricValue(state, 'claude', 'weekly', 'text'));
  setValue('claudeWeeklyPercent', metricValue(state, 'claude', 'weekly', 'percent'));
}

function fillDemo(){
  const stamp = new Date().toISOString().slice(0,16).replace('T',' ');
  setValue('updatedAt', stamp);
  setValue('codexDailyText', '72% left');
  setValue('codexDailyPercent', 72);
  setValue('codexWeeklyText', '58% left');
  setValue('codexWeeklyPercent', 58);
  setValue('claudeDailyText', '4h 10m left');
  setValue('claudeDailyPercent', 35);
  setValue('claudeWeeklyText', '2d 03h left');
  setValue('claudeWeeklyPercent', 61);
}

async function refreshState(){
  try{
    const resp = await fetch('/state');
    const data = await resp.json();
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    statusEl.textContent = 'State loaded';
  }catch(err){
    statusEl.textContent = 'State error: ' + err.message;
  }
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();

  const params = new URLSearchParams();
  for (const [key, value] of new FormData(form).entries()) {
    const text = String(value).trim();
    if (text) params.append(key, text);
  }

  if (!params.toString()) {
    statusEl.textContent = 'Nothing to upload';
    return;
  }

  try {
    statusEl.textContent = 'Uploading...';
    const resp = await fetch('/limits', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body: params.toString()
    });
    const body = await resp.json();
    if (!resp.ok) throw new Error(body.error || ('HTTP ' + resp.status));
    stateEl.textContent = JSON.stringify(body, null, 2);
    syncForm(body);
    statusEl.textContent = 'Uploaded';
  } catch (err) {
    statusEl.textContent = 'Upload error: ' + err.message;
  }
});

refreshState();
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

// ===================== Web handlers =====================

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
    server.send_P(200, "text/html", LIMITS_HTML);
    return;
  }

  String page = FPSTR(SETUP_HTML);
  page.replace("%NETWORKS%", scannedNetworks);
  server.send(200, "text/html", page);
}

void handleScan() {
  scanNetworks();
  String page = FPSTR(SETUP_HTML);
  page.replace("%NETWORKS%", scannedNetworks);
  server.send(200, "text/html", page);
}

void handleState() {
  server.send(200, "application/json", buildStateJson());
}

void handleLimitsUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"wifi_not_connected\"}");
    return;
  }

  bool touched = false;

  if (server.hasArg("updatedAt")) {
    copyToBuffer(updatedAt, sizeof(updatedAt), server.arg("updatedAt"));
    touched = true;
  }

  touched |= updateMetricFromArgs("codexDailyText", "codexDailyPercent", codex.daily);
  touched |= updateMetricFromArgs("codexWeeklyText", "codexWeeklyPercent", codex.weekly);
  touched |= updateMetricFromArgs("claudeDailyText", "claudeDailyPercent", claude.daily);
  touched |= updateMetricFromArgs("claudeWeeklyText", "claudeWeeklyPercent", claude.weekly);

  if (!touched) {
    server.send(400, "application/json", "{\"error\":\"no_fields\"}");
    return;
  }

  hasUploadedData = providerHasData(codex) || providerHasData(claude);
  updateCounter++;
  renderDashboard();

  server.send(200, "application/json", buildStateJson());
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
    renderDashboard();
    startServer();
  } else {
    showConnectionFailed();
    startAPMode();
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

// ===================== Server setup =====================

void startServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/state", HTTP_GET, handleState);
  server.on("/limits", HTTP_POST, handleLimitsUpdate);
  server.onNotFound(handleNotFound);
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
  tft.fillScreen(COLOR_BG);

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
      renderDashboard();
      startServer();
      return;
    }

    showConnectionFailed();
  }

  startAPMode();
}

void loop() {
  server.handleClient();
}
