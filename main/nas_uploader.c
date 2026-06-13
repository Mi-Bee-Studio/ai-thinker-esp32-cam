/*
 * Copyright (C) 2024 MiBee Cam Authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "nas_uploader.h"
#include "webdav_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include <sys/stat.h>

static const char *TAG = "nas";

#define UPLOAD_QUEUE_SIZE    16
#define MAX_RETRIES          3
#define MAX_CONSEC_FAILS     10
#define PAUSE_DURATION_MS    (5 * 60 * 1000)  /* 5 minutes */
#define PATH_BUF_SIZE        128
#define QUEUE_FILE_PATH       "/spiffs/upload_queue.json"
#define MAX_PERSISTENT_QUEUE  100

static QueueHandle_t s_queue        = NULL;
static TaskHandle_t  s_task_handle  = NULL;
static bool          s_initialized  = false;
static int           s_consec_fails = 0;
static int64_t       s_paused_until_ms = 0;
static char          s_last_upload_str[32] = "";
static int           s_queue_count  = 0;
static uint32_t      s_stack_hwm    = 0;
static volatile int   s_upload_success = 0;
static volatile int   s_upload_failure = 0;

static SemaphoreHandle_t s_file_mutex  = NULL;

/* ---- Persistent Queue (SPIFFS-backed) ---- */

/**
 * @brief Read persistent queue file, return cJSON array (caller must delete)
 * @return cJSON array or NULL if file missing/corrupt
 */
static cJSON *read_queue_json(void)
{
    FILE *f = fopen(QUEUE_FILE_PATH, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 16384) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, fsize, f);
    buf[rd] = '\0';
    fclose(f);

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    return arr;
}

/**
 * @brief Write cJSON array to persistent queue file
 */
static esp_err_t write_queue_json(cJSON *arr)
{
    FILE *f = fopen(QUEUE_FILE_PATH, "w");
    if (!f) return ESP_FAIL;

    char *json_str = cJSON_PrintUnformatted(arr);
    if (json_str) {
        fputs(json_str, f);
        cJSON_free(json_str);
    }
    fclose(f);
    return ESP_OK;
}

/**
 * @brief Add filepath to persistent queue file (FIFO, max 100)
 */
static void persist_enqueue(const char *filepath)
{
    if (!s_file_mutex) return;
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    cJSON *arr = read_queue_json();
    if (!arr) {
        arr = cJSON_CreateArray();
    }

    /* Enforce FIFO cap — remove oldest entries */
    while (cJSON_GetArraySize(arr) >= MAX_PERSISTENT_QUEUE) {
        cJSON_DeleteItemFromArray(arr, 0);
    }

    cJSON_AddItemToArray(arr, cJSON_CreateString(filepath));
    write_queue_json(arr);
    cJSON_Delete(arr);

    xSemaphoreGive(s_file_mutex);
}

/**
 * @brief Remove filepath from persistent queue file after successful upload
 */
static void persist_remove(const char *filepath)
{
    if (!s_file_mutex) return;
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    cJSON *arr = read_queue_json();
    if (!arr) {
        xSemaphoreGive(s_file_mutex);
        return;
    }

    int size = cJSON_GetArraySize(arr);
    for (int i = 0; i < size; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (item && item->valuestring && strcmp(item->valuestring, filepath) == 0) {
            cJSON_DeleteItemFromArray(arr, i);
            break;
        }
    }

    if (cJSON_GetArraySize(arr) == 0) {
        remove(QUEUE_FILE_PATH);
        cJSON_Delete(arr);
    } else {
        write_queue_json(arr);
        cJSON_Delete(arr);
    }

    xSemaphoreGive(s_file_mutex);
}

/**
 * @brief Try to load the next item from persistent file into FreeRTOS queue
 * Called after each successful upload to keep the pipeline flowing
 */
static void persist_load_next(void)
{
    if (!s_file_mutex || !s_queue) return;
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    cJSON *arr = read_queue_json();
    if (!arr || cJSON_GetArraySize(arr) == 0) {
        if (arr) cJSON_Delete(arr);
        xSemaphoreGive(s_file_mutex);
        return;
    }

    /* Only enqueue if there is room in the FreeRTOS queue */
    UBaseType_t spaces = UPLOAD_QUEUE_SIZE - uxQueueMessagesWaiting(s_queue);
    if (spaces == 0) {
        cJSON_Delete(arr);
        xSemaphoreGive(s_file_mutex);
        return;
    }

    cJSON *item = cJSON_GetArrayItem(arr, 0);
    if (item && item->valuestring) {
        char buf[PATH_BUF_SIZE] = {0};
        strncpy(buf, item->valuestring, sizeof(buf) - 1);
        xQueueSend(s_queue, buf, 0);
    }

    cJSON_Delete(arr);
    xSemaphoreGive(s_file_mutex);
}

