#include "ui.h"
#include "ui_fonts.h"
#include "word_clock.h"
#include "config.h"
#include "settings_mgr.h"
#include "image_fetcher.h"
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_bg_img = NULL;

// Text Outline / Shadow (8 directions for crisp stroke outline)
#define OUTLINE_COUNT 8
static lv_obj_t *s_outline_labels[OUTLINE_COUNT];
static lv_obj_t *s_time_label = NULL;

static const int8_t OUTLINE_OFFSETS[OUTLINE_COUNT][2] = {
    {-2, -2}, { 0, -2}, { 2, -2},
    {-2,  0},           { 2,  0},
    {-2,  2}, { 0,  2}, { 2,  2}
};

static lv_obj_t *s_status_shadow = NULL;
static lv_obj_t *s_status_label = NULL;

// Info Overlay (shown on touch)
static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_overlay_label = NULL;
static bool s_overlay_visible = false;
static lv_timer_t *s_overlay_timer = NULL;
static lv_timer_t *s_status_timer = NULL;

static font_style_t s_font_style = FONT_STYLE_MODERN_SANS;
static int s_last_hour = -1;
static int s_last_min = -1;
static int s_last_block_min = -1;

static void auto_hide_overlay_cb(lv_timer_t *timer)
{
    ESP_LOGI(TAG, "Auto-dismissing info overlay after timeout");
    ui_hide_info_overlay();
}

static void auto_hide_status_cb(lv_timer_t *timer)
{
    ESP_LOGI(TAG, "Auto-hiding bottom IP address status after 1 minute");
    if (s_status_label) lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
    if (s_status_shadow) lv_obj_add_flag(s_status_shadow, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(void)
{
    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0f172a), 0);

    // Background Image widget (LVGL handles streaming lines from bg.bin on Flash)
    s_bg_img = lv_img_create(s_scr);
    lv_obj_set_size(s_bg_img, LCD_H_RES, LCD_V_RES);
    lv_obj_align(s_bg_img, LV_ALIGN_CENTER, 0, 0);

    const app_settings_t *s = settings_mgr_get();
    if (s->font_mode >= 1 && s->font_mode <= FONT_STYLE_COUNT) {
        s_font_style = (font_style_t)(s->font_mode - 1);
    } else {
        s_font_style = FONT_STYLE_MODERN_SANS;
    }
    const lv_font_t *font = ui_fonts_get(s_font_style);

    // 1. Black Outline Labels (8-directional stroke outline)
    for (int i = 0; i < OUTLINE_COUNT; i++) {
        s_outline_labels[i] = lv_label_create(s_scr);
        lv_obj_set_width(s_outline_labels[i], LCD_H_RES - 24);
        lv_obj_align(s_outline_labels[i], LV_ALIGN_CENTER, OUTLINE_OFFSETS[i][0], OUTLINE_OFFSETS[i][1]);
        lv_obj_set_style_text_align(s_outline_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_outline_labels[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(s_outline_labels[i], font, 0);
        lv_label_set_text(s_outline_labels[i], "It's\ntwelve o'clock.");
    }

    // 2. Foreground Pure White Label
    s_time_label = lv_label_create(s_scr);
    lv_obj_set_width(s_time_label, LCD_H_RES - 24);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_time_label, font, 0);
    lv_label_set_text(s_time_label, "It's\ntwelve o'clock.");

    // Status Label with black shadow
    s_status_shadow = lv_label_create(s_scr);
    lv_obj_align(s_status_shadow, LV_ALIGN_BOTTOM_MID, 1, -3);
    lv_obj_set_style_text_color(s_status_shadow, lv_color_black(), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_12) && CONFIG_LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(s_status_shadow, &lv_font_montserrat_12, 0);
#endif
    lv_label_set_text(s_status_shadow, "Connecting to Wi-Fi...");

    s_status_label = lv_label_create(s_scr);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xf1f5f9), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_12) && CONFIG_LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_12, 0);
