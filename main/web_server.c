/*
 * Web Server Module Implementation
 *
 * REST API endpoints + SPIFFS static file serving for AI_Thinker ESP32-CAM.
 *
 * Endpoints:
 *   GET  /api/status   — Device status JSON
 *   GET  /api/config   — Current config JSON (passwords excluded)
 *   POST /api/config   — Partial config update (password-protected, SET_PASSWORD_FIRST support)
 *   POST /api/reset    — Reset config to defaults (password-protected)
 *   POST /api/reboot   — Reboot device after 1s delay (password-protected)
 *   GET  /api/capture  — Single JPEG frame capture
 *   GET  /stream       — MJPEG video stream (via mjpeg_streamer)
 *   GET  /metrics      — Prometheus-format metrics
 *   GET  /api/files    — List SD card photos
 *   GET  /api/download — Download photo file
 *   DELETE /api/files  — Delete SD card file (password-protected)
 *   POST /api/led      — Flash LED control (password-protected)
 *   GET  /api/capabilities — Board capability flags
 *   GET  /api/auth     — Validate password
 *   GET/POST /api/timelapse (/) — Timelapse control
 *   POST /api/record   — Recording control (password-protected)
 *   GET  /api/record   — Recording status
 *   GET  /api/storage   — SD card storage info
 *   POST /api/format   — Format SD card
 *   POST /api/ota (/)   — OTA update endpoints
 *   OPTIONS *          - CORS preflight
 *   GET    *          - SPIFFS static files (fallback)
 */
#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "cJSON.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "camera_driver.h"
#include "storage_manager.h"
#include "mjpeg_streamer.h"
#include "health_monitor.h"
#include "motion_detect.h"
#include "flash_led.h"
#include "time_sync.h"
#include "timelapse.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"   /* free_psram：本板有 4MB PSRAM（2026-09-04 API 对齐三姐妹板） */

#include "video_recorder.h"
#include "onvif_service.h"
#include "frame_broadcaster.h"
#include "esp_wifi.h"
#include "sd_log.h"
#include "ota_updater.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

/* Forward declarations for helper functions */
static esp_err_t send_json_error(httpd_req_t *req, const char *msg, int http_code);
static esp_err_t send_unauthorized(httpd_req_t *req);
static char *read_body(httpd_req_t *req, size_t max_len);
/* ------------------------------------------------------------------ */
/*  JSON / HTTP helpers                                                */
/* ------------------------------------------------------------------ */

