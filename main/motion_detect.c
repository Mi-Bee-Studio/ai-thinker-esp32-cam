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
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motion_detect.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "storage_manager.h"
#include "time_sync.h"
#include "health_monitor.h"

static const char *TAG = "motion_detect";

/* ---- Tuning constants ---- */
#define SAMPLE_STEP            10    /* Sample every 10th JPEG byte */
#define PIXEL_DELTA            20    /* Byte difference threshold for "changed" (bright scene) */
#define PIXEL_DELTA_DARK       180   /* Higher threshold for dark scenes — filters JPEG noise */

#define MOTION_TASK_PRIORITY   5
#define MOTION_TASK_STACK_SIZE 8192

#define CAPTURE_INTERVAL_MS    500   /* ~2 FPS detection loop */
#define STOP_WAIT_MS           2000  /* Max wait for task exit on stop */

/* Auto flash constants — LEDC PWM driven */
#define FLASH_GPIO             4     /* AI-Thinker CAM flash LED */
#define FLASH_WARMUP_MS        200   /* Wait for camera to adapt after flash on */
#define FLASH_LEDC_TIMER       LEDC_TIMER_1   /* Timer 0 used by camera XCLK */
#define FLASH_LEDC_CHANNEL     LEDC_CHANNEL_1 /* Channel 0 used by camera XCLK */
#define FLASH_LEDC_SPEED       LEDC_LOW_SPEED_MODE
#define FLASH_PWM_FREQ         2000  /* 2 kHz */
#define FLASH_PWM_RES          LEDC_TIMER_8_BIT /* 0-255 duty range */
#define FLASH_PWM_DUTY         205   /* ~80% — safe for AI-Thinker (no current-limit resistor) */

/* ---- Static state ---- */
static TaskHandle_t s_motion_task_handle = NULL;
static volatile bool s_running = false;
static bool s_in_cooldown = false;
static int64_t s_cooldown_start_us = 0;
static bool s_flash_initialized = false;

/* Adaptive brightness baseline (EMA)
 * Tracks average bright-scene frame size using signed arithmetic.
 * Dark = current frame < baseline * DARK_RATIO_THRESH.
 * Baseline only updates on bright frames to avoid dark-scene drift. */
#define DARK_RATIO_THRESH        0.70  /* Frame < 70% of baseline = dark */
#define BASELINE_EMA_ALPHA       8     /* EMA smoothing factor */
static int32_t s_brightness_baseline = 0;  /* 0 = not calibrated yet */

/* ---- Internal helpers ---- */

/**
 * @brief Initialize flash LED using LEDC PWM
 *
 * AI-Thinker ESP32-CAM flash LED has no current-limiting resistor.
 * Using PWM at ~80% duty prevents burnout (Prusa production firmware approach).
 * Uses Timer 1 / Channel 1 to avoid conflict with camera XCLK (Timer 0 / Channel 0).
 */