/**
 * @brief Load pending items from persistent file into FreeRTOS queue on startup
 * Fills the queue up to its capacity; remaining items stay in the file
 */
static void persist_load(void)
{
    if (!s_file_mutex || !s_queue) return;
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    cJSON *arr = read_queue_json();
    if (!arr) {
        xSemaphoreGive(s_file_mutex);
        return;
    }

    int total = cJSON_GetArraySize(arr);
    int loaded = 0;

    for (int i = 0; i < total; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item || !item->valuestring) continue;

        char buf[PATH_BUF_SIZE] = {0};
        strncpy(buf, item->valuestring, sizeof(buf) - 1);

        /* Non-blocking — stop when queue is full */
        if (xQueueSend(s_queue, buf, 0) != pdTRUE) {
            break;
        }
        loaded++;
    }

    ESP_LOGI(TAG, "Loaded %d/%d items from persistent queue", loaded, total);
    cJSON_Delete(arr);

    xSemaphoreGive(s_file_mutex);
}

/**
 * @brief Upload task main loop (FreeRTOS task function)
 * Dequeue file path from queue, upload via WebDAV
 * Supports exponential backoff retry and auto-pause on consecutive failures
 * @param arg Unused
 */
static void upload_task(void *arg)
{
    (void)arg;

    /* Register with task watchdog */
    esp_task_wdt_add(NULL);

    while (1) {
        /* Feed task watchdog each iteration */
        esp_task_wdt_reset();

        /* Track stack high-water mark */
        s_stack_hwm = uxTaskGetStackHighWaterMark(NULL);

        /* Check global pause */
        if (esp_timer_get_time() / 1000 < s_paused_until_ms) {
            for (int i = 0; i < 5; i++) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(1000)); }
            continue;
        }

        /* Check WiFi connectivity — avoid wasting TCP attempts when disconnected */
        if (wifi_get_state() != WIFI_STATE_STA_CONNECTED) {
            ESP_LOGW(TAG, "WiFi not connected, pausing upload for 10s");
            for (int i = 0; i < 10; i++) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(1000)); }
            continue;
        }

        /* Wait for a queue item */
        char filepath[PATH_BUF_SIZE];
        if (xQueueReceive(s_queue, filepath, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        s_queue_count = (int)uxQueueMessagesWaiting(s_queue);

        const cam_config_t *cfg = config_get();
        bool success = false;

        /* Extract filename for fallback remote path */
        const char *filename = strrchr(filepath, '/');
        filename = filename ? filename + 1 : filepath;

        /* Try to read SHA256 companion file for integrity logging */
        char sha_path[PATH_BUF_SIZE + 8];
        snprintf(sha_path, sizeof(sha_path), "%s.sha256", filepath);
        FILE *sf = fopen(sha_path, "r");
        if (sf) {
            char hex[65] = {0};
            if (fread(hex, 1, 64, sf) > 0) {
                hex[64] = '\0';
                ESP_LOGI(TAG, "SHA256[%s] = %s", filename, hex);
            }
            fclose(sf);
        }

        /* Build remote path from /recordings/... portion */
        char remote_path[256];
        const char *rec_part = strstr(filepath, "/recordings/");
        if (rec_part) {
            snprintf(remote_path, sizeof(remote_path), "%s%s",
                     cfg->upload_base_path, rec_part + strlen("/recordings"));
        } else {
            snprintf(remote_path, sizeof(remote_path), "%s/%s",
                     cfg->upload_base_path, filename);
        }

        /* Extract parent directory for mkdir */
        char remote_dir[256];
        strncpy(remote_dir, remote_path, sizeof(remote_dir) - 1);
        remote_dir[sizeof(remote_dir) - 1] = '\0';
        char *last_slash = strrchr(remote_dir, '/');
        if (last_slash) *last_slash = '\0';

        /* ---- Upload dispatch ---- */
        esp_task_wdt_reset();

        if (cfg->webdav_enabled && strlen(cfg->webdav_url) > 0) {
            webdav_config_t dav_cfg = {0};
            strncpy(dav_cfg.url, cfg->webdav_url, sizeof(dav_cfg.url) - 1);
            dav_cfg.url[sizeof(dav_cfg.url) - 1] = '\0';
            strncpy(dav_cfg.user, cfg->webdav_user, sizeof(dav_cfg.user) - 1);
            dav_cfg.user[sizeof(dav_cfg.user) - 1] = '\0';
            strncpy(dav_cfg.pass, cfg->webdav_pass, sizeof(dav_cfg.pass) - 1);
            dav_cfg.pass[sizeof(dav_cfg.pass) - 1] = '\0';

            ESP_LOGI(TAG, "Starting upload: file=%s method=webdav", filepath);
            webdav_mkdir_recursive(&dav_cfg, remote_dir);
            if (webdav_upload(&dav_cfg, remote_path, filepath) == ESP_OK) {
                success = true;
            }
        } else {
            ESP_LOGD(TAG, "Upload disabled or not configured, skipping: %s", filepath);
        }

        /* ---- Handle result ---- */
        if (success) {
            s_consec_fails = 0;
            const char *t = time_sync_get_str();
            if (t) {
                strncpy(s_last_upload_str, t, sizeof(s_last_upload_str) - 1);
                s_last_upload_str[sizeof(s_last_upload_str) - 1] = '\0';
            }
            s_upload_success++;
            ESP_LOGI(TAG, "Upload success: %s", filepath);
            /* Remove from persistent queue and load next pending item */
            persist_remove(filepath);
            persist_load_next();
        } else {
            s_consec_fails++;
            s_upload_failure++;
            ESP_LOGW(TAG, "Upload failed (%d consecutive): %s",
                     s_consec_fails, filepath);

            if (s_consec_fails >= MAX_CONSEC_FAILS) {
                s_paused_until_ms = esp_timer_get_time() / 1000 + PAUSE_DURATION_MS;
                ESP_LOGW(TAG, "Too many failures, pausing for 5 minutes");
            }
        }
    }
}

