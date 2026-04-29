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
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "sensor.h"

static const char *TAG = "camera_driver";

/* ── Defaults ── */
#define DEFAULT_RESOLUTION   CAMERA_RES_VGA
#define DEFAULT_FPS          15
#define DEFAULT_JPEG_QUALITY 12

/* ── Module state ── */
static bool s_camera_initialized = false;
static camera_resolution_t s_current_resolution = CAMERA_RES_VGA;

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

    /* XCLK frequency: 20MHz for all modes on AI_Thinker */
    uint32_t xclk_freq_hz = 20000000;

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

    /* Pre-scan I2C bus to verify camera hardware presence */
    {
        i2c_master_bus_handle_t bus_handle = NULL;
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = CAM_PIN_SIOD,
            .scl_io_num = CAM_PIN_SIOC,
            .clk_source = I2C_CLK_SRC_DEFAULT,
        };
        esp_err_t bus_ret = i2c_new_master_bus(&bus_config, &bus_handle);
        if (bus_ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C bus scan:");
            bool found_camera = false;
            for (int addr = 0x08; addr < 0x78; addr++) {
                if (i2c_master_probe(bus_handle, addr, 50) == ESP_OK) {
                    ESP_LOGI(TAG, "  Found I2C device at 0x%02x", addr);
                    if (addr == 0x3c) {
                        found_camera = true;
                    }
                }
            }
            if (!found_camera) {
                ESP_LOGW(TAG, "No camera sensor detected at 0x3c (OV2640 SCCB address)");
            }
            i2c_del_master_bus(bus_handle);
        } else {
            ESP_LOGE(TAG, "Failed to init I2C bus for scan: %s", esp_err_to_name(bus_ret));
        }
    }

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
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (fb == NULL) {
        ESP_LOGE(TAG, "Null frame buffer pointer");
        return ESP_ERR_INVALID_ARG;
    }

    *fb = esp_camera_fb_get();
    if (*fb == NULL) {
        ESP_LOGE(TAG, "Failed to capture frame");
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
    return ESP_OK;
}

bool camera_is_initialized(void)
{
    return s_camera_initialized;
}

const char* camera_get_sensor_name(void)
{
    if (!s_camera_initialized) {
        return "Unknown";
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL) {
        return "Unknown";
    }

    if (sensor->id.PID == OV2640_PID) {
        return "OV2640";
    }

    return "Unknown";
}

camera_resolution_t camera_get_resolution(void)
{
    return s_current_resolution;
}
