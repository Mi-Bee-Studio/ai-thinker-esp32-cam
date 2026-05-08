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

/**
 * Initialize the OV2640 camera with the given parameters.
 *
 * Performs an I2C bus scan to verify camera presence before initializing.
 * Uses PSRAM for frame buffers with dual-buffer streaming.
 *
 * @param resolution   Desired resolution (CAMERA_RES_VGA, etc.)
 * @param fps          Desired frame rate (affects XCLK frequency)
 * @param jpeg_quality JPEG quality 0-63, lower = better quality
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
 * @param jpeg_quality New JPEG quality (0-63)
 * @return ESP_OK on success
 */
esp_err_t camera_apply_settings(camera_resolution_t resolution, uint8_t fps, uint8_t jpeg_quality);

/**
 * Initialize camera in grayscale mode for brightness probing.
 * Deinitializes current camera, reinitializes with GRAYSCALE+QQVGA.
 * Must be followed by camera_restore_jpeg() to resume normal operation.
 *
 * @return ESP_OK on success
 */
esp_err_t camera_init_grayscale(void);

/**
 * Restore camera to normal JPEG mode after grayscale probing.
 * Reads current user config to restore resolution/quality settings.
 *
 * @return ESP_OK on success
 */
esp_err_t camera_restore_jpeg(void);

#endif /* CAMERA_DRIVER_H */
