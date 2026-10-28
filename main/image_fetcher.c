#include "image_fetcher.h"
#include "config.h"
#include "settings_mgr.h"
#include "sntp_time.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "lvgl.h"
#include "extra/libs/sjpg/tjpgd.h"

static const char *TAG = "image_fetcher";

static image_updated_cb_t s_on_updated_cb = NULL;
static bool s_is_busy = false;
static SemaphoreHandle_t s_trigger_sem = NULL;

static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

bool image_fetcher_has_valid_image(void)
{
    struct stat st;
    if (stat(IMAGE_BIN_PATH, &st) == 0) {
        return (st.st_size >= 153604);
    }
    return false;
}

bool image_fetcher_is_busy(void)
{
    return s_is_busy;
}

void image_fetcher_trigger_download(void)
{
    if (s_is_busy) {
        ESP_LOGI(TAG, "Download already in progress, ignoring duplicate trigger");
        return;
    }
    if (s_trigger_sem) {
        xSemaphoreGive(s_trigger_sem);
    }
}

// Fast sequential band conversion context
typedef struct {
    FILE *f_in;
    FILE *f_out;
    uint16_t *band_buf; // 320 x 16 pixels = 10KB
    int current_band_y; // 0, 16, 32, ...
} fast_conv_ctx_t;

static size_t conv_input_cb(JDEC *jd, uint8_t *buff, size_t ndata)
{
    fast_conv_ctx_t *ctx = (fast_conv_ctx_t *)jd->device;
    if (!ctx || !ctx->f_in) return 0;
    if (buff) {
        return fread(buff, 1, ndata, ctx->f_in);
    } else {
        return (fseek(ctx->f_in, ndata, SEEK_CUR) == 0) ? ndata : 0;
    }
}

static int conv_output_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    fast_conv_ctx_t *ctx = (fast_conv_ctx_t *)jd->device;
    if (!ctx || !ctx->f_out || !ctx->band_buf) return 0;

    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;

    // Check if we advanced to the next 16-row band
    int band_y = (rect->top / 16) * 16;
    if (band_y != ctx->current_band_y) {
        // Flush previous band sequentially
        fwrite(ctx->band_buf, sizeof(uint16_t), LCD_H_RES * 16, ctx->f_out);
        memset(ctx->band_buf, 0, LCD_H_RES * 16 * sizeof(uint16_t));
        ctx->current_band_y = band_y;
        vTaskDelay(1); // Feed FreeRTOS watchdog
    }

    uint8_t *src = (uint8_t *)bitmap;
    int local_y_start = rect->top % 16;

    for (int y = 0; y < h; y++) {
        int band_line = local_y_start + y;
        if (band_line >= 16) break;

        for (int x = 0; x < w; x++) {
            int px = rect->left + x;
            if (px >= LCD_H_RES) break;

            uint8_t r = src[(y * w + x) * 3 + 0];
            uint8_t g = src[(y * w + x) * 3 + 1];
            uint8_t b = src[(y * w + x) * 3 + 2];
            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
#if LV_COLOR_16_SWAP
            ctx->band_buf[band_line * LCD_H_RES + px] = (rgb565 >> 8) | (rgb565 << 8);
#else
            ctx->band_buf[band_line * LCD_H_RES + px] = rgb565;
#endif
        }
    }

    return 1;
}

