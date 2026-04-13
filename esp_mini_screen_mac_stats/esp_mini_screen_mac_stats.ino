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

// --- Payload sizing ---
#define UPDATED_AT_LEN    32
#define MEMORY_TEXT_LEN   24
#define MAX_VISIBLE_CORES 16
#define FOOTER_TEXT_LEN   48

// --- Dashboard layout ---
#define DASHBOARD_TITLE_Y     6
#define DASHBOARD_STATUS_Y    28
#define DASHBOARD_STATUS_H    12
#define DASHBOARD_MEMORY_Y    42
#define DASHBOARD_MEMORY_H    22
#define DASHBOARD_CORES_Y     66
#define DASHBOARD_TITLE_H     10
#define DASHBOARD_SECTION_GAP 2
#define DASHBOARD_WAITING_Y   80
#define DASHBOARD_WAITING_H   72
#define DASHBOARD_FOOTER_Y    226
#define DASHBOARD_FOOTER_H    12

const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";

const uint16_t COLOR_BG = 0x0841;
const uint16_t COLOR_MUTED = 0x8410;
const uint16_t COLOR_TEXT = TFT_WHITE;
const uint16_t COLOR_BAR_BG = 0x2104;
const uint16_t COLOR_BORDER = 0x31C7;
const uint16_t COLOR_PERF = 0xFC60;
const uint16_t COLOR_EFF = 0x2E9F;
const uint16_t COLOR_RAM = 0x867F;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite lineSprite = TFT_eSprite(&tft);
TFT_eSprite memoryRowSprite = TFT_eSprite(&tft);
TFT_eSprite coreRowSprite = TFT_eSprite(&tft);
ESP8266WebServer server(80);

String scannedNetworks = "";

int performanceCount = 0;
int efficiencyCount = 0;
int coreLoads[MAX_VISIBLE_CORES];
char updatedAt[UPDATED_AT_LEN] = "";
char memoryText[MEMORY_TEXT_LEN] = "";
int memoryPercent = 0;
bool hasUploadedData = false;
unsigned long updateCounter = 0;
bool dashboardDirty = false;
bool dashboardCacheValid = false;
bool drawnHasUploadedData = false;
int drawnPerformanceCount = -1;
int drawnEfficiencyCount = -1;
int drawnMemoryPercent = -1;
int drawnCoreLoads[MAX_VISIBLE_CORES];
char drawnUpdatedAt[UPDATED_AT_LEN] = "";
char drawnMemoryText[MEMORY_TEXT_LEN] = "";
char drawnFooter[FOOTER_TEXT_LEN] = "";
bool lineSpriteReady = false;
bool memoryRowSpriteReady = false;
bool coreRowSpriteReady = false;
int coreRowSpriteHeight = 0;

void startServer();
void startAPMode();
void renderDashboard();

struct DashboardLayout {
  int rowHeight;
  int perfTitleY;
  int perfRowsY;
  int effTitleY;
  int effRowsY;
};

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

int clampPercent(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return value;
}