#endif
    lv_label_set_text(s_status_label, "Connecting to Wi-Fi...");

    // Info Overlay for touch
    s_overlay = lv_obj_create(s_scr);
    lv_obj_set_size(s_overlay, 300, 200);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x18181b), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_border_width(s_overlay, 2, 0);
    lv_obj_set_style_radius(s_overlay, 12, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    s_overlay_label = lv_label_create(s_overlay);
    lv_obj_set_width(s_overlay_label, 270);
    lv_obj_align(s_overlay_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(s_overlay_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_overlay_label, LV_TEXT_ALIGN_CENTER, 0);

    // Auto-hide bottom IP address status bar after 1 minute (60,000 ms) from boot
    s_status_timer = lv_timer_create(auto_hide_status_cb, 60000, NULL);
    lv_timer_set_repeat_count(s_status_timer, 1);

    ui_reload_background();
}

void ui_reload_background(void)
{
    if (image_fetcher_has_valid_image()) {
        ESP_LOGI(TAG, "Loading background image from %s", LVGL_BIN_IMAGE_PATH);
        lv_img_set_src(s_bg_img, LVGL_BIN_IMAGE_PATH);
        lv_obj_clear_flag(s_bg_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_bg_img);
    } else {
        ESP_LOGI(TAG, "No valid background image yet, using dark theme fallback");
        lv_obj_add_flag(s_bg_img, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_next_font(void)
{
    s_font_style = (font_style_t)((s_font_style + 1) % FONT_STYLE_COUNT);
    ESP_LOGI(TAG, "Switched font style to %s", ui_fonts_get_name(s_font_style));
    const lv_font_t *font = ui_fonts_get(s_font_style);
    lv_obj_set_style_text_font(s_time_label, font, 0);
    for (int i = 0; i < OUTLINE_COUNT; i++) {
        lv_obj_set_style_text_font(s_outline_labels[i], font, 0);
    }
}

void ui_update_time(const struct tm *t)
{
    if (!t) return;

    // Update whenever hour or minute changes (number of trailing periods changes every minute)
    if (s_last_hour != t->tm_hour || s_last_min != t->tm_min) {
        s_last_hour = t->tm_hour;
        s_last_min = t->tm_min;

        char buf[64];
        word_clock_get_text(t->tm_hour, t->tm_min, buf, sizeof(buf));

        // Update foreground and all outline shadow labels
        lv_label_set_text(s_time_label, buf);
        for (int i = 0; i < OUTLINE_COUNT; i++) {
            lv_label_set_text(s_outline_labels[i], buf);
        }

        // Cycle font style on each 5-minute block change (if in random cycle mode)
        int block_min = (t->tm_min / 5) * 5;
        if (s_last_block_min != block_min) {
            s_last_block_min = block_min;
            const app_settings_t *s = settings_mgr_get();
            if (s->font_mode == 0) {
                ui_next_font();
            }
        }
    }
}

void ui_set_status(const char *status)
{
    if (status) {
        if (s_status_shadow) lv_label_set_text(s_status_shadow, status);
        if (s_status_label) lv_label_set_text(s_status_label, status);
    }
}

void ui_show_status(void)
{
    if (s_status_label) lv_obj_clear_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
    if (s_status_shadow) lv_obj_clear_flag(s_status_shadow, LV_OBJ_FLAG_HIDDEN);

    if (s_status_timer) {
        lv_timer_reset(s_status_timer);
        lv_timer_resume(s_status_timer);
    } else {
        s_status_timer = lv_timer_create(auto_hide_status_cb, 60000, NULL);
        lv_timer_set_repeat_count(s_status_timer, 1);
    }
}

void ui_hide_status(void)
{
    if (s_status_label) lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
    if (s_status_shadow) lv_obj_add_flag(s_status_shadow, LV_OBJ_FLAG_HIDDEN);
    if (s_status_timer) {
        lv_timer_pause(s_status_timer);
    }
}

void ui_show_info_overlay(const char *ip_str, const char *ssid_str)
{
    if (!s_overlay) return;
    char text[256];
    snprintf(text, sizeof(text),
             "#38bdf8 CYD Word Clock#\n\n"
             "Wi-Fi: %s\n"
             "IP: %s\n\n"
             "#a1a1aa Tap screen to dismiss#",
             ssid_str ? ssid_str : "None",
             ip_str ? ip_str : "192.168.4.1");

    lv_label_set_recolor(s_overlay_label, true);
    lv_label_set_text(s_overlay_label, text);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_overlay_visible = true;

    // Reset or start 5-second auto-dismiss timer
    if (s_overlay_timer) {
        lv_timer_reset(s_overlay_timer);
    } else {
        s_overlay_timer = lv_timer_create(auto_hide_overlay_cb, 5000, NULL);
        lv_timer_set_repeat_count(s_overlay_timer, 1);
    }
}

void ui_hide_info_overlay(void)
{
    if (s_overlay) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        s_overlay_visible = false;
    }
    if (s_overlay_timer) {
        lv_timer_del(s_overlay_timer);
        s_overlay_timer = NULL;
    }
}

bool ui_is_overlay_visible(void)
{
    return s_overlay_visible;
}
