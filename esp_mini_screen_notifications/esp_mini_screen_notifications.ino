#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef TFT_BL
#define TFT_BL 5
#endif

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON LOW
#endif

// --- Display size ---
#define DISPLAY_SIZE 240

// --- EEPROM layout (compatible with other sketches) ---
#define EEPROM_SIZE 512
#define SSID_ADDR   0
#define PASS_ADDR   32
#define MAX_SSID    32
#define MAX_PASS    64
#define SETTINGS_ADDR          96
#define CLOCK_SETTINGS_ADDR    128
#define WEATHER_SETTINGS_ADDR  160

// --- Notification payload sizing ---
#define APP_TEXT_LEN     24
#define BUNDLE_ID_LEN    64
#define SENDER_TEXT_LEN  32
#define TITLE_TEXT_LEN   64
#define SUBTITLE_TEXT_LEN 64
#define BODY_TEXT_LEN    224
#define UPDATED_AT_LEN   32
#define MAX_WRAP_LINES   8

// --- Dashboard layout ---
#define DASHBOARD_CARD_X       10
#define DASHBOARD_CARD_Y       18
#define DASHBOARD_CARD_W       (DISPLAY_SIZE - 20)
#define DASHBOARD_CARD_H       204
#define DASHBOARD_CARD_SLICE_H 17
#define DASHBOARD_CARD_HIDDEN_X DISPLAY_SIZE

// --- Notification animation ---
#define NOTIFICATION_TOTAL_MS   5000UL
#define NOTIFICATION_ENTER_MS   800UL
#define NOTIFICATION_EXIT_MS    800UL
#define NOTIFICATION_DEFAULT_VISIBLE_MS (NOTIFICATION_TOTAL_MS - NOTIFICATION_ENTER_MS - NOTIFICATION_EXIT_MS)
#define NOTIFICATION_MIN_VISIBLE_MS 1000UL
#define NOTIFICATION_MAX_VISIBLE_MS 30000UL
#define NOTIFICATION_FRAME_MS   33UL

#define NOTIFICATION_PHASE_IDLE     0
#define NOTIFICATION_PHASE_ENTERING 1
#define NOTIFICATION_PHASE_VISIBLE  2
#define NOTIFICATION_PHASE_EXITING  3

// --- Backlight settings ---
#define SETTINGS_MAGIC              0x4E4F5449UL
#define SETTINGS_VERSION            1
#define BRIGHTNESS_MODE_MANUAL      0
#define BRIGHTNESS_MODE_SCHEDULE    1
#define DEFAULT_MANUAL_BRIGHTNESS   72
#define DEFAULT_DAY_BRIGHTNESS      88
#define DEFAULT_NIGHT_BRIGHTNESS    14
#define DEFAULT_DAY_START_HOUR      8
#define DEFAULT_NIGHT_START_HOUR    22
#define BACKLIGHT_REFRESH_MS        15000UL

// --- Clock & weather settings ---
#define CLOCK_SETTINGS_MAGIC        0x434C4B31UL
#define CLOCK_SETTINGS_VERSION      1
#define WEATHER_SETTINGS_MAGIC      0x57544831UL
#define WEATHER_SETTINGS_VERSION    1
#define CLOCK_FORMAT_24H            24
#define CLOCK_FORMAT_12H            12
#define WEATHER_PROVIDER_OPEN_METEO "open-meteo"
#define DEFAULT_WEATHER_REFRESH_MIN 30
#define MIN_WEATHER_REFRESH_MIN     15
#define MAX_WEATHER_REFRESH_MIN     240
#define WEATHER_FETCH_TIMEOUT_MS    12000
#define IDLE_CLOCK_REFRESH_MS       250UL
#define IDLE_INFO_ROTATE_MS         7000UL
#define CLOCK_SMOOTH_MAIN_FONT      "ClockMain48"

#define WEATHER_CITY_LEN            48
#define WEATHER_TOKEN_LEN           48
#define WEATHER_TIME_LABEL_LEN      8
#define WEATHER_STATUS_LEN          64
#define WEATHER_CONDITION_LEN       18
#define FORECAST_DAYS_TO_SHOW       2

#define WEATHER_CONDITION_UNKNOWN   -1000

#define IDLE_CLOCK_PANEL_X          10
#define IDLE_CLOCK_PANEL_Y          10
#define IDLE_CLOCK_PANEL_W          220
#define IDLE_CLOCK_PANEL_H          84
#define IDLE_WEATHER_PANEL_X        10
#define IDLE_WEATHER_PANEL_Y        102
#define IDLE_WEATHER_PANEL_W        220
#define IDLE_WEATHER_PANEL_H        128
#define IDLE_TOP_REGION_H           144
#define IDLE_BOTTOM_REGION_Y        IDLE_TOP_REGION_H
#define IDLE_BOTTOM_REGION_H        (DISPLAY_SIZE - IDLE_TOP_REGION_H)

const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";

const uint16_t COLOR_BG = 0x0841;
const uint16_t COLOR_PANEL = 0x10A2;
const uint16_t COLOR_PANEL_ALT = 0x18E3;
const uint16_t COLOR_BORDER = 0x31A6;
const uint16_t COLOR_MUTED = 0x7BCF;
const uint16_t COLOR_TEXT = TFT_WHITE;
const uint16_t COLOR_DEFAULT = 0x55DF;
const uint16_t COLOR_TELEGRAM = 0x55DF;
const uint16_t COLOR_MAIL = 0xFD20;
const uint16_t COLOR_MESSAGES = 0x4FE0;
const uint16_t COLOR_DISCORD = 0x7B5D;
const uint16_t COLOR_GITHUB = 0xC618;
const uint16_t COLOR_IDLE_BG = TFT_BLACK;
const uint16_t COLOR_IDLE_CITY = 0xBFF0;
const uint16_t COLOR_IDLE_HOURS = TFT_WHITE;//0xFEC0;
const uint16_t COLOR_IDLE_MINUTES = TFT_WHITE;//0xFA28;
const uint16_t COLOR_IDLE_SECONDS = TFT_WHITE;//0x2D9F;
const uint16_t COLOR_IDLE_TEMP = 0xFD20;
const uint16_t COLOR_IDLE_HUMIDITY = 0x55DF;
const uint16_t COLOR_IDLE_PILL_BG = 0xE79B;
const uint16_t COLOR_IDLE_PILL_TEXT = 0x1082;
const uint16_t COLOR_IDLE_CHIP = 0x18E3;
const uint16_t COLOR_IDLE_BAR_BG = 0x2145;
const uint16_t COLOR_IDLE_DIVIDER = 0x31A6;

struct NotificationState {
  char app[APP_TEXT_LEN];
  char bundleId[BUNDLE_ID_LEN];
  char sender[SENDER_TEXT_LEN];
  char title[TITLE_TEXT_LEN];
  char subtitle[SUBTITLE_TEXT_LEN];
  char body[BODY_TEXT_LEN];
  uint16_t accentColor;
  uint16_t foregroundColor;
};

struct BacklightSettings {
  uint32_t magic;
  uint8_t version;
  uint8_t mode;
  uint8_t manualBrightness;
  uint8_t dayBrightness;
  uint8_t nightBrightness;
  uint8_t dayStartHour;
  uint8_t dayStartMinute;
  uint8_t nightStartHour;
  uint8_t nightStartMinute;
  int16_t timezoneOffsetMinutes;
} __attribute__((packed));

struct ClockSettings {
  uint32_t magic;
  uint8_t version;
  uint8_t formatHours;
} __attribute__((packed));

struct WeatherSettings {
  uint32_t magic;
  uint8_t version;
  uint8_t enabled;
  uint16_t refreshMinutes;
  int32_t latitudeE4;
  int32_t longitudeE4;
  char city[WEATHER_CITY_LEN];
  char apiToken[WEATHER_TOKEN_LEN];
} __attribute__((packed));

struct ForecastDay {
  bool valid;
  char label[WEATHER_TIME_LABEL_LEN];
  int minTempC;
  int maxTempC;
  int weatherCode;
};

struct WeatherRuntimeState {
  bool valid;
  bool fetching;
  int temperatureC;
  int humidity;
  int windSpeedKmh;
  int weatherCode;
  int utcOffsetMinutes;
  char updatedAt[WEATHER_TIME_LABEL_LEN];
  char condition[WEATHER_CONDITION_LEN];
  char status[WEATHER_STATUS_LEN];
  ForecastDay forecast[FORECAST_DAYS_TO_SHOW];
} __attribute__((packed));

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite cardSliceSprite = TFT_eSprite(&tft);
ESP8266WebServer server(80);

String scannedNetworks = "";
bool smoothFontFsReady = false;
bool clockSmoothFontReady = false;

NotificationState currentNotification = {"", "", "", "", "", "", COLOR_DEFAULT, COLOR_TEXT};
char updatedAt[UPDATED_AT_LEN] = "";
bool hasNotification = false;
unsigned long updateCounter = 0;
bool dashboardCacheValid = false;
bool drawnHasNotification = false;
NotificationState drawnNotification = {"", "", "", "", "", "", COLOR_DEFAULT, COLOR_TEXT};
char drawnUpdatedAt[UPDATED_AT_LEN] = "";
bool cardSliceSpriteReady = false;
int cardSliceSpriteHeight = 0;
int currentCardX = DASHBOARD_CARD_X;
int drawnCardX = DASHBOARD_CARD_X;
int animationStartCardX = DASHBOARD_CARD_X;
int animationTargetCardX = DASHBOARD_CARD_X;
uint8_t notificationPhase = NOTIFICATION_PHASE_IDLE;
unsigned long notificationPhaseStartedAt = 0;
unsigned long notificationVisibleUntilMs = 0;
unsigned long currentNotificationVisibleMs = NOTIFICATION_DEFAULT_VISIBLE_MS;
unsigned long lastNotificationFrameMs = 0;
ClockSettings clockSettings = {
  CLOCK_SETTINGS_MAGIC,
  CLOCK_SETTINGS_VERSION,
  CLOCK_FORMAT_24H
};
WeatherSettings weatherSettings = {
  WEATHER_SETTINGS_MAGIC,
  WEATHER_SETTINGS_VERSION,
  0,
  DEFAULT_WEATHER_REFRESH_MIN,
  0,
  0,
  "",
  ""
};
WeatherRuntimeState weatherState = {};
BacklightSettings backlightSettings = {
  SETTINGS_MAGIC,
  SETTINGS_VERSION,
  BRIGHTNESS_MODE_MANUAL,
  DEFAULT_MANUAL_BRIGHTNESS,
  DEFAULT_DAY_BRIGHTNESS,
  DEFAULT_NIGHT_BRIGHTNESS,
  DEFAULT_DAY_START_HOUR,
  0,
  DEFAULT_NIGHT_START_HOUR,
  0,
  0
};
int appliedBrightness = -1;
unsigned long lastBacklightRefreshMs = 0;
unsigned long lastIdleClockRefreshMs = 0;
unsigned long lastWeatherAttemptMs = 0;
unsigned long nextWeatherRefreshMs = 0;
bool weatherRefreshRequested = false;
bool idleScreenDrawn = false;
char drawnIdleClockMain[8] = "";
char drawnIdleClockSeconds[4] = "";
char drawnIdleClockSuffix[4] = "";
char drawnIdleDateLine[20] = "";
char drawnIdleInfoLine[48] = "";
char drawnIdleWeatherKey[192] = "";

void startServer();
void startAPMode();
void renderDashboard();
void invalidateDashboardCache();
void clearNotification();
void refreshWeather(bool force = false);
void drawIdleBackgroundRegion(int startY, int height);
bool idleWeatherIsReady();
void initSmoothFonts();

template <typename Surface>
void drawIdleClockPanelOn(Surface& surface, int offsetY);

template <typename Surface>
void drawIdleWeatherPanelOn(Surface& surface, int offsetY);

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

int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue)
    return minValue;
  if (value > maxValue)
    return maxValue;
  return value;
}

String twoDigits(int value) {
  if (value < 10)
    return "0" + String(value);
  return String(value);
}

String formatClockTime(int hour, int minute) {
  return twoDigits(hour) + ":" + twoDigits(minute);
}

bool parseIntClamped(const String& raw, int minValue, int maxValue, int& result) {
  char* endPtr = nullptr;
  long parsed = strtol(raw.c_str(), &endPtr, 10);
  if (endPtr == raw.c_str() || *endPtr != 0)
    return false;
  if (parsed < minValue || parsed > maxValue)
    return false;
  result = (int)parsed;
  return true;
}

bool parseClockTime(const String& raw, int& hour, int& minute) {
  if (raw.length() != 5 || raw.charAt(2) != ':')
    return false;

  int parsedHour = 0;
  int parsedMinute = 0;
  if (!parseIntClamped(raw.substring(0, 2), 0, 23, parsedHour))
    return false;
  if (!parseIntClamped(raw.substring(3, 5), 0, 59, parsedMinute))
    return false;

  hour = parsedHour;
  minute = parsedMinute;
  return true;
}

int clockToMinutes(int hour, int minute) {
  return hour * 60 + minute;
}

void normalizeBacklightSettingsValue(BacklightSettings& settings) {
  settings.magic = SETTINGS_MAGIC;
  settings.version = SETTINGS_VERSION;
  settings.mode = settings.mode == BRIGHTNESS_MODE_SCHEDULE ? BRIGHTNESS_MODE_SCHEDULE : BRIGHTNESS_MODE_MANUAL;
  settings.manualBrightness = clampInt(settings.manualBrightness, 0, 100);
  settings.dayBrightness = clampInt(settings.dayBrightness, 0, 100);
  settings.nightBrightness = clampInt(settings.nightBrightness, 0, 100);
  settings.dayStartHour = clampInt(settings.dayStartHour, 0, 23);
  settings.dayStartMinute = clampInt(settings.dayStartMinute, 0, 59);
  settings.nightStartHour = clampInt(settings.nightStartHour, 0, 23);
  settings.nightStartMinute = clampInt(settings.nightStartMinute, 0, 59);
  settings.timezoneOffsetMinutes = clampInt(settings.timezoneOffsetMinutes, -720, 840);
}

void normalizeBacklightSettings() {
  normalizeBacklightSettingsValue(backlightSettings);
}

void setBacklightDefaults() {
  backlightSettings.magic = SETTINGS_MAGIC;
  backlightSettings.version = SETTINGS_VERSION;
  backlightSettings.mode = BRIGHTNESS_MODE_MANUAL;
  backlightSettings.manualBrightness = DEFAULT_MANUAL_BRIGHTNESS;
  backlightSettings.dayBrightness = DEFAULT_DAY_BRIGHTNESS;
  backlightSettings.nightBrightness = DEFAULT_NIGHT_BRIGHTNESS;
  backlightSettings.dayStartHour = DEFAULT_DAY_START_HOUR;
  backlightSettings.dayStartMinute = 0;
  backlightSettings.nightStartHour = DEFAULT_NIGHT_START_HOUR;
  backlightSettings.nightStartMinute = 0;
  backlightSettings.timezoneOffsetMinutes = 0;
}

