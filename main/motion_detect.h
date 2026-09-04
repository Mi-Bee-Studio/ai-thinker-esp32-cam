/*
 * motion_detect.h — ΣΔ pixel-domain motion detection + darkness probe
 *
 * Decodes each analysis frame to a 1/8 grayscale grid (TJpgDec DC path)
 * and runs the 7-stage Sigma-Delta pipeline (lm_motion.c). Darkness comes
 * from a locked-exposure probe (lm_dark_probe.c) sampled every 30s when
 * idle. On a confirmed motion event: flash photo (frame-driven warm-up
 * in dark scenes) + SD save + cooldown.
 */

#ifndef MOTION_DETECT_H
#define MOTION_DETECT_H

#include "common.h"
#include "esp_err.h"
#include "lm_motion.h"

/**
 * @brief Initialize motion detection module (clears internal state)
 * @return ESP_OK on success
 */
esp_err_t motion_detect_init(void);

/**
 * @brief Start the motion detection FreeRTOS task
 *
 * Creates a background task that continuously decodes frames into the
 * analysis grid and runs the ΣΔ pipeline. On a confirmed motion event,
 * saves a photo to SD card (with flash if dark) and enters cooldown.
 * Does nothing if already running.
 *
 * @return ESP_OK on success, ESP_FAIL if task creation fails
 */
esp_err_t motion_detect_start(void);

/**
 * @brief Stop the motion detection task
 *
 * Signals the detection task to exit and waits up to 5 seconds for it.
 * Force-deletes if it doesn't exit in time.
 */
esp_err_t motion_detect_stop(void);

/**
 * @brief Check if motion detection task is currently running
 * @return true if running, false otherwise
 */
bool motion_detect_is_running(void);

/**
 * @brief Get current brightness percentage (0-100)
 * @return brightness percentage, 0 if not yet measured
 */
uint8_t motion_detect_get_brightness_pct(void);

/**
 * @brief Get current brightness detection method
 * @return 0=uninitialized, 1=auto-exposure luma fallback, 2=locked-exposure probe
 */
uint8_t motion_detect_get_brightness_method(void);

/**
 * @brief Check if scene is currently dark
 * @return true if scene brightness below flash_threshold
 */
bool motion_detect_is_scene_dark(void);

/** ΣΔ pipeline diagnostics snapshot (σ̂, energy, fg%, mode …) — /api/status */
const lm_result_t *motion_detect_get_diag(void);

/** Frames analyzed since boot (analysis fps = d/dt of this counter) */
uint32_t motion_detect_get_frames_analyzed(void);

/** Last JPEG→grid decode duration in µs (-1 = none yet) */
int32_t motion_detect_get_decode_us(void);

#endif // MOTION_DETECT_H
