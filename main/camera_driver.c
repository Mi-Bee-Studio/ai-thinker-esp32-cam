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
#include "esp_heap_caps.h"
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
static const char *s_cap_source = "board";   /* 最近一次 effective 计算的钳制层 */

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

/* 家族统一刻度（契约 v1.3 §5）：camera_resolution_t 的值即 framesize_t 枚举 */
static framesize_t resolution_to_framesize(camera_resolution_t res)
{
    if ((int)res >= FRAMESIZE_VGA && (int)res <= FRAMESIZE_UXGA) {
        return (framesize_t)res;
    }
    return FRAMESIZE_VGA;
}

static const char* resolution_to_string(camera_resolution_t res)
{
    switch (res) {
        case CAMERA_RES_VGA:  return "VGA";
        case CAMERA_RES_SVGA: return "SVGA";
        case CAMERA_RES_XGA:  return "XGA";
        case CAMERA_RES_HD:   return "HD";
        case CAMERA_RES_SXGA: return "SXGA";
        case CAMERA_RES_UXGA: return "UXGA";
        default:              return "Unknown";
    }
}

/* ── 三层分辨率上限（sensor ∩ board ∩ memory，PIT-021 附录）─────────
 * memory 层：esp32-camera 的 JPEG fb 按 宽*高/5 分配（cam_hal 同式），
 * 预算 = fb_size * fb_count，分完后须仍留 floor 给其它 PSRAM 消费者。
 * 只能收紧上限（防御 PSRAM 退化态），不会把 effective 放宽超过实测常数。 */
#define CAMERA_FB_COUNT      2     /* 与 camera_init 一致（dual buffer） */
#define CAMERA_RES_MEM_FLOOR (256 * 1024)   /* PSRAM 保留水位（本板 4MB 可用） */

/* framesize → 尺寸表，下标 0 对应 FRAMESIZE_VGA（钉死 esp32-camera 2.1.x
 * 枚举序；组件枚举漂移时由 _Static_assert 在构建期暴露） */
static const struct { uint16_t w, h; } s_fs_dims[] = {
    { 640,  480},  { 800,  600},  {1024,  768},  {1280,  720},  {1280, 1024},
    {1600, 1200},  {1920, 1080},  { 720, 1280},  { 864, 1536},  {2048, 1536},
    {2560, 1440},  {2560, 1600},  {1080, 1920},  {2560, 1920},  {2592, 1944},
};
_Static_assert(FRAMESIZE_VGA == 10, "s_fs_dims pinned to esp32-camera 2.1.x enum");

static size_t fb_bytes_for_res(camera_resolution_t res)
{
    int idx = (int)res - (int)FRAMESIZE_VGA;
    if (idx < 0 || (size_t)idx >= sizeof(s_fs_dims) / sizeof(s_fs_dims[0])) {
        return 0;
    }
    return (size_t)s_fs_dims[idx].w * s_fs_dims[idx].h / 5;
}

static bool fb_budget_ok(camera_resolution_t res)
{
    size_t need = fb_bytes_for_res(res) * CAMERA_FB_COUNT;
    size_t cur_fb = s_camera_initialized
        ? fb_bytes_for_res(s_current_resolution) * CAMERA_FB_COUNT : 0;
    size_t avail = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) + cur_fb;
    return need != 0 && avail >= need + CAMERA_RES_MEM_FLOOR;
}

/** sensor 层：查 esp32-camera 组件自带能力表（单一事实源，勿手抄 PID 表）。
 *  未初始化/未知 PID → 回退板级常数（不放宽）。统一刻度下值域同 framesize_t。 */
static camera_resolution_t sensor_max_resolution(void)
{
    if (s_camera_initialized) {
        sensor_t *s = esp_camera_sensor_get();
        camera_sensor_info_t *info = s ? esp_camera_sensor_get_info(&s->id) : NULL;
        if (info && (int)info->max_size >= (int)FRAMESIZE_VGA) {
            int res = (int)info->max_size;
            if (res > (int)CAMERA_RES_UXGA) {
                res = (int)CAMERA_RES_UXGA;   /* 本板枚举天花板 */
            }
            return (camera_resolution_t)res;
        }
        ESP_LOGW(TAG, "Unknown sensor — sensor layer falls back to board max");
    }
    return CAMERA_RES_BOARD_MAX;
}

