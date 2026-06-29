/*
 * WiFi Manager - AP/STA mode with auto-reconnect and state callbacks
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include <string.h>
#include "config_manager.h"
#include "sd_logger.h"

static const char *TAG = "wifi_manager";

// --- State ---
static wifi_state_t s_state = WIFI_STATE_AP;
static char s_ip_str[16] = "0.0.0.0";
static bool s_sta_active = false;

/* Dual WiFi failover state */
static int s_sta_retry_count = 0;
static bool s_using_secondary = false;
#define MAX_RETRIES_PER_NETWORK 10

// Callbacks (up to 4)
typedef struct {
    wifi_state_cb_t cb;
    void *user_data;
} callback_entry_t;

static callback_entry_t s_callbacks[WIFI_MAX_CALLBACKS] = {0};

// Netifs
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

// Event handler instances
static esp_event_handler_instance_t s_wifi_handler = NULL;
static esp_event_handler_instance_t s_ip_handler = NULL;

// Reconnect timer
static TimerHandle_t s_reconnect_timer = NULL;
#define RECONNECT_INTERVAL_MS (2000)

// --- Forward declarations ---
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void set_state(wifi_state_t new_state);
static void notify_callbacks(wifi_state_t state);
static void reconnect_timer_cb(TimerHandle_t timer);

// --- Helpers ---

/** @brief Human-readable WiFi disconnect reason for diagnostics */
static const char *wifi_disconnect_reason_str(uint8_t reason)
{
    switch (reason) {
    case 2:   return "AUTH_EXPIRE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT (likely wrong password)";
    case 200: return "BEACON_TIMEOUT (weak signal/interference)";
    case 201: return "NO_AP_FOUND (wrong SSID or out of range)";
    case 202: return "AUTH_FAIL (wrong password or auth mismatch)";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT (likely wrong password)";
    case 205: return "CONNECTION_FAIL";
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD (authmode mismatch)";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    default:  return "OTHER";
    }
}

static void set_state(wifi_state_t new_state)
{
    if (s_state != new_state) {
        ESP_LOGI(TAG, "State: %d -> %d", s_state, new_state);
        s_state = new_state;
        notify_callbacks(new_state);
    }
}

static void notify_callbacks(wifi_state_t state)
{
    for (int i = 0; i < WIFI_MAX_CALLBACKS; i++) {
        if (s_callbacks[i].cb) {
            s_callbacks[i].cb(state, s_callbacks[i].user_data);
        }
    }
}

static void start_reconnect_timer(void)
{
    if (!s_reconnect_timer) {
        s_reconnect_timer = xTimerCreate("wifi_reconn", pdMS_TO_TICKS(RECONNECT_INTERVAL_MS),
                                         pdFALSE, NULL, reconnect_timer_cb);
    }
    if (s_reconnect_timer && !xTimerIsTimerActive(s_reconnect_timer)) {
        xTimerStart(s_reconnect_timer, 0);
    }
}

static void stop_reconnect_timer(void)
{
    if (s_reconnect_timer && xTimerIsTimerActive(s_reconnect_timer)) {
        xTimerStop(s_reconnect_timer, 0);
    }
}

static void reconnect_timer_cb(TimerHandle_t timer)
{
    if (s_sta_active) {
        ESP_LOGI(TAG, "Reconnect timer fired, attempting connect...");
        esp_wifi_connect();
    }
}

// --- Event handler ---

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            set_state(WIFI_STATE_STA_CONNECTING);
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected to AP, waiting for IP...");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "STA disconnected reason=%d %s (retry %d/%d)",
                     disc->reason, wifi_disconnect_reason_str(disc->reason),
                     s_sta_retry_count + 1, MAX_RETRIES_PER_NETWORK);
            sd_logf(SD_LOG_WARN, "wifi", "STA disconnected reason=%d (%s), retry %d/%d",
                     disc->reason, wifi_disconnect_reason_str(disc->reason),
                     s_sta_retry_count + 1, MAX_RETRIES_PER_NETWORK);
            stop_reconnect_timer();
            set_state(WIFI_STATE_STA_DISCONNECTED);
            s_sta_retry_count++;

            /* Dual WiFi failover: try secondary network after max retries on primary */
            if (s_sta_retry_count >= MAX_RETRIES_PER_NETWORK) {
                const cam_config_t *cfg = config_get();
                if (!s_using_secondary && cfg->wifi_ssid_2[0] != '\0') {
                    ESP_LOGW(TAG, "Primary WiFi failed %d times, switching to secondary: %s",
                             s_sta_retry_count, cfg->wifi_ssid_2);
                    sd_logf(SD_LOG_ERROR, "wifi", "Primary WiFi failed, switching to secondary: %s", cfg->wifi_ssid_2);
                    s_using_secondary = true;
                    s_sta_retry_count = 0;
                    wifi_start_sta(cfg->wifi_ssid_2, cfg->wifi_pass_2);
                    return;
                } else if (s_using_secondary) {
                    /* Failed on secondary too — try primary again */
                    ESP_LOGW(TAG, "Secondary WiFi also failed, retrying primary: %s", cfg->wifi_ssid);
                    sd_logf(SD_LOG_ERROR, "wifi", "Secondary WiFi also failed, retrying primary: %s", cfg->wifi_ssid);
                    s_using_secondary = false;
                    s_sta_retry_count = 0;
                    wifi_start_sta(cfg->wifi_ssid, cfg->wifi_pass);
                    return;
                }
                /* No secondary configured or alternating — keep retrying with reconnect timer */
            }

            start_reconnect_timer();
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            set_state(WIFI_STATE_AP);
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "STA got IP: %s", s_ip_str);
            stop_reconnect_timer();
            set_state(WIFI_STATE_STA_CONNECTED);
            if (s_sta_retry_count > 0) {
                sd_logf(SD_LOG_WARN, "wifi", "STA reconnected after %d retries, IP: %s", s_sta_retry_count, s_ip_str);
            }
            s_sta_retry_count = 0;  /* reset failover counter on success */
        }
    }
}

