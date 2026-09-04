#include "serial_config.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config_manager.h"
#include "common.h"
#include "camera_driver.h"
#include "wifi_manager.h"
#include "esp_timer.h"
#include "esp_app_desc.h"

static const char *TAG = "serial_cfg";

/* ── AT command parser ────────────────────────────────────────────── */
static void process_at_command(char *line)
{
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') return;

    /* AT — basic handshake */
    if (strcmp(line, "AT") == 0) {
        printf("OK\r\n");
        return;
    }

    /* ── 家族核心指令（docs/at-command.md v1.0，2026-09-04 统一） ── */

    /* AT+WIFI? — query (不回显密码) */
    if (strcmp(line, "AT+WIFI?") == 0) {
        bool conn = (wifi_get_state() == WIFI_STATE_STA_CONNECTED);
        const cam_config_t *c = config_get();
        printf("State: %s\r\nSSID: %s\r\nIP: %s\r\n",
               conn ? "connected" : "disconnected",
               c->wifi_ssid[0] ? c->wifi_ssid : "(not set)",
               conn ? wifi_get_ip_str() : "-");
        printf("OK\r\n");
        return;
    }

    /* AT+GMR — version/chip/board */
    if (strcmp(line, "AT+GMR") == 0) {
        const esp_app_desc_t *app = esp_app_get_description();
        printf("MiBee Cam (ai-thinker ESP32 + OV2640)\r\n"
               "Firmware: %s %s\r\nESP-IDF: %s\r\n",
               app->project_name, app->version, app->idf_ver);
        printf("OK\r\n");
        return;
    }

    /* AT+STATUS — consolidated status */
    if (strcmp(line, "AT+STATUS") == 0) {
        const cam_config_t *c = config_get();
        bool conn = (wifi_get_state() == WIFI_STATE_STA_CONNECTED);
        static const char *res_names[] = {"VGA", "SVGA", "XGA", "UXGA"};
        printf("Board:      AI-Thinker ESP32-CAM (OV2640)\r\n"
               "Uptime:     %lld s\r\n"
               "Heap:       %lu bytes\r\n"
               "WiFi:       %s\r\n"
               "SSID:       %s\r\n"
               "IP:         %s\r\n"
               "Camera:     res=%s quality=%u [%d-%d]\r\n",
               (long long)(esp_timer_get_time() / 1000000),
               (unsigned long)esp_get_free_heap_size(),
               conn ? "STA connected" : "disconnected",
               c->wifi_ssid[0] ? c->wifi_ssid : "(none)",
               conn ? wifi_get_ip_str() : "-",
               (c->cam_framesize < 4) ? res_names[c->cam_framesize] : "?",
               c->cam_quality, CAMERA_QUALITY_MIN, CAMERA_QUALITY_MAX);
        printf("OK\r\n");
        return;
    }

    /* AT+CAMRES? / AT+CAMRES=n (热重配 — 与 web 同路径，三层上限) */
    if (strcmp(line, "AT+CAMRES?") == 0 || strcmp(line, "AT+CAMRES") == 0) {
        static const char *res_names[] = {"VGA", "SVGA", "XGA", "UXGA"};
        const cam_config_t *c = config_get();
        printf("Resolution: %s  supported: 0-%d (cap source: %s)\r\n",
               (c->cam_framesize < 4) ? res_names[c->cam_framesize] : "?",
               (int)camera_get_effective_max_res(), camera_res_cap_source());
        printf("OK\r\n");
        return;
    }
    if (strncmp(line, "AT+CAMRES=", 10) == 0) {
        int n = atoi(line + 10);
        if (n < 0 || n > (int)camera_get_effective_max_res()) {
            printf("ERROR: resolution must be 0-%d (cap source: %s)\r\n",
                   (int)camera_get_effective_max_res(), camera_res_cap_source());
            return;
        }
        config_set_resolution((camera_resolution_t)n);
        const cam_config_t *now = config_get();
        esp_err_t ret = camera_apply_settings(now->cam_framesize, now->fps, now->cam_quality);
        if (ret != ESP_OK) {
            printf("ERROR: camera apply failed (%s) — saved, applies next boot\r\n",
                   esp_err_to_name(ret));
            return;
        }
        printf("OK — resolution set to %d (applied)\r\n", n);
        return;
    }

    /* AT+CAMQUAL? / AT+CAMQUAL=n (10-63，热重配) */
    if (strcmp(line, "AT+CAMQUAL?") == 0 || strcmp(line, "AT+CAMQUAL") == 0) {
        const cam_config_t *c = config_get();
        printf("Quality: %u  range: [%d-%d]\r\n",
               c->cam_quality, CAMERA_QUALITY_MIN, CAMERA_QUALITY_MAX);
        printf("OK\r\n");
        return;
    }
    if (strncmp(line, "AT+CAMQUAL=", 11) == 0) {
        int n = atoi(line + 11);
        if (n < CAMERA_QUALITY_MIN || n > CAMERA_QUALITY_MAX) {
            printf("ERROR: quality must be %d-%d\r\n",
                   CAMERA_QUALITY_MIN, CAMERA_QUALITY_MAX);
            return;
        }
        config_set_jpeg_quality((uint8_t)n);
        const cam_config_t *now = config_get();
        esp_err_t ret = camera_apply_settings(now->cam_framesize, now->fps, now->cam_quality);
        if (ret != ESP_OK) {
            printf("ERROR: camera apply failed (%s) — saved, applies next boot\r\n",
                   esp_err_to_name(ret));
            return;
        }
        printf("OK — quality set to %d (applied)\r\n", n);
        return;
    }

    /* AT+RESTORE — factory reset (契约核心名；AT+RESET 为历史别名) */
    if (strcmp(line, "AT+RESTORE") == 0) {
        printf("OK — factory reset, rebooting...\r\n");
        fflush(stdout);
        config_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }

    /* AT+WIFI=ssid,password */
    if (strncmp(line, "AT+WIFI=", 8) == 0) {
        char *ssid = line + 8;
        char *comma = strchr(ssid, ',');
        if (!comma) {
            printf("ERROR: Usage: AT+WIFI=ssid,password\r\n");
            return;
        }
        *comma = '\0';
        char *pass = comma + 1;
        if (strlen(ssid) == 0) {
            printf("ERROR: SSID empty\r\n");
            return;
        }
        esp_err_t ret = config_set_wifi(ssid, pass);
        if (ret == ESP_OK) {
            printf("OK — WiFi set: SSID='%s', rebooting...\r\n", ssid);
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else {
            printf("ERROR: %s\r\n", esp_err_to_name(ret));
        }
        return;
    }

    /* AT+DEVICE=name */
    if (strncmp(line, "AT+DEVICE=", 10) == 0) {
        char *name = line + 10;
        if (strlen(name) == 0 || strlen(name) >= 32) {
            printf("ERROR: name too long or empty\r\n");
            return;
        }
        config_set_device_name(name);
        printf("OK — device name set: '%s'\r\n", name);
        return;
    }

    /* AT+TZ=timezone */
    if (strncmp(line, "AT+TZ=", 6) == 0) {
        char *tz = line + 6;
        config_set_timezone(tz);
        printf("OK — timezone set: '%s'\r\n", tz);
        return;
    }

    /* AT+CONFIG — print current config */
    if (strcmp(line, "AT+CONFIG") == 0) {
        const cam_config_t *c = config_get();
        printf("{\"device\":\"%s\",\"ssid\":\"%s\",\"pass_set\":%s,"
               "\"resolution\":%d,\"fps\":%d,\"quality\":%d,"
               "\"motion_threshold\":%d,\"record_mode\":%d,"
               "\"timezone\":\"%s\",\"version\":%lu}"
               "\r\nOK\r\n",
               c->device_name, c->wifi_ssid, c->wifi_pass[0] ? "true" : "false",
               c->cam_framesize, c->fps, c->cam_quality,
               c->motion_threshold, c->record_mode,
               c->timezone, (unsigned long)c->version);
        return;
    }

    /* AT+RESET — factory reset */
    if (strcmp(line, "AT+RESET") == 0) {
        printf("OK — factory reset, rebooting...\r\n");
        fflush(stdout);
        config_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }

    /* AT+REBOOT */
    if (strcmp(line, "AT+REBOOT") == 0) {
        printf("OK — rebooting...\r\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }

    /* AT+IP? */
    if (strcmp(line, "AT+IP?") == 0 || strcmp(line, "AT+IP") == 0) {
        bool conn = (wifi_get_state() == WIFI_STATE_STA_CONNECTED);
        printf("IP: %s\r\n", conn ? wifi_get_ip_str() : "-");
        printf("OK\r\n");
        return;
    }

    /* AT+HELP */
    if (strcmp(line, "AT+HELP") == 0) {
        printf("\r\nCommands (family contract v1.0):\r\n"
               "  AT                       handshake\r\n"
               "  AT+HELP                  this message\r\n"
               "  AT+GMR                   firmware/board version\r\n"
               "  AT+STATUS                consolidated status\r\n"
               "  AT+WIFI?                 WiFi state (no password)\r\n"
               "  AT+WIFI=ssid,password    set WiFi, save, reboot\r\n"
               "  AT+IP?                   IP address\r\n"
               "  AT+CAMRES? | AT+CAMRES=n resolution 0-3 (hot)\r\n"
               "  AT+CAMQUAL? | AT+CAMQUAL=n quality 10-63 (hot)\r\n"
               "  AT+REBOOT                reboot\r\n"
               "  AT+RESTORE | AT+RESET    factory reset + reboot\r\n"
               "  AT+DEVICE=name           set device name (legacy)\r\n"
               "  AT+TZ=CST-8              set timezone (legacy)\r\n"
               "  AT+CONFIG                print config JSON (legacy)\r\n"
               "OK\r\n");
        return;
    }

    printf("ERROR: unknown. Type AT+HELP\r\n");
}

/* ── Background task: read lines from UART0 via VFS ───────────────── */
static void serial_config_task(void *arg)
{
    char line[256];

    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Ready. Type AT+HELP for commands.");

    while (1) {
        /* fgets blocks until a complete line arrives on stdin */
        if (fgets(line, sizeof(line), stdin) != NULL) {
            process_at_command(line);
            fflush(stdout);
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────── */
esp_err_t serial_config_init(void)
{
    /* Install UART driver on UART0 and connect VFS so fgets/getchar work */
    if (!uart_is_driver_installed(UART_NUM_0)) {
        uart_driver_install(UART_NUM_0, 512, 512, 0, NULL, 0);
    }
    /* Switch VFS from ROM (output-only) to UART driver (bidirectional) */
    uart_vfs_dev_use_driver(UART_NUM_0);

    xTaskCreatePinnedToCore(serial_config_task, "serial_cfg", 4096, NULL, 2, NULL, 0);
    return ESP_OK;
}
