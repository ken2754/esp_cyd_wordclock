#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lvgl.h"

#include "config.h"
#include "settings_mgr.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "ui.h"
#include "image_fetcher.h"
#include "wifi_portal.h"
#include "sntp_time.h"

static const char *TAG = "main";
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static bool s_time_synced = false;

static void on_touch(void)
{
    int tx = 0, ty = 0;
    touch_driver_get_xy(&tx, &ty);
    ESP_LOGI(TAG, "Touch event detected at (%d, %d)", tx, ty);

    // If screen backlight is completely off (0%), wake it up to 25% on any tap
    if (display_driver_get_brightness() == 0) {
        uint8_t next_b = display_driver_cycle_brightness();
        ESP_LOGI(TAG, "Screen was off -> Woke up backlight to %d%%", next_b);
        return;
    }

    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (ui_is_overlay_visible()) {
            ESP_LOGI(TAG, "Overlay active -> Dismissing overlay");
            ui_hide_info_overlay();
        } else if (ty < 100) {
            // Top 100 pixels: cycle brightness (0% -> 25% -> 100% loop)
            uint8_t next_b = display_driver_cycle_brightness();
            ESP_LOGI(TAG, "Top area tapped (y=%d < 100) -> Cycled brightness to %d%%", ty, next_b);
        } else if (ty >= 140) {
            // Bottom 100 pixels: re-display IP address status for 1 minute
            ESP_LOGI(TAG, "Bottom area tapped (y=%d >= 140) -> Re-displaying IP address status", ty);
            ui_show_status();
        } else {
            // Middle area (100 <= y < 140): cycle font style
            ESP_LOGI(TAG, "Middle area tapped (100 <= y=%d < 140) -> Cycling font", ty);
            ui_next_font();
        }
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static void on_image_updated(void)
{
    ESP_LOGI(TAG, "New image ready -> Reloading UI background");
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        ui_reload_background();
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static void on_wifi_state(bool connected, const char *ip_str)
{
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        char status[64];
        if (connected) {
            snprintf(status, sizeof(status), "IP: %s", ip_str);
            ui_set_status(status);
            // Re-kick SNTP sync upon getting IP (SNTP sync callback will trigger image fetch)
            sntp_time_restart();
        } else {
            snprintf(status, sizeof(status), "AP: %s (192.168.4.1)", PORTAL_AP_SSID);
            ui_set_status(status);
        }
        xSemaphoreGive(s_lvgl_mutex);
    }
}

static void on_sntp_synced(void)
{
    s_time_synced = true;
    ESP_LOGI(TAG, "SNTP time synced successfully -> Triggering image fetch");
    image_fetcher_trigger_download();
}

static void gui_task(void *pv)
{
    ESP_LOGI(TAG, "GUI Task started");

    while (1) {
        // Touch polling
        touch_driver_poll();

        if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            // Update time
            struct tm timeinfo;
            if (sntp_time_get_local(&timeinfo)) {
                ui_update_time(&timeinfo);
            } else {
                // If not synced yet, show placeholder time
                timeinfo.tm_hour = 12;
                timeinfo.tm_min = 0;
                ui_update_time(&timeinfo);
            }

            lv_timer_handler();
            xSemaphoreGive(s_lvgl_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, " Starting CYD 2.8\" Word Clock");
    ESP_LOGI(TAG, "====================================");

    s_lvgl_mutex = xSemaphoreCreateMutex();
    assert(s_lvgl_mutex != NULL);

    // 1. Settings from NVS
    settings_mgr_init();

    // 2. Display and LVGL
    ESP_ERROR_CHECK(display_driver_init());

    // 3. Touch driver
    ESP_ERROR_CHECK(touch_driver_init(on_touch));

    // 4. Image Fetcher (SPIFFS Mount + HTTP task) - CRITICAL: Must be BEFORE ui_init() so SPIFFS is mounted!
    ESP_ERROR_CHECK(image_fetcher_init(on_image_updated));

    // 5. UI Layer (Reads cached /spiffs/bg.bin immediately from SPIFFS!)
    ui_init();

    // 6. GUI Task
    xTaskCreatePinnedToCore(gui_task, "gui_task", 6144, NULL, 5, NULL, 1);

    // 7. Wi-Fi & Web Portal
    wifi_portal_init(on_wifi_state);

    // 8. SNTP (Time synchronization)
    sntp_time_init(on_sntp_synced);

    ESP_LOGI(TAG, "System initialization complete");
}
