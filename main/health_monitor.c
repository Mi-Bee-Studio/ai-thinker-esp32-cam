#include "motion_detect.h"

#include "health_monitor.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "camera_driver.h"
#include "mjpeg_streamer.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timelapse.h"
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "video_recorder.h"
#include "config_manager.h"
#include "esp_mac.h"
#include "device_id.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "web_server.h"
#include "lwip/inet.h"

static const char *TAG = "health_monitor";

#define HEALTH_UPDATE_INTERVAL_US (10 * 1000000) // 10 seconds
#define PROMETHEUS_BUF_SIZE 8192

static health_metrics_t s_metrics;
static esp_timer_handle_t s_timer;
static char s_uptime_str[32];
static char *s_prometheus_str = NULL;

// Cached metrics not in health_metrics_t struct (added for prometheus enhancement)
static uint32_t s_frames_dropped;
static recorder_state_t s_recording_state;

/* httpd :80 self-heal probe — detects when all httpd workers are stuck
 * (slow handlers) or sockets are exhausted. A plain TCP connect is NOT
 * enough: LWIP accepts the connection at the TCP layer even when httpd
 * has no free worker to process it. Sending a real HTTP request forces a
 * worker to handle it, proving the event loop is alive. */
typedef enum {
    PROBE_OK,          /* 完整 HTTP 应答 */
    PROBE_APP_DEAD,    /* TCP 通但应用层无响应（worker 卡死/连不上/拒连） */
    PROBE_NO_RESOURCE, /* 探针自身拿不到资源（EMFILE/ENOBUFS/ENOMEM）——
                          socket 表满 ≠ httpd 死，饱和窗常可自愈（见下） */
} probe_result_t;

static probe_result_t probe_httpd_port80(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return (errno == EMFILE || errno == ENOBUFS || errno == ENOMEM)
                   ? PROBE_NO_RESOURCE : PROBE_APP_DEAD;
    }

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port   = htons(80),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };

    probe_result_t r = PROBE_OK;
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) == 0) {
        static const char req[] =
            "GET /api/status HTTP/1.0\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: close\r\n\r\n";
        if (send(sock, req, sizeof(req) - 1, 0) > 0) {
            char buf[32];
            int n = recv(sock, buf, sizeof(buf), 0);
            r = (n > 0) ? PROBE_OK : PROBE_APP_DEAD;
        } else {
            r = (errno == ENOBUFS || errno == ENOMEM)
                    ? PROBE_NO_RESOURCE : PROBE_APP_DEAD;
        }
    } else {
        /* 回环 connect 失败：内存类=资源紧张；ECONNREFUSED=httpd 没在听（真死） */
        r = (errno == ENOBUFS || errno == ENOMEM)
                ? PROBE_NO_RESOURCE : PROBE_APP_DEAD;
    }
    close(sock);
    return r;
}


static void health_collect_metrics(void)
{
    // Heap
    s_metrics.free_heap = esp_get_free_heap_size();
    s_metrics.min_free_heap = esp_get_minimum_free_heap_size();
    s_metrics.total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);

    // Uptime
    s_metrics.uptime_seconds = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

    // WiFi
    s_metrics.wifi_state = wifi_get_state();
    s_metrics.wifi_rssi = -1;
    if (s_metrics.wifi_state == WIFI_STATE_STA_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_metrics.wifi_rssi = (int8_t)ap_info.rssi;
        }
    }

    // CPU usage — ESP32 has no built-in CPU% API, leave as 0
    s_metrics.cpu_usage_pct = 0;

    // Storage
    s_metrics.sd_mounted = storage_is_available();
    s_metrics.photo_count = s_metrics.sd_mounted ? storage_get_photo_count() : 0;
    s_metrics.sd_free_mb = s_metrics.sd_mounted ? storage_get_free_space() : 0;
    s_metrics.sd_total_mb = s_metrics.sd_mounted ? storage_get_total_space() : 0;
    s_metrics.sd_usage_pct = (s_metrics.sd_total_mb > 0)
        ? (uint8_t)((uint64_t)(s_metrics.sd_total_mb - s_metrics.sd_free_mb) * 100 / s_metrics.sd_total_mb)
        : 0;

    // SPIFFS (cached — very rarely changes)
    size_t spiffs_used = 0;
    esp_spiffs_info("spiffs", &s_metrics.spiffs_total, &spiffs_used);
    s_metrics.spiffs_free = s_metrics.spiffs_total - spiffs_used;

    // Camera
    s_metrics.camera_initialized = camera_is_initialized();

    // MJPEG stream clients
    s_metrics.stream_clients = (uint32_t)mjpeg_streamer_get_client_count();

    // Motion events — updated by motion_detect module
    // (motion_events incremented via health_monitor_incr_motion_events)

    // Brightness metrics (populated by motion_detect module)
    s_metrics.brightness_pct = motion_detect_get_brightness_pct();
    s_metrics.brightness_method = motion_detect_get_brightness_method();
    s_metrics.scene_dark = motion_detect_is_scene_dark();


    // Recording state
    s_recording_state = recorder_get_state();
    s_frames_dropped = recorder_get_frames_dropped();
}

