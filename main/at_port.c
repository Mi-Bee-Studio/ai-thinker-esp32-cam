/*
 * at_port.c — ai-thinker 板级 port 层（家族 AT 核心 at_command.c 的钩子）
 *
 * IO：UART0（CH340，/dev/ttyUSB0）115200-8N1，VFS+fgets（承袭 serial_config.c
 * 的成熟路径）。生效语义（契约 §6）：分辨率/画质热重配；WiFi 保存+重启。
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"

#include "at_port.h"
#include "config_manager.h"
#include "camera_driver.h"
#include "wifi_manager.h"
#include "storage_manager.h"

static const char *TAG = "at_port";

/* ── 能力裁剪 ──────────────────────────────────────────────────── */

static const at_caps_t s_caps = {
    .wifi_scan = true,
    .cfg       = true,
};

const at_caps_t *at_port_caps(void)
{
    return &s_caps;
}

/* ── IO：UART0 + VFS（fgets 阻塞行读） ─────────────────────────── */

esp_err_t at_port_init(void)
{
    if (!uart_is_driver_installed(UART_NUM_0)) {
        uart_driver_install(UART_NUM_0, 512, 512, 0, NULL, 0);
    }
    /* VFS 从 ROM（只出）切到 UART 驱动（双向），stdin 方可 fgets */
    uart_vfs_dev_use_driver(UART_NUM_0);
    return ESP_OK;
}

int at_port_getline(char *buf, int len)
{
    if (!fgets(buf, len, stdin)) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return -1;
    }
    /* 去 \r\n */
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) {
        buf[--n] = '\0';
    }
    fflush(stdout);
    return (int)n;
}

void at_port_write(const char *s)
{
    fputs(s, stdout);
    fflush(stdout);
}

/* ── GMR / 传感器 ──────────────────────────────────────────────── */

void at_port_gmr_info(at_gmr_info_t *out)
{
    strlcpy(out->board, "ai-thinker", sizeof(out->board));
    strlcpy(out->sensor, camera_get_sensor_name(), sizeof(out->sensor));
}

/* ── WiFi ──────────────────────────────────────────────────────── */

void at_port_wifi_info(at_wifi_info_t *out)
{
    memset(out, 0, sizeof(*out));
    switch (wifi_get_state()) {
        case WIFI_STATE_AP:              strlcpy(out->state, "ap", sizeof(out->state)); break;
        case WIFI_STATE_STA_CONNECTING:  strlcpy(out->state, "connecting", sizeof(out->state)); break;
        case WIFI_STATE_STA_CONNECTED:   strlcpy(out->state, "connected", sizeof(out->state)); break;
        default:                         strlcpy(out->state, "disconnected", sizeof(out->state)); break;
    }
    strlcpy(out->ssid, config_get()->wifi_ssid, sizeof(out->ssid));
    strlcpy(out->ip, wifi_get_ip_str(), sizeof(out->ip));

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            snprintf(out->gw, sizeof(out->gw), IPSTR, IP2STR(&ip.gw));
            snprintf(out->mask, sizeof(out->mask), IPSTR, IP2STR(&ip.netmask));
        }
    }
}

esp_err_t at_port_wifi_set(const char *ssid, const char *pass)
{
    esp_err_t ret = config_set_wifi(ssid, pass);
    if (ret != ESP_OK) {
        return ret;
    }
    at_port_write("+REBOOTING: wifi credentials saved\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;   /* unreachable */
}

esp_err_t at_port_wifi_scan(at_scan_emit_fn emit)
{
    /* 本板 WiFi RF 较弱，扫描会短暂中断 STA 流量，属预期行为（同 /api/scan） */
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 20) n = 20;

    wifi_ap_record_t *recs = malloc(sizeof(wifi_ap_record_t) * (n ? n : 1));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    esp_wifi_scan_get_ap_records(&n, recs);

    /* RSSI 降序（契约 §2 与 /api/scan 一致） */
    for (int i = 1; i < (int)n; i++) {
        wifi_ap_record_t key = recs[i];
        int j = i - 1;
        while (j >= 0 && recs[j].rssi < key.rssi) {
            recs[j + 1] = recs[j];
            j--;
        }
        recs[j + 1] = key;
    }
    for (int i = 0; i < (int)n; i++) {
        emit((const char *)recs[i].ssid, recs[i].rssi, recs[i].authmode);
    }
    free(recs);
    return ESP_OK;
}