int clampValue(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

void copyToBuffer(char* dst, size_t dstSize, const String& src) {
  String trimmed = src;
  trimmed.trim();
  size_t n = trimmed.length();
  if (n >= dstSize) n = dstSize - 1;
  memcpy(dst, trimmed.c_str(), n);
  dst[n] = 0;
}

bool parseIntClamped(const String& raw, int minValue, int maxValue, int& out) {
  String value = raw;
  value.trim();
  if (value.length() == 0)
    return false;

  char* endPtr = nullptr;
  long parsed = strtol(value.c_str(), &endPtr, 10);
  if (endPtr == value.c_str())
    return false;

  out = clampValue((int)parsed, minValue, maxValue);
  return true;
}

bool parseCoreLoadsCsv(const String& raw, int* values, int maxValues, int& parsedCount) {
  String source = raw;
  source.trim();
  if (source.length() == 0)
    return false;

  parsedCount = 0;
  int start = 0;

  while (start <= source.length()) {
    int comma = source.indexOf(',', start);
    if (comma < 0)
      comma = source.length();

    String token = source.substring(start, comma);
    token.trim();
    if (token.length() == 0)
      return false;

    if (parsedCount >= maxValues)
      return false;

    int load = 0;
    if (!parseIntClamped(token, 0, 100, load))
      return false;

    values[parsedCount++] = load;

    if (comma >= source.length())
      break;
    start = comma + 1;
  }

  return parsedCount > 0;
}

template <typename Surface>
String fitTextOn(Surface& surface, const char* text, int maxWidth) {
  String value = String(text);
  if (value.length() == 0)
    return value;
  if (surface.textWidth(value) <= maxWidth)
    return value;

  while (value.length() > 1) {
    value.remove(value.length() - 1);
    String candidate = value + "...";
    if (surface.textWidth(candidate) <= maxWidth)
      return candidate;
  }
  return "...";
}

String fitText(const char* text, int maxWidth) {
  return fitTextOn(tft, text, maxWidth);
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

String buildStateJson() {
  String json = "";
  json.reserve(360);

  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  json += "{\"wifiConnected\":";
  json += ((WiFi.status() == WL_CONNECTED) ? "true" : "false");
  json += ",\"ip\":\"";
  json += jsonEscape(ip.c_str());
  json += "\",\"updatedAt\":\"";
  json += jsonEscape(updatedAt);
  json += "\",\"memoryText\":\"";
  json += jsonEscape(memoryText);
  json += "\",\"memoryPercent\":";
  json += String(memoryPercent);
  json += ",\"performanceCount\":";
  json += String(performanceCount);
  json += ",\"efficiencyCount\":";
  json += String(efficiencyCount);
  json += ",\"hasData\":";
  json += (hasUploadedData ? "true" : "false");
  json += ",\"updates\":";
  json += String(updateCounter);
  json += ",\"coreLoads\":[";

  int totalCores = performanceCount + efficiencyCount;
  for (int i = 0; i < totalCores; i++) {
    if (i > 0) json += ",";
    json += String(coreLoads[i]);
  }

  json += "]}";
  return json;
}

// ===================== Display helpers =====================

void invalidateDashboardCache() {
  dashboardCacheValid = false;
  drawnHasUploadedData = false;
  drawnPerformanceCount = -1;
  drawnEfficiencyCount = -1;
  drawnMemoryPercent = -1;
  drawnUpdatedAt[0] = 0;
  drawnMemoryText[0] = 0;
  drawnFooter[0] = 0;
  for (int i = 0; i < MAX_VISIBLE_CORES; i++) {
    drawnCoreLoads[i] = -1;
  }
}

bool ensureLineSprite() {
  if (lineSpriteReady)
    return true;

  lineSprite.setColorDepth(16);
  lineSpriteReady = lineSprite.createSprite(DISPLAY_SIZE, DASHBOARD_STATUS_H) != nullptr;
  return lineSpriteReady;
}

bool ensureMemoryRowSprite() {
  if (memoryRowSpriteReady)
    return true;

  memoryRowSprite.setColorDepth(16);
  memoryRowSpriteReady = memoryRowSprite.createSprite(DISPLAY_SIZE, DASHBOARD_MEMORY_H) != nullptr;
  return memoryRowSpriteReady;
}

bool ensureCoreRowSprite(int rowHeight) {
  if (coreRowSpriteReady && coreRowSpriteHeight == rowHeight)
    return true;

  if (coreRowSpriteReady) {
    coreRowSprite.deleteSprite();
    coreRowSpriteReady = false;
    coreRowSpriteHeight = 0;
  }

  coreRowSprite.setColorDepth(16);
  coreRowSpriteReady = coreRowSprite.createSprite(DISPLAY_SIZE, rowHeight) != nullptr;
  if (coreRowSpriteReady) {
    coreRowSpriteHeight = rowHeight;
  }
  return coreRowSpriteReady;
}

void displayCentered(const String& text, int y, int size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, COLOR_BG);
  int16_t tw = tft.textWidth(text);
  tft.setCursor((DISPLAY_SIZE - tw) / 2, y);
  tft.print(text);
}

void showAPInfo() {
  invalidateDashboardCache();
  dashboardDirty = false;
  tft.fillScreen(COLOR_BG);
  displayCentered("WiFi Setup", 10, 2, TFT_YELLOW);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(10, 50);
  tft.print("Connect to WiFi:");
  tft.setCursor(10, 70);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, COLOR_BG);
  tft.print(AP_SSID);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(10, 100);
  tft.print("Password:");
  tft.setCursor(10, 120);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, COLOR_BG);
  tft.print(AP_PASS);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(10, 155);
  tft.print("Then open:");
  tft.setCursor(10, 175);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.print("192.168.4.1");
}