camera_resolution_t camera_get_effective_max_res(void)
{
    camera_resolution_t sensor_cap = sensor_max_resolution();
    camera_resolution_t board_cap  = CAMERA_RES_BOARD_MAX;
    camera_resolution_t cap = (sensor_cap < board_cap) ? sensor_cap : board_cap;
    while (cap > CAMERA_RES_VGA && !fb_budget_ok(cap)) {
        cap = (camera_resolution_t)((int)cap - 1);
    }
    if (cap == sensor_cap) {
        s_cap_source = "sensor";
    } else if (cap == board_cap) {
        s_cap_source = "board";
    } else {
        s_cap_source = "memory";
    }
    return cap;
}

/* 本板可选档（家族刻度；HD/SXGA 传感器虽支持，历史档位四档维持不变） */
static const camera_res_opt_t s_supported_res[] = {
    { "VGA (640x480)",    CAMERA_RES_VGA  },
    { "SVGA (800x600)",   CAMERA_RES_SVGA },
    { "XGA (1024x768)",   CAMERA_RES_XGA  },
    { "UXGA (1600x1200)", CAMERA_RES_UXGA },
};

const camera_res_opt_t *camera_supported_resolutions(int *count)
{
    if (count) {
        *count = (int)(sizeof(s_supported_res) / sizeof(s_supported_res[0]));
    }
    return s_supported_res;
}

const char *camera_res_label(int value)
{
    switch (value) {
        case CAMERA_RES_VGA:  return "VGA";
        case CAMERA_RES_SVGA: return "SVGA";
        case CAMERA_RES_XGA:  return "XGA";
        case CAMERA_RES_HD:   return "HD";
        case CAMERA_RES_SXGA: return "SXGA";
        case CAMERA_RES_UXGA: return "UXGA";
        default:              return "UNKNOWN";
    }
}

const char *camera_res_cap_source(void)
{
    return s_cap_source;
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

    /* Validate parameters（家族刻度 10-15；越界回退 VGA） */
    if (resolution < CAMERA_RES_VGA || resolution > CAMERA_RES_UXGA) {
        ESP_LOGW(TAG, "Invalid resolution %d, defaulting to VGA", resolution);
        resolution = DEFAULT_RESOLUTION;
    }
    if (fps == 0) {
        ESP_LOGW(TAG, "Invalid fps 0, defaulting to %d", DEFAULT_FPS);
        fps = DEFAULT_FPS;
    }
    if (jpeg_quality < CAMERA_QUALITY_MIN || jpeg_quality > CAMERA_QUALITY_MAX) {
        ESP_LOGW(TAG, "JPEG quality %d out of range [%d-%d], clamping",
                 jpeg_quality, CAMERA_QUALITY_MIN, CAMERA_QUALITY_MAX);
        jpeg_quality = (jpeg_quality < CAMERA_QUALITY_MIN) ? CAMERA_QUALITY_MIN : CAMERA_QUALITY_MAX;
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
        /* 三层上限统一钳制（sensor ∩ board ∩ memory）。OV2640 运行时
         * set_framesize 有效（与 seeed 的 OV5640 不同，PIT-019），此处
         * 钳制即时生效；fb 已按请求档分配，下调只多不少。 */
        if (resolution > camera_get_effective_max_res()) {
            ESP_LOGW(TAG, "Requested res %s exceeds effective max %s (source: %s), clamping",
                     resolution_to_string(resolution),
                     resolution_to_string(camera_get_effective_max_res()),
                     camera_res_cap_source());
            resolution = camera_get_effective_max_res();
            if (sensor->set_framesize) {
                sensor->set_framesize(sensor, resolution_to_framesize(resolution));
            }
        }
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
    if (cam_cfg->cam_vflip && sensor && sensor->set_vflip) {
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

