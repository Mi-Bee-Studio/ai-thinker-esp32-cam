#include "webdav_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "webdav";

#define UPLOAD_CHUNK_SIZE  4096
#define MAX_RETRIES        3
#define RETRY_DELAY_MS     2000

/**
 * @brief Set Basic Authorization header on esp_http_client
 */
static void set_auth_header(esp_http_client_handle_t client,
                            const char *user, const char *pass)
{
    char credentials[96];
    snprintf(credentials, sizeof(credentials), "%s:%s", user, pass);

    size_t encoded_len = 0;
    char encoded[128];
    mbedtls_base64_encode((unsigned char *)encoded, sizeof(encoded), &encoded_len,
                          (const unsigned char *)credentials, strlen(credentials));
    encoded[encoded_len] = '\0';

    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Basic %s", encoded);
    esp_http_client_set_header(client, "Authorization", auth_header);
}

esp_err_t webdav_upload(const char *host, uint16_t port,
                        const char *user, const char *pass,
                        const char *remote_path, const char *local_path)
{
    /* Build URL: http://host:port/remote_path */
    char url[384];
    snprintf(url, sizeof(url), "http://%s:%u%s", host, port, remote_path);

    /* Open local file */
    FILE *f = fopen(local_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open local file: %s", local_path);
        return ESP_FAIL;
    }

    /* Get file size */
    struct stat st;
    if (stat(local_path, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat local file: %s", local_path);
        fclose(f);
        return ESP_FAIL;
    }
    long file_size = st.st_size;

    /* Check heap before loading */
    if ((size_t)file_size > esp_get_free_heap_size()) {
        ESP_LOGE(TAG, "File %ld bytes exceeds free heap %u", file_size, esp_get_free_heap_size());
        fclose(f);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Uploading %s (%ld bytes) -> %s", local_path, file_size, url);

    esp_err_t result = ESP_FAIL;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "Retry %d/%d after %d ms", attempt + 1, MAX_RETRIES, RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            fseek(f, 0, SEEK_SET);
        }

        esp_http_client_config_t http_cfg = {
            .url = url,
            .method = HTTP_METHOD_PUT,
            .timeout_ms = 60000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            ESP_LOGE(TAG, "Failed to init HTTP client");
            continue;
        }

        set_auth_header(client, user, pass);
        esp_http_client_set_header(client, "Content-Type", "image/jpeg");

        /* Open connection with content length */
        esp_err_t open_err = esp_http_client_open(client, (int)file_size);
        if (open_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(open_err));
            esp_http_client_cleanup(client);
            continue;
        }

        /* Stream file in chunks */
        uint8_t *chunk = malloc(UPLOAD_CHUNK_SIZE);
        if (!chunk) {
            ESP_LOGE(TAG, "OOM alloc chunk buffer");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            continue;
        }

        size_t total_written = 0;
        bool write_error = false;

        while (total_written < (size_t)file_size) {
            size_t to_read = UPLOAD_CHUNK_SIZE;
            if (total_written + to_read > (size_t)file_size) {
                to_read = (size_t)file_size - total_written;
            }

            size_t nread = fread(chunk, 1, to_read, f);
            if (nread == 0) {
                ESP_LOGE(TAG, "fread error at offset %zu", total_written);
                write_error = true;
                break;
            }

            int nwritten = esp_http_client_write(client, (const char *)chunk, (int)nread);
            if (nwritten < 0) {
                ESP_LOGE(TAG, "HTTP write error at offset %zu", total_written);
                write_error = true;
                break;
            }

            total_written += (size_t)nwritten;
        }

        free(chunk);
        esp_http_client_close(client);

        if (write_error) {
            esp_http_client_cleanup(client);
            continue;
        }

        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        ESP_LOGI(TAG, "Upload HTTP %d (%zu/%ld bytes)", status, total_written, file_size);

        if (status == 201 || status == 200 || status == 204) {
            ESP_LOGI(TAG, "WebDAV upload success: %s", remote_path);
            result = ESP_OK;
            break;
        }

        ESP_LOGW(TAG, "Upload failed with HTTP %d, attempt %d/%d",
                 status, attempt + 1, MAX_RETRIES);
    }

    fclose(f);
    return result;
}
