/**
 * frame_broker.c - Producer-consumer frame distribution
 *
 * See frame_broker.h for architecture overview.
 *
 * The producer task is the ONLY runtime caller of camera_capture().
 * It allocates a new refcounted frame in PSRAM and memcpy's the camera
 * data OUTSIDE any lock, then publishes it via a brief spinlock-protected
 * pointer swap.  Camera buffer is returned immediately.
 *
 * Consumers acquire a reference to the latest published frame under the
 * same brief spinlock, then copy it OUTSIDE the lock.  No memcpy or malloc
 * ever happens inside the critical section — the lock is held for
 * nanoseconds (pointer swap + refcount inc), eliminating the cross-core
 * lock contention that serialized producer and consumers under the old
 * mutex-protected memcpy design.
 */

#include "frame_broker.h"
#include "camera_driver.h"
#include "mjpeg_streamer.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_logger.h"
#include <string.h>

static const char *TAG = "frame_broker";

#define BROKER_FPS             15
#define BROKER_FRAME_DELAY     pdMS_TO_TICKS(1000 / BROKER_FPS)
#define BROKER_TASK_STACK      6144
#define BROKER_TASK_PRIO       5
/* No core pinning: let FreeRTOS scheduler distribute across both cores.
 * Pinning all tasks to Core 1 caused httpd starvation when the stream task's
 * blocking httpd_resp_send_chunk (slow WiFi) held session locks that prevented
 * the httpd main thread from processing other requests. */
#define BROKER_TASK_CORE       tskNO_AFFINITY
#define DMA_STALL_WARN_THRESH  30  /* log warning after this many consecutive fails */

/* Compute capture cadence from config fps (falls back to BROKER_FPS).
 * ESP32 camera has no hardware FPS register; this throttles producer captures. */
static TickType_t broker_frame_delay(void)
{
    uint8_t fps = config_get()->fps;
    if (fps == 0) fps = BROKER_FPS;
    /* When no stream clients are watching, slow to 2fps to cut CPU/WiFi/PSRAM
     * load — motion detection only needs ~2fps. Full rate resumes on connect. */
    if (mjpeg_streamer_get_client_count() == 0) {
        fps = 2;
    }
    return pdMS_TO_TICKS(1000 / fps);
}

/* ---- Refcounted immutable frame ---- */
/* Published by producer via pointer swap; acquired by consumers via brief
 * spinlock-protected retain.  Once published, the frame contents (buf/len/
 * format) are immutable — consumers read freely without additional locking.
 * The frame is freed only when the last reference is released. */

typedef struct {
    uint8_t     *buf;          /* PSRAM */
    size_t       len;
    pixformat_t  format;
    uint32_t     timestamp;    /* esp_timer seconds at capture */
    volatile uint32_t refcount;
} broker_frame_t;

/* Currently published frame. Read with a volatile peek for polling; acquired
 * (retain) only under s_lock.  32-bit aligned pointer reads are atomic on
 * Xtensa LX6, so the volatile peek never sees a torn pointer. */
static broker_frame_t * volatile s_current = NULL;

/* Spinlock protecting s_current swaps and refcount inc/dec.
 * Held for nanoseconds (pointer assign + integer op) — never during memcpy
 * or malloc.  Uses portMUX_TYPE (SMP spinlock) instead of a FreeRTOS mutex
 * because the critical section is too short to justify mutex overhead
 * (context save, priority inheritance). */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t      s_producer_task = NULL;
static volatile bool     s_running = false;
static volatile uint32_t s_frame_count = 0;
static volatile uint32_t s_fail_count  = 0;

/* Release a reference. If refcount drops to 0, free the frame.
 * The decrement is under s_lock (nanoseconds); the actual free happens
 * OUTSIDE the lock to avoid blocking other cores during heap_caps_free. */
static void frame_release(broker_frame_t *f)
{
    if (f == NULL) return;

    bool should_free = false;
    taskENTER_CRITICAL(&s_lock);
    if (f->refcount > 0) {
        f->refcount--;
        should_free = (f->refcount == 0);
    }
    taskEXIT_CRITICAL(&s_lock);

    if (should_free) {
        if (f->buf) {
            heap_caps_free(f->buf);
        }
        free(f);
    }
}

/* ---- Producer task ---- */