/* ── 摄像头 ────────────────────────────────────────────────────── */

bool at_port_cam_res_info(at_cam_res_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->cur = config_get()->cam_framesize;
    out->cur_label = camera_res_label(out->cur);

    /* camera_driver 的表 → 家族 AT 表类型桥接（布局同构） */
    static at_res_opt_t s_opts[8];
    int n = 0;
    const camera_res_opt_t *src = camera_supported_resolutions(&n);
    if (n > (int)(sizeof(s_opts) / sizeof(s_opts[0]))) n = (int)(sizeof(s_opts) / sizeof(s_opts[0]));
    for (int i = 0; i < n; i++) {
        s_opts[i].value = src[i].value;
        s_opts[i].label = src[i].label;
    }
    out->opts = s_opts;
    out->opt_count = n;

    out->cap = camera_get_effective_max_res();
    out->cap_source = camera_res_cap_source();
    return true;
}

esp_err_t at_port_cam_res_set(int value)
{
    /* 合法性 = 在板级可选表内 且 ≤ 三层上限（与 POST /api/camera 同源） */
    at_cam_res_info_t r;
    if (!at_port_cam_res_info(&r)) {
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = value <= r.cap;
    if (ok) {
        ok = false;
        for (int i = 0; i < r.opt_count; i++) {
            if (r.opts[i].value == value) { ok = true; break; }
        }
    }
    if (!ok) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = config_set_resolution((camera_resolution_t)value);
    if (ret != ESP_OK) {
        return ret;
    }
    /* 本板生效语义：热重配（契约 §6） */
    const cam_config_t *cfg = config_get();
    ret = camera_apply_settings(cfg->cam_framesize, cfg->cam_fps, cfg->cam_quality);
    if (ret != ESP_OK) {
        return ret;
    }
    at_port_write("+APPLY: hot-reconfig done\r\n");
    return ESP_OK;
}

int at_port_cam_qual_get(int *qmin, int *qmax)
{
    if (qmin) *qmin = CAMERA_QUALITY_MIN;
    if (qmax) *qmax = CAMERA_QUALITY_MAX;
    return config_get()->cam_quality;
}

esp_err_t at_port_cam_qual_set(int value)
{
    if (value < CAMERA_QUALITY_MIN || value > CAMERA_QUALITY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = config_set_jpeg_quality((uint8_t)value);
    if (ret != ESP_OK) {
        return ret;
    }
    /* 本板生效语义：热重配（契约 §6） */
    const cam_config_t *cfg = config_get();
    ret = camera_apply_settings(cfg->cam_framesize, cfg->cam_fps, cfg->cam_quality);
    if (ret != ESP_OK) {
        return ret;
    }
    at_port_write("+APPLY: hot-reconfig done\r\n");
    return ESP_OK;
}

/* ── STATUS 板级增量行 ─────────────────────────────────────────── */

void at_port_status_extra(void (*emit)(const char *name, const char *value))
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", storage_is_available() ? 1 : 0);
    emit("sd", buf);
}

/* ── 系统动作 ──────────────────────────────────────────────────── */

void at_port_reboot(void)
{
    at_port_write("+REBOOTING\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

void at_port_restore(void)
{
    at_port_write("+RESTORING: factory reset\r\n");
    config_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/* ── CFGGET/CFGSET 白名单（契约 §2；读侧自动剔除 secret） ──────── */

/* 泛型读取：按 offset/type 从快照取值（单一事实源 = struct 布局） */
typedef struct {
    const char *name;
    at_cfg_type_t type;
    bool secret;
    size_t off;
} port_field_t;

static const port_field_t s_fields[] = {
    { "device_name",           AT_CFG_STR, false, offsetof(cam_config_t, device_name) },
    { "wifi_ssid",             AT_CFG_STR, false, offsetof(cam_config_t, wifi_ssid) },
    { "wifi_pass",             AT_CFG_STR, true,  offsetof(cam_config_t, wifi_pass) },
    { "wifi_ssid_2",           AT_CFG_STR, false, offsetof(cam_config_t, wifi_ssid_2) },
    { "wifi_pass_2",           AT_CFG_STR, true,  offsetof(cam_config_t, wifi_pass_2) },
    { "web_password",          AT_CFG_STR, true,  offsetof(cam_config_t, web_password) },
    { "timezone",              AT_CFG_STR, false, offsetof(cam_config_t, timezone) },
    { "cam_framesize",         AT_CFG_U8,  false, offsetof(cam_config_t, cam_framesize) },
    { "cam_fps",               AT_CFG_U8,  false, offsetof(cam_config_t, cam_fps) },
    { "cam_quality",           AT_CFG_U8,  false, offsetof(cam_config_t, cam_quality) },
    { "cam_vflip",             AT_CFG_U8,  false, offsetof(cam_config_t, cam_vflip) },
    { "xclk_freq_mhz",         AT_CFG_U8,  false, offsetof(cam_config_t, xclk_freq_mhz) },
    { "onvif_enable",          AT_CFG_U8,  false, offsetof(cam_config_t, onvif_enable) },
    { "motion_enabled",        AT_CFG_U8,  false, offsetof(cam_config_t, motion_enabled) },
    { "motion_sensitivity",    AT_CFG_U8,  false, offsetof(cam_config_t, motion_sensitivity) },
    { "motion_cooldown_s",     AT_CFG_U16, false, offsetof(cam_config_t, motion_cooldown_s) },
    { "motion_active_interval_s", AT_CFG_U8, false, offsetof(cam_config_t, motion_active_interval_s) },
    { "flash_threshold",       AT_CFG_U8,  false, offsetof(cam_config_t, flash_threshold) },
};

static void field_get_generic(const port_field_t *f, char *buf, size_t len)
{
    cam_config_t snap;
    config_get_copy(&snap);
    const void *p = (const uint8_t *)&snap + f->off;
    switch (f->type) {
        case AT_CFG_STR: snprintf(buf, len, "%s", (const char *)p); break;
        case AT_CFG_U8:  snprintf(buf, len, "%u", (unsigned)*(const uint8_t *)p); break;
        case AT_CFG_U16: snprintf(buf, len, "%u", (unsigned)*(const uint16_t *)p); break;
        case AT_CFG_I8:  snprintf(buf, len, "%d", (int)*(const int8_t *)p); break;
    }
}

typedef struct { const char *name; at_cfg_type_t type; bool secret; } at_cfg_row_t;

static void cfg_get_device_name(char *buf, size_t len)   { field_get_generic(&s_fields[0],  buf, len); }
static void cfg_get_wifi_ssid(char *buf, size_t len)     { field_get_generic(&s_fields[1],  buf, len); }
static void cfg_get_wifi_ssid_2(char *buf, size_t len)   { field_get_generic(&s_fields[3],  buf, len); }
static void cfg_get_timezone(char *buf, size_t len)      { field_get_generic(&s_fields[6],  buf, len); }
static void cfg_get_cam_framesize(char *buf, size_t len) { field_get_generic(&s_fields[7],  buf, len); }
static void cfg_get_cam_fps(char *buf, size_t len)       { field_get_generic(&s_fields[8],  buf, len); }
static void cfg_get_cam_quality(char *buf, size_t len)   { field_get_generic(&s_fields[9],  buf, len); }
static void cfg_get_cam_vflip(char *buf, size_t len)     { field_get_generic(&s_fields[10], buf, len); }
static void cfg_get_xclk(char *buf, size_t len)          { field_get_generic(&s_fields[11], buf, len); }
static void cfg_get_onvif(char *buf, size_t len)         { field_get_generic(&s_fields[12], buf, len); }
static void cfg_get_motion_en(char *buf, size_t len)     { field_get_generic(&s_fields[13], buf, len); }
static void cfg_get_motion_sens(char *buf, size_t len)   { field_get_generic(&s_fields[14], buf, len); }
static void cfg_get_motion_cool(char *buf, size_t len)   { field_get_generic(&s_fields[15], buf, len); }
static void cfg_get_motion_act(char *buf, size_t len)    { field_get_generic(&s_fields[16], buf, len); }
static void cfg_get_flash_thr(char *buf, size_t len)     { field_get_generic(&s_fields[17], buf, len); }

/* 写侧：走 config_manager 类型化 setter（含校验/落盘；凭据单写不清对端） */
static esp_err_t cfg_set_wifi_ssid(const char *v)
{
    return config_set_wifi(v, config_get()->wifi_pass);
}
static esp_err_t cfg_set_wifi_pass(const char *v)
{
    return config_set_wifi(config_get()->wifi_ssid, v);
}
static esp_err_t cfg_set_wifi_ssid_2(const char *v)
{
    return config_set_wifi_secondary(v, config_get()->wifi_pass_2);
}
static esp_err_t cfg_set_wifi_pass_2(const char *v)
{
    return config_set_wifi_secondary(config_get()->wifi_ssid_2, v);
}
static esp_err_t cfg_set_web_password(const char *v)
{
    if (strlen(v) < 6) return ESP_ERR_INVALID_ARG;
    return config_set_web_password(v);
}
static esp_err_t cfg_set_cam_framesize(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end) return ESP_ERR_INVALID_ARG;
    return at_port_cam_res_set((int)val) == ESP_OK ? ESP_OK : ESP_ERR_INVALID_ARG;
}
static esp_err_t cfg_set_cam_fps(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end || val < 1 || val > 30) return ESP_ERR_INVALID_ARG;
    return config_set_cam_fps((uint8_t)val);
}
static esp_err_t cfg_set_cam_quality(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end || val < CAMERA_QUALITY_MIN || val > CAMERA_QUALITY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return config_set_jpeg_quality((uint8_t)val);
}
static esp_err_t cfg_set_cam_vflip(const char *v)
{
    return config_set_vflip((*v == '1') ? 1 : 0);
}
static esp_err_t cfg_set_xclk(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end) return ESP_ERR_INVALID_ARG;
    return config_set_xclk_freq((uint8_t)val);
}
static esp_err_t cfg_set_onvif(const char *v)
{
    return config_set_onvif_enable((*v == '1') ? 1 : 0);
}
/* motion 组：以快照补全四参后走组 setter */
static esp_err_t cfg_set_motion_en(const char *v)
{
    const cam_config_t *c = config_get();
    return config_set_motion((*v == '1') ? 1 : 0, c->motion_sensitivity,
                             c->motion_cooldown_s, c->motion_active_interval_s);
}
static esp_err_t cfg_set_motion_sens(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end || val < 0 || val > 100) return ESP_ERR_INVALID_ARG;
    const cam_config_t *c = config_get();
    return config_set_motion(c->motion_enabled, (uint8_t)val,
                             c->motion_cooldown_s, c->motion_active_interval_s);
}
static esp_err_t cfg_set_motion_cool(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end || val < 1 || val > 300) return ESP_ERR_INVALID_ARG;
    const cam_config_t *c = config_get();
    return config_set_motion(c->motion_enabled, c->motion_sensitivity,
                             (uint16_t)val, c->motion_active_interval_s);
}
static esp_err_t cfg_set_motion_act(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end || val < 1 || val > 30) return ESP_ERR_INVALID_ARG;
    const cam_config_t *c = config_get();
    return config_set_motion(c->motion_enabled, c->motion_sensitivity,
                             c->motion_cooldown_s, (uint8_t)val);
}
static esp_err_t cfg_set_flash_thr(const char *v)
{
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v || *end || val < 0 || val > 255) return ESP_ERR_INVALID_ARG;
    return config_set_flash_threshold((uint8_t)val);
}
static esp_err_t cfg_set_device_name(const char *v)
{
    if (!v[0]) return ESP_ERR_INVALID_ARG;
    return config_set_device_name(v);
}
static esp_err_t cfg_set_timezone(const char *v)
{
    if (!v[0]) return ESP_ERR_INVALID_ARG;
    return config_set_timezone(v);
}