static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, X-Password");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
}
/** @brief Check X-Password header against stored web_password */
bool check_auth(httpd_req_t *req)
{
    char password[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Password", password, sizeof(password)) == ESP_OK) {
        const cam_config_t *cfg = config_get();
        if (strcmp(password, cfg->web_password) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief Auth helper for write operations - implements SET_PASSWORD_FIRST state machine */
static esp_err_t require_auth(httpd_req_t *req, const char *uri)
{
    const cam_config_t *cfg = config_get();

    /* State A: web_password is empty - only POST /api/config with web_password field is allowed */
    if (strlen(cfg->web_password) == 0) {
        if (req->method == HTTP_POST && strcmp(uri, "/api/config") == 0) {
            /* Check if body contains web_password field */
            char *body = read_body(req, 2048);
            if (!body) {
                return send_json_error(req, "empty or invalid body", 400);
            }
            cJSON *json = cJSON_Parse(body);
            free(body);
            if (!json) {
                return send_json_error(req, "invalid JSON", 400);
            }
            cJSON *pw_item = cJSON_GetObjectItem(json, "web_password");
            bool has_password = (pw_item && cJSON_IsString(pw_item));
            cJSON_Delete(json);
            if (!has_password) {
                return send_json_error(req, "SET_PASSWORD_FIRST", 401);
            }
            /* Allow request to proceed - handler will set the password */
            return ESP_OK;
        }
        /* All other write operations return SET_PASSWORD_FIRST */
        return send_json_error(req, "SET_PASSWORD_FIRST", 401);
    }

    /* State B: web_password is set - require X-Password header */
    if (!check_auth(req)) {
        return send_unauthorized(req);
    }
    return ESP_OK;
}


/** @brief Send JSON response {"ok":true,"data":...} with CORS */
static esp_err_t send_json_ok(httpd_req_t *req, cJSON *data)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    if (data) {
        cJSON_AddItemToObject(root, "data", data);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ret;
}

/** @brief Send JSON error response with HTTP status code and CORS */
static esp_err_t send_json_error(httpd_req_t *req, const char *msg, int http_code)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    char status[32];
    snprintf(status, sizeof(status), "%d", http_code);
    httpd_resp_set_status(req, status);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", msg);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ret;
}

/** @brief Read full request body into malloc'd buffer (caller frees) */
static char *read_body(httpd_req_t *req, size_t max_len)
{
    size_t len = req->content_len;
    if (len == 0 || len > max_len) return NULL;

    char *buf = malloc(len + 1);
    if (!buf) return NULL;

    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0) {
        free(buf);
        return NULL;
    }
    buf[ret] = '\0';
    return buf;
}

/** @brief Send 401 unauthorized JSON */
static esp_err_t send_unauthorized(httpd_req_t *req)
{
    return send_json_error(req, "unauthorized", 401);
}

/* ------------------------------------------------------------------ */
/*  GET /api/status                                                    */
/* ------------------------------------------------------------------ */

/* Family scale framesize_t → short label (Contract v1.3 §5) — 单一事实源在
 * camera_driver.c（camera_res_label），此处薄封装保持调用点简洁 */
static const char *framesize_label(int val)
{
    return camera_res_label(val);
}

static esp_err_t handler_api_status(httpd_req_t *req)
{
    const health_metrics_t *m = health_monitor_get_metrics();
    const cam_config_t *cfg = config_get();

    cJSON *data = cJSON_CreateObject();

    /* Device info */
    cJSON_AddStringToObject(data, "device_name", cfg->device_name);
    cJSON_AddNumberToObject(data, "uptime", (double)m->uptime_seconds);
    const esp_app_desc_t *app_desc = esp_app_get_description();
    cJSON_AddStringToObject(data, "firmware_version",
        (app_desc && app_desc->version[0]) ? app_desc->version : "unknown");

    /* Camera — 契约 v1.0: 传感器字段名统一为 camera */
    cJSON_AddBoolToObject(data, "camera_ok", camera_is_initialized());
    cJSON_AddStringToObject(data, "camera", camera_get_sensor_name());

    cJSON_AddStringToObject(data, "resolution", framesize_label(cfg->cam_framesize));

    /* WiFi — 契约 v1.0: wifi_state 小写枚举（中文展示由前端翻译） */
    const char *wifi_state_str;
    switch (m->wifi_state) {
        case WIFI_STATE_AP:              wifi_state_str = "ap"; break;
        case WIFI_STATE_STA_CONNECTING:  wifi_state_str = "connecting"; break;
        case WIFI_STATE_STA_CONNECTED:   wifi_state_str = "connected"; break;
        case WIFI_STATE_STA_DISCONNECTED: wifi_state_str = "disconnected"; break;
        default:                         wifi_state_str = "unknown"; break;
    }
    cJSON_AddStringToObject(data, "wifi_state", wifi_state_str);
    cJSON_AddStringToObject(data, "ip", wifi_get_ip_str());
    cJSON_AddNumberToObject(data, "wifi_rssi", (double)m->wifi_rssi);

    /* 当前实际连接的网络（区别于 /api/config 里的"配置值"）：主/备槽位 + 实时 AP 信息。
     * seeed 同名字段先例：current_ssid / wifi_channel。 */
    cJSON_AddStringToObject(data, "wifi_net", wifi_using_secondary() ? "secondary" : "primary");
    if (m->wifi_state == WIFI_STATE_STA_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddStringToObject(data, "current_ssid", (const char *)ap_info.ssid);
            cJSON_AddNumberToObject(data, "wifi_channel", (double)ap_info.primary);
        }
    } else {
        cJSON_AddStringToObject(data, "current_ssid", "");
    }

    /* SPIFFS storage (cached in health metrics, updated every 30s) */
    cJSON_AddNumberToObject(data, "spiffs_total", (double)m->spiffs_total);
    cJSON_AddNumberToObject(data, "spiffs_free", (double)m->spiffs_free);

    /* SD card storage (cached in health metrics) */
    cJSON_AddNumberToObject(data, "sd_free_mb", (double)m->sd_free_mb);
    cJSON_AddNumberToObject(data, "sd_total_mb", (double)m->sd_total_mb);
    cJSON_AddNumberToObject(data, "photo_count", (double)m->photo_count);

    /* 契约对齐（seeed 先例）：统一 SPA 读 sd_present / sd_*_bytes / sd_free_percent。
     * 2026-09-03 事故：缺这些字段时 SPA 存储卡片显示"未检测到 SD 卡"而文件列表正常。 */
    cJSON_AddBoolToObject(data, "sd_present", storage_is_available());
    cJSON_AddNumberToObject(data, "sd_total_bytes", (double)((uint64_t)m->sd_total_mb * 1048576ULL));
    cJSON_AddNumberToObject(data, "sd_free_bytes", (double)((uint64_t)m->sd_free_mb * 1048576ULL));
    if (m->sd_total_mb > 0) {
        cJSON_AddNumberToObject(data, "sd_free_percent",
                                (double)((uint64_t)m->sd_free_mb * 100ULL / m->sd_total_mb));
    }

    /* 统一 SPA 读 status.recording（seeed 先例）；本板原来只有 /api/record */
    switch (recorder_get_state()) {
        case RECORDER_RECORDING: cJSON_AddStringToObject(data, "recording", "recording"); break;
        case RECORDER_PAUSED:    cJSON_AddStringToObject(data, "recording", "paused"); break;
        default:                 cJSON_AddStringToObject(data, "recording", "idle"); break;
    }

    /* Motion */
    cJSON_AddBoolToObject(data, "motion_enabled", motion_detect_is_running());
    cJSON_AddNumberToObject(data, "motion_events", (double)m->motion_events);

    /* Timelapse 运行态（契约 v1.1：/api/timelapse/status 并入此处） */
    cJSON_AddBoolToObject(data, "timelapse_running", timelapse_is_running());
    cJSON_AddNumberToObject(data, "timelapse_photo_count", (double)timelapse_get_photo_count());

    /* Heap */
    cJSON_AddNumberToObject(data, "free_heap", (double)m->free_heap);
    cJSON_AddNumberToObject(data, "min_heap", (double)m->min_free_heap);
    /* PSRAM（SPA 统计条/系统面板的 PSRAM 芯片；无 PSRAM 的板按契约省略） */
    cJSON_AddNumberToObject(data, "free_psram",
        (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Stream clients — 契约 v1.0: 附带上限 */
    cJSON_AddNumberToObject(data, "stream_clients", (double)mjpeg_streamer_get_client_count());
    cJSON_AddNumberToObject(data, "stream_clients_max", 1);

    /* Brightness — method 2 = locked-exposure probe, 1 = auto-exposure luma */
    cJSON_AddNumberToObject(data, "brightness_pct", (double)m->brightness_pct);
    cJSON_AddStringToObject(data, "brightness_method",
        m->brightness_method == 1 ? "luma" : (m->brightness_method == 2 ? "grayscale" : "init"));
    cJSON_AddBoolToObject(data, "scene_dark", m->scene_dark);

    /* ΣΔ motion pipeline diagnostics (2026-09-04 rework) */
    {
        const lm_result_t *d = motion_detect_get_diag();
        cJSON *md = cJSON_CreateObject();
        cJSON_AddNumberToObject(md, "sigma_x100", d->sigma_x100);
        cJSON_AddNumberToObject(md, "energy", d->energy);
        cJSON_AddNumberToObject(md, "energy_smooth", d->energy_smooth);
        cJSON_AddNumberToObject(md, "fg_pct", d->fg_filt_pct);
        cJSON_AddNumberToObject(md, "blobs", d->blobs);
        cJSON_AddNumberToObject(md, "mode", d->mode);          /* 0 normal 1 warmup 2 reconverge 3 blind */
        cJSON_AddNumberToObject(md, "luma_mean", d->luma_mean);
        cJSON_AddNumberToObject(md, "decode_us", motion_detect_get_decode_us());
        cJSON_AddNumberToObject(md, "frames", motion_detect_get_frames_analyzed());
        cJSON_AddItemToObject(data, "motion_diag", md);
    }

    /* Flash LED */
    cJSON_AddBoolToObject(data, "flash_on", flash_led_is_on());

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  GET /api/config                                                    */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_config_get(httpd_req_t *req)
{
    const cam_config_t *cfg = config_get();

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device_name", cfg->device_name);
    cJSON_AddStringToObject(data, "wifi_ssid", cfg->wifi_ssid);
    cJSON_AddStringToObject(data, "wifi_ssid_2", cfg->wifi_ssid_2);
    cJSON_AddNumberToObject(data, "allow_ap_fallback", (double)cfg->allow_ap_fallback);
    cJSON_AddNumberToObject(data, "cam_framesize", (double)cfg->cam_framesize);
    cJSON_AddNumberToObject(data, "cam_fps", (double)cfg->cam_fps);
    cJSON_AddNumberToObject(data, "cam_quality", (double)cfg->cam_quality);
    cJSON_AddNumberToObject(data, "xclk_freq_mhz", (double)cfg->xclk_freq_mhz);
    cJSON_AddStringToObject(data, "timezone", cfg->timezone);
    /* 家族 motion 超集（契约 §3.2） */
    cJSON_AddNumberToObject(data, "motion_enabled", (double)cfg->motion_enabled);
    cJSON_AddNumberToObject(data, "motion_sensitivity", (double)cfg->motion_sensitivity);
    cJSON_AddNumberToObject(data, "motion_cooldown_s", (double)cfg->motion_cooldown_s);
    cJSON_AddNumberToObject(data, "motion_active_interval_s", (double)cfg->motion_active_interval_s);
    cJSON_AddNumberToObject(data, "cam_vflip", (double)cfg->cam_vflip);
    cJSON_AddNumberToObject(data, "wifi_tx_power", (double)cfg->wifi_tx_power);
    cJSON_AddNumberToObject(data, "wifi_power_save", (double)cfg->wifi_power_save);
    cJSON_AddNumberToObject(data, "flash_threshold", (double)cfg->flash_threshold);
    cJSON_AddNumberToObject(data, "timelapse_enabled", (double)cfg->timelapse_enabled);
    cJSON_AddNumberToObject(data, "timelapse_interval_s", (double)cfg->timelapse_interval_s);
    cJSON_AddNumberToObject(data, "timelapse_burst_count", (double)cfg->timelapse_burst_count);
    cJSON_AddNumberToObject(data, "timelapse_mode", (double)cfg->timelapse_mode);
    cJSON_AddNumberToObject(data, "timelapse_min_interval_s", (double)cfg->timelapse_min_interval_s);
    cJSON_AddNumberToObject(data, "timelapse_max_interval_s", (double)cfg->timelapse_max_interval_s);
    cJSON_AddNumberToObject(data, "timelapse_decay_factor", (double)cfg->timelapse_decay_factor);
    cJSON_AddNumberToObject(data, "timelapse_decay_period_s", (double)cfg->timelapse_decay_period_s);

    /* Recording settings */
    cJSON_AddNumberToObject(data, "record_mode", (double)cfg->record_mode);
    cJSON_AddNumberToObject(data, "segment_sec", (double)cfg->segment_sec);
    cJSON_AddNumberToObject(data, "frame_drop_enabled", (double)cfg->frame_drop_enabled);

    /* Storage cleanup thresholds */
    cJSON_AddNumberToObject(data, "cleanup_low_pct", (double)cfg->cleanup_low_pct);
    cJSON_AddNumberToObject(data, "cleanup_high_pct", (double)cfg->cleanup_high_pct);

    /* SD card write enable */
    cJSON_AddNumberToObject(data, "save_to_sd", (double)cfg->save_to_sd);
    cJSON_AddNumberToObject(data, "sd_log_enabled", (double)cfg->sd_log_enabled);
    cJSON_AddNumberToObject(data, "wifi_reconnect_hours", (double)cfg->wifi_reconnect_hours);
    cJSON_AddNumberToObject(data, "wifi_roam_rssi", (double)cfg->wifi_roam_rssi);
    cJSON_AddNumberToObject(data, "wifi_roam_gap_s", (double)cfg->wifi_roam_gap_s);
    cJSON_AddNumberToObject(data, "onvif_enable", (double)cfg->onvif_enable);
    cJSON_AddNumberToObject(data, "schema_version", (double)CONFIG_SCHEMA_VERSION);

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/config                                                   */
/* ------------------------------------------------------------------ */

/* POST 值合法性 = 在可选表内 且 ≤ 三层上限（与 supported_resolutions 同源，
 * 表本体在 camera_driver.c 单一事实源） */
static bool res_value_supported(int val, int eff_max)
{
    int count = 0;
    const camera_res_opt_t *opts = camera_supported_resolutions(&count);
    for (int i = 0; i < count; i++) {
        if (opts[i].value == val) {
            return val <= eff_max;
        }
    }
    return false;
}

static esp_err_t handler_api_config_post(httpd_req_t *req)
{
    ESP_LOGW(TAG, "=== POST /api/config ENTRY === content_len=%d", (int)req->content_len);
    
    char *body = read_body(req, 2048);
    if (!body) {
        ESP_LOGW(TAG, "POST /api/config: BODY is NULL (content_len=%d)", (int)req->content_len);
        return send_json_error(req, "empty or invalid body", 400);
    }
    ESP_LOGI(TAG, "POST /api/config received (%d bytes)", (int)req->content_len);

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return send_json_error(req, "invalid JSON", 400);
    }

    /* Auth check - handle SET_PASSWORD_FIRST state machine */
    const cam_config_t *cfg = config_get();
    bool password_is_empty = (strlen(cfg->web_password) == 0);
    if (password_is_empty) {
        /* Only allow if body contains web_password field */
        cJSON *pw_item = cJSON_GetObjectItem(json, "web_password");
        if (!(pw_item && cJSON_IsString(pw_item) && strlen(pw_item->valuestring) > 0)) {
            cJSON_Delete(json);
            return send_json_error(req, "SET_PASSWORD_FIRST", 401);
        }
        /* Allow request to proceed - password will be set in handler below */
    } else {
        /* Password is set - require X-Password header */
        if (!check_auth(req)) {
            cJSON_Delete(json);
            ESP_LOGW(TAG, "POST /api/config: AUTH FAILED");
            return send_unauthorized(req);
        }
    }

    cJSON *item;
    bool need_save = false;
    bool wifi_changed = false;
    if ((item = cJSON_GetObjectItem(json, "device_name")) && cJSON_IsString(item)) {
        config_set_device_name(item->valuestring);
        need_save = false;
    }

    /* WiFi credentials */
    {
        const char *ssid = NULL;
        const char *pass = NULL;
        if ((item = cJSON_GetObjectItem(json, "wifi_ssid")) && cJSON_IsString(item)) {
            ssid = item->valuestring;
        }
        if ((item = cJSON_GetObjectItem(json, "wifi_pass")) && cJSON_IsString(item)) {
            pass = item->valuestring;
        }
        ESP_LOGI(TAG, "WiFi save request: ssid='%s' pass_len=%d",
                 ssid ? ssid : "(null)", pass ? (int)strlen(pass) : -1);
        if (ssid && strlen(ssid) > 0) {
            /* Only update WiFi if ssid is non-empty.
             * Frontend wifi_pass field is always empty (GET never returns password),
             * so only save WiFi when user explicitly fills in the SSID field.
             * If pass is empty but ssid matches current, skip WiFi save to preserve password. */
            if (pass && strlen(pass) > 0) {
                esp_err_t wret = config_set_wifi(ssid, pass);
                wifi_changed = (wret == ESP_OK);
                ESP_LOGI(TAG, "config_set_wifi (with pass) result: %s", esp_err_to_name(wret));
            } else if (strcmp(ssid, config_get()->wifi_ssid) != 0) {
                /* SSID changed but no password provided — save anyway (user must re-enter) */
                esp_err_t wret = config_set_wifi(ssid, "");
                wifi_changed = (wret == ESP_OK);
                ESP_LOGI(TAG, "config_set_wifi (SSID changed, no pass) result: %s", esp_err_to_name(wret));
            } else {
                ESP_LOGI(TAG, "WiFi unchanged (same SSID, no new password)");
            }
    }
    }
    /* Secondary WiFi (dual network) */
    {
        const char *ssid2 = NULL;
        const char *pass2 = NULL;
        if ((item = cJSON_GetObjectItem(json, "wifi_ssid_2")) && cJSON_IsString(item)) {
            ssid2 = item->valuestring;
        }
        if ((item = cJSON_GetObjectItem(json, "wifi_pass_2")) && cJSON_IsString(item)) {
            pass2 = item->valuestring;
        }
        if (ssid2) {
            config_set_wifi_secondary(ssid2, pass2);
        }
    }
    if ((item = cJSON_GetObjectItem(json, "allow_ap_fallback")) && cJSON_IsNumber(item)) {
        config_set_allow_ap_fallback((uint8_t)item->valueint);
    }
    /* Camera settings (resolution/fps/quality) — also apply live */
    bool camera_changed = false;
    if ((item = cJSON_GetObjectItem(json, "cam_framesize")) && cJSON_IsNumber(item)) {
        int val = item->valueint;
        int eff_max = (int)camera_get_effective_max_res();
        if (!res_value_supported(val, eff_max)) {
            cJSON_Delete(json);
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "cam_framesize %d unsupported (max %d %s, source: %s)",
                     val, eff_max, framesize_label(eff_max), camera_res_cap_source());
            return send_json_error(req, msg, 400);
        }
        config_set_resolution((camera_resolution_t)val);
        camera_changed = true;
    }

    if ((item = cJSON_GetObjectItem(json, "cam_fps")) && cJSON_IsNumber(item)) {
        int val = item->valueint;
        if (val < 1 || val > 30) {
            cJSON_Delete(json);
            return send_json_error(req, "cam_fps out of range (1-30)", 400);
        }
        config_set_cam_fps((uint8_t)val);
        camera_changed = true;
    }

    if ((item = cJSON_GetObjectItem(json, "cam_quality")) && cJSON_IsNumber(item)) {
        int val = item->valueint;
        if (val < CAMERA_QUALITY_MIN || val > CAMERA_QUALITY_MAX) {
            cJSON_Delete(json);
            return send_json_error(req, "cam_quality out of range (10-63)", 400);
        }
        config_set_jpeg_quality((uint8_t)val);
        camera_changed = true;
    }

    if ((item = cJSON_GetObjectItem(json, "xclk_freq_mhz")) && cJSON_IsNumber(item)) {
        uint8_t new_xclk = (uint8_t)item->valueint;
        if (new_xclk != config_get()->xclk_freq_mhz) {
            config_set_xclk_freq(new_xclk);
            camera_changed = true;
        }
    }

    /* Apply camera changes in one batch (deinit+init is expensive).
     * Note: camera_apply_settings drains in-flight captures and blocks
     * stream tasks during the ~2-3s reinit. Frontend should disable
     * controls during this window. */
    if (camera_changed) {
        const cam_config_t *cur = config_get();
        esp_err_t cam_ret = camera_apply_settings(cur->cam_framesize, cur->cam_fps, cur->cam_quality);
        if (cam_ret != ESP_OK) {
            ESP_LOGE(TAG, "camera_apply_settings failed: %s", esp_err_to_name(cam_ret));
            cJSON_Delete(json);
            return send_json_error(req, "camera apply failed", 500);
        }
    }

    /* Web password (has dedicated setter) */
    if ((item = cJSON_GetObjectItem(json, "web_password")) && cJSON_IsString(item)) {
        /* 契约 v1.1：拒绝空/过短密码 */
        if (strlen(item->valuestring) < 6) {
            cJSON_Delete(json);
            return send_json_error(req, "web_password must be at least 6 characters", HTTPD_400_BAD_REQUEST);
        }
        config_set_web_password(item->valuestring);
    }

    /* Timezone (no dedicated setter, apply immediately) */
    if ((item = cJSON_GetObjectItem(json, "timezone")) && cJSON_IsString(item)
        && strlen(item->valuestring) > 0) {
        config_set_timezone(item->valuestring);
        setenv("TZ", item->valuestring, 1);
        tzset();
    }

    /* Motion detection — 家族超集模型（契约 §3.2，旧 threshold/saved_threshold
     * V-save 技巧随字段废弃，enable 成为显式字段） */
    {
        const cam_config_t *cur = config_get();
        bool set_motion = false;
        uint8_t m_en = cur->motion_enabled;
        uint8_t m_sens = cur->motion_sensitivity;
        uint16_t m_cool = cur->motion_cooldown_s;
        uint8_t m_act = cur->motion_active_interval_s;

        item = cJSON_GetObjectItem(json, "motion_enabled");
        if (item && cJSON_IsNumber(item)) { m_en = item->valueint ? 1 : 0; set_motion = true; }
        item = cJSON_GetObjectItem(json, "motion_sensitivity");
        if (item && cJSON_IsNumber(item)) {
            int val = item->valueint;
            if (val < 0 || val > 100) {
                cJSON_Delete(json);
                return send_json_error(req, "motion_sensitivity out of range (0-100)", 400);
            }
            m_sens = (uint8_t)val;
            set_motion = true;
        }
        item = cJSON_GetObjectItem(json, "motion_cooldown_s");
        if (item && cJSON_IsNumber(item)) {
            int val = item->valueint;
            if (val < 1 || val > 300) {
                cJSON_Delete(json);
                return send_json_error(req, "motion_cooldown_s out of range (1-300)", 400);
            }
            m_cool = (uint16_t)val;
            set_motion = true;
        }
        item = cJSON_GetObjectItem(json, "motion_active_interval_s");
        if (item && cJSON_IsNumber(item)) {
            int val = item->valueint;
            if (val < 1 || val > 30) {
                cJSON_Delete(json);
                return send_json_error(req, "motion_active_interval_s out of range (1-30)", 400);
            }
            m_act = (uint8_t)val;
            set_motion = true;
        }
        if (set_motion) {
            config_set_motion(m_en, m_sens, m_cool, m_act);
            /* 启停即时生效（无需重启检测任务：enabled=0 时评分门槛拉满即可） */
            motion_detect_apply_config();
        }
    }

    /* ONVIF 开关（契约核心字段，重启生效） */
    if ((item = cJSON_GetObjectItem(json, "onvif_enable")) && cJSON_IsNumber(item)) {
        config_set_onvif_enable(item->valueint ? 1 : 0);
    }

    /* Vflip (apply immediately via sensor register) */
    if ((item = cJSON_GetObjectItem(json, "cam_vflip")) && cJSON_IsNumber(item)) {
        uint8_t new_vflip = (uint8_t)item->valueint;
        config_set_vflip(new_vflip);
        camera_apply_vflip(new_vflip);
    }

    /* WiFi power settings */
    if ((item = cJSON_GetObjectItem(json, "wifi_tx_power")) && cJSON_IsNumber(item)) {
        uint8_t tx = (uint8_t)item->valueint;
        uint8_t ps = 0;
        cJSON *ps_item = cJSON_GetObjectItem(json, "wifi_power_save");
        if (ps_item && cJSON_IsNumber(ps_item)) ps = (uint8_t)ps_item->valueint;
        config_set_wifi_power(tx, ps);
    }

    /* Flash threshold setting */
    item = cJSON_GetObjectItem(json, "flash_threshold");
    if (item && cJSON_IsNumber(item)) {
        config_set_flash_threshold((uint8_t)item->valueint);
    }
    {
        bool timelapse_changed = false;
        uint8_t tl_enabled = config_get()->timelapse_enabled;
        uint16_t tl_interval = config_get()->timelapse_interval_s;
        uint8_t tl_burst = config_get()->timelapse_burst_count;

        item = cJSON_GetObjectItem(json, "timelapse_enabled");
        if (item && cJSON_IsNumber(item)) { tl_enabled = (uint8_t)item->valueint; timelapse_changed = true; }
        item = cJSON_GetObjectItem(json, "timelapse_interval_s");
        if (item && cJSON_IsNumber(item)) { tl_interval = (uint16_t)item->valueint; timelapse_changed = true; }
        item = cJSON_GetObjectItem(json, "timelapse_burst_count");
        if (item && cJSON_IsNumber(item)) { tl_burst = (uint8_t)item->valueint; timelapse_changed = true; }

        if (timelapse_changed) {
            config_set_timelapse(tl_enabled, tl_interval, tl_burst);
            /* 契约 v1.1 收敛：timelapse 启停统一走 config 字段，独立端点已移除 */
            if (tl_enabled && !timelapse_is_running()) {
                timelapse_start();
            } else if (!tl_enabled && timelapse_is_running()) {
                timelapse_stop();
            }
        }
    }

    /* Dynamic timelapse settings */
    {
        bool dynamic_changed = false;
        uint8_t tl_mode = config_get()->timelapse_mode;
        uint16_t tl_min = config_get()->timelapse_min_interval_s;
        uint16_t tl_max = config_get()->timelapse_max_interval_s;
        uint8_t tl_decay = config_get()->timelapse_decay_factor;
        uint16_t tl_period = config_get()->timelapse_decay_period_s;

        item = cJSON_GetObjectItem(json, "timelapse_mode");
        if (item && cJSON_IsNumber(item)) {
            uint8_t val = (uint8_t)item->valueint;
            if (val > 1) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Invalid timelapse_mode: %u (must be 0 or 1)", val);
                cJSON_Delete(json);
                return send_json_error(req, msg, 400);
            }
            tl_mode = val;
            dynamic_changed = true;
        }

        item = cJSON_GetObjectItem(json, "timelapse_min_interval_s");
        if (item && cJSON_IsNumber(item)) { tl_min = (uint16_t)item->valueint; dynamic_changed = true; }

        item = cJSON_GetObjectItem(json, "timelapse_max_interval_s");
        if (item && cJSON_IsNumber(item)) { tl_max = (uint16_t)item->valueint; dynamic_changed = true; }

        item = cJSON_GetObjectItem(json, "timelapse_decay_factor");
        if (item && cJSON_IsNumber(item)) {
            uint8_t val = (uint8_t)item->valueint;
            if (val <= 1) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Invalid timelapse_decay_factor: %u (must be > 1)", val);
                cJSON_Delete(json);
                return send_json_error(req, msg, 400);
            }
            tl_decay = val;
            dynamic_changed = true;
        }

        item = cJSON_GetObjectItem(json, "timelapse_decay_period_s");
        if (item && cJSON_IsNumber(item)) {
            uint16_t val = (uint16_t)item->valueint;
            if (val == 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid timelapse_decay_period_s: %u (must be > 0)", val);
                cJSON_Delete(json);
                return send_json_error(req, msg, 400);
            }
            tl_period = val;
            dynamic_changed = true;
        }

        if (dynamic_changed) {
            if (tl_min >= tl_max) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Invalid timelapse interval: min=%u must be less than max=%u", tl_min, tl_max);
                cJSON_Delete(json);
                return send_json_error(req, msg, 400);
            }
            config_set_timelapse_dynamic(tl_mode, tl_min, tl_max, tl_decay, tl_period);
        }
    }

    /* ── Recording settings ── */
    {
        const cam_config_t *cur = config_get();
        bool rec_changed = false;
        uint8_t rec_mode = cur->record_mode;
        uint16_t rec_seg = cur->segment_sec;
        uint8_t rec_drop = cur->frame_drop_enabled;

        item = cJSON_GetObjectItem(json, "record_mode");
        if (item && cJSON_IsNumber(item)) { rec_mode = (uint8_t)item->valueint; rec_changed = true; }
        item = cJSON_GetObjectItem(json, "segment_sec");
        if (item && cJSON_IsNumber(item)) {
            int val = item->valueint;
            if (val < 5 || val > 3600) {
                cJSON_Delete(json);
                return send_json_error(req, "segment_sec out of range (5-3600)", 400);
            }
            rec_seg = (uint16_t)val;
            rec_changed = true;
        }
        item = cJSON_GetObjectItem(json, "frame_drop_enabled");
        if (item && cJSON_IsNumber(item)) { rec_drop = (uint8_t)item->valueint; rec_changed = true; }

        if (rec_changed) {
            config_set_recording(rec_mode, rec_seg, rec_drop);
        }
    }

    /* ── Storage cleanup thresholds ── */
    {
        const cam_config_t *cur = config_get();
        uint8_t cl_low = cur->cleanup_low_pct;
        uint8_t cl_high = cur->cleanup_high_pct;
        bool cl_changed = false;

        item = cJSON_GetObjectItem(json, "cleanup_low_pct");
        if (item && cJSON_IsNumber(item)) { cl_low = (uint8_t)item->valueint; cl_changed = true; }
        item = cJSON_GetObjectItem(json, "cleanup_high_pct");
        if (item && cJSON_IsNumber(item)) { cl_high = (uint8_t)item->valueint; cl_changed = true; }

        if (cl_changed) {
            /* V16 语义 = 空闲百分比（家族统一，seeed 先例）：
             * free < low 触发清理，删到 free >= high 停止 → 必须 low < high。
             * 旧"已用百分比"语义的反向校验已废弃。 */
            if (cl_low >= cl_high) {
                cJSON_Delete(json);
                return send_json_error(req, "cleanup_low_pct must be less than cleanup_high_pct (free-percent semantics)", 400);
            }
            config_set_cleanup(cl_low, cl_high);
        }
    }

    /* ── SD card write enable ── */
    item = cJSON_GetObjectItem(json, "save_to_sd");
    if (item && cJSON_IsNumber(item)) {
        config_set_save_to_sd((uint8_t)item->valueint);
    }

    /* ── SD card error logging ── */
    item = cJSON_GetObjectItem(json, "sd_log_enabled");
    if (item && cJSON_IsNumber(item)) {
        config_set_sd_log_enabled((uint8_t)item->valueint);
    }

    /* ── Periodic WiFi reconnect interval ── */
    item = cJSON_GetObjectItem(json, "wifi_reconnect_hours");
    if (item && cJSON_IsNumber(item)) {
        config_set_wifi_reconnect_interval((uint16_t)item->valueint);
    }

    /* ── WiFi RSSI-based roaming（契约名 wifi_roam_rssi / wifi_roam_gap_s） ── */
    {
        cJSON *rt = cJSON_GetObjectItem(json, "wifi_roam_rssi");
        cJSON *rg = cJSON_GetObjectItem(json, "wifi_roam_gap_s");
        if ((rt && cJSON_IsNumber(rt)) || (rg && cJSON_IsNumber(rg))) {
            int8_t rssi = (rt && cJSON_IsNumber(rt)) ? (int8_t)rt->valueint
                                                     : config_get()->wifi_roam_rssi;
            uint8_t gap = (rg && cJSON_IsNumber(rg)) ? (uint8_t)rg->valueint
                                                     : config_get()->wifi_roam_gap_s;
            config_set_wifi_roam(rssi, gap);
        }
    }


    if (need_save) {
        config_save();
    }

    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "message", "config updated");
    if (wifi_changed) {
        cJSON_AddBoolToObject(resp, "wifi_changed", true);
    }
    return send_json_ok(req, resp);
}

