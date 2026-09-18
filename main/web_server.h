/*
 * Web Server Module - REST API + SPIFFS static file serving
 *
 * Provides 11 REST endpoints for device control, monitoring,
 * and file management, plus SPIFFS-based static file serving.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "common.h"
#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Initialize web server module (no-op, reserved for future use)
 */
esp_err_t web_server_init(void);

/**
 * @brief Start HTTP server on specified port with all URI handlers
 * @param port  TCP port to listen on
 * @return ESP_OK on success
 */
esp_err_t web_server_start(uint16_t port);

/**
 * @brief Stop HTTP server and release resources
 * @return ESP_OK on success
 */
esp_err_t web_server_stop(void);

/**
 * @brief Get the underlying HTTP server handle
 * @return Server handle, or NULL if not running
 */
httpd_handle_t web_server_get_handle(void);

#endif // WEB_SERVER_H