void showConnecting(const String& ssid) {
  invalidateDashboardCache();
  dashboardDirty = false;
  tft.fillScreen(COLOR_BG);
  displayCentered("Connecting...", 80, 2, TFT_YELLOW);
  displayCentered(ssid, 120, 1, COLOR_TEXT);
}

void showConnectionFailed() {
  invalidateDashboardCache();
  dashboardDirty = false;
  tft.fillScreen(COLOR_BG);
  displayCentered("WiFi failed", 80, 2, TFT_RED);
  displayCentered("Starting AP...", 120, 1, COLOR_TEXT);
  delay(2000);
}

int computeCoreRowHeight(int perfCount, int effCount) {
  int totalCores = perfCount + effCount;
  if (totalCores <= 0)
    return 10;

  int sectionHeaders = 0;
  if (perfCount > 0) sectionHeaders++;
  if (effCount > 0) sectionHeaders++;

  int availableHeight = DASHBOARD_FOOTER_Y - DASHBOARD_CORES_Y;
  int rowHeight = (availableHeight - sectionHeaders * DASHBOARD_TITLE_H) / totalCores;
  return clampValue(rowHeight, 8, 12);
}

DashboardLayout buildDashboardLayout() {
  DashboardLayout layout;
  layout.rowHeight = computeCoreRowHeight(performanceCount, efficiencyCount);
  layout.perfTitleY = -1;
  layout.perfRowsY = -1;
  layout.effTitleY = -1;
  layout.effRowsY = -1;

  int y = DASHBOARD_CORES_Y;
  if (performanceCount > 0) {
    layout.perfTitleY = y;
    y += DASHBOARD_TITLE_H;
    layout.perfRowsY = y;
    y += performanceCount * layout.rowHeight;
  }

  if (performanceCount > 0 && efficiencyCount > 0)
    y += DASHBOARD_SECTION_GAP;

  if (efficiencyCount > 0) {
    layout.effTitleY = y;
    y += DASHBOARD_TITLE_H;
    layout.effRowsY = y;
  }

  return layout;
}

String buildStatusText() {
  tft.setTextSize(1);
  if (!hasUploadedData)
    return String("Connect sender and upload stats");

  String stamp = updatedAt[0] ? (String("Updated: ") + updatedAt) : String("Updated via /stats");
  return fitText(stamp.c_str(), DISPLAY_SIZE - 20);
}

String buildFooterText() {
  tft.setTextSize(1);
  String footer = (WiFi.status() == WL_CONNECTED)
    ? ("http://" + WiFi.localIP().toString() + "/stats")
    : String("Finish WiFi setup first");
  return fitText(footer.c_str(), DISPLAY_SIZE - 20);
}

void syncDashboardCache(const String& footer) {
  dashboardCacheValid = true;
  drawnHasUploadedData = hasUploadedData;
  drawnPerformanceCount = performanceCount;
  drawnEfficiencyCount = efficiencyCount;
  drawnMemoryPercent = memoryPercent;
  copyToBuffer(drawnUpdatedAt, sizeof(drawnUpdatedAt), String(updatedAt));
  copyToBuffer(drawnMemoryText, sizeof(drawnMemoryText), String(memoryText));
  copyToBuffer(drawnFooter, sizeof(drawnFooter), footer);

  int totalCores = performanceCount + efficiencyCount;
  for (int i = 0; i < MAX_VISIBLE_CORES; i++) {
    drawnCoreLoads[i] = (i < totalCores) ? coreLoads[i] : -1;
  }
}

