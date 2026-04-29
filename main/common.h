#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Config version and magic
#define CONFIG_VERSION  1
#define CONFIG_MAGIC    0xA5B6C7D8

// Default values
#define CONFIG_DEFAULT_TIMEZONE     "CST-8"
#define CONFIG_DEFAULT_DEVICE_NAME  "ai-thinker-cam"
#define CONFIG_DEFAULT_AP_SSID      "ai-thinker-cam"
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

// NAS upload protocols
typedef enum {
    NAS_PROTOCOL_HTTP = 0,
    NAS_PROTOCOL_FTP  = 1,
    NAS_PROTOCOL_WEBDAV = 2,
} nas_protocol_t;

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
    // NAS upload settings
    nas_protocol_t nas_protocol;
    char nas_host[65];
    uint16_t nas_port;
    char nas_user[33];
    char nas_pass[65];
    char nas_path[65];
    uint32_t magic;
    uint32_t version;
} cam_config_t;

#endif // COMMON_H