/* PIT-052 后续：socket 表卡满时探针判"应用层死"仍会触发兜底重启（有界自愈，
 * 设计如此），但占用者不可见。失败到 3/6 时转储 httpd 会话与对端——下次
 * 卡表即可点名谁占着表（半开连接/扫描器/NVR），供针对性修复。 */
static void log_socket_holders(void)
{
    httpd_handle_t h = web_server_get_handle();
    if (!h) {
        ESP_LOGW(TAG, "diag: httpd handle NULL");
        return;
    }
    int fds[16];
    size_t count = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(h, &count, fds) != ESP_OK) {
        ESP_LOGW(TAG, "diag: httpd_get_client_list failed");
        return;
    }
    ESP_LOGW(TAG, "diag: httpd sessions=%u mjpeg_clients=%d rssi=%d int_free=%u",
             (unsigned)count, mjpeg_streamer_get_client_count(),
             (int)s_metrics.wifi_rssi,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    for (size_t i = 0; i < count; i++) {
        struct sockaddr_in sa;
        socklen_t sl = sizeof(sa);
        char ip[16] = "?";
        int port = 0;
        if (getpeername(fds[i], (struct sockaddr *)&sa, &sl) == 0) {
            strncpy(ip, inet_ntoa(sa.sin_addr), sizeof(ip) - 1);
            port = ntohs(sa.sin_port);
        }
        ESP_LOGW(TAG, "diag: fd=%d peer=%s:%d", fds[i], ip, port);
    }
}

static void health_timer_callback(void *arg)
{
    (void)arg;
    health_collect_metrics();

    /* httpd :80 self-heal: probe the local HTTP server every cycle (10s).
     * If unresponsive for 6 consecutive cycles (60s), reboot to recover.
     * 2026-09-03 无声重启事故：弱射频掉线 >60s 时探测必然失败（EHOSTUNREACH），
     * 计数满 6 触发 esp_restart —— 掉线被翻译成"重启"，反而放大不稳定
     * （日志特征：recv 113 ×N → wifi txq stop → rst:0xc 无 panic 文字）。
     * 修复：WiFi 未连接时不计数——那说明网络不在，不说明 httpd 死了。
     * 2026-09-29 EMFILE 误杀（同 seeed PIT-002 家族教训）：弱窗 wifi:mem fail
     * 令 socket 表饱和，探针自身 socket() 即 EMFILE → 被当"httpd 死"计满 6
     * → 每天重启 17-41 次（serialtap events 实证）。修复：资源紧张
     * （EMFILE/ENOBUFS/ENOMEM）改走慢通道——连续 30 周期（5 分钟）才重启，
     * 分钟级饱和窗可自愈不重启；TCP 通而应用层无响应仍走 60s 快通道。 */
    static int httpd_stuck_count = 0;
    static int httpd_resource_streak = 0;
    probe_result_t pr = probe_httpd_port80();
    if (pr == PROBE_OK) {
        httpd_stuck_count = 0;
        httpd_resource_streak = 0;
    } else if (wifi_get_state() != WIFI_STATE_STA_CONNECTED &&
               wifi_get_state() != WIFI_STATE_AP) {
        httpd_stuck_count = 0;
        httpd_resource_streak = 0;
        ESP_LOGW(TAG, "httpd probe failed but WiFi down — not counting (network issue, not httpd)");
    } else if (pr == PROBE_NO_RESOURCE) {
        httpd_stuck_count = 0;
        httpd_resource_streak++;
        ESP_LOGW(TAG, "httpd probe starved (EMFILE/ENOBUFS, streak %d/30) — socket table saturated, not httpd dead",
                 httpd_resource_streak);
        if (httpd_resource_streak >= 30) {
            ESP_LOGE(TAG, "httpd socket table starved for 5min — rebooting");
            esp_restart();
        }
    } else {
        httpd_resource_streak = 0;
        httpd_stuck_count++;
        if (httpd_stuck_count == 3) {
            log_socket_holders();
        }
        ESP_LOGW(TAG, "httpd :80 probe failed (%d/6)", httpd_stuck_count);
        if (httpd_stuck_count >= 6) {
            ESP_LOGE(TAG, "httpd :80 unresponsive for 60s — rebooting");
            esp_restart();
        }
    }
}