static void producer_task(void *arg)
{
    ESP_LOGI(TAG, "Producer task started on core %d (%u fps)",
             xPortGetCoreID(), BROKER_FPS);

    while (s_running) {
        /* Skip if camera not ready (reinit in progress or not started) */
        if (!camera_is_initialized()) {
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
            vTaskDelay(broker_frame_delay());
            continue;
        }

        /* ---- All expensive work OUTSIDE the lock ---- */

        /* Allocate new frame struct (small, internal RAM) */
        broker_frame_t *nf = (broker_frame_t *)calloc(1, sizeof(broker_frame_t));
        if (nf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate broker frame struct");
            camera_return_fb(fb);
            vTaskDelay(broker_frame_delay());
            continue;
        }

        /* Allocate PSRAM buffer for frame data */
        nf->buf = (uint8_t *)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
        if (nf->buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM frame buffer (%u bytes)",
                     (unsigned)fb->len);
            free(nf);
            camera_return_fb(fb);
            vTaskDelay(broker_frame_delay());
            continue;
        }

        /* memcpy camera data OUTSIDE the lock (~5-10ms for 20-70KB PSRAM) */
        memcpy(nf->buf, fb->buf, fb->len);
        nf->len       = fb->len;
        nf->format    = fb->format;
        nf->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
        nf->refcount  = 1;  /* publisher's reference (held via s_current) */

        /* ---- Publish: brief critical section (pointer swap only) ---- */
        broker_frame_t *old = NULL;
        taskENTER_CRITICAL(&s_lock);
        old = s_current;
        s_current = nf;
        taskEXIT_CRITICAL(&s_lock);

        /* Release old published frame OUTSIDE the lock.
         * If no consumers hold a ref (refcount was 1), it's freed immediately.
         * If consumers are mid-copy (refcount > 1), it stays alive until they
         * finish — no torn reads possible. */
        frame_release(old);

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

        vTaskDelay(broker_frame_delay());
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

    s_running = true;
    s_frame_count = 0;
    s_fail_count = 0;
    s_current = NULL;

    BaseType_t ret = xTaskCreatePinnedToCore(producer_task, "fb_broker",
                                              BROKER_TASK_STACK, NULL,
                                              BROKER_TASK_PRIO, &s_producer_task,
                                              BROKER_TASK_CORE);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create producer task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Frame broker initialized (core %d)",
             BROKER_TASK_CORE == tskNO_AFFINITY ? -1 : BROKER_TASK_CORE);
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

    /* Release the currently published frame.
     * Any consumers still mid-copy hold their own ref, so the frame stays
     * alive until they finish — this just drops the publisher's reference. */
    broker_frame_t *old = NULL;
    taskENTER_CRITICAL(&s_lock);
    old = s_current;
    s_current = NULL;
    taskEXIT_CRITICAL(&s_lock);
    frame_release(old);

    ESP_LOGI(TAG, "Frame broker stopped");
    return ESP_OK;
}

bool frame_broker_is_running(void)
{
    return s_running;
}

esp_err_t frame_broker_get_copy(camera_fb_t **fb_out, uint32_t timeout_ms)
{
    if (fb_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *fb_out = NULL;

    if (!s_running) {
        return ESP_FAIL;
    }

    /* Wait for first published frame (volatile peek — cheap poll, no lock) */
    uint32_t waited = 0;
    while (s_current == NULL) {
        if (!s_running) return ESP_FAIL;
        if (waited >= timeout_ms) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }

    /* Acquire a reference to the current frame (brief critical section).
     * This is the ONLY lock hold on the consumer side — pointer read +
     * refcount inc, nanoseconds.  The actual malloc + memcpy happens below,
     * OUTSIDE the lock. */
    broker_frame_t *ref = NULL;
    while (ref == NULL) {
        taskENTER_CRITICAL(&s_lock);
        ref = s_current;
        if (ref != NULL) {
            ref->refcount++;   /* retain under lock — atomic with pointer read */
        }
        taskEXIT_CRITICAL(&s_lock);

        if (ref != NULL) break;
        /* s_current was NULL (stop raced) — retry or bail */
        if (!s_running) return ESP_FAIL;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    /* ---- All expensive work OUTSIDE the lock ---- */

    /* Allocate output frame struct (small, internal RAM).
     * Reuses camera_fb_t so downstream functions (storage_save_photo,
     * flash_brightness_detect) need zero changes. */
    camera_fb_t *fb = (camera_fb_t *)calloc(1, sizeof(camera_fb_t));
    if (fb == NULL) {
        frame_release(ref);
        return ESP_ERR_NO_MEM;
    }

    /* Copy frame data OUTSIDE the lock (~5-10ms for 20-70KB PSRAM).
     * Safe because 'ref' holds a retained reference — the producer cannot
     * free or overwrite this buffer while we hold the ref. */
    fb->buf = (uint8_t *)heap_caps_malloc(ref->len, MALLOC_CAP_SPIRAM);
    if (fb->buf == NULL) {
        free(fb);
        frame_release(ref);
        return ESP_ERR_NO_MEM;
    }

    memcpy(fb->buf, ref->buf, ref->len);
    fb->len    = ref->len;
    fb->format = ref->format;
    /* width/height not tracked — no consumer uses them;
     * camera_fb_t zeroed by calloc so they read as 0 */

    /* Release our reference to the published frame */
    frame_release(ref);

    *fb_out = fb;
    return ESP_OK;
}

void frame_broker_free(camera_fb_t *fb)
{
    if (fb == NULL) return;
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
