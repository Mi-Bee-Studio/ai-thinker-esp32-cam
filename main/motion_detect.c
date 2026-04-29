/*
 * motion_detect.c - Frame-difference motion detection for AI_Thinker ESP32-CAM
 *
 * Algorithm (JPEG byte-sampling heuristic):
 *   1. Capture frame A, sample every Nth JPEG byte into a static buffer
 *   2. Return frame A to camera driver
 *   3. Wait ~500ms for scene change
 *   4. Capture frame B, compare its bytes against saved samples
 *   5. If changed bytes exceed threshold → motion detected
 *   6. On trigger: capture fresh frame, save to SD card, enter cooldown
 *
 * Rationale: OV2640 outputs compressed JPEG — pixel-level comparison would
 * require a full JPEG decode (too heavy for ESP32). Comparing raw JPEG bytes
 * at a coarse sample rate is a lightweight heuristic that works well for
 * detecting scene changes between consecutive frames.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motion_detect.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "storage_manager.h"
#include "time_sync.h"

static const char *TAG = "motion_detect";

/* ---- Tuning constants ---- */
#define SAMPLE_STEP            10    /* Sample every 10th JPEG byte */
#define PIXEL_DELTA            20    /* Byte difference threshold for "changed" */

#define MOTION_TASK_PRIORITY   5
#define MOTION_TASK_STACK_SIZE 8192

#define CAPTURE_INTERVAL_MS    500   /* ~2 FPS detection loop */
#define STOP_WAIT_MS           2000  /* Max wait for task exit on stop */

/* ---- Static state ---- */
static TaskHandle_t s_motion_task_handle = NULL;
static volatile bool s_running = false;
static bool s_in_cooldown = false;
static int64_t s_cooldown_start_us = 0;

/* ---- Internal helpers ---- */

/**
 * @brief Check if cooldown period has expired
 * @return true if cooldown is still active, false if expired or not in cooldown
 */
static bool is_in_cooldown(void)
{
    if (!s_in_cooldown) {
        return false;
    }

    const cam_config_t *cfg = config_get();
    int64_t elapsed_us = esp_timer_get_time() - s_cooldown_start_us;
    int64_t cooldown_us = (int64_t)cfg->motion_cooldown * 1000000LL;

    if (elapsed_us >= cooldown_us) {
        s_in_cooldown = false;
        ESP_LOGD(TAG, "Cooldown expired (%lld us elapsed, %lld us required)",
                 (long long)elapsed_us, (long long)cooldown_us);
        return false;
    }

    return true;
}

/**
 * @brief Handle motion detected event — save photo to SD card
 *
 * Captures a fresh frame, generates a timestamped filename, and saves
 * via storage_save_photo(). Only saves if SD card is available.
 */
static void handle_motion_event(void)
{
    ESP_LOGI(TAG, "Motion detected!");

    if (!storage_is_available()) {
        ESP_LOGW(TAG, "SD card not available, skipping photo save");
        return;
    }

    /* Capture a fresh frame for saving */
    camera_fb_t *fb = NULL;
    if (camera_capture(&fb) != ESP_OK || fb == NULL) {
        ESP_LOGW(TAG, "Failed to capture frame for photo save");
        return;
    }

    /* Generate timestamped filename */
    const char *timestamp = time_sync_get_str();
    char filename[64];
    snprintf(filename, sizeof(filename), "motion_%s.jpg",
             (timestamp != NULL) ? timestamp : "unknown");

    esp_err_t err = storage_save_photo(fb, filename);
    camera_return_fb(fb);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Motion photo saved: %s", filename);
    } else {
        ESP_LOGE(TAG, "Failed to save motion photo: %s", esp_err_to_name(err));
    }

    /* Enter cooldown */
    s_in_cooldown = true;
    s_cooldown_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Entering cooldown (%u seconds)", config_get()->motion_cooldown);
}

/**
 * @brief FreeRTOS task entry point for continuous motion detection
 */
