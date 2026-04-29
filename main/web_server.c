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
#include "nas_uploader.h"
#include "time_sync.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

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

    /* Camera */
    cJSON_AddStringToObject(data, "camera", camera_get_sensor_name());

    /* WiFi sub-object */
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddNumberToObject(wifi, "state", (double)m->wifi_state);
    cJSON_AddStringToObject(wifi, "ip", wifi_get_ip_str());
    if (m->wifi_rssi >= -127) {
        cJSON_AddNumberToObject(wifi, "rssi", (double)m->wifi_rssi);
    }
    cJSON_AddItemToObject(data, "wifi", wifi);

    /* Storage sub-object */
    cJSON *storage = cJSON_CreateObject();
    cJSON_AddBoolToObject(storage, "available", m->sd_mounted);
    cJSON_AddNumberToObject(storage, "free_mb", (double)m->sd_free_mb);
    cJSON_AddNumberToObject(storage, "photos", (double)m->photo_count);
    cJSON_AddItemToObject(data, "storage", storage);

    /* Uptime */
    cJSON_AddStringToObject(data, "uptime", health_monitor_get_uptime_str());

    /* Motion detection enabled */
    cJSON_AddBoolToObject(data, "motion", cfg->motion_threshold > 0);

    /* Stream clients */
    cJSON_AddNumberToObject(data, "stream_clients", (double)m->stream_clients);

    /* NAS upload queue */
    cJSON_AddNumberToObject(data, "nas_pending", (double)m->nas_pending);

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
    cJSON_AddNumberToObject(data, "resolution", (double)cfg->resolution);
    cJSON_AddNumberToObject(data, "fps", (double)cfg->fps);
    cJSON_AddNumberToObject(data, "jpeg_quality", (double)cfg->jpeg_quality);
    cJSON_AddStringToObject(data, "timezone", cfg->timezone);
    cJSON_AddNumberToObject(data, "motion_threshold", (double)cfg->motion_threshold);
    cJSON_AddNumberToObject(data, "motion_cooldown", (double)cfg->motion_cooldown);
    cJSON_AddNumberToObject(data, "nas_protocol", (double)cfg->nas_protocol);
    cJSON_AddStringToObject(data, "nas_host", cfg->nas_host);
    cJSON_AddNumberToObject(data, "nas_port", (double)cfg->nas_port);
    cJSON_AddStringToObject(data, "nas_path", cfg->nas_path);

    return send_json_ok(req, data);
}

