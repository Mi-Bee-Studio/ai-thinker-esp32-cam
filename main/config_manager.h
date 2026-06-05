#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "common.h"
#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize config manager - loads from NVS or applies defaults
 * @return ESP_OK on success
 */
esp_err_t config_init(void);

/**
 * @brief Get read-only pointer to current config
 * @return const pointer to internal static cam_config_t
 */
const cam_config_t *config_get(void);

/**
 * @brief Save current config to NVS
 * @return ESP_OK on success
 */
esp_err_t config_save(void);

/**
 * @brief Reset config to defaults and save to NVS
 * @return ESP_OK on success
 */
esp_err_t config_reset(void);

/**
 * @brief Check if current config is valid (magic + version)
 * @return true if valid
 */
bool config_is_valid(void);

/**
 * @brief Set WiFi SSID and password, save immediately
 */
esp_err_t config_set_wifi(const char *ssid, const char *pass);

/**
 * @brief Set camera resolution, save immediately
 */
esp_err_t config_set_resolution(camera_resolution_t res);

/**
 * @brief Set web UI password, save immediately
 */
esp_err_t config_set_web_password(const char *pass);

/**
 * @brief Set motion detection settings, save immediately
 */
esp_err_t config_set_motion(uint8_t threshold, uint8_t cooldown);
/**
 * @brief Set device name, save immediately
 */
esp_err_t config_set_device_name(const char *name);

/**
 * @brief Set FPS, save immediately
 */
esp_err_t config_set_fps(uint8_t fps);

/**
 * @brief Set JPEG quality, save immediately
 */
esp_err_t config_set_jpeg_quality(uint8_t quality);

/**
 * @brief Set timezone string, save immediately
 */
esp_err_t config_set_timezone(const char *tz);

/**
 * @brief Set vertical flip, save immediately
 */
esp_err_t config_set_vflip(uint8_t vflip);

/**
 * @brief Set WiFi TX power and power save mode, save immediately
 * @param tx_power TX power in 0.25dBm units (80=20dBm)
 * @param power_save 0=WIFI_PS_NONE, 1=WIFI_PS_MIN_MODEM
 */
esp_err_t config_set_wifi_power(uint8_t tx_power, uint8_t power_save);
esp_err_t config_set_motion_saved_threshold(uint8_t threshold);

/**
 * @brief Set flash threshold, save immediately
 */
esp_err_t config_set_flash_threshold(uint8_t threshold);

esp_err_t config_set_timelapse(uint8_t enabled, uint16_t interval_s, uint8_t burst_count);
esp_err_t config_set_timelapse_dynamic(uint8_t mode, uint16_t min_interval, uint16_t max_interval, uint8_t decay_factor, uint16_t decay_period);

/**
 * @brief Load WiFi config from /sdcard/config.txt (key=value format)
 * Parses ssid and password, updates NVS if changed.
 * The config file is preserved on SD card for persistent use.
 * @return ESP_OK if config was updated, ESP_ERR_NOT_FOUND if no file/unchanged
 */
esp_err_t config_load_from_sd(void);

#endif // CONFIG_MANAGER_H
