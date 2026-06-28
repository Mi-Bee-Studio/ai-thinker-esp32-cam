/*
 * Web Server Module Implementation
 *
 * REST API endpoints + SPIFFS static file serving for AI_Thinker ESP32-CAM.
 *
 * Endpoints:
 *   GET  /api/status   — Device status JSON
 *   GET  /api/config   — Current config JSON (passwords excluded)
 *   POST /api/config   — Partial config update (password-protected)
 *   POST /api/reset    — Reset config to defaults (password-protected)
 *   POST /api/reboot   — Reboot device after 1s delay (password-protected)
 *   GET  /capture      — Single JPEG frame capture
 *   GET  /stream       — MJPEG video stream (via mjpeg_streamer)
 *   GET  /metrics      — Prometheus-format metrics
 *   GET  /api/files    — List SD card photos
 *   GET  /api/download — Download photo file
 *   POST /api/upload   — Trigger manual NAS upload
 *   OPTIONS *          - CORS preflight
 *   GET    *          - SPIFFS static files (fallback)
 */

#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
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

#include "video_recorder.h"
#include "onvif_service.h"
#include "frame_broker.h"
#include "sd_logger.h"

#define FIRMWARE_VERSION "v1.0"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

/* ------------------------------------------------------------------ */
/*  JSON / HTTP helpers                                                */
/* ------------------------------------------------------------------ */

static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, X-Password");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
}

/** @brief Check X-Password header against stored web_password */
static bool check_auth(httpd_req_t *req)
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

