#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "common.h"
#include "esp_err.h"

// Callback type for state changes
typedef void (*wifi_state_cb_t)(wifi_state_t state, void *user_data);

#define WIFI_MAX_CALLBACKS 4

/**
 * @brief Initialize WiFi subsystem (netif, event loop, default STA)
 */
esp_err_t wifi_init(void);

/**
 * @brief Start STA mode and connect to specified WiFi
 * @param ssid WiFi SSID
 * @param pass WiFi password
 */
esp_err_t wifi_start_sta(const char *ssid, const char *pass);

/**
 * @brief Start AP mode (SSID/password from CONFIG_DEFAULT_AP_SSID / CONFIG_DEFAULT_AP_PASS)
 */
esp_err_t wifi_start_ap(void);

/**
 * @brief Get current WiFi state
 */
wifi_state_t wifi_get_state(void);

/**
 * @brief Get current IP address string (static buffer, do not free)
 * @return IP string pointer, or "0.0.0.0" if not connected
 */
const char *wifi_get_ip_str(void);

/**
 * @brief Register WiFi state change callback (up to WIFI_MAX_CALLBACKS)
 * @param cb Callback function
 * @param user_data User data passed to callback
 * @return ESP_OK on success, ESP_ERR_NO_MEM if no slot available
 */
esp_err_t wifi_register_callback(wifi_state_cb_t cb, void *user_data);

/**
 * @brief Stop WiFi
 */
esp_err_t wifi_stop(void);

#endif // WIFI_MANAGER_H
