/*
 * config_manager.c — 家族配置契约 v1.0（docs/config-contract.md）
 *
 * 持久化：mibee_cfg 命名空间逐键 NVS + schema_ver 版本键（2026-09-05 起，
 * 取代旧的 camcfg blob+magic 方案）。单键自治：读缺键=默认值；写单键失败
 * 只 WARN 不中断（PIT-022 教训）。所有 NVS 键 ≤15 字符，构建期断言。
 *
 * 迁移：legacy blob（camcfg/config，V16 及更早短 blob）由 migrate_legacy_blob()
 * 一次性翻译为逐键并落 schema_ver=1；旧键改名 config_bak 留存只读（回滚备份）。
 * 迁移幂等：schema_ver 已存在则跳过。
 */
#include <stdio.h>
#include <unistd.h>
#include "config_manager.h"
#include "camera_driver.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "config";

#define NVS_NS          "mibee_cfg"      /* 家族统一命名空间（契约 §1） */
#define NVS_NS_LEGACY   "camcfg"         /* ai-thinker legacy blob 命名空间 */
#define KEY_SCHEMA_VER  "schema_ver"
#define KEY_LEGACY_BAK  "config_bak"     /* 迁移后的 legacy blob 备份键 */
#define KEY_PW_SEED     "pw_seed_v1"     /* 契约 v1.1 密码一次性种子标记 */

static cam_config_t s_config = {0};
static bool s_config_initialized = false;
static SemaphoreHandle_t s_lock = NULL;

static void config_lock(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}
static void config_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* ── 旧刻度 0-3 → 家族 framesize_t 刻度（契约 v1.3 §5 迁移表） ── */
static uint8_t legacy_scale_to_framesize(uint8_t legacy)
{
    switch (legacy) {
        case 0:  return 10;  /* VGA */
        case 1:  return 11;  /* SVGA */
        case 2:  return 12;  /* XGA */
        case 3:  return 15;  /* UXGA */
        default: return 10;
    }
}

static void apply_defaults(cam_config_t *cfg)
{
    memset(cfg, 0, sizeof(cam_config_t));
    strncpy(cfg->device_name, CONFIG_DEFAULT_DEVICE_NAME, sizeof(cfg->device_name) - 1);
    cfg->cam_framesize = CAMERA_RES_VGA;
    cfg->cam_fps = 15;
    cfg->cam_quality = 12;
    strncpy(cfg->web_password, CONFIG_DEFAULT_WEB_PASSWORD, sizeof(cfg->web_password) - 1);
    strncpy(cfg->timezone, CONFIG_DEFAULT_TIMEZONE, sizeof(cfg->timezone) - 1);
    /* 家族 motion 超集（契约 §3.2）：默认灵敏度 70 = 旧 threshold 30 */
    cfg->motion_enabled = 1;
    cfg->motion_sensitivity = 70;
    cfg->motion_cooldown_s = 10;
    cfg->motion_active_interval_s = 5;
    cfg->cam_vflip = 0;
    cfg->wifi_tx_power = 80;   /* 20dBm max */
    cfg->wifi_power_save = 0;  /* disabled for streaming */
    cfg->flash_threshold = 40;
    cfg->timelapse_enabled = 0;
    cfg->timelapse_interval_s = 30;
    cfg->timelapse_burst_count = 3;
    cfg->timelapse_mode = 0;
    cfg->timelapse_min_interval_s = 3;
    cfg->timelapse_max_interval_s = 300;
    cfg->timelapse_decay_factor = 2;
    cfg->timelapse_decay_period_s = 10;
    cfg->record_mode = 0;           /* continuous */
    cfg->segment_sec = 300;
    cfg->frame_drop_enabled = 1;
    /* cleanup_*_pct 语义 = 空闲百分比（契约 v1.2 §11.6） */
    cfg->cleanup_low_pct = 20;
    cfg->cleanup_high_pct = 30;
    memset(cfg->wifi_ssid_2, 0, sizeof(cfg->wifi_ssid_2));
    memset(cfg->wifi_pass_2, 0, sizeof(cfg->wifi_pass_2));
    cfg->allow_ap_fallback = 1;
    cfg->save_to_sd = 1;
    cfg->sd_log_enabled = 1;
    cfg->wifi_reconnect_hours = 24;
    cfg->xclk_freq_mhz = CONFIG_MIBEE_CAM_DEFAULT_XCLK_MHZ;
    cfg->wifi_roam_rssi = -65;   /* scan for better AP below this */
    cfg->wifi_roam_gap_s = 10;   /* switch when other AP is 10dBm+ stronger */
    cfg->onvif_enable = 1;       /* 契约核心字段；本板历史上始终开启 */
}

/* ──────────────────────────────────────────────────────────────────
 * Legacy blob 布局（V16 时代 common.h 的精确快照，仅迁移用，勿再改）
 * ────────────────────────────────────────────────────────────────── */
#define LEGACY_CONFIG_VERSION 16
#define LEGACY_CONFIG_MAGIC   0xA5B6C7D8

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char device_name[33];
    uint8_t cam_framesize;      /* 旧刻度 0-3 */
    uint8_t fps;
    uint8_t cam_quality;
    char web_password[33];
    char timezone[33];
    uint8_t motion_threshold;
    uint8_t motion_cooldown;
    uint8_t cam_vflip;
    uint8_t motion_saved_threshold;
    uint8_t wifi_tx_power;
    uint8_t wifi_power_save;
    uint8_t flash_threshold;
    uint8_t timelapse_enabled;
    uint16_t timelapse_interval_s;
    uint8_t timelapse_burst_count;
    uint8_t timelapse_mode;
    uint16_t timelapse_min_interval_s;
    uint16_t timelapse_max_interval_s;
    uint8_t timelapse_decay_factor;
    uint16_t timelapse_decay_period_s;
    uint8_t record_mode;
    uint16_t record_segment_sec;
    uint8_t frame_drop_enabled;
    uint8_t webdav_enabled;
    char webdav_url[129];
    char webdav_user[33];
    char webdav_pass[65];
    char upload_base_path[65];
    uint8_t alert_webhook_enabled;
    char alert_webhook_url[257];
    uint8_t cleanup_low_pct;
    uint8_t cleanup_high_pct;
    char wifi_ssid_2[33];
    char wifi_pass_2[65];
    uint8_t allow_ap_fallback;
    uint8_t save_to_sd;
    uint8_t sd_log_enabled;
    uint16_t wifi_reconnect_hours;
    uint8_t xclk_freq_mhz;
    int8_t wifi_roam_rssi_threshold;
    uint8_t wifi_roam_rssi_gap;
    uint32_t magic;
    uint32_t version;
} legacy_config_t;

