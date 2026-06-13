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

#include "webdav_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_task_wdt.h"
#include <ctype.h>

static const char *TAG = "webdav";

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Set HTTP client Basic authentication header
 * Base64-encode user:pass and set as Authorization header
 * @param client HTTP client handle
 * @param user Username
 * @param pass Password
 */
static void set_auth_header(esp_http_client_handle_t client, const char *user, const char *pass)
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

/**
 * @brief Build full WebDAV URL: cfg->url + remote_path
 * Caller must free returned buffer
 * @param cfg WebDAV connection configuration
 * @param remote_path Remote resource path
 * @return malloc'd URL string, caller must free
 */
static char *build_url(const webdav_config_t *cfg, const char *remote_path)
{
    /* URL-encode remote path (preserve '/' separators) */
    size_t raw_len = strlen(cfg->url) + strlen(remote_path) * 3 + 2;
    char *url = malloc(raw_len);
    if (!url) return NULL;

    char *dst = url;
    const char *src = remote_path;

    /* Copy base URL as-is */
    size_t base_len = strlen(cfg->url);
    memcpy(dst, cfg->url, base_len);
    dst += base_len;

    while (*src) {
        if (*src == '/' || *src == '-' || *src == '_' || *src == '.' || *src == '~' || isalnum((unsigned char)*src)) {
            *dst++ = *src;
        } else {
            dst += snprintf(dst, 4, "%%%02X", (unsigned char)*src);
        }
        src++;
    }
    *dst = '\0';
    return url;
}

/* ------------------------------------------------------------------ */
/*  webdav_exists  (HTTP HEAD)                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Check if remote resource exists via HTTP HEAD
 * @param cfg WebDAV connection configuration
 * @param remote_path Remote resource path
 * @return ESP_OK exists (200), ESP_ERR_NOT_FOUND not found (404), ESP_FAIL other error
 */
