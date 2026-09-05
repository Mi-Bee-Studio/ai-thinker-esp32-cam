#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "common.h"
#include "esp_err.h"
#include <stdbool.h>

/* Forward declaration - avoids pulling cJSON.h into every includer */
typedef struct cJSON cJSON;

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
 * @brief Get a mutex-protected snapshot copy of current config（契约 §1 统一访问层）
 */
void config_get_copy(cam_config_t *out);

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
 * @brief Set secondary (fallback) WiFi credentials, save immediately
 * Pass NULL or empty ssid to disable secondary network.
 */
esp_err_t config_set_wifi_secondary(const char *ssid, const char *pass);

/**
 * @brief Set AP fallback behavior (1=allow AP mode on STA failure, 0=retry only)
 */
esp_err_t config_set_allow_ap_fallback(uint8_t allow);

/**
 * @brief Set camera resolution, save immediately
 */
esp_err_t config_set_resolution(camera_resolution_t res);

/**
 * @brief Set web UI password, save immediately
 */
esp_err_t config_set_web_password(const char *pass);

/**
 * @brief Set motion detection settings (家族超集模型，契约 §3.2), save immediately
 * @param enabled 1=on
 * @param sensitivity 0-100, higher = more sensitive（旧 threshold 迁移: 100-t）
 * @param cooldown_s 1-300, min seconds between triggers
 * @param active_interval_s 1-30, re-trigger interval while motion is continuous
 */
esp_err_t config_set_motion(uint8_t enabled, uint8_t sensitivity, uint16_t cooldown_s, uint8_t active_interval_s);
/**
 * @brief Set device name, save immediately
 */
esp_err_t config_set_device_name(const char *name);

/**
 * @brief Set camera FPS, save immediately
 */
esp_err_t config_set_cam_fps(uint8_t fps);

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

/**
 * @brief Set flash threshold (板级扩展), save immediately
 */
esp_err_t config_set_flash_threshold(uint8_t threshold);

/**
 * @brief Set ONVIF enable (契约核心字段，重启生效), save immediately
 */
esp_err_t config_set_onvif_enable(uint8_t enable);

esp_err_t config_set_timelapse(uint8_t enabled, uint16_t interval_s, uint8_t burst_count);
esp_err_t config_set_timelapse_dynamic(uint8_t mode, uint16_t min_interval, uint16_t max_interval, uint8_t decay_factor, uint16_t decay_period);

/**
 * @brief Set recording settings, save immediately
 * @param mode 0=continuous, 1=timelapse, 2=dynamic
 * @param segment_sec segment duration in seconds
 * @param frame_drop 1=enable smart frame drop
 */
esp_err_t config_set_recording(uint8_t mode, uint16_t segment_sec, uint8_t frame_drop);

/**
 * @brief Set storage cleanup thresholds, save immediately
 */
esp_err_t config_set_cleanup(uint8_t low_pct, uint8_t high_pct);

/**
 * @brief Set SD card write enable, save immediately
 * @param enabled 1=enable writes (save photos+recordings), 0=NVR-only
 */
esp_err_t config_set_save_to_sd(uint8_t enabled);

/**
 * @brief Set SD card error logging enable, save immediately
 * @param enabled 1=log severe errors+WiFi anomalies to SD, 0=disabled
 */
esp_err_t config_set_sd_log_enabled(uint8_t enabled);

/**
 * @brief Set periodic WiFi reconnect interval, save immediately
 * @param hours Reconnect interval in hours (0=disabled, default 24)
 */
esp_err_t config_set_wifi_reconnect_interval(uint16_t hours);

/**
 * @brief Set camera XCLK frequency, save immediately
 * @param mhz 10, 16, or 20 (20=standard, 10=stable for clone modules)
 */
esp_err_t config_set_xclk_freq(uint8_t mhz);

/**
 * @brief Set WiFi RSSI-based roaming parameters, save immediately
 * @param rssi Scan for better AP when current RSSI below this (0=off, default -65)
 * @param gap_s Min RSSI difference (dBm) to trigger switch (default 10)
 */
esp_err_t config_set_wifi_roam(int8_t rssi, uint8_t gap_s);

/**
 * @brief Load WiFi credentials from SD provisioning file（契约 §9）
 * 家族格式 /sdcard/config/wifi.txt（KEY=VALUE）；legacy /sdcard/config.txt
 * （wifi.ssid= 风格）兼容读取一个版本。仅在 NVS 无凭据时生效；首行
 * `one_time` 标记在成功导入后删除文件。
 * @return ESP_OK if config was updated, ESP_ERR_NOT_FOUND if no file/unchanged
 */
esp_err_t config_load_from_sd(void);

/**
 * @brief Export current config as JSON object
 * Returns a cJSON* with all config keys using unified cam_* names.
 * Password field (web_password) is masked as empty string.
 * CALLER MUST cJSON_Delete() the returned object when done.
 * @return cJSON* object, or NULL on allocation failure
 */
cJSON *config_get_json(void);

/**
 * @brief Get web UI password pointer
 * @return const pointer to stored password (valid until next save)
 */
const char *config_get_web_password(void);

#endif // CONFIG_MANAGER_H