static esp_err_t handler_api_status(httpd_req_t *req)
{
    const health_metrics_t *m = health_monitor_get_metrics();
    const cam_config_t *cfg = config_get();

    cJSON *data = cJSON_CreateObject();

    /* Device info */
    cJSON_AddStringToObject(data, "device_name", cfg->device_name);
    cJSON_AddNumberToObject(data, "uptime", (double)m->uptime_seconds);
    cJSON_AddStringToObject(data, "firmware_version", FIRMWARE_VERSION);

    /* Camera */
    cJSON_AddBoolToObject(data, "camera_ok", camera_is_initialized());
    cJSON_AddStringToObject(data, "sensor", camera_get_sensor_name());

    /* Resolution as display string */
    static const char *res_names[] = {"VGA", "SVGA", "XGA", "UXGA"};
    cJSON_AddStringToObject(data, "resolution",
        cfg->resolution < CAMERA_RES_MAX ? res_names[cfg->resolution] : "Unknown");

    /* WiFi */
    const char *wifi_mode_str;
    switch (m->wifi_state) {
        case WIFI_STATE_AP:              wifi_mode_str = "AP"; break;
        case WIFI_STATE_STA_CONNECTING:  wifi_mode_str = "STA连接中"; break;
        case WIFI_STATE_STA_CONNECTED:   wifi_mode_str = "STA已连接"; break;
        case WIFI_STATE_STA_DISCONNECTED: wifi_mode_str = "STA断开"; break;
        default:                         wifi_mode_str = "--"; break;
    }
    cJSON_AddStringToObject(data, "wifi_mode", wifi_mode_str);
    cJSON_AddStringToObject(data, "ip", wifi_get_ip_str());
    cJSON_AddNumberToObject(data, "wifi_rssi", (double)m->wifi_rssi);

    /* SPIFFS storage (cached in health metrics, updated every 30s) */
    cJSON_AddNumberToObject(data, "spiffs_total", (double)m->spiffs_total);
    cJSON_AddNumberToObject(data, "spiffs_free", (double)m->spiffs_free);

    /* SD card storage (cached in health metrics) */
    cJSON_AddNumberToObject(data, "sd_free_mb", (double)m->sd_free_mb);
    cJSON_AddNumberToObject(data, "sd_total_mb", (double)m->sd_total_mb);
    cJSON_AddNumberToObject(data, "photo_count", (double)m->photo_count);

    /* Motion */
    cJSON_AddBoolToObject(data, "motion_enabled", motion_detect_is_running());
    cJSON_AddNumberToObject(data, "motion_events", (double)m->motion_events);

    /* Heap */
    cJSON_AddNumberToObject(data, "free_heap", (double)m->free_heap);
    cJSON_AddNumberToObject(data, "min_heap", (double)m->min_free_heap);

    /* Stream clients */
    cJSON_AddNumberToObject(data, "stream_clients", (double)mjpeg_streamer_get_client_count());

    /* Brightness */
    cJSON_AddNumberToObject(data, "brightness_pct", (double)m->brightness_pct);
    cJSON_AddStringToObject(data, "brightness_method",
        m->brightness_method == 1 ? "register" : (m->brightness_method == 2 ? "grayscale" : "init"));
    cJSON_AddBoolToObject(data, "scene_dark", m->scene_dark);

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
    cJSON_AddNumberToObject(data, "resolution", (double)cfg->resolution);
    cJSON_AddNumberToObject(data, "fps", (double)cfg->fps);
    cJSON_AddNumberToObject(data, "jpeg_quality", (double)cfg->jpeg_quality);
    cJSON_AddNumberToObject(data, "xclk_freq_mhz", (double)cfg->xclk_freq_mhz);
    cJSON_AddStringToObject(data, "timezone", cfg->timezone);
    cJSON_AddNumberToObject(data, "motion_threshold", (double)cfg->motion_threshold);
    cJSON_AddNumberToObject(data, "motion_cooldown", (double)cfg->motion_cooldown);
    cJSON_AddNumberToObject(data, "vflip", (double)cfg->vflip);
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
    cJSON_AddNumberToObject(data, "record_segment_sec", (double)cfg->record_segment_sec);
    cJSON_AddNumberToObject(data, "frame_drop_enabled", (double)cfg->frame_drop_enabled);

    /* Storage cleanup thresholds */
    cJSON_AddNumberToObject(data, "cleanup_low_pct", (double)cfg->cleanup_low_pct);
    cJSON_AddNumberToObject(data, "cleanup_high_pct", (double)cfg->cleanup_high_pct);

    /* SD card write enable */
    cJSON_AddNumberToObject(data, "save_to_sd", (double)cfg->save_to_sd);
    cJSON_AddNumberToObject(data, "sd_log_enabled", (double)cfg->sd_log_enabled);
    cJSON_AddNumberToObject(data, "wifi_reconnect_hours", (double)cfg->wifi_reconnect_hours);

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/config                                                   */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_config_post(httpd_req_t *req)
{
    ESP_LOGW(TAG, "=== POST /api/config ENTRY === content_len=%d", (int)req->content_len);
    if (!check_auth(req)) {
        ESP_LOGW(TAG, "POST /api/config: AUTH FAILED");
        return send_unauthorized(req);
    }

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
    if ((item = cJSON_GetObjectItem(json, "resolution")) && cJSON_IsNumber(item)) {
        config_set_resolution((camera_resolution_t)item->valueint);
        camera_changed = true;
    }

    if ((item = cJSON_GetObjectItem(json, "fps")) && cJSON_IsNumber(item)) {
        config_set_fps((uint8_t)item->valueint);
        camera_changed = true;
    }

    if ((item = cJSON_GetObjectItem(json, "jpeg_quality")) && cJSON_IsNumber(item)) {
        config_set_jpeg_quality((uint8_t)item->valueint);
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
        esp_err_t cam_ret = camera_apply_settings(cur->resolution, cur->fps, cur->jpeg_quality);
        if (cam_ret != ESP_OK) {
            ESP_LOGE(TAG, "camera_apply_settings failed: %s", esp_err_to_name(cam_ret));
            cJSON_Delete(json);
            return send_json_error(req, "camera apply failed", 500);
        }
    }

    /* Web password (has dedicated setter) */
    if ((item = cJSON_GetObjectItem(json, "web_password")) && cJSON_IsString(item)) {
        config_set_web_password(item->valuestring);
    }

    /* Timezone (no dedicated setter, apply immediately) */
    if ((item = cJSON_GetObjectItem(json, "timezone")) && cJSON_IsString(item)
        && strlen(item->valuestring) > 0) {
        config_set_timezone(item->valuestring);
        setenv("TZ", item->valuestring, 1);
        tzset();
    }

    /* Motion detection (has dedicated setter) */
    {
        bool set_motion = false;
        uint8_t threshold = config_get()->motion_threshold;
        uint8_t cooldown = config_get()->motion_cooldown;
        bool motion_enabled = true;

        /* Handle motion_enabled boolean from frontend */
        item = cJSON_GetObjectItem(json, "motion_enabled");
        if (item && cJSON_IsBool(item)) {
            motion_enabled = item->valueint;
        }

        if (motion_enabled) {
            /* If enabling, restore saved threshold (or use current if valid) */
            uint8_t saved = config_get()->motion_saved_threshold;
            if (saved > 0) threshold = saved;
            else if (threshold == 0) threshold = 30; /* safety default */
            set_motion = true;
        } else {
            /* If disabling, save current threshold first */
            if (threshold > 0) {
                config_set_motion_saved_threshold(threshold);
            }
            threshold = 0;
            set_motion = true;
        }

        item = cJSON_GetObjectItem(json, "motion_threshold");
        if (item && cJSON_IsNumber(item)) {
            threshold = (uint8_t)item->valueint;
            set_motion = true;
        }
        item = cJSON_GetObjectItem(json, "motion_cooldown");
        if (item && cJSON_IsNumber(item)) {
            cooldown = (uint8_t)item->valueint;
            set_motion = true;
        }
        if (set_motion) {
            config_set_motion(threshold, cooldown);
        }
    }

    /* Vflip (apply immediately via sensor register) */
    if ((item = cJSON_GetObjectItem(json, "vflip")) && cJSON_IsNumber(item)) {
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
        uint16_t rec_seg = cur->record_segment_sec;
        uint8_t rec_drop = cur->frame_drop_enabled;

        item = cJSON_GetObjectItem(json, "record_mode");
        if (item && cJSON_IsNumber(item)) { rec_mode = (uint8_t)item->valueint; rec_changed = true; }
        item = cJSON_GetObjectItem(json, "record_segment_sec");
        if (item && cJSON_IsNumber(item)) { rec_seg = (uint16_t)item->valueint; rec_changed = true; }
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
            if (cl_low <= cl_high) {
                cJSON_Delete(json);
                return send_json_error(req, "cleanup_low_pct must be greater than cleanup_high_pct", 400);
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
    if (!check_auth(req)) {
        return send_unauthorized(req);
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
    if (!check_auth(req)) {
        return send_unauthorized(req);
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
    esp_err_t err = frame_broker_get_copy(&fb, 3000);
    if (err != ESP_OK || fb == NULL) {
        sd_logf(SD_LOG_ERROR, "http", "/capture failed: frame_broker timeout");
        return send_json_error(req, "camera capture failed", 500);
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
/*  GET /api/files  (list SD card photos)                              */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_files(httpd_req_t *req)
{
    /* Use cached health metrics for SD info — avoid direct SD card access.
     * storage_list_photos() traverses directories via opendir/readdir which
     * hangs on the degraded SPI bus after camera init, causing watchdog reset. */
    const health_metrics_t *hm = health_monitor_get_metrics();

    cJSON *arr = cJSON_CreateArray();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "files", arr);
    cJSON_AddNumberToObject(data, "total", 0);
    cJSON_AddNumberToObject(data, "offset", 0);
    cJSON_AddNumberToObject(data, "limit", 10);
    cJSON_AddNumberToObject(data, "sd_free_mb", (double)hm->sd_free_mb);
    cJSON_AddNumberToObject(data, "sd_total_mb", (double)hm->sd_total_mb);

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  DELETE /api/files?name=xxx                                         */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_files_delete(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_unauthorized(req);
    }

    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_json_error(req, "missing query parameter", 400);
    }

    char name[192] = {0};
    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        return send_json_error(req, "missing name parameter", 400);
    }

    /* Path traversal protection */
    if (strstr(name, "..") != NULL) {
        return send_json_error(req, "invalid file name", 400);
    }

    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    esp_err_t ret = storage_delete_photo(name);
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
/*  GET /api/download?name=xxx                                         */
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

    /* Path traversal protection */
    if (strstr(name, "..") != NULL) {
        return send_json_error(req, "invalid file name", 400);
    }

    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    /* Build full path */
    char filepath[280];
    snprintf(filepath, sizeof(filepath), "/sdcard/photos/%s", name);

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

    httpd_resp_set_type(req, "image/jpeg");
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
/*  GET/POST /api/flash  — Manual flash LED control                    */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_flash(httpd_req_t *req)
{
    set_cors_headers(req);

    /* POST: toggle/on/off via ?action= query param */
    if (req->method == HTTP_POST) {
        if (!check_auth(req)) {
            return send_unauthorized(req);
        }
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

    /* GET: return current state */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "on", flash_led_is_on());
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
/*  GET /api/timelapse/start, /api/timelapse/stop, /api/timelapse/status */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_timelapse_start(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_unauthorized(req);
    }
    esp_err_t ret = timelapse_start();
    if (ret != ESP_OK) {
        return send_json_error(req, "timelapse start failed", 500);
    }
    return send_json_ok(req, NULL);
}

static esp_err_t handler_api_timelapse_stop(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_unauthorized(req);
    }
    esp_err_t ret = timelapse_stop();
    if (ret != ESP_OK) {
        return send_json_error(req, "timelapse stop failed", 500);
    }
    return send_json_ok(req, NULL);
}

static esp_err_t handler_api_timelapse_status(httpd_req_t *req)
{
    const cam_config_t *cfg = config_get();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "running", timelapse_is_running());
    cJSON_AddNumberToObject(data, "interval_s", (double)cfg->timelapse_interval_s);
    cJSON_AddNumberToObject(data, "burst_count", (double)cfg->timelapse_burst_count);
    cJSON_AddNumberToObject(data, "photo_count", (double)timelapse_get_photo_count());
    cJSON_AddNumberToObject(data, "burst_photo_count", (double)timelapse_get_burst_photo_count());
    cJSON_AddNumberToObject(data, "current_interval_s", (double)timelapse_get_current_interval_s());
    cJSON_AddNumberToObject(data, "mode", (double)timelapse_get_mode());
    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/format  — Format SD card (all data lost!)               */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_format(httpd_req_t *req)
{
    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    /* SD card format via API is disabled.
     * The camera's active I2S DMA interferes with SDSPI bus write operations,
     * causing esp_vfs_fat_sdcard_format() to hang and trigger watchdog reset.
     * Users should format the TF card on a PC instead. */
    ESP_LOGW(TAG, "Format SD via API rejected (SPI bus conflict with camera)");
    return send_json_error(req, "Format via API not available while camera is running. Please format on PC.", 503);
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
    cJSON_AddNumberToObject(data, "recording_count", 0); /* TODO: add to health metrics cache */
    cJSON_AddNumberToObject(data, "mounted", hm->sd_mounted ? 1 : 0);

    return send_json_ok(req, data);
}
/* ------------------------------------------------------------------ */
/*  POST /api/record?action=start|stop  — Recording control            */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_record_post(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_unauthorized(req);
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
            return send_json_error(req, "failed to stop recording", 500);
        }
    } else {
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

typedef struct {
    const char       *uri;
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
    { "/capture",      HTTP_GET,    handler_capture          },
    { "/metrics",      HTTP_GET,    handler_metrics          },
    { "/api/files",    HTTP_GET,    handler_api_files        },
    { "/api/files",    HTTP_DELETE, handler_api_files_delete  },
    { "/api/download", HTTP_GET,    handler_api_download     },
    { "/api/auth",     HTTP_GET,    handler_api_auth         },
    { "/api/timelapse/start",  HTTP_POST, handler_api_timelapse_start  },
    { "/api/timelapse/stop",   HTTP_POST, handler_api_timelapse_stop   },
    { "/api/timelapse/status", HTTP_GET,  handler_api_timelapse_status },
    { "/api/format",   HTTP_POST,   handler_api_format       },
    { "/api/storage",  HTTP_GET,    handler_api_storage      },
    { "/api/record",    HTTP_POST,   handler_api_record_post  },
    { "/api/record",    HTTP_GET,    handler_api_record_get   },
    { "/api/flash",    HTTP_GET,    handler_api_flash        },
    { "/api/flash",    HTTP_POST,   handler_api_flash        },

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
    config.max_uri_handlers = 30;
    config.stack_size = 8192;
    config.recv_wait_timeout = 10;    /* longer tolerance for slow WiFi */
    config.send_wait_timeout = 5;     /* free stalled connections faster (keepalive is primary) */
    config.lru_purge_enable = true;   /* clean up stale connections */
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.open_fn = on_session_open;  /* TCP_NODELAY on all connections */

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
    mjpeg_streamer_register(s_server);

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
