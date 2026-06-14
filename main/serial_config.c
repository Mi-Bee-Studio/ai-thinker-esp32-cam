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
               "timezone":"%s","version":%lu}\
OK\r\n",
               c->device_name, c->wifi_ssid, c->wifi_pass[0] ? "true" : "false",
               c->resolution, c->fps, c->jpeg_quality,
               c->motion_threshold, c->record_mode,
               c->timezone, (unsigned long)c->version);
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

    /* AT+HELP */
    if (strcmp(line, "AT+HELP") == 0) {
        printf("\r\nCommands:\r\n"
               "  AT                       handshake\r\n"
               "  AT+WIFI=ssid,password    set WiFi, save, reboot\r\n"
               "  AT+DEVICE=name           set device name\r\n"
               "  AT+TZ=CST-8              set timezone\r\n"
               "  AT+CONFIG                print config JSON\r\n"
               "  AT+RESET                 factory reset + reboot\r\n"
               "  AT+REBOOT                reboot\r\n"
               "  AT+HELP                  this message\r\n"
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
