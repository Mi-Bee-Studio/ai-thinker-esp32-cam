/*
 * camera_driver.h - OV2640 camera driver for AI_Thinker ESP32-CAM
 *
 * Provides camera initialization, frame capture, and resource management
 * for the OV2640 camera module on the AI_Thinker ESP32-CAM board.
 * Uses the esp32-camera component for hardware abstraction.
 */

#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include "common.h"
#include "esp_err.h"
#include "esp_camera.h"

/* JPEG quality bounds (lower number = better quality / larger frames).
 * esp32-camera sizes JPEG frame buffers at width*height/5 (assumes max 1:5
 * compression); quality < 10 regularly exceeds that on OV2640 and produces
 * truncated frames. 2026-09-04 measured on this board: UXGA q10 = ~224KB,
 * safely under the 384KB fb limit. */
#define CAMERA_QUALITY_MIN 10
#define CAMERA_QUALITY_MAX 63

/* ── 分辨率三层上限（2026-09-04 家族统一，PIT-021 附录）─────────────
 * effective = min(传感器上限, 板级实测上限, 运行时 fb 预算)：
 *  1. sensor  — esp32-camera 自动检测（camera_sensor_info_t.max_size，
 *               OV2640→UXGA），换传感器候选表自适应收缩；
 *  2. board   — 本板实测常数（唯一手工数字，禁止沿用姐妹板数值）：
 *               2026-09-04 实测 UXGA 采集 ~1.7fps / 推流 ~0.6fps 无崩溃
 *               ——本板上限即传感器上限；
 *  3. memory  — 运行时 fb 预算校验（宽*高/5*fb_count + floor ≤ 可用
 *               PSRAM），只能收紧，防御 PSRAM 退化态。
 * GET /api/camera 下发 res_cap_source 报告被哪一层钳制（诊断用）。 */
#define CAMERA_RES_BOARD_MAX CAMERA_RES_UXGA

/**
 * @brief 当前实际可用最大分辨率 min(sensor, board, memory)
 */
camera_resolution_t camera_get_effective_max_res(void);

/**
 * @brief 上限被哪一层钳制（sensor / board / memory），静态字符串
 */
const char *camera_res_cap_source(void);

/* 本板可选分辨率档（家族刻度 framesize_t）。单一事实源：
 * GET /api/camera 的 supported_resolutions 与 AT 端口共用（调用方按
 * camera_get_effective_max_res() 过滤）。 */
typedef struct { const char *label; int value; } camera_res_opt_t;

const camera_res_opt_t *camera_supported_resolutions(int *count);

/* 家族刻度值 → 短名（"VGA"…"UXGA"，越界 "UNKNOWN"） */
const char *camera_res_label(int value);

/**
 * Initialize the OV2640 camera with the given parameters.
 *
 * Initializes the OV2640 via the esp32-camera component (sensor auto-detected).
 * Uses PSRAM for frame buffers with dual-buffer streaming.
 *
 * @param resolution   Desired resolution (CAMERA_RES_VGA, etc.)
 * @param fps          Desired frame rate (controls frame_broadcaster capture cadence)
 * @param jpeg_quality JPEG quality 10-63 (CAMERA_QUALITY_MIN..MAX), lower = better
 * @return ESP_OK on success, error code on failure
 */
esp_err_t camera_init(camera_resolution_t resolution, uint8_t fps, uint8_t jpeg_quality);

/**
 * Deinitialize the camera and release all resources.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t camera_deinit(void);

/**
 * Capture a single frame from the camera.
 *
 * @param fb  Output pointer to the frame buffer. Caller must call
 *            camera_return_fb() when done with the buffer.
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if not initialized
 */
esp_err_t camera_capture(camera_fb_t **fb);

/**
 * Return a previously captured frame buffer to the driver for reuse.
 *
 * @param fb  Frame buffer to return
 * @return ESP_OK on success
 */
esp_err_t camera_return_fb(camera_fb_t *fb);

/**
 * Check if the camera has been initialized.
 *
 * @return true if initialized, false otherwise
 */
bool camera_is_initialized(void);

/**
 * Get the human-readable name of the camera sensor.
 *
 * @return Sensor name string (e.g. "OV2640"), or "Unknown" if not initialized
 */
const char* camera_get_sensor_name(void);

/**
 * Get the current resolution setting.
 *
 * @return Current resolution enum value
 */
camera_resolution_t camera_get_resolution(void);

/**
 * Release SD card SPI bus so GPIO14 can be used by camera.
 *
 * Must be called before camera_init() if SD card was previously initialized,
 * since GPIO14 is shared between SD card CLK and camera data pins.
 */
void camera_release_sd_bus(void);

/**
 * Apply vertical flip setting immediately without camera reinit.
 *
 * @param vflip  Non-zero to enable, 0 to disable
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if camera not initialized
 */
esp_err_t camera_apply_vflip(uint8_t vflip);

/**
 * Reinitialize camera with new resolution, fps, and jpeg quality.
 * Locks the camera mutex to prevent concurrent capture during reinit.
 * Re-applies vflip from config after reinit.
 *
 * @param resolution   New resolution
 * @param fps          New frame rate
 * @param jpeg_quality New JPEG quality (clamped to 10-63)
 * @return ESP_OK on success
 */
esp_err_t camera_apply_settings(camera_resolution_t resolution, uint8_t fps, uint8_t jpeg_quality);

#endif /* CAMERA_DRIVER_H */
