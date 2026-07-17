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
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ota_update";

/* ---------- GET /api/ota/info ---------- */

esp_err_t handler_api_ota_info(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t  *desc    = esp_app_get_description();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char json[300];
    int len = snprintf(json, sizeof(json),
             "{\"ok\":true,\"data\":{"
             "\"firmware_version\":\"%s\","
             "\"running_partition\":\"%s\","
             "\"next_partition\":\"%s\","
             "\"max_image_size\":%u,"
             "\"rollback_enabled\":true"
             "}}",
             desc->version,
             running ? running->label : "unknown",
             next    ? next->label    : "unknown",
             next    ? (unsigned)next->size : 0);

    return httpd_resp_send(req, json, len);
}

/* ---------- POST /api/ota/upload (stub — filled in Commit 4) ---------- */

esp_err_t handler_api_ota_upload(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"ok\":false,\"error\":\"OTA upload not yet implemented\"}",
                    HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "ota_upload: stub handler called");
    return ESP_OK;
}