bool isStoredSettingsValid(const BacklightSettings& settings) {
  if (settings.magic != SETTINGS_MAGIC || settings.version != SETTINGS_VERSION)
    return false;
  if (settings.mode != BRIGHTNESS_MODE_MANUAL && settings.mode != BRIGHTNESS_MODE_SCHEDULE)
    return false;
  if (settings.manualBrightness > 100 || settings.dayBrightness > 100 || settings.nightBrightness > 100)
    return false;
  if (settings.dayStartHour > 23 || settings.dayStartMinute > 59)
    return false;
  if (settings.nightStartHour > 23 || settings.nightStartMinute > 59)
    return false;
  if (settings.timezoneOffsetMinutes < -720 || settings.timezoneOffsetMinutes > 840)
    return false;
  return true;
}

void loadBacklightSettings() {
  EEPROM.begin(EEPROM_SIZE);
  BacklightSettings stored;
  EEPROM.get(SETTINGS_ADDR, stored);
  EEPROM.end();

  if (!isStoredSettingsValid(stored)) {
    setBacklightDefaults();
    return;
  }

  backlightSettings = stored;
  normalizeBacklightSettings();
}

void saveBacklightSettings() {
  normalizeBacklightSettings();
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(SETTINGS_ADDR, backlightSettings);
  EEPROM.commit();
  EEPROM.end();
}

void setClockDefaults() {
  clockSettings.magic = CLOCK_SETTINGS_MAGIC;
  clockSettings.version = CLOCK_SETTINGS_VERSION;
  clockSettings.formatHours = CLOCK_FORMAT_24H;
}

bool isStoredClockSettingsValid(const ClockSettings& settings) {
  if (settings.magic != CLOCK_SETTINGS_MAGIC || settings.version != CLOCK_SETTINGS_VERSION)
    return false;
  return settings.formatHours == CLOCK_FORMAT_24H || settings.formatHours == CLOCK_FORMAT_12H;
}

void loadClockSettings() {
  EEPROM.begin(EEPROM_SIZE);
  ClockSettings stored;
  EEPROM.get(CLOCK_SETTINGS_ADDR, stored);
  EEPROM.end();

  if (!isStoredClockSettingsValid(stored)) {
    setClockDefaults();
    return;
  }

  clockSettings = stored;
}

void saveClockSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(CLOCK_SETTINGS_ADDR, clockSettings);
  EEPROM.commit();
  EEPROM.end();
}

void setWeatherDefaults() {
  weatherSettings.magic = WEATHER_SETTINGS_MAGIC;
  weatherSettings.version = WEATHER_SETTINGS_VERSION;
  weatherSettings.enabled = 0;
  weatherSettings.refreshMinutes = DEFAULT_WEATHER_REFRESH_MIN;
  weatherSettings.latitudeE4 = 0;
  weatherSettings.longitudeE4 = 0;
  weatherSettings.city[0] = 0;
  weatherSettings.apiToken[0] = 0;
}

bool isStoredWeatherSettingsValid(const WeatherSettings& settings) {
  if (settings.magic != WEATHER_SETTINGS_MAGIC || settings.version != WEATHER_SETTINGS_VERSION)
    return false;
  if (settings.enabled > 1)
    return false;
  if (settings.refreshMinutes < MIN_WEATHER_REFRESH_MIN || settings.refreshMinutes > MAX_WEATHER_REFRESH_MIN)
    return false;
  return true;
}

void normalizeWeatherSettingsValue(WeatherSettings& settings) {
  settings.magic = WEATHER_SETTINGS_MAGIC;
  settings.version = WEATHER_SETTINGS_VERSION;
  settings.enabled = settings.enabled ? 1 : 0;
  settings.refreshMinutes = clampInt(settings.refreshMinutes, MIN_WEATHER_REFRESH_MIN, MAX_WEATHER_REFRESH_MIN);
  if (settings.latitudeE4 < -900000)
    settings.latitudeE4 = -900000;
  if (settings.latitudeE4 > 900000)
    settings.latitudeE4 = 900000;
  if (settings.longitudeE4 < -1800000)
    settings.longitudeE4 = -1800000;
  if (settings.longitudeE4 > 1800000)
    settings.longitudeE4 = 1800000;
}

void normalizeWeatherSettings() {
  normalizeWeatherSettingsValue(weatherSettings);
}

void loadWeatherSettings() {
  EEPROM.begin(EEPROM_SIZE);
  WeatherSettings stored;
  EEPROM.get(WEATHER_SETTINGS_ADDR, stored);
  EEPROM.end();

  if (!isStoredWeatherSettingsValid(stored)) {
    setWeatherDefaults();
    return;
  }

  weatherSettings = stored;
  normalizeWeatherSettings();
}

void saveWeatherSettings() {
  normalizeWeatherSettings();
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(WEATHER_SETTINGS_ADDR, weatherSettings);
  EEPROM.commit();
  EEPROM.end();
}

void resetWeatherRuntime(const char* statusText) {
  weatherState.valid = false;
  weatherState.fetching = false;
  weatherState.temperatureC = 0;
  weatherState.humidity = 0;
  weatherState.windSpeedKmh = 0;
  weatherState.weatherCode = WEATHER_CONDITION_UNKNOWN;
  weatherState.utcOffsetMinutes = 0;
  weatherState.updatedAt[0] = 0;
  copyToBuffer(weatherState.condition, sizeof(weatherState.condition), "");
  copyToBuffer(weatherState.status, sizeof(weatherState.status), statusText ? String(statusText) : String(""));
  for (int i = 0; i < FORECAST_DAYS_TO_SHOW; i++) {
    weatherState.forecast[i].valid = false;
    weatherState.forecast[i].label[0] = 0;
    weatherState.forecast[i].minTempC = 0;
    weatherState.forecast[i].maxTempC = 0;
    weatherState.forecast[i].weatherCode = WEATHER_CONDITION_UNKNOWN;
  }
}

void configureClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
}

bool getLocalDateTime(struct tm& localTm) {
  time_t now = time(nullptr);
  if (now < 100000)
    return false;

  long localNow = (long)now + (long)backlightSettings.timezoneOffsetMinutes * 60L;
  time_t localEpoch = (time_t)localNow;
  gmtime_r(&localEpoch, &localTm);
  return true;
}

bool getLocalClockMinutes(int& totalMinutes) {
  struct tm localTm;
  if (!getLocalDateTime(localTm))
    return false;
  totalMinutes = localTm.tm_hour * 60 + localTm.tm_min;
  return true;
}

String currentLocalTimeText() {
  struct tm localTm;
  if (!getLocalDateTime(localTm))
    return String("");
  return formatClockTime(localTm.tm_hour, localTm.tm_min);
}

String currentDeviceIpText() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String currentLocalDateText() {
  static const char* DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  struct tm localTm;
  if (!getLocalDateTime(localTm))
    return String("Syncing time");

  String text = String(DAY_NAMES[localTm.tm_wday]);
  text += " ";
  text += String(localTm.tm_mday);
  text += " ";
  text += String(MONTH_NAMES[localTm.tm_mon]);
  return text;
}

String currentIdleInfoText() {
  String items[3];
  int count = 0;

  String ip = currentDeviceIpText();
  if (ip.length() > 0)
    items[count++] = "IP " + ip;

  if (idleWeatherIsReady()) {
    items[count++] = "Wind " + String(weatherState.windSpeedKmh) + " km/h";
    if (weatherState.updatedAt[0])
      items[count++] = "Updated " + String(weatherState.updatedAt);
  } else if (weatherSettings.enabled) {
    items[count++] = weatherState.status[0] ? String(weatherState.status) : String("Syncing weather");
  } else {
    items[count++] = "Weather off";
  }

  if (count <= 0)
    return String("");
  return items[(millis() / IDLE_INFO_ROTATE_MS) % count];
}

void buildClockDisplay(char* mainTime, size_t mainSize, char* secondsText, size_t secondsSize, char* suffixText, size_t suffixSize) {
  struct tm localTm;
  if (!getLocalDateTime(localTm)) {
    copyToBuffer(mainTime, mainSize, "--:--");
    copyToBuffer(secondsText, secondsSize, "--");
    copyToBuffer(suffixText, suffixSize, "");
    return;
  }

  int displayHour = localTm.tm_hour;
  const char* suffix = "";
  if (clockSettings.formatHours == CLOCK_FORMAT_12H) {
    suffix = displayHour >= 12 ? "PM" : "AM";
    displayHour %= 12;
    if (displayHour == 0)
      displayHour = 12;
  }

  snprintf(mainTime, mainSize, "%02d:%02d", displayHour, localTm.tm_min);
  snprintf(secondsText, secondsSize, "%02d", localTm.tm_sec);
  copyToBuffer(suffixText, suffixSize, suffix);
}

bool isDayScheduleActive(int totalMinutes) {
  int dayStart = clockToMinutes(backlightSettings.dayStartHour, backlightSettings.dayStartMinute);
  int nightStart = clockToMinutes(backlightSettings.nightStartHour, backlightSettings.nightStartMinute);

  if (dayStart == nightStart)
    return true;
  if (dayStart < nightStart)
    return totalMinutes >= dayStart && totalMinutes < nightStart;
  return totalMinutes >= dayStart || totalMinutes < nightStart;
}

int resolveBacklightBrightness(bool* scheduleTimeReady = nullptr) {
  bool ready = false;
  int brightness = backlightSettings.manualBrightness;

  if (backlightSettings.mode == BRIGHTNESS_MODE_SCHEDULE) {
    int totalMinutes = 0;
    ready = getLocalClockMinutes(totalMinutes);
    if (ready) {
      brightness = isDayScheduleActive(totalMinutes) ? backlightSettings.dayBrightness : backlightSettings.nightBrightness;
    } else {
      brightness = backlightSettings.dayBrightness;
    }
  }

  if (scheduleTimeReady)
    *scheduleTimeReady = ready;
  return clampInt(brightness, 0, 100);
}

void writeBacklightBrightness(int brightness) {
  brightness = clampInt(brightness, 0, 100);

  long pwmDuty = map(brightness, 0, 100, 0, 255);
  if (TFT_BACKLIGHT_ON == LOW)
    pwmDuty = 255 - pwmDuty;

  analogWrite(TFT_BL, (int)pwmDuty);
  appliedBrightness = brightness;
}

void refreshBacklight(bool force = false) {
  int targetBrightness = resolveBacklightBrightness();
  if (!force && targetBrightness == appliedBrightness)
    return;
  writeBacklightBrightness(targetBrightness);
}

float easeInOut(float progress) {
  if (progress <= 0.0f)
    return 0.0f;
  if (progress >= 1.0f)
    return 1.0f;
  return progress * progress * (3.0f - 2.0f * progress);
}

int interpolateInt(int fromValue, int toValue, float progress) {
  float eased = easeInOut(progress);
  return fromValue + (int)((toValue - fromValue) * eased + (toValue >= fromValue ? 0.5f : -0.5f));
}

void resetNotificationAnimation() {
  currentCardX = DASHBOARD_CARD_X;
  animationStartCardX = DASHBOARD_CARD_X;
  animationTargetCardX = DASHBOARD_CARD_X;
  notificationPhase = NOTIFICATION_PHASE_IDLE;
  notificationPhaseStartedAt = 0;
  notificationVisibleUntilMs = 0;
  currentNotificationVisibleMs = NOTIFICATION_DEFAULT_VISIBLE_MS;
  lastNotificationFrameMs = 0;
}

void startNotificationEnterAnimation() {
  currentCardX = DASHBOARD_CARD_HIDDEN_X;
  animationStartCardX = DASHBOARD_CARD_HIDDEN_X;
  animationTargetCardX = DASHBOARD_CARD_X;
  notificationPhase = NOTIFICATION_PHASE_ENTERING;
  notificationPhaseStartedAt = millis();
  notificationVisibleUntilMs = 0;
  lastNotificationFrameMs = 0;
  invalidateDashboardCache();
}

void startNotificationExitAnimation() {
  if (!hasNotification)
    return;

  animationStartCardX = currentCardX;
  animationTargetCardX = DASHBOARD_CARD_HIDDEN_X;
  notificationPhase = NOTIFICATION_PHASE_EXITING;
  notificationPhaseStartedAt = millis();
  lastNotificationFrameMs = 0;
}

void updateNotificationAnimation(bool force = false) {
  if (notificationPhase == NOTIFICATION_PHASE_IDLE)
    return;

  unsigned long now = millis();
  if (!force && lastNotificationFrameMs != 0 && now - lastNotificationFrameMs < NOTIFICATION_FRAME_MS) {
    if (notificationPhase != NOTIFICATION_PHASE_VISIBLE || now < notificationVisibleUntilMs)
      return;
  }

  lastNotificationFrameMs = now;

  if (notificationPhase == NOTIFICATION_PHASE_ENTERING) {
    unsigned long elapsed = now - notificationPhaseStartedAt;
    if (elapsed >= NOTIFICATION_ENTER_MS) {
      currentCardX = DASHBOARD_CARD_X;
      notificationPhase = NOTIFICATION_PHASE_VISIBLE;
      notificationVisibleUntilMs = now + currentNotificationVisibleMs;
    } else {
      currentCardX = interpolateInt(animationStartCardX, animationTargetCardX, (float)elapsed / (float)NOTIFICATION_ENTER_MS);
    }
    renderDashboard();
    return;
  }

  if (notificationPhase == NOTIFICATION_PHASE_VISIBLE) {
    currentCardX = DASHBOARD_CARD_X;
    if (notificationVisibleUntilMs != 0 && now >= notificationVisibleUntilMs) {
      startNotificationExitAnimation();
    }
    return;
  }

  if (notificationPhase == NOTIFICATION_PHASE_EXITING) {
    unsigned long elapsed = now - notificationPhaseStartedAt;
    if (elapsed >= NOTIFICATION_EXIT_MS) {
      clearNotification();
      resetNotificationAnimation();
      invalidateDashboardCache();
      renderDashboard();
      return;
    }

    currentCardX = interpolateInt(animationStartCardX, animationTargetCardX, (float)elapsed / (float)NOTIFICATION_EXIT_MS);
    renderDashboard();
  }
}

String urlEncode(const String& input) {
  String encoded = "";
  encoded.reserve(input.length() * 3);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    bool isSafe = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '.' || c == '~';
    if (isSafe) {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      encoded += hex;
    }
  }
  return encoded;
}

bool httpGetString(const String& url, String& payload, String& error) {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(WEATHER_FETCH_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    error = "http_begin_failed";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = "http_" + String(code);
    http.end();
    return false;
  }

  payload = http.getString();
  http.end();
  return payload.length() > 0;
}

int findJsonValueStart(const String& json, const char* key, int fromPos = 0) {
  String pattern = "\"";
  pattern += key;
  pattern += "\":";
  int keyPos = json.indexOf(pattern, fromPos);
  if (keyPos < 0)
    return -1;

  int valuePos = keyPos + pattern.length();
  while (valuePos < json.length() && (json.charAt(valuePos) == ' ' || json.charAt(valuePos) == '\n' || json.charAt(valuePos) == '\r' || json.charAt(valuePos) == '\t'))
    valuePos++;
  return valuePos;
}

bool parseJsonFloatAt(const String& json, int valuePos, float& value) {
  if (valuePos < 0 || valuePos >= json.length())
    return false;

  int endPos = valuePos;
  while (endPos < json.length()) {
    char c = json.charAt(endPos);
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
      endPos++;
      continue;
    }
    break;
  }

  if (endPos == valuePos)
    return false;

  String token = json.substring(valuePos, endPos);
  value = token.toFloat();
  return true;
}