// --- Public API ---


/* WiFi watchdog: RSSI monitor, periodic reconnect, and smart SSID roaming */
static void wifi_watchdog_task(void *arg)
{
    int low_rssi_count = 0;
    TickType_t last_reconnect_tick = xTaskGetTickCount();
    TickType_t last_roam_scan_tick = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));  /* 30s interval */
        if (s_state != WIFI_STATE_STA_CONNECTED) continue;

        /* --- Periodic reconnect: clear WiFi driver state every N hours --- */
        uint16_t reconnect_hours = config_get()->wifi_reconnect_hours;
        if (reconnect_hours > 0) {
            TickType_t elapsed_ms = (xTaskGetTickCount() - last_reconnect_tick) * portTICK_PERIOD_MS;
            uint32_t threshold_ms = (uint32_t)reconnect_hours * 3600U * 1000U;
            if (elapsed_ms >= threshold_ms) {
                ESP_LOGI(TAG, "Periodic WiFi reconnect (%uh interval)", reconnect_hours);
                sd_logf(SD_LOG_WARN, "wifi", "Periodic reconnect (%uh interval)", reconnect_hours);
                esp_wifi_disconnect();
                last_reconnect_tick = xTaskGetTickCount();
                continue;
            }
        }

        /* --- RSSI watchdog: critical signal check (-90 dBm) --- */
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) continue;

        ESP_LOGI(TAG, "RSSI: %d dBm", ap.rssi);

        if (ap.rssi < -90) {
            if (++low_rssi_count >= 3) {  /* 90s of critical RSSI */
                ESP_LOGW(TAG, "RSSI %d dBm for 90s, forcing reconnect", ap.rssi);
                sd_logf(SD_LOG_WARN, "wifi", "RSSI %d dBm sustained, forcing reconnect", ap.rssi);
                esp_wifi_disconnect();
                last_reconnect_tick = xTaskGetTickCount();
                low_rssi_count = 0;
            }
        } else {
            low_rssi_count = 0;
        }

        /* --- V14: RSSI-based smart roaming across SSIDs --- */
        const cam_config_t *cfg = config_get();
        if (cfg->wifi_roam_rssi_threshold == 0) continue;  /* 0 = roaming disabled */
        if (cfg->wifi_ssid[0] == '\0') continue;

        /* Determine the "other" network to probe */
        const char *other_ssid = s_using_secondary ? cfg->wifi_ssid : cfg->wifi_ssid_2;
        const char *other_pass = s_using_secondary ? cfg->wifi_pass : cfg->wifi_pass_2;
        if (!other_ssid || other_ssid[0] == '\0') continue;

        /* Throttle roam scans to every 60s */
        TickType_t roam_elapsed_ms = (xTaskGetTickCount() - last_roam_scan_tick) * portTICK_PERIOD_MS;
        if (roam_elapsed_ms < 60000) continue;

        /* Decide whether to scan:
         * - On secondary: always check for primary (preferred network)
         * - On primary: only scan when RSSI drops below threshold */
        bool should_scan = s_using_secondary || (ap.rssi < cfg->wifi_roam_rssi_threshold);
        if (!should_scan) continue;

        last_roam_scan_tick = xTaskGetTickCount();

        /* Scan for the other SSID */
        ESP_LOGI(TAG, "Roam scan: probing '%s' (on %s, RSSI=%d)",
                 other_ssid, s_using_secondary ? "secondary" : "primary", ap.rssi);

        wifi_scan_config_t scan_cfg = {
            .ssid = (uint8_t *)other_ssid,
            .bssid = NULL,
            .channel = 0,
        };

        if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
            ESP_LOGW(TAG, "Roam scan failed");
            continue;
        }

        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num == 0) continue;

        wifi_ap_record_t *aps = calloc(ap_num, sizeof(wifi_ap_record_t));
        if (!aps) continue;
        esp_wifi_scan_get_ap_records(&ap_num, aps);

        int8_t best_rssi = -128;
        for (int i = 0; i < ap_num; i++) {
            if (aps[i].rssi > best_rssi) best_rssi = aps[i].rssi;
        }
        free(aps);

        int8_t gap = best_rssi - ap.rssi;
        ESP_LOGI(TAG, "Roam: '%s' RSSI=%d, current=%d, gap=%d (need>=%d)",
                 other_ssid, best_rssi, ap.rssi, gap, cfg->wifi_roam_rssi_gap);

        if (gap >= (int8_t)cfg->wifi_roam_rssi_gap) {
            ESP_LOGI(TAG, "Smart roaming: switching to '%s' (%d dBm > current %d, gap=%d)",
                     other_ssid, best_rssi, ap.rssi, gap);
            sd_logf(SD_LOG_WARN, "wifi", "Smart roam: %s(%d) -> %s(%d), gap=%d",
                    s_using_secondary ? "secondary" : "primary", ap.rssi,
                    other_ssid, best_rssi, gap);

            s_using_secondary = !s_using_secondary;
            s_sta_retry_count = 0;
            wifi_start_sta(other_ssid, other_pass);
            last_reconnect_tick = xTaskGetTickCount();
        }
    }
}
esp_err_t wifi_init(void)
{
    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* TX power and power save will be applied after wifi_start() */

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, &s_wifi_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, &s_ip_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    s_state = WIFI_STATE_AP;
    s_sta_active = false;
    memset(s_ip_str, 0, sizeof(s_ip_str));
    strcpy(s_ip_str, "0.0.0.0");

    ESP_LOGI(TAG, "WiFi manager initialized");
    xTaskCreate(wifi_watchdog_task, "wifi_wd", 4096, NULL, 3, NULL);
    return ESP_OK;
}