static esp_err_t convert_jpeg_to_bin(const char *jpg_path, const char *bin_path)
{
    ESP_LOGI(TAG, "Fast converting %s to LVGL TrueColor bin: %s", jpg_path, bin_path);

    char tmp_bin[64];
    snprintf(tmp_bin, sizeof(tmp_bin), "%s.tmp", bin_path);

    FILE *f_in = fopen(jpg_path, "rb");
    if (!f_in) {
        ESP_LOGE(TAG, "Failed to open input JPEG: %s", jpg_path);
        return ESP_FAIL;
    }

    FILE *f_out = fopen(tmp_bin, "wb");
    if (!f_out) {
        ESP_LOGE(TAG, "Failed to open output bin: %s", tmp_bin);
        fclose(f_in);
        return ESP_FAIL;
    }

    // Write LVGL 4-byte header
    lv_img_header_t header;
    memset(&header, 0, sizeof(header));
    header.cf = LV_IMG_CF_TRUE_COLOR;
    header.always_zero = 0;
    header.reserved = 0;
    header.w = LCD_H_RES;
    header.h = LCD_V_RES;
    fwrite(&header, 1, sizeof(header), f_out);

    // Allocate 10KB band buffer (320 x 16 pixels) for ultra-fast sequential disk writes
    uint16_t *band_buf = calloc(LCD_H_RES * 16, sizeof(uint16_t));
    void *work_pool = malloc(4096);
    if (!band_buf || !work_pool) {
        free(band_buf);
        free(work_pool);
        fclose(f_in);
        fclose(f_out);
        unlink(tmp_bin);
        return ESP_ERR_NO_MEM;
    }

    fast_conv_ctx_t ctx = {
        .f_in = f_in,
        .f_out = f_out,
        .band_buf = band_buf,
        .current_band_y = 0
    };

    JDEC jd;
    JRESULT res = jd_prepare(&jd, conv_input_cb, work_pool, 4096, &ctx);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "TJpgDec prepare failed (code %d)", res);
        free(band_buf);
        free(work_pool);
        fclose(f_in);
        fclose(f_out);
        unlink(tmp_bin);
        return ESP_FAIL;
    }

    res = jd_decomp(&jd, conv_output_cb, 0);

    // Flush the final band
    if (res == JDR_OK && ctx.band_buf) {
        fwrite(ctx.band_buf, sizeof(uint16_t), LCD_H_RES * 16, f_out);
    }

    free(band_buf);
    free(work_pool);
    fclose(f_in);
    fclose(f_out);

    if (res != JDR_OK) {
        ESP_LOGE(TAG, "TJpgDec decomp failed (code %d)", res);
        unlink(tmp_bin);
        return ESP_FAIL;
    }

    unlink(bin_path);
    if (rename(tmp_bin, bin_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename tmp to %s", bin_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully converted JPEG to LVGL TrueColor bin (153604 bytes) in milliseconds!");
    return ESP_OK;
}

