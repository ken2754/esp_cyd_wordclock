# ESP32 CYD 2.8" Colloquial English Word Clock

A sophisticated, low-memory colloquial English Word Clock firmware for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028)** 2.8" TFT with resistive touch. Built from the ground up using **ESP-IDF v5.3+** and **LVGL 8.3**, requiring **no external PSRAM or SD card**.

---

## Highlights & Features

- **Colloquial English Phrasing**: Converts time into natural, conversational English updated in 5-minute blocks (e.g., *"It's ten past ten"*, *"It's a quarter to eight"*, *"It's half past two"*).
- **Subtle Minute Remainder via Trailing Periods**: Rather than cluttered corner LEDs or dots, the remainder minutes (0 to 4) are subtly indicated by the number of trailing periods at the end of the sentence:
  - **Remainder 0** (e.g. 10:00, 10:05): No period (`It's\nten o'clock`)
  - **Remainder 1** (e.g. 10:01, 10:06): 1 period (`It's\nten o'clock.`)
  - **Remainder 2** (e.g. 10:02, 10:07): 2 periods (`It's\nten o'clock..`)
  - **Remainder 3** (e.g. 10:03, 10:08): 3 periods (`It's\nten o'clock...`)
  - **Remainder 4** (e.g. 10:04, 10:09): 4 periods (`It's\nten o'clock....`)
- **Daily Internet Wallpaper**: Automatically downloads scenic wallpapers from Bing Daily Images, Picsum Photos, or custom URLs over HTTPS and caches them to internal SPIFFS flash.
- **Ultra-Low Memory Architecture (Zero PSRAM)**:
  - Custom streaming converter converts downloaded JPEG images directly to TrueColor RGB565 binaries (`bg.bin`) in ~5.2 seconds using sequential flash writes.
  - LVGL line-by-line decoder streams background images from Flash using only a 640-byte buffer, leaving >100KB of free internal SRAM.
- **8-Direction Stroke Outline Typography**: Pure white foreground text backed by an 8-directional crisp black stroke outline. Ensures perfect legibility against bright or busy photographic backgrounds without obscuring the scenery with opaque cards.
- **Multi-Font Engine**: Built-in typography options including *Modern Sans*, *Bold Display*, *Elegant Serif*, and *Retro Monospace*. Can rotate randomly every 5 minutes or stay fixed.
- **Smart Touch Interaction**:
  - **Top Zone ($y < 100\text{px}$)**: 3-stage brightness loop (**100% Bright $\rightarrow$ 0% Display Off $\rightarrow$ 25% Dim Night**). Tapping anywhere on the screen when off turns the display back on safely.
  - **Bottom Zone ($y \ge 140\text{px}$)**: Re-displays the Wi-Fi SSID and assigned IP address status bar (auto-dismisses after 1 minute).
  - **Middle Zone ($100 \le y < 140\text{px}$)**: Cycles through the typography fonts immediately.
- **Dual SoftAP & Station Web Portal**:
  - Automatically creates a SoftAP (`CYD-WordClock-Setup`, `192.168.4.1`) when Wi-Fi is unconfigured.
  - Keeps the HTTP Web Server running continuously on port 80 (`http://<device-ip>/`) when connected to your home Wi-Fi.
  - Clean web interface allows configuring Wi-Fi SSID/password, POSIX timezone, wallpaper provider, update interval, font style, and screen brightness.

---

## Hardware Compatibility

Designed specifically for the **ESP32-2432S028** (often called the "CYD" or Cheap Yellow Display, 2.8-inch TFT LCD with XPT2046 resistive touch, dual USB port version):