/* 旧 V1→V16 梯子的默认补种逻辑（照搬旧行为：短 blob 的 version 位为 0，
 * 所有 <=N 判断天然成立，与旧实现一致） */
static void legacy_seed_missing_fields(legacy_config_t *lc)
{
    if (lc->version <= 6) {
        lc->timelapse_min_interval_s = 3;
        lc->timelapse_max_interval_s = 300;
        lc->timelapse_decay_factor = 2;
        lc->timelapse_decay_period_s = 10;
    }
    if (lc->version <= 7) {
        lc->record_mode = 0;
        lc->record_segment_sec = 300;
        lc->frame_drop_enabled = 1;
    }
    if (lc->version <= 8) {
        lc->allow_ap_fallback = 1;
    }
    if (lc->version <= 9) {
        lc->save_to_sd = 1;
    }
    if (lc->version <= 10) {
        lc->sd_log_enabled = 1;
    }
    if (lc->version <= 11) {
        lc->wifi_reconnect_hours = 24;
    }
    if (lc->version <= 12) {
        lc->xclk_freq_mhz = CONFIG_MIBEE_CAM_DEFAULT_XCLK_MHZ;
    }
    if (lc->version <= 13) {
        lc->wifi_roam_rssi_threshold = -65;
        lc->wifi_roam_rssi_gap = 10;
    }
    if (lc->version <= 14) {
        lc->wifi_roam_rssi_threshold = -65;
    }
    if (lc->version <= 15) {
        /* V16 起 cleanup 语义=空闲百分比，旧值无意义 → 家族默认 */
        lc->cleanup_low_pct = 20;
        lc->cleanup_high_pct = 30;
    }
}

/* legacy → 契约 v1.0 映射（含刻度翻译与 motion 模型收敛，契约 §6/§6.1） */
static void map_legacy_to_config(const legacy_config_t *lc, cam_config_t *cfg)
{
    apply_defaults(cfg);
    strlcpy(cfg->wifi_ssid, lc->wifi_ssid, sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_pass, lc->wifi_pass, sizeof(cfg->wifi_pass));
    strlcpy(cfg->device_name, lc->device_name, sizeof(cfg->device_name));
    cfg->cam_framesize = legacy_scale_to_framesize(lc->cam_framesize);
    cfg->cam_fps = lc->fps ? lc->fps : 15;
    cfg->cam_quality = lc->cam_quality;
    strlcpy(cfg->web_password, lc->web_password, sizeof(cfg->web_password));
    strlcpy(cfg->timezone, lc->timezone, sizeof(cfg->timezone));
    cfg->motion_enabled = 1;
    cfg->motion_sensitivity = (uint8_t)(100 - (lc->motion_threshold > 100 ? 100 : lc->motion_threshold));
    cfg->motion_cooldown_s = lc->motion_cooldown ? lc->motion_cooldown : 10;
    cfg->motion_active_interval_s = 5;
    cfg->cam_vflip = lc->cam_vflip;
    cfg->wifi_tx_power = lc->wifi_tx_power;
    cfg->wifi_power_save = lc->wifi_power_save;
    cfg->flash_threshold = lc->flash_threshold;
    cfg->timelapse_enabled = lc->timelapse_enabled;
    cfg->timelapse_interval_s = lc->timelapse_interval_s;
    cfg->timelapse_burst_count = lc->timelapse_burst_count;
    cfg->timelapse_mode = lc->timelapse_mode;
    cfg->timelapse_min_interval_s = lc->timelapse_min_interval_s;
    cfg->timelapse_max_interval_s = lc->timelapse_max_interval_s;
    cfg->timelapse_decay_factor = lc->timelapse_decay_factor;
    cfg->timelapse_decay_period_s = lc->timelapse_decay_period_s;
    cfg->record_mode = lc->record_mode;
    cfg->segment_sec = lc->record_segment_sec;
    cfg->frame_drop_enabled = lc->frame_drop_enabled;
    cfg->cleanup_low_pct = lc->cleanup_low_pct;
    cfg->cleanup_high_pct = lc->cleanup_high_pct;
    strlcpy(cfg->wifi_ssid_2, lc->wifi_ssid_2, sizeof(cfg->wifi_ssid_2));
    strlcpy(cfg->wifi_pass_2, lc->wifi_pass_2, sizeof(cfg->wifi_pass_2));
    cfg->allow_ap_fallback = lc->allow_ap_fallback;
    cfg->save_to_sd = lc->save_to_sd;
    cfg->sd_log_enabled = lc->sd_log_enabled;
    cfg->wifi_reconnect_hours = lc->wifi_reconnect_hours;
    cfg->xclk_freq_mhz = lc->xclk_freq_mhz;
    cfg->wifi_roam_rssi = lc->wifi_roam_rssi_threshold;
    cfg->wifi_roam_gap_s = lc->wifi_roam_rssi_gap;
    /* webdav 系列、upload_base_path、alert_webhook 系列、motion_saved_threshold：
     * 随对应子系统移除或模型收敛而废弃（契约 "不适用即省略"），不迁移 */
}

/* ──────────────────────────────────────────────────────────────────
 * 逐键 NVS 读写（契约 §1）
 * ────────────────────────────────────────────────────────────────── */
