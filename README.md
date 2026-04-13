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

### esp_mini_screen_image

Uploads a single image from browser to TFT over WiFi. Includes built-in WiFi provisioning (same AP flow as above, credentials in EEPROM).

**How it works:**
1. Device connects to WiFi (or starts AP for setup) and shows the URL on screen
2. Open `http://<device-ip>` in browser
3. Select an image file or paste image from clipboard (Ctrl+V / Cmd+V)
4. Browser fits it to square `240x240` by the narrow side and centers/crops by the wide side
5. Browser converts the result to RGB565 and uploads it
6. ESP draws the received `240x240` frame line-by-line to the display

### esp_mini_screen_limits

Displays remaining daily and weekly limits for two services (`Codex` and `Claude`) and exposes a small HTTP API for updating the screen from another machine.

![AI Limits sketch on device](esp_mini_screen_limits/ai_limits_on_device.jpg)

**How it works:**
1. Device connects to WiFi (or starts AP for setup) and shows the dashboard on the TFT
2. Open `http://<device-ip>` in browser to use the built-in test form
3. Upload new values with `POST http://<device-ip>/limits`
4. Check the current device state with `GET http://<device-ip>/state`

`POST /limits` currently accepts `application/x-www-form-urlencoded` fields:

- `updatedAt`
- `codexDailyText`
- `codexDailyPercent`
- `codexWeeklyText`
- `codexWeeklyPercent`
- `claudeDailyText`
- `claudeDailyPercent`
- `claudeWeeklyText`
- `claudeWeeklyPercent`

You can send only the fields you want to update. Text is shown as-is on the display, and percent drives the progress bar (`0..100`).

Example:

```bash
curl -X POST "http://192.168.1.50/limits" \
  --data-urlencode "updatedAt=2026-04-13 22:15" \
  --data-urlencode "codexDailyText=72% left" \
  --data-urlencode "codexDailyPercent=72" \
  --data-urlencode "codexWeeklyText=58% left" \
  --data-urlencode "codexWeeklyPercent=58" \
  --data-urlencode "claudeDailyText=4h 10m left" \
  --data-urlencode "claudeDailyPercent=35" \
  --data-urlencode "claudeWeeklyText=2d 03h left" \
  --data-urlencode "claudeWeeklyPercent=61"
```

### esp_mini_screen_mac_stats

Displays Mac CPU usage per core and RAM usage on the TFT. CPU rows are split into `Performance` and `Efficiency` groups, and the device exposes a small HTTP API for updates from your Mac.

![Mac Monitor sketch on device](esp_mini_screen_mac_stats/mac_monitor_on_device.jpg)

You do **not** need a separate macOS app for this. The included sender script is enough unless you specifically want a menu-bar app or GUI settings.

**How it works:**
1. Device connects to WiFi (or starts AP for setup) and shows the dashboard on the TFT
2. From the repository root, run the sender on your Mac:
   ```bash
   python3 tools/send_mac_stats.py --device-url http://<device-ip> --interval 1
   ```
3. The script samples per-core CPU load through macOS Mach APIs and RAM usage through `host_statistics64`
4. On Apple Silicon it auto-detects `Performance` / `Efficiency` core counts via `sysctlbyname`
5. The ESP receives the update on `POST /stats` and renders RAM + per-core bars

Useful sender flags:

- `--performance-cores 4`
- `--efficiency-cores 6`
- `--cpu-order eff-first`
- `--max-visible-cores 12`
- `--once --dry-run`

`POST /stats` accepts `application/x-www-form-urlencoded` fields:

- `updatedAt`
- `memoryText`
- `memoryPercent`
- `performanceCount`
- `efficiencyCount`
- `coreLoads` - comma-separated percentages in screen order (`P...` first, then `E...`)

`performanceCount`, `efficiencyCount`, and `coreLoads` must be sent together in the same request. Memory fields can be updated independently.

Example:

```bash
curl -X POST "http://192.168.1.50/stats" \
  --data-urlencode "updatedAt=22:15:04" \
  --data-urlencode "memoryText=12.3/16.0 GB" \
  --data-urlencode "memoryPercent=77" \
  --data-urlencode "performanceCount=4" \
  --data-urlencode "efficiencyCount=6" \
  --data-urlencode "coreLoads=41,55,38,67,12,14,9,11,8,7"
```

### macos_ai_limits

Helpers for macOS that bridge local CLI/account state to `esp_mini_screen_limits`.
The current setup is manual only: nothing runs in `launchd`, and nothing is hooked into Claude automatically.

Files:

- `macos_ai_limits/ai_limits_sync.py` - parses local `Codex` and `Claude` limit data and builds the ESP payload
- `macos_ai_limits/run_sync.sh` - the single manual entrypoint to push current limits from Terminal
- `macos_ai_limits/config.example.json` - local config template

How data is collected:

- `Codex` - reads the newest `rate_limits.primary/secondary` entries from `~/.codex/sessions/**/*.jsonl`
- `Claude` - reads local Claude Desktop transcripts from `~/.claude/projects/**/*.jsonl`

Current limitation:

- `Claude` data is based on locally stored transcript text. If Claude only stores a generic message like `You've hit your limit · resets 6pm`, the script maps that to the weekly slot and leaves daily as `waiting`.

#### Setup on macOS

1. Create local config outside the repo:

```bash
mkdir -p ~/Library/Application\ Support/esp-mini-screen-ai-limits
cp ./macos_ai_limits/config.example.json \
  ~/Library/Application\ Support/esp-mini-screen-ai-limits/config.json
```

2. Edit `~/Library/Application Support/esp-mini-screen-ai-limits/config.json` and set the real ESP URL:

```json
{
  "esp_url": "http://192.168.1.50/limits",
  "codex_sessions_dir": "~/.codex/sessions",
  "claude_projects_dir": "~/.claude/projects",
  "timeout_seconds": 15,
  "retries": 3
}
```

3. Inspect the merged payload locally:

```bash
python3 ./macos_ai_limits/ai_limits_sync.py show
```

4. Send it to the device manually from Terminal:

```bash
./macos_ai_limits/run_sync.sh
```

For a one-off dry run without sending:

```bash
python3 ./macos_ai_limits/ai_limits_sync.py push --dry-run
```

## Credits

Based on the reverse-engineering work shared on [Reddit](https://www.reddit.com/r/hardwarehacking/comments/1rbsapl/esp12f_based_smart_wifi_weather_station_hack/) by the community, including pinout discovery and CH340C mod.
