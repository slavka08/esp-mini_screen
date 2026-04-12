# esp-mini_screen

Custom firmware for the cheap ESP12F-based smart WiFi weather station (~$20 on Amazon) with a 240x240 ST7789 TFT display.

## Hardware

- ESP12F (ESP8266) module
- ST7789V 240x240 TFT display (SPI)
- CH340C soldered to the U3 socket on the back of the board for USB-C programming

### Display Pin Mapping

| Signal   | GPIO | Notes              |
|----------|------|--------------------|
| TFT_MOSI | 13   | HSPI               |
| TFT_SCLK | 14   | HSPI               |
| TFT_CS   | -1   | Tied to GND on PCB |
| TFT_DC   | 0    |                    |
| TFT_RST  | 2    |                    |
| TFT_BL   | 5    | Active LOW         |

## Prerequisites

1. **Arduino IDE** (1.8.x or 2.x)
2. **ESP8266 board support** — in Arduino IDE go to **File -> Preferences**, add this URL to *Additional Board Manager URLs*:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
   Then go to **Tools -> Board -> Boards Manager**, search for **esp8266** and install it.
3. **TFT_eSPI library** — go to **Sketch -> Include Library -> Manage Libraries**, search for **TFT_eSPI** by Bodmer and install it.

## Setup

### 1. Configure TFT_eSPI

The ST7789 display will **not** work with default library settings. You must replace the `User_Setup.h` file inside the TFT_eSPI library folder with the one provided in this repository.

Find the library folder (typical paths):

| OS      | Path                                                        |
|---------|-------------------------------------------------------------|
| macOS   | `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`      |
| Windows | `C:\Users\<you>\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h` |
| Linux   | `~/Arduino/libraries/TFT_eSPI/User_Setup.h`                |

Copy `User_Setup.h` from this repo root and replace the original file.

### 2. Select the board

In Arduino IDE:
- **Tools -> Board** -> **Generic ESP8266 Module**
- **Tools -> Port** -> select the CH340 serial port

### 3. Upload

Open `esp_mini_screen_colors/esp_mini_screen_colors.ino` in Arduino IDE and click **Upload**.

## What the demo sketch does

Cycles through 8 colors (red, green, blue, yellow, cyan, magenta, white, orange) every 2 seconds, filling the entire screen and displaying the color name in the center.

## Credits

Based on the reverse-engineering work shared on [Reddit](https://www.reddit.com/r/hardwarehacking/comments/1rbsapl/esp12f_based_smart_wifi_weather_station_hack/) by the community, including pinout discovery and CH340C mod.
