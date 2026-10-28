#include "sntp_time.h"
#include "settings_mgr.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "sntp_time";
static bool s_is_synced = false;
static sntp_sync_cb_t s_sync_cb = NULL;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP time synchronized successfully");
    s_is_synced = true;
    if (s_sync_cb) {
        s_sync_cb();
    }
}

void sntp_time_set_timezone(const char *tz)
{
    if (!tz || strlen(tz) == 0) {
        tz = DEFAULT_TIMEZONE;
    }
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to '%s'", tz);
}

void sntp_time_restart(void)
{
    ESP_LOGI(TAG, "Restarting SNTP client to force sync...");
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, DEFAULT_NTP_SERVER);
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

void sntp_time_init(sntp_sync_cb_t cb)
{
    s_sync_cb = cb;
    const app_settings_t *s = settings_mgr_get();
    sntp_time_set_timezone(s->timezone);
    sntp_time_restart();
}

bool sntp_time_is_synced(void)
{
    return s_is_synced;
}

bool sntp_time_get_local(struct tm *out_tm)
{
    time_t now;
    time(&now);
    if (now < 1600000000) { // Prior to 2020: not synced
        return false;
    }
    if (out_tm) {
        localtime_r(&now, out_tm);
    }
    return true;
}