void drawSectionTitle(int y, const char* label, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(color, COLOR_BG);
  tft.setCursor(10, y);
  tft.print(label);

  int lineX = 10 + tft.textWidth(label) + 6;
  if (lineX < DISPLAY_SIZE - 10) {
    tft.drawFastHLine(lineX, y + 4, DISPLAY_SIZE - lineX - 10, color);
  }
}

void drawStatusLine() {
  tft.fillRect(0, DASHBOARD_STATUS_Y, DISPLAY_SIZE, DASHBOARD_STATUS_H, COLOR_BG);
  tft.setTextSize(1);

  String text = buildStatusText();
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  int16_t width = tft.textWidth(text);
  tft.setCursor((DISPLAY_SIZE - width) / 2, DASHBOARD_STATUS_Y);
  tft.print(text);
}

void drawStatusLineSprite() {
  if (!ensureLineSprite()) {
    drawStatusLine();
    return;
  }

  lineSprite.fillSprite(COLOR_BG);
  lineSprite.setTextSize(1);

  String text = buildStatusText();
  lineSprite.setTextColor(COLOR_MUTED, COLOR_BG);
  int16_t width = lineSprite.textWidth(text);
  lineSprite.setCursor((DISPLAY_SIZE - width) / 2, 0);
  lineSprite.print(text);
  lineSprite.pushSprite(0, DASHBOARD_STATUS_Y);
}

template <typename Surface>
void drawUsageBarOn(Surface& surface, int x, int y, int width, int height, int percent, uint16_t accent) {
  int clamped = clampPercent(percent);
  surface.fillRect(x, y, width, height, COLOR_BAR_BG);
  int fillWidth = 0;
  if (clamped > 0) {
    fillWidth = (width * clamped + 50) / 100;
  }
  if (fillWidth > 0) {
    surface.fillRect(x, y, fillWidth, height, accent);
  }
  surface.drawRect(x, y, width, height, COLOR_BORDER);
}

void drawUsageBar(int x, int y, int width, int height, int percent, uint16_t accent) {
  drawUsageBarOn(tft, x, y, width, height, percent, accent);
}

