# AI_Thinker ESP32-CAM 固件

开源 ESP32-CAM 固件，包含 Web 界面、MJPEG 流传输、运动检测和 NAS 上传功能。

## 功能特性

- **摄像头捕获** — OV2640 摄像头，VGA/SVGA/XGA/UXGA 分辨率，JPEG 输出
- **WiFi 管理** — STA 模式自动重连，首次配置时 AP 热点回退模式
- **Web 界面** — 仪表板、实时 MJPEG 预览、配置页面
- **MJPEG 流传输** — 通过 `/stream` 端点实现最多 15 FPS 的实时视频
- **运动检测** — 帧差算法，可配置阈值和冷却时间
- **SD 卡存储** — 自动将 JPEG 照片保存到 TF 卡（仅照片存储）
- **NAS 上传** — 运动触发时自动上传（WebDAV/HTTP POST）
- **REST API** — 完整的状态、配置、文件管理和捕获 API
- **健康监控** — 兼容 Prometheus 的 `/metrics` 端点
- **状态 LED** — GPIO33 LED 指示系统状态
- **时间同步** — NTP 时间同步，可配置时区

## 硬件要求

- **主板**: AI_Thinker ESP32-CAM
- **摄像头**: OV2640 摄像头模块
- **存储**: TF 卡（可选，用于照片存储）
- **闪存**: 4MB
- **PSRAM**: 4MB

## 引脚映射

| 引脚 | GPIO | 功能 |
|-----|------|----------|
| XCLK | 0 | 主时钟 |
| SIOD | 26 | I2C 数据线（SDA） |
| SIOC | 27 | I2C 时钟线（SCL） |
| D0 | 5 | 并行数据 |
| D1 | 18 | 并行数据 |
| D2 | 19 | 并行数据 |
| D3 | 21 | 并行数据 |
| D4 | 36 | 并行数据（仅输入） |
| D5 | 39 | 并行数据（仅输入） |
| D6 | 34 | 并行数据（仅输入） |
| D7 | 35 | 并行数据（仅输入） |
| VSYNC | 25 | 垂直同步 |
| HREF | 23 | 水平参考 |
| PCLK | 22 | 像素时钟 |
| PWDN | 32 | 断电控制 |
| SD_CS | 13 | SD 卡片选 |
| SD_CLK | 14 | SD 卡时钟（与摄像头共享） |
| SD_MOSI | 15 | SD 卡 MOSI |
| SD_MISO | 2 | SD 卡 MISO |
| LED_STATUS | 33 | 状态 LED（低电平有效） |
| LED_FLASH | 4 | 闪光灯 LED（PWM） |
| BOOT_BTN | 0 | 恢复出厂按钮 |

## 快速开始

### 前置要求

- 已安装 ESP-IDF v6.0
- Python 3.8+
- esptool.py
- AI_Thinker ESP32-CAM 主板
- OV2640 摄像头模块

### 构建

```bash
idf.py set-target esp32
idf.py build
```

### 烧录

```bash
idf.py -p COM8 flash monitor
```

将 `COM8` 替换为您的串口。

### 首次配置

首次启动时，设备进入 AP 模式：

1. 连接 WiFi 网络 **ai-thinker-cam**（密码：`12345678`）
2. 在浏览器中打开 **http://192.168.4.1**
3. 进入配置页面，输入您的 WiFi SSID 和密码
4. 保存 — 设备重启并以 STA 模式连接

### 恢复出厂设置

按住 **BOOT** 按钮（GPIO 0）**5秒** 可将配置恢复为默认值。

## REST API

| 方法 | 路径 | 描述 |
|--------|------|-------------|
| GET | `/api/status` | 设备状态 JSON |
| GET | `/api/config` | 当前配置 JSON |
| POST | `/api/config` | 更新配置 |
| POST | `/api/reset` | 恢复默认值 |
| POST | `/api/reboot` | 重启设备 |
| GET | `/capture` | 单张 JPEG 捕获 |
| GET | `/stream` | MJPEG 视频流 |
| GET | `/metrics` | Prometheus 指标 |
| GET | `/api/files` | 列出 SD 卡照片 |
| GET | `/api/download?name=xxx` | 下载照片 |
| POST | `/api/upload` | 触发手动上传 |

## 项目结构

```
AI_Thinker-ESP32-cam/
├── main/
│   ├── main.c              # 系统入口，16步启动序列
│   ├── camera_driver.c/h   # OV2640 摄像头驱动
│   ├── wifi_manager.c/h    # WiFi AP/STA 管理
│   ├── config_manager.c/h  # NVS 配置持久化
│   ├── web_server.c/h      # HTTP 服务器 + REST API
│   ├── mjpeg_streamer.c/h  # MJPEG 实时流传输
│   ├── storage_manager.c/h # SD 卡存储管理
│   ├── motion_detect.c/h   # 运动检测
│   ├── nas_uploader.c/h   # NAS 上传调度器
│   ├── webdav_client.c/h  # WebDAV 协议客户端
│   ├── status_led.c/h     # 状态 LED 驱动
│   ├── health_monitor.c/h # 系统健康监控
│   └── web_ui/            # Web 界面文件
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/common.h          # 共享类型和常量
└── main/idf_component.yml # 组件依赖
```

## 启动序列

固件遵循 16 步初始化序列：

1. NVS 闪存初始化
2. 从 NVS 加载配置
3. 状态 LED 初始化（GPIO33）
4. 挂载 SPIFFS 用于 Web 界面
5. 摄像头初始化（在 WiFi 之前，避免 I2C 冲突）
6. WiFi 子系统初始化
7. 注册 WiFi 状态回调
8. 健康监控初始化
9. WiFi 模式选择（如果已配置则 STA，否则 AP）
10. MJPEG 流传输器初始化
11. 时间同步初始化（仅 STA 模式）
12. 启动 Web 服务器（端口 80）
13. 启动运动检测（仅 STA 模式）
14. 初始化 SD 卡（在摄像头之后，处理 GPIO14 共享）
15. 初始化 NAS 上传器
16. BOOT 按钮监控任务（5秒长按 = 恢复出厂设置）

## 配置

所有设置都存储在 NVS 中，可通过 Web 界面或 REST API 访问：

- WiFi SSID 和密码
- 设备名称
- 摄像头分辨率和质量
- 运动检测阈值和冷却时间
- NAS 服务器设置（WebDAV/HTTP）
- 时区
- Web 密码（可选）

## 许可证

MIT 许可证