esp_err_t health_monitor_init(void)
{
    esp_err_t err;

    // Initial collection
    health_collect_metrics();

    // Allocate Prometheus string buffer (prefer PSRAM for large buffer)
    s_prometheus_str = (char *)heap_caps_malloc(PROMETHEUS_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_prometheus_str) {
        ESP_LOGW(TAG, "PSRAM not available for metrics buffer, using internal RAM");
        s_prometheus_str = (char *)malloc(PROMETHEUS_BUF_SIZE);
        if (!s_prometheus_str) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for metrics buffer", PROMETHEUS_BUF_SIZE);
            return ESP_ERR_NO_MEM;
        }
    }

    // Create periodic timer
    const esp_timer_create_args_t timer_args = {
        .callback = health_timer_callback,
        .name = "health_monitor",
        .dispatch_method = ESP_TIMER_TASK,
    };
    err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_start_periodic(s_timer, HEALTH_UPDATE_INTERVAL_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Health monitor started (interval %ds)", HEALTH_UPDATE_INTERVAL_US / 1000000);
    return ESP_OK;
}

const health_metrics_t *health_monitor_get_metrics(void)
{
    return &s_metrics;
}

const char *health_monitor_get_uptime_str(void)
{
    uint32_t total = s_metrics.uptime_seconds;
    uint32_t days = total / 86400;
    total %= 86400;
    uint32_t hours = total / 3600;
    total %= 3600;
    uint32_t mins = total / 60;
    uint32_t secs = total % 60;

    snprintf(s_uptime_str, sizeof(s_uptime_str),
             "%" PRIu32 "d %" PRIu32 "h %" PRIu32 "m %" PRIu32 "s",
             days, hours, mins, secs);
    return s_uptime_str;
}