#define KEY_ASSERT(k) _Static_assert(sizeof(k) <= 16, "NVS key >15 chars: " k)
KEY_ASSERT("schema_ver");
KEY_ASSERT("device_name");
KEY_ASSERT("wifi_ssid");
KEY_ASSERT("wifi_pass");
KEY_ASSERT("wifi_ssid_2");
KEY_ASSERT("wifi_pass_2");
KEY_ASSERT("ap_fallback");
KEY_ASSERT("timezone");
KEY_ASSERT("web_password");
KEY_ASSERT("cam_framesize");
KEY_ASSERT("cam_fps");
KEY_ASSERT("cam_quality");
KEY_ASSERT("cam_vflip");
KEY_ASSERT("onvif_enable");
KEY_ASSERT("motion_en");
KEY_ASSERT("motion_sens");
KEY_ASSERT("motion_cool_s");
KEY_ASSERT("motion_act_s");
KEY_ASSERT("wifi_tx_pwr");
KEY_ASSERT("wifi_ps");
KEY_ASSERT("flash_thr");
KEY_ASSERT("tl_en");
KEY_ASSERT("tl_int_s");
KEY_ASSERT("tl_burst");
KEY_ASSERT("tl_mode");
KEY_ASSERT("tl_min_int_s");
KEY_ASSERT("tl_max_int_s");
KEY_ASSERT("tl_decay_f");
KEY_ASSERT("tl_decay_p_s");
KEY_ASSERT("rec_mode");
KEY_ASSERT("segment_sec");
KEY_ASSERT("frame_drop");
KEY_ASSERT("cleanup_low");
KEY_ASSERT("cleanup_high");
KEY_ASSERT("save_to_sd");
KEY_ASSERT("sd_log_en");
KEY_ASSERT("wifi_rec_h");
KEY_ASSERT("xclk_mhz");
KEY_ASSERT("wifi_roam_rssi");
KEY_ASSERT("wifi_roam_gap");
KEY_ASSERT("pw_seed_v1");

/* 字符串键读取：缺键/超长 → 保持默认（返回 false） */
static bool rd_str(nvs_handle_t h, const char *key, char *out, size_t outsz)
{
    size_t len = outsz;
    esp_err_t ret = nvs_get_str(h, key, out, &len);
    return ret == ESP_OK;
}
static bool rd_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    return nvs_get_u8(h, key, out) == ESP_OK;
}
static bool rd_u16(nvs_handle_t h, const char *key, uint16_t *out)
{
    return nvs_get_u16(h, key, out) == ESP_OK;
}
static bool rd_i8(nvs_handle_t h, const char *key, int8_t *out)
{
    return nvs_get_i8(h, key, out) == ESP_OK;
}

/* 写入辅助：单键失败只 WARN（PIT-022） */
static void wr_str(nvs_handle_t h, const char *key, const char *val)
{
    esp_err_t ret = nvs_set_str(h, key, val);
    if (ret != ESP_OK) ESP_LOGW(TAG, "nvs_set_str(%s) failed: %s", key, esp_err_to_name(ret));
}
static void wr_u8(nvs_handle_t h, const char *key, uint8_t val)
{
    esp_err_t ret = nvs_set_u8(h, key, val);
    if (ret != ESP_OK) ESP_LOGW(TAG, "nvs_set_u8(%s) failed: %s", key, esp_err_to_name(ret));
}
static void wr_u16(nvs_handle_t h, const char *key, uint16_t val)
{
    esp_err_t ret = nvs_set_u16(h, key, val);
    if (ret != ESP_OK) ESP_LOGW(TAG, "nvs_set_u16(%s) failed: %s", key, esp_err_to_name(ret));
}
static void wr_i8(nvs_handle_t h, const char *key, int8_t val)
{
    esp_err_t ret = nvs_set_i8(h, key, val);
    if (ret != ESP_OK) ESP_LOGW(TAG, "nvs_set_i8(%s) failed: %s", key, esp_err_to_name(ret));
}

static void load_keys_from_nvs(nvs_handle_t h, cam_config_t *cfg)
{
    /* 缺键 = apply_defaults 已就位的默认值（契约 §1 单键自治） */
    rd_str(h, "device_name",   cfg->device_name,   sizeof(cfg->device_name));
    rd_str(h, "wifi_ssid",     cfg->wifi_ssid,     sizeof(cfg->wifi_ssid));
    rd_str(h, "wifi_pass",     cfg->wifi_pass,     sizeof(cfg->wifi_pass));
    rd_str(h, "wifi_ssid_2",   cfg->wifi_ssid_2,   sizeof(cfg->wifi_ssid_2));
    rd_str(h, "wifi_pass_2",   cfg->wifi_pass_2,   sizeof(cfg->wifi_pass_2));
    rd_u8(h, "ap_fallback",   &cfg->allow_ap_fallback);
    rd_str(h, "timezone",      cfg->timezone,      sizeof(cfg->timezone));
    rd_str(h, "web_password",  cfg->web_password,  sizeof(cfg->web_password));
    rd_u8(h, "cam_framesize", &cfg->cam_framesize);
    rd_u8(h, "cam_fps",       &cfg->cam_fps);
    rd_u8(h, "cam_quality",   &cfg->cam_quality);
    rd_u8(h, "cam_vflip",     &cfg->cam_vflip);
    rd_u8(h, "onvif_enable",  &cfg->onvif_enable);
    rd_u8(h, "motion_en",     &cfg->motion_enabled);
    rd_u8(h, "motion_sens",   &cfg->motion_sensitivity);
    rd_u16(h, "motion_cool_s", &cfg->motion_cooldown_s);
    rd_u8(h, "motion_act_s",  &cfg->motion_active_interval_s);
    rd_u8(h, "wifi_tx_pwr",   &cfg->wifi_tx_power);
    rd_u8(h, "wifi_ps",       &cfg->wifi_power_save);
    rd_u8(h, "flash_thr",     &cfg->flash_threshold);
    rd_u8(h, "tl_en",         &cfg->timelapse_enabled);
    rd_u16(h, "tl_int_s",     &cfg->timelapse_interval_s);
    rd_u8(h, "tl_burst",      &cfg->timelapse_burst_count);
    rd_u8(h, "tl_mode",       &cfg->timelapse_mode);
    rd_u16(h, "tl_min_int_s", &cfg->timelapse_min_interval_s);
    rd_u16(h, "tl_max_int_s", &cfg->timelapse_max_interval_s);
    rd_u8(h, "tl_decay_f",    &cfg->timelapse_decay_factor);
    rd_u16(h, "tl_decay_p_s", &cfg->timelapse_decay_period_s);
    rd_u8(h, "rec_mode",      &cfg->record_mode);
    rd_u16(h, "segment_sec",  &cfg->segment_sec);
    rd_u8(h, "frame_drop",    &cfg->frame_drop_enabled);
    rd_u8(h, "cleanup_low",   &cfg->cleanup_low_pct);
    rd_u8(h, "cleanup_high",  &cfg->cleanup_high_pct);
    rd_u8(h, "save_to_sd",    &cfg->save_to_sd);
    rd_u8(h, "sd_log_en",     &cfg->sd_log_enabled);
    rd_u16(h, "wifi_rec_h",   &cfg->wifi_reconnect_hours);
    rd_u8(h, "xclk_mhz",      &cfg->xclk_freq_mhz);
    rd_i8(h, "wifi_roam_rssi", &cfg->wifi_roam_rssi);
    rd_u8(h, "wifi_roam_gap", &cfg->wifi_roam_gap_s);
}

