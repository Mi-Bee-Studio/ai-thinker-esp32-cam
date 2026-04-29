#ifndef WEBDAV_CLIENT_H
#define WEBDAV_CLIENT_H

#include "common.h"
#include "esp_err.h"

/**
 * @brief Upload a local file to WebDAV server via HTTP PUT
 *
 * Uses esp_http_client to PUT the file with Basic auth and
 * streams data in 4KB chunks. Retries up to 3 times with
 * exponential backoff (2s, 4s).
 *
 * @param host         Server hostname or IP
 * @param port         Server port (usually 80 or 5005)
 * @param user         Username for Basic auth
 * @param pass         Password for Basic auth
 * @param remote_path  Remote file path (appended to URL path)
 * @param local_path   Local file path to upload
 * @return ESP_OK on success (HTTP 200/201/204), ESP_FAIL otherwise
 */
esp_err_t webdav_upload(const char *host, uint16_t port,
                        const char *user, const char *pass,
                        const char *remote_path, const char *local_path);

#endif // WEBDAV_CLIENT_H
