/**
 * @file mjpeg_streamer.h
 * @brief MJPEG real-time video streaming over independent TCP server on port 81.
 *
 * Serves MJPEG frames via a separate TCP server that bypasses the httpd.
 * Supports up to 2 concurrent clients.
 */

#ifndef MJPEG_STREAMER_H
#define MJPEG_STREAMER_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MJPEG streamer internals (mutex, client counter).
 * @return ESP_OK or ESP_ERR_NO_MEM
 */
esp_err_t mjpeg_streamer_init(void);

/**
 * @brief Start the MJPEG TCP streaming server on the specified port.
 * @param port TCP port to listen on (typically 81)
 * @return ESP_OK or ESP_FAIL
 *
 * Creates a listening socket and spawns a task that accepts connections.
 * Each connection spawns a client task that streams MJPEG via multipart/x-mixed-replace.
 */
esp_err_t mjpeg_stream_server_start(uint16_t port);

/**
 * @brief Return current number of active MJPEG stream clients.
 */
int mjpeg_streamer_get_client_count(void);

/**
 * @brief Stop all active streams and release resources.
 */
void mjpeg_streamer_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MJPEG_STREAMER_H */