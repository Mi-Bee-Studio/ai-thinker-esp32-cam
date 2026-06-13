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
 * @brief WebDAV connection configuration struct
 */
typedef struct {
    char url[128];     /* WebDAV server URL, e.g. "http://192.168.1.100:5005" */
    char user[32];     /* Login username */
    char pass[32];     /* Login password */
} webdav_config_t;

/**
 * @brief Upload local file to WebDAV remote path via HTTP PUT
 * Includes exponential backoff retry (max 3 times)
 * @param cfg WebDAV connection configuration
 * @param remote_path Remote file path
 * @param local_path Local file path
 * @return ESP_OK success, ESP_FAIL upload failed
 */
esp_err_t webdav_upload(const webdav_config_t *cfg, const char *remote_path, const char *local_path);

/**
 * @brief Create remote directory via WebDAV MKCOL
 * 201=created, 405=exists, both return ESP_OK
 * @param cfg WebDAV connection configuration
 * @param remote_dir Remote directory path
 * @return ESP_OK created or exists, ESP_FAIL failed
 */
esp_err_t webdav_mkdir(const webdav_config_t *cfg, const char *remote_dir);

/**
 * @brief Check if remote resource exists via HTTP HEAD
 * @param cfg WebDAV connection configuration
 * @param remote_path Remote resource path
 * @return ESP_OK exists (200), ESP_ERR_NOT_FOUND not found (404), ESP_FAIL other error
 */
esp_err_t webdav_exists(const webdav_config_t *cfg, const char *remote_path);

/**
 * @brief Recursively create all directory levels in remote path
 * E.g., "/A/B/C" -> creates /A, /A/B, /A/B/C in order
 * @param cfg WebDAV connection configuration
 * @param path Remote directory path
 * @return ESP_OK all success, or last failed directory error
 */
esp_err_t webdav_mkdir_recursive(const webdav_config_t *cfg, const char *path);