static void write_keys_to_nvs(nvs_handle_t h, const cam_config_t *cfg)
{
    wr_str(h, "device_name",  cfg->device_name);
    wr_str(h, "wifi_ssid",    cfg->wifi_ssid);
    wr_str(h, "wifi_pass",    cfg->wifi_pass);
    wr_str(h, "wifi_ssid_2",  cfg->wifi_ssid_2);
    wr_str(h, "wifi_pass_2",  cfg->wifi_pass_2);
    wr_u8(h, "ap_fallback",  cfg->allow_ap_fallback);
    wr_str(h, "timezone",     cfg->timezone);
    wr_str(h, "web_password", cfg->web_password);
    wr_u8(h, "cam_framesize", cfg->cam_framesize);
    wr_u8(h, "cam_fps",      cfg->cam_fps);
    wr_u8(h, "cam_quality",  cfg->cam_quality);
    wr_u8(h, "cam_vflip",    cfg->cam_vflip);
    wr_u8(h, "onvif_enable", cfg->onvif_enable);
    wr_u8(h, "motion_en",    cfg->motion_enabled);
    wr_u8(h, "motion_sens",  cfg->motion_sensitivity);
    wr_u16(h, "motion_cool_s", cfg->motion_cooldown_s);
    wr_u8(h, "motion_act_s", cfg->motion_active_interval_s);
    wr_u8(h, "wifi_tx_pwr",  cfg->wifi_tx_power);
    wr_u8(h, "wifi_ps",      cfg->wifi_power_save);
    wr_u8(h, "flash_thr",    cfg->flash_threshold);
    wr_u8(h, "tl_en",        cfg->timelapse_enabled);
    wr_u16(h, "tl_int_s",    cfg->timelapse_interval_s);
    wr_u8(h, "tl_burst",     cfg->timelapse_burst_count);
    wr_u8(h, "tl_mode",      cfg->timelapse_mode);
    wr_u16(h, "tl_min_int_s", cfg->timelapse_min_interval_s);
    wr_u16(h, "tl_max_int_s", cfg->timelapse_max_interval_s);
    wr_u8(h, "tl_decay_f",   cfg->timelapse_decay_factor);
    wr_u16(h, "tl_decay_p_s", cfg->timelapse_decay_period_s);
    wr_u8(h, "rec_mode",     cfg->record_mode);
    wr_u16(h, "segment_sec", cfg->segment_sec);
    wr_u8(h, "frame_drop",   cfg->frame_drop_enabled);
    wr_u8(h, "cleanup_low",  cfg->cleanup_low_pct);
    wr_u8(h, "cleanup_high", cfg->cleanup_high_pct);
    wr_u8(h, "save_to_sd",   cfg->save_to_sd);
    wr_u8(h, "sd_log_en",    cfg->sd_log_enabled);
    wr_u16(h, "wifi_rec_h",  cfg->wifi_reconnect_hours);
    wr_u8(h, "xclk_mhz",     cfg->xclk_freq_mhz);
    wr_i8(h, "wifi_roam_rssi", cfg->wifi_roam_rssi);
    wr_u8(h, "wifi_roam_gap", cfg->wifi_roam_gap_s);
    wr_u16(h, KEY_SCHEMA_VER, CONFIG_SCHEMA_VERSION);
}

