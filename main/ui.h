#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_update_time(const struct tm *timeinfo);
void ui_set_status(const char *status);
void ui_reload_background(void);
void ui_next_font(void);
void ui_show_info_overlay(const char *ip_str, const char *ssid_str);
void ui_hide_info_overlay(void);
bool ui_is_overlay_visible(void);
void ui_show_status(void);
void ui_hide_status(void);

#ifdef __cplusplus
}
#endif
