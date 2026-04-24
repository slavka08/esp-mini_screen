// User_Setup.h для ESP12F метеостанции с экраном ST7789 240x240
// Этот файл нужно скопировать в папку библиотеки TFT_eSPI,
// заменив оригинальный User_Setup.h

#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ESP8266 HSPI пины
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   -1  // CS привязан к GND на плате
#define TFT_DC    0
#define TFT_RST   2
#define TFT_BL    5

#define TFT_BACKLIGHT_ON LOW

#define LOAD_GLCD
#define SMOOTH_FONT

#define SPI_FREQUENCY 20000000
