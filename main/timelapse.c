#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timelapse.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "storage_manager.h"
#include "time_sync.h"
#include "health_monitor.h"

static const char *TAG = "timelapse";

#define SAMPLE_STEP            10
#define PIXEL_DELTA            20
#define PIXEL_DELTA_DARK       180

#define TIMELAPSE_TASK_PRIORITY   4
#define TIMELAPSE_TASK_STACK_SIZE 4096

#define BURST_INTERVAL_MS      500
#define COMPARE_INTERVAL_MS    500
#define STOP_WAIT_MS           2000

#define FLASH_GPIO             4
#define FLASH_WARMUP_MS        200
#define FLASH_LEDC_TIMER       LEDC_TIMER_1
#define FLASH_LEDC_CHANNEL     LEDC_CHANNEL_1
#define FLASH_LEDC_SPEED       LEDC_LOW_SPEED_MODE
#define FLASH_PWM_FREQ         2000
#define FLASH_PWM_RES          LEDC_TIMER_8_BIT
#define FLASH_PWM_DUTY         205

static TaskHandle_t s_task_handle = NULL;
static volatile bool s_running = false;
static bool s_flash_initialized = false;
static uint32_t s_photo_count = 0;
static uint32_t s_burst_photo_count = 0;

static void flash_led_init(void)
{
    if (s_flash_initialized) return;

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

static void resolve_filename_conflict(char *filename, size_t filename_size, const char *base_name)
{
    struct stat st;
    int suffix = 0;

    snprintf(filename, filename_size, "%s.jpg", base_name);
    while (stat(filename, &st) == 0 && suffix < 99) {
        suffix++;
        snprintf(filename, filename_size, "%s_%d.jpg", base_name, suffix);
    }
}

static void do_burst_capture(const char *base_name, bool dark_scene, uint8_t burst_count)
{
    for (uint8_t b = 1; b <= burst_count; b++) {
        if (!s_running) break;

        if (b > 1) {
            vTaskDelay(pdMS_TO_TICKS(BURST_INTERVAL_MS));
        }

        camera_fb_t *fb_burst = NULL;
        if (dark_scene) {
            ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, FLASH_PWM_DUTY);
            ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
            vTaskDelay(pdMS_TO_TICKS(FLASH_WARMUP_MS));
            camera_capture(&fb_burst);
            ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, 0);
            ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
        } else {
            camera_capture(&fb_burst);
        }

        if (fb_burst == NULL) {
            ESP_LOGW(TAG, "Burst photo %u/%u capture failed", b, burst_count);
            continue;
        }

        char burst_name[80];
        snprintf(burst_name, sizeof(burst_name), "%s_burst_%u.jpg", base_name, b);

        esp_err_t err = storage_save_photo(fb_burst, burst_name);
        camera_return_fb(fb_burst);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Burst photo saved: %s", burst_name);
            health_monitor_incr_timelapse_burst_photos();
            s_burst_photo_count++;
        } else {
            ESP_LOGE(TAG, "Failed to save burst photo: %s", esp_err_to_name(err));
        }
    }
}

