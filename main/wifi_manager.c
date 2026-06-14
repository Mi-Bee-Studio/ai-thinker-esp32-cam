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
#include <string.h>
#include "config_manager.h"

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
#define RECONNECT_INTERVAL_MS (5000)

// --- Forward declarations ---
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void set_state(wifi_state_t new_state);
static void notify_callbacks(wifi_state_t state);
static void reconnect_timer_cb(TimerHandle_t timer);

// --- Helpers ---

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

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA disconnected (retry %d/%d)", s_sta_retry_count + 1, MAX_RETRIES_PER_NETWORK);
            stop_reconnect_timer();
            set_state(WIFI_STATE_STA_DISCONNECTED);
            s_sta_retry_count++;

            /* Dual WiFi failover: try secondary network after max retries on primary */
            if (s_sta_retry_count >= MAX_RETRIES_PER_NETWORK) {
                const cam_config_t *cfg = config_get();
                if (!s_using_secondary && cfg->wifi_ssid_2[0] != '\0') {
                    ESP_LOGW(TAG, "Primary WiFi failed %d times, switching to secondary: %s",
                             s_sta_retry_count, cfg->wifi_ssid_2);
                    s_using_secondary = true;
                    s_sta_retry_count = 0;
                    wifi_start_sta(cfg->wifi_ssid_2, cfg->wifi_pass_2);
                    return;
                } else if (s_using_secondary) {
                    /* Failed on secondary too — try primary again */
                    ESP_LOGW(TAG, "Secondary WiFi also failed, retrying primary: %s", cfg->wifi_ssid);
                    s_using_secondary = false;
                    s_sta_retry_count = 0;
                    wifi_start_sta(cfg->wifi_ssid, cfg->wifi_pass);
                    return;
                }
                /* No secondary configured or alternating — keep retrying with reconnect timer */
            }

            start_reconnect_timer();
            break;

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
            s_sta_retry_count = 0;  /* reset failover counter on success */
        }
    }
}

// --- Public API ---

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
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
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
