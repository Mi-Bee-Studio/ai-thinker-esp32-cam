/**
 * WebDAV Client — HTTP PUT upload with Basic authentication.
 *
 * Uses esp_http_client to perform a PUT request, streaming the file
 * in 4 KB chunks.  A 201 Created or 204 No Content response is
 * considered success.
 */

#include "webdav_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "webdav";

/* ── helpers ─────────────────────────────────────────────────────── */

/**
 * Build a "Basic <base64(user:pass)>" string into dst.
 * Returns dst for convenience.
 */
static char *make_basic_auth(char *dst, size_t dst_size,
                             const char *user, const char *pass)
{
    if (!user || !user[0]) {
        dst[0] = '\0';
        return dst;
    }

    char plain[130];
    snprintf(plain, sizeof(plain), "%s:%s", user, pass ? pass : "");

    size_t plain_len = strlen(plain);
    size_t b64_needed = 0;
    mbedtls_base64_encode(NULL, 0, &b64_needed, (const unsigned char *)plain, plain_len);

    if (b64_needed + 6 >= dst_size) {
        dst[0] = '\0';
        return dst;
    }

    memcpy(dst, "Basic ", 6);
    size_t out_len = 0;
    mbedtls_base64_encode((unsigned char *)dst + 6, dst_size - 6, &out_len,
                   (const unsigned char *)plain, plain_len);
    dst[6 + out_len] = '\0';
    return dst;
}

/* ── public API ──────────────────────────────────────────────────── */

esp_err_t webdav_upload(const char *host, uint16_t port,
                        const char *user, const char *pass,
                        const char *remote, const char *local)
{
    if (!host || !remote || !local) return ESP_ERR_INVALID_ARG;

    /* Build URL */
    char url[384];
    snprintf(url, sizeof(url), "http://%s:%u%s", host, (unsigned)port, remote);

    /* Stat local file */
    struct stat st;
    if (stat(local, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat %s", local);
        return ESP_FAIL;
    }
    long file_size = st.st_size;

    if ((size_t)file_size > esp_get_free_heap_size()) {
        ESP_LOGE(TAG, "File too large for heap");
        return ESP_FAIL;
    }

    FILE *f = fopen(local, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", local);
        return ESP_FAIL;
    }

    /* Configure HTTP client */
    esp_http_client_config_t cfg = {
        .url        = url,
        .method     = HTTP_METHOD_PUT,
        .timeout_ms = 60000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        fclose(f);
        return ESP_FAIL;
    }

    /* Set headers */
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");

    char auth_buf[200];
    make_basic_auth(auth_buf, sizeof(auth_buf), user, pass);
    if (auth_buf[0]) {
        esp_http_client_set_header(client, "Authorization", auth_buf);
    }

    /* Open connection with Content-Length */
    esp_err_t err = esp_http_client_open(client, (int)file_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PUT open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fclose(f);
        return ESP_FAIL;
    }

    /* Stream file in 4 KB chunks */
    uint8_t *chunk = malloc(4096);
    if (!chunk) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(f);
        return ESP_FAIL;
    }

    size_t total_written = 0;
    bool write_error     = false;

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
        ESP_LOGE(TAG, "PUT write error for %s", local);
        return ESP_FAIL;
    }

    /* WebDAV success: 201 Created or 204 No Content */
    if (status == 201 || status == 204 || status == 200) {
        ESP_LOGI(TAG, "WebDAV upload OK: %s (%ld bytes, status %d)",
                 local, file_size, status);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WebDAV upload failed: status %d for %s", status, local);
    return ESP_FAIL;
}