static void flash_led_init(void)
{
    if (s_flash_initialized) return;

    /* Configure LEDC timer */
    ledc_timer_config_t timer_conf = {
        .speed_mode      = FLASH_LEDC_SPEED,
        .duty_resolution = FLASH_PWM_RES,
        .timer_num       = FLASH_LEDC_TIMER,
        .freq_hz         = FLASH_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash LEDC timer config failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Configure LEDC channel on GPIO4 */
    ledc_channel_config_t ch_conf = {
        .gpio_num   = FLASH_GPIO,
        .speed_mode = FLASH_LEDC_SPEED,
        .channel    = FLASH_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = FLASH_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash LEDC channel config failed: %s", esp_err_to_name(ret));
        return;
    }

    s_flash_initialized = true;
    ESP_LOGI(TAG, "Flash LED initialized (LEDC PWM on GPIO%d, %d%% duty)", FLASH_GPIO,
             (FLASH_PWM_DUTY * 100 + 127) / 255);
}

/**
 * @brief Check if scene is dark based on JPEG frame size vs EMA baseline
 * @param jpeg_len Current frame JPEG size in bytes
 * @return true if frame is significantly smaller than running baseline
 *
 * Uses int32_t signed arithmetic for EMA to avoid unsigned underflow.
 * Baseline only updates on bright frames to avoid dark-scene drift.
 */
static bool is_scene_dark(size_t jpeg_len)
{
    if (s_brightness_baseline <= 0) {
        s_brightness_baseline = (int32_t)jpeg_len;
        ESP_LOGI(TAG, "Brightness baseline initialized: %d bytes", s_brightness_baseline);
        return false;
    }

    int32_t thresh = (int32_t)(s_brightness_baseline * DARK_RATIO_THRESH);
    bool dark = (int32_t)jpeg_len < thresh;

    if (!dark) {
        /* Signed EMA: baseline += (frame - baseline) / alpha
         * Using int32_t ensures no unsigned underflow. */
        s_brightness_baseline += ((int32_t)jpeg_len - s_brightness_baseline) / BASELINE_EMA_ALPHA;
    }

    return dark;
}

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
 * If flash was used for detection, recaptures with flash for a clean photo.
 */
static void handle_motion_event(bool dark_scene)
{
    ESP_LOGI(TAG, "Motion detected!%s (scene %s)", dark_scene ? " (auto-flash)" : "", dark_scene ? "DARK" : "bright");
    health_monitor_incr_motion_events();

    if (!storage_is_available()) {
        ESP_LOGW(TAG, "SD card not available, skipping photo save");
        return;
    }

    /* If scene is dark, capture with flash ON for a usable photo */
    camera_fb_t *fb = NULL;
    if (dark_scene) {
        ESP_LOGI(TAG, "Dark scene detected, enabling flash for photo");
        ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, FLASH_PWM_DUTY);
        ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(FLASH_WARMUP_MS));
        if (camera_capture(&fb) != ESP_OK || fb == NULL) {
            ESP_LOGW(TAG, "Failed to capture with flash");
        }
        ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, 0);
        ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
        ESP_LOGI(TAG, "Flash OFF");
    }

    /* If no flash or flash recapture failed, capture normally */
    if (fb == NULL) {
        if (camera_capture(&fb) != ESP_OK || fb == NULL) {
            ESP_LOGW(TAG, "Failed to capture frame for photo save");
            return;
        }
    }

    /* Generate timestamped filename (FAT-safe: no colons) */
    const char *timestamp = time_sync_get_str();
    char filename[64];
    if (timestamp != NULL) {
        snprintf(filename, sizeof(filename), "motion_%s.jpg", timestamp);
        /* Replace spaces and colons with underscores for FAT compatibility */
        for (char *p = filename; *p; p++) {
            if (*p == ' ' || *p == ':') *p = '_';
        }
    } else {
        snprintf(filename, sizeof(filename), "motion_unknown.jpg");
    }

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

        flash_led_init();

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

        /* Sample bytes from fb_a for later comparison AND compute brightness */
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

        /* Check scene brightness via JPEG size ratio to baseline */
        bool dark_scene = is_scene_dark(fb_a->len);
        {
            ESP_LOGI(TAG, "[DBG] ref_frame len=%zu, dark=%s, baseline=%d, thresh=%d",
                     fb_a->len, dark_scene ? "YES" : "no",
                     s_brightness_baseline,
                     s_brightness_baseline > 0 ? (int32_t)(s_brightness_baseline * DARK_RATIO_THRESH) : 0);
        }

        /* Return fb_a immediately — free the frame buffer */
        camera_return_fb(fb_a);
        fb_a = NULL;

        /* Wait between captures for detectable scene change */
        vTaskDelay(pdMS_TO_TICKS(CAPTURE_INTERVAL_MS));

        /* Capture comparison frame — no flash during detection.
         * Flash between ref/comp frames creates false 80%+ diffs.
         * Flash is used ONLY for the final photo capture in handle_motion_event(). */
        camera_fb_t *fb_b = NULL;
        if (camera_capture(&fb_b) != ESP_OK || fb_b == NULL) {
            ESP_LOGW(TAG, "Failed to capture comparison frame");
            free(samples_a);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        size_t min_len = (a_len < fb_b->len) ? a_len : fb_b->len;
        size_t total = 0;
        size_t changed = 0;
        int delta_thresh = dark_scene ? PIXEL_DELTA_DARK : PIXEL_DELTA;
        for (size_t i = 0, j = 0; i < min_len && j < sample_count; i += SAMPLE_STEP, j++) {
            total++;
            int diff = (int)samples_a[j] - (int)fb_b->buf[i];
            if (diff < 0) diff = -diff;
            if (diff > delta_thresh) {
                changed++;
            }
        }

        bool motion = false;
        if (total > 0) {
            uint8_t percent = (uint8_t)((changed * 100) / total);
            motion = (percent >= cfg->motion_threshold);
            ESP_LOGI(TAG, "[DBG] diff: %u/%u = %u%% (thresh=%u%%, delta=%d, motion=%s, cooldown=%s)",
                     (unsigned)changed, (unsigned)total, percent,
                     cfg->motion_threshold, delta_thresh, motion ? "YES" : "no",
                     is_in_cooldown() ? "YES" : "no");
        }

        /* Free fb_b and sample buffer */
        camera_return_fb(fb_b);
        free(samples_a);

        /* Trigger action on motion (if not in cooldown) */
        if (motion && !is_in_cooldown()) {
            handle_motion_event(dark_scene);
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
    s_brightness_baseline = 0;
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
    s_brightness_baseline = 0;

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
