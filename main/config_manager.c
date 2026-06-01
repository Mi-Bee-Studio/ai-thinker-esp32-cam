#include <stdio.h>
#include <unistd.h>
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
    cfg->vflip = 0;
    cfg->motion_saved_threshold = 30;
    cfg->nas_protocol = NAS_PROTOCOL_HTTP;
    cfg->nas_port = 8080;
    strncpy(cfg->nas_path, "/upload", sizeof(cfg->nas_path) - 1);
    cfg->wifi_tx_power = 80;   /* 20dBm max */
    cfg->wifi_power_save = 0;  /* disabled for streaming */
    cfg->flash_threshold = 40;  /* default brightness threshold */
    cfg->timelapse_enabled = 0;
    cfg->timelapse_interval_s = 30;
    cfg->timelapse_burst_count = 3;
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

        // Handle config version migrations (blob size mismatch)
        if (ret == ESP_ERR_NVS_INVALID_LENGTH) {
            ESP_LOGW(TAG, "Config blob size mismatch, attempting migration");
            ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
            if (ret == ESP_OK) {
                // Try reading with current size to get the blob
                size_t cur_len = 0;
                nvs_get_blob(handle, NVS_CONFIG_KEY, NULL, &cur_len);
                uint8_t *tmp = malloc(cur_len);
                if (tmp) {
                    ret = nvs_get_blob(handle, NVS_CONFIG_KEY, tmp, &cur_len);
                    nvs_close(handle);
                    if (ret == ESP_OK) {
                        // Copy old fields into new struct
                        memset(&s_config, 0, sizeof(s_config));
                        memcpy(&s_config, tmp, cur_len);
                        // Fill new fields with defaults
                        if (cur_len < sizeof(cam_config_t) - 2) {
                            /* V1: missing vflip + motion_saved_threshold + wifi fields */
                            s_config.vflip = 0;
                            s_config.motion_saved_threshold = 30;
                        }
                        if (cur_len < sizeof(cam_config_t)) {
                            /* V2: missing wifi_tx_power + wifi_power_save */
                            s_config.wifi_tx_power = 80;
                            s_config.wifi_power_save = 0;
                            /* V3→V4: missing flash_threshold */
                            s_config.flash_threshold = 40;
                            /* V4→V5: missing timelapse fields */
                            s_config.timelapse_enabled = 0;
                            s_config.timelapse_interval_s = 30;
                            s_config.timelapse_burst_count = 3;
                        }
                        s_config.version = CONFIG_VERSION;
                        ESP_LOGI(TAG, "Config migrated V%d->V%d (blob %u->%u), saving",
                                 s_config.version, CONFIG_VERSION, (unsigned)cur_len, (unsigned)sizeof(cam_config_t));
                        config_save();
                        ret = ESP_OK;
                    }
                    free(tmp);
                } else {
                    nvs_close(handle);
                }
        }
    }
    }

    if (ret == ESP_OK && s_config.magic == CONFIG_MAGIC && s_config.version == CONFIG_VERSION) {
        ESP_LOGI(TAG, "Config loaded from NVS (device=%s, wifi_ssid='%s', pass[0]=0x%02x, pass_len=%u)",
                 s_config.device_name, s_config.wifi_ssid, s_config.wifi_pass[0], (unsigned)strlen(s_config.wifi_pass));
        return ESP_OK;
    }

    // No valid config found
    // Only save defaults on first boot (nvs_open failed = namespace missing)
    // If nvs_open succeeded but blob read failed, NVS may be corrupted —
    // don't overwrite with defaults, just use them in memory.
    bool first_boot = (ret != ESP_OK);  // nvs_open or nvs_get_blob failed
    ESP_LOGW(TAG, "No valid config found (first_boot=%d), applying defaults", first_boot);
    apply_defaults(&s_config);
    if (first_boot) {
        config_save();  // Save defaults only on truly first boot
    } else {
        ESP_LOGW(TAG, "NVS blob read OK but invalid — NOT overwriting with defaults");
    }
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

    ESP_LOGW(TAG, "Config saved: wifi_ssid='%s' device=%s", s_config.wifi_ssid, s_config.device_name);
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
    ESP_LOGI(TAG, "NAS set (host=%s, port=%u, path=%s, user=%s, pass=***)",
             host, port, path, s_config.nas_user);
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

