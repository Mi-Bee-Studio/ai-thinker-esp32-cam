#ifndef WEBDAV_CLIENT_H
#define WEBDAV_CLIENT_H

#include "esp_err.h"

/**
 * @brief Upload a file to a WebDAV server via HTTP PUT
 *
 * @param host     Server hostname or IP
 * @param port     Server port
 * @param user     Username for Basic auth (can be NULL)
 * @param pass     Password for Basic auth (can be NULL)
 * @param remote   Remote path (e.g. "/photos/img.jpg")
 * @param local    Local file path (e.g. "/sdcard/photos/img.jpg")
 * @return ESP_OK on success
 */
esp_err_t webdav_upload(const char *host, uint16_t port,
                        const char *user, const char *pass,
                        const char *remote, const char *local);

#endif // WEBDAV_CLIENT_H