/* ------------------------------------------------------------------ */
/*  POST /api/config                                                   */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_config_post(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_unauthorized(req);
    }

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
    bool need_save = false;

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
        if (ssid) {
            config_set_wifi(ssid, pass ? pass : "");
        }
    }

    /* Resolution (has dedicated setter) */
    if ((item = cJSON_GetObjectItem(json, "resolution")) && cJSON_IsNumber(item)) {
        config_set_resolution((camera_resolution_t)item->valueint);
    }

    if ((item = cJSON_GetObjectItem(json, "fps")) && cJSON_IsNumber(item)) {
        config_set_fps((uint8_t)item->valueint);
    }

    if ((item = cJSON_GetObjectItem(json, "jpeg_quality")) && cJSON_IsNumber(item)) {
        config_set_jpeg_quality((uint8_t)item->valueint);
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

    /* NAS settings (has dedicated setter — merge with current values) */
    {
        const cam_config_t *cur = config_get();
        nas_protocol_t protocol = cur->nas_protocol;
        const char *host = cur->nas_host;
        uint16_t port = cur->nas_port;
        const char *user = cur->nas_user;
        const char *pass = cur->nas_pass;
        const char *path = cur->nas_path;
        bool nas_changed = false;

        item = cJSON_GetObjectItem(json, "nas_protocol");
        if (item && cJSON_IsNumber(item)) { protocol = (nas_protocol_t)item->valueint; nas_changed = true; }
        item = cJSON_GetObjectItem(json, "nas_host");
        if (item && cJSON_IsString(item)) { host = item->valuestring; nas_changed = true; }
        item = cJSON_GetObjectItem(json, "nas_port");
        if (item && cJSON_IsNumber(item)) { port = (uint16_t)item->valueint; nas_changed = true; }
        item = cJSON_GetObjectItem(json, "nas_user");
        if (item && cJSON_IsString(item)) { user = item->valuestring; nas_changed = true; }
        item = cJSON_GetObjectItem(json, "nas_pass");
        if (item && cJSON_IsString(item)) { pass = item->valuestring; nas_changed = true; }
        item = cJSON_GetObjectItem(json, "nas_path");
        if (item && cJSON_IsString(item)) { path = item->valuestring; nas_changed = true; }

        if (nas_changed) {
            config_set_nas(protocol, host, port, user, pass, path);
        }
    }

    if (need_save) {
        config_save();
    }

    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "message", "config updated");
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
    esp_err_t err = camera_capture(&fb);
    if (err != ESP_OK || fb == NULL) {
        return send_json_error(req, "camera capture failed", 500);
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t ret = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    camera_return_fb(fb);
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
    if (!storage_is_available()) {
        return send_json_error(req, "SD card not available", 503);
    }

    char *buf = malloc(4096);
    if (!buf) {
        return send_json_error(req, "out of memory", 500);
    }

    int count = storage_list_photos("/sdcard/photos", buf, 4096);
    if (count <= 0) {
        free(buf);
        cJSON *arr = cJSON_CreateArray();
        cJSON *data = cJSON_CreateObject();
        cJSON_AddItemToObject(data, "files", arr);
        return send_json_ok(req, data);
    }

    /* Parse newline-separated paths into JSON array */
    cJSON *arr = cJSON_CreateArray();
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        if (strlen(line) > 0) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(line));
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(buf);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "files", arr);
    cJSON_AddNumberToObject(data, "count", (double)count);
    return send_json_ok(req, data);
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
/*  POST /api/upload  (trigger manual NAS upload)                      */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_upload(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_unauthorized(req);
    }

    char *body = read_body(req, 512);
    if (!body) {
        return send_json_error(req, "empty or invalid body", 400);
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return send_json_error(req, "invalid JSON", 400);
    }

    cJSON *file_item = cJSON_GetObjectItem(json, "file");
    if (!file_item || !cJSON_IsString(file_item)) {
        cJSON_Delete(json);
        return send_json_error(req, "missing 'file' field", 400);
    }

    const char *filepath = file_item->valuestring;

    /* Path traversal protection */
    if (strstr(filepath, "..") != NULL) {
        cJSON_Delete(json);
        return send_json_error(req, "invalid file path", 400);
    }

    esp_err_t ret = nas_uploader_enqueue(filepath);
    cJSON_Delete(json);

    if (ret != ESP_OK) {
        return send_json_error(req, "failed to enqueue upload", 500);
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "upload queued");
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
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(filepath));

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
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
    { "/api/download", HTTP_GET,    handler_api_download     },
    { "/api/upload",   HTTP_POST,   handler_api_upload       },
    /* Wildcards last (lowest priority) */
    { "/*",            HTTP_OPTIONS, handler_options         },
    { "/*",            HTTP_GET,    handler_static          },
};

#define NUM_URIS (sizeof(s_uris) / sizeof(s_uris[0]))

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t web_server_init(void)
{
    ESP_LOGI(TAG, "web_server_init (reserved)");
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
    config.max_uri_handlers = 20;
    config.stack_size = 8192;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server on port %d: %s",
                 port, esp_err_to_name(ret));
        return ret;
    }

    /* Register API endpoints (specific paths before wildcards) */
    for (size_t i = 0; i < NUM_URIS; i++) {
        httpd_uri_t uri = {
            .uri      = s_uris[i].uri,
            .method   = s_uris[i].method,
            .handler  = s_uris[i].handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(s_server, &uri);
    }

    /* Register MJPEG stream handler (before wildcard, after API) */
    ret = mjpeg_streamer_register(s_server);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register MJPEG stream handler");
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