/**
 * @brief Initialize NAS upload module
 * Create upload queue (capacity 16) and background upload task (stack 6144, core 1, priority 3)
 * @return ESP_OK success, ESP_FAIL failed to create queue or task
 */
esp_err_t nas_uploader_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(UPLOAD_QUEUE_SIZE, PATH_BUF_SIZE);
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create upload queue");
        return ESP_FAIL;
    }

    s_file_mutex = xSemaphoreCreateMutex();
    if (!s_file_mutex) {
        ESP_LOGE(TAG, "Failed to create file mutex");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(upload_task, "upload",
                                              6144, NULL, 3,
                                              &s_task_handle, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create upload task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }

    /* Restore pending uploads from persistent queue */
    persist_load();

    s_initialized = true;
    ESP_LOGI(TAG, "NAS uploader initialized (queue %d, core 1)", UPLOAD_QUEUE_SIZE);
    return ESP_OK;
}

/**
 * @brief Add file path to upload queue
 * @param filepath Full path of file to upload
 * @return ESP_OK success, ESP_ERR_INVALID_STATE not initialized, ESP_ERR_NO_MEM queue full
 */
esp_err_t nas_uploader_enqueue(const char *filepath)
{
    if (!s_initialized || !s_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    char buf[PATH_BUF_SIZE] = {0};
    strncpy(buf, filepath, sizeof(buf) - 1);

    persist_enqueue(filepath);

    if (xQueueSend(s_queue, buf, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Upload queue full, %d entries queued", s_queue_count);
        return ESP_ERR_NO_MEM;
    }

    s_queue_count = (int)uxQueueMessagesWaiting(s_queue);
    return ESP_OK;
}

/**
 * @brief Get upload module current status
 * @param last_upload Output last successful upload time string
 * @param len last_upload buffer length
 * @param queue_count Output current file count in queue
 * @param paused Output if paused due to consecutive failures
 */
void nas_uploader_get_status(char *last_upload, size_t len,
                             int *queue_count, bool *paused)
{
    if (last_upload) {
        strncpy(last_upload, s_last_upload_str, len - 1);
        last_upload[len - 1] = '\0';
    }
    if (queue_count) {
        *queue_count = s_queue_count;
    }
    if (paused) {
        *paused = (esp_timer_get_time() / 1000 < s_paused_until_ms);
    }
}

/**
 * @brief Get upload task stack high-water mark
 * @return Remaining stack space (bytes)
 */
uint32_t nas_uploader_get_stack_hwm(void)
{
    return s_stack_hwm;
}

/**
 * @brief Get upload success/failure cumulative counts
 * @param success Output success count
 * @param failure Output failure count
 */
void nas_uploader_get_stats(int *success, int *failure)
{
    if (success) {
        *success = s_upload_success;
    }
    if (failure) {
        *failure = s_upload_failure;
    }
}