/* ------------------------------------------------------------------ */
/*  POST /api/reset                                                    */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_reset(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/reset");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    ESP_LOGW(TAG, "Factory reset requested via web API");
    config_reset();

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "config reset to defaults");
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/reboot                                                   */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_reboot(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/reboot");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    ESP_LOGW(TAG, "Reboot requested via web API");

    /* Send response before rebooting */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "rebooting");
    send_json_ok(req, data);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  GET /capture                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t handler_capture(httpd_req_t *req)
{
    set_cors_headers(req);
    camera_fb_t *fb = NULL;
    /* Non-blocking peek: broker's s_current always holds the last published
     * frame, so this returns instantly. timeout=0 avoids blocking the
     * single-threaded httpd event loop. */
    esp_err_t err = frame_broker_get_copy(&fb, 0);
    if (err != ESP_OK || fb == NULL) {
        ESP_LOGW(TAG, "/api/capture: no frame available (broker rc=%s)", esp_err_to_name(err));
        return send_json_error(req, "camera not ready", 503);
    }
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t ret = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    frame_broker_free(fb);
    return ret;
}

/* ------------------------------------------------------------------ */
/*  GET /metrics  (Prometheus format via health_monitor)               */
/* ------------------------------------------------------------------ */

static esp_err_t handler_metrics(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "text/plain; version=0.0.4; charset=utf-8");

    const char *prom = health_monitor_get_prometheus_str();
    return httpd_resp_send(req, prom, strlen(prom));
}

