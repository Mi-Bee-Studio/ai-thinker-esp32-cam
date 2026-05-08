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
#include "mjpeg_streamer.h"

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

/* Brightness detection state (register-based, reads OV2640 AEC/AGC) */
typedef struct {
    uint8_t method;         /* 0=uninitialized, 1=register, 2=grayscale fallback */
    uint8_t brightness_pct; /* 0-100 */
    bool is_dark;
    int fail_count;         /* consecutive register failures or stale reads */
} brightness_state_t;
static brightness_state_t s_brightness = {0};
static int64_t s_last_grayscale_probe_us = 0;
#define GRAYSCALE_PROBE_INTERVAL_US (30LL * 1000000LL)  /* 30 seconds */

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
 * @brief Probe brightness by switching camera to grayscale mode
 *
 * Temporarily reinitializes camera in GRAYSCALE+QQVGA mode to capture
 * raw pixel data, computes average brightness, then restores JPEG mode.
 * Skipped if MJPEG streaming clients are connected.
 * Called every 60 seconds when method=2 (grayscale fallback).
 */
static void brightness_probe_grayscale(void)
{
    /* Skip if MJPEG clients are watching — reinit would disrupt stream */
    if (mjpeg_streamer_get_client_count() > 0) {
            ESP_LOGI(TAG, "Grayscale probe skipped: MJPEG clients connected");
        return;
    }

    int64_t start_us = esp_timer_get_time();

    /* Switch to grayscale mode */
    esp_err_t ret = camera_init_grayscale();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Grayscale probe: init failed, keeping method=2");
        return;
    }

    /* Discard 2 warmup frames after mode switch */
    for (int i = 0; i < 2; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }

    /* Capture one frame for brightness measurement */
    camera_fb_t *fb = NULL;
    if (camera_capture(&fb) != ESP_OK || fb == NULL) {
        ESP_LOGE(TAG, "Grayscale probe: capture failed");
        camera_restore_jpeg();
        return;
    }

    /* Average all pixel bytes (grayscale = one byte per pixel) */
    uint32_t sum = 0;
    for (size_t i = 0; i < fb->len; i++) {
        sum += fb->buf[i];
    }
    size_t fb_len = fb->len;
    camera_return_fb(fb);

    uint8_t avg = (uint8_t)(sum / fb_len);
    uint8_t pct = (uint8_t)((uint32_t)avg * 100 / 255);
    const cam_config_t *cfg = config_get();
    bool is_dark = (pct < cfg->flash_threshold);

    /* Restore JPEG mode */
    camera_restore_jpeg();

    /* Update brightness state */
    s_brightness.method = 2;
    s_brightness.brightness_pct = pct;
    s_brightness.is_dark = is_dark;

    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGI(TAG, "Grayscale probe: avg=%u, pct=%u%%, dark=%s (probe took %lld ms)",
             avg, pct, is_dark ? "YES" : "no", (long long)elapsed_ms);

    s_last_grayscale_probe_us = esp_timer_get_time();
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

        /*
         * Brightness detection — grayscale probe (primary) + JPEG fallback.
         *
         * The OV2640 AEC registers (init_status) do not reliably reflect
         * real-time lighting in continuous JPEG capture mode — values stabilize
         * at max (671) regardless of actual brightness.
         *
         * Primary method: Grayscale probe every 30 seconds.
         *   Temporarily switches camera to GRAYSCALE+QQVGA, samples actual
         *   pixel luminance. Accurate but disrupts MJPEG stream — skipped
         *   when MJPEG clients are connected.
         *
         * Fallback: JPEG frame size heuristic.
         *   Dark scenes produce smaller JPEGs due to compression of uniform
         *   dark pixels. Less accurate but always available.
         */
        {
            int64_t now = esp_timer_get_time();
            int mjpeg_clients = mjpeg_streamer_get_client_count();
            bool do_probe = (now - s_last_grayscale_probe_us >= GRAYSCALE_PROBE_INTERVAL_US)
                         && (mjpeg_clients == 0);
            if (!do_probe && (now - s_last_grayscale_probe_us >= GRAYSCALE_PROBE_INTERVAL_US)) {
                ESP_LOGI(TAG, "Probe due but %d MJPEG client(s), using JPEG fallback", mjpeg_clients);
            }

            if (do_probe) {
                /* Run grayscale probe — this takes ~1 second */
                brightness_probe_grayscale();
                /* brightness_probe_grayscale() updates s_brightness directly */
            }

            if (s_brightness.method != 2) {
                /* No recent grayscale data — use JPEG size fallback */
                const cam_config_t *cfg = config_get();
                uint32_t jpeg_kb = (uint32_t)a_len / 1024;
                uint8_t pct;

                /* JPEG size heuristic:
                 * Real SVGA JPEG sizes with quality=10:
                 *   Dark (no light): ~12-14 KB
                 *   Dim (indoor ambient): ~14-17 KB
                 *   Bright (facing light): ~17-25 KB
                 * Map: 12KB=0%, 22KB=100% */
                if (jpeg_kb >= 22) {
                    pct = 100;
                } else if (jpeg_kb <= 12) {
                    pct = 0;
                } else {
                    pct = (uint8_t)((jpeg_kb - 12) * 100 / 10);
                }

                s_brightness.method = 1;
                s_brightness.brightness_pct = pct;
                s_brightness.is_dark = (pct < cfg->flash_threshold);
                ESP_LOGI(TAG, "Brightness: jpeg=%uKB, pct=%u%%, dark=%s, method=jpeg-fallback",
                         (unsigned)jpeg_kb, pct,
                         s_brightness.is_dark ? "YES" : "no");
            }
        }

        bool dark_scene = s_brightness.is_dark;

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

        /*
         * Motion threshold: in dark scenes, JPEG frames have low byte variance
         * so we lower the effective threshold to remain sensitive.
         */
        uint8_t effective_thresh = dark_scene
            ? (cfg->motion_threshold > 20 ? cfg->motion_threshold / 4 : 5)
            : cfg->motion_threshold;
        bool motion = false;
        if (total > 0) {
            uint8_t percent = (uint8_t)((changed * 100) / total);
            motion = (percent >= effective_thresh);
            ESP_LOGI(TAG, "[DBG] diff: %u/%u = %u%% (thresh=%u%%%s, delta=%d, motion=%s, cooldown=%s)",
                     (unsigned)changed, (unsigned)total, percent,
                     effective_thresh, dark_scene ? "-dark" : "", delta_thresh, motion ? "YES" : "no",
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
    s_brightness = (brightness_state_t){0};
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
    s_brightness = (brightness_state_t){0};

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

/* ---- Brightness public API ---- */

uint8_t motion_detect_get_brightness_pct(void)
{
    return s_brightness.brightness_pct;
}

uint8_t motion_detect_get_brightness_method(void)
{
    return s_brightness.method;
}

bool motion_detect_is_scene_dark(void)
{
    return s_brightness.is_dark;
}