esp_err_t wifi_start_sta(const char *ssid, const char *pass)
{
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,   /* auto-negotiate WPA2/WPA3/mixed/WPA/OPEN */
            .pmf_cfg = { .capable = true, .required = false }, /* PMF capable, not required (802.11w) */
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,        /* WPA3 SAE: H2E + hunting-and-pecking */
            .listen_interval = 1,                      /* listen to every beacon — prevent AP traffic buffering */
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (pass) {
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        ESP_LOGI(TAG, "STA config: SSID='%s', pass_len=%u", ssid, (unsigned)strlen(pass));
    } else {
        ESP_LOGW(TAG, "STA config: SSID='%s', NO PASSWORD", ssid);
    }

    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sta_active = true;
    stop_reconnect_timer();
    memset(s_ip_str, 0, sizeof(s_ip_str));
    strcpy(s_ip_str, "0.0.0.0");

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start STA: %s", esp_err_to_name(ret));
        s_sta_active = false;
        return ret;
    }

    /* Set DHCP hostname from device_name (replaces default "espressif") */
    esp_netif_set_hostname(s_sta_netif, config_get()->device_name);

    /* Apply TX power from config (must be after wifi_start) */
    const cam_config_t *cam_cfg = config_get();
    int8_t tx_pwr = (cam_cfg->wifi_tx_power >= 8 && cam_cfg->wifi_tx_power <= 84) ? (int8_t)cam_cfg->wifi_tx_power : 80;
    ret = esp_wifi_set_max_tx_power(tx_pwr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set_max_tx_power(%d/%.1fdBm) failed: %s", tx_pwr, tx_pwr * 0.25f, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi TX power set to %d (%.1fdBm)", tx_pwr, tx_pwr * 0.25f);
    }

    wifi_ps_type_t ps = cam_cfg->wifi_power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
    ret = esp_wifi_set_ps(ps);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set_ps failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi power save: %s", cam_cfg->wifi_power_save ? "MIN_MODEM" : "NONE");
    }

    /* HT20: ~3dB better RX sensitivity than HT40 — critical for weak-signal links */
    ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set_bandwidth HT20 failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi bandwidth: HT20 (weak-signal optimized)");
    }

    ESP_LOGI(TAG, "STA starting, connecting to %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_start_ap(void)
{
    esp_err_t ret;

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_DEFAULT_AP_SSID,
            .password = CONFIG_DEFAULT_AP_PASS,
            .ssid_len = strlen(CONFIG_DEFAULT_AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sta_active = false;
    stop_reconnect_timer();

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "AP starting: SSID=%s", CONFIG_DEFAULT_AP_SSID);
    return ESP_OK;
}

wifi_state_t wifi_get_state(void)
{
    return s_state;
}

const char *wifi_get_ip_str(void)
{
    return s_ip_str;
}

esp_err_t wifi_register_callback(wifi_state_cb_t cb, void *user_data)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < WIFI_MAX_CALLBACKS; i++) {
        if (s_callbacks[i].cb == NULL) {
            s_callbacks[i].cb = cb;
            s_callbacks[i].user_data = user_data;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "No free callback slot (max %d)", WIFI_MAX_CALLBACKS);
    return ESP_ERR_NO_MEM;
}

esp_err_t wifi_stop(void)
{
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sta_active = false;
    stop_reconnect_timer();
    memset(s_ip_str, 0, sizeof(s_ip_str));
    strcpy(s_ip_str, "0.0.0.0");
    set_state(WIFI_STATE_AP);

    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}