/* ------------------------------------------------------------------ */
/*  GET /api/files  (list SD card files — photos, recordings, or all)  */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_files(httpd_req_t *req)
{
    const health_metrics_t *hm = health_monitor_get_metrics();

    /* Parse query parameters */
    char query[128] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char type[16] = "all";
    httpd_query_key_value(query, "type", type, sizeof(type));

    char limit_str[8] = "50";
    httpd_query_key_value(query, "limit", limit_str, sizeof(limit_str));
    int limit = atoi(limit_str);
    if (limit <= 0 || limit > 200) limit = 50;

    char offset_str[8] = "0";
    httpd_query_key_value(query, "offset", offset_str, sizeof(offset_str));
    int offset = atoi(offset_str);
    if (offset < 0) offset = 0;

    cJSON *data = cJSON_CreateObject();

    /* 先把所选类型的完整列表拼进 combined，再统一切片分页。
     * 旧实现对 type=all 时 offset 只作用于照片段、录像段恒从 0 开始，
     * 前端用 offset 翻页会出现重复/漏项。 */
    cJSON *combined = cJSON_CreateArray();
    int total = 0;

    if (strcmp(type, "photos") == 0 || strcmp(type, "all") == 0) {
        cJSON *photos = storage_get_photo_list_json();
        if (photos) {
            total += cJSON_GetArraySize(photos);
            while (cJSON_GetArraySize(photos) > 0) {
                cJSON_AddItemToArray(combined, cJSON_DetachItemFromArray(photos, 0));
            }
            cJSON_Delete(photos);
        }
    }

    if (strcmp(type, "recordings") == 0 || strcmp(type, "all") == 0) {
        cJSON *recordings = storage_get_recording_list_json();
        if (recordings) {
            total += cJSON_GetArraySize(recordings);
            while (cJSON_GetArraySize(recordings) > 0) {
                cJSON_AddItemToArray(combined, cJSON_DetachItemFromArray(recordings, 0));
            }
            cJSON_Delete(recordings);
        }
    }

    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(data, "files", arr);
    for (int i = offset; i < total && (i - offset) < limit; i++) {
        cJSON *item = cJSON_GetArrayItem(combined, i);
        if (item) {
            cJSON *clone = cJSON_Duplicate(item, 1);
            if (clone) cJSON_AddItemToArray(arr, clone);
        }
    }
    cJSON_Delete(combined);

    cJSON_AddNumberToObject(data, "sd_free_mb", (double)hm->sd_free_mb);
    cJSON_AddNumberToObject(data, "sd_total_mb", (double)hm->sd_total_mb);
    cJSON_AddNumberToObject(data, "total", total);
    cJSON_AddNumberToObject(data, "offset", offset);
    cJSON_AddNumberToObject(data, "limit", limit);

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  DELETE /api/files?name=xxx[&type=photo|recording]                  */
/* ------------------------------------------------------------------ */

#ifndef RECORDINGS_PATH
#define RECORDINGS_PATH "/sdcard/recordings"  /* 与 storage_manager.c 内部定义一致 */
#endif

/** @brief 当前正在写入的录像段文件（若是返回其相对名，否则 NULL） */
static const char *current_recording_relname(void)
{
    const char *cur = recorder_get_current_file();
    if (!cur || !cur[0]) return NULL;
    const char *prefix = RECORDINGS_PATH "/";
    if (strncmp(cur, prefix, strlen(prefix)) == 0) return cur + strlen(prefix);
    return cur;
}

static esp_err_t handler_api_files_delete(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/files");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_json_error(req, "missing query parameter", 400);
    }

    char name[192] = {0};
    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        return send_json_error(req, "missing name parameter", 400);
    }

    /* type 可选；缺省 photo 保持向后兼容 */
    char ftype[16] = "photo";
    httpd_query_key_value(query, "type", ftype, sizeof(ftype));

    /* Path traversal protection */
    if (strstr(name, "..") != NULL) {
        return send_json_error(req, "invalid file name", 400);
    }

    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    const char *cur_rel = current_recording_relname();
    if (cur_rel && strcmp(cur_rel, name) == 0) {
        return send_json_error(req, "file is currently being recorded", 409);
    }

    esp_err_t ret;
    if (strcmp(ftype, "recording") == 0) {
        ret = storage_delete_recording(name);
    } else {
        ret = storage_delete_photo(name);
        if (ret != ESP_OK) {
            /* 未指明 type 或 type=photo 的名字可能是录像 — 回落尝试录像路径 */
            ret = storage_delete_recording(name);
        }
    }
    if (ret != ESP_OK) {
        return send_json_error(req, "delete failed", 500);
    }

    /* Clean up empty parent directory (e.g. 1970-01/) */
    char *slash = strrchr(name, '/');
    if (slash) {
        char dirpath[280];
        snprintf(dirpath, sizeof(dirpath), "/sdcard/photos/%.*s", (int)(slash - name), name);
        rmdir(dirpath);  /* ignore error — dir may not be empty */
    }

    return send_json_ok(req, NULL);
}

