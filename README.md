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

### 2. Generate local TLS certificate (camera sketch)

`esp_mini_screen_camera` uses HTTPS for camera access and expects a local file `esp_mini_screen_camera/tls_local.h` with your private key.

Generate it once after cloning:

```bash
bash setup.sh
```

This creates a unique self-signed cert/key pair for your machine. The generated file is ignored by Git.
If you only want to regenerate TLS, run:

```bash
bash esp_mini_screen_camera/generate_tls_cert.sh
```

Template file: `esp_mini_screen_camera/tls_local.h.example`.

### 3. Select the board

In Arduino IDE:
- **Tools -> Board** -> **Generic ESP8266 Module**
- **Tools -> Port** -> select the CH340 serial port

### 4. Upload

Open any sketch `.ino` file from the sketches below in Arduino IDE and click **Upload**.

## Sketches

### esp_mini_screen_colors

Display test sketch. Cycles through 8 colors (red, green, blue, yellow, cyan, magenta, white, orange) every 2 seconds, filling the entire screen and showing the color name in the center. Useful for verifying that the display and `User_Setup.h` are configured correctly.

### esp_mini_screen_wifi

WiFi provisioning sketch with a captive web portal. On first boot (or when the saved network is unavailable) the device starts as a WiFi access point:

- **AP name:** `MiniScreen-Setup`
- **AP password:** `12345678`
- **Config URL:** `http://192.168.4.1`

All connection details are shown on the TFT screen. After connecting to the AP and opening the URL in a browser you get a page that lists nearby WiFi networks — tap one, enter the password, and hit Connect. Credentials are saved to EEPROM so the device reconnects automatically on reboot. Once connected, the screen displays the assigned IP address, SSID, and signal strength.

### esp_mini_screen_camera

Streams your phone's camera to the TFT display over WiFi. Uses HTTPS with a self-signed certificate so the browser grants camera access. Includes built-in WiFi provisioning (same AP flow as above — credentials are shared via EEPROM).

If compilation fails with `Missing tls_local.h`, run:
```bash
bash setup.sh
```

**How it works:**
1. Device connects to WiFi (or starts AP for setup) and shows the HTTPS URL on screen
2. Open `https://<device-ip>` in a phone browser and **accept the self-signed certificate warning**
3. Select front/back camera, target FPS, resolution profile, and (optional) smoothing mode, then tap **Start**
4. The browser captures video, downsamples it (30x30 / 40x40 / 48x48 / 60x60 / 80x80), converts to RGB565, and POSTs frames over HTTPS
5. The ESP scales each pixel to fill 240x240 (8x / 6x / 5x / 4x / 3x)

**Expected performance:** target up to 10 FPS in good WiFi conditions (HTTPS encryption still adds overhead on ESP8266). Lower resolutions are usually faster. For 60x60 mode, FPS is capped to 8; for 80x80 mode, FPS is capped to 5 for stability. Smoothing improves visual quality but can slightly reduce FPS (capped to 5 FPS when enabled), and is automatically limited to profiles up to 48x48 for stability on ESP8266 (disabled by default).

**Browser note:** When you first open the URL, the browser will show a security warning because the certificate is self-signed. Tap **Advanced → Proceed** (Chrome) or **Accept the Risk** (Firefox) to continue. This is expected and safe on your local network.

## Credits

Based on the reverse-engineering work shared on [Reddit](https://www.reddit.com/r/hardwarehacking/comments/1rbsapl/esp12f_based_smart_wifi_weather_station_hack/) by the community, including pinout discovery and CH340C mod.