/* ── Legacy blob → 逐键一次性迁移（幂等：schema_ver 存在即返回） ── */
static bool migrate_legacy_blob(void)
{
    nvs_handle_t h_new;
    uint16_t schema_ver = 0;

    if (nvs_open(NVS_NS, NVS_READONLY, &h_new) == ESP_OK) {
        bool have = nvs_get_u16(h_new, KEY_SCHEMA_VER, &schema_ver) == ESP_OK;
        nvs_close(h_new);
        if (have && schema_ver >= CONFIG_SCHEMA_VERSION) {
            return false;   /* 已迁移或本就逐键 */
        }
    }

    /* 读 legacy blob */
    nvs_handle_t h_old;
    if (nvs_open(NVS_NS_LEGACY, NVS_READONLY, &h_old) != ESP_OK) {
        return false;   /* 无 legacy（真空出厂） */
    }
    size_t blob_len = 0;
    if (nvs_get_blob(h_old, "config", NULL, &blob_len) != ESP_OK || blob_len == 0) {
        nvs_close(h_old);
        return false;
    }
    uint8_t *raw = malloc(blob_len);
    legacy_config_t *lc = calloc(1, sizeof(legacy_config_t));
    if (!raw || !lc) {
        free(raw); free(lc); nvs_close(h_old);
        return false;
    }
    bool ok = nvs_get_blob(h_old, "config", raw, &blob_len) == ESP_OK;
    nvs_close(h_old);
    if (!ok) {
        free(raw); free(lc);
        return false;
    }
    memcpy(lc, raw, blob_len < sizeof(legacy_config_t) ? blob_len : sizeof(legacy_config_t));
    free(raw);

    if (lc->magic == LEGACY_CONFIG_MAGIC && lc->version == 0 && blob_len == sizeof(legacy_config_t)) {
        /* 异常态：全长 blob 且 version=0 —— 按旧梯子语义当最老版本补种 */
    }
    legacy_seed_missing_fields(lc);

    /* 密码一次性种子标记跨命名空间迁移（已种过的设备不得重种） */
    bool pw_seeded = false;
    {
        nvs_handle_t h_chk;
        uint8_t flag = 0;
        if (nvs_open(NVS_NS_LEGACY, NVS_READONLY, &h_chk) == ESP_OK) {
            pw_seeded = (nvs_get_u8(h_chk, KEY_PW_SEED, &flag) == ESP_OK && flag == 1);
            nvs_close(h_chk);
        }
        if (!pw_seeded && nvs_open(NVS_NS, NVS_READONLY, &h_chk) == ESP_OK) {
            pw_seeded = (nvs_get_u8(h_chk, KEY_PW_SEED, &flag) == ESP_OK && flag == 1);
            nvs_close(h_chk);
        }
        if (!pw_seeded) {
            strlcpy(lc->web_password, CONFIG_DEFAULT_WEB_PASSWORD, sizeof(lc->web_password));
        }
    }

    /* 翻译 + 写逐键 */
    cam_config_t migrated;
    map_legacy_to_config(lc, &migrated);
    free(lc);

    if (nvs_open(NVS_NS, NVS_READWRITE, &h_new) != ESP_OK) {
        return false;
    }
    write_keys_to_nvs(h_new, &migrated);
    if (!pw_seeded) {
        nvs_set_u8(h_new, KEY_PW_SEED, 1);
    }
    nvs_commit(h_new);
    nvs_close(h_new);

    /* legacy key 改名备份（读旧 blob 原文，写回 config_bak，删 config） */
    if (nvs_open(NVS_NS_LEGACY, NVS_READWRITE, &h_old) == ESP_OK) {
        nvs_handle_t h_rd;
        if (nvs_open(NVS_NS_LEGACY, NVS_READONLY, &h_rd) == ESP_OK) {
            size_t len2 = 0;
            if (nvs_get_blob(h_rd, "config", NULL, &len2) == ESP_OK && len2 > 0 && len2 <= 4096) {
                void *buf = malloc(len2);
                if (buf && nvs_get_blob(h_rd, "config", buf, &len2) == ESP_OK) {
                    nvs_set_blob(h_old, KEY_LEGACY_BAK, buf, len2);
                    nvs_erase_key(h_old, "config");
                    nvs_commit(h_old);
                }
                free(buf);
            }
            nvs_close(h_rd);
        }
        nvs_close(h_old);
    }

    ESP_LOGI(TAG, "Legacy blob (%u bytes) migrated to per-key NVS (schema_ver=%d)",
             (unsigned)blob_len, CONFIG_SCHEMA_VERSION);
    return true;
}

/* ──────────────────────────────────────────────────────────────────
 * Public API
 * ────────────────────────────────────────────────────────────────── */

esp_err_t config_init(void)
{
    if (s_config_initialized) {
        return ESP_OK;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }

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

    migrate_legacy_blob();

    apply_defaults(&s_config);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        load_keys_from_nvs(h, &s_config);
        nvs_close(h);
    } else {
        /* 首次出厂：落一份默认值（含 schema_ver） */
        ESP_LOGW(TAG, "No per-key config yet, saving defaults");
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            write_keys_to_nvs(h, &s_config);
            nvs_commit(h);
            nvs_close(h);
        }
    }

    /* 板级边界钳制（契约 §4）：旧值 q<10 超JPEG fb 预算（PIT-021）、
     * 分辨率越界（家族刻度 10-15）→ 加载时钳回 */
    if (s_config.cam_quality < CAMERA_QUALITY_MIN) {
        ESP_LOGW(TAG, "cam_quality=%u clamped to %d on load", s_config.cam_quality, CAMERA_QUALITY_MIN);
        s_config.cam_quality = CAMERA_QUALITY_MIN;
    } else if (s_config.cam_quality > CAMERA_QUALITY_MAX) {
        s_config.cam_quality = CAMERA_QUALITY_MAX;
    }
    if (s_config.cam_framesize < FRAMESIZE_VGA || s_config.cam_framesize > FRAMESIZE_UXGA) {
        ESP_LOGW(TAG, "cam_framesize=%u out of family scale, defaulting to VGA", s_config.cam_framesize);
        s_config.cam_framesize = CAMERA_RES_VGA;
    }
    if (s_config.cam_fps == 0 || s_config.cam_fps > 30) {
        s_config.cam_fps = 15;
    }
    /* 契约 v1.1：空密码迁移到家族统一默认 */
    if (s_config.web_password[0] == '\0') {
        strlcpy(s_config.web_password, CONFIG_DEFAULT_WEB_PASSWORD, sizeof(s_config.web_password));
    }

    ESP_LOGI(TAG, "Config loaded (device=%s, wifi_ssid='%s', schema v%d)",
             s_config.device_name, s_config.wifi_ssid, CONFIG_SCHEMA_VERSION);
    s_config_initialized = true;
    return ESP_OK;
}

const cam_config_t *config_get(void)
{
    return &s_config;
}

void config_get_copy(cam_config_t *out)
{
    if (!out) return;
    config_lock();
    *out = s_config;
    config_unlock();
}

esp_err_t config_save(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    config_lock();
    write_keys_to_nvs(handle, &s_config);
    config_unlock();
    ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Config saved: wifi_ssid='%s' device=%s", s_config.wifi_ssid, s_config.device_name);
    return ESP_OK;
}

esp_err_t config_reset(void)
{
    ESP_LOGW(TAG, "Resetting config to defaults");
    config_lock();
    apply_defaults(&s_config);
    config_unlock();
    return config_save();
}

bool config_is_valid(void)
{
    /* 逐键方案无 blob magic；初始化完成 + schema 落盘即为有效 */
    return s_config_initialized;
}

/* ── Typed setters（写后立即落盘，契约 §1） ── */

static esp_err_t set_and_save(void)
{
    return config_save();
}

esp_err_t config_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !pass) {
        return ESP_ERR_INVALID_ARG;
    }
    config_lock();
    strlcpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid));
    strlcpy(s_config.wifi_pass, pass, sizeof(s_config.wifi_pass));
    config_unlock();
    ESP_LOGI(TAG, "WiFi set (ssid=%s, pass=***)", ssid);
    return set_and_save();
}

