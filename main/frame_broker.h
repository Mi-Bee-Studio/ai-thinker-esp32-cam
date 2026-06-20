#ifndef FRAME_BROKER_H
#define FRAME_BROKER_H

#include "esp_camera.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file frame_broker.h
 * @brief Producer-consumer frame distribution for multi-consumer camera access.
 *
 * Architecture:
 *   - ONE producer task captures from camera at fixed FPS, copies frame to
 *     a single "latest frame" slot in PSRAM, returns camera fb immediately
 *     (hold time < 1ms).  Camera DMA never starves.
 *   - Multiple consumers call frame_broker_get_copy() to receive an
 *     independent PSRAM copy.  Slow consumers (e.g. MJPEG over weak WiFi)
 *     never block the camera or other consumers.
 *
 * Must call frame_broker_init() AFTER camera_init() succeeds.
 * The producer coordinates with camera_apply_settings() via the existing
 * s_camera_mutex — no explicit stop/start needed during resolution changes.
 */

/** Initialize broker and start producer task (call after camera_init). */
esp_err_t frame_broker_init(void);

/** Stop broker (stops producer task, frees latest frame buffer). */
esp_err_t frame_broker_stop(void);

/** Check if producer task is running. */
bool frame_broker_is_running(void);

/**
 * @brief Get an independent copy of the latest camera frame.
 *
 * Returns a camera_fb_t* whose ->buf is malloc'd in PSRAM (NOT from the
 * camera pool).  Caller MUST free with frame_broker_free().
 * Reuses camera_fb_t so downstream functions (storage_save_photo,
 * flash_brightness_detect) need zero changes.
 *
 * @param fb_out     Receives allocated frame (caller frees).
 * @param timeout_ms Max wait for first frame (0 = return immediately if none).
 * @return ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_NO_MEM, or ESP_FAIL.
 */
esp_err_t frame_broker_get_copy(camera_fb_t **fb_out, uint32_t timeout_ms);

/** Free a frame obtained from frame_broker_get_copy(). */
void frame_broker_free(camera_fb_t *fb);

/** Total frames produced since init. */
uint32_t frame_broker_get_frame_count(void);

/** Consecutive capture failures (resets on success). */
uint32_t frame_broker_get_fail_count(void);

#endif /* FRAME_BROKER_H */
