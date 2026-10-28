#include "settings_mgr.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "settings_mgr";
static const char *NVS_NAMESPACE = "wordclock";

static app_settings_t s_settings;

static void set_defaults(app_settings_t *s)
{
    memset(s, 0, sizeof(app_settings_t));
    s->wifi_ssid[0] = '\0';
    s->wifi_pass[0] = '\0';
    strncpy(s->timezone, DEFAULT_TIMEZONE, sizeof(s->timezone) - 1);
    s->img_provider = DEFAULT_IMG_PROVIDER;
    s->img_interval_min = DEFAULT_IMG_INTERVAL_MIN;
    strncpy(s->custom_img_url, DEFAULT_CUSTOM_IMG_URL, sizeof(s->custom_img_url) - 1);
    s->font_mode = DEFAULT_FONT_MODE;
    s->brightness = DEFAULT_BRIGHTNESS;
}

void settings_mgr_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    set_defaults(&s_settings);

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No settings in NVS, using defaults");
        return;
    }

    size_t len;
    len = sizeof(s_settings.wifi_ssid);
    nvs_get_str(handle, "ssid", s_settings.wifi_ssid, &len);

    len = sizeof(s_settings.wifi_pass);
    nvs_get_str(handle, "pass", s_settings.wifi_pass, &len);

    len = sizeof(s_settings.timezone);
    nvs_get_str(handle, "tz", s_settings.timezone, &len);

    uint8_t u8_val;
    if (nvs_get_u8(handle, "img_prov", &u8_val) == ESP_OK) {
        s_settings.img_provider = u8_val;
    }

    uint16_t u16_val;
    if (nvs_get_u16(handle, "img_int", &u16_val) == ESP_OK) {
        s_settings.img_interval_min = u16_val;
    }

    len = sizeof(s_settings.custom_img_url);
    nvs_get_str(handle, "img_url", s_settings.custom_img_url, &len);

    if (nvs_get_u8(handle, "font_mode", &u8_val) == ESP_OK) {
        s_settings.font_mode = u8_val;
    }

    if (nvs_get_u8(handle, "bright", &u8_val) == ESP_OK) {
        s_settings.brightness = u8_val;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Settings loaded: SSID='%s', TZ='%s', ImgProv=%d, Brightness=%d%%",
             s_settings.wifi_ssid, s_settings.timezone, s_settings.img_provider, s_settings.brightness);
}

const app_settings_t *settings_mgr_get(void)
{
    return &s_settings;
}

void settings_mgr_save(const app_settings_t *settings)
{
    if (!settings) return;
    s_settings = *settings;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing (%s)", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, "ssid", s_settings.wifi_ssid);
    nvs_set_str(handle, "pass", s_settings.wifi_pass);
    nvs_set_str(handle, "tz", s_settings.timezone);
    nvs_set_u8(handle, "img_prov", s_settings.img_provider);
    nvs_set_u16(handle, "img_int", s_settings.img_interval_min);
    nvs_set_str(handle, "img_url", s_settings.custom_img_url);
    nvs_set_u8(handle, "font_mode", s_settings.font_mode);
    nvs_set_u8(handle, "bright", s_settings.brightness);

    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Settings successfully saved to NVS");
}

void settings_mgr_set_wifi(const char *ssid, const char *pass)
{
    if (ssid) {
        strncpy(s_settings.wifi_ssid, ssid, sizeof(s_settings.wifi_ssid) - 1);
    }
    if (pass) {
        strncpy(s_settings.wifi_pass, pass, sizeof(s_settings.wifi_pass) - 1);
    }
    settings_mgr_save(&s_settings);
}

void settings_mgr_set_brightness(uint8_t brightness)
{
    s_settings.brightness = brightness;
    settings_mgr_save(&s_settings);
}
