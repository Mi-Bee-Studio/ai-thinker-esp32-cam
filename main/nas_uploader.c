#include "nas_uploader.h"
#include "config_manager.h"
#include "webdav_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "nas_uploader";

#define UPLOAD_QUEUE_SIZE    10
#define MAX_RETRIES          3
#define RETRY_DELAY_MS       2000
#define FILEPATH_BUF_SIZE    256

typedef struct {
    char filepath[FILEPATH_BUF_SIZE];
} upload_entry_t;

static QueueHandle_t s_queue       = NULL;
static TaskHandle_t  s_task_handle = NULL;
static bool          s_initialized = false;
static volatile bool s_busy        = false;

/* ── HTTP upload ─────────────────────────────────────────────────── */

/**
 * @brief Upload a local file via HTTP POST
 */
static esp_err_t http_upload(const char *host, uint16_t port,
                             const char *device_name,
                             const char *remote_path, const char *local_path)
{
    char url[384];
    snprintf(url, sizeof(url), "http://%s:%u%s", host, port, remote_path);

    FILE *f = fopen(local_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", local_path);
        return ESP_FAIL;
    }

    struct stat st;
    if (stat(local_path, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat %s", local_path);
        fclose(f);
        return ESP_FAIL;
    }
    long file_size = st.st_size;

    if ((size_t)file_size > esp_get_free_heap_size()) {
        ESP_LOGE(TAG, "File too large for heap");
        fclose(f);
        return ESP_FAIL;
    }

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        fclose(f);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "X-Device-ID", device_name);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");

    esp_err_t open_err = esp_http_client_open(client, (int)file_size);
    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(open_err));
        esp_http_client_cleanup(client);
        fclose(f);
        return ESP_FAIL;
    }

    /* Stream file in 4KB chunks */
    uint8_t *chunk = malloc(4096);
    if (!chunk) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(f);
        return ESP_FAIL;
    }

    size_t total_written = 0;
    bool write_error = false;

    while (total_written < (size_t)file_size) {
        size_t to_read = 4096;
        if (total_written + to_read > (size_t)file_size) {
            to_read = (size_t)file_size - total_written;
        }
        size_t nread = fread(chunk, 1, to_read, f);
        if (nread == 0) break;

        int nwritten = esp_http_client_write(client, (const char *)chunk, (int)nread);
        if (nwritten < 0) {
            write_error = true;
            break;
        }
        total_written += (size_t)nwritten;
    }

    free(chunk);
    esp_http_client_close(client);

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    fclose(f);

    if (write_error) {
        ESP_LOGE(TAG, "HTTP write error");
        return ESP_FAIL;
    }

    if (status == 200) {
        ESP_LOGI(TAG, "HTTP upload success: %s (%ld bytes)", local_path, file_size);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "HTTP upload failed: status %d", status);
    return ESP_FAIL;
}

/* ── Upload task ────────────────────────────────────────────────── */

static void upload_task(void *arg)
{
    (void)arg;
    upload_entry_t entry;

    while (1) {
        /* Wait for queue item (blocks forever) */
        if (xQueueReceive(s_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_busy = true;

        const cam_config_t *cfg = config_get();

        /* Skip if no NAS host configured */
        if (strlen(cfg->nas_host) == 0) {
            ESP_LOGW(TAG, "No NAS host configured, skipping: %s", entry.filepath);
            s_busy = false;
            continue;
        }

        /* Extract filename for remote path */
        const char *filename = strrchr(entry.filepath, '/');
        filename = filename ? filename + 1 : entry.filepath;

        /* Build remote path: nas_path + "/" + filename */
        char remote_path[256];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation="
        if (cfg->nas_path[0] == '\0') {
            snprintf(remote_path, sizeof(remote_path), "/%s", filename);
        } else {
            snprintf(remote_path, sizeof(remote_path), "%s/%s", cfg->nas_path, filename);
        }
#pragma GCC diagnostic pop

        const char *proto_name = (cfg->nas_protocol == NAS_PROTOCOL_WEBDAV) ? "WebDAV" : "HTTP";
        ESP_LOGI(TAG, "Uploading %s via %s", entry.filepath, proto_name);

        bool success = false;

        for (int retry = 0; retry < MAX_RETRIES && !success; retry++) {
            if (retry > 0) {
                ESP_LOGW(TAG, "Retry %d/%d in %d ms", retry + 1, MAX_RETRIES, RETRY_DELAY_MS);
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            }

            if (cfg->nas_protocol == NAS_PROTOCOL_WEBDAV) {
                success = (webdav_upload(cfg->nas_host, cfg->nas_port,
                                         cfg->nas_user, cfg->nas_pass,
                                         remote_path, entry.filepath) == ESP_OK);
            } else {
                success = (http_upload(cfg->nas_host, cfg->nas_port,
                                       cfg->device_name,
                                       remote_path, entry.filepath) == ESP_OK);
            }
        }

        if (success) {
            ESP_LOGI(TAG, "Upload complete: %s", entry.filepath);
            /* TODO: optionally delete local file */
        } else {
            ESP_LOGE(TAG, "Upload failed after %d retries: %s", MAX_RETRIES, entry.filepath);
        }

        s_busy = false;
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t nas_uploader_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(UPLOAD_QUEUE_SIZE, sizeof(upload_entry_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create upload queue");
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(upload_task, "nas_upload",
                                             6144, NULL, 3,
                                             &s_task_handle, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create upload task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "NAS uploader initialized (queue %d, core 1)", UPLOAD_QUEUE_SIZE);
    return ESP_OK;
}

esp_err_t nas_uploader_enqueue(const char *filepath)
{
    if (!s_initialized || !s_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    upload_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.filepath, filepath, sizeof(entry.filepath) - 1);

    if (xQueueSend(s_queue, &entry, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Upload queue full, dropping: %s", filepath);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Enqueued: %s", filepath);
    return ESP_OK;
}

int nas_uploader_get_pending_count(void)
{
    if (!s_queue) return 0;
    return (int)uxQueueMessagesWaiting(s_queue);
}

bool nas_uploader_is_busy(void)
{
    return s_busy;
}
