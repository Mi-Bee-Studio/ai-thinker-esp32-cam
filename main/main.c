/*
 * MiBee Cam - Main Entry Point
 * 16-step boot sequence integrating all modules.
 *
 * Boot order (CRITICAL):
 *   NVS -> config -> LED -> SPIFFS -> SD bus -> WiFi -> callback -> health
 *   -> STA/AP -> MJPEG -> time_sync -> web_server -> motion -> SD -> timelapse
 *   -> recorder -> auto-record
 */

#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "common.h"
#include "config_manager.h"
#include "status_led.h"
#include "wifi_manager.h"
#include "camera_driver.h"
#include "health_monitor.h"
#include "mjpeg_streamer.h"
#include "time_sync.h"
#include "web_server.h"
#include "motion_detect.h"
#include "storage_manager.h"
#include "timelapse.h"
#include "video_recorder.h"
#include "serial_config.h"

static const char *TAG = "main";

/* Track what's been started (prevent double-init from WiFi callback) */
static bool s_mjpeg_started = false;
static bool s_web_server_started = false;
static bool s_motion_started = false;
static bool s_time_sync_started = false;
static bool s_sd_init_done = false;
static bool s_timelapse_started = false;

/* ---------------------------------------------------------------------------
 * WiFi state callback - triggers post-connection services
 * --------------------------------------------------------------------------- */
