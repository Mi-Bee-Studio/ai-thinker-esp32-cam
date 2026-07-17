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
#include "esp_system.h"
#include "web_server.h"
#include "config_manager.h"
#include "motion_detect.h"
#include "timelapse.h"
#include "video_recorder.h"
#include "mjpeg_streamer.h"
#include "frame_broker.h"
#include "camera_driver.h"
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

/* ---------- System quiesce for OTA ---------- */

static void ota_quiesce_system(void)
{
    ESP_LOGI(TAG, "Quiescing system for OTA flash write...");

    /* Stop in dependency order: consumers first, then producer, then HW.
     * Prevents DMA/flash conflicts during esp_ota_write(). */
    motion_detect_stop();
    timelapse_stop();
    recorder_stop();
    mjpeg_streamer_stop();
    frame_broker_stop();
    camera_deinit();

    ESP_LOGI(TAG, "System quiesced \u2014 camera and all dependents stopped");
}

/* ---------- POST /api/ota/upload ---------- */

esp_err_t handler_api_ota_upload(httpd_req_t *req)
{
    /* Auth */
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Unauthorized\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    size_t content_len = req->content_len;
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    if (!next) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"No OTA partition\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (content_len == 0 || content_len > next->size) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char err[128];
        snprintf(err, sizeof(err),
                 "{\"ok\":false,\"error\":\"Invalid size %u (max %u)\"}",
                 (unsigned)content_len, (unsigned)next->size);
        httpd_resp_send(req, err, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA upload: %u bytes \u2192 partition %s",
             (unsigned)content_len, next->label);

    /* Stop camera + all dependents to prevent flash/DMA conflicts */
    ota_quiesce_system();

    /* Begin OTA write */
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(next, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"OTA begin failed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Stream body to flash in 4KB chunks */
    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        ESP_LOGE(TAG, "OTA buffer alloc failed");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Out of memory\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    size_t remaining = content_len;
    size_t written   = 0;
    bool   ok        = true;

    while (remaining > 0) {
        int to_recv = (remaining < 4096) ? (int)remaining : 4096;
        int recv_len = httpd_req_recv(req, buf, to_recv);
        if (recv_len < 0) {
            ESP_LOGE(TAG, "httpd_req_recv failed: %d", recv_len);
            ok = false;
            break;
        }
        if (recv_len == 0) {
            ESP_LOGE(TAG, "Client disconnected during upload");
            ok = false;
            break;
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            ok = false;
            break;
        }

        written   += recv_len;
        remaining -= recv_len;

        if ((written % 65536) < 4096) {
            ESP_LOGI(TAG, "OTA progress: %u/%u bytes (%d%%)",
                     (unsigned)written, (unsigned)content_len,
                     (int)(written * 100 / content_len));
        }
    }

    free(buf);

    if (!ok || written != content_len) {
        esp_ota_abort(ota_handle);
        ESP_LOGE(TAG, "OTA failed: wrote %u/%u bytes",
                 (unsigned)written, (unsigned)content_len);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Upload failed or truncated\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Finalize and validate firmware hash */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char err2[192];
        snprintf(err2, sizeof(err2),
                 "{\"ok\":false,\"error\":\"Validation failed: %s\"}",
                 esp_err_to_name(err));
        httpd_resp_send(req, err2, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Boot partition set failed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA complete: %u bytes validated, boot \u2192 %s",
             (unsigned)written, next->label);

    /* Send success response */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[128];
    int resp_len = snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"data\":{\"bytes_written\":%u,\"next_partition\":\"%s\"}}",
             (unsigned)written, next->label);
    httpd_resp_send(req, resp, resp_len);

    /* Reboot after short delay to let TCP flush */
    ESP_LOGI(TAG, "Rebooting into new firmware in 1s...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK; /* unreachable */
}
