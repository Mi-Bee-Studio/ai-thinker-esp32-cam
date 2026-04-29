/**
 * @file mjpeg_streamer.c
 * @brief MJPEG real-time video streaming implementation.
 *
 * Captures camera frames via camera_driver and pushes them as a
 * multipart/x-mixed-replace MJPEG stream to HTTP clients.
 * Maximum 2 concurrent clients to limit PSRAM usage.
 * Target 15 FPS with 4 KB chunked transfer.
 */

#include "mjpeg_streamer.h"
#include "camera_driver.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mjpeg_streamer";

/* ---------- Stream protocol constants ---------- */

#define BOUNDARY            "frame"
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace; boundary=" BOUNDARY
#define STREAM_BOUNDARY     "\r\n--" BOUNDARY "\r\n" \
                           "Content-Type: image/jpeg\r\n" \
                           "Content-Length: %u\r\n\r\n"

#define MAX_STREAM_CLIENTS  2
#define CHUNK_SIZE          4096
#define STREAM_FPS          15
#define STREAM_FRAME_DELAY  pdMS_TO_TICKS(1000 / STREAM_FPS)

/* ---------- Module state ---------- */

static SemaphoreHandle_t s_client_sem = NULL;

/* ---------- Internal helpers ---------- */

static int get_connected_count(void)
{
    if (s_client_sem == NULL) {
        return 0;
    }
    /* Semaphore starts at MAX, each client takes one slot */
    return MAX_STREAM_CLIENTS - (int)uxSemaphoreGetCount(s_client_sem);
}

/* ---------- HTTP handler ---------- */

esp_err_t mjpeg_streamer_http_handler(httpd_req_t *req)
{
    /* --- Client limit check via counting semaphore --- */
    if (xSemaphoreTake(s_client_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Max stream connections reached", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "Stream rejected: max clients (%d) reached", MAX_STREAM_CLIENTS);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stream client connected (total %d/%d)",
             get_connected_count(), MAX_STREAM_CLIENTS);

    /* --- Response headers --- */
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* --- Streaming loop --- */
    camera_fb_t *fb = NULL;
    char part_hdr[128];

    while (1) {
        /* Capture frame with retry (buffer may be temporarily unavailable) */
        esp_err_t ret;
        int retries;
        for (retries = 0; retries < 3; retries++) {
            ret = camera_capture(&fb);
            if (ret == ESP_OK) {
                break;
            }
            ESP_LOGD(TAG, "Capture retry %d/3", retries + 1);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Camera capture failed after %d retries, ending stream", retries);
            break;
        }

        /* Build multipart header for this frame */
        int hdrlen = snprintf(part_hdr, sizeof(part_hdr),
                              STREAM_BOUNDARY, (unsigned int)fb->len);

        /* Send part header */
        if (httpd_resp_send_chunk(req, part_hdr, hdrlen) != ESP_OK) {
            camera_return_fb(fb);
            break;
        }

        /* Send JPEG body in 4 KB chunks */
        size_t remaining = fb->len;
        const uint8_t *ptr = fb->buf;
        bool send_ok = true;

        while (remaining > 0) {
            size_t chunk = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
            if (httpd_resp_send_chunk(req, (const char *)ptr, chunk) != ESP_OK) {
                send_ok = false;
                break;
            }
            ptr += chunk;
            remaining -= chunk;
        }

        /* Send trailing CRLF to close the part */
        if (send_ok) {
            if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
                send_ok = false;
            }
        }

        /* Return frame buffer regardless of send outcome */
        camera_return_fb(fb);

        if (!send_ok) {
            break;
        }

        /* Frame delay: target 15 FPS (~66 ms) */
        vTaskDelay(STREAM_FRAME_DELAY);
    }

    /* --- Cleanup --- */
    httpd_resp_send_chunk(req, NULL, 0);  /* end chunked response */
    xSemaphoreGive(s_client_sem);
    ESP_LOGI(TAG, "Stream client disconnected (total %d)", get_connected_count());
    return ESP_OK;
}

/* ---------- Public API ---------- */

esp_err_t mjpeg_streamer_init(void)
{
    if (s_client_sem == NULL) {
        s_client_sem = xSemaphoreCreateCounting(MAX_STREAM_CLIENTS, MAX_STREAM_CLIENTS);
        if (s_client_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create client semaphore");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "MJPEG streamer initialized (max %d clients)", MAX_STREAM_CLIENTS);
    return ESP_OK;
}

esp_err_t mjpeg_streamer_register(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Cannot register: server handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    httpd_uri_t stream_uri = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = mjpeg_streamer_http_handler,
        .user_ctx = NULL,
    };

    esp_err_t ret = httpd_register_uri_handler(server, &stream_uri);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Registered /stream endpoint");
    } else if (ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
        ESP_LOGD(TAG, "/stream endpoint already registered, skipping");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to register /stream endpoint: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

void mjpeg_streamer_stop(void)
{
    if (s_client_sem != NULL) {
        vSemaphoreDelete(s_client_sem);
        s_client_sem = NULL;
    }
    ESP_LOGI(TAG, "MJPEG streamer stopped, clients reset");
}

int mjpeg_streamer_get_client_count(void)
{
    return get_connected_count();
}
