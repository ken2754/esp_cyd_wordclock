#include "wifi_portal.h"
#include "settings_mgr.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

static const char *TAG = "wifi_portal";

static bool s_is_ap_active = false;
static bool s_is_connected = false;
static char s_current_ip[32] = "192.168.4.1";
static httpd_handle_t s_http_server = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static wifi_state_cb_t s_user_state_cb = NULL;
static int s_retry_num = 0;

static const char HTML_PAGE[] = 
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>CYD Word Clock Settings</title>"
"<style>"
"body{font-family:sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:16px;}"
".card{max-width:480px;margin:0 auto;background:#1e293b;padding:24px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.5);}"
"h2{margin-top:0;color:#38bdf8;font-size:1.4rem;text-align:center;}"
"label{display:block;margin-top:14px;font-size:0.85rem;color:#94a3b8;font-weight:600;}"
"input,select{width:100%%;padding:10px;margin-top:6px;border-radius:6px;border:1px solid #334155;background:#0f172a;color:#fff;box-sizing:border-box;font-size:0.95rem;}"
"input:focus,select:focus{border-color:#38bdf8;outline:none;}"
"button{width:100%%;padding:12px;margin-top:24px;background:#0284c7;color:#fff;border:none;border-radius:6px;font-weight:bold;cursor:pointer;font-size:1rem;transition:0.2s;}"
"button:hover{background:#0369a1;}"
".note{font-size:0.75rem;color:#64748b;margin-top:4px;}"
".badge{background:#0369a1;color:#fff;padding:4px 8px;border-radius:4px;font-size:0.8rem;text-align:center;margin-bottom:16px;}"
"</style></head><body>"
"<div class='card'>"
"<h2>CYD Word Clock</h2>"
"<div class='badge'>Connected / Portal Active</div>"
"<form method='POST' action='/save'>"
"<label>Wi-Fi SSID</label><input type='text' name='ssid' value='%s' required>"
"<label>Wi-Fi Password</label><input type='password' name='pass' value='%s'>"
"<label>Timezone (POSIX format)</label><input type='text' name='tz' value='%s' placeholder='JST-9'>"
"<div class='note'>e.g. JST-9 (Japan), EST5EDT (US Eastern), PST8PDT (US Pacific), UTC0</div>"
"<hr style='border:0;border-top:1px solid #334155;margin:20px 0;'>"
"<label>Background Image Provider</label>"
"<select name='prov'>"
"<option value='0' %s>Bing Daily Image (Recommended)</option>"
"<option value='1' %s>Picsum Photos (Random Scenic)</option>"
"<option value='2' %s>Custom Image URL</option>"
"</select>"
"<label>Custom Image URL (if Custom selected)</label>"
"<input type='text' name='url' value='%s' placeholder='https://.../320/240.jpg'>"
"<label>Image Refresh Interval</label>"
"<select name='interval'>"
"<option value='5' %s>Every 5 minutes</option>"
"<option value='15' %s>Every 15 minutes</option>"
"<option value='30' %s>Every 30 minutes</option>"
"<option value='60' %s>Every 1 hour</option>"
"<option value='1440' %s>Once a day (24 hours)</option>"
"</select>"
"<hr style='border:0;border-top:1px solid #334155;margin:20px 0;'>"
"<label>Font Style Mode</label>"
"<select name='font'>"
"<option value='0' %s>Random Cycle (Changes every 5 min)</option>"
"<option value='1' %s>Modern Sans (Clean)</option>"
"<option value='2' %s>Bold Display</option>"
"<option value='3' %s>Elegant Serif</option>"
"<option value='4' %s>Retro Monospace</option>"
"</select>"
"<label>Display Brightness</label>"
"<select name='bright'>"
"<option value='100' %s>100%% (Bright)</option>"
"<option value='25' %s>25%% (Night Dim)</option>"
"<option value='0' %s>0%% (Display Off)</option>"
"</select>"
"<button type='submit'>Save and Apply</button>"
"</form></div></body></html>";

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    char a, b;
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            dst[i++] = 16*a + b;
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static bool extract_param(const char *post_data, const char *key, char *out, size_t out_len)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    const char *start = strstr(post_data, search_key);
    if (!start) return false;
    start += strlen(search_key);
    const char *end = strchr(start, '&');
    size_t val_len = end ? (size_t)(end - start) : strlen(start);
    char *tmp = malloc(val_len + 1);
    if (!tmp) return false;
    memcpy(tmp, start, val_len);
    tmp[val_len] = '\0';
    url_decode(out, tmp, out_len);
    free(tmp);
    return true;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const app_settings_t *s = settings_mgr_get();

    char prov0[12] = "", prov1[12] = "", prov2[12] = "";
    if (s->img_provider == 1) strcpy(prov1, "selected");
    else if (s->img_provider == 2) strcpy(prov2, "selected");
    else strcpy(prov0, "selected");

    char int5[12] = "", int15[12] = "", int30[12] = "", int60[12] = "", int1440[12] = "";
    if (s->img_interval_min == 5) strcpy(int5, "selected");
    else if (s->img_interval_min == 30) strcpy(int30, "selected");
    else if (s->img_interval_min == 60) strcpy(int60, "selected");
    else if (s->img_interval_min == 1440) strcpy(int1440, "selected");
    else strcpy(int15, "selected");

    char f0[12] = "", f1[12] = "", f2[12] = "", f3[12] = "", f4[12] = "";
    if (s->font_mode == 1) strcpy(f1, "selected");
    else if (s->font_mode == 2) strcpy(f2, "selected");
    else if (s->font_mode == 3) strcpy(f3, "selected");
    else if (s->font_mode == 4) strcpy(f4, "selected");
    else strcpy(f0, "selected");

    char b100[12] = "", b25[12] = "", b0[12] = "";
    if (s->brightness == 0) strcpy(b0, "selected");
    else if (s->brightness <= 25) strcpy(b25, "selected");
    else strcpy(b100, "selected");

    char *buf = malloc(4096);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(buf, 4096, HTML_PAGE,
             s->wifi_ssid, s->wifi_pass, s->timezone,
             prov0, prov1, prov2,
             s->custom_img_url,
             int5, int15, int30, int60, int1440,
             f0, f1, f2, f3, f4,
             b100, b25, b0);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static void restart_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char content[1024];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    app_settings_t new_settings = *settings_mgr_get();
    extract_param(content, "ssid", new_settings.wifi_ssid, sizeof(new_settings.wifi_ssid));
    extract_param(content, "pass", new_settings.wifi_pass, sizeof(new_settings.wifi_pass));
    extract_param(content, "tz", new_settings.timezone, sizeof(new_settings.timezone));

    char tmp[32];
    if (extract_param(content, "prov", tmp, sizeof(tmp))) new_settings.img_provider = atoi(tmp);
    if (extract_param(content, "interval", tmp, sizeof(tmp))) new_settings.img_interval_min = atoi(tmp);
    extract_param(content, "url", new_settings.custom_img_url, sizeof(new_settings.custom_img_url));
    if (extract_param(content, "font", tmp, sizeof(tmp))) new_settings.font_mode = atoi(tmp);
    if (extract_param(content, "bright", tmp, sizeof(tmp))) new_settings.brightness = atoi(tmp);

    settings_mgr_save(&new_settings);

    const char *resp = "<html><body style='font-family:sans-serif;background:#0f172a;color:#fff;text-align:center;padding:40px;'>"
                       "<h2>Settings Saved!</h2><p>Restarting CYD Word Clock...</p></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void start_web_server(void)
{
    if (s_http_server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
        };
        httpd_register_uri_handler(s_http_server, &root_uri);

        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler,
        };
        httpd_register_uri_handler(s_http_server, &save_uri);
        ESP_LOGI(TAG, "Web server started on port 80");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        const app_settings_t *s = settings_mgr_get();
        if (strlen(s->wifi_ssid) > 0) {
            ESP_LOGI(TAG, "STA started, connecting to '%s'...", s->wifi_ssid);
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_retry_num < 5) {
            s_retry_num++;
            ESP_LOGI(TAG, "STA connect failed, retry %d...", s_retry_num);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "STA connection failed after 5 retries. Remaining in SoftAP mode.");
            if (s_user_state_cb) {
                s_user_state_cb(false, "192.168.4.1");
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_current_ip, sizeof(s_current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA Connected! IP: %s", s_current_ip);
        s_is_connected = true;
        s_retry_num = 0;

        start_web_server();

        if (s_user_state_cb) {
            s_user_state_cb(true, s_current_ip);
        }
    }
}

