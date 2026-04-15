#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include <math.h>
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
#define GPU_TEXT_LEN      24
#define FOOTER_TEXT_LEN   48
#define MAX_VISIBLE_CORES 16

// --- Animation timing ---
#define ANIMATION_DURATION_MS 1000UL
#define ANIMATION_FRAME_MS    33UL

// --- Dashboard layout ---
#define DASHBOARD_TITLE_Y     0
#define DASHBOARD_STATUS_Y    0
#define DASHBOARD_STATUS_H    0
#define DASHBOARD_TOP_Y       8
#define DASHBOARD_TOP_W       74
#define DASHBOARD_TOP_H       76
#define DASHBOARD_TOP_GAP     4
#define DASHBOARD_LEGEND_Y    88
#define DASHBOARD_GRID_Y      100
#define DASHBOARD_GRID_GAP    4
#define DASHBOARD_GRID_PAD_X  8
#define DASHBOARD_FOOTER_Y    238
#define DASHBOARD_FOOTER_H    0

const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";

const uint16_t COLOR_BG = 0x0841;
const uint16_t COLOR_PANEL = 0x10A2;
const uint16_t COLOR_PANEL_ALT = 0x18E3;
const uint16_t COLOR_PANEL_STROKE = 0x31A6;
const uint16_t COLOR_MUTED = 0x7BCF;
const uint16_t COLOR_TEXT = TFT_WHITE;
const uint16_t COLOR_TRACK = 0x2124;
const uint16_t COLOR_PERF = 0xFC60;
const uint16_t COLOR_EFF = 0x2E9F;
const uint16_t COLOR_RAM = 0x867F;
const uint16_t COLOR_GPU = 0x55DF;

const float RING_DEG_TO_RAD = 0.01745329251f;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite lineSprite = TFT_eSprite(&tft);
TFT_eSprite gaugeSprite = TFT_eSprite(&tft);
TFT_eSprite coreTileSprite = TFT_eSprite(&tft);
ESP8266WebServer server(80);

String scannedNetworks = "";

int performanceCount = 0;
int efficiencyCount = 0;
int coreLoads[MAX_VISIBLE_CORES];
char updatedAt[UPDATED_AT_LEN] = "";
char memoryText[MEMORY_TEXT_LEN] = "";
char gpuText[GPU_TEXT_LEN] = "";
int memoryPercent = 0;
int gpuPercent = 0;

int displayCoreLoads[MAX_VISIBLE_CORES];
int displayMemoryPercent = 0;
int displayGpuPercent = 0;
int animationStartCoreLoads[MAX_VISIBLE_CORES];
int animationStartMemoryPercent = 0;
int animationStartGpuPercent = 0;

bool hasUploadedData = false;
unsigned long updateCounter = 0;
bool dashboardDirty = false;
bool dashboardCacheValid = false;
bool animationActive = false;
unsigned long animationStartedAt = 0;
unsigned long lastAnimationFrameAt = 0;

bool drawnHasUploadedData = false;
int drawnPerformanceCount = -1;
int drawnEfficiencyCount = -1;
int drawnMemoryPercent = -1;
int drawnGpuPercent = -1;
int drawnCpuAveragePercent = -1;
int drawnCoreLoads[MAX_VISIBLE_CORES];
char drawnUpdatedAt[UPDATED_AT_LEN] = "";
char drawnMemoryText[MEMORY_TEXT_LEN] = "";
char drawnGpuText[GPU_TEXT_LEN] = "";
char drawnFooter[FOOTER_TEXT_LEN] = "";

bool lineSpriteReady = false;
bool gaugeSpriteReady = false;
bool coreTileSpriteReady = false;
int coreTileSpriteWidth = 0;
int coreTileSpriteHeight = 0;

void startServer();
void startAPMode();
void renderDashboard();

struct CoreGridLayout {
  int count;
  int columns;
  int rows;
  int cellWidth;
  int cellHeight;
  int startX;
  int startY;
};

int dashboardTopStartX() {
  return (DISPLAY_SIZE - (DASHBOARD_TOP_W * 3 + DASHBOARD_TOP_GAP * 2)) / 2;
}

