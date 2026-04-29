#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include "common.h"
#include "esp_err.h"
#include <stdbool.h>

esp_err_t time_sync_init(const char *timezone);
bool time_sync_is_synced(void);
const char *time_sync_get_str(void);
const char *time_sync_get_date_path(void);

#endif // TIME_SYNC_H
