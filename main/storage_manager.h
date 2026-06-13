#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "common.h"
#include "esp_err.h"
#include "esp_camera.h"

/**
 * @brief Initialize SD card (SPI mode), mount FAT at /sdcard, start hot-plug monitor
 *
 * Uses SPI2_HOST with pins: CLK=14, MOSI=15, MISO=2, CS=13
 * GPIO14 is shared with camera XCLK — storage_deinit() must be called before camera init.
 */
esp_err_t storage_init(void);

/**
 * @brief Unmount FAT filesystem and release SPI bus
 *
 * CRITICAL: Must be called before camera uses GPIO14 (XCLK).
 * After this, storage_is_available() returns false until storage_init() is called again.
 */
esp_err_t storage_deinit(void);

/** @brief Check if SD card is currently mounted and available */
bool storage_is_available(void);

/**
 * @brief Re-verify card presence by stat on mount point
 * @return ESP_OK if card present, ESP_FAIL otherwise
 */
esp_err_t storage_check(void);

/**
 * @brief Save JPEG frame buffer to /sdcard/photos/YYYY-MM/ directory
 * @param fb     Camera frame buffer (fb->buf contains JPEG data)
 * @param filename  Output filename (e.g. "photo_001.jpg")
 * @return ESP_OK on success, error code on failure
 */
esp_err_t storage_save_photo(camera_fb_t *fb, const char *filename);

/**
 * @brief List photo files in a directory
 * @param path     Directory path to list (e.g. "/sdcard/photos/2026-04")
 * @param buf      Output buffer for newline-separated file paths
 * @param buf_size Size of output buffer
 * @return Number of photos listed, or negative on error
 */
int storage_list_photos(const char *path, char *buf, size_t buf_size);

/**
 * @brief Delete oldest recording files when usage exceeds cleanup_low_pct
 *        until usage drops below cleanup_high_pct
 * @return ESP_OK on success or if cleanup not needed
 */
esp_err_t storage_cleanup(void);

/**
 * @brief Get free space on SD card
 * @return Free space in MB
 */
uint32_t storage_get_free_space(void);

/**
 * @brief Get total number of photos on SD card
 * @return Photo count
 */
uint32_t storage_get_photo_count(void);

/**
 * @brief Delete a photo file from SD card
 * @param name  Relative path within /sdcard/photos/ (e.g. "2026-06/photo.jpg")
 * @return ESP_OK on success, error code on failure
 */
esp_err_t storage_delete_photo(const char *name);

/**
 * @brief Invalidate the photo list cache (call after any photo save/delete from outside this module)
 */
void storage_invalidate_list_cache(void);

/**
 * @brief Get total space on SD card
 * @return Total space in MB
 */
uint32_t storage_get_total_space(void);

/** @brief Warm the photo list cache in background (call after storage_init) */
void storage_warm_cache(void);

/** @brief Format the SD card (all data lost). Remounts after format. */
esp_err_t storage_format(void);

/** @brief Recording file information struct */
typedef struct {
    char name[64];       /**< Relative path from /sdcard/recordings/ */
    uint32_t size;       /**< File size in bytes */
    char time_str[32];   /**< Modification time string (YYYY-MM-DD HH:MM:SS) */
} file_info_t;

/**
 * @brief Register a recording file for circular cleanup tracking
 * @param filepath Full path to the recording file
 * @param size File size in bytes
 */
void storage_register_file(const char *filepath, uint32_t size);

/**
 * @brief Get SD card usage percentage (0-100)
 * @return Used space as percentage of total capacity
 */
uint8_t storage_get_usage_pct(void);

/**
 * @brief Cleanup incomplete AVI files (RIFF header with size=0)
 * Scans /sdcard/recordings/ recursively for incomplete recordings.
 * @return ESP_OK on success
 */
esp_err_t storage_cleanup_incomplete_avi(void);

/**
 * @brief Get count of recording files tracked in cache
 * @return Number of recording files
 */
uint32_t storage_get_recording_count(void);

#endif // STORAGE_MANAGER_H
