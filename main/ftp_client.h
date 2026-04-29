#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include "common.h"
#include "esp_err.h"

/**
 * @brief Upload a local file to FTP server
 *
 * Opens a FTP control connection, authenticates, enters passive mode,
 * sends STOR command, streams file data in 4KB chunks, and disconnects.
 *
 * @param host         FTP server hostname or IP
 * @param port         FTP server port (usually 21)
 * @param user         FTP username
 * @param pass         FTP password
 * @param remote_path  Remote file path on server
 * @param local_path   Local file path to upload
 * @return ESP_OK on success, ESP_FAIL on any error
 */
esp_err_t ftp_upload(const char *host, uint16_t port,
                     const char *user, const char *pass,
                     const char *remote_path, const char *local_path);

#endif // FTP_CLIENT_H
