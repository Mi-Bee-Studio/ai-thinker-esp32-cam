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
 * @brief Set NAS upload settings, save immediately
 */
esp_err_t config_set_nas(nas_protocol_t protocol, const char *host,
                         uint16_t port, const char *user,
                         const char *pass, const char *path);

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

#endif // CONFIG_MANAGER_H