const char *health_monitor_get_prometheus_str(void)
{
    int offset = 0;
    int remaining = PROMETHEUS_BUF_SIZE;
    uint8_t wifi_metric_state;

#define PROM_LINE(fmt, ...) \
    do { \
        int written = snprintf(s_prometheus_str + offset, remaining, fmt "\n", ##__VA_ARGS__); \
        if (written > 0 && written < remaining) { \
            offset += written; \
            remaining -= written; \
        } \
    } while (0)

    /* ── Existing custom ESP metrics (preserved) ── */

    PROM_LINE("# HELP esp32_free_heap_bytes Free heap memory in bytes");
    PROM_LINE("# TYPE esp32_free_heap_bytes gauge");
    PROM_LINE("esp32_free_heap_bytes %" PRIu32, s_metrics.free_heap);

    PROM_LINE("# HELP esp32_min_free_heap_bytes Minimum free heap ever seen");
    PROM_LINE("# TYPE esp32_min_free_heap_bytes gauge");
    PROM_LINE("esp32_min_free_heap_bytes %" PRIu32, s_metrics.min_free_heap);

    PROM_LINE("# HELP esp32_uptime_seconds System uptime in seconds");
    PROM_LINE("# TYPE esp32_uptime_seconds counter");
    PROM_LINE("esp32_uptime_seconds %" PRIu32, s_metrics.uptime_seconds);

    PROM_LINE("# HELP esp32_wifi_rssi_dbm WiFi signal strength");
    PROM_LINE("# TYPE esp32_wifi_rssi_dbm gauge");
    PROM_LINE("esp32_wifi_rssi_dbm %d", s_metrics.wifi_rssi);

    PROM_LINE("# HELP esp32_sd_free_mb SD card free space in MB");
    PROM_LINE("# TYPE esp32_sd_free_mb gauge");
    PROM_LINE("esp32_sd_free_mb %" PRIu32, s_metrics.sd_free_mb);

    PROM_LINE("# HELP esp32_photo_count Total photos on SD card");
    PROM_LINE("# TYPE esp32_photo_count counter");
    PROM_LINE("esp32_photo_count %" PRIu32, s_metrics.photo_count);

    PROM_LINE("# HELP esp32_stream_clients Active MJPEG stream clients");
    PROM_LINE("# TYPE esp32_stream_clients gauge");
    PROM_LINE("esp32_stream_clients %" PRIu32, s_metrics.stream_clients);

    PROM_LINE("# HELP esp32_brightness_value Scene brightness percentage (0-100)");
    PROM_LINE("# TYPE esp32_brightness_value gauge");
    PROM_LINE("esp32_brightness_value %u", s_metrics.brightness_pct);

    PROM_LINE("# HELP esp32_brightness_method Brightness detection method (0=init, 1=register, 2=grayscale)");
    PROM_LINE("# TYPE esp32_brightness_method gauge");
    PROM_LINE("esp32_brightness_method{method=\"%s\"} %u",
            s_metrics.brightness_method == 1 ? "register" : (s_metrics.brightness_method == 2 ? "grayscale" : "init"),
            s_metrics.brightness_method);

    PROM_LINE("# HELP esp32_scene_dark Whether scene is detected as dark (1=yes, 0=no)");
    PROM_LINE("# TYPE esp32_scene_dark gauge");
    PROM_LINE("esp32_scene_dark %u", s_metrics.scene_dark ? 1 : 0);

    PROM_LINE("# HELP esp32_timelapse_photos Timelapse photos taken");
    PROM_LINE("# TYPE esp32_timelapse_photos counter");
    PROM_LINE("esp32_timelapse_photos %" PRIu32, s_metrics.timelapse_photos);
    PROM_LINE("# HELP esp32_timelapse_burst_photos Timelapse burst photos taken");
    PROM_LINE("# TYPE esp32_timelapse_burst_photos counter");
    PROM_LINE("esp32_timelapse_burst_photos %" PRIu32, s_metrics.timelapse_burst_photos);

    PROM_LINE("# HELP esp32_timelapse_current_interval Timelapse current interval in seconds");
    PROM_LINE("# TYPE esp32_timelapse_current_interval gauge");
    PROM_LINE("esp32_timelapse_current_interval %u", timelapse_get_current_interval_s());
    PROM_LINE("# HELP esp32_timelapse_mode Timelapse mode (0=static, 1=dynamic)");
    PROM_LINE("# TYPE esp32_timelapse_mode gauge");
    PROM_LINE("esp32_timelapse_mode %u", timelapse_get_mode());

    /* ── NEW: Custom esp_* metrics ── */

    // PSRAM
    PROM_LINE("# HELP esp_free_psram_bytes Free PSRAM in bytes");
    PROM_LINE("# TYPE esp_free_psram_bytes gauge");
    PROM_LINE("esp_free_psram_bytes %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    PROM_LINE("# HELP esp_total_psram_bytes Total PSRAM in bytes");
    PROM_LINE("# TYPE esp_total_psram_bytes gauge");
    PROM_LINE("esp_total_psram_bytes %d", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));

    // Recording state
    PROM_LINE("# HELP esp_recording_state Recording state (0=idle, 1=recording, 2=paused, 3=error)");
    PROM_LINE("# TYPE esp_recording_state gauge");
    PROM_LINE("esp_recording_state %u", (unsigned int)s_recording_state);


    // Frames dropped
    PROM_LINE("# HELP esp_frames_dropped_total Total frames dropped during recording");
    PROM_LINE("# TYPE esp_frames_dropped_total counter");
    PROM_LINE("esp_frames_dropped_total %" PRIu32, s_frames_dropped);

    // WiFi state as gauge
    wifi_metric_state = (s_metrics.wifi_state == WIFI_STATE_AP) ? 2
                      : (s_metrics.wifi_state == WIFI_STATE_STA_CONNECTED) ? 1 : 0;
    PROM_LINE("# HELP wifi_state WiFi state (0=disconnected, 1=STA, 2=AP)");
    PROM_LINE("# TYPE wifi_state gauge");
    PROM_LINE("wifi_state %u", wifi_metric_state);


    /* ── NEW: node_exporter-compatible metrics ── */

    {
        time_t now_t = time(NULL);
        uint32_t now = (now_t > 0) ? (uint32_t)now_t : 0;
        uint32_t uptime = s_metrics.uptime_seconds;
        uint32_t boot_time = (now > uptime) ? (now - uptime) : 0;
        uint8_t mac[6];
        char mac_str[18];
        device_get_mac(mac);
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        const char *device_name = config_get()->device_name;
        if (!device_name || device_name[0] == '\0') {
            device_name = "ESP32-CAM";
        }

        size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        size_t internal_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t psram_total    = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        size_t psram_free     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t spiffs_total   = s_metrics.spiffs_total > 0 ? s_metrics.spiffs_total : 458752;
        size_t spiffs_free    = s_metrics.spiffs_free;

        uint64_t sd_total_bytes = (uint64_t)s_metrics.sd_total_mb * 1048576ULL;
        uint64_t sd_free_bytes  = (uint64_t)s_metrics.sd_free_mb * 1048576ULL;

        PROM_LINE("");
        PROM_LINE("# HELP node_exporter_build_info node_exporter build info");
        PROM_LINE("# TYPE node_exporter_build_info gauge");
        PROM_LINE("node_exporter_build_info{version=\"mibee-cam\",revision=\"v1.0\"} 1");

        PROM_LINE("");
        PROM_LINE("# HELP node_uname_info Labeled system information");
        PROM_LINE("# TYPE node_uname_info gauge");
        PROM_LINE("node_uname_info{sysname=\"ESP-IDF\",release=\"v6.0.1\",version=\"v6.0.1\",machine=\"ESP32\",nodename=\"%s\"} 1", device_name);

        PROM_LINE("");
        PROM_LINE("# HELP node_boot_time_seconds Node boot time in unixtime");
        PROM_LINE("# TYPE node_boot_time_seconds gauge");
        PROM_LINE("node_boot_time_seconds %" PRIu32, boot_time);

        PROM_LINE("");
        PROM_LINE("# HELP node_time_seconds System time in seconds since epoch");
        PROM_LINE("# TYPE node_time_seconds gauge");
        PROM_LINE("node_time_seconds %" PRIu32, now);

        PROM_LINE("");
        PROM_LINE("# HELP node_memory_MemTotal_bytes Total internal RAM");
        PROM_LINE("# TYPE node_memory_MemTotal_bytes gauge");
        PROM_LINE("node_memory_MemTotal_bytes %u", internal_total);
        PROM_LINE("# HELP node_memory_MemFree_bytes Free internal RAM");
        PROM_LINE("# TYPE node_memory_MemFree_bytes gauge");
        PROM_LINE("node_memory_MemFree_bytes %u", internal_free);
        PROM_LINE("# HELP node_memory_MemAvailable_bytes Available internal RAM");
        PROM_LINE("# TYPE node_memory_MemAvailable_bytes gauge");
        PROM_LINE("node_memory_MemAvailable_bytes %u", internal_free);

        PROM_LINE("");
        PROM_LINE("# HELP node_memory_SwapTotal_bytes Total PSRAM");
        PROM_LINE("# TYPE node_memory_SwapTotal_bytes gauge");
        PROM_LINE("node_memory_SwapTotal_bytes %u", psram_total);
        PROM_LINE("# HELP node_memory_SwapFree_bytes Free PSRAM");
        PROM_LINE("# TYPE node_memory_SwapFree_bytes gauge");
        PROM_LINE("node_memory_SwapFree_bytes %u", psram_free);

        PROM_LINE("");
        PROM_LINE("# HELP node_filesystem_size_bytes Filesystem size");
        PROM_LINE("# TYPE node_filesystem_size_bytes gauge");
        if (s_metrics.sd_mounted) {
            PROM_LINE("node_filesystem_size_bytes{device=\"/dev/sdcard\",fstype=\"vfat\",mountpoint=\"/sdcard\"} %llu", sd_total_bytes);
            PROM_LINE("node_filesystem_avail_bytes{device=\"/dev/sdcard\",fstype=\"vfat\",mountpoint=\"/sdcard\"} %llu", sd_free_bytes);
            PROM_LINE("node_filesystem_free_bytes{device=\"/dev/sdcard\",fstype=\"vfat\",mountpoint=\"/sdcard\"} %llu", sd_free_bytes);
        } else {
            PROM_LINE("node_filesystem_size_bytes{device=\"/dev/sdcard\",fstype=\"vfat\",mountpoint=\"/sdcard\"} 0");
            PROM_LINE("node_filesystem_avail_bytes{device=\"/dev/sdcard\",fstype=\"vfat\",mountpoint=\"/sdcard\"} 0");
            PROM_LINE("node_filesystem_free_bytes{device=\"/dev/sdcard\",fstype=\"vfat\",mountpoint=\"/sdcard\"} 0");
        }
        PROM_LINE("node_filesystem_size_bytes{device=\"spiffs\",fstype=\"spiffs\",mountpoint=\"/spiffs\"} %u", spiffs_total);
        PROM_LINE("node_filesystem_avail_bytes{device=\"spiffs\",fstype=\"spiffs\",mountpoint=\"/spiffs\"} %u", spiffs_free);
        PROM_LINE("node_filesystem_free_bytes{device=\"spiffs\",fstype=\"spiffs\",mountpoint=\"/spiffs\"} %u", spiffs_free);

        PROM_LINE("");
        PROM_LINE("# HELP node_network_up WiFi interface up status");
        PROM_LINE("# TYPE node_network_up gauge");
        PROM_LINE("node_network_up{device=\"wifi0\"} %u", wifi_metric_state == 1 ? 1 : 0);

        PROM_LINE("");
        PROM_LINE("# HELP node_network_info Network info with MAC address");
        PROM_LINE("# TYPE node_network_info gauge");
        PROM_LINE("node_network_info{device=\"wifi0\",address=\"%s\"} 1", mac_str);

        PROM_LINE("");
        PROM_LINE("# HELP node_cpu_seconds_total CPU time per core/mode (estimated)");
        PROM_LINE("# TYPE node_cpu_seconds_total counter");
        PROM_LINE("node_cpu_seconds_total{cpu=\"0\",mode=\"idle\"} %" PRIu32, (uint32_t)(uptime * 0.8f));
        PROM_LINE("node_cpu_seconds_total{cpu=\"0\",mode=\"user\"} %" PRIu32, (uint32_t)(uptime * 0.18f));
        PROM_LINE("node_cpu_seconds_total{cpu=\"0\",mode=\"system\"} %" PRIu32, (uint32_t)(uptime * 0.02f));
        PROM_LINE("node_cpu_seconds_total{cpu=\"1\",mode=\"idle\"} %" PRIu32, (uint32_t)(uptime * 0.8f));
        PROM_LINE("node_cpu_seconds_total{cpu=\"1\",mode=\"user\"} %" PRIu32, (uint32_t)(uptime * 0.18f));
        PROM_LINE("node_cpu_seconds_total{cpu=\"1\",mode=\"system\"} %" PRIu32, (uint32_t)(uptime * 0.02f));
    }

#undef PROM_LINE

    return s_prometheus_str;
}

void health_monitor_incr_motion_events(void)
{
    s_metrics.motion_events++;
}

void health_monitor_incr_timelapse_photos(void)
{
    s_metrics.timelapse_photos++;
}

void health_monitor_incr_timelapse_burst_photos(void)
{
    s_metrics.timelapse_burst_photos++;
}

uint32_t health_monitor_get_timelapse_photos(void)
{
    return s_metrics.timelapse_photos;
}

uint32_t health_monitor_get_timelapse_burst_photos(void)
{
    return s_metrics.timelapse_burst_photos;
}