esp_err_t webdav_exists(const webdav_config_t *cfg, const char *remote_path)
{
    char *url = build_url(cfg, remote_path);
    if (!url) {
        ESP_LOGE(TAG, "OOM building URL");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(url);
        return ESP_FAIL;
    }

    set_auth_header(client, cfg->user, cfg->pass);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(url);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HEAD %s transport error: %s", remote_path, esp_err_to_name(err));
        return err;
    }

    if (status == 200) {
        ESP_LOGD(TAG, "HEAD %s -> 200 (exists)", remote_path);
        return ESP_OK;
    }

    if (status == 404) {
        ESP_LOGD(TAG, "HEAD %s -> 404 (not found)", remote_path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGW(TAG, "HEAD %s unexpected status %d", remote_path, status);
    return ESP_FAIL;
}

/* ------------------------------------------------------------------ */
/*  webdav_mkdir  (MKCOL via raw HTTP over TCP)                       */
/* ------------------------------------------------------------------ */

/**
 * ESP-IDF's esp_http_client doesn't natively support MKCOL.
 * We send a raw HTTP request over a TCP socket using esp_transport.
 */
#include "esp_transport_tcp.h"

/**
 * @brief Send raw HTTP request (supports non-standard methods like MKCOL)
 * Send HTTP request directly via TCP socket and parse response status line
 * @param host Target hostname
 * @param port Target port
 * @param method HTTP method (e.g., MKCOL)
 * @param path Request path
 * @param auth_header Authentication header value
 * @param out_status Output HTTP status code
 * @return ESP_OK success, ESP_FAIL TCP connection or parse failed
 */
static esp_err_t raw_http_request(const char *host, int port,
                                  const char *method, const char *path,
                                  const char *auth_header,
                                  int *out_status)
{
    esp_transport_handle_t tcp = esp_transport_tcp_init();
    if (!tcp) {
        ESP_LOGE(TAG, "Failed to init TCP transport");
        return ESP_FAIL;
    }

    esp_transport_tcp_set_keep_alive(tcp, &(esp_transport_keep_alive_t){
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    });

    /* Timeout 10 seconds */
    int sock = esp_transport_connect(tcp, host, port, 10000);
    if (sock < 0) {
        ESP_LOGE(TAG, "TCP connect to %s:%d failed", host, port);
        esp_transport_destroy(tcp);
        return ESP_FAIL;
    }

    /* Build HTTP request */
    char req[512];
    int req_len = snprintf(req, sizeof(req),
                           "%s %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Authorization: %s\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           method, path, host, port, auth_header);

    int written = esp_transport_write(tcp, req, req_len, 5000);
    if (written < 0) {
        ESP_LOGE(TAG, "TCP write failed");
        esp_transport_close(tcp);
        esp_transport_destroy(tcp);
        return ESP_FAIL;
    }

    /* Read response — we only need the status line */
    char resp[256] = {0};
    int rlen = esp_transport_read(tcp, resp, sizeof(resp) - 1, 5000);

    esp_transport_close(tcp);
    esp_transport_destroy(tcp);

    if (rlen <= 0) {
        ESP_LOGW(TAG, "No response for %s %s", method, path);
        return ESP_FAIL;
    }

    /* Parse "HTTP/1.x NNN ..." */
    int status = 0;
    if (sscanf(resp, "HTTP/%*d.%*d %d", &status) != 1) {
        ESP_LOGW(TAG, "Can't parse status from: %.80s", resp);
        return ESP_FAIL;
    }

    if (out_status) *out_status = status;
    ESP_LOGD(TAG, "%s %s -> %d", method, path, status);
    return ESP_OK;
}

/**
 * @brief Parse host/port from cfg->url
 * Returns malloc'd host, sets *port.
 * Caller must free host.
 * @param cfg WebDAV connection configuration
 * @param port Output parsed port number
 * @return malloc'd hostname string, caller must free
 */
static char *parse_host_port(const webdav_config_t *cfg, int *port)
{
    /* Expect "http://host:port" */
    const char *start = cfg->url;
    if (strncmp(start, "http://", 7) == 0) start += 7;

    const char *colon = strchr(start, ':');
    const char *slash = strchr(start, '/');
    const char *end = slash ? slash : start + strlen(start);

    char *host = NULL;
    if (colon && colon < end) {
        /* host:port */
        size_t hlen = colon - start;
        host = malloc(hlen + 1);
        memcpy(host, start, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
    } else {
        size_t hlen = end - start;
        host = malloc(hlen + 1);
        memcpy(host, start, hlen);
        host[hlen] = '\0';
        *port = 80;
    }
    return host;
}

/**
 * @brief Build Basic auth value string
 * Base64-encode user:pass
 * @param cfg WebDAV connection configuration
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
static void build_auth_value(const webdav_config_t *cfg, char *buf, size_t buf_size)
{
    char credentials[96];
    snprintf(credentials, sizeof(credentials), "%s:%s", cfg->user, cfg->pass);

    size_t encoded_len = 0;
    mbedtls_base64_encode((unsigned char *)buf, buf_size, &encoded_len,
                          (const unsigned char *)credentials, strlen(credentials));
    buf[encoded_len] = '\0';
}

/**
 * @brief Create remote directory via WebDAV MKCOL
 * 201=created, 405=exists, 200/204=some servers return these for existing dirs, all return ESP_OK
 * @param cfg WebDAV connection configuration
 * @param remote_dir Remote directory path
 * @return ESP_OK created or exists, ESP_FAIL failed
 */
esp_err_t webdav_mkdir(const webdav_config_t *cfg, const char *remote_dir)
{
    int port = 80;
    char *host = parse_host_port(cfg, &port);
    if (!host) return ESP_ERR_NO_MEM;

    char auth[128];
    build_auth_value(cfg, auth, sizeof(auth));

    /* Build auth header string "Basic xxxxx" */
    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Basic %s", auth);

    int status = 0;
    esp_err_t err = raw_http_request(host, port, "MKCOL", remote_dir, auth_header, &status);
    free(host);

    if (err != ESP_OK) return err;

    /* 201 = Created, 405 = Already exists, 301 = redirect (dir exists) */
    if (status == 201 || status == 405) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "MKCOL %s unexpected status %d", remote_dir, status);
    /* Some servers return 200 or 204 for existing dirs */
    if (status == 200 || status == 204) return ESP_OK;

    return ESP_FAIL;
}

/* ------------------------------------------------------------------ */
/*  webdav_mkdir_recursive                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Recursively create all directory levels in remote path
 * E.g., "/A/B/C" -> creates /A, /A/B, /A/B/C in order
 * @param cfg WebDAV connection configuration
 * @param path Remote directory path
 * @return ESP_OK all success, or last failed directory error
 */
esp_err_t webdav_mkdir_recursive(const webdav_config_t *cfg, const char *path)
{
    /* Work on a copy so we can NUL-terminate at each '/' */
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    esp_err_t last_err = ESP_OK;

    /* Walk through path components: /A/B/C -> create /A, /A/B, /A/B/C */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            esp_task_wdt_reset();
            ESP_LOGD(TAG, "mkdir_recursive: creating %s", tmp);
            esp_err_t err = webdav_mkdir(cfg, tmp);
            if (err != ESP_OK) last_err = err;
            *p = '/';
        }
    }

    /* Create final component */
    if (strlen(tmp) > 1) {
        esp_task_wdt_reset();
        ESP_LOGD(TAG, "mkdir_recursive: creating %s", tmp);
        esp_err_t err = webdav_mkdir(cfg, tmp);
        if (err != ESP_OK) last_err = err;
    }

    return last_err;
}

