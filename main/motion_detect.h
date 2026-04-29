/*
 * motion_detect.h - Frame-difference motion detection for AI_Thinker ESP32-CAM
 *
 * Captures frames at ~2 FPS, compares JPEG byte samples between consecutive
 * frames, and saves a photo to SD card when motion exceeds the configured
 * threshold. Includes a cooldown period between triggered events.
 */

#ifndef MOTION_DETECT_H
#define MOTION_DETECT_H

#include "common.h"
#include "esp_err.h"

/**
 * @brief Initialize motion detection module (clears internal state)
 * @return ESP_OK on success
 */
esp_err_t motion_detect_init(void);

/**
 * @brief Start the motion detection FreeRTOS task
 *
 * Creates a background task that continuously captures and compares frames.
 * On motion detection (above threshold), saves a photo to SD card and
 * enters cooldown. Does nothing if already running.
 *
 * @return ESP_OK on success, ESP_FAIL if task creation fails
 */
esp_err_t motion_detect_start(void);

/**
 * @brief Stop the motion detection task
 *
 * Signals the detection task to exit and waits up to 2 seconds for it.
 * Force-deletes if it doesn't exit in time.
 */
esp_err_t motion_detect_stop(void);

/**
 * @brief Check if motion detection task is currently running
 * @return true if running, false otherwise
 */
bool motion_detect_is_running(void);

#endif // MOTION_DETECT_H