esp_err_t config_set_wifi_secondary(const char *ssid, const char *pass)
{
    config_lock();
    if (!ssid) {
        memset(s_config.wifi_ssid_2, 0, sizeof(s_config.wifi_ssid_2));
        memset(s_config.wifi_pass_2, 0, sizeof(s_config.wifi_pass_2));
    } else {
        strlcpy(s_config.wifi_ssid_2, ssid, sizeof(s_config.wifi_ssid_2));
        if (pass && strlen(pass) > 0) {
            strlcpy(s_config.wifi_pass_2, pass, sizeof(s_config.wifi_pass_2));
        }
    }
    config_unlock();
    ESP_LOGI(TAG, "Secondary WiFi set (ssid=%s)", s_config.wifi_ssid_2);
    return set_and_save();
}

esp_err_t config_set_allow_ap_fallback(uint8_t allow)
{
    config_lock();
    s_config.allow_ap_fallback = allow ? 1 : 0;
    config_unlock();
    return set_and_save();
}

esp_err_t config_set_resolution(camera_resolution_t res)
{
    if (res < CAMERA_RES_VGA || res > CAMERA_RES_UXGA) {
        return ESP_ERR_INVALID_ARG;
    }
    config_lock();
    s_config.cam_framesize = (uint8_t)res;
    config_unlock();
    ESP_LOGI(TAG, "Resolution set to %d", s_config.cam_framesize);
    return set_and_save();
}

esp_err_t config_set_web_password(const char *pass)
{
    if (!pass) {
        return ESP_ERR_INVALID_ARG;
    }
    config_lock();
    strlcpy(s_config.web_password, pass, sizeof(s_config.web_password));
    config_unlock();
    ESP_LOGI(TAG, "Web password set (pass=***)");
    return set_and_save();
}

/* 家族 motion 超集（契约 §3.2） */
esp_err_t config_set_motion(uint8_t enabled, uint8_t sensitivity, uint16_t cooldown_s, uint8_t active_interval_s)
{
    if (sensitivity > 100) sensitivity = 100;
    if (cooldown_s < 1) cooldown_s = 1;
    if (cooldown_s > 300) cooldown_s = 300;
    if (active_interval_s < 1) active_interval_s = 1;
    if (active_interval_s > 30) active_interval_s = 30;
    config_lock();
    s_config.motion_enabled = enabled ? 1 : 0;
    s_config.motion_sensitivity = sensitivity;
    s_config.motion_cooldown_s = cooldown_s;
    s_config.motion_active_interval_s = active_interval_s;
    config_unlock();
    ESP_LOGI(TAG, "Motion set (en=%u, sens=%u, cooldown=%us, active=%us)",
             enabled, sensitivity, cooldown_s, active_interval_s);
    return set_and_save();
}

esp_err_t config_set_device_name(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }
    config_lock();
    strlcpy(s_config.device_name, name, sizeof(s_config.device_name));
    config_unlock();
    ESP_LOGI(TAG, "Device name set to %s", s_config.device_name);
    return set_and_save();
}

esp_err_t config_set_cam_fps(uint8_t fps)
{
    if (fps == 0) fps = 1;
    if (fps > 30) fps = 30;
    config_lock();
    s_config.cam_fps = fps;
    config_unlock();
    ESP_LOGI(TAG, "FPS set to %u", fps);
    return set_and_save();
}

esp_err_t config_set_vflip(uint8_t vflip)
{
    config_lock();
    s_config.cam_vflip = vflip ? 1 : 0;
    config_unlock();
    return set_and_save();
}

esp_err_t config_set_wifi_power(uint8_t tx_power, uint8_t power_save)
{
    if (tx_power < 8) tx_power = 8;
    if (tx_power > 84) tx_power = 84;  /* ESP32 max 21dBm */
    config_lock();
    s_config.wifi_tx_power = tx_power;
    s_config.wifi_power_save = power_save ? 1 : 0;
    config_unlock();
    ESP_LOGI(TAG, "WiFi power set (tx=%u=%.1fdBm, ps=%s)",
             tx_power, tx_power * 0.25f, power_save ? "on" : "off");
    return set_and_save();
}

esp_err_t config_set_jpeg_quality(uint8_t quality)
{
    /* q<10 overflows the driver's w*h/5 JPEG frame buffer (PIT-021) */
    if (quality < CAMERA_QUALITY_MIN) quality = CAMERA_QUALITY_MIN;
    if (quality > CAMERA_QUALITY_MAX) quality = CAMERA_QUALITY_MAX;
    config_lock();
    s_config.cam_quality = quality;
    config_unlock();
    ESP_LOGI(TAG, "JPEG quality set to %u", quality);
    return set_and_save();
}

esp_err_t config_set_flash_threshold(uint8_t threshold)
{
    config_lock();
    s_config.flash_threshold = threshold;
    config_unlock();
    return set_and_save();
}

esp_err_t config_set_onvif_enable(uint8_t enable)
{
    config_lock();
    s_config.onvif_enable = enable ? 1 : 0;
    config_unlock();
    ESP_LOGI(TAG, "ONVIF %s (applies after reboot)", s_config.onvif_enable ? "enabled" : "disabled");
    return set_and_save();
}

esp_err_t config_set_save_to_sd(uint8_t enabled)
{
    config_lock();
    s_config.save_to_sd = enabled ? 1 : 0;
    config_unlock();
    ESP_LOGI(TAG, "SD card write %s", s_config.save_to_sd ? "enabled" : "disabled");
    return set_and_save();
}

esp_err_t config_set_sd_log_enabled(uint8_t enabled)
{
    config_lock();
    s_config.sd_log_enabled = enabled ? 1 : 0;
    config_unlock();
    ESP_LOGI(TAG, "SD error logging %s", s_config.sd_log_enabled ? "enabled" : "disabled");
    return set_and_save();
}

esp_err_t config_set_wifi_reconnect_interval(uint16_t hours)
{
    config_lock();
    s_config.wifi_reconnect_hours = hours;
    config_unlock();
    return set_and_save();
}

