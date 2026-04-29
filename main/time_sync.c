#include "time_sync.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <esp_log.h>
#include <unistd.h>

static const char *TAG = "time_sync";

// Static buffers for thread-safe string operations
static char time_str_buf[20];
static char date_path_buf[8];
static bool initialized = false;

esp_err_t time_sync_init(const char *timezone) {
    if (initialized) {
        ESP_LOGW(TAG, "Time sync already initialized");
        return ESP_OK;
    }
    
    if (!timezone) {
        timezone = CONFIG_DEFAULT_TIMEZONE;
    }
    
    // Set timezone
    setenv("TZ", timezone, 1);
    tzset();
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    initialized = true;
    ESP_LOGI(TAG, "SNTP initialized, timezone: %s", timezone);
    return ESP_OK;
}

bool time_sync_is_synced(void) {
    time_t now = time(NULL);
    return now > 1704067200; // After Jan 1, 2024 threshold
}

const char *time_sync_get_str(void) {
    if (!initialized) {
        strncpy(time_str_buf, "Time not synced", sizeof(time_str_buf));
        return time_str_buf;
    }
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime_r(&now, &(struct tm){0});
    
    if (timeinfo) {
        strftime(time_str_buf, sizeof(time_str_buf), "%Y-%m-%d %H:%M:%S", timeinfo);
        return time_str_buf;
    } else {
        strncpy(time_str_buf, "Invalid time", sizeof(time_str_buf));
        return time_str_buf;
    }
}

const char *time_sync_get_date_path(void) {
    if (!initialized) {
        strncpy(date_path_buf, "2024-01", sizeof(date_path_buf));
        return date_path_buf;
    }
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime_r(&now, &(struct tm){0});
    
    if (timeinfo) {
        strftime(date_path_buf, sizeof(date_path_buf), "%Y-%m", timeinfo);
        return date_path_buf;
    } else {
        strncpy(date_path_buf, "2024-01", sizeof(date_path_buf));
        return date_path_buf;
    }
}
