/*
 * camera_driver.c - OV2640 camera driver for AI_Thinker ESP32-CAM
 *
 * Implements camera initialization, frame capture, and resource management
 * for the OV2640 camera module on the AI_Thinker ESP32-CAM board (ESP32).
 * Uses the esp32-camera component for hardware abstraction.
 */

#include "camera_driver.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "driver/gpio.h"
#include "sensor.h"
#include "config_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "camera_driver";

/* ── Defaults ── */
#define DEFAULT_RESOLUTION   CAMERA_RES_VGA
#define DEFAULT_FPS          15
#define DEFAULT_JPEG_QUALITY 12

/* ── Module state ── */
static bool s_camera_initialized = false;
static camera_resolution_t s_current_resolution = CAMERA_RES_VGA;
static SemaphoreHandle_t s_camera_mutex = NULL;

/* ── Drain gate: prevents use-after-free when reinit frees frame buffers
 * while consumers (streamer, motion, recorder, /capture) still hold them.
 *
 * Flow:
 *   camera_capture()    increments s_outstanding_fbs BEFORE releasing mutex
 *   camera_return_fb()  decrements after esp_camera_fb_return()
 *   camera_apply_settings():
 *                       take s_camera_mutex (blocks new captures), then
 *                       drain_outstanding_fbs() waits for holders to return
 *                       their fbs before calling esp_camera_deinit().
 * ────────────────────────────────────────────────────────────────── */
static portMUX_TYPE s_fb_count_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_outstanding_fbs = 0;

/* ── Helpers ── */

static framesize_t resolution_to_framesize(camera_resolution_t res)
{
    switch (res) {
        case CAMERA_RES_VGA:  return FRAMESIZE_VGA;
        case CAMERA_RES_SVGA: return FRAMESIZE_SVGA;
        case CAMERA_RES_XGA:  return FRAMESIZE_XGA;
        case CAMERA_RES_UXGA: return FRAMESIZE_UXGA;
        default:              return FRAMESIZE_VGA;
    }
}

static const char* resolution_to_string(camera_resolution_t res)
{
    switch (res) {
        case CAMERA_RES_VGA:  return "VGA";
        case CAMERA_RES_SVGA: return "SVGA";
        case CAMERA_RES_XGA:  return "XGA";
        case CAMERA_RES_UXGA: return "UXGA";
        default:              return "Unknown";
    }
}

/* Wait for all outstanding frame buffers to be returned by consumers.
 * Must be called AFTER taking s_camera_mutex (so no new captures can, 
 * start) and BEFORE any esp_camera_deinit()/camera_deinit() call.
 * @param timeout_ms Max wait time in milliseconds
 */
