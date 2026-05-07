#ifndef NAS_UPLOADER_H
#define NAS_UPLOADER_H

#include "common.h"
#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize NAS uploader
 *
 * Creates a FreeRTOS queue (max 10 entries) and a background upload
 * task pinned to core 1, priority 3, 6144 bytes stack.
 *
 * @return ESP_OK on success, ESP_FAIL if queue or task creation fails
 */
esp_err_t nas_uploader_init(void);

/**
 * @brief Enqueue a file path for background upload
 *
 * Copies the filepath into a queue entry and sends it non-blocking.
 * The background task will read NAS settings from config and upload
 * using HTTP POST.
 *
 * @param filepath  Absolute path to the local file
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized,
 *         ESP_ERR_NO_MEM if queue is full
 */
esp_err_t nas_uploader_enqueue(const char *filepath);

/**
 * @brief Get the number of files pending in the upload queue
 * @return Number of items currently in the queue
 */
int nas_uploader_get_pending_count(void);

/**
 * @brief Check if an upload is currently in progress
 * @return true if the upload task is actively transferring a file
 */
bool nas_uploader_is_busy(void);

#endif // NAS_UPLOADER_H
