/**
 * frame_broker.c - Producer-consumer frame distribution
 *
 * See frame_broker.h for architecture overview.
 *
 * The producer task is the ONLY runtime caller of camera_capture().
 * It holds the camera frame buffer for < 1ms (memcpy to PSRAM), then
 * returns it immediately.  This eliminates the buffer-exhaustion deadlock
 * where MJPEG stream + motion detect + /capture would consume all
 * fb_count=2 camera buffers and stall the DMA engine.
 */

#include "frame_broker.h"
#include "camera_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sd_logger.h"
#include <string.h>

static const char *TAG = "frame_broker";

#define BROKER_FPS           15
#define BROKER_FRAME_DELAY   pdMS_TO_TICKS(1000 / BROKER_FPS)
#define BROKER_TASK_STACK    6144
#define BROKER_TASK_PRIO     5
#define DMA_STALL_WARN_THRESH 30  /* log warning after this many consecutive fails */

/* ---- Latest frame snapshot (producer writes, consumers read-copy) ---- */

typedef struct {
    uint8_t     *buf;       /* PSRAM, reused across frames (grows as needed) */
    size_t       len;       /* current frame byte length */
    size_t       cap;       /* allocated capacity of buf */
    pixformat_t  format;
    uint32_t     timestamp; /* esp_timer seconds at capture */
    bool         valid;     /* at least one frame captured */
} latest_frame_t;

static latest_frame_t s_latest = {0};
static SemaphoreHandle_t s_mutex = NULL;        /* protects s_latest */
static TaskHandle_t      s_producer_task = NULL;
static volatile bool     s_running = false;
static volatile uint32_t s_frame_count = 0;
static volatile uint32_t s_fail_count  = 0;

/* ---- Producer task ---- */

static void producer_task(void *arg)
{
    ESP_LOGI(TAG, "Producer task started (%u fps)", BROKER_FPS);

    while (s_running) {
        if (!camera_is_initialized()) {
            /* Camera not ready (reinit in progress or not started) */
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        camera_fb_t *fb = NULL;
        esp_err_t ret = camera_capture(&fb);
        if (ret != ESP_OK || fb == NULL) {
            s_fail_count++;
            if (s_fail_count == DMA_STALL_WARN_THRESH) {
                ESP_LOGE(TAG, "Camera capture failed %u times (DMA stall?)",
                         (unsigned)s_fail_count);
                sd_logf(SD_LOG_ERROR, "camera",
                        "frame_broker: %u consecutive capture failures (DMA stall suspected)",
                        (unsigned)s_fail_count);
            }
            vTaskDelay(BROKER_FRAME_DELAY);
            continue;
        }

        /* Update latest frame snapshot — quick memcpy under mutex */
        xSemaphoreTake(s_mutex, portMAX_DELAY);

        /* Grow buffer if needed (PSRAM) */
        if (s_latest.cap < fb->len) {
            if (s_latest.buf) {
                heap_caps_free(s_latest.buf);
                s_latest.buf = NULL;
                s_latest.cap = 0;
            }
            s_latest.buf = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
            if (s_latest.buf) {
                s_latest.cap = fb->len;
            }
        }

        if (s_latest.buf) {
            memcpy(s_latest.buf, fb->buf, fb->len);
            s_latest.len       = fb->len;
            s_latest.format    = fb->format;
            s_latest.timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
            s_latest.valid     = true;
        }

        xSemaphoreGive(s_mutex);

        /* Return camera buffer IMMEDIATELY — this is the whole point */
        camera_return_fb(fb);

        if (s_fail_count >= DMA_STALL_WARN_THRESH) {
            ESP_LOGI(TAG, "Capture recovered after %u failures", (unsigned)s_fail_count);
            sd_logf(SD_LOG_WARN, "camera",
                    "frame_broker: capture recovered after %u failures",
                    (unsigned)s_fail_count);
        }
        s_fail_count = 0;
        s_frame_count++;

        vTaskDelay(BROKER_FRAME_DELAY);
    }

    ESP_LOGI(TAG, "Producer task exiting");
    s_producer_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

esp_err_t frame_broker_init(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_running = true;
    s_frame_count = 0;
    s_fail_count = 0;
    s_latest.valid = false;

    BaseType_t ret = xTaskCreate(producer_task, "fb_broker",
                                 BROKER_TASK_STACK, NULL,
                                 BROKER_TASK_PRIO, &s_producer_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create producer task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Frame broker initialized");
    return ESP_OK;
}

esp_err_t frame_broker_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;

    /* Wait for task to exit */
    int waited = 0;
    while (s_producer_task != NULL && waited < 2000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }

    /* Free latest frame buffer */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_latest.buf) {
        heap_caps_free(s_latest.buf);
        s_latest.buf = NULL;
        s_latest.cap = 0;
        s_latest.len = 0;
        s_latest.valid = false;
    }
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Frame broker stopped");
    return ESP_OK;
}

bool frame_broker_is_running(void)
{
    return s_running;
}

esp_err_t frame_broker_get_copy(camera_fb_t **fb_out, uint32_t timeout_ms)
{
    if (!fb_out) {
        return ESP_ERR_INVALID_ARG;
    }
    *fb_out = NULL;

    if (!s_running) {
        return ESP_FAIL;
    }

    /* Wait for first frame if needed */
    uint32_t waited = 0;
    for (;;) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        bool ready = s_latest.valid;
        xSemaphoreGive(s_mutex);
        if (ready) break;
        if (!s_running) return ESP_FAIL;
        if (waited >= timeout_ms) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }

    /* Allocate output frame struct (small, internal RAM) */
    camera_fb_t *fb = (camera_fb_t *)calloc(1, sizeof(camera_fb_t));
    if (!fb) {
        return ESP_ERR_NO_MEM;
    }

    /* Copy latest frame under mutex (malloc + memcpy ~20KB, < 1ms) */
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    fb->buf = (uint8_t *)heap_caps_malloc(s_latest.len, MALLOC_CAP_SPIRAM);
    if (!fb->buf) {
        xSemaphoreGive(s_mutex);
        free(fb);
        return ESP_ERR_NO_MEM;
    }

    memcpy(fb->buf, s_latest.buf, s_latest.len);
    fb->len    = s_latest.len;
    fb->format = s_latest.format;
    /* width/height not tracked — no consumer uses them;
     * camera_fb_t zeroed by calloc so they read as 0 */

    xSemaphoreGive(s_mutex);

    *fb_out = fb;
    return ESP_OK;
}

void frame_broker_free(camera_fb_t *fb)
{
    if (!fb) return;
    if (fb->buf) {
        heap_caps_free(fb->buf);
    }
    free(fb);
}

uint32_t frame_broker_get_frame_count(void)
{
    return s_frame_count;
}

uint32_t frame_broker_get_fail_count(void)
{
    return s_fail_count;
}