/* ------------------------------------------------------------------ */
/* STA services init task — runs in its own task context to avoid      */
/* stack overflow in the event callback. Camera init, MJPEG, web      */
/* server, motion detection all run here after WiFi STA connects.      */
/* ------------------------------------------------------------------ */
/* Background task to warm photo list cache after SD init */
static void storage_warm_cache_task(void *arg)
{
    storage_warm_cache();
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  Recording segment callback — enqueue for NAS upload                */
/* ------------------------------------------------------------------ */
static void on_segment_complete(const char *filepath, uint32_t size)
{
    ESP_LOGI(TAG, "Segment complete: %s (%u bytes)", filepath, size);
    storage_register_file(filepath, size);
    storage_cleanup();
}

static void sta_services_task(void *arg)
{
    ESP_LOGI(TAG, "STA services task started");

    /* Initialize camera AFTER WiFi STA start.
     * ESP32 known issue: WiFi STA start freezes camera I2S DMA.
     * Retry up to 3 times with delay - SCCB writes can fail intermittently.
     * See esp32-camera issue #620 (workaround for S3 only). */
    if (!camera_is_initialized()) {
        const cam_config_t *cam_cfg = config_get();
        for (int attempt = 1; attempt <= 3; attempt++) {
            esp_err_t cam_ret = camera_init(
                (camera_resolution_t)cam_cfg->resolution,
                cam_cfg->fps, cam_cfg->jpeg_quality);
            if (cam_ret == ESP_OK) {
                ESP_LOGI(TAG, "Camera initialized after WiFi connect (attempt %d)", attempt);
                break;
            }
            ESP_LOGW(TAG, "Camera init attempt %d failed: %s", attempt, esp_err_to_name(cam_ret));
            if (attempt < 3) {
                camera_deinit();
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }


    /* Start MJPEG streamer (once) */
    if (!s_mjpeg_started) {
        esp_err_t ret = mjpeg_streamer_init();
        if (ret == ESP_OK) {
            s_mjpeg_started = true;
            ESP_LOGI(TAG, "MJPEG streamer initialized");
        } else {
            ESP_LOGW(TAG, "MJPEG streamer init failed: %s", esp_err_to_name(ret));
        }
    }

    /* Start time sync (once) */
    if (!s_time_sync_started) {
        const cam_config_t *cfg = config_get();
        esp_err_t ret = time_sync_init(cfg->timezone[0] ? cfg->timezone : CONFIG_DEFAULT_TIMEZONE);
        if (ret == ESP_OK) {
            s_time_sync_started = true;
        } else {
            ESP_LOGW(TAG, "Time sync init failed: %s", esp_err_to_name(ret));
        }
    }

    /* Wait for NTP time sync before starting capture services.
     * Prevents 1970-timestamped photos from motion/timelapse.
     * Timeout after 30s — services start regardless (with fallback naming).
     */
    int sync_wait = 0;
    while (!time_sync_is_synced() && sync_wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sync_wait++;
    }
    if (time_sync_is_synced()) {
        ESP_LOGI(TAG, "Time synchronized after %d sec", sync_wait);
    } else {
        ESP_LOGW(TAG, "Time sync timeout (%d sec), capture will use fallback naming", sync_wait);
    }

    /* Start web server (once) */
    if (!s_web_server_started) {
        esp_err_t ret = web_server_start(80);
        if (ret == ESP_OK) {
            s_web_server_started = true;
            ESP_LOGI(TAG, "Web server started on port 80");
        } else {
            ESP_LOGE(TAG, "Web server start failed: %s", esp_err_to_name(ret));
        }
    }

    /* Start motion detection (once) */
    if (!s_motion_started) {
        esp_err_t ret = motion_detect_start();
        if (ret == ESP_OK) {
            s_motion_started = true;
            ESP_LOGI(TAG, "Motion detection started");
        } else {
            ESP_LOGE(TAG, "Motion detection start failed: %s", esp_err_to_name(ret));
        }
    }

    /* Initialize timelapse */
    if (!s_timelapse_started) {
        timelapse_init();
        const cam_config_t *cfg_tl = config_get();
        if (cfg_tl->timelapse_enabled) {
            esp_err_t ret = timelapse_start();
            if (ret == ESP_OK) {
                s_timelapse_started = true;
                ESP_LOGI(TAG, "Timelapse started (interval=%us, burst=%u)",
                         cfg_tl->timelapse_interval_s, cfg_tl->timelapse_burst_count);
            } else {
                ESP_LOGW(TAG, "Timelapse start failed: %s", esp_err_to_name(ret));
            }
        } else {
            s_timelapse_started = true;
            ESP_LOGI(TAG, "Timelapse disabled in config");
        }
    }


    /* ── Step 14/16: Video recorder init + cleanup + segment callback ── */
    if (camera_is_initialized() && storage_is_available()) {
        esp_err_t ret_rec = recorder_init();
        if (ret_rec == ESP_OK) {
            recorder_cleanup_incomplete();
            recorder_set_segment_cb(on_segment_complete);
            ESP_LOGI(TAG, "=== Step 14/16: Video recorder initialized ===");
        } else {
            ESP_LOGW(TAG, "Video recorder init failed: %s", esp_err_to_name(ret_rec));
        }
    } else {
        ESP_LOGW(TAG, "=== Step 14/16: Video recorder skipped (no camera or SD) ===");
    }

    /* ── Step 15/16: Auto-record ── */



    /* Auto-start recording if configured */
    if (camera_is_initialized() && storage_is_available()) {
        const cam_config_t *cfg_rec = config_get();
        if (cfg_rec->record_mode != 0) {
            esp_err_t ret_start = recorder_start();
            if (ret_start == ESP_OK) {
                ESP_LOGI(TAG, "Recording auto-started (mode=%u)", cfg_rec->record_mode);
            } else {
                ESP_LOGW(TAG, "Auto-start recording failed: %s", esp_err_to_name(ret_start));
            }
        }
    }

    /* System is fully up */
    led_set_status(LED_RUNNING);

    /* Task self-destructs after initialization */
    vTaskDelete(NULL);
}

static void wifi_state_cb(wifi_state_t state, void *user_data)
{
    switch (state) {
    case WIFI_STATE_STA_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_get_ip_str());

        /* Spawn a task for heavy initialization (camera, web server, etc.)
         * to avoid stack overflow in this event callback. */
        xTaskCreate(sta_services_task, "sta_init", 8192, NULL, 5, NULL);
        break;

    case WIFI_STATE_STA_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi disconnected");
        led_set_status(LED_WIFI_CONNECTING);
        break;

    case WIFI_STATE_AP:
        ESP_LOGI(TAG, "AP mode active - limited functionality");
        led_set_status(LED_AP_MODE);
        break;

    default:
        break;
    }
}


/* ---------------------------------------------------------------------------
 * app_main - system entry point, 16-step boot sequence
 * --------------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MiBee Cam");
    ESP_LOGI(TAG, "========================================");

    /* Step 1/19: NVS flash init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, formatting...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "=== Step 1/19: NVS initialized ===");

    /* NOTE: BOOT button factory reset DISABLED on MiBee Cam.
     * GPIO 0 is the camera XCLK pin and reads LOW even before camera init,
     * making it unreliable as a button input. Use POST /api/reset instead. */

    /* Step 2/19: Config manager init */
    ret = config_init();
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "=== Step 2/19: Config initialized ===");

    /* Start serial AT command listener (available immediately after config) */
    serial_config_init();

    /* Step 3/19: LED init */
    ret = led_init();
    ESP_ERROR_CHECK(ret);
    led_set_status(LED_STARTING);
    ESP_LOGI(TAG, "=== Step 3/19: LED initialized ===");

    /* Step 4/19: SPIFFS mount (/spiffs partition for Web UI) */
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info("spiffs", &total, &used);
        ESP_LOGI(TAG, "=== Step 4/19: SPIFFS mounted (%" PRIu32 " KB total, %" PRIu32 " KB used) ===",
                 total / 1024, used / 1024);
    }

    /* Step 5/19: Release SD SPI bus (must precede camera and WiFi)
     * Camera init DEFERRED to after WiFi connect (ESP32 DMA freeze fix).
     * WiFi STA start freezes camera I2S DMA - init camera AFTER WiFi. */
    camera_release_sd_bus();
    ESP_LOGI(TAG, "=== Step 5/19: SD SPI bus released (camera deferred to after WiFi) ===");

    /* Step 6/19: WiFi init (netif + event loop) */
    ret = wifi_init();
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "=== Step 6/19: WiFi subsystem initialized ===");

    /* Step 7/19: Register WiFi state change callback */
    wifi_register_callback(wifi_state_cb, NULL);
    ESP_LOGI(TAG, "=== Step 7/19: WiFi callback registered ===");

    /* Step 8/19: Health monitor init */
    ret = health_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Health monitor init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "=== Step 8/19: Health monitor initialized ===");
    }

    /* Step 9/19: WiFi mode selection (STA or AP) */
    const cam_config_t *cfg = config_get();
    bool has_wifi = cfg->wifi_ssid[0] != '\0' && cfg->wifi_pass[0] != '\0';

    if (has_wifi) {
        ESP_LOGI(TAG, "=== Step 9/19: Starting STA mode (SSID: %s) ===", cfg->wifi_ssid);
        ret = wifi_start_sta(cfg->wifi_ssid, cfg->wifi_pass);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi STA start failed: %s", esp_err_to_name(ret));
            led_set_status(LED_ERROR);
        }
        /* Remaining services (MJPEG, time_sync, web_server, motion)
         * are started by wifi_state_cb when WIFI_STATE_STA_CONNECTED fires. */
    } else {
        ESP_LOGI(TAG, "=== Step 9/19: No WiFi config, starting AP mode ===");
        ret = wifi_start_ap();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi AP start failed: %s", esp_err_to_name(ret));
            led_set_status(LED_ERROR);
            return;
        }

        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  AP Mode: SSID=%s  Pass=%s", CONFIG_DEFAULT_AP_SSID, CONFIG_DEFAULT_AP_PASS);
        ESP_LOGI(TAG, "  Config page: http://192.168.4.1");
        ESP_LOGI(TAG, "========================================");

        /* In AP mode: init camera (no DMA freeze risk without STA) */
        {
            const cam_config_t *cam_cfg = config_get();
            esp_err_t cam_ret = camera_init(
                (camera_resolution_t)cam_cfg->resolution,
                cam_cfg->fps, cam_cfg->jpeg_quality);
            if (cam_ret == ESP_OK) {
                ESP_LOGI(TAG, "Camera initialized (AP mode)");
            } else {
                ESP_LOGW(TAG, "Camera init failed in AP mode: %s", esp_err_to_name(cam_ret));
            }
        }

        ret = mjpeg_streamer_init();
        if (ret == ESP_OK) {
            s_mjpeg_started = true;
        }

        ret = web_server_start(80);
        if (ret == ESP_OK) {
            s_web_server_started = true;
            ESP_LOGI(TAG, "Web server started (AP mode)");
        }

        /* Initialize timelapse (AP mode) */
        if (!s_timelapse_started) {
            timelapse_init();
            const cam_config_t *cfg_tl = config_get();
            if (cfg_tl->timelapse_enabled) {
                esp_err_t tl_ret = timelapse_start();
                if (tl_ret == ESP_OK) {
                    s_timelapse_started = true;
                    ESP_LOGI(TAG, "Timelapse started (AP mode, interval=%us, burst=%u)",
                             cfg_tl->timelapse_interval_s, cfg_tl->timelapse_burst_count);
                } else {
                    ESP_LOGW(TAG, "Timelapse start failed (AP mode): %s", esp_err_to_name(tl_ret));
                }
            } else {
                s_timelapse_started = true;
                ESP_LOGI(TAG, "Timelapse disabled in config (AP mode)");
            }
        }
    }

    /* Step 10/19: MJPEG streamer init (STA mode - already done in AP branch above)
     * Note: In STA mode this is deferred to wifi_state_cb, but we log it here
     * for sequence tracking. The actual init happens on WiFi_STATE_STA_CONNECTED. */
    if (!s_mjpeg_started) {
        ESP_LOGI(TAG, "=== Step 10/19: MJPEG streamer deferred to WiFi connect ===");
    } else {
        ESP_LOGI(TAG, "=== Step 10/19: MJPEG streamer initialized ===");
    }

    /* Step 11/19: Time sync init (STA mode - deferred to wifi_state_cb) */
    if (!s_time_sync_started) {
        ESP_LOGI(TAG, "=== Step 11/19: Time sync deferred to WiFi connect ===");
    } else {
        ESP_LOGI(TAG, "=== Step 11/19: Time sync initialized ===");
    }

    /* Step 12/19: Web server start (STA mode - deferred to wifi_state_cb) */
    if (!s_web_server_started) {
        ESP_LOGI(TAG, "=== Step 12/19: Web server deferred to WiFi connect ===");
    } else {
        ESP_LOGI(TAG, "=== Step 12/19: Web server started ===");
    }

    /* Step 13/19: Motion detection start (STA mode - deferred to wifi_state_cb) */
    if (!s_motion_started) {
        ESP_LOGI(TAG, "=== Step 13/19: Motion detection deferred to WiFi connect ===");
    } else {
        ESP_LOGI(TAG, "=== Step 13/19: Motion detection started ===");
    }

    /* Step 14/19: SD card init - AFTER camera (GPIO14 conflict) */
    ret = storage_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card init failed: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "=== Step 14/19: SD card init failed ===");
    } else {
        s_sd_init_done = true;
        ESP_LOGI(TAG, "=== Step 14/19: SD card initialized ===");

        // Check /sdcard/config.txt for WiFi credentials
        esp_err_t sd_cfg_ret = config_load_from_sd();
        if (sd_cfg_ret == ESP_OK) {
            ESP_LOGI(TAG, "=== WiFi config loaded from SD card, reconnecting ===");
            wifi_stop();
            const cam_config_t *new_cfg = config_get();
            wifi_start_sta(new_cfg->wifi_ssid, new_cfg->wifi_pass);
        } else {
            ESP_LOGD(TAG, "=== SD config: no update ===");
        }

        /* Background cache warm-up so first /api/files call is fast */
        xTaskCreate(storage_warm_cache_task, "warm_cache", 4096, NULL, 1, NULL);
    }

    /* Step 15/19: NAS uploader removed */

    /* Step 16/19: Boot button factory reset disabled (GPIO0 = camera XCLK) */
    ESP_LOGI(TAG, "=== Step 16/19: BOOT button check disabled (GPIO0 = XCLK) ===");

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  System startup complete");
    ESP_LOGI(TAG, "========================================");
}
