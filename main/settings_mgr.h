#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char timezone[32];
    uint8_t img_provider;        // 0: Bing, 1: Picsum, 2: Custom
    uint16_t img_interval_min;   // 5, 15, 30, 60, 1440
    char custom_img_url[192];
    uint8_t font_mode;           // 0: Random, 1: Modern Sans, 2: Bold Display, 3: Elegant Serif, 4: Retro Mono
    uint8_t brightness;          // 0%, 25%, 100% loop
} app_settings_t;

void settings_mgr_init(void);
const app_settings_t *settings_mgr_get(void);
void settings_mgr_save(const app_settings_t *settings);
void settings_mgr_set_wifi(const char *ssid, const char *pass);
void settings_mgr_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