template <typename Surface>
void drawMemoryRowContentOn(Surface& surface, int y) {
  surface.setTextSize(1);
  surface.setTextColor(COLOR_RAM, COLOR_BG);
  surface.setCursor(10, y);
  surface.print("RAM");

  String value = memoryText[0] ? fitTextOn(surface, memoryText, 126) : String("waiting");
  surface.setTextColor(memoryText[0] ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
  surface.setCursor(38, y);
  surface.print(value);

  String percentText = String(memoryPercent) + "%";
  int16_t percentWidth = surface.textWidth(percentText);
  surface.setCursor(DISPLAY_SIZE - 10 - percentWidth, y);
  surface.print(percentText);

  drawUsageBarOn(surface, 10, y + 10, DISPLAY_SIZE - 20, 8, memoryPercent, COLOR_RAM);
}

void drawMemoryRow(int y) {
  tft.fillRect(0, y, DISPLAY_SIZE, DASHBOARD_MEMORY_H, COLOR_BG);
  drawMemoryRowContentOn(tft, y);
}

void drawMemoryRowSprite() {
  if (!ensureMemoryRowSprite()) {
    drawMemoryRow(DASHBOARD_MEMORY_Y);
    return;
  }

  memoryRowSprite.fillSprite(COLOR_BG);
  drawMemoryRowContentOn(memoryRowSprite, 0);
  memoryRowSprite.pushSprite(0, DASHBOARD_MEMORY_Y);
}

template <typename Surface>
void drawCoreRowContentOn(Surface& surface, int y, int rowHeight, const char* prefix, int index, int load, uint16_t accent) {
  surface.setTextSize(1);

  String label = String(prefix) + String(index + 1);
  surface.setTextColor(COLOR_TEXT, COLOR_BG);
  surface.setCursor(10, y);
  surface.print(label);

  String value = String(load) + "%";
  int16_t valueWidth = surface.textWidth(value);
  int barX = 32;
  int barWidth = DISPLAY_SIZE - barX - valueWidth - 16;
  int barHeight = rowHeight - 3;
  if (barHeight < 5)
    barHeight = 5;

  drawUsageBarOn(surface, barX, y + 1, barWidth, barHeight, load, accent);

  surface.setCursor(DISPLAY_SIZE - 10 - valueWidth, y);
  surface.print(value);
}

void drawCoreRow(int y, int rowHeight, const char* prefix, int index, int load, uint16_t accent) {
  tft.fillRect(0, y, DISPLAY_SIZE, rowHeight, COLOR_BG);
  drawCoreRowContentOn(tft, y, rowHeight, prefix, index, load, accent);
}

void drawCoreRowSprite(int y, int rowHeight, const char* prefix, int index, int load, uint16_t accent) {
  if (!ensureCoreRowSprite(rowHeight)) {
    drawCoreRow(y, rowHeight, prefix, index, load, accent);
    return;
  }

  coreRowSprite.fillSprite(COLOR_BG);
  drawCoreRowContentOn(coreRowSprite, 0, rowHeight, prefix, index, load, accent);
  coreRowSprite.pushSprite(0, y);
}

void drawCoreSection(int& y, const char* title, const char* prefix, int count, int startIndex, int rowHeight, uint16_t accent) {
  if (count <= 0)
    return;

  drawSectionTitle(y, title, accent);
  y += DASHBOARD_TITLE_H;

  for (int i = 0; i < count; i++) {
    drawCoreRow(y, rowHeight, prefix, i, coreLoads[startIndex + i], accent);
    y += rowHeight;
  }
}

void renderWaitingState() {
  tft.fillRect(0, DASHBOARD_WAITING_Y, DISPLAY_SIZE, DASHBOARD_WAITING_H, COLOR_BG);
  displayCentered("Waiting for Mac sender", 88, 1, COLOR_MUTED);
  displayCentered("POST /stats", 108, 2, COLOR_TEXT);
  displayCentered("from tools/send_mac_stats.py", 136, 1, COLOR_MUTED);
}

void renderFooter() {
  String footer = buildFooterText();
  tft.fillRect(0, DASHBOARD_FOOTER_Y, DISPLAY_SIZE, DASHBOARD_FOOTER_H, COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  int16_t footerWidth = tft.textWidth(footer);
  tft.setCursor((DISPLAY_SIZE - footerWidth) / 2, DASHBOARD_FOOTER_Y);
  tft.print(footer);
}

void renderFooterSprite() {
  if (!ensureLineSprite()) {
    renderFooter();
    return;
  }

  String footer = buildFooterText();
  lineSprite.fillSprite(COLOR_BG);
  lineSprite.setTextSize(1);
  lineSprite.setTextColor(COLOR_MUTED, COLOR_BG);
  int16_t footerWidth = lineSprite.textWidth(footer);
  lineSprite.setCursor((DISPLAY_SIZE - footerWidth) / 2, 0);
  lineSprite.print(footer);
  lineSprite.pushSprite(0, DASHBOARD_FOOTER_Y);
}

void renderDashboardFull() {
  tft.fillScreen(COLOR_BG);
  displayCentered("Mac Monitor", DASHBOARD_TITLE_Y, 2, COLOR_TEXT);
  drawStatusLine();

  if (!hasUploadedData) {
    renderWaitingState();
    renderFooter();
    syncDashboardCache(buildFooterText());
    return;
  }

  drawMemoryRow(DASHBOARD_MEMORY_Y);

  DashboardLayout layout = buildDashboardLayout();
  int y = DASHBOARD_CORES_Y;
  if (performanceCount > 0) {
    drawCoreSection(y, "Performance", "P", performanceCount, 0, layout.rowHeight, COLOR_PERF);
  }
  if (performanceCount > 0 && efficiencyCount > 0)
    y += DASHBOARD_SECTION_GAP;
  if (efficiencyCount > 0) {
    drawCoreSection(y, "Efficiency", "E", efficiencyCount, performanceCount, layout.rowHeight, COLOR_EFF);
  }

  renderFooter();
  syncDashboardCache(buildFooterText());
}

void renderDashboardPartial() {
  DashboardLayout layout = buildDashboardLayout();

  if (strcmp(drawnUpdatedAt, updatedAt) != 0) {
    drawStatusLineSprite();
  }

  if (strcmp(drawnMemoryText, memoryText) != 0 || drawnMemoryPercent != memoryPercent) {
    drawMemoryRowSprite();
  }

  for (int i = 0; i < performanceCount; i++) {
    if (drawnCoreLoads[i] != coreLoads[i]) {
      drawCoreRowSprite(layout.perfRowsY + i * layout.rowHeight, layout.rowHeight, "P", i, coreLoads[i], COLOR_PERF);
    }
  }

  for (int i = 0; i < efficiencyCount; i++) {
    int coreIndex = performanceCount + i;
    if (drawnCoreLoads[coreIndex] != coreLoads[coreIndex]) {
      drawCoreRowSprite(layout.effRowsY + i * layout.rowHeight, layout.rowHeight, "E", i, coreLoads[coreIndex], COLOR_EFF);
    }
  }

  String footer = buildFooterText();
  if (strcmp(drawnFooter, footer.c_str()) != 0) {
    renderFooterSprite();
  }

  syncDashboardCache(footer);
}

void renderDashboard() {
  if (!dashboardCacheValid ||
      drawnHasUploadedData != hasUploadedData ||
      drawnPerformanceCount != performanceCount ||
      drawnEfficiencyCount != efficiencyCount) {
    renderDashboardFull();
    return;
  }

  if (!hasUploadedData) {
    String footer = buildFooterText();
    if (strcmp(drawnFooter, footer.c_str()) != 0) {
      renderFooter();
      syncDashboardCache(footer);
    }
    return;
  }

  renderDashboardPartial();
}

// ===================== Web pages =====================

const char STATS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Mac Monitor</title>
<style>
body{font-family:sans-serif;background:#11161f;color:#e8eef6;margin:0;padding:16px}
.panel{max-width:440px;margin:0 auto}
h2{margin:0 0 8px;color:#9ef0ff}
.sub{margin:0 0 16px;color:#9fb2c8;font-size:14px;line-height:1.4}
label{display:block;font-size:13px;color:#b6c6da;margin:12px 0}
input{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:1px solid #31415a;border-radius:10px;background:#0f1520;color:#eef6ff}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
button{flex:1;min-width:120px;padding:12px;border:none;border-radius:10px;background:#9ef0ff;color:#07202a;font-weight:700;cursor:pointer}
button.alt{background:#28364b;color:#dce9f8}
#status{margin-top:12px;color:#9ef0a8;min-height:20px}
pre{background:#0b1018;border:1px solid #263345;border-radius:12px;padding:12px;white-space:pre-wrap;word-break:break-word;color:#a7f5ba}
</style>
</head>
<body>
<div class='panel'>
  <h2>Mac Monitor</h2>
  <p class='sub'>Test <code>POST /stats</code>. Core loads use CSV in P-then-E order, for example <code>41,55,38,67,12,14,9,11</code>.</p>
  <form id='form'>
    <label>Updated At
      <input name='updatedAt' placeholder='22:15:04'>
    </label>
    <label>Memory text
      <input name='memoryText' placeholder='12.3/16.0 GB'>
    </label>
    <label>Memory percent
      <input name='memoryPercent' type='number' min='0' max='100' placeholder='77'>
    </label>
    <label>Performance cores
      <input name='performanceCount' type='number' min='0' max='16' placeholder='4'>
    </label>
    <label>Efficiency cores
      <input name='efficiencyCount' type='number' min='0' max='16' placeholder='6'>
    </label>
    <label>Core loads CSV
      <input name='coreLoads' placeholder='41,55,38,67,12,14,9,11,8,7'>
    </label>

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

function syncForm(state){
  setValue('updatedAt', state && state.updatedAt ? state.updatedAt : '');
  setValue('memoryText', state && state.memoryText ? state.memoryText : '');
  setValue('memoryPercent', state ? state.memoryPercent : '');
  setValue('performanceCount', state ? state.performanceCount : '');
  setValue('efficiencyCount', state ? state.efficiencyCount : '');
  setValue('coreLoads', state && Array.isArray(state.coreLoads) ? state.coreLoads.join(',') : '');
}

function fillDemo(){
  const stamp = new Date().toLocaleTimeString('en-GB', {hour12:false});
  setValue('updatedAt', stamp);
  setValue('memoryText', '12.3/16.0 GB');
  setValue('memoryPercent', 77);
  setValue('performanceCount', 4);
  setValue('efficiencyCount', 6);
  setValue('coreLoads', '41,55,38,67,12,14,9,11,8,7');
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
    const resp = await fetch('/stats', {
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
    server.send_P(200, "text/html", STATS_HTML);
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

void handleStatsUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"wifi_not_connected\"}");
    return;
  }

  bool touched = false;

  if (server.hasArg("updatedAt")) {
    copyToBuffer(updatedAt, sizeof(updatedAt), server.arg("updatedAt"));
    touched = true;
  }

  if (server.hasArg("memoryText")) {
    copyToBuffer(memoryText, sizeof(memoryText), server.arg("memoryText"));
    touched = true;
  }

  if (server.hasArg("memoryPercent")) {
    int parsedMemory = memoryPercent;
    if (!parseIntClamped(server.arg("memoryPercent"), 0, 100, parsedMemory)) {
      server.send(400, "application/json", "{\"error\":\"invalid_memory_percent\"}");
      return;
    }
    memoryPercent = parsedMemory;
    touched = true;
  }

  bool cpuArgPresent = server.hasArg("performanceCount") || server.hasArg("efficiencyCount") || server.hasArg("coreLoads");
  if (cpuArgPresent) {
    if (!server.hasArg("performanceCount") || !server.hasArg("efficiencyCount") || !server.hasArg("coreLoads")) {
      server.send(400, "application/json", "{\"error\":\"cpu_fields_must_be_sent_together\"}");
      return;
    }

    int nextPerformanceCount = 0;
    int nextEfficiencyCount = 0;
    if (!parseIntClamped(server.arg("performanceCount"), 0, MAX_VISIBLE_CORES, nextPerformanceCount)) {
      server.send(400, "application/json", "{\"error\":\"invalid_performance_count\"}");
      return;
    }
    if (!parseIntClamped(server.arg("efficiencyCount"), 0, MAX_VISIBLE_CORES, nextEfficiencyCount)) {
      server.send(400, "application/json", "{\"error\":\"invalid_efficiency_count\"}");
      return;
    }

    int nextCoreLoads[MAX_VISIBLE_CORES];
    int parsedCount = 0;
    if (!parseCoreLoadsCsv(server.arg("coreLoads"), nextCoreLoads, MAX_VISIBLE_CORES, parsedCount)) {
      server.send(400, "application/json", "{\"error\":\"invalid_core_loads\"}");
      return;
    }

    if (parsedCount != nextPerformanceCount + nextEfficiencyCount) {
      server.send(400, "application/json", "{\"error\":\"core_count_mismatch\"}");
      return;
    }

    performanceCount = nextPerformanceCount;
    efficiencyCount = nextEfficiencyCount;
    for (int i = 0; i < parsedCount; i++) {
      coreLoads[i] = nextCoreLoads[i];
    }
    touched = true;
  }

  if (!touched) {
    server.send(400, "application/json", "{\"error\":\"no_fields\"}");
    return;
  }

  hasUploadedData = (performanceCount + efficiencyCount) > 0 || memoryText[0] != 0 || updatedAt[0] != 0;
  updateCounter++;
  dashboardDirty = true;

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
  server.on("/stats", HTTP_POST, handleStatsUpdate);
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
  ensureLineSprite();
  ensureMemoryRowSprite();

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
  if (dashboardDirty) {
    renderDashboard();
    dashboardDirty = false;
  }
}