esp_err_t config_set_vflip(uint8_t vflip)
{
    s_config.vflip = vflip ? 1 : 0;
    ESP_LOGI(TAG, "Vflip set to %u", s_config.vflip);
    return config_save();
}

esp_err_t config_set_wifi_power(uint8_t tx_power, uint8_t power_save)
{
    if (tx_power < 8) tx_power = 8;
    if (tx_power > 84) tx_power = 84;  /* ESP32 max 21dBm */
    s_config.wifi_tx_power = tx_power;
    s_config.wifi_power_save = power_save ? 1 : 0;
    ESP_LOGI(TAG, "WiFi power set (tx=%u=%.1fdBm, ps=%s)",
             tx_power, tx_power * 0.25f, power_save ? "on" : "off");
    return config_save();
}
esp_err_t config_set_motion_saved_threshold(uint8_t threshold)
{
    s_config.motion_saved_threshold = threshold;
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


esp_err_t config_set_flash_threshold(uint8_t threshold)
{
    s_config.flash_threshold = threshold;
    ESP_LOGI(TAG, "Flash threshold set to %u", threshold);
    return config_save();
}

esp_err_t config_set_timelapse(uint8_t enabled, uint16_t interval_s, uint8_t burst_count)
{
    if (interval_s < 1) interval_s = 1;
    if (burst_count < 1) burst_count = 1;
    if (burst_count > 10) burst_count = 10;
    s_config.timelapse_enabled = enabled ? 1 : 0;
    s_config.timelapse_interval_s = interval_s;
    s_config.timelapse_burst_count = burst_count;
    ESP_LOGI(TAG, "Timelapse set (enabled=%u, interval=%us, burst=%u)", enabled, interval_s, burst_count);
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

/*
 * Load WiFi config from /sdcard/config.txt
 * Format: simple key=value, one per line
 *   wifi.ssid=MyNetwork
 *   wifi.password=MyPassword123
 * Lines starting with # are comments. Empty lines ignored.
 * The config file is preserved on SD card for persistent use.
 */
esp_err_t config_load_from_sd(void)
{
    FILE *fp;
    char line[128];
    char new_ssid[33] = {0};
    char new_pass[65] = {0};
    bool ssid_found = false;
    bool pass_found = false;

    fp = fopen("/sdcard/config.txt", "r");
    if (!fp) {
        ESP_LOGD(TAG, "No config.txt on SD card");
        return ESP_ERR_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), fp)) {
        // Trim trailing whitespace
        char *end = line + strlen(line) - 1;
        while (end >= line && (*end == '\r' || *end == '\n' || *end == ' ')) {
            *end = '\0'; end--;
        }
        if (line[0] == '\0' || line[0] == '#') continue;

        // Parse key=value
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        if (strcmp(key, "wifi.ssid") == 0) {
            strncpy(new_ssid, value, sizeof(new_ssid) - 1);
            ssid_found = true;
        } else if (strcmp(key, "wifi.password") == 0) {
            strncpy(new_pass, value, sizeof(new_pass) - 1);
            pass_found = true;
        }
    }
    fclose(fp);

    if (!ssid_found || !pass_found) {
        ESP_LOGW(TAG, "config.txt incomplete (ssid=%d, password=%d)", ssid_found, pass_found);
        return ESP_ERR_NOT_FOUND;
    }

    // Skip if unchanged
    if (strcmp(new_ssid, s_config.wifi_ssid) == 0 &&
        strcmp(new_pass, s_config.wifi_pass) == 0) {
        ESP_LOGI(TAG, "WiFi config unchanged");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "WiFi from SD: ssid='%s'", new_ssid);
    esp_err_t ret = config_set_wifi(new_ssid, new_pass);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "WiFi config saved to NVS (SD config preserved)");
    return ESP_OK;
}
