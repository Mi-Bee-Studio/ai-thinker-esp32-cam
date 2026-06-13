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

/**
 * @brief Initialize NAS upload module
 * Create upload queue (capacity 16) and background upload task (bound to core 1, priority 3)
 * @return ESP_OK success, ESP_FAIL failed
 */
esp_err_t nas_uploader_init(void);

/**
 * @brief Add file path to upload queue
 * @param filepath Full path of file to upload
 * @return ESP_OK success, ESP_ERR_INVALID_STATE not initialized, ESP_ERR_NO_MEM queue full
 */
esp_err_t nas_uploader_enqueue(const char *filepath);

/**
 * @brief Get upload module current status
 * @param last_upload Output last successful upload time string
 * @param len last_upload buffer length
 * @param queue_count Output current file count in queue
 * @param paused Output if paused due to consecutive failures
 */
void nas_uploader_get_status(char *last_upload, size_t len, int *queue_count, bool *paused);

/**
 * @brief Get upload task stack high-water mark
 * @return Remaining stack space (bytes)
 */
uint32_t nas_uploader_get_stack_hwm(void);

/**
 * @brief Get upload success/failure cumulative counts
 * @param success Output success count
 * @param failure Output failure count
 */
void nas_uploader_get_stats(int *success, int *failure);