bool parseJsonIntAt(const String& json, int valuePos, int& value) {
  float parsedFloat = 0.0f;
  if (!parseJsonFloatAt(json, valuePos, parsedFloat))
    return false;
  value = (int)(parsedFloat >= 0.0f ? parsedFloat + 0.5f : parsedFloat - 0.5f);
  return true;
}

bool extractJsonFloatKey(const String& json, const char* key, float& value, int fromPos = 0) {
  int valuePos = findJsonValueStart(json, key, fromPos);
  return parseJsonFloatAt(json, valuePos, value);
}

bool extractJsonIntKey(const String& json, const char* key, int& value, int fromPos = 0) {
  int valuePos = findJsonValueStart(json, key, fromPos);
  return parseJsonIntAt(json, valuePos, value);
}

bool extractJsonStringKey(const String& json, const char* key, String& value, int fromPos = 0) {
  int valuePos = findJsonValueStart(json, key, fromPos);
  if (valuePos < 0 || valuePos >= json.length() || json.charAt(valuePos) != '"')
    return false;

  value = "";
  for (int i = valuePos + 1; i < json.length(); i++) {
    char c = json.charAt(i);
    if (c == '"' && json.charAt(i - 1) != '\\')
      return true;
    if (c == '\\' && i + 1 < json.length()) {
      char next = json.charAt(i + 1);
      if (next == 'n') {
        value += '\n';
        i++;
        continue;
      }
      if (next == 'r') {
        i++;
        continue;
      }
      value += next;
      i++;
      continue;
    }
    value += c;
  }

  return false;
}

bool extractJsonFloatArrayKey(const String& json, const char* key, float* values, int count, int fromPos = 0) {
  int valuePos = findJsonValueStart(json, key, fromPos);
  if (valuePos < 0 || valuePos >= json.length() || json.charAt(valuePos) != '[')
    return false;

  int index = 0;
  int pos = valuePos + 1;
  while (pos < json.length() && index < count) {
    while (pos < json.length() && (json.charAt(pos) == ' ' || json.charAt(pos) == ',' || json.charAt(pos) == '\n' || json.charAt(pos) == '\r'))
      pos++;
    if (pos >= json.length() || json.charAt(pos) == ']')
      break;
    if (!parseJsonFloatAt(json, pos, values[index]))
      return false;
    index++;
    while (pos < json.length() && json.charAt(pos) != ',' && json.charAt(pos) != ']')
      pos++;
    if (pos < json.length() && json.charAt(pos) == ',')
      pos++;
  }

  return index == count;
}

bool extractJsonIntArrayKey(const String& json, const char* key, int* values, int count, int fromPos = 0) {
  float parsed[8];
  if (count > 8)
    return false;
  if (!extractJsonFloatArrayKey(json, key, parsed, count, fromPos))
    return false;
  for (int i = 0; i < count; i++)
    values[i] = (int)(parsed[i] >= 0.0f ? parsed[i] + 0.5f : parsed[i] - 0.5f);
  return true;
}

bool extractJsonStringArrayKey(const String& json, const char* key, String* values, int count, int fromPos = 0) {
  int valuePos = findJsonValueStart(json, key, fromPos);
  if (valuePos < 0 || valuePos >= json.length() || json.charAt(valuePos) != '[')
    return false;

  int index = 0;
  int pos = valuePos + 1;
  while (pos < json.length() && index < count) {
    while (pos < json.length() && (json.charAt(pos) == ' ' || json.charAt(pos) == ',' || json.charAt(pos) == '\n' || json.charAt(pos) == '\r'))
      pos++;
    if (pos >= json.length() || json.charAt(pos) == ']')
      break;
    if (json.charAt(pos) != '"')
      return false;

    String item = "";
    for (int i = pos + 1; i < json.length(); i++) {
      char c = json.charAt(i);
      if (c == '"' && json.charAt(i - 1) != '\\') {
        values[index++] = item;
        pos = i + 1;
        break;
      }
      if (c == '\\' && i + 1 < json.length()) {
        item += json.charAt(i + 1);
        i++;
        continue;
      }
      item += c;
    }
  }

  return index == count;
}

int scaledCoordToRoundedInt(int32_t valueE4) {
  return valueE4 >= 0 ? (valueE4 + 50) / 100 : (valueE4 - 50) / 100;
}

String formatScaledCoord(int32_t valueE4) {
  int absoluteValue = valueE4 >= 0 ? valueE4 : -valueE4;
  String text = valueE4 < 0 ? "-" : "";
  text += String(absoluteValue / 10000);
  text += ".";
  int fraction = absoluteValue % 10000;
  if (fraction < 1000) text += "0";
  if (fraction < 100) text += "0";
  if (fraction < 10) text += "0";
  text += String(fraction);
  return text;
}

bool weatherLocationConfigured() {
  return weatherSettings.city[0] != 0 && !(weatherSettings.latitudeE4 == 0 && weatherSettings.longitudeE4 == 0);
}

const char* weatherCodeLabel(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Freezing rain";
    case 71:
    case 73:
    case 75:
    case 77: return "Snow";
    case 80:
    case 81:
    case 82: return "Showers";
    case 85:
    case 86: return "Snow showers";
    case 95: return "Thunder";
    case 96:
    case 99: return "Storm";
    default: return "Weather";
  }
}

String forecastLabelFromIsoDate(const String& isoDate) {
  if (isoDate.length() >= 10)
    return isoDate.substring(5, 7) + "/" + isoDate.substring(8, 10);
  return isoDate;
}

void setWeatherStatus(const String& text) {
  copyToBuffer(weatherState.status, sizeof(weatherState.status), text);
}

void markWeatherNeedsRefresh() {
  weatherRefreshRequested = true;
  nextWeatherRefreshMs = 0;
}

bool geocodeWeatherCity(const String& city, WeatherSettings& resolvedSettings, String& error) {
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(city) + "&count=1&language=en&format=json";
  String payload = "";
  if (!httpGetString(url, payload, error)) {
    return false;
  }

  int resultsPos = payload.indexOf("\"results\":[");
  if (resultsPos < 0) {
    error = "city_not_found";
    return false;
  }

  int firstResultPos = payload.indexOf('{', resultsPos);
  if (firstResultPos < 0) {
    error = "city_not_found";
    return false;
  }

  float latitude = 0.0f;
  float longitude = 0.0f;
  String name = city;

  if (!extractJsonFloatKey(payload, "latitude", latitude, firstResultPos) ||
      !extractJsonFloatKey(payload, "longitude", longitude, firstResultPos)) {
    error = "geocode_parse_failed";
    return false;
  }
  extractJsonStringKey(payload, "name", name, firstResultPos);

  copyToBuffer(resolvedSettings.city, sizeof(resolvedSettings.city), name);
  resolvedSettings.latitudeE4 = (int32_t)(latitude >= 0.0f ? latitude * 10000.0f + 0.5f : latitude * 10000.0f - 0.5f);
  resolvedSettings.longitudeE4 = (int32_t)(longitude >= 0.0f ? longitude * 10000.0f + 0.5f : longitude * 10000.0f - 0.5f);
  return true;
}

bool fetchWeatherNow(String& error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "wifi_not_connected";
    return false;
  }

  if (!weatherSettings.enabled) {
    resetWeatherRuntime("Weather disabled");
    return false;
  }

  if (!weatherLocationConfigured()) {
    resetWeatherRuntime("Set a city in the web panel");
    error = "weather_city_missing";
    return false;
  }

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + formatScaledCoord(weatherSettings.latitudeE4);
  url += "&longitude=" + formatScaledCoord(weatherSettings.longitudeE4);
  url += "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min";
  url += "&forecast_days=3&timezone=auto";

  weatherState.fetching = true;
  setWeatherStatus("Fetching weather...");

  String payload = "";
  if (!httpGetString(url, payload, error)) {
    weatherState.fetching = false;
    setWeatherStatus("Weather fetch failed");
    return false;
  }

  int currentPos = payload.indexOf("\"current\":");
  int dailyPos = payload.indexOf("\"daily\":");
  int utcOffsetSeconds = 0;
  float currentTemp = 0.0f;
  float currentWind = 0.0f;
  int currentHumidity = 0;
  int currentCode = WEATHER_CONDITION_UNKNOWN;
  float dailyMax[3];
  float dailyMin[3];
  int dailyCode[3];
  String dailyDates[3];

  if (!extractJsonIntKey(payload, "utc_offset_seconds", utcOffsetSeconds) ||
      !extractJsonFloatKey(payload, "temperature_2m", currentTemp, currentPos) ||
      !extractJsonFloatKey(payload, "wind_speed_10m", currentWind, currentPos) ||
      !extractJsonIntKey(payload, "relative_humidity_2m", currentHumidity, currentPos) ||
      !extractJsonIntKey(payload, "weather_code", currentCode, currentPos) ||
      !extractJsonFloatArrayKey(payload, "temperature_2m_max", dailyMax, 3, dailyPos) ||
      !extractJsonFloatArrayKey(payload, "temperature_2m_min", dailyMin, 3, dailyPos) ||
      !extractJsonIntArrayKey(payload, "weather_code", dailyCode, 3, dailyPos) ||
      !extractJsonStringArrayKey(payload, "time", dailyDates, 3, dailyPos)) {
    weatherState.fetching = false;
    setWeatherStatus("Weather parse failed");
    error = "weather_parse_failed";
    return false;
  }

  weatherState.valid = true;
  weatherState.fetching = false;
  weatherState.temperatureC = (int)(currentTemp >= 0.0f ? currentTemp + 0.5f : currentTemp - 0.5f);
  weatherState.humidity = currentHumidity;
  weatherState.windSpeedKmh = (int)(currentWind >= 0.0f ? currentWind + 0.5f : currentWind - 0.5f);
  weatherState.weatherCode = currentCode;
  weatherState.utcOffsetMinutes = utcOffsetSeconds / 60;
  copyToBuffer(weatherState.condition, sizeof(weatherState.condition), String(weatherCodeLabel(currentCode)));
  copyToBuffer(weatherState.updatedAt, sizeof(weatherState.updatedAt), currentLocalTimeText());
  setWeatherStatus("Weather updated");

  for (int i = 0; i < FORECAST_DAYS_TO_SHOW; i++) {
    weatherState.forecast[i].valid = true;
    copyToBuffer(weatherState.forecast[i].label, sizeof(weatherState.forecast[i].label), forecastLabelFromIsoDate(dailyDates[i + 1]));
    weatherState.forecast[i].minTempC = (int)(dailyMin[i + 1] >= 0.0f ? dailyMin[i + 1] + 0.5f : dailyMin[i + 1] - 0.5f);
    weatherState.forecast[i].maxTempC = (int)(dailyMax[i + 1] >= 0.0f ? dailyMax[i + 1] + 0.5f : dailyMax[i + 1] - 0.5f);
    weatherState.forecast[i].weatherCode = dailyCode[i + 1];
  }

  if (weatherState.utcOffsetMinutes >= -720 && weatherState.utcOffsetMinutes <= 840) {
    backlightSettings.timezoneOffsetMinutes = weatherState.utcOffsetMinutes;
  }

  return true;
}

