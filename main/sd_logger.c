/**
 * sd_logger.c - Buffered SD card logger for severe errors and WiFi anomalies
 *
 * See sd_logger.h for overview.
 *
 * Writer task uses ONLY safe SD operations (mkdir, fopen, fprintf, fclose).
 * Never calls stat/opendir/f_getfree — those hang on the degraded GPIO14
 * SPI bus after camera init.
 */

#include "sd_logger.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "config_manager.h"
#include "storage_manager.h"
#include "time_sync.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>   /* mkdir */
#include <sys/types.h>

static const char *TAG = "sd_logger";

#define SD_LOG_QUEUE_LEN    32
#define SD_LOG_TEXT_MAX     160
#define SD_LOG_TAG_MAX      16
#define SD_LOG_DIR          "/sdcard/logs"
#define SD_LOG_FILE         "/sdcard/logs/system.log"
#define WRITER_STACK        4096
#define WRITER_PRIO         3   /* low priority — logging never blocks system */

typedef struct {
    uint8_t  level;
    char     tag[SD_LOG_TAG_MAX];
    char     text[SD_LOG_TEXT_MAX];
    uint32_t uptime_sec;
} sd_log_entry_t;

static QueueHandle_t      s_queue = NULL;
static TaskHandle_t       s_writer_task = NULL;
static volatile bool      s_initialized = false;
static bool               s_dir_created = false;

/* ---- Writer task ---- */

static void writer_task(void *arg)
{
    sd_log_entry_t entry;
    ESP_LOGI(TAG, "Writer task started");

    while (s_initialized) {
        /* Block waiting for first entry (1s timeout to stay responsive) */
        if (xQueueReceive(s_queue, &entry, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        /* Skip if SD not mounted */
        if (!storage_is_available()) {
            continue;
        }

        /* Create log directory once (mkdir is safe, ignores EEXIST) */
        if (!s_dir_created) {
            mkdir(SD_LOG_DIR, 0775);
            s_dir_created = true;
        }

        /* Open in append mode, write batch, close */
        FILE *f = fopen(SD_LOG_FILE, "a");
        if (!f) {
            ESP_LOGD(TAG, "Failed to open %s", SD_LOG_FILE);
            continue;
        }

        /* Build timestamp prefix */
        char ts[32] = "";
        if (time_sync_is_synced()) {
            time_t now = time(NULL);
            struct tm tm;
            localtime_r(&now, &tm);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        }

        /* Write first entry */
        fprintf(f, "[%s] [%s] up=%us [%s] %s\n",
                ts[0] ? ts : "unsynced",
                entry.level == SD_LOG_ERROR ? "ERROR" : "WARN",
                (unsigned)entry.uptime_sec,
                entry.tag, entry.text);

        /* Drain remaining queue entries (batch write) */
        while (xQueueReceive(s_queue, &entry, 0) == pdTRUE) {
            fprintf(f, "[%s] [%s] up=%us [%s] %s\n",
                    ts[0] ? ts : "unsynced",
                    entry.level == SD_LOG_ERROR ? "ERROR" : "WARN",
                    (unsigned)entry.uptime_sec,
                    entry.tag, entry.text);
        }

        fclose(f);
    }

    ESP_LOGI(TAG, "Writer task exiting");
    s_writer_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

esp_err_t sd_logger_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_queue = xQueueCreate(SD_LOG_QUEUE_LEN, sizeof(sd_log_entry_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }

    s_dir_created = false;
    s_initialized = true;

    BaseType_t ret = xTaskCreate(writer_task, "sd_logger",
                                 WRITER_STACK, NULL,
                                 WRITER_PRIO, &s_writer_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create writer task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        s_initialized = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SD logger initialized (enabled=%s)",
             config_get()->sd_log_enabled ? "yes" : "no");
    return ESP_OK;
}

void sd_logger_set_enabled(bool enabled)
{
    config_set_sd_log_enabled(enabled ? 1 : 0);
    ESP_LOGI(TAG, "Logging %s", enabled ? "enabled" : "disabled");
}

bool sd_logger_is_enabled(void)
{
    return config_get()->sd_log_enabled != 0;
}

void sd_logf(sd_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (!s_initialized || !s_queue) return;
    if (!config_get()->sd_log_enabled) return;

    sd_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.level     = (uint8_t)level;
    entry.uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);

    if (tag) {
        strncpy(entry.tag, tag, SD_LOG_TAG_MAX - 1);
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(entry.text, SD_LOG_TEXT_MAX, fmt, ap);
    va_end(ap);

    /* Non-blocking — drop entry if queue full (never block caller) */
    xQueueSend(s_queue, &entry, 0);
}
