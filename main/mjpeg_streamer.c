/**
 * @file mjpeg_streamer.c
 * @brief MJPEG real-time video streaming implementation.
 *
 * Captures camera frames via camera_driver and pushes them as a
 * multipart/x-mixed-replace MJPEG stream to HTTP clients.
 * Uses async handler to avoid blocking the single-threaded HTTP server.
 * Maximum 2 concurrent clients to limit PSRAM usage.
 * Target 15 FPS with 4 KB chunked transfer.
 */

#include "mjpeg_streamer.h"
#include "camera_driver.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
#define STREAM_TASK_STACK   8192
#define STREAM_TASK_PRIO    4

/* ---------- Module state ---------- */

static SemaphoreHandle_t s_client_sem = NULL;

/* ---------- Per-client stream context ---------- */

typedef struct {
    httpd_req_t *req;       /* Async request handle */
    bool         active;    /* Set to false to signal task to stop */
} stream_ctx_t;

static stream_ctx_t s_clients[MAX_STREAM_CLIENTS];

/* ---------- Internal helpers ---------- */

static int get_connected_count(void)
{
    if (s_client_sem == NULL) {
        return 0;
    }
    /* Semaphore starts at MAX, each client takes one slot */
    return MAX_STREAM_CLIENTS - (int)uxSemaphoreGetCount(s_client_sem);
}

/* Find a free client slot, returns index or -1 */
static int alloc_client_slot(void)
{
    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (!s_clients[i].active && s_clients[i].req == NULL) {
            return i;
        }
    }
    return -1;
}

/* ---------- Stream task (runs per-client, independent of HTTP server) ---------- */

static void stream_task_fn(void *arg)
{
    stream_ctx_t *ctx = (stream_ctx_t *)arg;
    httpd_req_t *req = ctx->req;
    char part_hdr[128];
    int fail_streak = 0;

    ESP_LOGI(TAG, "Stream task started (total %d/%d)",
             get_connected_count(), MAX_STREAM_CLIENTS);

    while (ctx->active) {
        /* Capture frame with retry */
        camera_fb_t *fb = NULL;
        esp_err_t ret;
        int retries;
        for (retries = 0; retries < 3; retries++) {
            ret = camera_capture(&fb);
            if (ret == ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (ret != ESP_OK) {
            fail_streak++;
            if (fail_streak >= 10) {
                ESP_LOGE(TAG, "Too many capture failures (%d), ending stream", fail_streak);
                break;
            }
            vTaskDelay(STREAM_FRAME_DELAY);
            continue;
        }
        fail_streak = 0;

        /* Build multipart header */
        int hdrlen = snprintf(part_hdr, sizeof(part_hdr),
                              STREAM_BOUNDARY, (unsigned int)fb->len);

        /* Send part header */
        if (httpd_resp_send_chunk(req, part_hdr, hdrlen) != ESP_OK) {
            camera_return_fb(fb);
            break;
        }

        /* Send JPEG body in chunks */
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

        /* Trailing CRLF */
        if (send_ok) {
            if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
                send_ok = false;
            }
        }

        camera_return_fb(fb);

        if (!send_ok) break;

        vTaskDelay(STREAM_FRAME_DELAY);
    }

    /* Cleanup */
    httpd_resp_send_chunk(req, NULL, 0);  /* end chunked response */
    httpd_req_async_handler_complete(req);
    ctx->req = NULL;
    ctx->active = false;
    xSemaphoreGive(s_client_sem);

    ESP_LOGI(TAG, "Stream task exiting (total %d)", get_connected_count());
    vTaskDelete(NULL);
}

/* ---------- HTTP handler ---------- */

esp_err_t mjpeg_streamer_http_handler(httpd_req_t *req)
{
    /* Client limit check */
    if (xSemaphoreTake(s_client_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Max stream connections reached", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "Stream rejected: max clients (%d) reached", MAX_STREAM_CLIENTS);
        return ESP_FAIL;
    }

    /* Find a free slot */
    int slot = alloc_client_slot();
    if (slot < 0) {
        xSemaphoreGive(s_client_sem);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "No stream slots", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    /* Set response headers before detaching */
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");

    /* Detach request from HTTP server thread (async handler) */
    httpd_req_t *async_req = NULL;
    if (httpd_req_async_handler_begin(req, &async_req) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin async handler");
        xSemaphoreGive(s_client_sem);
        return ESP_FAIL;
    }

    /* Fill client slot */
    s_clients[slot].req = async_req;
    s_clients[slot].active = true;

    /* Launch stream task */
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "mjpeg_%d", slot);
    BaseType_t tc = xTaskCreate(stream_task_fn, task_name,
                                STREAM_TASK_STACK, &s_clients[slot],
                                STREAM_TASK_PRIO, NULL);
    if (tc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stream task");
        s_clients[slot].req = NULL;
        s_clients[slot].active = false;
        httpd_req_async_handler_complete(async_req);
        xSemaphoreGive(s_client_sem);
        return ESP_FAIL;
    }

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
    /* Initialize client slots */
    memset(s_clients, 0, sizeof(s_clients));
    ESP_LOGI(TAG, "MJPEG streamer initialized (max %d clients, async mode)", MAX_STREAM_CLIENTS);
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
        ESP_LOGI(TAG, "Registered /stream endpoint (async)");
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
    /* Signal all active clients to stop */
    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (s_clients[i].active) {
            s_clients[i].active = false;
        }
    }
    /* Give time for tasks to exit */
    vTaskDelay(pdMS_TO_TICKS(200));
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
