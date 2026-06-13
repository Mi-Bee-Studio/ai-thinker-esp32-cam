/*
 * Copyright (C) 2024 MiBee Cam Authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "webhook.h"
#include "config_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "webhook";

/* ------------------------------------------------------------------ */
/*  webhook_init                                                       */
/* ------------------------------------------------------------------ */

esp_err_t webhook_init(void)
{
    ESP_LOGI(TAG, "Webhook module initialized");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void webhook_task(void *pvParameters)
{
    const char *event_type = ((const char **)pvParameters)[0];
    const char *message = ((const char **)pvParameters)[1];
    
    const cam_config_t *cfg = config_get();
    if (!cfg->alert_webhook_enabled || strlen(cfg->alert_webhook_url) == 0) {
        ESP_LOGD(TAG, "Webhook disabled or no URL configured");
        free((void *)event_type);
        free((void *)message);
        vTaskDelete(NULL);
        return;
    }
    
    /* Build ISO 8601 timestamp */
    time_t now_sec = time(NULL);
    struct tm tm_info;
    localtime_r(&now_sec, &tm_info);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_info);
    
    /* Build JSON payload */
    char body[512];
    snprintf(body, sizeof(body),
             "{\"type\":\"%s\",\"message\":\"%s\",\"ts\":\"%s\"}",
             event_type, message, ts);
    
    esp_err_t ret = ESP_FAIL;
    
    esp_http_client_config_t http_cfg = {
        .url = cfg->alert_webhook_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGW(TAG, "Failed to create HTTP client");
        free((void *)event_type);
        free((void *)message);
        vTaskDelete(NULL);
        return;
    }
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));
    
    ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (ret == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "Alert sent: %s (%s) -> %d", event_type, message, status);
    } else {
        ESP_LOGW(TAG, "Alert failed: %s (HTTP %d)", esp_err_to_name(ret), status);
    }
    
    free((void *)event_type);
    free((void *)message);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  webhook_send_alert                                                 */
/* ------------------------------------------------------------------ */

esp_err_t webhook_send_alert(const char *event_type, const char *message)
{
    if (!event_type || !message) return ESP_ERR_INVALID_ARG;
    if (strlen(event_type) > 64 || strlen(message) > 256) return ESP_ERR_INVALID_ARG;
    
    /* Validate event type */
    const char *valid_events[] = {
        "motion_detected", "recording_started", "recording_stopped",
        "recording_segment", "nas_upload_success", "nas_upload_failure",
        "sd_card_removed", "sd_card_inserted"
    };
    bool valid = false;
    for (int i = 0; i < 8; i++) {
        if (strcmp(event_type, valid_events[i]) == 0) {
            valid = true;
            break;
        }
    }
    if (!valid) {
        ESP_LOGE(TAG, "Invalid event type: %s", event_type);
        return ESP_ERR_INVALID_ARG;
    }
    
    const cam_config_t *cfg = config_get();
    if (!cfg->alert_webhook_enabled || strlen(cfg->alert_webhook_url) == 0) {
        return ESP_OK; /* disabled or no URL configured */
    }
    
    /* Spawn task for non-blocking operation */
    char **params = malloc(2 * sizeof(char *));
    if (!params) {
        ESP_LOGE(TAG, "Failed to allocate params for webhook task");
        return ESP_ERR_NO_MEM;
    }
    params[0] = strdup(event_type);
    params[1] = strdup(message);
    if (!params[0] || !params[1]) {
        ESP_LOGE(TAG, "Failed to allocate strings for webhook task");
        free(params[0]);
        free(params[1]);
        free(params);
        return ESP_ERR_NO_MEM;
    }
    
    BaseType_t ret = xTaskCreate(webhook_task, "webhook", 4096, params, tskIDLE_PRIORITY + 1, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create webhook task");
        free(params[0]);
        free(params[1]);
        free(params);
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}
