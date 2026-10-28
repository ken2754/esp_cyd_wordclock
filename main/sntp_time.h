#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sntp_sync_cb_t)(void);

void sntp_time_init(sntp_sync_cb_t cb);
void sntp_time_restart(void);
void sntp_time_set_timezone(const char *tz);
bool sntp_time_is_synced(void);
bool sntp_time_get_local(struct tm *out_tm);

#ifdef __cplusplus
}
#endif