static void motion_detection_task(void *arg)
{
    ESP_LOGI(TAG, "Motion detection task started");

    while (s_running) {
        /* Skip if camera not ready */
        if (!camera_is_initialized()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const cam_config_t *cfg = config_get();

        /*
         * Single-buffer-safe capture sequence:
         * With fb_count=1 we cannot hold two frame buffers simultaneously.
         * Strategy: sample bytes from fb_a into a static array, return fb_a,
         * then capture fb_b and compare.
         */

        /* Capture reference frame */
        camera_fb_t *fb_a = NULL;
        if (camera_capture(&fb_a) != ESP_OK || fb_a == NULL) {
            ESP_LOGW(TAG, "Failed to capture reference frame");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Sample bytes from fb_a for later comparison */
        size_t a_len = fb_a->len;
        size_t sample_count = (a_len + SAMPLE_STEP - 1) / SAMPLE_STEP;
        uint8_t *samples_a = (uint8_t *)malloc(sample_count);
        if (samples_a == NULL) {
            ESP_LOGW(TAG, "Failed to allocate sample buffer (%u bytes)",
                     (unsigned)sample_count);
            camera_return_fb(fb_a);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        for (size_t i = 0, j = 0; i < a_len && j < sample_count; i += SAMPLE_STEP, j++) {
            samples_a[j] = fb_a->buf[i];
        }

        /* Return fb_a immediately — free the frame buffer */
        camera_return_fb(fb_a);
        fb_a = NULL;

        /* Wait between captures for detectable scene change */
        vTaskDelay(pdMS_TO_TICKS(CAPTURE_INTERVAL_MS));

        /* Capture comparison frame */
        camera_fb_t *fb_b = NULL;
        if (camera_capture(&fb_b) != ESP_OK || fb_b == NULL) {
            ESP_LOGW(TAG, "Failed to capture comparison frame");
            free(samples_a);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Compare saved samples against fb_b */
        size_t min_len = (a_len < fb_b->len) ? a_len : fb_b->len;
        size_t total = 0;
        size_t changed = 0;
        for (size_t i = 0, j = 0; i < min_len && j < sample_count; i += SAMPLE_STEP, j++) {
            total++;
            int diff = (int)samples_a[j] - (int)fb_b->buf[i];
            if (diff < 0) diff = -diff;
            if (diff > PIXEL_DELTA) {
                changed++;
            }
        }

        bool motion = false;
        if (total > 0) {
            uint8_t percent = (uint8_t)((changed * 100) / total);
            motion = (percent >= cfg->motion_threshold);
            ESP_LOGD(TAG, "Frame diff: %u/%u = %u%% (threshold=%u%%)",
                     (unsigned)changed, (unsigned)total, percent,
                     cfg->motion_threshold);
        }

        /* Free fb_b and sample buffer */
        camera_return_fb(fb_b);
        free(samples_a);

        /* Trigger action on motion (if not in cooldown) */
        if (motion && !is_in_cooldown()) {
            handle_motion_event();
        }

        /* Brief yield to prevent watchdog trigger */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Motion detection task exiting");
    s_motion_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

esp_err_t motion_detect_init(void)
{
    s_running = false;
    s_in_cooldown = false;
    s_cooldown_start_us = 0;
    s_motion_task_handle = NULL;
    ESP_LOGI(TAG, "Motion detection module initialized");
    return ESP_OK;
}

esp_err_t motion_detect_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Motion detection already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    s_in_cooldown = false;
    s_cooldown_start_us = 0;

    BaseType_t ret = xTaskCreate(
        motion_detection_task,
        "motion_detect",
        MOTION_TASK_STACK_SIZE,
        NULL,
        MOTION_TASK_PRIORITY,
        &s_motion_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motion detection task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Motion detection started (threshold=%u%%, cooldown=%us)",
             config_get()->motion_threshold, config_get()->motion_cooldown);
    return ESP_OK;
}

esp_err_t motion_detect_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping motion detection...");
    s_running = false;

    /* Wait for task to exit */
    TickType_t timeout = pdMS_TO_TICKS(STOP_WAIT_MS);
    while (s_motion_task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout -= pdMS_TO_TICKS(100);
    }

    if (s_motion_task_handle != NULL) {
        ESP_LOGW(TAG, "Motion task did not exit in time, force deleting");
        vTaskDelete(s_motion_task_handle);
        s_motion_task_handle = NULL;
    }

    s_in_cooldown = false;
    ESP_LOGI(TAG, "Motion detection stopped");
    return ESP_OK;
}

bool motion_detect_is_running(void)
{
    return s_running;
}