// Download URL directly into a file
static esp_err_t download_url_to_file(const char *url, const char *dest_path)
{
    ESP_LOGI(TAG, "Downloading image from: %s", url);
    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dest_path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", tmp_path);
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        fclose(f);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fclose(f);
        unlink(tmp_path);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status = %d, content_length = %d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGW(TAG, "HTTP status %d is not 200 OK", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(f);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    char *buf = malloc(2048);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(f);
        unlink(tmp_path);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    int read_bytes = 0;
    while ((read_bytes = esp_http_client_read(client, buf, 2048)) > 0) {
        fwrite(buf, 1, read_bytes, f);
        total_read += read_bytes;
        vTaskDelay(1); // Yield to IDLE task to keep watchdog fed
    }

    free(buf);
    fclose(f);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read < 2048) {
        ESP_LOGE(TAG, "Downloaded file too small (%d bytes), discarded", total_read);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    unlink(dest_path);
    if (rename(tmp_path, dest_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename temp file to %s", dest_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully downloaded image (%d bytes) to %s", total_read, dest_path);
    return ESP_OK;
}

// Fetch Bing Image of the Day URL
static esp_err_t fetch_bing_image(char *out_img_url, size_t max_len)
{
    const char *bing_api = "https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=en-US";
    ESP_LOGI(TAG, "Fetching Bing metadata from: %s", bing_api);

    esp_http_client_config_t config = {
        .url = bing_api,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    char *resp_buf = calloc(1, 2048);
    if (!resp_buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int read_len = esp_http_client_read_response(client, resp_buf, 2047);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        free(resp_buf);
        return ESP_FAIL;
    }

    const char *key = "\"urlbase\":\"";
    char *p = strstr(resp_buf, key);
    if (!p) {
        ESP_LOGW(TAG, "Could not find urlbase in Bing response, trying direct fallback");
        free(resp_buf);
        return ESP_FAIL;
    }

    p += strlen(key);
    char *end = strchr(p, '"');
    if (!end) {
        free(resp_buf);
        return ESP_FAIL;
    }
    *end = '\0';

    snprintf(out_img_url, max_len, "https://www.bing.com%s_320x240.jpg", p);
    free(resp_buf);
    ESP_LOGI(TAG, "Resolved Bing Image URL: %s", out_img_url);
    return ESP_OK;
}

static void image_fetcher_task(void *pv)
{
    ESP_LOGI(TAG, "Image fetcher task started");

    while (1) {
        const app_settings_t *s = settings_mgr_get();
        uint32_t interval_min = s->img_interval_min ? s->img_interval_min : DEFAULT_IMG_INTERVAL_MIN;
        TickType_t wait_ticks = pdMS_TO_TICKS(interval_min * 60 * 1000);

        // Drain any stale triggers before waiting for next update interval
        if (s_trigger_sem) {
            xSemaphoreTake(s_trigger_sem, 0);
        }
        xSemaphoreTake(s_trigger_sem, wait_ticks);

        // Wait until SNTP time is synchronized
        int wait_retries = 0;
        while (!sntp_time_is_synced() && wait_retries < 20) {
            ESP_LOGI(TAG, "Waiting for SNTP clock sync before TLS download (%d/20)...", wait_retries + 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            wait_retries++;
        }

        if (!sntp_time_is_synced()) {
            ESP_LOGW(TAG, "Time not synced yet, deferring image download");
            continue;
        }

        s = settings_mgr_get();
        char target_url[256];
        memset(target_url, 0, sizeof(target_url));

        s_is_busy = true;

        if (s->img_provider == 0) { // Bing
            if (fetch_bing_image(target_url, sizeof(target_url)) != ESP_OK) {
                ESP_LOGW(TAG, "Bing fetch failed, falling back to Picsum");
                strncpy(target_url, "https://picsum.photos/320/240.jpg", sizeof(target_url) - 1);
            }
        } else if (s->img_provider == 1) { // Picsum
            strncpy(target_url, "https://picsum.photos/320/240.jpg", sizeof(target_url) - 1);
        } else { // Custom
            if (strlen(s->custom_img_url) > 0) {
                strncpy(target_url, s->custom_img_url, sizeof(target_url) - 1);
            } else {
                strncpy(target_url, DEFAULT_CUSTOM_IMG_URL, sizeof(target_url) - 1);
            }
        }

        esp_err_t ret = download_url_to_file(target_url, IMAGE_JPG_PATH);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Initial download failed, retrying in 5 seconds...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            ret = download_url_to_file(target_url, IMAGE_JPG_PATH);
        }

        if (ret == ESP_OK) {
            // Ultra-fast sequential conversion of JPEG to LVGL TrueColor bin
            esp_err_t conv_ret = convert_jpeg_to_bin(IMAGE_JPG_PATH, IMAGE_BIN_PATH);
            if (conv_ret == ESP_OK) {
                ESP_LOGI(TAG, "Image ready, invoking update callback");
                if (s_on_updated_cb) {
                    s_on_updated_cb();
                }
            } else {
                ESP_LOGE(TAG, "Failed to convert downloaded JPEG to LVGL bin");
            }
        } else {
            ESP_LOGW(TAG, "Failed to download image from %s", target_url);
        }

        s_is_busy = false;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t image_fetcher_init(image_updated_cb_t on_updated)
{
    s_on_updated_cb = on_updated;
    s_trigger_sem = xSemaphoreCreateBinary();

    esp_err_t ret = init_spiffs();
    if (ret != ESP_OK) {
        return ret;
    }

    // Run at low priority (priority 2) on Core 0 so IDLE task is never starved
    xTaskCreatePinnedToCore(image_fetcher_task, "img_fetcher", 8192, NULL, 2, NULL, 0);
    return ESP_OK;
}
