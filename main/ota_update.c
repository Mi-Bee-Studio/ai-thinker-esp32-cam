/**
 * @file ota_update.c
 * @brief OTA firmware update via browser upload.
 *
 * Implements streaming firmware upload to the inactive OTA partition.
 * The upload handler receives the binary in chunks via httpd_req_recv()
 * and writes each chunk immediately to flash via esp_ota_write(),
 * so the full image is never buffered in RAM.
 *
 * Before writing, ota_quiesce_system() stops the camera and all dependent
 * tasks to prevent flash/DMA conflicts during erase and write operations.
 * After a successful upload + validation, the boot partition is switched
 * and the device reboots into the new firmware.
 */

#include "ota_update.h"
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "ota_update";

/* ---------- Handler stubs (filled in Commit 4) ---------- */

esp_err_t handler_api_ota_info(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":false,\"error\":\"OTA not yet implemented\"}",
                    HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "ota_info: stub handler called");
    return ESP_OK;
}

esp_err_t handler_api_ota_upload(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":false,\"error\":\"OTA not yet implemented\"}",
                    HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "ota_upload: stub handler called");
    return ESP_OK;
}