esp_err_t config_set_wifi_roam(int8_t rssi, uint8_t gap_s)
{
    if (rssi > -40) rssi = -40;     /* sane upper bound */
    if (rssi < -100) rssi = -100;   /* sane lower bound */
    if (gap_s < 3) gap_s = 3;       /* avoid flapping */
    if (gap_s > 30) gap_s = 30;
    config_lock();
    s_config.wifi_roam_rssi = rssi;
    s_config.wifi_roam_gap_s = gap_s;
    config_unlock();
    ESP_LOGI(TAG, "WiFi roam set (rssi=%d dBm, gap=%u dBm)", rssi, gap_s);
    return set_and_save();
}

esp_err_t config_set_xclk_freq(uint8_t mhz)
{
    if (mhz != 10 && mhz != 16 && mhz != 20) mhz = 20;
    config_lock();
    s_config.xclk_freq_mhz = mhz;
    config_unlock();
    ESP_LOGI(TAG, "Camera XCLK set to %u MHz", mhz);
    return set_and_save();
}

esp_err_t config_set_timelapse(uint8_t enabled, uint16_t interval_s, uint8_t burst_count)
{
    if (interval_s < 1) interval_s = 1;
    if (burst_count < 1) burst_count = 1;
    if (burst_count > 10) burst_count = 10;
    config_lock();
    s_config.timelapse_enabled = enabled ? 1 : 0;
    s_config.timelapse_interval_s = interval_s;
    s_config.timelapse_burst_count = burst_count;
    config_unlock();
    ESP_LOGI(TAG, "Timelapse set (enabled=%u, interval=%us, burst=%u)", enabled, interval_s, burst_count);
    return set_and_save();
}

esp_err_t config_set_timelapse_dynamic(uint8_t mode, uint16_t min_interval, uint16_t max_interval, uint8_t decay_factor, uint16_t decay_period)
{
    config_lock();
    s_config.timelapse_mode = mode;
    s_config.timelapse_min_interval_s = min_interval;
    s_config.timelapse_max_interval_s = max_interval;
    s_config.timelapse_decay_factor = decay_factor;
    s_config.timelapse_decay_period_s = decay_period;
    config_unlock();
    ESP_LOGI(TAG, "Timelapse dynamic set (mode=%u, min=%us, max=%us, decay=%u, period=%us)",
             mode, min_interval, max_interval, decay_factor, decay_period);
    return set_and_save();
}