/* ------------------------------------------------------------------ */
/*  POST /api/files/batch  {names:[...]}  或  {scope:"all|photos|recordings"} */
/*  契约 v1.2：与 seeed 同名端点同语义（seeed 现仅支持 names）。        */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_files_batch(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/files/batch");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    if (req->content_len <= 0 || req->content_len > 16384) {
        return send_json_error(req, "invalid request body", 400);
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) return send_json_error(req, "out of memory", 500);
    int len = httpd_req_recv(req, buf, req->content_len);
    if (len <= 0) { free(buf); return send_json_error(req, "empty request body", 400); }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return send_json_error(req, "invalid JSON", 400);

    /* 组装待删清单：显式 names 优先；否则按 scope 从缓存列表展开 */
    cJSON *list = cJSON_CreateArray();
    cJSON *names = cJSON_GetObjectItem(root, "names");
    cJSON *scope = cJSON_GetObjectItem(root, "scope");
    bool have_photos_scope = false, have_recordings_scope = false;

    if (cJSON_IsArray(names)) {
        cJSON *it;
        cJSON_ArrayForEach(it, names) {
            if (cJSON_IsString(it) && it->valuestring[0]) {
                cJSON_AddItemToArray(list, cJSON_CreateString(it->valuestring));
            }
        }
    } else if (cJSON_IsString(scope)) {
        if (strcmp(scope->valuestring, "photos") == 0 || strcmp(scope->valuestring, "all") == 0) {
            have_photos_scope = true;
            cJSON *photos = storage_get_photo_list_json();
            if (photos) {
                cJSON *it;
                cJSON_ArrayForEach(it, photos) {
                    cJSON *n = cJSON_GetObjectItem(it, "name");
                    if (cJSON_IsString(n)) cJSON_AddItemToArray(list, cJSON_CreateString(n->valuestring));
                }
                cJSON_Delete(photos);
            }
        }
        if (strcmp(scope->valuestring, "recordings") == 0 || strcmp(scope->valuestring, "all") == 0) {
            have_recordings_scope = true;
            cJSON *recs = storage_get_recording_list_json();
            if (recs) {
                cJSON *it;
                cJSON_ArrayForEach(it, recs) {
                    cJSON *n = cJSON_GetObjectItem(it, "name");
                    if (cJSON_IsString(n)) cJSON_AddItemToArray(list, cJSON_CreateString(n->valuestring));
                }
                cJSON_Delete(recs);
            }
        }
    }

    if (!cJSON_GetArraySize(list) && !cJSON_IsArray(names) && !cJSON_IsString(scope)) {
        cJSON_Delete(root);
        cJSON_Delete(list);
        return send_json_error(req, "missing 'names' array or 'scope' string", 400);
    }

    const char *cur_rel = current_recording_relname();
    int deleted = 0, failed = 0;

    cJSON *it;
    cJSON_ArrayForEach(it, list) {
        const char *name = cJSON_GetStringValue(it);
        if (!name || strstr(name, "..")) { failed++; continue; }
        if (cur_rel && strcmp(cur_rel, name) == 0) { failed++; continue; }

        /* 名字路由：scope 展开时类型已知；显式 names 先试照片再试录像 */
        esp_err_t ret = ESP_FAIL;
        if (have_photos_scope && !have_recordings_scope) {
            ret = storage_delete_photo(name);
        } else if (have_recordings_scope && !have_photos_scope) {
            ret = storage_delete_recording(name);
        } else {
            ret = storage_delete_photo(name);
            if (ret != ESP_OK) ret = storage_delete_recording(name);
        }
        if (ret == ESP_OK) deleted++; else failed++;
    }
    cJSON_Delete(list);
    cJSON_Delete(root);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "deleted", deleted);
    cJSON_AddNumberToObject(data, "failed", failed);
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  GET /api/download?name=xxx&type=photo|recording                    */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_download(httpd_req_t *req)
{
    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_json_error(req, "missing query parameter", 400);
    }

    char name[192] = {0};
    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        return send_json_error(req, "missing name parameter", 400);
    }

    char ftype[16] = "photo";
    httpd_query_key_value(query, "type", ftype, sizeof(ftype));

    /* Path traversal protection */
    if (strstr(name, "..") != NULL) {
        return send_json_error(req, "invalid file name", 400);
    }

    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    /* Build full path based on type */
    char filepath[280];
    if (strcmp(ftype, "recording") == 0) {
        snprintf(filepath, sizeof(filepath), "/sdcard/recordings/%s", name);
    } else {
        snprintf(filepath, sizeof(filepath), "/sdcard/photos/%s", name);
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        return send_json_error(req, "file not found", 404);
    }

    /* Get file size for Content-Length */
    struct stat st;
    if (stat(filepath, &st) == 0 && st.st_size > 0) {
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "%ld", (long)st.st_size);
        httpd_resp_set_hdr(req, "Content-Length", hdr);
    }

    /* Set content type based on file extension */
    const char *ext = strrchr(name, '.');
    if (ext && strcmp(ext, ".avi") == 0) {
        httpd_resp_set_type(req, "video/avi");
    } else {
        httpd_resp_set_type(req, "image/jpeg");
    }
    set_cors_headers(req);

    /* Content-Disposition: use basename */
    const char *basename = strrchr(name, '/');
    basename = basename ? basename + 1 : name;
    char disp[224];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", basename);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    /* Stream file in chunks */
    char file_buf[4096];
    size_t n;
    while ((n = fread(file_buf, 1, sizeof(file_buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, file_buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  POST /api/led  — Manual flash LED control (POST only)              */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_led(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/led");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }
    set_cors_headers(req);

    /* 契约 v1.1 主语义：JSON body {"brightness":0-100}（0=灭，>0=亮）；
     * 兼容遗留 ?action=on|off|toggle 查询参数式（MPA 仍在使用） */
    {
        char *led_body = read_body(req, 256);
        if (led_body) {
            cJSON *led_json = cJSON_Parse(led_body);
            free(led_body);
            if (led_json) {
                cJSON *bri = cJSON_GetObjectItem(led_json, "brightness");
                if (bri && cJSON_IsNumber(bri)) {
                    int b = bri->valueint;
                    cJSON_Delete(led_json);
                    if (b < 0 || b > 100) {
                        return send_json_error(req, "brightness must be 0-100", HTTPD_400_BAD_REQUEST);
                    }
                    if (b == 0) flash_led_off(); else flash_led_on();
                    cJSON *led_data = cJSON_CreateObject();
                    cJSON_AddNumberToObject(led_data, "brightness", b);
                    return send_json_ok(req, led_data);
                }
                cJSON_Delete(led_json);
                /* JSON 无 brightness 字段 → 落回 ?action= 流程 */
            }
        }
    }

    /* POST: toggle/on/off via ?action= query param */
    char query[32] = {0};
    char action[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "action", action, sizeof(action));
    }
    bool now_on;
    if (strcmp(action, "on") == 0) {
        flash_led_on();
        now_on = true;
    } else if (strcmp(action, "off") == 0) {
        flash_led_off();
        now_on = false;
    } else {
        /* default or "toggle" */
        now_on = flash_led_toggle();
    }
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "on", now_on);
    ESP_LOGI(TAG, "Flash LED %s (API)", now_on ? "ON" : "OFF");
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  GET /api/capabilities  — Board capability flags (12 booleans)     */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_capabilities(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *data = cJSON_CreateObject();
    /* 契约 v1.0：12 个布尔能力位 + api_version/wifi_scan（见 docs/api-contract.md） */
    cJSON_AddStringToObject(data, "api_version", "1.3");
    cJSON_AddBoolToObject(data, "wifi_scan", true);
    /* ai-thinker capabilities matrix */
    cJSON_AddBoolToObject(data, "ai", false);
    cJSON_AddBoolToObject(data, "sd", true);
    cJSON_AddBoolToObject(data, "audio", false);
    cJSON_AddBoolToObject(data, "ota", true);
    cJSON_AddBoolToObject(data, "mic", false);
    cJSON_AddBoolToObject(data, "flash_led", true);
    cJSON_AddBoolToObject(data, "recording", true);
    cJSON_AddBoolToObject(data, "timelapse", true);
    cJSON_AddBoolToObject(data, "onvif", true);
    cJSON_AddBoolToObject(data, "rtsp", false);
    cJSON_AddBoolToObject(data, "websocket", false);
    cJSON_AddBoolToObject(data, "mdns", false);

    return send_json_ok(req, data);
}


/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  GET /api/auth  - Validate password (returns 200 or 401)            */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_auth(httpd_req_t *req)
{
    if (check_auth(req)) {
        return send_json_ok(req, NULL);
    }
    return send_unauthorized(req);
}

/* ------------------------------------------------------------------ */
/*  GET /api/camera  - Read camera settings                           */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_camera_get(httpd_req_t *req)
{
    set_cors_headers(req);

    const cam_config_t *cfg = config_get();
    cJSON *data = cJSON_CreateObject();
    /* Persisted fields */
    cJSON_AddNumberToObject(data, "cam_framesize", (double)cfg->cam_framesize);
    cJSON_AddNumberToObject(data, "cam_quality",   (double)cfg->cam_quality);
    /* 契约扩展（2026-09-04）：画质滑杆边界由板端声明，前端据此钳制输入 */
    cJSON_AddNumberToObject(data, "quality_min",   CAMERA_QUALITY_MIN);
    cJSON_AddNumberToObject(data, "quality_max",   CAMERA_QUALITY_MAX);
    cJSON_AddNumberToObject(data, "cam_vflip",     (double)cfg->cam_vflip);
    /* 契约 v1.3 §5：分辨率名 + 动态分辨率表（value = 家族刻度 framesize_t；
     * 三层上限：sensor ∩ board ∩ memory，换小传感器时列表自动收缩） */
    {
        int eff_max = (int)camera_get_effective_max_res();
        cJSON_AddStringToObject(data, "resolution", framesize_label(cfg->cam_framesize));
        cJSON *res_arr = cJSON_CreateArray();
        int opt_count = 0;
        const camera_res_opt_t *opts = camera_supported_resolutions(&opt_count);
        for (int i = 0; i < opt_count; i++) {
            if (opts[i].value > eff_max) break;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", opts[i].label);
            cJSON_AddNumberToObject(item, "value", opts[i].value);
            cJSON_AddItemToArray(res_arr, item);
        }
        cJSON_AddItemToObject(data, "supported_resolutions", res_arr);
        /* 契约扩展（2026-09-04）：上限被哪一层钳制（sensor/board/memory） */
        cJSON_AddStringToObject(data, "res_cap_source", camera_res_cap_source());
    }
    /* Not persisted on this board — sensor defaults (0/false) match the
     * SPA's `?? 0` / `?? false` fallbacks, so the camera tab populates
     * with valid OV2640 defaults instead of staying blank. */
    cJSON_AddNumberToObject(data, "cam_brightness", 0);
    cJSON_AddNumberToObject(data, "cam_contrast",   0);
    cJSON_AddNumberToObject(data, "cam_saturation", 0);
    cJSON_AddNumberToObject(data, "cam_sharpness",  0);
    cJSON_AddBoolToObject(data,   "cam_hmirror",    false);
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/camera  - Update camera settings                        */
/*                                                                    */
/* Only cam_framesize / cam_quality / cam_vflip are persisted on this
 * board. cam_brightness/contrast/saturation/sharpness/hmirror are
 * accepted (so the SPA's saveCamera payload doesn't 400) but ignored
 * — sensor defaults apply on reinit. */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_camera_post(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/camera");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }
    set_cors_headers(req);

    char *body = read_body(req, 2048);
    if (!body) {
        return send_json_error(req, "empty or invalid body", 400);
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return send_json_error(req, "invalid JSON", 400);
    }

    cJSON *item;
    bool need_apply = false;

    if ((item = cJSON_GetObjectItem(json, "cam_framesize")) && cJSON_IsNumber(item)) {
        int val = item->valueint;
        int eff_max = (int)camera_get_effective_max_res();
        if (!res_value_supported(val, eff_max)) {
            cJSON_Delete(json);
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "cam_framesize %d unsupported (max %d %s, source: %s)",
                     val, eff_max, framesize_label(eff_max), camera_res_cap_source());
            return send_json_error(req, msg, 400);
        }
        config_set_resolution((camera_resolution_t)val);
        need_apply = true;
    }
    if ((item = cJSON_GetObjectItem(json, "cam_quality")) && cJSON_IsNumber(item)) {
        int val = item->valueint;
        if (val < CAMERA_QUALITY_MIN || val > CAMERA_QUALITY_MAX) {
            cJSON_Delete(json);
            return send_json_error(req, "cam_quality out of range (10-63)", 400);
        }
        config_set_jpeg_quality((uint8_t)val);
        need_apply = true;
    }
    if ((item = cJSON_GetObjectItem(json, "cam_vflip")) && cJSON_IsBool(item)) {
        config_set_vflip(item->valueint ? 1 : 0);
        need_apply = true;
    }
    /* cam_hmirror / cam_brightness / cam_contrast / cam_saturation /
     * cam_sharpness are accepted but not persisted — see file header note. */
    cJSON_Delete(json);

    if (need_apply) {
        const cam_config_t *now = config_get();
        esp_err_t cam_ret = camera_apply_settings(now->cam_framesize, now->cam_fps, now->cam_quality);
        if (cam_ret != ESP_OK) {
            ESP_LOGW(TAG, "camera_apply_settings rc=%s", esp_err_to_name(cam_ret));
        }
    }

    cJSON *data = cJSON_CreateObject();
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  GET /api/led  - Read flash LED state (no state change)             */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_led_get(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "on", flash_led_is_on());
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/time — 手动设置系统时间（契约 v1.2 §2 核心端点，v1.3 补齐） */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_time(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/time");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char *body = read_body(req, 256);
    if (!body) {
        return send_json_error(req, "Empty body", 400);
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return send_json_error(req, "Invalid JSON", 400);
    }

    const cJSON *jy  = cJSON_GetObjectItem(json, "year");
    const cJSON *jmo = cJSON_GetObjectItem(json, "month");
    const cJSON *jd  = cJSON_GetObjectItem(json, "day");
    const cJSON *jh  = cJSON_GetObjectItem(json, "hour");
    const cJSON *jmi = cJSON_GetObjectItem(json, "min");
    const cJSON *js  = cJSON_GetObjectItem(json, "sec");

    if (!cJSON_IsNumber(jy) || !cJSON_IsNumber(jmo) || !cJSON_IsNumber(jd) ||
        !cJSON_IsNumber(jh) || !cJSON_IsNumber(jmi) || !cJSON_IsNumber(js)) {
        cJSON_Delete(json);
        return send_json_error(req, "Missing time fields", 400);
    }

    struct tm tm_now = {
        .tm_year = jy->valueint - 1900,
        .tm_mon  = jmo->valueint - 1,
        .tm_mday = jd->valueint,
        .tm_hour = jh->valueint,
        .tm_min  = jmi->valueint,
        .tm_sec  = js->valueint,
    };
    time_t epoch = mktime(&tm_now);
    cJSON_Delete(json);

    if (epoch < (time_t)1577836800) {  /* < 2020-01-01: 非法日期 */
        return send_json_error(req, "Invalid date", 400);
    }
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    return send_json_ok(req, NULL);
}


/* ------------------------------------------------------------------ */
/*  GET /api/timelapse/start, /api/timelapse/stop, /api/timelapse/status */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_format(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/format");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    /* GPIO14 上相机与 SD 共享 SPI 总线：相机运行中 esp_vfs_fat_sdcard_format()
     * 必挂死（相机 I2S DMA 干扰 SDSPI，触发看门狗，2026-09-03 前旧实现因此直接 503）。
     * 安全路径：持久化格式化请求 → 应答后重启 → 开机在相机初始化之前
     * （main.c Step 5.5）执行格式化。 */
    esp_err_t ret = storage_format_request_set();
    if (ret != ESP_OK) {
        return send_json_error(req, "cannot persist format request", 500);
    }

    ESP_LOGW(TAG, "Format SD requested via API — rebooting to format before camera init");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message",
                            "Format scheduled. Device reboots; SD card is erased during boot.");
    esp_err_t resp = send_json_ok(req, data);

    /* 给 httpd ~2s 把应答发完再重启 */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return resp;
}

/* ------------------------------------------------------------------ */
/*  GET /api/storage  — SD card storage info                           */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_storage(httpd_req_t *req)
{
    /* Use cached health metrics instead of direct SD card access.
     * The SD card SPI bus can become unreliable after camera init, causing
     * f_getfree/opendir to hang or crash. Cached values are updated every 10s
     * by the health monitor task and are always safe to read. */
    const health_metrics_t *hm = health_monitor_get_metrics();

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "total_mb", (double)hm->sd_total_mb);
    cJSON_AddNumberToObject(data, "free_mb", (double)hm->sd_free_mb);
    cJSON_AddNumberToObject(data, "usage_pct", (double)hm->sd_usage_pct);
    cJSON_AddNumberToObject(data, "photo_count", (double)hm->photo_count);
    cJSON_AddNumberToObject(data, "mounted", hm->sd_mounted ? 1 : 0);

    return send_json_ok(req, data);
}
/* ------------------------------------------------------------------ */
/*  POST /api/record?action=start|stop  — Recording control            */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_record_post(httpd_req_t *req)
{
    esp_err_t auth_ret = require_auth(req, "/api/record");
    if (auth_ret != ESP_OK) {
        return auth_ret;
    }

    char query[64] = {0};
    char action[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "action", action, sizeof(action));
    }

    esp_err_t ret = ESP_OK;
    recorder_state_t state = recorder_get_state();
    cJSON *data = cJSON_CreateObject();

    if (strcmp(action, "start") == 0) {
        if (state == RECORDER_RECORDING) {
            ret = ESP_ERR_INVALID_STATE;
        } else {
            ret = recorder_start();
        }
        if (ret == ESP_OK) {
            cJSON_AddStringToObject(data, "state", "RECORDING");
        } else {
            cJSON_Delete(data);
            return send_json_error(req, "failed to start recording", 500);
        }
    } else if (strcmp(action, "stop") == 0) {
        if (state == RECORDER_IDLE) {
            ret = ESP_ERR_INVALID_STATE;
        } else {
            ret = recorder_stop();
        }
        if (ret == ESP_OK) {
            cJSON_AddStringToObject(data, "state", "IDLE");
        } else {
            cJSON_Delete(data);
            return send_json_error(req, "failed to stop recording", 500);
        }
    } else {
        cJSON_Delete(data);
        return send_json_error(req, "invalid action, use start|stop", 400);
    }

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  GET /api/record  — Recording status                                 */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_record_get(httpd_req_t *req)
{
    recorder_state_t state = recorder_get_state();
    cJSON *data = cJSON_CreateObject();

    const char *state_str;
    switch (state) {
        case RECORDER_IDLE:      state_str = "IDLE"; break;
        case RECORDER_RECORDING: state_str = "RECORDING"; break;
        case RECORDER_PAUSED:    state_str = "PAUSED"; break;
        case RECORDER_ERROR:     state_str = "ERROR"; break;
        default:                 state_str = "UNKNOWN"; break;
    }
    cJSON_AddStringToObject(data, "state", state_str);

    if (state == RECORDER_RECORDING || state == RECORDER_PAUSED) {
        const char *file = recorder_get_current_file();
        if (file && strlen(file) > 0) {
            cJSON_AddStringToObject(data, "current_file", file);
        }
    }

    cJSON_AddNumberToObject(data, "frames_dropped", (double)recorder_get_frames_dropped());

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  OPTIONS *   - CORS preflight                                       */
/* ------------------------------------------------------------------ */

static esp_err_t handler_options(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ------------------------------------------------------------------ */
/*  GET *       - SPIFFS static file serving (fallback)                    */
/* ------------------------------------------------------------------ */

static const char *get_content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html; charset=utf-8";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".js"))   return "application/javascript";
    if (strstr(path, ".png"))  return "image/png";
    if (strstr(path, ".jpg"))  return "image/jpeg";
    if (strstr(path, ".ico"))  return "image/x-icon";
    if (strstr(path, ".svg"))  return "image/svg+xml";
    if (strstr(path, ".json")) return "application/json";
    return "application/octet-stream";
}

static esp_err_t handler_static(httpd_req_t *req)
{
    set_cors_headers(req);

    /* Map "/" to "/index.html" */
    const char *uri = req->uri;
    char filepath[560];
    if (strcmp(uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "/spiffs/index.html");
    } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(filepath, sizeof(filepath), "/spiffs%s", uri);
#pragma GCC diagnostic pop
    }

    /* Security: reject path traversal */
    if (strstr(filepath, "..") != NULL) {
        httpd_resp_send_404(req);
        return ESP_OK;  /* response already sent — ESP_FAIL would confuse httpd */
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_OK;  /* response already sent — ESP_FAIL would confuse httpd */
    }

    httpd_resp_set_type(req, get_content_type(filepath));

    /* Prevent browsers from caching stale HTML after firmware updates.
     * SPIFFS content changes on every reflash; a cached old page sends
     * field names/values the new firmware rejects (e.g. cleanup 400). */
    if (strstr(filepath, ".html")) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, must-revalidate");
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_OK;  /* partial response already sent */
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


/* ------------------------------------------------------------------ */
/*  URI registration table                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  GET /api/scan — WiFi 扫描（契约 v1.0 核心端点，按 RSSI 降序）       */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_scan(httpd_req_t *req)
{
    set_cors_headers(req);

    /* 注意：本板 WiFi RF 较弱，扫描会短暂中断 STA 流量，属预期行为 */
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        return send_json_error(req, "Scan failed (WiFi not ready?)",
                               HTTPD_500_INTERNAL_SERVER_ERROR);
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 20) n = 20;

    wifi_ap_record_t *recs = malloc(sizeof(wifi_ap_record_t) * (n ? n : 1));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return send_json_error(req, "No memory", HTTPD_500_INTERNAL_SERVER_ERROR);
    }
    esp_wifi_scan_get_ap_records(&n, recs);

    for (int i = 1; i < (int)n; i++) {
        wifi_ap_record_t key = recs[i];
        int j = i - 1;
        while (j >= 0 && recs[j].rssi < key.rssi) {
            recs[j + 1] = recs[j];
            j--;
        }
        recs[j + 1] = key;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < (int)n; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (const char *)recs[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", recs[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", recs[i].authmode);
        cJSON_AddItemToArray(arr, ap);
    }
    free(recs);
    cJSON_AddItemToObject(data, "networks", arr);
    return send_json_ok(req, data);
}


typedef struct {
    const char        *uri;
    httpd_method_t    method;
    esp_err_t       (*handler)(httpd_req_t *);
} uri_entry_t;

static const uri_entry_t s_uris[] = {
    /* Specific API endpoints first (for URI match specificity) */
    { "/api/status",   HTTP_GET,    handler_api_status       },
    { "/api/config",   HTTP_GET,    handler_api_config_get   },
    { "/api/config",   HTTP_POST,   handler_api_config_post  },
    { "/api/reset",    HTTP_POST,   handler_api_reset        },
    { "/api/reboot",   HTTP_POST,   handler_api_reboot       },
    { "/api/capture",  HTTP_GET,    handler_capture          },
    { "/metrics",      HTTP_GET,    handler_metrics          },
    { "/api/files",    HTTP_GET,    handler_api_files        },
    { "/api/files",    HTTP_DELETE, handler_api_files_delete  },
    { "/api/files/batch", HTTP_POST, handler_api_files_batch  },
    { "/api/download", HTTP_GET,    handler_api_download     },
    { "/api/auth",     HTTP_GET,    handler_api_auth         },
    { "/api/scan",     HTTP_GET,    handler_api_scan         },
    { "/api/capabilities", HTTP_GET, handler_api_capabilities  },
    /* 契约 v1.3：补齐 §2 核心端点 POST /api/time（此前缺席，家族唯一缺口） */
    { "/api/time",     HTTP_POST,   handler_api_time         },
    /* 契约 v1.1：timelapse 独立端点已移除 — 启停走 POST /api/config 的
     * timelapse_enabled，运行态并入 GET /api/status */
    { "/api/format",   HTTP_POST,   handler_api_format       },
    { "/api/storage",  HTTP_GET,    handler_api_storage      },
    { "/api/record",    HTTP_POST,   handler_api_record_post  },
    { "/api/record",    HTTP_GET,    handler_api_record_get   },
    { "/api/led",      HTTP_POST,   handler_api_led           },
    { "/api/led",         HTTP_GET,    handler_api_led_get       },
    { "/api/camera",      HTTP_GET,    handler_api_camera_get    },
    { "/api/camera",      HTTP_POST,   handler_api_camera_post   },
    /* 契约 v1.3：ai/status 桩移除（SPA 轮询本就 Caps.ai 门控，见 app.js
     * loadCapsRetry——桩是历史遗留，违反 "false ⇒ 不注册" 红线）；
     * OTA URL 触发补齐（与 n16r8/seeed 语义一致） */
    { "/api/ota",      HTTP_POST,   handler_api_ota_url      },
    { "/api/ota/info",   HTTP_GET,    handler_api_ota_info     },
    { "/api/ota/upload", HTTP_POST,   handler_api_ota_upload   },
    { "/api/ota/spiffs", HTTP_POST,   handler_api_spiffs_upload },

    { "/*",             HTTP_OPTIONS, handler_options         },
    { "/*",             HTTP_GET,    handler_static          },
};

#define NUM_URIS (sizeof(s_uris) / sizeof(s_uris[0]))

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t web_server_init(void)
{
    /*
     * Module initialization notes:
     * Module initialization notes:
     * - recorder_set_segment_cb() is set in main.c after recorder_init()
     */
    ESP_LOGI(TAG, "web_server_init (reserved)");
    return ESP_OK;
    }

/* Set TCP_NODELAY + keepalive on every new HTTP connection.
 * NODELAY: disable Nagle's algorithm so small HTTP writes (headers, MJPEG
 * boundaries) aren't delayed by up to 1 RTT (~100-265ms on marginal WiFi).
 * KEEPALIVE: detect dead connections in ~11s (5s idle + 3×2s probes),
 * freeing up limited server sockets for new clients. */
static esp_err_t on_session_open(httpd_handle_t hd, int sockfd)
{
    int enable = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));

    int keepalive = 1;
    int keepidle  = 5;  /* start probing after 5s idle */
    int keepintvl = 2;  /* probe every 2s */
    int keepcnt   = 3;  /* 3 failed probes = dead */
    setsockopt(sockfd, SOL_SOCKET,  SO_KEEPALIVE,  &keepalive, sizeof(keepalive));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE,  &keepidle,  sizeof(keepidle));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT,   &keepcnt,   sizeof(keepcnt));
    return ESP_OK;
}

