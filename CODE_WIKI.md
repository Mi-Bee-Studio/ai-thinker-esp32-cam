# AI_Thinker ESP32-CAM 固件 Code Wiki

## 目录
- [项目概述](#项目概述)
- [系统架构](#系统架构)
- [启动序列](#启动序列)
- [主要模块说明](#主要模块说明)
- [关键函数与API](#关键函数与api)
- [依赖关系](#依赖关系)
- [配置管理](#配置管理)
- [项目运行方式](#项目运行方式)

---

## 项目概述

### 基本信息
- **项目名称**: AI_Thinker ESP32-CAM 固件
- **硬件平台**: AI_Thinker ESP32-CAM 开发板
- **芯片**: ESP32
- **摄像头**: OV2640
- **固件版本**: v1.0
- **主要特性**: 摄像头捕获、MJPEG 流、运动检测、NAS 上传、Web 管理界面、Prometheus 监控

### 硬件需求
| 组件 | 规格 |
|------|------|
| Flash | 4 MB |
| PSRAM | 4 MB (必需) |
| 摄像头 | OV2640 |
| 存储 | TF 卡 (可选) |

---

## 系统架构

### 分层架构
```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Application Layer)                 │
├─────────────────────────────────────────────────────────────┤
│  Web Server    │ MJPEG Streamer │ Motion Detect │ NAS Uploader│
│  (HTTP + API)  │   (Streaming)  │   (Capture)  │   (Upload)  │
├─────────────────────────────────────────────────────────────┤
│                    硬件接口层 (Hardware Interface Layer)      │
├─────────────────────────────────────────────────────────────┤
│  Camera Driver │  Storage Mgr  │   WiFi Mgr   │  Time Sync  │
│    (OV2640)     │  (SD Card)    │ (AP/STA)     │   (NTP)     │
├─────────────────────────────────────────────────────────────┤
│                    系统核心层 (System Core Layer)             │
├─────────────────────────────────────────────────────────────┤
│  Config Manager │ Health Monitor │ Status LED │     Main     │
│    (NVS)        │   (Metrics)   │   (GPIO)   │ (Boot Seq)  │
├─────────────────────────────────────────────────────────────┤
│                  ESP-IDF 框架                                 │
├─────────────────────────────────────────────────────────────┤
│  FreeRTOS      │ LWIP          │ SPIFFS      │ NVS         │
│  (Tasks/RTOS)  │  (Networking) │ (Storage)   │ (Config)    │
└─────────────────────────────────────────────────────────────┘
```

### 模块概览
项目共包含 **14 个核心模块**：

| 模块名称 | 文件路径 | 主要职责 |
|---------|---------|---------|
| 系统入口 | [main.c](file:///workspace/main/main.c) | 16步启动序列、系统协调 |
| 摄像头驱动 | [camera_driver.c](file:///workspace/main/camera_driver.c)/[.h](file:///workspace/main/camera_driver.h) | OV2640 初始化、帧捕获、亮度检测 |
| WiFi 管理 | [wifi_manager.c](file:///workspace/main/wifi_manager.c)/[.h](file:///workspace/main/wifi_manager.h) | AP/STA 模式切换、自动重连、状态回调 |
| 配置管理 | [config_manager.c](file:///workspace/main/config_manager.c)/[.h](file:///workspace/main/config_manager.h) | NVS 配置持久化、版本迁移 |
| Web 服务器 | [web_server.c](file:///workspace/main/web_server.c)/[.h](file:///workspace/main/web_server.h) | HTTP REST API、静态文件服务 |
| MJPEG 流 | [mjpeg_streamer.c](file:///workspace/main/mjpeg_streamer.c)/[.h](file:///workspace/main/mjpeg_streamer.h) | 实时视频流 (max 2 clients) |
| 存储管理 | [storage_manager.c](file:///workspace/main/storage_manager.c)/[.h](file:///workspace/main/storage_manager.h) | SD 卡挂载、照片存储/清理 |
| 运动检测 | [motion_detect.c](file:///workspace/main/motion_detect.c)/[.h](file:///workspace/main/motion_detect.h) | 帧差检测、亮度感知、自动闪光 |
| NAS 上传 | [nas_uploader.c](file:///workspace/main/nas_uploader.c)/[.h](file:///workspace/main/nas_uploader.h) | 照片队列上传 (HTTP/WEBDav) |
| WebDAV 客户端 | [webdav_client.c](file:///workspace/main/webdav_client.c)/[.h](file:///workspace/main/webdav_client.h) | WebDAV 协议上传 |
| 状态 LED | [status_led.c](file:///workspace/main/status_led.c)/[.h](file:///workspace/main/status_led.h) | GPIO 33 LED 状态指示 |
| 健康监控 | [health_monitor.c](file:///workspace/main/health_monitor.c)/[.h](file:///workspace/main/health_monitor.h) | Prometheus 指标、系统状态 |
| 时间同步 | [time_sync.c](file:///workspace/main/time_sync.c)/[.h](file:///workspace/main/time_sync.h) | NTP 时间同步、时区设置 |
| 延时摄影 | [timelapse.c](file:///workspace/main/timelapse.c)/[.h](file:///workspace/main/timelapse.h) | 定时拍照、连拍功能 |

---

## 启动序列

### 16步完整启动流程
```c
// 来自 main.c 的 app_main()
1. NVS Flash 初始化
2. 配置管理初始化 (加载/初始化默认配置)
3. 状态 LED 初始化 (GPIO 33)
4. SPIFFS 挂载 (Web UI 静态文件)
5. 释放 SD 卡 SPI 总线 (GPIO 14)
6. WiFi 子系统初始化 (netif + event loop)
7. WiFi 状态回调注册
8. 健康监控初始化
9. WiFi 模式选择 (STA 或 AP)
10. MJPEG 流初始化 (AP 模式直接，STA 模式延迟)
11. 时间同步初始化 (STA 模式)
12. Web 服务器启动 (端口 80)
13. 运动检测启动 (STA 模式)
14. SD 卡初始化 (仅在摄像头后)
15. NAS 上传器初始化
16. 启动按钮监控 (已禁用，GPIO 0 = XCLK)
```

**关键细节**:
- **摄像头初始化时机**: 原计划在 WiFi 前，但因 ESP32 DMA 冲突问题，**STA 模式下摄像头初始化延迟到 WiFi 连接后**，通过独立任务完成
- **GPIO 冲突**: GPIO 14 同时被 SD 卡 CLK 和摄像头使用，需要时分复用，通过 `camera_release_sd_bus()` 和 `storage_deinit()` 解决
- **AP 模式**: 没有配置 WiFi 时直接进入，此时摄像头、流、Web 服务器直接启动

---

## 主要模块说明

### 1. 系统入口 (main)
- **核心文件**: [main.c](file:///workspace/main/main.c)
- **功能**:
  - 16步启动序列控制
  - WiFi 状态回调处理
  - STA 服务初始化任务 (`sta_services_task`)
  - 协调各模块初始化顺序

**关键函数**:
- `app_main()`: 系统入口，完整启动序列
- `wifi_state_cb()`: WiFi 状态变化回调
- `sta_services_task()`: STA 模式下的服务初始化任务（独立任务，防止栈溢出）

### 2. 摄像头驱动 (camera_driver)
- **核心文件**: [camera_driver.c](file:///workspace/main/camera_driver.c) / [camera_driver.h](file:///workspace/main/camera_driver.h)
- **功能**:
  - OV2640 初始化与配置
  - JPEG 帧捕获
  - 灰度模式切换 (用于亮度检测)
  - 垂直翻转控制
  - 帧缓冲区管理

**关键函数**:
- `camera_init()`: 初始化摄像头，支持 VGA/SVGA/XGA/UXGA，双缓冲 PSRAM
- `camera_capture()`: 捕获一帧，带互斥锁，超时 3s
- `camera_return_fb()`: 释放帧缓冲区
- `camera_init_grayscale()`: 切换到灰度模式用于亮度采样
- `camera_restore_jpeg()`: 恢复正常 JPEG 模式
- `camera_apply_vflip()`: 实时应用垂直翻转
- `camera_apply_settings()`: 重新初始化摄像头并应用新设置

**技术要点**:
- 使用 `esp32-camera` 组件硬件抽象
- XCLK 频率固定 20 MHz
- 初始化后丢弃前 5 帧用于自动曝光稳定
- 线程安全：使用互斥锁防止并发访问

### 3. WiFi 管理 (wifi_manager)
- **核心文件**: [wifi_manager.c](file:///workspace/main/wifi_manager.c) / [wifi_manager.h](file:///workspace/main/wifi_manager.h)
- **功能**:
  - STA 模式连接与自动重连 (5秒间隔)
  - AP 模式启动 (默认 SSID: `ai-thinker-cam`, 密码: `12345678`)
  - 状态回调注册 (最多 4 个回调)
  - IP 地址获取

**关键函数**:
- `wifi_init()`: 初始化 netif 和 event loop
- `wifi_start_sta()`: 启动 STA 模式连接
- `wifi_start_ap()`: 启动 AP 模式
- `wifi_register_callback()`: 注册状态回调
- `wifi_get_state()` / `wifi_get_ip_str()`: 获取当前状态

**WiFi 状态枚举** (来自 [common.h](file:///workspace/main/common.h)):
```c
typedef enum {
    WIFI_STATE_AP,
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_STA_DISCONNECTED,
} wifi_state_t;
```

### 4. 配置管理 (config_manager)
- **核心文件**: [config_manager.c](file:///workspace/main/config_manager.c) / [config_manager.h](file:///workspace/main/config_manager.h)
- **功能**:
  - NVS 键值存储 (namespace: `camcfg`, key: `config`)
  - 配置版本迁移 (当前版本 5)
  - 默认配置应用
  - SD 卡配置文件加载 (`/sdcard/config.txt`)

**配置结构体** (来自 [common.h](file:///workspace/main/common.h#L82-L110)):
```c
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
    nas_protocol_t nas_protocol;
    char nas_host[65];
    uint16_t nas_port;
    char nas_user[33];
    char nas_pass[65];
    char nas_path[65];
    uint8_t vflip;
    uint8_t motion_saved_threshold;
    uint8_t wifi_tx_power;
    uint8_t wifi_power_save;
    uint8_t flash_threshold;
    uint8_t timelapse_enabled;
    uint16_t timelapse_interval_s;
    uint8_t timelapse_burst_count;
    uint32_t magic;
    uint32_t version;
} cam_config_t;
```

**关键函数**:
- `config_init()`: 初始化配置，处理版本迁移
- `config_get()`: 获取配置只读指针
- `config_save()`: 保存当前配置到 NVS
- `config_reset()`: 恢复默认配置
- `config_load_from_sd()`: 从 SD 卡加载配置

### 5. Web 服务器 (web_server)
- **核心文件**: [web_server.c](file:///workspace/main/web_server.c) / [web_server.h](file:///workspace/main/web_server.h)
- **功能**:
  - HTTP 服务器 (端口 80)
  - 完整 REST API
  - SPIFFS 静态文件服务 (Web UI)
  - CORS 支持
  - 可选密码认证 (X-Password 头)

**REST API 端点**:
| 方法 | 路径 | 功能 | 认证 |
|------|------|------|------|
| GET | `/api/status` | 设备状态 JSON | 否 |
| GET | `/api/config` | 当前配置 JSON | 否 |
| POST | `/api/config` | 更新配置 | 是 |
| POST | `/api/reset` | 重置默认配置 | 是 |
| POST | `/api/reboot` | 重启设备 | 是 |
| GET | `/capture` | 单帧 JPEG 捕获 | 否 |
| GET | `/stream` | MJPEG 视频流 | 否 |
| GET | `/metrics` | Prometheus 指标 | 否 |
| GET | `/api/files` | SD 卡照片列表 | 否 |
| GET | `/api/download` | 下载照片 | 否 |
| POST | `/api/upload` | 触发 NAS 上传 | 否 |

**关键函数**:
- `web_server_start()`: 启动 HTTP 服务器
- `web_server_stop()`: 停止服务器

### 6. MJPEG 流 (mjpeg_streamer)
- **核心文件**: [mjpeg_streamer.c](file:///workspace/main/mjpeg_streamer.c) / [mjpeg_streamer.h](file:///workspace/main/mjpeg_streamer.h)
- **功能**:
  - `multipart/x-mixed-replace` 协议流
  - 目标 15 FPS
  - 最多 2 并发客户端
  - 4KB 分块传输

**关键函数**:
- `mjpeg_streamer_init()`: 初始化流服务，创建客户端计数信号量
- `mjpeg_streamer_register()`: 注册 `/stream` 端点
- `mjpeg_streamer_get_client_count()`: 获取当前连接数

**技术要点**:
- 每个客户端启动独立 FreeRTOS 任务
- 使用 `httpd_req_send()` 分块发送
- 连续失败 10 次自动断开连接

### 7. 存储管理 (storage_manager)
- **核心文件**: [storage_manager.c](file:///workspace/main/storage_manager.c) / [storage_manager.h](file:///workspace/main/storage_manager.h)
- **功能**:
  - SD 卡 SPI 模式挂载 (SDSPI)
  - FAT32 文件系统
  - 日期目录组织 (`/sdcard/photos/YYYY-MM/`)
  - 空间自动清理 (低于 20% 时删除最旧照片)
  - 热插拔监控任务

**关键函数**:
- `storage_init()`: 初始化并挂载 SD 卡
- `storage_deinit()`: 卸载 SD 卡，释放 GPIO 14
- `storage_save_photo()`: 保存 JPEG 帧到 SD 卡
- `storage_list_photos()`: 列出所有照片
- `storage_delete_photo()`: 删除照片
- `storage_cleanup()`: 执行空间清理

**目录结构**:
```
/sdcard/
└── photos/
    ├── 2026-05/
    │   ├── photo_001.jpg
    │   └── ...
    └── 2026-06/
        └── ...
```

### 8. 运动检测 (motion_detect)
- **核心文件**: [motion_detect.c](file:///workspace/main/motion_detect.c) / [motion_detect.h](file:///workspace/main/motion_detect.h)
- **功能**:
  - JPEG 字节采样帧差检测 (避免解码开销)
  - 亮度感知 (灰度采样 30s 间隔)
  - 自动闪光 (GPIO 4 PWM)
  - 检测到运动时自动保存照片

**检测算法**:
1. 捕获帧 A，每隔 10 字节采样到缓冲区
2. 500ms 后捕获帧 B，比较采样字节
3. 差异超过阈值 (默认 30%) → 运动触发
4. 冷却时间 (默认 10s) 内不再触发
5. 检测到暗场景时自动闪光

**关键函数**:
- `motion_detect_start()`: 启动检测任务
- `motion_detect_stop()`: 停止检测
- `motion_detect_get_brightness_pct()`: 获取亮度百分比
- `motion_detect_is_scene_dark()`: 判断是否暗场景

**闪光灯控制**:
- LEDC PWM 驱动
- 频率 2 kHz，8位分辨率
- 占空比 80% (205/255) — 防止烧坏 (无限流电阻)

### 9. NAS 上传 (nas_uploader)
- **核心文件**: [nas_uploader.c](file:///workspace/main/nas_uploader.c) / [nas_uploader.h](file:///workspace/main/nas_uploader.h)
- **功能**:
  - 照片队列管理 (最大 10 个)
  - HTTP POST 上传
  - WebDAV 上传 (通过 [webdav_client.c](file:///workspace/main/webdav_client.c))
  - 失败重试 3 次，间隔 2s

**关键函数**:
- `nas_uploader_init()`: 初始化队列和上传任务
- `nas_uploader_enqueue()`: 将文件路径加入队列
- `nas_uploader_get_pending_count()`: 队列等待数
- `nas_uploader_is_busy()`: 是否正在上传

### 10. 健康监控 (health_monitor)
- **核心文件**: [health_monitor.c](file:///workspace/main/health_monitor.c) / [health_monitor.h](file:///workspace/main/health_monitor.h)
- **功能**:
  - 系统指标收集 (每 10 秒)
  - Prometheus `/metrics` 端点
  - 系统状态快照

**指标结构体**:
```c
typedef struct {
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t total_heap;
    uint32_t uptime_seconds;
    int8_t wifi_rssi;
    wifi_state_t wifi_state;
    uint8_t cpu_usage_pct;
    uint32_t photo_count;
    uint32_t sd_free_mb;
    uint32_t sd_total_mb;
    size_t spiffs_total;
    size_t spiffs_free;
    bool camera_initialized;
    bool sd_mounted;
    uint32_t stream_clients;
    uint32_t motion_events;
    uint32_t nas_pending;
    uint8_t brightness_pct;
    uint8_t brightness_method;
    bool scene_dark;
    uint32_t timelapse_photos;
    uint32_t timelapse_burst_photos;
} health_metrics_t;
```

**关键函数**:
- `health_monitor_init()`: 启动定时器，每 10s 收集
- `health_monitor_get_metrics()`: 获取指标快照
- `health_monitor_get_prometheus_str()`: Prometheus 格式字符串

### 11. 状态 LED (status_led)
- **核心文件**: [status_led.c](file:///workspace/main/status_led.c) / [status_led.h](file:///workspace/main/status_led.h)
- **功能**: GPIO 33 LED 状态指示 (低电平有效)

**LED 状态枚举**:
```c
typedef enum {
    LED_STARTING,
    LED_WIFI_CONNECTING,
    LED_RUNNING,
    LED_ERROR,
    LED_AP_MODE,
} led_status_t;
```

### 12. 时间同步 (time_sync)
- **核心文件**: [time_sync.c](file:///workspace/main/time_sync.c) / [time_sync.h](file:///workspace/main/time_sync.h)
- **功能**: NTP 时间同步，时区设置

**关键函数**:
- `time_sync_init()`: 初始化 SNTP，设置时区
- `time_sync_is_synced()`: 是否同步成功 (阈值: 2024-01-01)
- `time_sync_get_str()`: 格式化时间字符串
- `time_sync_get_date_path()`: 日期目录 (YYYY-MM)

### 13. 延时摄影 (timelapse)
- **核心文件**: [timelapse.c](file:///workspace/main/timelapse.c) / [timelapse.h](file:///workspace/main/timelapse.h)
- **功能**: 定时拍照、连拍功能

**关键函数**:
- `timelapse_init()`: 初始化
- `timelapse_start()`: 启动延时摄影任务
- `timelapse_stop()`: 停止
- `timelapse_is_running()`: 状态检查

### 14. WebDAV 客户端 (webdav_client)
- **核心文件**: [webdav_client.c](file:///workspace/main/webdav_client.c) / [webdav_client.h](file:///workspace/main/webdav_client.h)
- **功能**: WebDAV 协议文件上传 (HTTP PUT)

**关键函数**:
- `webdav_upload()`: 上传本地文件到 WebDAV 服务器

---

## 关键函数与API

### 配置相关
- `const cam_config_t *config_get()`: 获取当前配置 ([config_manager.c](file:///workspace/main/config_manager.c))
- `esp_err_t config_save()`: 保存配置到 NVS
- `esp_err_t config_set_resolution()`: 设置分辨率
- `esp_err_t config_set_flash_threshold()`: 设置闪光阈值

### 摄像头相关
- `esp_err_t camera_init(resolution, fps, jpeg_quality)`: 初始化摄像头 ([camera_driver.c](file:///workspace/main/camera_driver.c))
- `esp_err_t camera_capture(fb)`: 捕获帧
- `esp_err_t camera_return_fb(fb)`: 释放帧

### 网络相关
- `esp_err_t wifi_start_sta(ssid, pass)`: 连接 WiFi ([wifi_manager.c](file:///workspace/main/wifi_manager.c))
- `const char *wifi_get_ip_str()`: 获取 IP
- `wifi_state_t wifi_get_state()`: 获取 WiFi 状态

### 存储相关
- `esp_err_t storage_save_photo(fb, filename)`: 保存照片 ([storage_manager.c](file:///workspace/main/storage_manager.c))
- `bool storage_is_available()`: SD 卡是否可用
- `uint32_t storage_get_free_space()`: 可用空间(MB)

---

## 依赖关系

### 外部依赖
- **ESP-IDF**: v6.0
- **esp32-camera**: ^2.1.6 ([idf_component.yml](file:///workspace/main/idf_component.yml))

### ESP-IDF 组件依赖 (来自 [CMakeLists.txt](file:///workspace/main/CMakeLists.txt))
- `esp_wifi`
- `nvs_flash`
- `esp_http_server`
- `driver`
- `esp_driver_i2c`
- `spiffs`
- `fatfs`
- `sdmmc`
- `esp_http_client`
- `tcp_transport`
- `mbedtls`
- `esp_netif`
- `esp_event`
- `esp_timer`
- `lwip`
- `freertos`
- `esp_system`
- `log`

### 模块内部依赖关系
```
main
├── config_manager
├── status_led
├── wifi_manager
│   └── config_manager
├── camera_driver
│   └── config_manager
├── health_monitor
│   ├── wifi_manager
│   ├── storage_manager
│   ├── camera_driver
│   ├── mjpeg_streamer
│   └── motion_detect
├── mjpeg_streamer
│   └── camera_driver
├── time_sync
├── web_server
│   ├── config_manager
│   ├── wifi_manager
│   ├── camera_driver
│   ├── storage_manager
│   ├── mjpeg_streamer
│   ├── health_monitor
│   ├── motion_detect
│   ├── time_sync
│   └── timelapse
├── motion_detect
│   ├── camera_driver
│   ├── config_manager
│   ├── storage_manager
│   ├── time_sync
│   ├── health_monitor
│   └── mjpeg_streamer
├── storage_manager
├── nas_uploader
│   ├── config_manager
│   └── webdav_client
├── timelapse
│   ├── camera_driver
│   ├── config_manager
│   ├── storage_manager
│   ├── time_sync
│   └── health_monitor
└── webdav_client
```

---

## 配置管理

### 分区表 (partitions.csv)
```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x380000
otadata,  data, ota,     0x390000, 0x2000
spiffs,   data, spiffs,  0x392000, 0x6e000
```
- **NVS**: 配置存储 (24KB)
- **Factory**: 固件 (3.5MB)
- **SPIFFS**: Web UI 文件 (440KB)

### 默认配置 (config_manager.c)
- SSID: 空
- 密码: 空
- 设备名: `ai-thinker-cam`
- 分辨率: VGA
- FPS: 15
- JPEG 质量: 12
- Web 密码: `admin`
- 时区: `CST-8` (中国标准时间)
- 运动阈值: 30%
- 冷却时间: 10s
- 闪光阈值: 40%
- NAS 协议: HTTP
- NAS 端口: 8080
- NAS 路径: `/upload`
- WiFi TX 功率: 80 (20dBm)
- WiFi 省电: 关
- 延时摄影: 关，间隔 30s，连拍 3 张

---

## 项目运行方式

### 1. 环境准备
- 安装 ESP-IDF v6.0
- 安装 Python 3.8+
- 安装 `esptool.py`

### 2. 编译
```bash
idf.py set-target esp32
idf.py build
```

### 3. 烧录
```bash
idf.py -p COMx flash monitor
# 替换 COMx 为实际串口 (Windows: COMx, Linux: /dev/ttyUSBx)
```

### 4. 首次使用
1. 启动后，若无 WiFi 配置，自动进入 AP 模式
2. 连接 WiFi `ai-thinker-cam` (密码 `12345678`)
3. 浏览器访问 `http://192.168.4.1`
4. 进入配置页面输入 WiFi SSID 和密码
5. 保存后设备重启，进入 STA 模式

### 5. 恢复出厂设置
使用 API (推荐):
```bash
curl -X POST http://DEVICE_IP/api/reset -H "X-Password: admin"
```

---

## 引脚映射
| 功能 | GPIO | 说明 |
|------|------|------|
| XCLK | 0 | 摄像头主时钟 |
| SIOD (SDA) | 26 | I2C 数据 |
| SIOC (SCL) | 27 | I2C 时钟 |
| D0 | 5 | 并行数据 |
| D1 | 18 | 并行数据 |
| D2 | 19 | 并行数据 |
| D3 | 21 | 并行数据 |
| D4 | 36 | 并行数据 (仅输入) |
| D5 | 39 | 并行数据 (仅输入) |
| D6 | 34 | 并行数据 (仅输入) |
| D7 | 35 | 并行数据 (仅输入) |
| VSYNC | 25 | 垂直同步 |
| HREF | 23 | 水平参考 |
| PCLK | 22 | 像素时钟 |
| PWDN | 32 | 掉电控制 |
| SD_CS | 13 | SD 卡片选 |
| SD_CLK | 14 | SD 卡时钟 (与摄像头共享) |
| SD_MOSI | 15 | SD 卡 MOSI |
| SD_MISO | 2 | SD 卡 MISO |
| LED_STATUS | 33 | 状态 LED (低电平有效) |
| LED_FLASH | 4 | 闪光灯 LED (PWM) |
| BOOT | 0 | 启动按钮 (与 XCLK 共享，未使用) |

---

## 编译配置 (sdkconfig.defaults)
- CPU 频率: 240 MHz
- PSRAM: 启用 (malloc 模式)
- Flash 大小: 4MB
- 分区表: 自定义
- HTTP 服务器: 启用，请求头 2KB，URI 1KB
- 主任务栈: 8KB
- FreeRTOS: 1 kHz，16 字符任务名
- 优化: 大小优先 (`-Os`)
- 核心转储: 禁用
- 调试: 禁用

---

## CI/CD (GitHub Actions)
Release 流程 ([.github/workflows/release.yml](file:///workspace/.github/workflows/release.yml)):
1. 推送 `v*` 标签时触发
2. 使用 `espressif/idf:v6.0` 容器编译
3. 打包固件 (bootloader, partition, app, merged)
4. 创建 GitHub Release
5. 发布固件文件

---

## 贡献指南
参考 [CONTRIBUTING.md](file:///workspace/CONTRIBUTING.md):
1. Fork 仓库
2. 创建功能分支
3. 修改代码
4. 编译测试: `idf.py build`
5. 提交 PR

---

## 许可证
MIT License