/* ------------------------------------------------------------------ */
/*  webdav_upload  (HTTP PUT with retry)                              */
/* ------------------------------------------------------------------ */

#define UPLOAD_CHUNK_SIZE  4096
#define MAX_RETRIES        3
#define INITIAL_DELAY_MS   1000

/**
 * @brief Upload local file to WebDAV remote path via HTTP PUT
 * Use 4KB chunk streaming, exponential backoff retry (max 3, delay 1s/2s/4s)
 * @param cfg WebDAV connection configuration
 * @param remote_path Remote file path
 * @param local_path Local file path
 * @return ESP_OK success (HTTP 200/201/204), ESP_FAIL upload failed
 */
esp_err_t webdav_upload(const webdav_config_t *cfg, const char *remote_path, const char *local_path)
{
    char *url = build_url(cfg, remote_path);
    if (!url) {
        ESP_LOGE(TAG, "OOM building URL");
        return ESP_ERR_NO_MEM;
    }

    /* Open local file */
    FILE *f = NULL;
    esp_err_t result = ESP_FAIL;

    f = fopen(local_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open local file: %s", local_path);
        result = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    /* Get file size */
    struct stat st;
    if (stat(local_path, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat local file: %s", local_path);
        goto cleanup;
    }
    long file_size = st.st_size;

    ESP_LOGI(TAG, "Uploading %s (%ld bytes) -> %s", local_path, file_size, remote_path);

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            int delay = INITIAL_DELAY_MS * (1 << (attempt - 1));  /* 1s, 2s, 4s */
            ESP_LOGW(TAG, "Retry %d/%d after %d ms", attempt + 1, MAX_RETRIES, delay);
            vTaskDelay(pdMS_TO_TICKS(delay));
            fseek(f, 0, SEEK_SET);  /* Reset file position */
        }

        /* Feed WDT: each retry blocks for up to timeout_ms on network I/O */
        esp_task_wdt_reset();
        esp_http_client_config_t http_cfg = {
            .url = url,
            .method = HTTP_METHOD_PUT,
            .timeout_ms = 20000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            ESP_LOGE(TAG, "Failed to init HTTP client");
            continue;
        }

        set_auth_header(client, cfg->user, cfg->pass);
        esp_http_client_set_header(client, "Content-Type", "video/avi");

        /* Open connection with content length */
        esp_err_t open_err = esp_http_client_open(client, (int)file_size);
        if (open_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(open_err));
            esp_http_client_cleanup(client);
            continue;
        }

        /* Stream file in chunks */
        uint8_t *buf = malloc(UPLOAD_CHUNK_SIZE);
        if (!buf) {
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

            size_t nread = fread(buf, 1, to_read, f);
            if (nread == 0) {
                ESP_LOGE(TAG, "fread error at offset %zu", total_written);
                write_error = true;
                break;
            }

            int nwritten = esp_http_client_write(client, (const char *)buf, (int)nread);
            if (nwritten < 0) {
                ESP_LOGE(TAG, "HTTP write error at offset %zu", total_written);
                write_error = true;
                break;
            }

            total_written += (size_t)nwritten;
        }

        free(buf);
        esp_http_client_close(client);

        if (write_error) {
            esp_http_client_cleanup(client);
            continue;
        }

        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        ESP_LOGI(TAG, "Upload HTTP %d (%zu / %ld bytes)", status, total_written, file_size);

        if (status == 201 || status == 200 || status == 204) {
            ESP_LOGI(TAG, "Upload success: %s", remote_path);
            result = ESP_OK;
            break;
        }

        ESP_LOGW(TAG, "Upload failed with HTTP %d, attempt %d/%d", status, attempt + 1, MAX_RETRIES);
    }

    cleanup:
    if (f) fclose(f);
    free(url);
    return result;
}