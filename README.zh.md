# AI_Thinker ESP32-CAM 固件

开源 ESP32-CAM 固件，包含 Web 界面、MJPEG 流传输、运动检测和 NAS 上传功能。

## 功能特性

- **摄像头捕获** — OV2640 摄像头，VGA/SVGA/XGA/UXGA 分辨率，JPEG 输出
- **WiFi 管理** — STA 模式自动重连，首次配置时 AP 热点回退模式
- **Web 界面** — 仪表板、实时 MJPEG 预览、配置页面
- **MJPEG 流传输** — 通过 `/stream` 端点实现最多 15 FPS 的实时视频
- **运动检测** — 帧差算法，可配置阈值和冷却时间
- **智能闪光** — 传感器寄存器亮度检测，暗场景自动闪光拍照
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

所有设置存储在 NVS（配置版本 4）中，可通过 Web 界面（`/config.html`）或 REST API（`GET/POST /api/config`）访问。

### WiFi
- **ssid** — WiFi 网络名称
- **wifi_pass** — WiFi 密码（出于安全考虑，API 不会返回此字段）

### 设备
- **device_name** — 仪表板显示的设备名称（默认：`ai-thinker-cam`）
- **web_password** — 可选的 Web 界面访问密码（留空 = 无需认证）

### 摄像头
- **resolution** — 图像分辨率：5=VGA, 6=SVGA, 7=XGA, 8=UXGA（默认：SVGA）
- **quality** — JPEG 质量 1-63，数值越低质量越好（默认：10）

### 运动检测
- **motion_enabled** — 启用/禁用运动检测（默认：启用）
- **motion_threshold**（1-100）— 触发运动的帧差异百分比（默认：30）。数值越高，灵敏度越低。
- **motion_cooldown**（1-60 秒）— 连续触发之间的冷却时间（默认：5）

### 亮度检测与智能闪光

固件使用**灰度像素探测**来检测场景亮度——这是 OV2640 传感器在 AI-Thinker 开发板上最可靠的方法。

#### 工作原理

1. **灰度探测（主要方法）**：每 30 秒，摄像头临时切换到 GRAYSCALE+QQVGA（160×120）模式并捕获一帧。所有像素亮度值取平均，生成准确的亮度读数（0-100%）。模式切换后丢弃 2 帧预热帧以确保曝光稳定。然后摄像头恢复 JPEG 模式。整个探测过程约 1.5 秒。

2. **JPEG 回退**：当灰度探测不可用时（例如 MJPEG 流客户端已连接，摄像头模式切换会中断流），系统回退到 JPEG 帧大小启发式——较小的 JPEG 文件表示较暗的场景。精度较低但始终可用。

3. **自动闪光**：当在暗场景中检测到运动事件（`brightness_pct < flash_threshold`）时，闪光灯 LED（GPIO4）会以 ~80% PWM 占空比自动开启（AI-Thinker 板没有限流电阻，~80% 是安全上限），拍照后立即关闭。

4. **暗场景运动灵敏度**：在暗场景中，JPEG 帧的字节差异非常小，运动更难被检测到。固件会在检测到暗场景时自动将有效运动阈值降低为配置值的 1/4（最低 5%）。

#### 为什么不用传感器寄存器？

OV2640 的 AEC（自动曝光控制）寄存器曾被测试作为亮度来源，但在连续 JPEG 捕获模式下不可靠——`aec_value` 稳定在最大值（671）且不随实际光照条件变化。灰度像素采样是该传感器唯一可靠的方法。

#### 算法与公式

**灰度探测（主要方法）：**

```
avg = sum(所有像素字节) / 总像素数        // 0-255 灰度平均值
pct = avg × 100 / 255                       // 归一化到 0-100%
is_dark = (pct < flash_threshold)
```

实测值（AI-Thinker 板，SVGA，quality=10）：
- 暗室（无灯光）：avg=32, pct=12%
- 对着天花板灯光：avg=136, pct=53%

**JPEG 帧大小回退：**

```
jpeg_kb = 帧大小 / 1024
if jpeg_kb >= 22:   pct = 100%      // 明亮
elif jpeg_kb <= 12: pct = 0%        // 非常暗
else:               pct = (jpeg_kb - 12) × 100 / 10   // 12-22 KB 线性映射
is_dark = (pct < flash_threshold)
```

实测 JPEG 大小（SVGA，quality=10）：
- 暗室：~12-14 KB
- 室内昏暗：~14-17 KB
- 对着灯光：~17-25 KB

**闪光灯 LED PWM 控制：**

```
// GPIO4，LEDC Timer 1 / Channel 1（Timer 0 被摄像头 XCLK 占用）
// PWM：2 kHz，8 位分辨率（0-255 占空比）
// 占空比 = 205（~80%）— AI-Thinker 安全上限（无限流电阻）

开灯：ledc_set_duty(205) → 等待 200ms 预热 → 拍照 → ledc_set_duty(0)
关灯：ledc_set_duty(0)
```

闪光灯仅在 `handle_motion_event()` 中用于最终拍照。运动检测期间**不会**在参考帧/比较帧之间开灯——这样做会产生虚假的 80%+ 帧差异。

**暗场景运动阈值降低：**

```
if 暗场景:
    effective_thresh = max(motion_threshold / 4, 5)   // 配置值的 1/4，最低 5%
else:
    effective_thresh = motion_threshold               // 使用配置值
```

#### 配置参数

- **flash_threshold**（0-100）— 触发闪光的亮度百分比阈值（默认：40）
  - **0** = 永不触发闪光（闪光禁用）
  - **20-30** = 仅在非常暗的场景触发闪光
  - **34-40**（推荐）= 在中等偏暗的室内环境触发闪光
  - **60-80** = 在较暗的光线下触发闪光（走廊、黄昏等）
  - **100** = 每次运动事件都触发闪光

  通过 Web 界面配置页（`/config.html`）或 REST API 调整。

#### 亮度监控

亮度数据可通过以下渠道查看：

- **仪表板**（`/index.html`）：每 10 秒自动刷新，显示 `scene_dark` 状态
- **状态 API**（`GET /api/status`）：返回 `brightness_pct`（亮度百分比）、`brightness_method`（"grayscale" 或 "jpeg-fallback"）、`scene_dark`（是否暗场景）
- **Prometheus 指标**（`GET /metrics`）：`esp32_brightness_value`、`esp32_brightness_method{method="grayscale|jpeg-fallback"}`、`esp32_scene_dark`
- **串口日志**：`Grayscale probe: avg=32, pct=12%, dark=YES (probe took 1490 ms)`
### NAS 上传
- **nas_protocol** — 0=HTTP POST, 1=WebDAV
- **nas_host** — 服务器 IP 或主机名
- **nas_port** — 服务器端口
- **nas_user** / **nas_pass** — 认证凭据
- **nas_path** — 远程上传路径（如 `/photos`）

### 系统
- **timezone** — 时区字符串（如 `CST-8` 表示北京时间）

### API 使用示例

```bash
# 读取当前配置
curl http://DEVICE_IP/api/config

# 设置闪光阈值为 60（对较暗光线更敏感）
curl -X POST http://DEVICE_IP/api/config \
  -H 'Content-Type: application/json' \
  -d '{"flash_threshold":60}'

# 查看亮度状态
curl http://DEVICE_IP/api/status | jq '.data | {brightness_pct, brightness_method, scene_dark}'

# 查看 Prometheus 指标
curl http://DEVICE_IP/metrics | grep brightness
```

## 许可证

MIT 许可证