static void timelapse_task(void *arg)
{
    ESP_LOGI(TAG, "Timelapse task started");

    while (s_running) {
        const cam_config_t *cfg = config_get();

        for (uint32_t i = 0; i < cfg->timelapse_interval_s && s_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!s_running) break;

        if (!camera_is_initialized()) {
            ESP_LOGW(TAG, "Camera not initialized, skipping cycle");
            continue;
        }

        if (!storage_is_available()) {
            ESP_LOGW(TAG, "SD card not available, skipping cycle");
            continue;
        }

        flash_led_init();

        camera_fb_t *fb_test = NULL;
        if (camera_capture(&fb_test) != ESP_OK || fb_test == NULL) {
            ESP_LOGW(TAG, "Failed to capture brightness test frame");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint32_t jpeg_kb = (uint32_t)fb_test->len / 1024;
        uint8_t brightness_pct;
        if (jpeg_kb >= 22) {
            brightness_pct = 100;
        } else if (jpeg_kb <= 12) {
            brightness_pct = 0;
        } else {
            brightness_pct = (uint8_t)((jpeg_kb - 12) * 100 / 10);
        }

        cfg = config_get();
        bool dark_scene = (brightness_pct < cfg->flash_threshold);

        ESP_LOGI(TAG, "Brightness: jpeg=%uKB, pct=%u%%, dark=%s",
                 (unsigned)jpeg_kb, brightness_pct, dark_scene ? "YES" : "no");

        camera_return_fb(fb_test);
        fb_test = NULL;

        camera_fb_t *fb = NULL;
        if (dark_scene) {
            ESP_LOGI(TAG, "Dark scene, enabling flash for photo");
            ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, FLASH_PWM_DUTY);
            ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
            vTaskDelay(pdMS_TO_TICKS(FLASH_WARMUP_MS));
            if (camera_capture(&fb) != ESP_OK || fb == NULL) {
                ESP_LOGW(TAG, "Failed to capture with flash");
            }
            ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, 0);
            ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
        }

        if (fb == NULL) {
            if (camera_capture(&fb) != ESP_OK || fb == NULL) {
                ESP_LOGW(TAG, "Failed to capture timelapse photo");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        size_t fb_len = fb->len;
        size_t sample_count = (fb_len + SAMPLE_STEP - 1) / SAMPLE_STEP;
        uint8_t *samples = (uint8_t *)malloc(sample_count);
        if (samples == NULL) {
            ESP_LOGW(TAG, "Failed to allocate sample buffer (%u bytes)",
                     (unsigned)sample_count);
            camera_return_fb(fb);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        for (size_t i = 0, j = 0; i < fb_len && j < sample_count; i += SAMPLE_STEP, j++) {
            samples[j] = fb->buf[i];
        }

        const char *timestamp = time_sync_get_str();
        char base_name[64];
        if (timestamp != NULL) {
            snprintf(base_name, sizeof(base_name), "timelapse_%s", timestamp);
            for (char *p = base_name; *p; p++) {
                if (*p == ' ' || *p == ':') *p = '_';
            }
        } else {
            snprintf(base_name, sizeof(base_name), "timelapse_unknown");
        }

        char filename[80];
        resolve_filename_conflict(filename, sizeof(filename), base_name);

        esp_err_t err = storage_save_photo(fb, filename);
        camera_return_fb(fb);
        fb = NULL;

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Timelapse photo saved: %s", filename);
            health_monitor_incr_timelapse_photos();
            s_photo_count++;
        } else {
            ESP_LOGE(TAG, "Failed to save timelapse photo: %s", esp_err_to_name(err));
            free(samples);
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(COMPARE_INTERVAL_MS));

        camera_fb_t *fb_comp = NULL;
        if (camera_capture(&fb_comp) != ESP_OK || fb_comp == NULL) {
            ESP_LOGW(TAG, "Failed to capture comparison frame");
            free(samples);
            continue;
        }

        size_t min_len = (fb_len < fb_comp->len) ? fb_len : fb_comp->len;
        size_t total = 0;
        size_t changed = 0;
        int delta_thresh = dark_scene ? PIXEL_DELTA_DARK : PIXEL_DELTA;
        for (size_t i = 0, j = 0; i < min_len && j < sample_count; i += SAMPLE_STEP, j++) {
            total++;
            int diff = (int)samples[j] - (int)fb_comp->buf[i];
            if (diff < 0) diff = -diff;
            if (diff > delta_thresh) {
                changed++;
            }
        }

        camera_return_fb(fb_comp);
        free(samples);

        uint8_t effective_thresh = dark_scene
            ? (cfg->motion_threshold > 20 ? cfg->motion_threshold / 4 : 5)
            : cfg->motion_threshold;

        bool motion = false;
        if (total > 0) {
            uint8_t percent = (uint8_t)((changed * 100) / total);
            motion = (percent >= effective_thresh);
            ESP_LOGI(TAG, "Motion diff: %u/%u = %u%% (thresh=%u%%%s, motion=%s)",
                     (unsigned)changed, (unsigned)total, percent,
                     effective_thresh, dark_scene ? "-dark" : "",
                     motion ? "YES" : "no");
        }

        if (motion) {
            ESP_LOGI(TAG, "Motion detected, starting burst capture (%u photos)",
                     cfg->timelapse_burst_count);
            do_burst_capture(base_name, dark_scene, cfg->timelapse_burst_count);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Timelapse task exiting");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t timelapse_init(void)
{
    s_running = false;
    s_task_handle = NULL;
    s_photo_count = 0;
    s_burst_photo_count = 0;
    ESP_LOGI(TAG, "Timelapse module initialized");
    return ESP_OK;
}

esp_err_t timelapse_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Timelapse already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    s_photo_count = 0;
    s_burst_photo_count = 0;

    BaseType_t ret = xTaskCreate(
        timelapse_task,
        "timelapse",
        TIMELAPSE_TASK_STACK_SIZE,
        NULL,
        TIMELAPSE_TASK_PRIORITY,
        &s_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create timelapse task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Timelapse started (interval=%us, burst=%u)",
             config_get()->timelapse_interval_s, config_get()->timelapse_burst_count);
    return ESP_OK;
}

esp_err_t timelapse_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping timelapse...");
    s_running = false;

    TickType_t timeout = pdMS_TO_TICKS(STOP_WAIT_MS);
    while (s_task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout -= pdMS_TO_TICKS(100);
    }

    if (s_task_handle != NULL) {
        ESP_LOGW(TAG, "Timelapse task did not exit in time, force deleting");
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Timelapse stopped");
    return ESP_OK;
}

bool timelapse_is_running(void)
{
    return s_running;
}

uint32_t timelapse_get_photo_count(void)
{
    return s_photo_count;
}

uint32_t timelapse_get_burst_photo_count(void)
{
    return s_burst_photo_count;
}