void refreshWeather(bool force) {
  if (!weatherSettings.enabled) {
    if (weatherState.status[0] == 0 || strcmp(weatherState.status, "Weather disabled") != 0) {
      resetWeatherRuntime("Weather disabled");
      invalidateDashboardCache();
      if (!hasNotification)
        renderDashboard();
    }
    return;
  }

  if (!weatherLocationConfigured()) {
    if (strcmp(weatherState.status, "Set a city in the web panel") != 0) {
      resetWeatherRuntime("Set a city in the web panel");
      invalidateDashboardCache();
      if (!hasNotification)
        renderDashboard();
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED)
    return;

  unsigned long now = millis();
  bool due = force || weatherRefreshRequested || nextWeatherRefreshMs == 0 || now >= nextWeatherRefreshMs;
  if (!due)
    return;

  weatherRefreshRequested = false;
  lastWeatherAttemptMs = now;

  String error = "";
  bool success = fetchWeatherNow(error);
  nextWeatherRefreshMs = now + (unsigned long)weatherSettings.refreshMinutes * 60000UL;
  invalidateDashboardCache();
  if (!success && error.length() > 0) {
    setWeatherStatus(error);
  }
  if (!hasNotification)
    renderDashboard();
}

void clearNotification() {
  currentNotification.app[0] = 0;
  currentNotification.bundleId[0] = 0;
  currentNotification.sender[0] = 0;
  currentNotification.title[0] = 0;
  currentNotification.subtitle[0] = 0;
  currentNotification.body[0] = 0;
  currentNotification.accentColor = COLOR_DEFAULT;
  currentNotification.foregroundColor = COLOR_TEXT;
  updatedAt[0] = 0;
  hasNotification = false;
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
  json.reserve(1400);

  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  bool scheduleTimeReady = false;
  int activeBrightness = resolveBacklightBrightness(&scheduleTimeReady);
  String localTimeText = currentLocalTimeText();

  json += "{\"wifiConnected\":";
  json += ((WiFi.status() == WL_CONNECTED) ? "true" : "false");
  json += ",\"ip\":\"";
  json += jsonEscape(ip.c_str());
  json += "\",\"updatedAt\":\"";
  json += jsonEscape(updatedAt);
  json += "\",\"clockLocalTime\":\"";
  json += jsonEscape(currentLocalTimeText().c_str());
  json += "\",\"clockDate\":\"";
  json += jsonEscape(currentLocalDateText().c_str());
  json += "\",\"clockFormat\":\"";
  json += String(clockSettings.formatHours == CLOCK_FORMAT_12H ? "12h" : "24h");
  json += "\",\"hasData\":";
  json += (hasNotification ? "true" : "false");
  json += ",\"updates\":";
  json += String(updateCounter);
  json += ",\"app\":\"";
  json += jsonEscape(currentNotification.app);
  json += "\",\"bundleId\":\"";
  json += jsonEscape(currentNotification.bundleId);
  json += "\",\"sender\":\"";
  json += jsonEscape(currentNotification.sender);
  json += "\",\"title\":\"";
  json += jsonEscape(currentNotification.title);
  json += "\",\"subtitle\":\"";
  json += jsonEscape(currentNotification.subtitle);
  json += "\",\"body\":\"";
  json += jsonEscape(currentNotification.body);
  json += "\",\"accentColor\":\"#";
  if (currentNotification.accentColor < 0x1000) json += "0";
  if (currentNotification.accentColor < 0x0100) json += "0";
  if (currentNotification.accentColor < 0x0010) json += "0";
  json += String(currentNotification.accentColor, HEX);
  json += "\",\"foregroundColor\":\"#";
  if (currentNotification.foregroundColor < 0x1000) json += "0";
  if (currentNotification.foregroundColor < 0x0100) json += "0";
  if (currentNotification.foregroundColor < 0x0010) json += "0";
  json += String(currentNotification.foregroundColor, HEX);
  json += "\",\"durationMs\":";
  json += String(currentNotificationVisibleMs);
  json += ",\"brightnessMode\":\"";
  json += (backlightSettings.mode == BRIGHTNESS_MODE_SCHEDULE ? "schedule" : "manual");
  json += "\",\"manualBrightness\":";
  json += String(backlightSettings.manualBrightness);
  json += ",\"dayBrightness\":";
  json += String(backlightSettings.dayBrightness);
  json += ",\"nightBrightness\":";
  json += String(backlightSettings.nightBrightness);
  json += ",\"dayStart\":\"";
  json += formatClockTime(backlightSettings.dayStartHour, backlightSettings.dayStartMinute);
  json += "\",\"nightStart\":\"";
  json += formatClockTime(backlightSettings.nightStartHour, backlightSettings.nightStartMinute);
  json += "\",\"timezoneOffsetMinutes\":";
  json += String(backlightSettings.timezoneOffsetMinutes);
  json += ",\"activeBrightness\":";
  json += String(activeBrightness);
  json += ",\"scheduleTimeReady\":";
  json += (scheduleTimeReady ? "true" : "false");
  json += ",\"weatherEnabled\":";
  json += (weatherSettings.enabled ? "true" : "false");
  json += ",\"weatherConfigured\":";
  json += (weatherLocationConfigured() ? "true" : "false");
  json += ",\"weatherRefreshMinutes\":";
  json += String(weatherSettings.refreshMinutes);
  json += ",\"weatherProvider\":\"";
  json += WEATHER_PROVIDER_OPEN_METEO;
  json += "\",\"weatherCity\":\"";
  json += jsonEscape(weatherSettings.city);
  json += "\",\"weatherTokenConfigured\":";
  json += (weatherSettings.apiToken[0] ? "true" : "false");
  json += ",\"weatherValid\":";
  json += (weatherState.valid ? "true" : "false");
  json += ",\"weatherTemperature\":";
  json += String(weatherState.temperatureC);
  json += ",\"weatherHumidity\":";
  json += String(weatherState.humidity);
  json += ",\"weatherWindKmh\":";
  json += String(weatherState.windSpeedKmh);
  json += ",\"weatherCode\":";
  json += String(weatherState.weatherCode);
  json += ",\"weatherCondition\":\"";
  json += jsonEscape(weatherState.condition);
  json += "\",\"weatherStatus\":\"";
  json += jsonEscape(weatherState.status);
  json += "\",\"weatherUpdatedAt\":\"";
  json += jsonEscape(weatherState.updatedAt);
  json += "\",\"forecast0Label\":\"";
  json += jsonEscape(weatherState.forecast[0].label);
  json += "\",\"forecast0Min\":";
  json += String(weatherState.forecast[0].minTempC);
  json += ",\"forecast0Max\":";
  json += String(weatherState.forecast[0].maxTempC);
  json += ",\"forecast1Label\":\"";
  json += jsonEscape(weatherState.forecast[1].label);
  json += "\",\"forecast1Min\":";
  json += String(weatherState.forecast[1].minTempC);
  json += ",\"forecast1Max\":";
  json += String(weatherState.forecast[1].maxTempC);
  json += ",\"localTime\":\"";
  json += jsonEscape(localTimeText.c_str());
  json += "\"}";

  return json;
}

void trimLeadingSpaces(String& text) {
  while (text.length() > 0) {
    char c = text.charAt(0);
    if (c != ' ' && c != '\t')
      break;
    text.remove(0, 1);
  }
}

template <typename Surface>
int appendWrappedParagraphOn(
  Surface& surface,
  String paragraph,
  String* lines,
  int startIndex,
  int maxLines,
  int maxWidth,
  bool& truncated
) {
  trimLeadingSpaces(paragraph);

  if (paragraph.length() == 0) {
    if (startIndex < maxLines) {
      lines[startIndex++] = "";
    } else {
      truncated = true;
    }
    return startIndex;
  }

  while (paragraph.length() > 0) {
    if (startIndex >= maxLines) {
      truncated = true;
      return startIndex;
    }

    trimLeadingSpaces(paragraph);
    if (paragraph.length() == 0)
      break;

    if (surface.textWidth(paragraph) <= maxWidth) {
      lines[startIndex++] = paragraph;
      break;
    }

    int lastGood = -1;
    int searchFrom = 0;
    while (true) {
      int space = paragraph.indexOf(' ', searchFrom);
      if (space < 0)
        break;

      String candidate = paragraph.substring(0, space);
      if (surface.textWidth(candidate) <= maxWidth) {
        lastGood = space;
        searchFrom = space + 1;
      } else {
        break;
      }
    }

    if (lastGood < 0) {
      lines[startIndex++] = fitTextOn(surface, paragraph.c_str(), maxWidth);
      if (lines[startIndex - 1].length() < paragraph.length())
        truncated = true;
      break;
    }

    lines[startIndex++] = paragraph.substring(0, lastGood);
    paragraph = paragraph.substring(lastGood + 1);
  }

  return startIndex;
}

template <typename Surface>
int wrapTextLinesOn(Surface& surface, const char* raw, String* lines, int maxLines, int maxWidth) {
  for (int i = 0; i < maxLines; i++) {
    lines[i] = "";
  }

  String source = String(raw);
  source.replace("\r", "");

  int lineCount = 0;
  bool truncated = false;
  int start = 0;

  while (start <= source.length()) {
    int newline = source.indexOf('\n', start);
    if (newline < 0)
      newline = source.length();

    String paragraph = source.substring(start, newline);
    lineCount = appendWrappedParagraphOn(surface, paragraph, lines, lineCount, maxLines, maxWidth, truncated);

    if (newline >= source.length())
      break;

    if (lineCount < maxLines) {
      lines[lineCount++] = "";
    } else {
      truncated = true;
      break;
    }

    start = newline + 1;
  }

  if (truncated && lineCount > 0) {
    lines[lineCount - 1] = fitTextOn(surface, (lines[lineCount - 1] + "...").c_str(), maxWidth);
  }

  return lineCount;
}

char badgeCharForApp(const char* app) {
  if (app[0] == 0)
    return 'N';

  char c = app[0];
  if (c >= 'a' && c <= 'z')
    return c - ('a' - 'A');
  return c;
}

uint16_t accentForApp(const char* app) {
  String value = String(app);
  value.toLowerCase();

  if (value.indexOf("telegram") >= 0)
    return COLOR_TELEGRAM;
  if (value.indexOf("mail") >= 0)
    return COLOR_MAIL;
  if (value.indexOf("message") >= 0 || value.indexOf("imessage") >= 0)
    return COLOR_MESSAGES;
  if (value.indexOf("discord") >= 0)
    return COLOR_DISCORD;
  if (value.indexOf("github") >= 0)
    return COLOR_GITHUB;
  return COLOR_DEFAULT;
}

void invalidateDashboardCache() {
  dashboardCacheValid = false;
  drawnHasNotification = false;
  drawnCardX = DASHBOARD_CARD_X;
  idleScreenDrawn = false;
  drawnIdleClockMain[0] = 0;
  drawnIdleClockSeconds[0] = 0;
  drawnIdleClockSuffix[0] = 0;
  drawnIdleDateLine[0] = 0;
  drawnIdleInfoLine[0] = 0;
  drawnIdleWeatherKey[0] = 0;
  drawnNotification.app[0] = 0;
  drawnNotification.bundleId[0] = 0;
  drawnNotification.sender[0] = 0;
  drawnNotification.title[0] = 0;
  drawnNotification.subtitle[0] = 0;
  drawnNotification.body[0] = 0;
  drawnNotification.accentColor = COLOR_DEFAULT;
  drawnNotification.foregroundColor = COLOR_TEXT;
  drawnUpdatedAt[0] = 0;
}

void syncDashboardCache() {
  dashboardCacheValid = true;
  drawnHasNotification = hasNotification;
  drawnCardX = currentCardX;
  copyToBuffer(drawnNotification.app, sizeof(drawnNotification.app), String(currentNotification.app));
  copyToBuffer(drawnNotification.bundleId, sizeof(drawnNotification.bundleId), String(currentNotification.bundleId));
  copyToBuffer(drawnNotification.sender, sizeof(drawnNotification.sender), String(currentNotification.sender));
  copyToBuffer(drawnNotification.title, sizeof(drawnNotification.title), String(currentNotification.title));
  copyToBuffer(drawnNotification.subtitle, sizeof(drawnNotification.subtitle), String(currentNotification.subtitle));
  copyToBuffer(drawnNotification.body, sizeof(drawnNotification.body), String(currentNotification.body));
  drawnNotification.accentColor = currentNotification.accentColor;
  drawnNotification.foregroundColor = currentNotification.foregroundColor;
  copyToBuffer(drawnUpdatedAt, sizeof(drawnUpdatedAt), String(updatedAt));
}

bool ensureCardSliceSprite(int sliceHeight) {
  if (cardSliceSpriteReady && cardSliceSpriteHeight == sliceHeight)
    return true;

  if (cardSliceSpriteReady) {
    cardSliceSprite.deleteSprite();
    cardSliceSpriteReady = false;
    cardSliceSpriteHeight = 0;
  }

  cardSliceSprite.setColorDepth(16);
  cardSliceSpriteReady = cardSliceSprite.createSprite(DISPLAY_SIZE, sliceHeight) != nullptr;
  if (cardSliceSpriteReady) {
    cardSliceSpriteHeight = sliceHeight;
  }
  return cardSliceSpriteReady;
}

bool notificationCardChanged() {
  if (drawnHasNotification != hasNotification)
    return true;

  if (!hasNotification)
    return false;

  return strcmp(drawnNotification.app, currentNotification.app) != 0 ||
         strcmp(drawnNotification.bundleId, currentNotification.bundleId) != 0 ||
         strcmp(drawnNotification.sender, currentNotification.sender) != 0 ||
         strcmp(drawnNotification.title, currentNotification.title) != 0 ||
         strcmp(drawnNotification.subtitle, currentNotification.subtitle) != 0 ||
         strcmp(drawnNotification.body, currentNotification.body) != 0 ||
         drawnNotification.accentColor != currentNotification.accentColor ||
         drawnNotification.foregroundColor != currentNotification.foregroundColor ||
         strcmp(drawnUpdatedAt, updatedAt) != 0 ||
         drawnCardX != currentCardX;
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
  invalidateDashboardCache();
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
  invalidateDashboardCache();
  tft.fillScreen(COLOR_BG);
  displayCentered("Connecting...", 80, 2, TFT_YELLOW);
  displayCentered(ssid, 120, 1, TFT_WHITE);
}

void showConnectionFailed() {
  invalidateDashboardCache();
  tft.fillScreen(COLOR_BG);
  displayCentered("WiFi failed", 80, 2, TFT_RED);
  displayCentered("Starting AP...", 120, 1, TFT_WHITE);
  delay(2000);
}

template <typename Surface>
void drawNotificationCardOn(Surface& surface, int cardX, int cardY) {
  const int cardW = DASHBOARD_CARD_W;
  const int cardH = DASHBOARD_CARD_H;
  const int bodyWidth = cardW - 30;

  if (cardX >= DISPLAY_SIZE || cardX + cardW <= 0)
    return;

  surface.setTextWrap(false, false);
  uint16_t accent = currentNotification.accentColor ? currentNotification.accentColor : accentForApp(currentNotification.app);
  uint16_t foreground = currentNotification.foregroundColor ? currentNotification.foregroundColor : COLOR_TEXT;

  surface.fillRoundRect(cardX, cardY, cardW, cardH, 14, COLOR_PANEL);
  surface.drawRoundRect(cardX, cardY, cardW, cardH, 14, COLOR_BORDER);
  surface.fillRoundRect(cardX, cardY, 8, cardH, 14, accent);

  const int badgeCx = cardX + 22;
  const int badgeCy = cardY + 18;
  surface.fillCircle(badgeCx, badgeCy, 12, accent);
  surface.setTextSize(2);
  surface.setTextColor(COLOR_TEXT, accent);
  String badge = String(badgeCharForApp(currentNotification.app));
  surface.setTextDatum(MC_DATUM);
  surface.drawString(badge, badgeCx, badgeCy + 1);
  surface.setTextDatum(TL_DATUM);

  surface.setTextSize(1);
  String stamp = updatedAt[0] ? String(updatedAt) : String("");
  int16_t stampWidth = stamp.length() > 0 ? surface.textWidth(stamp) : 0;
  int appMaxWidth = cardW - 52;
  if (stampWidth > 0)
    appMaxWidth -= stampWidth + 10;
  if (appMaxWidth < 48)
    appMaxWidth = 48;
  String appName = currentNotification.app[0] ? fitTextOn(surface, currentNotification.app, appMaxWidth) : String("Notification");

  surface.setTextColor(accent, COLOR_PANEL);
  surface.setCursor(cardX + 40, cardY + 8);
  surface.print(appName);

  if (stamp.length() > 0) {
    surface.setTextColor(COLOR_MUTED, COLOR_PANEL);
    surface.setCursor(cardX + cardW - 12 - stampWidth, cardY + 8);
    surface.print(stamp);
  }

  String senderSource = currentNotification.sender[0] ? String(currentNotification.sender) : String(currentNotification.subtitle);
  String sender = senderSource.length() > 0 ? fitTextOn(surface, senderSource.c_str(), cardW - 52) : String("Incoming notification");
  surface.setTextColor(COLOR_MUTED, COLOR_PANEL);
  surface.setCursor(cardX + 40, cardY + 22);
  surface.print(sender);

  surface.drawFastHLine(cardX + 14, cardY + 40, cardW - 28, COLOR_BORDER);

  int textY = cardY + 50;

  surface.setTextSize(2);
  surface.setTextColor(foreground, COLOR_PANEL);
  String titleLines[2];
  int titleCount = wrapTextLinesOn(surface, currentNotification.title[0] ? currentNotification.title : "Notification", titleLines, 2, bodyWidth);
  for (int i = 0; i < titleCount; i++) {
    surface.setCursor(cardX + 16, textY);
    surface.print(titleLines[i]);
    textY += 18;
  }

  textY += 4;

  surface.setTextSize(1);
  surface.setTextColor(foreground, COLOR_PANEL);
  String bodyLines[MAX_WRAP_LINES];
  int bodyCount = wrapTextLinesOn(surface, currentNotification.body[0] ? currentNotification.body : "No message body.", bodyLines, MAX_WRAP_LINES, bodyWidth);
  for (int i = 0; i < bodyCount; i++) {
    surface.setCursor(cardX + 16, textY);
    surface.print(bodyLines[i]);
    textY += 12;
  }
}

void drawNotificationCardAt(int cardX) {
  drawNotificationCardOn(tft, cardX, DASHBOARD_CARD_Y);
}

void drawNotificationCard() {
  drawNotificationCardAt(currentCardX);
}

void drawNotificationCardSlice(int sliceTop, int sliceHeight, int cardX) {
  if (!ensureCardSliceSprite(sliceHeight)) {
    drawIdleBackgroundRegion(DASHBOARD_CARD_Y + sliceTop, sliceHeight);
    drawNotificationCardAt(cardX);
    return;
  }

  cardSliceSprite.fillSprite(COLOR_IDLE_BG);
  drawIdleClockPanelOn(cardSliceSprite, -(DASHBOARD_CARD_Y + sliceTop));
  drawIdleWeatherPanelOn(cardSliceSprite, -(DASHBOARD_CARD_Y + sliceTop));
  drawNotificationCardOn(cardSliceSprite, cardX, -sliceTop);
  cardSliceSprite.pushSprite(0, DASHBOARD_CARD_Y + sliceTop);
}

void drawNotificationCardSprite() {
  for (int sliceTop = 0; sliceTop < DASHBOARD_CARD_H; sliceTop += DASHBOARD_CARD_SLICE_H) {
    int sliceHeight = DASHBOARD_CARD_SLICE_H;
    if (sliceTop + sliceHeight > DASHBOARD_CARD_H)
      sliceHeight = DASHBOARD_CARD_H - sliceTop;
    drawNotificationCardSlice(sliceTop, sliceHeight, currentCardX);
  }
}

void buildIdleWeatherKey(char* out, size_t outSize) {
  String key = String((int)weatherSettings.enabled);
  key += "|";
  key += String(weatherSettings.city);
  key += "|";
  key += String((int)weatherState.valid);
  key += "|";
  key += String(weatherState.temperatureC);
  key += "|";
  key += String(weatherState.humidity);
  key += "|";
  key += String(weatherState.windSpeedKmh);
  key += "|";
  key += String(weatherState.weatherCode);
  key += "|";
  key += String(weatherState.updatedAt);
  key += "|";
  key += String(weatherState.status);
  for (int i = 0; i < FORECAST_DAYS_TO_SHOW; i++) {
    key += "|";
    key += String((int)weatherState.forecast[i].valid);
    key += "|";
    key += String(weatherState.forecast[i].label);
    key += "|";
    key += String(weatherState.forecast[i].minTempC);
    key += "|";
    key += String(weatherState.forecast[i].maxTempC);
    key += "|";
    key += String(weatherState.forecast[i].weatherCode);
  }
  copyToBuffer(out, outSize, key);
}

bool idleWeatherIsReady() {
  return weatherSettings.enabled && weatherLocationConfigured() && weatherState.valid;
}

void initSmoothFonts() {
#ifdef SMOOTH_FONT
  smoothFontFsReady = LittleFS.begin();
  clockSmoothFontReady = smoothFontFsReady && LittleFS.exists("/" CLOCK_SMOOTH_MAIN_FONT ".vlw");
#else
  smoothFontFsReady = false;
  clockSmoothFontReady = false;
#endif
}

bool idleVisualUsesNightPalette() {
  struct tm localTm;
  if (!getLocalDateTime(localTm))
    return false;
  return localTm.tm_hour < 6 || localTm.tm_hour >= 20;
}

uint16_t idleAccentForWeatherCode(int code) {
  if (code == WEATHER_CONDITION_UNKNOWN)
    return COLOR_IDLE_CITY;
  if (code == 0 || code == 1)
    return COLOR_IDLE_TEMP;
  if (code == 2 || code == 3 || code == 45 || code == 48)
    return 0xC69F;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
    return COLOR_IDLE_HUMIDITY;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    return TFT_WHITE;
  if (code >= 95)
    return 0xFB20;
  return COLOR_IDLE_CITY;
}

template <typename Surface>
void drawIdleStarOn(Surface& surface, int x, int y, uint16_t color) {
  surface.drawPixel(x, y, color);
  surface.drawFastHLine(x - 1, y, 3, color);
  surface.drawFastVLine(x, y - 1, 3, color);
}

template <typename Surface>
void drawIdleSunOn(Surface& surface, int cx, int cy, uint16_t color) {
  surface.fillCircle(cx, cy, 10, color);
  surface.drawLine(cx, cy - 20, cx, cy - 14, color);
  surface.drawLine(cx, cy + 14, cx, cy + 20, color);
  surface.drawLine(cx - 20, cy, cx - 14, cy, color);
  surface.drawLine(cx + 14, cy, cx + 20, cy, color);
  surface.drawLine(cx - 15, cy - 15, cx - 11, cy - 11, color);
  surface.drawLine(cx + 11, cy - 11, cx + 15, cy - 15, color);
  surface.drawLine(cx - 15, cy + 15, cx - 11, cy + 11, color);
  surface.drawLine(cx + 11, cy + 11, cx + 15, cy + 15, color);
}

template <typename Surface>
void drawIdleCompactSunOn(Surface& surface, int cx, int cy, uint16_t color) {
  surface.fillCircle(cx, cy, 6, color);
  surface.drawLine(cx, cy - 11, cx, cy - 8, color);
  surface.drawLine(cx, cy + 8, cx, cy + 11, color);
  surface.drawLine(cx - 11, cy, cx - 8, cy, color);
  surface.drawLine(cx + 8, cy, cx + 11, cy, color);
  surface.drawLine(cx - 8, cy - 8, cx - 6, cy - 6, color);
  surface.drawLine(cx + 6, cy - 6, cx + 8, cy - 8, color);
  surface.drawLine(cx - 8, cy + 8, cx - 6, cy + 6, color);
  surface.drawLine(cx + 6, cy + 6, cx + 8, cy + 8, color);
}

template <typename Surface>
void drawIdleMoonOn(Surface& surface, int cx, int cy, uint16_t color, uint16_t bgColor) {
  surface.fillCircle(cx, cy, 12, color);
  surface.fillCircle(cx + 6, cy - 3, 11, bgColor);
  drawIdleStarOn(surface, cx + 14, cy - 10, TFT_WHITE);
  drawIdleStarOn(surface, cx + 20, cy - 2, TFT_WHITE);
}

template <typename Surface>
void drawIdleCloudOn(Surface& surface, int cx, int cy, uint16_t color) {
  surface.fillCircle(cx - 12, cy + 2, 8, color);
  surface.fillCircle(cx, cy - 2, 11, color);
  surface.fillCircle(cx + 13, cy + 3, 8, color);
  surface.fillRoundRect(cx - 21, cy + 3, 42, 12, 6, color);
}

template <typename Surface>
void drawIdleWeatherIconOn(Surface& surface, int cx, int cy, int weatherCode, bool night, uint16_t bgColor = COLOR_IDLE_BG, bool compact = false) {
  const uint16_t accent = idleAccentForWeatherCode(weatherCode);

  if (compact) {
    if (weatherCode == WEATHER_CONDITION_UNKNOWN) {
      surface.fillCircle(cx, cy, 7, COLOR_IDLE_CITY);
      surface.fillCircle(cx + 3, cy - 2, 6, bgColor);
      drawIdleStarOn(surface, cx + 8, cy - 6, TFT_WHITE);
      return;
    }

    if (weatherCode == 0 || weatherCode == 1) {
      if (night) {
        surface.fillCircle(cx, cy, 7, 0xC7BF);
        surface.fillCircle(cx + 3, cy - 2, 6, bgColor);
        drawIdleStarOn(surface, cx + 8, cy - 6, TFT_WHITE);
      } else {
        drawIdleCompactSunOn(surface, cx, cy, accent);
      }
      return;
    }

    if (weatherCode == 2 || weatherCode == 3 || weatherCode == 45 || weatherCode == 48) {
      if (night) {
        surface.fillCircle(cx - 4, cy - 4, 6, 0xC7BF);
        surface.fillCircle(cx - 1, cy - 6, 5, bgColor);
      } else {
        surface.fillCircle(cx - 4, cy - 4, 5, COLOR_IDLE_TEMP);
      }
      surface.fillCircle(cx - 5, cy + 2, 5, 0xC69F);
      surface.fillCircle(cx + 2, cy, 6, 0xC69F);
      surface.fillCircle(cx + 8, cy + 3, 4, 0xC69F);
      surface.fillRoundRect(cx - 10, cy + 3, 20, 7, 3, 0xC69F);
      return;
    }

    surface.fillCircle(cx - 5, cy + 2, 5, 0xBDF7);
    surface.fillCircle(cx + 2, cy, 6, 0xBDF7);
    surface.fillCircle(cx + 8, cy + 3, 4, 0xBDF7);
    surface.fillRoundRect(cx - 10, cy + 3, 20, 7, 3, 0xBDF7);

    if ((weatherCode >= 51 && weatherCode <= 67) || (weatherCode >= 80 && weatherCode <= 82)) {
      surface.drawLine(cx - 5, cy + 11, cx - 7, cy + 16, COLOR_IDLE_HUMIDITY);
      surface.drawLine(cx + 1, cy + 11, cx - 1, cy + 16, COLOR_IDLE_HUMIDITY);
      surface.drawLine(cx + 7, cy + 11, cx + 5, cy + 16, COLOR_IDLE_HUMIDITY);
      return;
    }

    if ((weatherCode >= 71 && weatherCode <= 77) || weatherCode == 85 || weatherCode == 86) {
      drawIdleStarOn(surface, cx - 5, cy + 13, TFT_WHITE);
      drawIdleStarOn(surface, cx + 1, cy + 15, TFT_WHITE);
      drawIdleStarOn(surface, cx + 7, cy + 13, TFT_WHITE);
      return;
    }

    if (weatherCode >= 95) {
      surface.fillTriangle(cx - 4, cy + 9, cx + 1, cy + 9, cx - 2, cy + 17, 0xFBC0);
      surface.fillTriangle(cx + 1, cy + 9, cx + 5, cy + 9, cx + 2, cy + 17, 0xFBC0);
    }
    return;
  }

  if (weatherCode == WEATHER_CONDITION_UNKNOWN) {
    drawIdleMoonOn(surface, cx, cy, COLOR_IDLE_CITY, bgColor);
    return;
  }

  if (weatherCode == 0 || weatherCode == 1) {
    if (night)
      drawIdleMoonOn(surface, cx, cy, 0xC7BF, bgColor);
    else
      drawIdleSunOn(surface, cx, cy, accent);
    return;
  }

  if (weatherCode == 2 || weatherCode == 3 || weatherCode == 45 || weatherCode == 48) {
    if (night)
      drawIdleMoonOn(surface, cx - 6, cy - 4, 0xC7BF, bgColor);
    else
      drawIdleSunOn(surface, cx - 8, cy - 5, COLOR_IDLE_TEMP);
    drawIdleCloudOn(surface, cx + 4, cy + 2, 0xC69F);
    return;
  }

  drawIdleCloudOn(surface, cx, cy, 0xBDF7);

  if ((weatherCode >= 51 && weatherCode <= 67) || (weatherCode >= 80 && weatherCode <= 82)) {
    for (int i = -10; i <= 10; i += 10) {
      surface.drawLine(cx + i, cy + 16, cx + i - 3, cy + 24, COLOR_IDLE_HUMIDITY);
      surface.drawLine(cx + i + 2, cy + 16, cx + i - 1, cy + 24, COLOR_IDLE_HUMIDITY);
    }
    return;
  }

  if ((weatherCode >= 71 && weatherCode <= 77) || weatherCode == 85 || weatherCode == 86) {
    drawIdleStarOn(surface, cx - 10, cy + 20, TFT_WHITE);
    drawIdleStarOn(surface, cx, cy + 23, TFT_WHITE);
    drawIdleStarOn(surface, cx + 10, cy + 20, TFT_WHITE);
    return;
  }

  if (weatherCode >= 95) {
    surface.fillTriangle(cx - 6, cy + 12, cx + 2, cy + 12, cx - 4, cy + 24, 0xFBC0);
    surface.fillTriangle(cx + 2, cy + 12, cx + 8, cy + 12, cx + 1, cy + 25, 0xFBC0);
  }
}

template <typename Surface>
void drawIdleStatusPillOn(Surface& surface, int x, int y, const String& text) {
  String pill = text;
  surface.setTextWrap(false, false);
  surface.setTextSize(1);
  pill = fitTextOn(surface, pill.c_str(), 74);
  int pillW = surface.textWidth(pill) + 16;
  if (pillW < 48)
    pillW = 48;
  surface.fillRoundRect(x, y, pillW, 18, 9, COLOR_IDLE_PILL_BG);
  surface.setTextColor(COLOR_IDLE_PILL_TEXT, COLOR_IDLE_PILL_BG);
  surface.setCursor(x + 8, y + 5);
  surface.print(pill);
}

template <typename Surface>
void drawIdleMetricRowOn(Surface& surface, int x, int y, int totalWidth, bool humidityMetric, const String& label, const String& value, uint16_t accent, int fillPercent) {
  fillPercent = clampInt(fillPercent, 0, 100);
  if (humidityMetric) {
    surface.fillTriangle(x + 8, y + 2, x + 2, y + 10, x + 14, y + 10, accent);
    surface.fillCircle(x + 8, y + 12, 5, accent);
  } else {
    surface.fillRoundRect(x + 6, y + 1, 4, 15, 2, accent);
    surface.fillCircle(x + 8, y + 16, 5, accent);
  }

  surface.setTextSize(1);
  surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
  surface.setCursor(x + 20, y);
  surface.print(label);

  surface.setTextSize(2);
  int valueWidth = surface.textWidth(value);
  int valueX = x + totalWidth - valueWidth;
  int barX = x + 20;
  int barW = valueX - barX - 8;
  if (barW < 28)
    barW = 28;
  if (barX + barW > x + totalWidth - 8)
    barW = x + totalWidth - 8 - barX;
  if (barW < 12)
    barW = 12;

  surface.fillRoundRect(barX, y + 10, barW, 6, 3, COLOR_IDLE_BAR_BG);
  int filled = (barW * fillPercent) / 100;
  if (filled > 0)
    surface.fillRoundRect(barX, y + 10, filled, 6, 3, accent);

  surface.setTextColor(COLOR_TEXT, COLOR_IDLE_BG);
  surface.setCursor(valueX, y + 1);
  surface.print(value);
}

template <typename Surface>
void drawIdleForecastChipOn(Surface& surface, int x, int y, int w, int h, const ForecastDay& day) {
  const uint16_t accent = day.valid ? idleAccentForWeatherCode(day.weatherCode) : COLOR_IDLE_DIVIDER;
  surface.fillRoundRect(x, y, w, h, 10, COLOR_IDLE_CHIP);
  surface.drawRoundRect(x, y, w, h, 10, accent);

  surface.setTextSize(1);
  if (!day.valid) {
    surface.setTextColor(COLOR_MUTED, COLOR_IDLE_CHIP);
    surface.setCursor(x + 8, y + h / 2 - 4);
    surface.print("--");
    return;
  }

  String label = fitTextOn(surface, day.label, w - 12);
  surface.setTextColor(accent, COLOR_IDLE_CHIP);
  int labelWidth = surface.textWidth(label);
  surface.setCursor(x + (w - labelWidth) / 2, y + 7);
  surface.print(label);

  drawIdleWeatherIconOn(surface, x + w / 2, y + 32, day.weatherCode, idleVisualUsesNightPalette(), COLOR_IDLE_CHIP, true);

  String maxNumber = String(day.maxTempC);
  String minText = String(day.minTempC) + "C";
  surface.setTextSize(2);
  surface.setTextColor(COLOR_TEXT, COLOR_IDLE_CHIP);
  int maxNumberWidth = surface.textWidth(maxNumber);
  surface.setTextSize(1);
  int maxUnitWidth = surface.textWidth("C");
  int maxWidth = maxNumberWidth + maxUnitWidth + 1;
  int maxX = x + (w - maxWidth) / 2;
  surface.setTextSize(2);
  surface.setCursor(maxX, y + h - 33);
  surface.print(maxNumber);
  surface.setTextSize(1);
  surface.setCursor(maxX + maxNumberWidth + 1, y + h - 28);
  surface.print("C");
  surface.setTextSize(1);
  int minWidth = surface.textWidth(minText);
  surface.setTextColor(0xBDF7, COLOR_IDLE_CHIP);
  surface.setCursor(x + (w - minWidth) / 2, y + h - 13);
  surface.print(minText);
}

bool drawIdleSmoothClockTimeOn(TFT_eSPI& surface, int offsetY, const String& hoursText, const String& minutesText, const char* secondsText, const char* suffixText) {
#ifdef SMOOTH_FONT
  if (!clockSmoothFontReady)
    return false;

  const int gapLarge = 1;
  const int gapSmall = 1;
  const int timeBottom = offsetY + 114;
  const int smoothTop = timeBottom - 48;
  const int secondsTop = timeBottom - 24;
  const int dotTop = timeBottom - 16;

  surface.setTextSize(2);
  int dotWidth = surface.textWidth(".");
  surface.setTextSize(3);
  int secondsWidth = surface.textWidth(secondsText);

  surface.setTextSize(1);
  surface.setTextDatum(TL_DATUM);
  surface.loadFont(CLOCK_SMOOTH_MAIN_FONT, LittleFS);
  int hoursWidth = surface.textWidth(hoursText);
  int colonWidth = surface.textWidth(":");
  int minutesWidth = surface.textWidth(minutesText);
  int timeWidth = hoursWidth + colonWidth + minutesWidth + dotWidth + secondsWidth + gapLarge * 2 + gapSmall * 2;
  int hoursX = (DISPLAY_SIZE - timeWidth) / 2;
  if (hoursX < 8)
    hoursX = 8;

  surface.setTextColor(COLOR_IDLE_HOURS, COLOR_IDLE_BG);
  surface.drawString(hoursText, hoursX, smoothTop);
  int colonX = hoursX + hoursWidth + gapLarge;
  surface.setTextColor(COLOR_IDLE_MINUTES, COLOR_IDLE_BG);
  surface.drawString(":", colonX, smoothTop);
  int minutesX = colonX + colonWidth + gapLarge;
  surface.drawString(minutesText, minutesX, smoothTop);
  surface.unloadFont();

  int dotX = minutesX + minutesWidth + gapSmall;
  surface.setTextSize(2);
  surface.setTextColor(COLOR_IDLE_SECONDS, COLOR_IDLE_BG);
  surface.setCursor(dotX, dotTop);
  surface.print(".");

  int secondsX = dotX + dotWidth + gapSmall;
  surface.setTextSize(3);
  surface.setCursor(secondsX, secondsTop);
  surface.print(secondsText);

  if (suffixText[0]) {
    surface.setTextSize(2);
    surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
    int suffixWidth = surface.textWidth(suffixText);
    int suffixX = secondsX + (secondsWidth / 2) - (suffixWidth / 2);
    surface.setCursor(suffixX, offsetY + 72);
    surface.print(suffixText);
  }

  return true;
#else
  (void)surface;
  (void)offsetY;
  (void)hoursText;
  (void)minutesText;
  (void)secondsText;
  (void)suffixText;
  return false;
#endif
}

bool drawIdleSmoothClockTimeOn(TFT_eSprite& surface, int offsetY, const String& hoursText, const String& minutesText, const char* secondsText, const char* suffixText) {
  (void)surface;
  (void)offsetY;
  (void)hoursText;
  (void)minutesText;
  (void)secondsText;
  (void)suffixText;
  return false;
}

template <typename Surface>
void drawIdleClockPanelOn(Surface& surface, int offsetY) {
  char mainTime[8];
  char secondsText[4];
  char suffixText[4];
  buildClockDisplay(mainTime, sizeof(mainTime), secondsText, sizeof(secondsText), suffixText, sizeof(suffixText));
  String dateLine = currentLocalDateText();
  String city = weatherSettings.city[0] ? String(weatherSettings.city) : String("Clock");
  String infoLine = currentIdleInfoText();

  String timeText = String(mainTime);
  String hoursText = timeText.length() >= 2 ? timeText.substring(0, 2) : String("--");
  String minutesText = timeText.length() >= 5 ? timeText.substring(3, 5) : String("--");
  bool night = idleVisualUsesNightPalette();

  surface.setTextWrap(false, false);
  surface.fillRect(0, offsetY, DISPLAY_SIZE, IDLE_TOP_REGION_H, COLOR_IDLE_BG);
  surface.drawFastHLine(12, offsetY + IDLE_TOP_REGION_H - 1, DISPLAY_SIZE - 24, COLOR_IDLE_DIVIDER);

  surface.setTextSize(2);
  surface.setTextColor(COLOR_IDLE_CITY, COLOR_IDLE_BG);
  surface.setCursor(14, offsetY + 12);
  surface.print(fitTextOn(surface, city.c_str(), 118));

  surface.setTextSize(1);
  surface.setTextColor(COLOR_TEXT, COLOR_IDLE_BG);
  surface.setCursor(14, offsetY + 34);
  surface.print(fitTextOn(surface, infoLine.c_str(), 122));

  if (!drawIdleSmoothClockTimeOn(surface, offsetY, hoursText, minutesText, secondsText, suffixText)) {
    surface.setTextSize(5);
    int hoursWidth = surface.textWidth(hoursText);
    int minutesWidth = surface.textWidth(minutesText);
    surface.setTextSize(5);
    int colonWidth = surface.textWidth(":");
    surface.setTextSize(2);
    int dotWidth = surface.textWidth(".");
    surface.setTextSize(3);
    int secondsWidth = surface.textWidth(secondsText);

    const int gapLarge = 0;
    const int gapSmall = 1;
    int timeWidth = hoursWidth + colonWidth + minutesWidth + dotWidth + secondsWidth + gapLarge * 2 + gapSmall * 2;
    int hoursX = (DISPLAY_SIZE - timeWidth) / 2;
    if (hoursX < 8)
      hoursX = 8;
    const int timeBottom = offsetY + 114;
    const int digitTop = timeBottom - 40;
    const int secondsTop = timeBottom - 24;
    const int dotTop = timeBottom - 16;

    surface.setTextSize(5);
    surface.setTextColor(COLOR_IDLE_HOURS, COLOR_IDLE_BG);
    surface.setCursor(hoursX, digitTop);
    surface.print(hoursText);

    surface.setTextSize(5);
    int colonX = hoursX + hoursWidth + gapLarge;
    surface.setTextColor(COLOR_IDLE_MINUTES, COLOR_IDLE_BG);
    surface.setCursor(colonX, digitTop);
    surface.print(":");

    surface.setTextSize(5);
    surface.setTextColor(COLOR_IDLE_MINUTES, COLOR_IDLE_BG);
    int minutesX = colonX + colonWidth + gapLarge;
    surface.setCursor(minutesX, digitTop);
    surface.print(minutesText);

    surface.setTextSize(3);
    surface.setTextColor(COLOR_IDLE_SECONDS, COLOR_IDLE_BG);
    int dotX = minutesX + minutesWidth + gapSmall;
    surface.setTextSize(2);
    surface.setCursor(dotX, dotTop);
    surface.print(".");

    surface.setTextSize(3);
    int secondsX = dotX + dotWidth + gapSmall;
    surface.setCursor(secondsX, secondsTop);
    surface.print(secondsText);

    if (suffixText[0]) {
      surface.setTextSize(2);
      surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
      int suffixWidth = surface.textWidth(suffixText);
      int suffixX = secondsX + (secondsWidth / 2) - (suffixWidth / 2);
      surface.setCursor(suffixX, offsetY + 72);
      surface.print(suffixText);
    }
  }

  surface.setTextSize(1);
  String pillText = idleWeatherIsReady() ? String(weatherState.condition) : (weatherSettings.enabled ? String("Weather") : String("Clock"));
  pillText = fitTextOn(surface, pillText.c_str(), 56);
  int pillWidth = surface.textWidth(pillText) + 16;
  if (pillWidth < 48)
    pillWidth = 48;
  int pillCenterX = 206;
  int pillX = pillCenterX - pillWidth / 2;
  if (pillX < 154)
    pillX = 154;
  if (pillX + pillWidth > DISPLAY_SIZE - 8)
    pillX = DISPLAY_SIZE - 8 - pillWidth;
  int iconCenterX = pillX + pillWidth / 2;
  drawIdleWeatherIconOn(surface, iconCenterX, offsetY + 24, weatherState.weatherCode, night);
  drawIdleStatusPillOn(surface, pillX, offsetY + 49, pillText);

  surface.setTextSize(2);
  surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
  int16_t dateWidth = surface.textWidth(dateLine);
  surface.setCursor((DISPLAY_SIZE - dateWidth) / 2, offsetY + 118);
  surface.print(dateLine);
}

template <typename Surface>
void drawIdleWeatherPanelOn(Surface& surface, int offsetY) {
  const int baseY = IDLE_BOTTOM_REGION_Y + offsetY;
  surface.setTextWrap(false, false);
  surface.fillRect(0, baseY, DISPLAY_SIZE, IDLE_BOTTOM_REGION_H, COLOR_IDLE_BG);

  if (!weatherSettings.enabled) {
    surface.setTextSize(2);
    surface.setTextColor(COLOR_TEXT, COLOR_IDLE_BG);
    surface.setCursor(14, baseY + 26);
    surface.print("Weather off");
    surface.setTextSize(1);
    surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
    surface.setCursor(14, baseY + 48);
    surface.print("Enable weather in the web panel");
    return;
  }

  if (!weatherLocationConfigured()) {
    surface.setTextSize(2);
    surface.setTextColor(COLOR_TEXT, COLOR_IDLE_BG);
    surface.setCursor(14, baseY + 26);
    surface.print("Set city");
    surface.setTextSize(1);
    surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
    surface.setCursor(14, baseY + 48);
    surface.print("Add a city in Clock & Weather");
    return;
  }

  if (!weatherState.valid) {
    surface.setTextSize(2);
    surface.setTextColor(COLOR_TEXT, COLOR_IDLE_BG);
    surface.setCursor(14, baseY + 26);
    surface.print("Syncing");
    surface.setTextSize(1);
    surface.setTextColor(COLOR_MUTED, COLOR_IDLE_BG);
    surface.setCursor(14, baseY + 48);
    surface.print(fitTextOn(surface, weatherState.status, 190));
    return;
  }

  int tempFill = clampInt((weatherState.temperatureC + 10) * 100 / 45, 0, 100);
  int humidityFill = clampInt(weatherState.humidity, 0, 100);
  drawIdleMetricRowOn(surface, 10, baseY + 25, 92, false, "TEMP", String(weatherState.temperatureC) + "C", COLOR_IDLE_TEMP, tempFill);
  drawIdleMetricRowOn(surface, 10, baseY + 53, 92, true, "HUM", String(weatherState.humidity) + "%", COLOR_IDLE_HUMIDITY, humidityFill);

  drawIdleForecastChipOn(surface, 108, baseY + 8, 60, 82, weatherState.forecast[0]);
  drawIdleForecastChipOn(surface, 172, baseY + 8, 60, 82, weatherState.forecast[1]);
}

void drawIdleClockPanel() {
  drawIdleClockPanelOn(tft, 0);
}

void drawIdleWeatherPanel() {
  drawIdleWeatherPanelOn(tft, 0);
}

void drawIdleBackgroundSlice(int sliceTop, int sliceHeight) {
  if (!ensureCardSliceSprite(sliceHeight)) {
    tft.fillRect(0, sliceTop, DISPLAY_SIZE, sliceHeight, COLOR_IDLE_BG);
    drawIdleClockPanel();
    drawIdleWeatherPanel();
    return;
  }

  cardSliceSprite.fillSprite(COLOR_IDLE_BG);
  drawIdleClockPanelOn(cardSliceSprite, -sliceTop);
  drawIdleWeatherPanelOn(cardSliceSprite, -sliceTop);
  cardSliceSprite.pushSprite(0, sliceTop);
}

void drawIdleBackgroundRegion(int startY, int height) {
  int sliceTop = startY;
  int remaining = height;
  while (remaining > 0) {
    int sliceHeight = remaining > DASHBOARD_CARD_SLICE_H ? DASHBOARD_CARD_SLICE_H : remaining;
    drawIdleBackgroundSlice(sliceTop, sliceHeight);
    sliceTop += sliceHeight;
    remaining -= sliceHeight;
  }
}

void renderIdleDashboard(bool forceFull) {
  char mainTime[8];
  char secondsText[4];
  char suffixText[4];
  char infoLine[48];
  char weatherKey[192];
  buildClockDisplay(mainTime, sizeof(mainTime), secondsText, sizeof(secondsText), suffixText, sizeof(suffixText));
  String dateLine = currentLocalDateText();
  copyToBuffer(infoLine, sizeof(infoLine), currentIdleInfoText());
  buildIdleWeatherKey(weatherKey, sizeof(weatherKey));

  bool clockChanged = strcmp(drawnIdleClockMain, mainTime) != 0 ||
                      strcmp(drawnIdleClockSeconds, secondsText) != 0 ||
                      strcmp(drawnIdleClockSuffix, suffixText) != 0 ||
                      strcmp(drawnIdleDateLine, dateLine.c_str()) != 0;
  bool infoChanged = strcmp(drawnIdleInfoLine, infoLine) != 0;
  bool weatherChanged = strcmp(drawnIdleWeatherKey, weatherKey) != 0;

  if (forceFull || !idleScreenDrawn) {
    drawIdleBackgroundRegion(0, DISPLAY_SIZE);
    idleScreenDrawn = true;
  } else {
    if (weatherChanged)
      drawIdleBackgroundRegion(0, DISPLAY_SIZE);
    else if (clockChanged || infoChanged)
      drawIdleBackgroundRegion(0, IDLE_TOP_REGION_H);
  }

  copyToBuffer(drawnIdleClockMain, sizeof(drawnIdleClockMain), String(mainTime));
  copyToBuffer(drawnIdleClockSeconds, sizeof(drawnIdleClockSeconds), String(secondsText));
  copyToBuffer(drawnIdleClockSuffix, sizeof(drawnIdleClockSuffix), String(suffixText));
  copyToBuffer(drawnIdleDateLine, sizeof(drawnIdleDateLine), dateLine);
  copyToBuffer(drawnIdleInfoLine, sizeof(drawnIdleInfoLine), String(infoLine));
  copyToBuffer(drawnIdleWeatherKey, sizeof(drawnIdleWeatherKey), String(weatherKey));
}

void renderWaitingState() {
  renderIdleDashboard(true);
}

void renderDashboard() {
  if (!hasNotification) {
    renderIdleDashboard(!dashboardCacheValid || drawnHasNotification != hasNotification);
    syncDashboardCache();
    return;
  }

  if (!dashboardCacheValid || drawnHasNotification != hasNotification) {
    drawIdleBackgroundRegion(0, DISPLAY_SIZE);
    drawNotificationCard();
    syncDashboardCache();
    return;
  }

  if (!notificationCardChanged()) {
    return;
  }

  drawNotificationCardSprite();
  syncDashboardCache();
}

// ===================== Web pages =====================

const char NOTIFY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Notifications</title>
<style>
body{font-family:sans-serif;background:#10161f;color:#e8eef6;margin:0;padding:16px}
.panel{max-width:440px;margin:0 auto}
h2{margin:0 0 8px;color:#84d6ff}
h3{margin:0 0 8px;color:#d7e9ff}
.sub{margin:0 0 16px;color:#a7b6c9;font-size:14px;line-height:1.4}
label{display:block;font-size:13px;color:#bfd0e3;margin:12px 0}
input,textarea,select{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:1px solid #31425a;border-radius:10px;background:#0f1520;color:#eef6ff;font:inherit}
input[type='range']{padding:0;border:none;background:transparent}
textarea{min-height:96px;resize:vertical}
.panel-block{margin-top:18px;padding:14px;border:1px solid #263345;border-radius:14px;background:#0b1119}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.range-head{display:flex;align-items:center;justify-content:space-between;gap:12px}
.range-value{color:#84d6ff;font-weight:700}
.tiny{margin-top:8px;color:#8ea4be;font-size:12px;line-height:1.4}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
button{flex:1;min-width:120px;padding:12px;border:none;border-radius:10px;background:#84d6ff;color:#06222c;font-weight:700;cursor:pointer}
button.alt{background:#28364b;color:#dce9f8}
#status{margin-top:12px;color:#9ef0a8;min-height:20px}
#brightnessStatus{margin-top:12px;color:#9ef0a8;min-height:20px}
#clockWeatherStatus{margin-top:12px;color:#9ef0a8;min-height:20px}
pre{background:#0b1018;border:1px solid #263345;border-radius:12px;padding:12px;white-space:pre-wrap;word-break:break-word;color:#a7f5ba}
</style>
</head>
<body>
<div class='panel'>
  <h2>Notifications</h2>
  <p class='sub'>Use this page to test <code>POST /notify</code>. The ESP shows the latest notification only, so each upload replaces the current card. Backlight settings are saved on the device.</p>
	<form id='form'>
	    <label>App
	      <input name='app' placeholder='Telegram'>
	    </label>
	    <label>Bundle ID
	      <input name='bundleId' placeholder='com.tdesktop.telegram'>
	    </label>
	    <label>Sender
	      <input name='sender' placeholder='Alice'>
	    </label>
	    <label>Title
	      <input name='title' placeholder='New message'>
	    </label>
	    <label>Subtitle
	      <input name='subtitle' placeholder='Chat or channel'>
	    </label>
	    <label>Body
	      <textarea name='body' placeholder='Hey, can you check the new layout on the device?'></textarea>
	    </label>
	    <label>Time
	      <input name='updatedAt' placeholder='10:42:15'>
	    </label>
	    <div class='grid'>
	      <label>Accent
	        <input name='accent' type='color' value='#2AABEE'>
	      </label>
	      <label>Text
	        <input name='foreground' type='color' value='#FFFFFF'>
	      </label>
	    </div>
	    <label>Visible seconds
	      <input name='durationSeconds' type='number' min='1' max='30' step='1' value='3'>
	    </label>

    <div class='actions'>
      <button type='button' class='alt' onclick='loadTelegramDemo()'>Telegram demo</button>
      <button type='button' class='alt' onclick='loadMailDemo()'>Mail demo</button>
      <button type='submit'>Upload</button>
      <button type='button' class='alt' onclick='clearNotification()'>Clear</button>
      <button type='button' class='alt' onclick='refreshState()'>Refresh state</button>
    </div>
  </form>

  <div class='panel-block'>
    <h3>Backlight</h3>
    <p class='sub'>Use a manual brightness slider or switch automatically between day and night by local time.</p>
    <form id='brightnessForm'>
      <label>Mode
        <select name='brightnessMode'>
          <option value='manual'>Manual brightness</option>
          <option value='schedule'>Day / night schedule</option>
        </select>
      </label>

      <label class='manual-only'> 
        <span class='range-head'>
          <span>Manual brightness</span>
          <span class='range-value' id='manualBrightnessValue'>72%</span>
        </span>
        <input type='range' name='manualBrightness' min='0' max='100' value='72'>
      </label>

      <div class='schedule-only'>
        <label>
          <span class='range-head'>
            <span>Day brightness</span>
            <span class='range-value' id='dayBrightnessValue'>88%</span>
          </span>
          <input type='range' name='dayBrightness' min='0' max='100' value='88'>
        </label>

        <label>
          <span class='range-head'>
            <span>Night brightness</span>
            <span class='range-value' id='nightBrightnessValue'>14%</span>
          </span>
          <input type='range' name='nightBrightness' min='0' max='100' value='14'>
        </label>

        <div class='grid'>
          <label>Day starts
            <input type='time' name='dayStart' value='08:00'>
          </label>
          <label>Night starts
            <input type='time' name='nightStart' value='22:00'>
          </label>
        </div>
        <div class='tiny'>The browser sends its current UTC offset when you save settings, so the schedule follows your local time.</div>
      </div>

      <div class='actions'>
        <button type='submit'>Save backlight</button>
      </div>
    </form>
    <div id='brightnessStatus'>Idle</div>
  </div>

  <div class='panel-block'>
    <h3>Clock & Weather</h3>
    <p class='sub'>Idle screen shows live time plus weather from Open-Meteo. This provider is free and does not require a token; the token field is optional and reserved for future providers.</p>
    <form id='clockWeatherForm'>
      <div class='grid'>
        <label>Time format
          <select name='clockFormat'>
            <option value='24h'>24-hour</option>
            <option value='12h'>12-hour</option>
          </select>
        </label>
        <label>Weather
          <select name='weatherEnabled'>
            <option value='true'>Enabled</option>
            <option value='false'>Disabled</option>
          </select>
        </label>
      </div>

      <label>City
        <input name='weatherCity' placeholder='Warsaw'>
      </label>

      <div class='grid'>
        <label>Refresh minutes
          <input name='weatherRefreshMinutes' type='number' min='15' max='240' step='5' value='30'>
        </label>
        <label>Token (optional)
          <input name='weatherToken' placeholder='Unused for Open-Meteo'>
        </label>
      </div>

      <div class='actions'>
        <button type='submit'>Save clock/weather</button>
        <button type='button' class='alt' onclick='refreshWeatherNow()'>Refresh weather now</button>
      </div>
    </form>
    <div id='clockWeatherStatus'>Idle</div>
  </div>

  <div id='status'>Idle</div>
  <pre id='state'>Loading...</pre>
</div>

<script>
const form = document.getElementById('form');
const brightnessForm = document.getElementById('brightnessForm');
const clockWeatherForm = document.getElementById('clockWeatherForm');
const statusEl = document.getElementById('status');
const brightnessStatusEl = document.getElementById('brightnessStatus');
const clockWeatherStatusEl = document.getElementById('clockWeatherStatus');
const stateEl = document.getElementById('state');

function setFormValue(targetForm, name, value){
  const input = targetForm.elements.namedItem(name);
  if(input) input.value = value == null ? '' : String(value);
}

function setValue(name, value){
  setFormValue(form, name, value);
}

	function syncForm(state){
	  setValue('app', state && state.app ? state.app : '');
	  setValue('bundleId', state && state.bundleId ? state.bundleId : '');
	  setValue('sender', state && state.sender ? state.sender : '');
	  setValue('title', state && state.title ? state.title : '');
	  setValue('subtitle', state && state.subtitle ? state.subtitle : '');
	  setValue('body', state && state.body ? state.body : '');
	  setValue('updatedAt', state && state.updatedAt ? state.updatedAt : '');
	  setValue('durationSeconds', state && Number.isFinite(state.durationMs) ? Math.round(state.durationMs / 1000) : 3);
	}

function updateBrightnessLabels(){
  document.getElementById('manualBrightnessValue').textContent = String(brightnessForm.elements.namedItem('manualBrightness').value) + '%';
  document.getElementById('dayBrightnessValue').textContent = String(brightnessForm.elements.namedItem('dayBrightness').value) + '%';
  document.getElementById('nightBrightnessValue').textContent = String(brightnessForm.elements.namedItem('nightBrightness').value) + '%';
}

function syncBrightnessMode(){
  const scheduled = brightnessForm.elements.namedItem('brightnessMode').value === 'schedule';
  document.querySelectorAll('.schedule-only').forEach((node) => {
    node.style.display = scheduled ? '' : 'none';
  });
  document.querySelectorAll('.manual-only').forEach((node) => {
    node.style.display = scheduled ? 'none' : '';
  });
}

function syncBrightnessForm(state){
  setFormValue(brightnessForm, 'brightnessMode', state && state.brightnessMode ? state.brightnessMode : 'manual');
  setFormValue(brightnessForm, 'manualBrightness', state && Number.isFinite(state.manualBrightness) ? state.manualBrightness : 72);
  setFormValue(brightnessForm, 'dayBrightness', state && Number.isFinite(state.dayBrightness) ? state.dayBrightness : 88);
  setFormValue(brightnessForm, 'nightBrightness', state && Number.isFinite(state.nightBrightness) ? state.nightBrightness : 14);
  setFormValue(brightnessForm, 'dayStart', state && state.dayStart ? state.dayStart : '08:00');
  setFormValue(brightnessForm, 'nightStart', state && state.nightStart ? state.nightStart : '22:00');
  updateBrightnessLabels();
  syncBrightnessMode();

  const info = [];
  if (state && Number.isFinite(state.activeBrightness)) info.push('active ' + state.activeBrightness + '%');
  if (state && state.brightnessMode) info.push(state.brightnessMode === 'schedule' ? 'scheduled' : 'manual');
  if (state && state.localTime) info.push('local time ' + state.localTime);
  if (state && state.brightnessMode === 'schedule' && state.scheduleTimeReady === false) info.push('clock pending');
  brightnessStatusEl.textContent = info.length ? info.join(' • ') : 'Idle';
}

function syncClockWeatherForm(state){
  setFormValue(clockWeatherForm, 'clockFormat', state && state.clockFormat ? state.clockFormat : '24h');
  setFormValue(clockWeatherForm, 'weatherEnabled', state && state.weatherEnabled ? 'true' : 'false');
  setFormValue(clockWeatherForm, 'weatherCity', state && state.weatherCity ? state.weatherCity : '');
  setFormValue(clockWeatherForm, 'weatherRefreshMinutes', state && Number.isFinite(state.weatherRefreshMinutes) ? state.weatherRefreshMinutes : 30);
  setFormValue(clockWeatherForm, 'weatherToken', '');

  const info = [];
  if (state && state.clockLocalTime) info.push('clock ' + state.clockLocalTime);
  if (state && state.clockDate) info.push(state.clockDate);
  if (state && state.weatherValid && state.weatherCondition) info.push(state.weatherCondition);
  if (state && state.weatherValid && Number.isFinite(state.weatherTemperature)) info.push(state.weatherTemperature + 'C');
  if (state && state.weatherValid && Number.isFinite(state.weatherHumidity)) info.push(state.weatherHumidity + '%');
  if (state && state.weatherStatus) info.push(state.weatherStatus);
  clockWeatherStatusEl.textContent = info.length ? info.join(' • ') : 'Idle';
}

function loadTelegramDemo(){
	  const stamp = new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'});
	  setValue('app', 'Telegram');
	  setValue('bundleId', 'com.tdesktop.telegram');
	  setValue('sender', 'Alice');
	  setValue('title', 'New message');
	  setValue('subtitle', 'Friends');
	  setValue('body', 'Hey, the ESP notification sketch is up. Check the device and tell me if the text card feels balanced.');
	  setValue('updatedAt', stamp);
	  setValue('accent', '#2AABEE');
	  setValue('foreground', '#FFFFFF');
	  setValue('durationSeconds', '5');
	}

function loadMailDemo(){
	  const stamp = new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'});
	  setValue('app', 'Mail');
	  setValue('bundleId', 'com.apple.mail');
	  setValue('sender', 'Build Bot');
	  setValue('title', 'CI finished successfully');
	  setValue('subtitle', 'Release pipeline');
	  setValue('body', 'The latest pipeline passed. Release artifacts are ready for review.');
	  setValue('updatedAt', stamp);
	  setValue('accent', '#FFB020');
	  setValue('foreground', '#FFFFFF');
	  setValue('durationSeconds', '8');
	}

async function refreshState(){
  try{
    const resp = await fetch('/state');
    const data = await resp.json();
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    syncBrightnessForm(data);
    syncClockWeatherForm(data);
    statusEl.textContent = 'State loaded';
  }catch(err){
    statusEl.textContent = 'State error: ' + err.message;
  }
}

async function clearNotification(){
  try{
    statusEl.textContent = 'Clearing...';
    const resp = await fetch('/clear', {method:'POST'});
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || ('HTTP ' + resp.status));
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    syncBrightnessForm(data);
    syncClockWeatherForm(data);
    statusEl.textContent = 'Cleared';
  }catch(err){
    statusEl.textContent = 'Clear error: ' + err.message;
  }
}

async function refreshWeatherNow(){
  try {
    clockWeatherStatusEl.textContent = 'Refreshing weather...';
    const resp = await fetch('/weather/refresh', {method:'POST'});
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || ('HTTP ' + resp.status));
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    syncBrightnessForm(data);
    syncClockWeatherForm(data);
    clockWeatherStatusEl.textContent = 'Weather refreshed';
  } catch (err) {
    clockWeatherStatusEl.textContent = 'Refresh error: ' + err.message;
  }
}

brightnessForm.addEventListener('submit', async (event) => {
  event.preventDefault();

  const params = new URLSearchParams();
  params.append('brightnessMode', brightnessForm.elements.namedItem('brightnessMode').value);
  params.append('manualBrightness', brightnessForm.elements.namedItem('manualBrightness').value);
  params.append('dayBrightness', brightnessForm.elements.namedItem('dayBrightness').value);
  params.append('nightBrightness', brightnessForm.elements.namedItem('nightBrightness').value);
  params.append('dayStart', brightnessForm.elements.namedItem('dayStart').value);
  params.append('nightStart', brightnessForm.elements.namedItem('nightStart').value);
  params.append('timezoneOffsetMinutes', String(-new Date().getTimezoneOffset()));

  try {
    brightnessStatusEl.textContent = 'Saving...';
    const resp = await fetch('/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body: params.toString()
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || ('HTTP ' + resp.status));
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    syncBrightnessForm(data);
    brightnessStatusEl.textContent = 'Saved';
  } catch (err) {
    brightnessStatusEl.textContent = 'Save error: ' + err.message;
  }
});

['manualBrightness', 'dayBrightness', 'nightBrightness'].forEach((name) => {
  brightnessForm.elements.namedItem(name).addEventListener('input', updateBrightnessLabels);
});
brightnessForm.elements.namedItem('brightnessMode').addEventListener('change', syncBrightnessMode);

clockWeatherForm.addEventListener('submit', async (event) => {
  event.preventDefault();

  const params = new URLSearchParams();
  params.append('clockFormat', clockWeatherForm.elements.namedItem('clockFormat').value);
  params.append('weatherEnabled', clockWeatherForm.elements.namedItem('weatherEnabled').value);
  params.append('weatherCity', clockWeatherForm.elements.namedItem('weatherCity').value.trim());
  params.append('weatherRefreshMinutes', clockWeatherForm.elements.namedItem('weatherRefreshMinutes').value);
  params.append('weatherToken', clockWeatherForm.elements.namedItem('weatherToken').value.trim());
  params.append('timezoneOffsetMinutes', String(-new Date().getTimezoneOffset()));

  try {
    clockWeatherStatusEl.textContent = 'Saving...';
    const resp = await fetch('/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body: params.toString()
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || ('HTTP ' + resp.status));
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    syncBrightnessForm(data);
    syncClockWeatherForm(data);
    clockWeatherStatusEl.textContent = 'Saved';
  } catch (err) {
    clockWeatherStatusEl.textContent = 'Save error: ' + err.message;
  }
});

form.addEventListener('submit', async (event) => {
  event.preventDefault();

	  const values = {};
	  for (const [key, value] of new FormData(form).entries()) values[key] = String(value).trim();
	
	  if (!values.title && !values.body) {
	    statusEl.textContent = 'Title or body is required';
	    return;
	  }
	
	  const payload = {
	    version: 2,
	    source: {
	      appName: values.app || 'App',
	      bundleId: values.bundleId || '',
	      sender: values.sender || ''
	    },
	    content: {
	      title: values.title || '',
	      subtitle: values.subtitle || '',
	      body: values.body || '',
	      time: values.updatedAt || ''
	    },
	    style: {
	      accent: values.accent || '#55AAFF',
	      foreground: values.foreground || '#FFFFFF',
	      durationMs: Math.max(1, Math.min(30, Number(values.durationSeconds || 3))) * 1000
	    }
	  };
	
	  try {
	    statusEl.textContent = 'Uploading...';
	    const resp = await fetch('/notify', {
	      method: 'POST',
	      headers: {'Content-Type': 'application/json;charset=UTF-8'},
	      body: JSON.stringify(payload)
	    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || ('HTTP ' + resp.status));
    stateEl.textContent = JSON.stringify(data, null, 2);
    syncForm(data);
    syncBrightnessForm(data);
    syncClockWeatherForm(data);
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
    server.send_P(200, "text/html", NOTIFY_HTML);
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

void handleSettingsUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"wifi_not_connected\"}");
    return;
  }

  BacklightSettings nextSettings = backlightSettings;
  ClockSettings nextClockSettings = clockSettings;
  WeatherSettings nextWeatherSettings = weatherSettings;

  if (server.hasArg("brightnessMode")) {
    String mode = server.arg("brightnessMode");
    mode.toLowerCase();
    if (mode == "manual") {
      nextSettings.mode = BRIGHTNESS_MODE_MANUAL;
    } else if (mode == "schedule") {
      nextSettings.mode = BRIGHTNESS_MODE_SCHEDULE;
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid_brightness_mode\"}");
      return;
    }
  }

  int parsedValue = 0;
  if (server.hasArg("manualBrightness")) {
    if (!parseIntClamped(server.arg("manualBrightness"), 0, 100, parsedValue)) {
      server.send(400, "application/json", "{\"error\":\"invalid_manual_brightness\"}");
      return;
    }
    nextSettings.manualBrightness = parsedValue;
  }

  if (server.hasArg("dayBrightness")) {
    if (!parseIntClamped(server.arg("dayBrightness"), 0, 100, parsedValue)) {
      server.send(400, "application/json", "{\"error\":\"invalid_day_brightness\"}");
      return;
    }
    nextSettings.dayBrightness = parsedValue;
  }

  if (server.hasArg("nightBrightness")) {
    if (!parseIntClamped(server.arg("nightBrightness"), 0, 100, parsedValue)) {
      server.send(400, "application/json", "{\"error\":\"invalid_night_brightness\"}");
      return;
    }
    nextSettings.nightBrightness = parsedValue;
  }

  if (server.hasArg("timezoneOffsetMinutes")) {
    if (!parseIntClamped(server.arg("timezoneOffsetMinutes"), -720, 840, parsedValue)) {
      server.send(400, "application/json", "{\"error\":\"invalid_timezone_offset\"}");
      return;
    }
    nextSettings.timezoneOffsetMinutes = parsedValue;
  }

  int parsedHour = 0;
  int parsedMinute = 0;
  if (server.hasArg("dayStart")) {
    if (!parseClockTime(server.arg("dayStart"), parsedHour, parsedMinute)) {
      server.send(400, "application/json", "{\"error\":\"invalid_day_start\"}");
      return;
    }
    nextSettings.dayStartHour = parsedHour;
    nextSettings.dayStartMinute = parsedMinute;
  }

  if (server.hasArg("nightStart")) {
    if (!parseClockTime(server.arg("nightStart"), parsedHour, parsedMinute)) {
      server.send(400, "application/json", "{\"error\":\"invalid_night_start\"}");
      return;
    }
    nextSettings.nightStartHour = parsedHour;
    nextSettings.nightStartMinute = parsedMinute;
  }

  if (server.hasArg("clockFormat")) {
    String format = server.arg("clockFormat");
    format.toLowerCase();
    if (format == "24h") {
      nextClockSettings.formatHours = CLOCK_FORMAT_24H;
    } else if (format == "12h") {
      nextClockSettings.formatHours = CLOCK_FORMAT_12H;
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid_clock_format\"}");
      return;
    }
  }

  if (server.hasArg("weatherEnabled")) {
    String enabled = server.arg("weatherEnabled");
    enabled.toLowerCase();
    if (enabled == "1" || enabled == "true" || enabled == "on" || enabled == "enabled") {
      nextWeatherSettings.enabled = 1;
    } else if (enabled == "0" || enabled == "false" || enabled == "off" || enabled == "disabled") {
      nextWeatherSettings.enabled = 0;
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid_weather_enabled\"}");
      return;
    }
  }

  if (server.hasArg("weatherRefreshMinutes")) {
    if (!parseIntClamped(server.arg("weatherRefreshMinutes"), MIN_WEATHER_REFRESH_MIN, MAX_WEATHER_REFRESH_MIN, parsedValue)) {
      server.send(400, "application/json", "{\"error\":\"invalid_weather_refresh_minutes\"}");
      return;
    }
    nextWeatherSettings.refreshMinutes = (uint16_t)parsedValue;
  }

  if (server.hasArg("weatherToken")) {
    copyToBuffer(nextWeatherSettings.apiToken, sizeof(nextWeatherSettings.apiToken), server.arg("weatherToken"));
  }

  if (server.hasArg("weatherCity")) {
    String nextCity = server.arg("weatherCity");
    nextCity.trim();

    if (nextCity.length() == 0) {
      nextWeatherSettings.city[0] = 0;
      nextWeatherSettings.latitudeE4 = 0;
      nextWeatherSettings.longitudeE4 = 0;
    } else {
      WeatherSettings resolvedWeather = nextWeatherSettings;
      String geocodeError = "";
      if (!geocodeWeatherCity(nextCity, resolvedWeather, geocodeError)) {
        server.send(400, "application/json", String("{\"error\":\"") + geocodeError + "\"}");
        return;
      }
      nextWeatherSettings = resolvedWeather;
    }
  }

  normalizeBacklightSettingsValue(nextSettings);
  normalizeWeatherSettingsValue(nextWeatherSettings);

  if (nextWeatherSettings.enabled && !((nextWeatherSettings.city[0] != 0) && !(nextWeatherSettings.latitudeE4 == 0 && nextWeatherSettings.longitudeE4 == 0))) {
    server.send(400, "application/json", "{\"error\":\"weather_city_required\"}");
    return;
  }

  backlightSettings = nextSettings;
  clockSettings = nextClockSettings;
  weatherSettings = nextWeatherSettings;
  saveBacklightSettings();
  saveClockSettings();
  saveWeatherSettings();
  refreshBacklight(true);
  markWeatherNeedsRefresh();
  if (weatherSettings.enabled) {
    refreshWeather(true);
  } else {
    resetWeatherRuntime("Weather disabled");
  }
  invalidateDashboardCache();
  if (!hasNotification)
    renderDashboard();

  server.send(200, "application/json", buildStateJson());
}

void handleWeatherRefresh() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"wifi_not_connected\"}");
    return;
  }

  if (!weatherSettings.enabled) {
    server.send(400, "application/json", "{\"error\":\"weather_disabled\"}");
    return;
  }

  markWeatherNeedsRefresh();
  refreshWeather(true);
  server.send(200, "application/json", buildStateJson());
}

String jsonUnescapeString(const String& encoded) {
  String out = "";
  out.reserve(encoded.length());
  for (size_t i = 0; i < encoded.length(); i++) {
    char c = encoded[i];
    if (c != '\\' || i + 1 >= encoded.length()) {
      out += c;
      continue;
    }

    char esc = encoded[++i];
    if (esc == '"' || esc == '\\' || esc == '/') {
      out += esc;
    } else if (esc == 'n') {
      out += '\n';
    } else if (esc == 'r') {
      out += '\r';
    } else if (esc == 't') {
      out += '\t';
    }
  }
  return out;
}

bool jsonStringValue(const String& json, const char* key, String& out) {
  String needle = String("\"") + key + "\"";
  int keyPos = json.indexOf(needle);
  if (keyPos < 0)
    return false;

  int colonPos = json.indexOf(':', keyPos + needle.length());
  if (colonPos < 0)
    return false;

  int quotePos = colonPos + 1;
  while (quotePos < (int)json.length() && isspace((unsigned char)json[quotePos]))
    quotePos++;
  if (quotePos >= (int)json.length() || json[quotePos] != '"')
    return false;

  String encoded = "";
  bool escaped = false;
  for (int i = quotePos + 1; i < (int)json.length(); i++) {
    char c = json[i];
    if (escaped) {
      encoded += '\\';
      encoded += c;
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      out = jsonUnescapeString(encoded);
      return true;
    }
    encoded += c;
  }
  return false;
}

bool jsonUnsignedLongValue(const String& json, const char* key, unsigned long& out) {
  String needle = String("\"") + key + "\"";
  int keyPos = json.indexOf(needle);
  if (keyPos < 0)
    return false;

  int colonPos = json.indexOf(':', keyPos + needle.length());
  if (colonPos < 0)
    return false;

  int valuePos = colonPos + 1;
  while (valuePos < (int)json.length() && isspace((unsigned char)json[valuePos]))
    valuePos++;

  unsigned long value = 0;
  bool hasDigit = false;
  for (int i = valuePos; i < (int)json.length(); i++) {
    char c = json[i];
    if (c < '0' || c > '9')
      break;
    hasDigit = true;
    value = value * 10UL + (unsigned long)(c - '0');
  }

  if (!hasDigit)
    return false;
  out = value;
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseCssColor565(const String& raw, uint16_t& out) {
  String value = raw;
  value.trim();
  if (value.startsWith("#"))
    value.remove(0, 1);
  if (value.length() != 6)
    return false;

  int digits[6];
  for (int i = 0; i < 6; i++) {
    digits[i] = hexNibble(value[i]);
    if (digits[i] < 0)
      return false;
  }

  uint8_t r = (digits[0] << 4) | digits[1];
  uint8_t g = (digits[2] << 4) | digits[3];
  uint8_t b = (digits[4] << 4) | digits[5];
  out = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return true;
}

void handleNotifyUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"wifi_not_connected\"}");
    return;
  }

  String json = server.hasArg("plain") ? server.arg("plain") : String("");
  json.trim();
  if (json.length() == 0 || json[0] != '{') {
    server.send(400, "application/json", "{\"error\":\"json_body_required\"}");
    return;
  }

  String nextApp = "";
  String nextBundleId = "";
  String nextSender = "";
  String nextTitle = "";
  String nextSubtitle = "";
  String nextBody = "";
  String nextUpdatedAt = "";
  String nextAccent = "";
  String nextForeground = "";
  unsigned long nextDurationMs = NOTIFICATION_DEFAULT_VISIBLE_MS;

  jsonStringValue(json, "appName", nextApp);
  jsonStringValue(json, "bundleId", nextBundleId);
  jsonStringValue(json, "sender", nextSender);
  jsonStringValue(json, "title", nextTitle);
  jsonStringValue(json, "subtitle", nextSubtitle);
  jsonStringValue(json, "body", nextBody);
  jsonStringValue(json, "time", nextUpdatedAt);
  jsonStringValue(json, "accent", nextAccent);
  jsonStringValue(json, "foreground", nextForeground);
  jsonUnsignedLongValue(json, "durationMs", nextDurationMs);

  nextTitle.trim();
  nextBody.trim();
  if (nextTitle.length() == 0 && nextBody.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"title_or_body_required\"}");
    return;
  }

  if (nextApp.length() == 0)
    nextApp = "App";

  uint16_t nextAccentColor = accentForApp(nextApp.c_str());
  uint16_t nextForegroundColor = COLOR_TEXT;
  if (nextAccent.length() > 0 && !parseCssColor565(nextAccent, nextAccentColor)) {
    server.send(400, "application/json", "{\"error\":\"invalid_accent_color\"}");
    return;
  }
  if (nextForeground.length() > 0 && !parseCssColor565(nextForeground, nextForegroundColor)) {
    server.send(400, "application/json", "{\"error\":\"invalid_foreground_color\"}");
    return;
  }
  if (nextDurationMs < NOTIFICATION_MIN_VISIBLE_MS)
    nextDurationMs = NOTIFICATION_MIN_VISIBLE_MS;
  if (nextDurationMs > NOTIFICATION_MAX_VISIBLE_MS)
    nextDurationMs = NOTIFICATION_MAX_VISIBLE_MS;

  copyToBuffer(currentNotification.app, sizeof(currentNotification.app), nextApp);
  copyToBuffer(currentNotification.bundleId, sizeof(currentNotification.bundleId), nextBundleId);
  copyToBuffer(currentNotification.sender, sizeof(currentNotification.sender), nextSender);
  copyToBuffer(currentNotification.title, sizeof(currentNotification.title), nextTitle);
  copyToBuffer(currentNotification.subtitle, sizeof(currentNotification.subtitle), nextSubtitle);
  copyToBuffer(currentNotification.body, sizeof(currentNotification.body), nextBody);
  copyToBuffer(updatedAt, sizeof(updatedAt), nextUpdatedAt);
  currentNotification.accentColor = nextAccentColor;
  currentNotification.foregroundColor = nextForegroundColor;
  currentNotificationVisibleMs = nextDurationMs;

  hasNotification = true;
  updateCounter++;
  startNotificationEnterAnimation();
  renderDashboard();

  server.send(200, "application/json", buildStateJson());
}

void handleClear() {
  clearNotification();
  resetNotificationAnimation();
  invalidateDashboardCache();
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
    configureClock();
    markWeatherNeedsRefresh();
    refreshWeather(true);
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
  server.on("/settings", HTTP_POST, handleSettingsUpdate);
  server.on("/weather/refresh", HTTP_POST, handleWeatherRefresh);
  server.on("/notify", HTTP_POST, handleNotifyUpdate);
  server.on("/clear", HTTP_POST, handleClear);
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
  loadBacklightSettings();
  loadClockSettings();
  loadWeatherSettings();
  pinMode(TFT_BL, OUTPUT);
  analogWriteRange(255);
  analogWriteFreq(4000);
  refreshBacklight(true);

  tft.init();
  tft.setRotation(0);
  initSmoothFonts();
  clearNotification();
  resetNotificationAnimation();
  resetWeatherRuntime(weatherSettings.enabled ? "Waiting for weather sync" : "Weather disabled");
  renderWaitingState();

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
      configureClock();
      markWeatherNeedsRefresh();
      refreshWeather(true);
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
  if (now - lastBacklightRefreshMs >= BACKLIGHT_REFRESH_MS) {
    lastBacklightRefreshMs = now;
    refreshBacklight();
  }

  updateNotificationAnimation();

  if (!hasNotification && now - lastIdleClockRefreshMs >= IDLE_CLOCK_REFRESH_MS) {
    lastIdleClockRefreshMs = now;
    renderDashboard();
  }

  refreshWeather(false);
}
