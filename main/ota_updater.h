/*
 * OTA Firmware Update Module - Browser-based firmware upload
 *
 * Provides REST endpoints for uploading a new firmware binary via HTTP,
 * streaming it to the inactive OTA partition, and rebooting into it.
 * Uses A/B partition scheme with bootloader rollback support.
 */

#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief GET /api/ota/info handler
 *
 * Returns JSON with running partition, next (target) partition,
 * max image size, firmware version, and rollback status.
 */
esp_err_t handler_api_ota_info(httpd_req_t *req);

/**
 * @brief POST /api/ota/upload handler
 *
 * Accepts a raw application/octet-stream body (the .bin firmware image),
 * streams it to the inactive OTA partition, validates, sets boot partition,
 * and reboots.
 *
 * Requires X-Password header for authentication.
 */
esp_err_t handler_api_ota_upload(httpd_req_t *req);

/**
 * @brief POST /api/ota/spiffs handler
 *
 * Accepts a raw SPIFFS image binary, erases and writes it to the SPIFFS
 * partition, then reboots. Used for updating the Web UI without serial flash.
 *
 * Requires X-Password header for authentication.
 */
esp_err_t handler_api_spiffs_upload(httpd_req_t *req);

/**
 * @brief POST /api/ota handler (URL-triggered OTA, contract v1.3)
 *
 * Accepts {"url":"http://..."} and pulls the firmware image over HTTP,
 * writes it to the inactive OTA partition and reboots. Only http:// URLs
 * (LAN distribution). Same semantics as n16r8/seeed.
 *
 * Requires X-Password header for authentication.
 */
esp_err_t handler_api_ota_url(httpd_req_t *req);

#endif /* OTA_UPDATER_H */