esp_err_t web_server_start(uint16_t port)
{
    if (s_server) {
        ESP_LOGW(TAG, "Server already running on port %d", port);
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 40;  /* 27 API + 2 wildcard + 2 ONVIF + headroom */
    config.stack_size = 8192;
    config.recv_wait_timeout = 10;    /* longer tolerance for slow WiFi */
    config.send_wait_timeout = 5;     /* free stalled connections faster (keepalive is primary) */
    config.lru_purge_enable = true;   /* clean up stale connections */
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.open_fn = on_session_open;  /* TCP_NODELAY on all connections */
    /* No core pinning: httpd default (tskNO_AFFINITY) lets the scheduler */
    /* place it on either core, avoiding starvation when stream blocks. */

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server on port %d: %s",
                 port, esp_err_to_name(ret));
        return ret;
    }

    /* Register API endpoints */
    for (size_t i = 0; i < NUM_URIS; i++) {
        httpd_uri_t uri = {
            .uri      = s_uris[i].uri,
            .method   = s_uris[i].method,
            .handler  = s_uris[i].handler,
            .user_ctx = NULL,
        };
        /* Stop before wildcards - register /stream first */
        if (strcmp(s_uris[i].uri, "/*") == 0) break;
        httpd_register_uri_handler(s_server, &uri);
    }

    /* Register /stream and WebSocket BEFORE wildcards (wildcard would block them) */
    /* MJPEG streamer is now independent TCP server on port 81, no httpd registration */

    /* Register ONVIF SOAP service handlers */
    onvif_register_handlers(s_server);

    /* Now register wildcard handlers */
    for (size_t i = 0; i < NUM_URIS; i++) {
        if (strcmp(s_uris[i].uri, "/*") != 0) continue;
        httpd_uri_t uri = {
            .uri      = s_uris[i].uri,
            .method   = s_uris[i].method,
            .handler  = s_uris[i].handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(s_server, &uri);
    }

    ESP_LOGI(TAG, "Web server started on port %d", port);
    return ESP_OK;
}
esp_err_t web_server_stop(void)
{
    if (s_server) {
        mjpeg_streamer_stop();
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
    return ESP_OK;
}

httpd_handle_t web_server_get_handle(void)
{
    return s_server;
}
