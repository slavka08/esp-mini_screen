#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// Массив цветов для заливки экрана
const uint16_t colors[] = {
  TFT_RED,
  TFT_GREEN,
  TFT_BLUE,
  TFT_YELLOW,
  TFT_CYAN,
  TFT_MAGENTA,
  TFT_WHITE,
  TFT_ORANGE
};

const char* colorNames[] = {
  "RED",
  "GREEN",
  "BLUE",
  "YELLOW",
  "CYAN",
  "MAGENTA",
  "WHITE",
  "ORANGE"
};

const int colorCount = sizeof(colors) / sizeof(colors[0]);

void setup() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  for (int i = 0; i < colorCount; i++) {
    // Заливаем экран цветом
    tft.fillScreen(colors[i]);

    // Выводим название цвета по центру
    tft.setTextColor(TFT_BLACK, colors[i]);
    tft.setTextSize(3);

    // Центрируем текст
    int16_t tw = tft.textWidth(colorNames[i]);
    int16_t x = (240 - tw) / 2;
    int16_t y = 110;

    tft.setCursor(x, y);
    tft.print(colorNames[i]);

    delay(2000);
  }
}
