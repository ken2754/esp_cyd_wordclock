#pragma once

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*image_updated_cb_t)(void);

esp_err_t image_fetcher_init(image_updated_cb_t on_updated);
void image_fetcher_trigger_download(void);
bool image_fetcher_has_valid_image(void);
bool image_fetcher_is_busy(void);

#ifdef __cplusplus
}
#endif