| Peripheral | Controller / Pin | Notes |
|---|---|---|
| **LCD Panel** | ST7789 / ILI9341 (Auto-detected) | `SPI2_HOST`, 320x240 Landscape, 40MHz |
| LCD SCLK / MOSI / MISO | GPIO 14 / 13 / 12 | Hardware SPI |
| LCD DC / CS / RST | GPIO 2 / 15 / -1 | Software reset |
| LCD Backlight (PWM) | GPIO 21 | LEDC Timer 0 (5kHz, 0-100% duty) |
| **Touch Controller** | XPT2046 | `SPI3_HOST`, 2MHz |
| Touch CLK / MOSI / MISO | GPIO 25 / 32 / 39 | Separate SPI bus |
| Touch CS / IRQ | GPIO 33 / 36 | Debounced polling with coordinates mapping |
| **RGB LED** | GPIO 4 (R) / 16 (G) / 17 (B) | Active LOW |

---

## Getting Started

### Prerequisites
- **ESP-IDF v5.3+** installed and activated in your terminal environment (`. $IDF_PATH/export.sh`).
- USB-to-UART drivers (CH340 or CP2102 depending on CYD revision).

### Building and Flashing

1. Clone this repository:
   ```bash
   git clone https://github.com/your-username/esp_wordclock.git
   cd esp_wordclock
   ```

2. Set target to ESP32:
   ```bash
   idf.py set-target esp32
   ```

3. Build the project:
   ```bash
   idf.py build
   ```

4. Connect your CYD via USB and flash:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

---

## Initial Configuration

1. **Power on the device**:
   If no Wi-Fi credentials have been saved yet, the clock starts up and displays `AP: CYD-WordClock-Setup (192.168.4.1)` at the bottom of the screen.
2. **Connect to SoftAP**:
   Using your phone or laptop, connect to the Wi-Fi network named **`CYD-WordClock-Setup`** (open network, no password required).
3. **Open Portal**:
   Navigate to `http://192.168.4.1` in your browser.
4. **Configure Settings**:
   - **Wi-Fi SSID & Password**: Enter your local 2.4GHz Wi-Fi credentials.
   - **Timezone**: Enter your POSIX timezone string (e.g., `JST-9` for Japan, `EST5EDT,M3.2.0,M11.1.0` for US Eastern, `UTC0` for UTC).
   - **Wallpaper Provider**: Choose between Bing Daily Image, Picsum Photos, or Custom URL.
   - **Refresh Interval**: Choose how often to fetch fresh wallpapers (from 5 minutes up to once a day).
   - **Font Style & Brightness**: Select your default typography and display brightness.
5. **Save and Reboot**:
   Click **"Save and Apply"**. The device will save your preferences to NVS flash and restart.
6. **Normal Operation**:
   Once connected, the clock synchronizes with NTP servers, downloads the latest wallpaper, and begins displaying the time. You can access the configuration portal anytime by visiting `http://<device-ip>/` from any device on your local network.

---

## Project Structure

```
esp_wordclock/
├── CMakeLists.txt              # Root CMake configuration
├── partitions.csv              # 4MB flash partition table (nvs, app, 1.2MB SPIFFS)
├── sdkconfig.defaults          # Default configurations (TWDT 20s, LVGL font flags)
├── main/
│   ├── CMakeLists.txt          # Main component CMake build script
│   ├── idf_component.yml       # Component dependencies (LVGL 8.3, ILI9341 driver)
│   ├── config.h                # Hardware pinouts, default parameters, paths
│   ├── display_driver.c/.h     # ST7789/ILI9341 auto-detection & PWM backlight
│   ├── touch_driver.c/.h       # XPT2046 touch driver with debouncing
│   ├── word_clock.c/.h         # Colloquial English time conversion & period counter
│   ├── ui_fonts.c/.h           # Typography engine (Sans, Display, Serif, Mono)
│   ├── ui.c/.h                 # LVGL interface with outline text & touch zones
│   ├── settings_mgr.c/.h       # NVS persistent configuration storage
│   ├── sntp_time.c/.h          # SNTP network time synchronization
│   ├── wifi_portal.c/.h        # APSTA Wi-Fi mode & Web configuration server
│   ├── image_fetcher.c/.h      # Asynchronous HTTPS fetcher & fast RGB565 converter
│   └── main.c                  # Application entry point & task coordination
└── README.md                   # Project documentation
```

---

## License

This project is licensed under the [MIT License](LICENSE).