/* 白名单表（get 仅非 secret 字段；set 含 secret 写入） */
static const at_cfg_field_t s_cfg_fields[] = {
    { "device_name",              AT_CFG_STR, false, cfg_get_device_name,  cfg_set_device_name },
    { "wifi_ssid",                AT_CFG_STR, false, cfg_get_wifi_ssid,    cfg_set_wifi_ssid },
    { "wifi_pass",                AT_CFG_STR, true,  NULL,                 cfg_set_wifi_pass },
    { "wifi_ssid_2",              AT_CFG_STR, false, cfg_get_wifi_ssid_2,  cfg_set_wifi_ssid_2 },
    { "wifi_pass_2",              AT_CFG_STR, true,  NULL,                 cfg_set_wifi_pass_2 },
    { "web_password",             AT_CFG_STR, true,  NULL,                 cfg_set_web_password },
    { "timezone",                 AT_CFG_STR, false, cfg_get_timezone,     cfg_set_timezone },
    { "cam_framesize",            AT_CFG_U8,  false, cfg_get_cam_framesize, cfg_set_cam_framesize },
    { "cam_fps",                  AT_CFG_U8,  false, cfg_get_cam_fps,      cfg_set_cam_fps },
    { "cam_quality",              AT_CFG_U8,  false, cfg_get_cam_quality,  cfg_set_cam_quality },
    { "cam_vflip",                AT_CFG_U8,  false, cfg_get_cam_vflip,    cfg_set_cam_vflip },
    { "xclk_freq_mhz",            AT_CFG_U8,  false, cfg_get_xclk,         cfg_set_xclk },
    { "onvif_enable",             AT_CFG_U8,  false, cfg_get_onvif,        cfg_set_onvif },
    { "motion_enabled",           AT_CFG_U8,  false, cfg_get_motion_en,    cfg_set_motion_en },
    { "motion_sensitivity",       AT_CFG_U8,  false, cfg_get_motion_sens,  cfg_set_motion_sens },
    { "motion_cooldown_s",        AT_CFG_U16, false, cfg_get_motion_cool,  cfg_set_motion_cool },
    { "motion_active_interval_s", AT_CFG_U8,  false, cfg_get_motion_act,   cfg_set_motion_act },
    { "flash_threshold",          AT_CFG_U8,  false, cfg_get_flash_thr,    cfg_set_flash_thr },
};

const at_cfg_field_t *at_port_cfg_fields(int *count)
{
    if (count) {
        *count = (int)(sizeof(s_cfg_fields) / sizeof(s_cfg_fields[0]));
    }
    return s_cfg_fields;
}

void at_port_save(void)
{
    /* 本仓所有写路径自带落盘 → 幂等空操作（契约 §2 AT+SAVE） */
}

/* ── 历史别名（契约 §4；ai-thinker：AT+DEVICE/AT+TZ 走 CFGSET 语义） ── */

const char *at_port_alias(const char *name)
{
    return NULL;
}

/* ── 板级扩展指令（契约 §5；ai-thinker 无扩展） ────────────────── */

const at_ext_cmd_t *at_port_ext_cmds(int *count)
{
    if (count) *count = 0;
    return NULL;
}
