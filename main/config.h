#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// CYD 2.8" ESP32-2432S028 Pin Configuration
#define LCD_HOST                SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ      (40 * 1000 * 1000)
#define LCD_PIN_NUM_SCLK        14
#define LCD_PIN_NUM_MOSI        13
#define LCD_PIN_NUM_MISO        12
#define LCD_PIN_NUM_DC          2
#define LCD_PIN_NUM_CS          15
#define LCD_PIN_NUM_RST         -1
#define LCD_PIN_NUM_BK_LIGHT    21

// Display dimensions (Landscape)
#define LCD_H_RES               320
#define LCD_V_RES               240

// CYD 2.8" ESP32-2432S028 Touch (XPT2046) Configuration
#define TOUCH_SPI_HOST          SPI3_HOST
#define TOUCH_PIN_NUM_CLK       25
#define TOUCH_PIN_NUM_MOSI      32
#define TOUCH_PIN_NUM_MISO      39
#define TOUCH_PIN_NUM_CS        33
#define TOUCH_PIN_NUM_IRQ       36

// CYD Onboard RGB LED (Active LOW)
#define LED_PIN_RED             4
#define LED_PIN_GREEN           16
#define LED_PIN_BLUE            17

// SoftAP Configuration
#define PORTAL_AP_SSID          "CYD-WordClock-Setup"
#define PORTAL_AP_PASS          ""          // Open AP for initial configuration
#define PORTAL_AP_CHANNEL       1
#define PORTAL_MAX_CONN         4
#define PORTAL_IP_ADDR          "192.168.4.1"

// Image Storage Path (SPIFFS)
#define SPIFFS_MOUNT_POINT      "/spiffs"
#define IMAGE_JPG_PATH          "/spiffs/bg.jpg"
#define IMAGE_BIN_PATH          "/spiffs/bg.bin"
#define LVGL_BIN_IMAGE_PATH     "S:/bg.bin"

// Default Settings
#define DEFAULT_TIMEZONE        "JST-9"
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#define DEFAULT_IMG_PROVIDER    0           // 0: Bing, 1: Picsum, 2: Custom
#define DEFAULT_IMG_INTERVAL_MIN 15          // Refresh image every 15 min
#define DEFAULT_CUSTOM_IMG_URL  "https://picsum.photos/320/240.jpg"
#define DEFAULT_FONT_MODE       0           // 0: Random cycle, 1: Modern Sans, 2: Bold Display, 3: Elegant Serif, 4: Retro Mono
#define DEFAULT_BRIGHTNESS      100         // 0%, 25%, 100% loop

#ifdef __cplusplus
}
#endif
