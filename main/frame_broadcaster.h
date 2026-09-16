#ifndef FRAME_BROADCASTER_H
#define FRAME_BROADCASTER_H

#include "esp_camera.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file frame_broadcaster.h
 * @brief Producer-consumer frame distribution for multi-consumer camera access.
 *
 * Architecture:
 *   - ONE producer task captures from camera at fixed FPS, allocates a new
 *     refcounted frame in PSRAM and memcpy's the camera data OUTSIDE any
 *     lock, then publishes it via a brief spinlock-protected pointer swap.
 *     Camera fb is returned immediately — DMA never starves.
 *   - Multiple consumers acquire a reference to the latest published frame
 *     under the same brief spinlock, then copy it OUTSIDE the lock.  No
 *     memcpy or malloc ever happens inside the critical section, so slow
 *     consumers (e.g. MJPEG over weak WiFi) never block the producer or
 *     other consumers.
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

/**
 * @brief Publication generation of the current frame (0 if none yet).
 *
 * Monotonic per-producer-publish counter. Used to wait for frames captured
 * AFTER some event (e.g. flash LED on) — frame_broker_get_copy() always
 * returns the CURRENT frame immediately, which may predate the event.
 */
uint32_t frame_broker_current_gen(void);

/**
 * @brief Like frame_broker_get_copy(), but only returns a frame whose
 *        publication generation is strictly greater than `gen_floor`.
 *
 * Polls at ~20ms until a NEWER frame is published or timeout. Use
 * frame_broker_current_gen() to snapshot the generation before the event.
 */
esp_err_t frame_broker_get_copy_after(uint32_t gen_floor, camera_fb_t **fb_out,
                                      uint32_t timeout_ms);

/**
 * @brief Temporarily raise the no-viewer idle cadence (2fps → 5fps).
 *
 * Short burst windows only (caller passes ms): the CSI photo chain needs
 * prompt frames for the darkness probe settle, flash warm-up discard and
 * capture — at 2fps idle those alone cost ~2s. With viewers connected the
 * cadence is already capped at 5fps, so this only affects the idle case.
 * Callable from any task.
 */
void frame_broker_boost(uint32_t ms);

/** Total frames produced since init. */
uint32_t frame_broker_get_frame_count(void);

/** Consecutive capture failures (resets on success). */
uint32_t frame_broker_get_fail_count(void);

#endif /* FRAME_BROADCASTER_H */