esp_err_t config_set_timezone(const char *tz)
{
    if (!tz || strlen(tz) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    config_lock();
    strlcpy(s_config.timezone, tz, sizeof(s_config.timezone));
    config_unlock();
    ESP_LOGI(TAG, "Timezone set to %s", s_config.timezone);
    return set_and_save();
}

esp_err_t config_set_recording(uint8_t mode, uint16_t segment_sec, uint8_t frame_drop)
{
    if (mode > 2) mode = 0;
    if (segment_sec < 10) segment_sec = 10;
    config_lock();
    s_config.record_mode = mode;
    s_config.segment_sec = segment_sec;
    s_config.frame_drop_enabled = frame_drop ? 1 : 0;
    config_unlock();
    ESP_LOGI(TAG, "Recording set (mode=%u, segment=%us, frame_drop=%u)", mode, segment_sec, frame_drop);
    return set_and_save();
}

esp_err_t config_set_cleanup(uint8_t low_pct, uint8_t high_pct)
{
    if (low_pct > 99) low_pct = 99;
    if (high_pct > 99) high_pct = 99;
    config_lock();
    s_config.cleanup_low_pct = low_pct;
    s_config.cleanup_high_pct = high_pct;
    config_unlock();
    ESP_LOGI(TAG, "Cleanup set (low=%u%%, high=%u%%)", low_pct, high_pct);
    return set_and_save();
}

/* ── SD provisioning（契约 §9：家族统一 KEY=VALUE 格式 + legacy 兼容读） ── */

static void parse_kv_line(char *line, char **key, char **value)
{
    char *eq = strchr(line, '=');
    if (!eq) {
        *key = NULL;
        return;
    }
    *eq = '\0';
    *key = line;
    *value = eq + 1;
    /* trim trailing whitespace of value */
    char *end = *value + strlen(*value) - 1;
    while (end >= *value && (*end == '\r' || *end == '\n' || *end == ' ')) {
        *end = '\0';
        end--;
    }
}

static bool sd_file_has_one_time(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    char line[64];
    bool first = true, one_time = false;
    while (fgets(line, sizeof(line), fp)) {
        char *end = line + strlen(line) - 1;
        while (end >= line && (*end == '\r' || *end == '\n' || *end == ' ')) { *end = '\0'; end--; }
        if (first) {
            first = false;
            if (strcmp(line, "one_time") == 0) one_time = true;
            break;
        }
    }
    fclose(fp);
    return one_time;
}

esp_err_t config_load_from_sd(void)
{
    /* 优先家族格式 /sdcard/config/wifi.txt；legacy /sdcard/config.txt
     * （wifi.ssid= 风格）保留一个版本的兼容读取（契约 §9 已废止该风格） */
    const char *path = "/sdcard/config/wifi.txt";
    bool legacy_fmt = false;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        path = "/sdcard/config.txt";
        legacy_fmt = true;
        fp = fopen(path, "r");
        if (!fp) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    char line[160];
    char new_ssid[33] = {0}, new_pass[65] = {0};
    char new_ssid_2[33] = {0}, new_pass_2[65] = {0};
    bool ssid_found = false, pass_found = false, ssid2_found = false;
    bool one_time = sd_file_has_one_time(path);

    while (fgets(line, sizeof(line), fp)) {
        char *end = line + strlen(line) - 1;
        while (end >= line && (*end == '\r' || *end == '\n' || *end == ' ')) { *end = '\0'; end--; }
        if (line[0] == '\0' || line[0] == '#') continue;

        char *key = NULL, *value = NULL;
        parse_kv_line(line, &key, &value);
        if (!key) continue;

        bool hit = false;
        if (legacy_fmt) {
            hit = true;
            if (strcmp(key, "wifi.ssid") == 0) strlcpy(new_ssid, value, sizeof(new_ssid)), ssid_found = true;
            else if (strcmp(key, "wifi.password") == 0) strlcpy(new_pass, value, sizeof(new_pass)), pass_found = true;
            else if (strcmp(key, "wifi2.ssid") == 0) strlcpy(new_ssid_2, value, sizeof(new_ssid_2)), ssid2_found = true;
            else if (strcmp(key, "wifi2.password") == 0) strlcpy(new_pass_2, value, sizeof(new_pass_2));
            else hit = false;
        } else {
            hit = true;
            if (strcmp(key, "wifi_ssid") == 0) strlcpy(new_ssid, value, sizeof(new_ssid)), ssid_found = true;
            else if (strcmp(key, "wifi_pass") == 0) strlcpy(new_pass, value, sizeof(new_pass)), pass_found = true;
            else if (strcmp(key, "wifi_ssid_2") == 0) strlcpy(new_ssid_2, value, sizeof(new_ssid_2)), ssid2_found = true;
            else if (strcmp(key, "wifi_pass_2") == 0) strlcpy(new_pass_2, value, sizeof(new_pass_2));
            else hit = false;
        }
        (void)hit;
    }
    fclose(fp);

    if (!ssid_found || !pass_found) {
        ESP_LOGW(TAG, "%s incomplete (ssid=%d, password=%d)", path, ssid_found, pass_found);
        return ESP_ERR_NOT_FOUND;
    }

    /* 安全红线（契约 §9）：NVS 已有凭据时绝不覆盖 */
    if (s_config.wifi_ssid[0] != '\0' && s_config.wifi_pass[0] != '\0') {
        ESP_LOGI(TAG, "NVS WiFi already configured ('%s'), skipping SD override", s_config.wifi_ssid);
        if (one_time) remove(path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "WiFi from SD (%s): ssid='%s' ssid_2='%s'", path, new_ssid, new_ssid_2);
    esp_err_t ret = config_set_wifi(new_ssid, new_pass);
    if (ret != ESP_OK) return ret;
    if (ssid2_found) {
        config_set_wifi_secondary(new_ssid_2, new_pass_2);
    }
    if (one_time) {
        remove(path);
        ESP_LOGI(TAG, "SD provisioning file removed (one_time)");
    }
    return ESP_OK;
}

/* ── JSON 导出（契约字段名；密码类掩码） ── */

cJSON *config_get_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    const cam_config_t *cfg = config_get();

    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);
    cJSON_AddStringToObject(root, "device_name", cfg->device_name);
    cJSON_AddNumberToObject(root, "cam_framesize", (double)cfg->cam_framesize);
    cJSON_AddNumberToObject(root, "cam_fps", (double)cfg->cam_fps);
    cJSON_AddNumberToObject(root, "cam_quality", (double)cfg->cam_quality);
    cJSON_AddStringToObject(root, "web_password", "****");  /* masked（契约 v1.1） */
    cJSON_AddStringToObject(root, "timezone", cfg->timezone);
    cJSON_AddNumberToObject(root, "motion_enabled", (double)cfg->motion_enabled);
    cJSON_AddNumberToObject(root, "motion_sensitivity", (double)cfg->motion_sensitivity);
    cJSON_AddNumberToObject(root, "motion_cooldown_s", (double)cfg->motion_cooldown_s);
    cJSON_AddNumberToObject(root, "motion_active_interval_s", (double)cfg->motion_active_interval_s);
    cJSON_AddNumberToObject(root, "cam_vflip", (double)cfg->cam_vflip);
    cJSON_AddNumberToObject(root, "wifi_tx_power", (double)cfg->wifi_tx_power);
    cJSON_AddNumberToObject(root, "wifi_power_save", (double)cfg->wifi_power_save);
    cJSON_AddNumberToObject(root, "flash_threshold", (double)cfg->flash_threshold);
    cJSON_AddNumberToObject(root, "timelapse_enabled", (double)cfg->timelapse_enabled);
    cJSON_AddNumberToObject(root, "timelapse_interval_s", (double)cfg->timelapse_interval_s);
    cJSON_AddNumberToObject(root, "timelapse_burst_count", (double)cfg->timelapse_burst_count);
    cJSON_AddNumberToObject(root, "timelapse_mode", (double)cfg->timelapse_mode);
    cJSON_AddNumberToObject(root, "timelapse_min_interval_s", (double)cfg->timelapse_min_interval_s);
    cJSON_AddNumberToObject(root, "timelapse_max_interval_s", (double)cfg->timelapse_max_interval_s);
    cJSON_AddNumberToObject(root, "timelapse_decay_factor", (double)cfg->timelapse_decay_factor);
    cJSON_AddNumberToObject(root, "timelapse_decay_period_s", (double)cfg->timelapse_decay_period_s);
    cJSON_AddNumberToObject(root, "record_mode", (double)cfg->record_mode);
    cJSON_AddNumberToObject(root, "segment_sec", (double)cfg->segment_sec);
    cJSON_AddNumberToObject(root, "frame_drop_enabled", (double)cfg->frame_drop_enabled);
    cJSON_AddNumberToObject(root, "cleanup_low_pct", (double)cfg->cleanup_low_pct);
    cJSON_AddNumberToObject(root, "cleanup_high_pct", (double)cfg->cleanup_high_pct);
    cJSON_AddStringToObject(root, "wifi_ssid_2", cfg->wifi_ssid_2);
    cJSON_AddNumberToObject(root, "allow_ap_fallback", (double)cfg->allow_ap_fallback);
    cJSON_AddNumberToObject(root, "save_to_sd", (double)cfg->save_to_sd);
    cJSON_AddNumberToObject(root, "sd_log_enabled", (double)cfg->sd_log_enabled);
    cJSON_AddNumberToObject(root, "wifi_reconnect_hours", (double)cfg->wifi_reconnect_hours);
    cJSON_AddNumberToObject(root, "xclk_freq_mhz", (double)cfg->xclk_freq_mhz);
    cJSON_AddNumberToObject(root, "wifi_roam_rssi", (double)cfg->wifi_roam_rssi);
    cJSON_AddNumberToObject(root, "wifi_roam_gap_s", (double)cfg->wifi_roam_gap_s);
    cJSON_AddNumberToObject(root, "onvif_enable", (double)cfg->onvif_enable);
    cJSON_AddNumberToObject(root, "schema_version", (double)CONFIG_SCHEMA_VERSION);

    return root;
}

const char *config_get_web_password(void)
{
    return s_config.web_password;
}