static void drain_outstanding_fbs(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    uint32_t initial = s_outstanding_fbs;
    while (s_outstanding_fbs > 0 && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    if (s_outstanding_fbs > 0) {
        ESP_LOGW(TAG, "Drain timeout: %lu fb(s) still outstanding after %ums "
                 "(proceeding with deinit — crash risk)",
                 (unsigned long)s_outstanding_fbs, timeout_ms);
    } else if (waited > 0) {
        ESP_LOGI(TAG, "Drained %u fb(s) in %ums",
                 (unsigned)initial, waited);
    }
}
/* ── Public API ── */

void camera_release_sd_bus(void)
{
    /* GPIO14 is shared between SD card CLK and camera.
     * Reset it so the camera driver can claim it. */
    gpio_reset_pin(SD_PIN_CLK);
    ESP_LOGI(TAG, "Released SD card SPI bus (GPIO14 reset)");
}

esp_err_t camera_init(camera_resolution_t resolution, uint8_t fps, uint8_t jpeg_quality)
{
    if (s_camera_initialized) {
        ESP_LOGW(TAG, "Camera already initialized, deinitializing first");
        esp_err_t ret = camera_deinit();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize existing camera: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    /* Validate parameters */
    if (resolution >= CAMERA_RES_MAX) {
        ESP_LOGW(TAG, "Invalid resolution %d, defaulting to VGA", resolution);
        resolution = DEFAULT_RESOLUTION;
    }
    if (fps == 0) {
        ESP_LOGW(TAG, "Invalid fps 0, defaulting to %d", DEFAULT_FPS);
        fps = DEFAULT_FPS;
    }
    if (jpeg_quality > 63) {
        ESP_LOGW(TAG, "JPEG quality %d out of range [0-63], clamping to 63", jpeg_quality);
        jpeg_quality = 63;
    }

    /* XCLK frequency: configurable via NVS (10/16/20 MHz)
     * 20 MHz = standard for genuine OV2640
     * 10 MHz = more stable for clone/unstable modules (fixes NO-SOI) */
    uint32_t xclk_freq_hz = (uint32_t)config_get()->xclk_freq_mhz * 1000000;
    if (xclk_freq_hz == 0) xclk_freq_hz = 20000000; /* safety default */

    camera_config_t config = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d0       = CAM_PIN_D0,
        .pin_d1       = CAM_PIN_D1,
        .pin_d2       = CAM_PIN_D2,
        .pin_d3       = CAM_PIN_D3,
        .pin_d4       = CAM_PIN_D4,
        .pin_d5       = CAM_PIN_D5,
        .pin_d6       = CAM_PIN_D6,
        .pin_d7       = CAM_PIN_D7,
        .pin_vsync    = CAM_PIN_VSYNC,
        .pin_href     = CAM_PIN_HREF,
        .pin_pclk     = CAM_PIN_PCLK,

        .xclk_freq_hz = xclk_freq_hz,

        /* Frame buffer: use PSRAM (AI_Thinker has 4MB PSRAM) */
        .fb_location  = CAMERA_FB_IN_PSRAM,

        /* JPEG output format */
        .pixel_format = PIXFORMAT_JPEG,

        .frame_size   = resolution_to_framesize(resolution),
        .jpeg_quality = jpeg_quality,
        .fb_count     = 2,   /* Dual buffer for streaming */

        /* When no frame buffer is available, grab latest frame */
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    ESP_LOGI(TAG, "Initializing camera: %s, %d fps, quality %d, XCLK %lu MHz",
             resolution_to_string(resolution), fps, jpeg_quality, xclk_freq_hz / 1000000);

    /* NOTE: I2C pre-scan removed — it conflicts with camera SCCB init.
     * The camera driver does its own sensor detection during esp_camera_init().
     */

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }

    /* Retrieve sensor info and auto-detect */
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        ESP_LOGI(TAG, "Camera: %s @ %s", camera_get_sensor_name(),
                 resolution_to_string(resolution));

        /* Ensure JPEG pixel format is set */
        if (sensor->set_pixformat) {
            sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
        }
    } else {
        ESP_LOGW(TAG, "Camera sensor info unavailable after init");
    }

    /* Warmup: discard initial frames for auto-exposure stabilization */
    ESP_LOGI(TAG, "Warming up sensor (discarding initial frames)...");
    for (int i = 0; i < 5; i++) {
        camera_fb_t *warmup_fb = esp_camera_fb_get();
        if (warmup_fb) {
            esp_camera_fb_return(warmup_fb);
        }
    }

    /* Test capture to verify pipeline */
    camera_fb_t *test_fb = esp_camera_fb_get();
    if (test_fb) {
        ESP_LOGI(TAG, "Test capture OK, frame size: %zu bytes", test_fb->len);
        esp_camera_fb_return(test_fb);
    } else {
        ESP_LOGW(TAG, "Test capture returned NULL (may succeed later)");
    }

    /* Create mutex for thread-safe reinit and vflip changes */
    if (s_camera_mutex == NULL) {
        s_camera_mutex = xSemaphoreCreateMutex();
    }

    /* Apply vflip from config */
    const cam_config_t *cam_cfg = config_get();
    if (cam_cfg->vflip && sensor && sensor->set_vflip) {
        sensor->set_vflip(sensor, 1);
        ESP_LOGI(TAG, "Vflip enabled from config");
    }

    s_camera_initialized = true;
    s_current_resolution = resolution;

    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

esp_err_t camera_deinit(void)
{
    if (!s_camera_initialized) {
        ESP_LOGW(TAG, "Camera not initialized, nothing to deinitialize");
        return ESP_OK;
    }

    esp_err_t ret = esp_camera_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_camera_initialized = false;
    ESP_LOGI(TAG, "Camera deinitialized");
    return ESP_OK;
}

esp_err_t camera_capture(camera_fb_t **fb)
{
    if (!s_camera_initialized) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (fb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Lock mutex with timeout — MJPEG stream and /capture compete for frame buffer
    if (s_camera_mutex && xSemaphoreTake(s_camera_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGW(TAG, "Camera capture timeout (mutex busy)");
        return ESP_ERR_TIMEOUT;
    }

    *fb = esp_camera_fb_get();

    /* Reserve slot BEFORE releasing mutex — prevents apply_settings from
     * seeing outstanding==0 between our mutex-give and the increment. */
    if (*fb != NULL) {
        portENTER_CRITICAL(&s_fb_count_lock);
        s_outstanding_fbs++;
        portEXIT_CRITICAL(&s_fb_count_lock);
    }

    if (s_camera_mutex) {
        xSemaphoreGive(s_camera_mutex);
    }

    if (*fb == NULL) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t camera_return_fb(camera_fb_t *fb)
{
    if (fb == NULL) {
        ESP_LOGW(TAG, "Attempted to return NULL frame buffer");
        return ESP_ERR_INVALID_ARG;
    }

    esp_camera_fb_return(fb);

    /* Notify drain gate that this fb is no longer held */
    portENTER_CRITICAL(&s_fb_count_lock);
    if (s_outstanding_fbs > 0) {
        s_outstanding_fbs--;
    }
    portEXIT_CRITICAL(&s_fb_count_lock);
    return ESP_OK;
}

bool camera_is_initialized(void)
{
    return s_camera_initialized;
}

const char* camera_get_sensor_name(void)
{
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL) {
        return "Unknown";
    }

    switch (sensor->id.PID) {
        case OV2640_PID:  return "OV2640";
        case OV5640_PID:  return "OV5640";
        case OV3660_PID:  return "OV3660";
        case OV7725_PID:  return "OV7725";
        case OV7670_PID:  return "OV7670";
        case GC2145_PID:  return "GC2145";
        case GC032A_PID:  return "GC032A";
        case GC0308_PID:  return "GC0308";
        case NT99141_PID: return "NT99141";
        case BF3005_PID:  return "BF3005";
        case BF20A6_PID:  return "BF20A6";
        default:           return "Unknown";
    }
}

camera_resolution_t camera_get_resolution(void)
{
    return s_current_resolution;
}

esp_err_t camera_apply_vflip(uint8_t vflip)
{
    if (!s_camera_initialized) return ESP_ERR_NOT_SUPPORTED;
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor || !sensor->set_vflip) return ESP_ERR_NOT_SUPPORTED;
    if (s_camera_mutex) xSemaphoreTake(s_camera_mutex, portMAX_DELAY);
    sensor->set_vflip(sensor, vflip ? 1 : 0);
    if (s_camera_mutex) xSemaphoreGive(s_camera_mutex);
    ESP_LOGI(TAG, "Vflip %s", vflip ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t camera_apply_settings(camera_resolution_t resolution, uint8_t fps, uint8_t jpeg_quality)
{
    if (s_camera_mutex) xSemaphoreTake(s_camera_mutex, portMAX_DELAY);
    drain_outstanding_fbs(3000);
    esp_err_t ret = camera_deinit();
    if (ret != ESP_OK) {
        if (s_camera_mutex) xSemaphoreGive(s_camera_mutex);
        return ret;
    }
    ret = camera_init(resolution, fps, jpeg_quality);
    if (s_camera_mutex) xSemaphoreGive(s_camera_mutex);
    return ret;
}

