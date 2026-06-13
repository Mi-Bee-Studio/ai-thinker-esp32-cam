/*
 * Copyright (C) 2024 MiBee Cam Authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Recorder state enumeration
 */
typedef enum {
    RECORDER_IDLE = 0,
    RECORDER_RECORDING,
    RECORDER_PAUSED,
    RECORDER_ERROR,
} recorder_state_t;

/**
 * @brief Callback invoked when a recording segment is finalized on SD card.
 * @param filepath Full path to the completed segment file
 * @param size     File size in bytes
 */
typedef void (*recorder_segment_cb_t)(const char *filepath, uint32_t size);

/**
 * @brief Initialize the video recorder module
 * @return ESP_OK on success
 */
esp_err_t recorder_init(void);

/**
 * @brief Start recording
 * Creates recording task on core 0, priority 5, stack 8192 bytes
 * @return ESP_OK on success
 */
esp_err_t recorder_start(void);

/**
 * @brief Stop recording
 * Waits for task to exit and cleans up resources
 * @return ESP_OK on success
 */
esp_err_t recorder_stop(void);

/**
 * @brief Pause recording
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not recording
 */
esp_err_t recorder_pause(void);

/**
 * @brief Get current recorder state
 * @return Current recorder state
 */
recorder_state_t recorder_get_state(void);

/**
 * @brief Set segment completion callback
 * @param cb Callback function to invoke when a segment completes
 */
void recorder_set_segment_cb(recorder_segment_cb_t cb);

/**
 * @brief Get current recording file path
 * @return Path to currently open segment file, or empty string if not recording
 */
const char *recorder_get_current_file(void);

/**
 * @brief Feed task watchdog (called externally if needed)
 */
void recorder_watchdog_feed(void);

/**
 * @brief Get recorder task stack high-water mark
 * @return Stack high-water mark in bytes
 */
uint32_t recorder_get_stack_hwm(void);

/**
 * @brief Get total frames dropped since recording started
 * @return Number of dropped frames
 */
uint32_t recorder_get_frames_dropped(void);

/**
 * @brief Cleanup incomplete AVI files at boot
 * Scans /sdcard/recordings/ for .avi files with RIFF size=0 and deletes them
 */
void recorder_cleanup_incomplete(void);