int dashboardLegendYForLayout(const CoreGridLayout& layout) {
  int legendY = layout.startY - 12;
  if (legendY < DASHBOARD_LEGEND_Y)
    legendY = DASHBOARD_LEGEND_Y;
  return legendY;
}

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

int roundFloatToInt(float value) {
  if (value >= 0.0f)
    return (int)(value + 0.5f);
  return (int)(value - 0.5f);
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

String compactMetricText(const char* raw) {
  String text = String(raw);
  text.trim();
  text.replace(".0/", "/");
  text.replace(".0 GB", " GB");
  return text;
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

int computeDisplayedCpuAverage() {
  int totalCores = performanceCount + efficiencyCount;
  if (totalCores <= 0)
    return 0;

  long sum = 0;
  for (int i = 0; i < totalCores; i++) {
    sum += displayCoreLoads[i];
  }
  return (int)((sum + totalCores / 2) / totalCores);
}

String buildStateJson() {
  String json = "";
  json.reserve(420);

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
  json += ",\"gpuText\":\"";
  json += jsonEscape(gpuText);
  json += "\",\"gpuPercent\":";
  json += String(gpuPercent);
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
  animationActive = false;
  lastAnimationFrameAt = 0;
  drawnHasUploadedData = false;
  drawnPerformanceCount = -1;
  drawnEfficiencyCount = -1;
  drawnMemoryPercent = -1;
  drawnGpuPercent = -1;
  drawnCpuAveragePercent = -1;
  drawnUpdatedAt[0] = 0;
  drawnMemoryText[0] = 0;
  drawnGpuText[0] = 0;
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

bool ensureGaugeSprite() {
  if (gaugeSpriteReady)
    return true;

  gaugeSprite.setColorDepth(16);
  gaugeSpriteReady = gaugeSprite.createSprite(DASHBOARD_TOP_W, DASHBOARD_TOP_H) != nullptr;
  return gaugeSpriteReady;
}

bool ensureCoreTileSprite(int width, int height) {
  if (coreTileSpriteReady && coreTileSpriteWidth == width && coreTileSpriteHeight == height)
    return true;

  if (coreTileSpriteReady) {
    coreTileSprite.deleteSprite();
    coreTileSpriteReady = false;
    coreTileSpriteWidth = 0;
    coreTileSpriteHeight = 0;
  }

  coreTileSprite.setColorDepth(16);
  coreTileSpriteReady = coreTileSprite.createSprite(width, height) != nullptr;
  if (coreTileSpriteReady) {
    coreTileSpriteWidth = width;
    coreTileSpriteHeight = height;
  }
  return coreTileSpriteReady;
}

template <typename Surface>
void drawTextCenteredOn(Surface& surface, const String& text, int centerX, int y, int size, uint16_t color, uint16_t bg) {
  surface.setTextSize(size);
  surface.setTextColor(color, bg);
  int16_t width = surface.textWidth(text);
  surface.setCursor(centerX - width / 2, y);
  surface.print(text);
}

void displayCentered(const String& text, int y, int size, uint16_t color) {
  drawTextCenteredOn(tft, text, DISPLAY_SIZE / 2, y, size, color, COLOR_BG);
}

float easeAnimationProgress(float progress) {
  if (progress <= 0.0f) return 0.0f;
  if (progress >= 1.0f) return 1.0f;
  return progress * progress * (3.0f - 2.0f * progress);
}

int interpolateAnimatedInt(int startValue, int endValue, float progress) {
  float value = startValue + (endValue - startValue) * progress;
  return roundFloatToInt(value);
}

void syncDisplayedMetricsToTargets() {
  displayMemoryPercent = memoryPercent;
  displayGpuPercent = gpuPercent;

  int totalCores = performanceCount + efficiencyCount;
  for (int i = 0; i < MAX_VISIBLE_CORES; i++) {
    displayCoreLoads[i] = (i < totalCores) ? coreLoads[i] : 0;
  }
}

bool displayedMetricsMatchTargets() {
  if (displayMemoryPercent != memoryPercent || displayGpuPercent != gpuPercent)
    return false;

  int totalCores = performanceCount + efficiencyCount;
  for (int i = 0; i < totalCores; i++) {
    if (displayCoreLoads[i] != coreLoads[i])
      return false;
  }

  return true;
}

void beginMetricsAnimation(unsigned long now, int previousTotalCores) {
  animationStartMemoryPercent = displayMemoryPercent;
  animationStartGpuPercent = displayGpuPercent;

  for (int i = 0; i < MAX_VISIBLE_CORES; i++) {
    animationStartCoreLoads[i] = (i < previousTotalCores) ? displayCoreLoads[i] : 0;
  }

  animationStartedAt = now;
  lastAnimationFrameAt = now;
  animationActive = !displayedMetricsMatchTargets();
  if (!animationActive) {
    syncDisplayedMetricsToTargets();
  }
}

bool updateMetricsAnimation(unsigned long now) {
  if (!animationActive)
    return false;

  unsigned long elapsed = now - animationStartedAt;
  float progress = (elapsed >= ANIMATION_DURATION_MS)
    ? 1.0f
    : (float)elapsed / (float)ANIMATION_DURATION_MS;
  float eased = easeAnimationProgress(progress);
  bool changed = false;

  int nextMemoryPercent = interpolateAnimatedInt(animationStartMemoryPercent, memoryPercent, eased);
  if (nextMemoryPercent != displayMemoryPercent) {
    displayMemoryPercent = nextMemoryPercent;
    changed = true;
  }

  int nextGpuPercent = interpolateAnimatedInt(animationStartGpuPercent, gpuPercent, eased);
  if (nextGpuPercent != displayGpuPercent) {
    displayGpuPercent = nextGpuPercent;
    changed = true;
  }

  int totalCores = performanceCount + efficiencyCount;
  for (int i = 0; i < MAX_VISIBLE_CORES; i++) {
    int nextValue = 0;
    if (i < totalCores) {
      nextValue = interpolateAnimatedInt(animationStartCoreLoads[i], coreLoads[i], eased);
    }
    if (nextValue != displayCoreLoads[i]) {
      displayCoreLoads[i] = nextValue;
      changed = true;
    }
  }

  if (progress >= 1.0f) {
    if (displayMemoryPercent != memoryPercent) {
      displayMemoryPercent = memoryPercent;
      changed = true;
    }

    if (displayGpuPercent != gpuPercent) {
      displayGpuPercent = gpuPercent;
      changed = true;
    }

    for (int i = 0; i < totalCores; i++) {
      if (displayCoreLoads[i] != coreLoads[i]) {
        displayCoreLoads[i] = coreLoads[i];
        changed = true;
      }
    }

    for (int i = totalCores; i < MAX_VISIBLE_CORES; i++) {
      if (displayCoreLoads[i] != 0) {
        displayCoreLoads[i] = 0;
        changed = true;
      }
    }

    animationActive = false;
  }

  return changed;
}

String buildStatusText() {
  tft.setTextSize(1);
  if (!hasUploadedData)
    return String("Send stats to /stats to wake the rings");

  String stamp = updatedAt[0] ? (String("Updated ") + updatedAt) : String("Updated via /stats");
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
  drawnMemoryPercent = displayMemoryPercent;
  drawnGpuPercent = displayGpuPercent;
  drawnCpuAveragePercent = computeDisplayedCpuAverage();

  copyToBuffer(drawnUpdatedAt, sizeof(drawnUpdatedAt), String(updatedAt));
  copyToBuffer(drawnMemoryText, sizeof(drawnMemoryText), String(memoryText));
  copyToBuffer(drawnGpuText, sizeof(drawnGpuText), String(gpuText));
  copyToBuffer(drawnFooter, sizeof(drawnFooter), footer);

  int totalCores = performanceCount + efficiencyCount;
  for (int i = 0; i < MAX_VISIBLE_CORES; i++) {
    drawnCoreLoads[i] = (i < totalCores) ? displayCoreLoads[i] : -1;
  }
}

int chooseCoreColumns(int count) {
  if (count <= 0) return 1;
  if (count <= 4) return count;
  if (count <= 8) return 4;
  if (count <= 10) return 5;
  return 4;
}

CoreGridLayout buildCoreGridLayout() {
  CoreGridLayout layout;
  layout.count = performanceCount + efficiencyCount;
  layout.columns = chooseCoreColumns(layout.count);
  layout.rows = (layout.count <= 0) ? 0 : (layout.count + layout.columns - 1) / layout.columns;
  layout.startX = DASHBOARD_GRID_PAD_X;
  layout.startY = DASHBOARD_GRID_Y;

  if (layout.rows <= 0) {
    layout.cellWidth = 0;
    layout.cellHeight = 0;
    return layout;
  }

  int availableWidth = DISPLAY_SIZE - DASHBOARD_GRID_PAD_X * 2;
  int availableHeight = DASHBOARD_FOOTER_Y - DASHBOARD_GRID_Y;

  layout.cellWidth = (availableWidth - DASHBOARD_GRID_GAP * (layout.columns - 1)) / layout.columns;
  layout.cellHeight = (availableHeight - DASHBOARD_GRID_GAP * (layout.rows - 1)) / layout.rows;
  int maxCellHeight = layout.cellWidth + 6;
  if (layout.cellHeight > maxCellHeight)
    layout.cellHeight = maxCellHeight;
  int totalGridHeight = layout.rows * layout.cellHeight + DASHBOARD_GRID_GAP * (layout.rows - 1);
  int alignedStartY = DASHBOARD_FOOTER_Y - totalGridHeight;
  if (alignedStartY > layout.startY)
    layout.startY = alignedStartY;
  return layout;
}

void coreCellPosition(const CoreGridLayout& layout, int index, int& x, int& y) {
  int col = index % layout.columns;
  int row = index / layout.columns;
  x = layout.startX + col * (layout.cellWidth + DASHBOARD_GRID_GAP);
  y = layout.startY + row * (layout.cellHeight + DASHBOARD_GRID_GAP);
}

template <typename Surface>
void drawRingOn(
  Surface& surface,
  int cx,
  int cy,
  int radius,
  int thickness,
  int percent,
  uint16_t accent,
  uint16_t track
) {
  int clamped = clampPercent(percent);
  bool pixelMode = thickness <= 1;
  int dotRadius = thickness / 2;
  if (!pixelMode && dotRadius < 1)
    dotRadius = 1;

  int pathRadius = pixelMode ? radius : (radius - dotRadius);
  float step = pixelMode
    ? ((radius >= 18) ? 4.0f : 6.0f)
    : ((radius >= 18) ? 4.0f : 7.0f);

  for (float angle = 0.0f; angle < 360.0f; angle += step) {
    float rad = (angle - 90.0f) * RING_DEG_TO_RAD;
    int x = cx + roundFloatToInt(cosf(rad) * pathRadius);
    int y = cy + roundFloatToInt(sinf(rad) * pathRadius);
    if (pixelMode) {
      surface.drawPixel(x, y, track);
    } else {
      surface.fillCircle(x, y, dotRadius, track);
    }
  }

  if (clamped <= 0)
    return;

  float endAngle = ((float)clamped / 100.0f) * 360.0f;
  for (float angle = 0.0f; angle <= endAngle; angle += step) {
    float rad = (angle - 90.0f) * RING_DEG_TO_RAD;
    int x = cx + roundFloatToInt(cosf(rad) * pathRadius);
    int y = cy + roundFloatToInt(sinf(rad) * pathRadius);
    if (pixelMode) {
      surface.drawPixel(x, y, accent);
    } else {
      surface.fillCircle(x, y, dotRadius, accent);
    }
  }

  float capRad = (endAngle - 90.0f) * RING_DEG_TO_RAD;
  int capX = cx + roundFloatToInt(cosf(capRad) * pathRadius);
  int capY = cy + roundFloatToInt(sinf(capRad) * pathRadius);
  if (pixelMode) {
    surface.drawPixel(capX, capY, accent);
  } else {
    surface.fillCircle(capX, capY, dotRadius + 1, accent);
  }
}

template <typename Surface>
void drawGaugeCardOn(
  Surface& surface,
  int x,
  int y,
  int width,
  int height,
  const String& title,
  const String& subtitle,
  int percent,
  uint16_t accent
) {
  surface.fillRoundRect(x, y, width, height, 14, COLOR_PANEL);
  surface.drawRoundRect(x, y, width, height, 14, COLOR_PANEL_STROKE);

  surface.setTextSize(1);
  String titleText = fitTextOn(surface, title.c_str(), width - 16);
  drawTextCenteredOn(surface, titleText, x + width / 2, y + 6, 1, accent, COLOR_PANEL);

  int ringCx = x + width / 2;
  int ringCy = y + 36;
  drawRingOn(surface, ringCx, ringCy, 17, 1, percent, accent, COLOR_TRACK);

  String percentText = String(percent) + "%";
  drawTextCenteredOn(surface, percentText, ringCx, ringCy - 4, 1, COLOR_TEXT, COLOR_PANEL);

  surface.setTextSize(1);
  String subtitleText = fitTextOn(surface, subtitle.c_str(), width - 14);
  drawTextCenteredOn(surface, subtitleText, x + width / 2, y + height - 11, 1, COLOR_MUTED, COLOR_PANEL);
}

template <typename Surface>
void drawCoreTileOn(
  Surface& surface,
  int x,
  int y,
  int width,
  int height,
  const char* prefix,
  int index,
  int load,
  uint16_t accent
) {
  surface.fillRoundRect(x, y, width, height, 8, COLOR_PANEL_ALT);
  surface.drawRoundRect(x, y, width, height, 8, COLOR_PANEL_STROKE);

  int ringRadius = min(width / 2 - 5, height / 2 - 10);
  if (ringRadius < 8)
    ringRadius = 8;
  int ringThickness = 1;
  int ringCx = x + width / 2;
  int ringCy = y + height / 2 + 1;

  drawRingOn(surface, ringCx, ringCy, ringRadius, ringThickness, load, accent, COLOR_TRACK);

  String label = String(prefix) + String(index + 1);
  String value = String(load) + "%";
  surface.setTextSize(1);
  surface.setTextColor(accent, COLOR_PANEL_ALT);
  surface.setCursor(x + 5, y + 5);
  surface.print(label);

  surface.setTextColor(COLOR_TEXT, COLOR_PANEL_ALT);
  drawTextCenteredOn(surface, value, ringCx, ringCy - 4, 1, COLOR_TEXT, COLOR_PANEL_ALT);
}

void drawStatusLine() {
  tft.fillRect(0, DASHBOARD_STATUS_Y, DISPLAY_SIZE, DASHBOARD_STATUS_H, COLOR_BG);
  String text = buildStatusText();
  drawTextCenteredOn(tft, text, DISPLAY_SIZE / 2, DASHBOARD_STATUS_Y, 1, COLOR_MUTED, COLOR_BG);
}

void drawStatusLineSprite() {
  if (!ensureLineSprite()) {
    drawStatusLine();
    return;
  }

  lineSprite.fillSprite(COLOR_BG);
  String text = buildStatusText();
  drawTextCenteredOn(lineSprite, text, DISPLAY_SIZE / 2, 0, 1, COLOR_MUTED, COLOR_BG);
  lineSprite.pushSprite(0, DASHBOARD_STATUS_Y);
}

void drawCoreLegend(const CoreGridLayout& layout) {
  int legendY = dashboardLegendYForLayout(layout);
  int legendHeight = layout.startY - legendY;
  if (legendHeight < 10)
    legendHeight = 10;

  tft.fillRect(0, legendY, DISPLAY_SIZE, legendHeight, COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(10, legendY);
  tft.print("CPU RINGS");

  tft.fillCircle(96, legendY + 4, 3, COLOR_PERF);
  tft.setCursor(103, legendY);
  tft.print("P");

  tft.fillCircle(122, legendY + 4, 3, COLOR_EFF);
  tft.setCursor(129, legendY);
  tft.print("E");

  String summary = String(performanceCount) + "P/" + String(efficiencyCount) + "E";
  int16_t width = tft.textWidth(summary);
  tft.setCursor(DISPLAY_SIZE - 10 - width, legendY);
  tft.print(summary);
}

void drawGaugeCard(int x, int y, const String& title, const String& subtitle, int percent, uint16_t accent) {
  tft.fillRect(x, y, DASHBOARD_TOP_W, DASHBOARD_TOP_H, COLOR_BG);
  drawGaugeCardOn(tft, x, y, DASHBOARD_TOP_W, DASHBOARD_TOP_H, title, subtitle, percent, accent);
}

void drawGaugeCardSprite(int x, int y, const String& title, const String& subtitle, int percent, uint16_t accent) {
  if (!ensureGaugeSprite()) {
    drawGaugeCard(x, y, title, subtitle, percent, accent);
    return;
  }

  gaugeSprite.fillSprite(COLOR_BG);
  drawGaugeCardOn(gaugeSprite, 0, 0, DASHBOARD_TOP_W, DASHBOARD_TOP_H, title, subtitle, percent, accent);
  gaugeSprite.pushSprite(x, y);
}

String buildMemorySubtitle() {
  if (memoryText[0] != 0)
    return compactMetricText(memoryText);
  return String("waiting");
}

String buildCpuSubtitle() {
  int totalCores = performanceCount + efficiencyCount;
  if (totalCores <= 0)
    return String("idle");
  return String(totalCores) + " cores";
}

String buildGpuSubtitle() {
  if (gpuText[0] != 0)
    return String(gpuText);
  if (hasUploadedData)
    return String("n/a");
  return String("auto");
}

void drawTopGauges() {
  int leftX = dashboardTopStartX();
  int midX = leftX + DASHBOARD_TOP_W + DASHBOARD_TOP_GAP;
  int rightX = midX + DASHBOARD_TOP_W + DASHBOARD_TOP_GAP;

  drawGaugeCard(leftX, DASHBOARD_TOP_Y, "RAM", buildMemorySubtitle(), displayMemoryPercent, COLOR_RAM);
  drawGaugeCard(midX, DASHBOARD_TOP_Y, "CPU", buildCpuSubtitle(), computeDisplayedCpuAverage(), COLOR_PERF);
  drawGaugeCard(rightX, DASHBOARD_TOP_Y, "GPU", buildGpuSubtitle(), displayGpuPercent, COLOR_GPU);
}

void drawMemoryGaugeSprite() {
  drawGaugeCardSprite(dashboardTopStartX(), DASHBOARD_TOP_Y, "RAM", buildMemorySubtitle(), displayMemoryPercent, COLOR_RAM);
}

void drawCpuGaugeSprite() {
  drawGaugeCardSprite(
    dashboardTopStartX() + DASHBOARD_TOP_W + DASHBOARD_TOP_GAP,
    DASHBOARD_TOP_Y,
    "CPU",
    buildCpuSubtitle(),
    computeDisplayedCpuAverage(),
    COLOR_PERF
  );
}

void drawGpuGaugeSprite() {
  drawGaugeCardSprite(
    dashboardTopStartX() + (DASHBOARD_TOP_W + DASHBOARD_TOP_GAP) * 2,
    DASHBOARD_TOP_Y,
    "GPU",
    buildGpuSubtitle(),
    displayGpuPercent,
    COLOR_GPU
  );
}

void drawCoreTile(int x, int y, int width, int height, const char* prefix, int index, int load, uint16_t accent) {
  tft.fillRect(x, y, width, height, COLOR_BG);
  drawCoreTileOn(tft, x, y, width, height, prefix, index, load, accent);
}

void drawCoreTileSprite(int x, int y, int width, int height, const char* prefix, int index, int load, uint16_t accent) {
  if (!ensureCoreTileSprite(width, height)) {
    drawCoreTile(x, y, width, height, prefix, index, load, accent);
    return;
  }

  coreTileSprite.fillSprite(COLOR_BG);
  drawCoreTileOn(coreTileSprite, 0, 0, width, height, prefix, index, load, accent);
  coreTileSprite.pushSprite(x, y);
}

void drawCoreGrid(const CoreGridLayout& layout) {
  if (layout.count <= 0)
    return;

  for (int index = 0; index < layout.count; index++) {
    int x = 0;
    int y = 0;
    coreCellPosition(layout, index, x, y);

    bool isPerf = index < performanceCount;
    const char* prefix = isPerf ? "P" : "E";
    int localIndex = isPerf ? index : (index - performanceCount);
    uint16_t accent = isPerf ? COLOR_PERF : COLOR_EFF;

    drawCoreTile(x, y, layout.cellWidth, layout.cellHeight, prefix, localIndex, displayCoreLoads[index], accent);
  }
}

void renderWaitingState() {
  int cardY = DASHBOARD_GRID_Y;
  int cardH = DISPLAY_SIZE - DASHBOARD_GRID_Y - 8;
  tft.fillRoundRect(12, cardY, DISPLAY_SIZE - 24, cardH, 14, COLOR_PANEL);
  tft.drawRoundRect(12, cardY, DISPLAY_SIZE - 24, cardH, 14, COLOR_PANEL_STROKE);
  drawTextCenteredOn(tft, "Waiting for Mac sender", DISPLAY_SIZE / 2, cardY + 18, 1, COLOR_MUTED, COLOR_PANEL);
  drawTextCenteredOn(tft, "POST /stats", DISPLAY_SIZE / 2, cardY + 42, 1, COLOR_TEXT, COLOR_PANEL);
  drawTextCenteredOn(tft, "run local tools/send_mac_stats.py", DISPLAY_SIZE / 2, cardY + 72, 1, COLOR_MUTED, COLOR_PANEL);
}

void renderFooter() {
  String footer = buildFooterText();
  tft.fillRect(0, DASHBOARD_FOOTER_Y, DISPLAY_SIZE, DASHBOARD_FOOTER_H, COLOR_BG);
  drawTextCenteredOn(tft, footer, DISPLAY_SIZE / 2, DASHBOARD_FOOTER_Y, 1, COLOR_MUTED, COLOR_BG);
}

void renderFooterSprite() {
  if (!ensureLineSprite()) {
    renderFooter();
    return;
  }

  String footer = buildFooterText();
  lineSprite.fillSprite(COLOR_BG);
  drawTextCenteredOn(lineSprite, footer, DISPLAY_SIZE / 2, 0, 1, COLOR_MUTED, COLOR_BG);
  lineSprite.pushSprite(0, DASHBOARD_FOOTER_Y);
}

void renderDashboardFull() {
  tft.fillScreen(COLOR_BG);
  drawTopGauges();

  if (!hasUploadedData) {
    renderWaitingState();
    syncDashboardCache("");
    return;
  }

  CoreGridLayout layout = buildCoreGridLayout();
  drawCoreLegend(layout);
  drawCoreGrid(layout);
  syncDashboardCache("");
}

void renderDashboardPartial() {
  int cpuAverage = computeDisplayedCpuAverage();
  if (strcmp(drawnMemoryText, memoryText) != 0 || drawnMemoryPercent != displayMemoryPercent) {
    drawMemoryGaugeSprite();
  }
  if (strcmp(drawnGpuText, gpuText) != 0 || drawnGpuPercent != displayGpuPercent) {
    drawGpuGaugeSprite();
  }
  if (drawnCpuAveragePercent != cpuAverage) {
    drawCpuGaugeSprite();
  }

  CoreGridLayout layout = buildCoreGridLayout();
  for (int index = 0; index < layout.count; index++) {
    if (drawnCoreLoads[index] == displayCoreLoads[index])
      continue;

    int x = 0;
    int y = 0;
    coreCellPosition(layout, index, x, y);

    bool isPerf = index < performanceCount;
    const char* prefix = isPerf ? "P" : "E";
    int localIndex = isPerf ? index : (index - performanceCount);
    uint16_t accent = isPerf ? COLOR_PERF : COLOR_EFF;

    drawCoreTileSprite(x, y, layout.cellWidth, layout.cellHeight, prefix, localIndex, displayCoreLoads[index], accent);
  }
  syncDashboardCache("");
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
    return;
  }

  renderDashboardPartial();
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

// ===================== Web pages =====================

const char STATS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Mac Orbit v2</title>
<style>
body{font-family:sans-serif;background:#0d1118;color:#edf4ff;margin:0;padding:16px}
.panel{max-width:480px;margin:0 auto}
h2{margin:0 0 8px;color:#9cf2ff}
.sub{margin:0 0 16px;color:#a6b9d0;font-size:14px;line-height:1.4}
label{display:block;font-size:13px;color:#bfd0e3;margin:12px 0}
input{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:1px solid #31425a;border-radius:10px;background:#101723;color:#eef6ff}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
button{flex:1;min-width:120px;padding:12px;border:none;border-radius:10px;background:#9cf2ff;color:#06222c;font-weight:700;cursor:pointer}
button.alt{background:#28364b;color:#dce9f8}
#status{margin-top:12px;color:#9ef0a8;min-height:20px}
pre{background:#0b1018;border:1px solid #263345;border-radius:12px;padding:12px;white-space:pre-wrap;word-break:break-word;color:#a7f5ba}
</style>
</head>
<body>
<div class='panel'>
  <h2>Mac Orbit v2</h2>
  <p class='sub'>Circular dashboard test page. CPU loads use CSV in P-then-E order. GPU fields are optional.</p>
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
    <label>GPU text
      <input name='gpuText' placeholder='Apple M2'>
    </label>
    <label>GPU percent
      <input name='gpuPercent' type='number' min='0' max='100' placeholder='34'>
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
  setValue('gpuText', state && state.gpuText ? state.gpuText : '');
  setValue('gpuPercent', state ? state.gpuPercent : '');
  setValue('performanceCount', state ? state.performanceCount : '');
  setValue('efficiencyCount', state ? state.efficiencyCount : '');
  setValue('coreLoads', state && Array.isArray(state.coreLoads) ? state.coreLoads.join(',') : '');
}

function fillDemo(){
  const stamp = new Date().toLocaleTimeString('en-GB', {hour12:false});
  setValue('updatedAt', stamp);
  setValue('memoryText', '12.3/16.0 GB');
  setValue('memoryPercent', 77);
  setValue('gpuText', 'Apple GPU');
  setValue('gpuPercent', 34);
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

  unsigned long now = millis();
  updateMetricsAnimation(now);
  int previousTotalCores = performanceCount + efficiencyCount;
  bool touched = false;
  bool numericValuesChanged = false;

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
    if (parsedMemory != memoryPercent) {
      numericValuesChanged = true;
    }
    memoryPercent = parsedMemory;
    touched = true;
  }

  if (server.hasArg("gpuText")) {
    copyToBuffer(gpuText, sizeof(gpuText), server.arg("gpuText"));
    touched = true;
  }

  if (server.hasArg("gpuPercent")) {
    int parsedGpu = gpuPercent;
    if (!parseIntClamped(server.arg("gpuPercent"), 0, 100, parsedGpu)) {
      server.send(400, "application/json", "{\"error\":\"invalid_gpu_percent\"}");
      return;
    }
    if (parsedGpu != gpuPercent) {
      numericValuesChanged = true;
    }
    gpuPercent = parsedGpu;
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

    if (nextPerformanceCount != performanceCount || nextEfficiencyCount != efficiencyCount) {
      numericValuesChanged = true;
    } else {
      for (int i = 0; i < parsedCount; i++) {
        if (nextCoreLoads[i] != coreLoads[i]) {
          numericValuesChanged = true;
          break;
        }
      }
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

  hasUploadedData =
    (performanceCount + efficiencyCount) > 0 ||
    memoryText[0] != 0 ||
    gpuText[0] != 0 ||
    updatedAt[0] != 0 ||
    memoryPercent > 0 ||
    gpuPercent > 0;

  updateCounter++;
  if (numericValuesChanged) {
    beginMetricsAnimation(now, previousTotalCores);
  }
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
  ensureGaugeSprite();
  syncDisplayedMetricsToTargets();

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
  unsigned long now = millis();
  bool shouldRender = false;

  if (dashboardDirty) {
    shouldRender = true;
    dashboardDirty = false;
  }

  if (animationActive && (now - lastAnimationFrameAt) >= ANIMATION_FRAME_MS) {
    lastAnimationFrameAt = now;
    if (updateMetricsAnimation(now)) {
      shouldRender = true;
    }
  }

  if (shouldRender) {
    renderDashboard();
  }
}