void wifi_portal_init(wifi_state_cb_t state_cb)
{
    s_user_state_cb = state_cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    // Explicitly configure SoftAP IP to 192.168.4.1 with DHCP server
    esp_netif_ip_info_t ip_info;
    esp_netif_set_ip4_addr(&ip_info.ip, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.gw, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Always start in APSTA mode so SoftAP is instantly available
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = PORTAL_AP_SSID,
            .ssid_len = strlen(PORTAL_AP_SSID),
            .channel = PORTAL_AP_CHANNEL,
            .password = PORTAL_AP_PASS,
            .max_connection = PORTAL_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .beacon_interval = 100,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    const app_settings_t *s = settings_mgr_get();
    if (strlen(s->wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Configuring STA SSID: '%s'", s->wifi_ssid);
        wifi_config_t sta_config = { 0 };
        strncpy((char *)sta_config.sta.ssid, s->wifi_ssid, sizeof(sta_config.sta.ssid));
        strncpy((char *)sta_config.sta.password, s->wifi_pass, sizeof(sta_config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }

    // CRITICAL: esp_wifi_start() MUST be called to activate Wi-Fi RF radio!
    ESP_ERROR_CHECK(esp_wifi_start());
    s_is_ap_active = true;

    // Start HTTP Web server
    start_web_server();

    ESP_LOGI(TAG, "SoftAP started: SSID='%s', IP=%s, Channel=%d",
             PORTAL_AP_SSID, PORTAL_IP_ADDR, PORTAL_AP_CHANNEL);

    if (strlen(s->wifi_ssid) == 0) {
        if (s_user_state_cb) {
            s_user_state_cb(false, "192.168.4.1");
        }
    }
}

bool wifi_portal_is_connected(void)
{
    return s_is_connected;
}

const char *wifi_portal_get_ip(void)
{
    return s_current_ip;
}

bool wifi_portal_is_ap_active(void)
{
    return s_is_ap_active;
}
