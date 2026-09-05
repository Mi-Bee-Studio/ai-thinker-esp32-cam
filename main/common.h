#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"

/* 家族配置契约 v1.0（docs/config-contract.md）：持久化为逐键 NVS（mibee_cfg
 * 命名空间 + schema_ver 版本键），不再使用 blob+magic。legacy blob（V16 及
 * 更早）由 config_manager.c 的一次性迁移函数翻译。 */
#define CONFIG_SCHEMA_VERSION 1

// Default values
#define CONFIG_DEFAULT_TIMEZONE     "CST-8"
/* 契约 v1.1：家族统一默认管理密码（公开默认 mibeecam2026，本地可在 gitignored sdkconfig 覆盖） */
#define CONFIG_DEFAULT_WEB_PASSWORD CONFIG_MIBEE_CAM_DEFAULT_WEB_PASSWORD
#define CONFIG_DEFAULT_DEVICE_NAME  "MiBeeCam"
#define CONFIG_DEFAULT_AP_SSID      "MiBeeCam"
#define CONFIG_DEFAULT_AP_PASS      "mibeecam2026"

/* Camera resolutions — 家族统一刻度 = esp32-camera framesize_t（契约 v1.3 §5：
 * VGA=10, SVGA=11, XGA=12, HD=13, SXGA=14, UXGA=15）。旧自有刻度 0-3 由
 * config 迁移函数翻译（0→10, 1→11, 2→12, 3→15）。 */
typedef enum {
    CAMERA_RES_VGA  = 10,   // FRAMESIZE_VGA  640x480
    CAMERA_RES_SVGA = 11,   // FRAMESIZE_SVGA 800x600
    CAMERA_RES_XGA  = 12,   // FRAMESIZE_XGA  1024x768
    CAMERA_RES_HD   = 13,   // FRAMESIZE_HD   1280x720（OV2640 支持，本板未入可选表）
    CAMERA_RES_SXGA = 14,   // FRAMESIZE_SXGA 1280x1024（同上）
    CAMERA_RES_UXGA = 15,   // FRAMESIZE_UXGA 1600x1200
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

/* Configuration struct（内存模型；持久化为 mibee_cfg 逐键 NVS，契约 v1.0）
 * 字段命名与 docs/config-contract.md §3 对齐；webdav 系列与 alert_webhook
 * 系列字段已随 NAS 上传器/告警钩子的移除从本板删除（"不适用即省略"）。 */
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char device_name[33];
    uint8_t cam_framesize;             /* framesize_t 刻度（10-15，见上） */
    uint8_t cam_fps;                   /* 1-30，default 15 */
    uint8_t cam_quality;               /* 10-63（PIT-021），default 12 */
    char web_password[33];
    char timezone[33];
    /* 家族 motion 超集模型（契约 §3.2）：sensitivity 越大越灵敏；
     * 旧 motion_threshold 迁移为 sensitivity = 100 - threshold */
    uint8_t motion_enabled;            /* default 1 */
    uint8_t motion_sensitivity;        /* 0-100，default 70（=旧 threshold 30） */
    uint16_t motion_cooldown_s;        /* 1-300，两次触发最小间隔，default 10 */
    uint8_t motion_active_interval_s;  /* 1-30，持续活动期再触发间隔，default 5 */
    uint8_t cam_vflip;
    uint8_t wifi_tx_power;      /* TX power in 0.25dBm units (80=20dBm) */
    uint8_t wifi_power_save;    /* 0=WIFI_PS_NONE, 1=WIFI_PS_MIN_MODEM */
    uint8_t flash_threshold;     /* 板级扩展：侦测联动闪光灯阈值 */
    uint8_t timelapse_enabled;
    uint16_t timelapse_interval_s;
    uint8_t timelapse_burst_count;
    uint8_t  timelapse_mode;          /* 0=static, 1=dynamic */
    uint16_t timelapse_min_interval_s; /* min interval in seconds (dynamic mode), default 3 */
    uint16_t timelapse_max_interval_s; /* max interval in seconds (dynamic mode), default 300 */
    uint8_t  timelapse_decay_factor;   /* interval multiplier per decay period, default 2 */
    uint16_t timelapse_decay_period_s; /* seconds of no motion before decay step, default 10 */
    uint8_t  record_mode;              /* 0=continuous, 1=timelapse, 2=dynamic timelapse */
    uint16_t segment_sec;              /* 录像分段秒数（default 300） */
    uint8_t  frame_drop_enabled;      /* smart frame drop (default 1=enabled) */
    uint8_t  cleanup_low_pct;         /* 空闲% < low 触发清理（default 20，契约 v1.2 §11.6） */
    uint8_t  cleanup_high_pct;        /* 清理到空闲% >= high 停止（default 30） */
    /* Dual WiFi (secondary fallback network) */
    char     wifi_ssid_2[33];         /* secondary WiFi SSID (empty = disabled) */
    char     wifi_pass_2[65];         /* secondary WiFi password */
    uint8_t  allow_ap_fallback;       /* 1=fall back to AP mode if STA fails, 0=keep retrying */
    /* SD card */
    uint8_t  save_to_sd;              /* 1=save photos/recordings (default), 0=NVR-only no writes */
    uint8_t  sd_log_enabled;          /* 1=log severe errors+WiFi anomalies to SD (default), 0=disabled */
    uint16_t wifi_reconnect_hours;   /* periodic WiFi reconnect interval (0=disabled, default 24) */
    /* Camera XCLK frequency (10/16/20 MHz, 20=default, lower=more stable for clone modules) */
    uint8_t  xclk_freq_mhz;          /* Camera master clock in MHz (default 20) */
    /* WiFi RSSI-based roaming (switch to stronger AP across SSIDs) */
    int8_t  wifi_roam_rssi;          /* scan for better AP when RSSI below this (0=off, default -65) */
    uint8_t wifi_roam_gap_s;         /* min RSSI gap (dBm) to trigger switch (5-15, default 10) */
    /* ONVIF 开关（契约核心字段；本板默认 1=始终开启，行为不变） */
    uint8_t onvif_enable;            /* default 1，重启生效 */
} cam_config_t;

#endif // COMMON_H
