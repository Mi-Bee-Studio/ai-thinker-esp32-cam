#include "config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "config";
static const char *NVS_NAMESPACE = "camcfg";
static const char *NVS_CONFIG_KEY = "config";

static cam_config_t s_config = {0};

static void apply_defaults(cam_config_t *cfg)
{
    memset(cfg, 0, sizeof(cam_config_t));
    strncpy(cfg->device_name, CONFIG_DEFAULT_DEVICE_NAME, sizeof(cfg->device_name) - 1);
    cfg->resolution = CAMERA_RES_VGA;
    cfg->fps = 15;
    cfg->jpeg_quality = 12;
    strncpy(cfg->web_password, "admin", sizeof(cfg->web_password) - 1);
    strncpy(cfg->timezone, CONFIG_DEFAULT_TIMEZONE, sizeof(cfg->timezone) - 1);
    cfg->motion_threshold = 30;
    cfg->motion_cooldown = 10;
    cfg->nas_protocol = NAS_PROTOCOL_HTTP;
    cfg->nas_port = 8080;
    strncpy(cfg->nas_path, "/upload", sizeof(cfg->nas_path) - 1);
    cfg->magic = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
}

esp_err_t config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Try loading existing config
    nvs_handle_t handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        size_t len = sizeof(cam_config_t);
        ret = nvs_get_blob(handle, NVS_CONFIG_KEY, &s_config, &len);
        nvs_close(handle);
    }

    if (ret == ESP_OK && s_config.magic == CONFIG_MAGIC && s_config.version == CONFIG_VERSION) {
        ESP_LOGI(TAG, "Config loaded from NVS (device=%s)", s_config.device_name);
        return ESP_OK;
    }

    // No valid config — apply defaults and save
    ESP_LOGW(TAG, "No valid config found, applying defaults");
    apply_defaults(&s_config);
    config_save();
    return ESP_OK;
}

const cam_config_t *config_get(void)
{
    return &s_config;
}

esp_err_t config_save(void)
{
    // Ensure magic and version are set
    s_config.magic = CONFIG_MAGIC;
    s_config.version = CONFIG_VERSION;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_CONFIG_KEY, &s_config, sizeof(cam_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Config saved (device=%s)", s_config.device_name);
    return ESP_OK;
}

esp_err_t config_reset(void)
{
    ESP_LOGW(TAG, "Resetting config to defaults");
    apply_defaults(&s_config);
    return config_save();
}

bool config_is_valid(void)
{
    return s_config.magic == CONFIG_MAGIC && s_config.version == CONFIG_VERSION;
}

esp_err_t config_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !pass) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid) - 1);
    s_config.wifi_ssid[sizeof(s_config.wifi_ssid) - 1] = '\0';
    strncpy(s_config.wifi_pass, pass, sizeof(s_config.wifi_pass) - 1);
    s_config.wifi_pass[sizeof(s_config.wifi_pass) - 1] = '\0';
    ESP_LOGI(TAG, "WiFi set (ssid=%s, pass=***)", s_config.wifi_ssid);
    return config_save();
}

esp_err_t config_set_resolution(camera_resolution_t res)
{
    if (res >= CAMERA_RES_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config.resolution = (uint8_t)res;
    ESP_LOGI(TAG, "Resolution set to %d", s_config.resolution);
    return config_save();
}

esp_err_t config_set_web_password(const char *pass)
{
    if (!pass) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_config.web_password, pass, sizeof(s_config.web_password) - 1);
    s_config.web_password[sizeof(s_config.web_password) - 1] = '\0';
    ESP_LOGI(TAG, "Web password set (pass=***)");
    return config_save();
}

esp_err_t config_set_nas(nas_protocol_t protocol, const char *host,
                         uint16_t port, const char *user,
                         const char *pass, const char *path)
{
    if (!host || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config.nas_protocol = protocol;
    strncpy(s_config.nas_host, host, sizeof(s_config.nas_host) - 1);
    s_config.nas_host[sizeof(s_config.nas_host) - 1] = '\0';
    s_config.nas_port = port;
    if (user) {
        strncpy(s_config.nas_user, user, sizeof(s_config.nas_user) - 1);
        s_config.nas_user[sizeof(s_config.nas_user) - 1] = '\0';
    } else {
        s_config.nas_user[0] = '\0';
    }
    if (pass) {
        strncpy(s_config.nas_pass, pass, sizeof(s_config.nas_pass) - 1);
        s_config.nas_pass[sizeof(s_config.nas_pass) - 1] = '\0';
    } else {
        s_config.nas_pass[0] = '\0';
    }
    strncpy(s_config.nas_path, path, sizeof(s_config.nas_path) - 1);
    s_config.nas_path[sizeof(s_config.nas_path) - 1] = '\0';
    ESP_LOGI(TAG, "NAS set (proto=%d, host=%s, port=%u, path=%s, user=%s, pass=***)",
             protocol, host, port, path, s_config.nas_user);
    return config_save();
}

esp_err_t config_set_motion(uint8_t threshold, uint8_t cooldown)
{
    s_config.motion_threshold = threshold;
    s_config.motion_cooldown = cooldown;
    ESP_LOGI(TAG, "Motion set (threshold=%u, cooldown=%u)", threshold, cooldown);
    return config_save();
}

esp_err_t config_set_device_name(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_config.device_name, name, sizeof(s_config.device_name) - 1);
    s_config.device_name[sizeof(s_config.device_name) - 1] = '\0';
    ESP_LOGI(TAG, "Device name set to %s", s_config.device_name);
    return config_save();
}

esp_err_t config_set_fps(uint8_t fps)
{
    if (fps == 0) fps = 1;
    if (fps > 30) fps = 30;
    s_config.fps = fps;
    ESP_LOGI(TAG, "FPS set to %u", fps);
    return config_save();
}

esp_err_t config_set_jpeg_quality(uint8_t quality)
{
    if (quality < 1) quality = 1;
    if (quality > 63) quality = 63;
    s_config.jpeg_quality = quality;
    ESP_LOGI(TAG, "JPEG quality set to %u", quality);
    return config_save();
}

esp_err_t config_set_timezone(const char *tz)
{
    if (!tz || strlen(tz) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_config.timezone, tz, sizeof(s_config.timezone) - 1);
    s_config.timezone[sizeof(s_config.timezone) - 1] = '\0';
    ESP_LOGI(TAG, "Timezone set to %s", s_config.timezone);
    return config_save();
}
