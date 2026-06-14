#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Config version and magic
#define CONFIG_VERSION  9
#define CONFIG_MAGIC    0xA5B6C7D8

// Default values
#define CONFIG_DEFAULT_TIMEZONE     "CST-8"
#define CONFIG_DEFAULT_DEVICE_NAME  "MiBeeCam"
#define CONFIG_DEFAULT_AP_SSID      "MiBeeCam"
#define CONFIG_DEFAULT_AP_PASS      "12345678"

// Camera resolutions
typedef enum {
    CAMERA_RES_VGA  = 0,    // 640x480
    CAMERA_RES_SVGA = 1,    // 800x600
    CAMERA_RES_XGA  = 2,    // 1024x768
    CAMERA_RES_UXGA = 3,    // 1600x1200
    CAMERA_RES_MAX
} camera_resolution_t;

// WiFi states
typedef enum {
    WIFI_STATE_AP,
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_STA_DISCONNECTED,
} wifi_state_t;

// LED states
typedef enum {
    LED_STARTING,
    LED_WIFI_CONNECTING,
    LED_RUNNING,
    LED_ERROR,
    LED_AP_MODE,
} led_status_t;

// AI_Thinker ESP32-CAM pin mapping (ESP32, NOT ESP32-S3!)
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   (-1)
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26   // I2C data (SDA)
#define CAM_PIN_SIOC    27   // I2C clock (SCL)
#define CAM_PIN_D0      5
#define CAM_PIN_D1      18
#define CAM_PIN_D2      19
#define CAM_PIN_D3      21
#define CAM_PIN_D4      36   // Input only
#define CAM_PIN_D5      39   // Input only
#define CAM_PIN_D6      34   // Input only
#define CAM_PIN_D7      35   // Input only
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// SD card SPI pins
#define SD_PIN_CS       13
#define SD_PIN_CLK      14
#define SD_PIN_MOSI     15
#define SD_PIN_MISO     2

// LED pins
#define LED_PIN_STATUS  33   // Active LOW
#define LED_PIN_FLASH   4    // Flash LED (PWM)

// Boot button
#define BOOT_BTN_GPIO   0

// Configuration struct (stored in NVS)
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char device_name[33];
    uint8_t resolution;
    uint8_t fps;
    uint8_t jpeg_quality;
    char web_password[33];
    char timezone[33];
    uint8_t motion_threshold;
    uint8_t motion_cooldown;
    uint8_t vflip;
    uint8_t motion_saved_threshold;
    uint8_t wifi_tx_power;      /* TX power in 0.25dBm units (80=20dBm) */
    uint8_t wifi_power_save;    /* 0=WIFI_PS_NONE, 1=WIFI_PS_MIN_MODEM */
    uint8_t flash_threshold;     /* Motion detection brightness threshold */
    uint8_t timelapse_enabled;
    uint16_t timelapse_interval_s;
    uint8_t timelapse_burst_count;
    uint8_t  timelapse_mode;          /* 0=static, 1=dynamic */
    uint16_t timelapse_min_interval_s; /* min interval in seconds (dynamic mode), default 3 */
    uint16_t timelapse_max_interval_s; /* max interval in seconds (dynamic mode), default 300 */
    uint8_t  timelapse_decay_factor;   /* interval multiplier per decay period, default 2 */
    uint16_t timelapse_decay_period_s; /* seconds of no motion before decay step, default 10 */
    uint8_t  record_mode;              /* 0=continuous, 1=timelapse, 2=dynamic timelapse */
    uint16_t record_segment_sec;      /* segment duration in seconds (default 300) */
    uint8_t  frame_drop_enabled;      /* smart frame drop (default 1=enabled) */
    uint8_t  webdav_enabled;          /* NAS upload enabled (default 0) */
    char     webdav_url[129];         /* WebDAV server URL */
    char     webdav_user[33];         /* WebDAV username */
    char     webdav_pass[65];         /* WebDAV password */
    char     upload_base_path[65];    /* remote base path (default "/mibee-cam") */
    uint8_t  alert_webhook_enabled;   /* webhook enabled (default 0) */
    char     alert_webhook_url[257];  /* webhook URL (default empty) */
    uint8_t  cleanup_low_pct;         /* start cleanup when used% > this (default 80) */
    uint8_t  cleanup_high_pct;        /* stop cleanup when used% < this (default 70) */
    /* V9: Dual WiFi (secondary fallback network) */
    char     wifi_ssid_2[33];         /* secondary WiFi SSID (empty = disabled) */
    char     wifi_pass_2[65];         /* secondary WiFi password */
    uint8_t  allow_ap_fallback;       /* 1=fall back to AP mode if STA fails, 0=keep retrying */
    uint32_t magic;
    uint32_t version;
} cam_config_t;

#endif // COMMON_H
