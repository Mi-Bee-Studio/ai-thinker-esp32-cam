# MiBee Cam 固件

[![ESP32](https://img.shields.io/badge/ESP32-ESP32_D0WD_V3-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
[![OV2640](https://img.shields.io/badge/Camera-OV2640-orange.svg)](https://www.ovti.com/products/imaging-sensors/ov2640)
[![Resolution](https://img.shields.io/badge/Resolution-VGA--UXGA-purple.svg)](https://en.wikipedia.org/wiki/Display_resolution)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build](https://img.shields.io/badge/Build-Working-brightgreen.svg)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam)

---

🚀 **高性能 ESP32-CAM 固件** - 基于 ESP-IDF v6.0.1 开发的开源固件，为 MiBee Cam 板卡提供完整的摄像头解决方案。

## 功能特性

- **📷 摄像头捕获** — OV2640 传感器，支持 VGA/SVGA/XGA/UXGA 分辨率，JPEG 压缩输出
- **📡 WiFi 管理** — STA 模式自动重连，双 WiFi 故障转移，智能漫游
- **🌐 Web 界面** — 直观的仪表板、实时 MJPEG 预览、折叠式设置页面
- **🎥 MJPEG 流传输** — 通过 `/stream` 端点实现高达 15 FPS 的实时视频流
- **🔍 运动检测** — 帧差算法，可配置触发阈值和冷却时间
- **💡 智能闪光** — 基于亮度检测的自动闪光，暗场景自动拍照
- **💾 SD 卡存储** — 自动保存照片和录像到 TF 卡，支持热插拔检测
- **📁 文件管理** — 网页管理 SD 卡文件，支持照片/录像类型筛选
- **🔄 固件升级** — 通过网页上传固件（OTA），支持进度条和自动重启
- **🎨 Web UI 升级** — 通过网页上传 SPIFFS 镜像更新界面，无需串口
- **📊 健康监控** — 兼容 Prometheus 的 `/metrics` 端点，系统状态监控
- **🔴 状态 LED** — GPIO33 LED 指示系统运行状态
- **⏰ 时间同步** — NTP 时间同步，支持自定义时区
- **🎯 延时摄影** — 爆发拍摄模式，可配置间隔和拍摄数量
- **🎥 录像功能** — 连续录像、延时录像、动态延时录像三种模式

## 硬件要求

- **主板**: MiBee Cam (ESP32-D0WD-V3)
- **摄像头**: OV2640 摄像头模块
- **存储**: TF 卡（可选，用于照片存储）
- **闪存**: 4MB
- **PSRAM**: 8MB（板载，4MB 可用）

## 技术栈

| 组件 | 描述 |
|------|------|
| **ESP-IDF v6.0.1** | Espressif IoT 开发框架 |
| **ESP32 Camera** | OV2640 驱动，支持 PSRAM DMA 传输 |
| **ESP32 WiFi** | 802.11 b/g/n，STA/AP 模式切换 |
| **SPIFFS** | 文件系统，Web 界面资源存储 |
| **FatFS** | SD 卡 FAT 文件系统支持 |
| **cJSON** | 嵌入式 JSON 解析器 |
| **FreeRTOS** | 实时操作系统，任务调度 |
| **LwIP** | 轻量级 TCP/IP 协议栈 |
| **Prometheus** | 系统指标监控协议 |

## 引脚映射

| 引脚 | GPIO | 功能 |
|------|------|----------|
| XCLK | 0 | 摄像头主时钟（与 SD 卡共享） |
| SIOD | 26 | I2C 数据线（SDA） |
| SIOC | 27 | I2C 时钟线（SCL） |
| D0 | 5 | 并行数据线 |
| D1 | 18 | 并行数据线 |
| D2 | 19 | 并行数据线 |
| D3 | 21 | 并行数据线 |
| D4 | 36 | 并行数据线（仅输入） |
| D5 | 39 | 并行数据线（仅输入） |
| D6 | 34 | 并行数据线（仅输入） |
| D7 | 35 | 并行数据线（仅输入） |
| VSYNC | 25 | 垂直同步信号 |
| HREF | 23 | 水平参考信号 |
| PCLK | 22 | 像素时钟信号 |
| PWDN | 32 | 摄像头断电控制 |
| SD_CS | 13 | SD 卡片选信号 |
| SD_CLK | 14 | SD 卡时钟（与摄像头 XCLK 共享） |
| SD_MOSI | 15 | SD 卡 MOSI 信号 |
| SD_MISO | 2 | SD 卡 MISO 信号 |
| LED_STATUS | 33 | 状态 LED（低电平有效） |
| LED_FLASH | 4 | 闪光灯 LED（PWM 控制） |

## 快速开始

### 前置要求

- ESP-IDF v6.0.1 已安装
- Python 3.8+
- esptool.py
- MiBee Cam 主板
- OV2640 摄像头模块

### 构建

```bash
# 设置目标芯片
idf.py set-target esp32

# 编译固件
idf.py build
```

### 烧录

```bash
# 烧录固件并启动监视器
idf.py -p /dev/ttyUSB0 flash monitor
```

将 `/dev/ttyUSB0` 替换为您的串口设备。

### 首次配置

首次启动时，设备进入 AP 模式：

1. 连接 WiFi 网络 **MiBeeCam**（密码：`12345678`）
2. 在浏览器中打开 **http://192.168.4.1**
3. 进入配置页面，输入您的 WiFi SSID 和密码
4. 保存配置 — 设备重启并以 STA 模式连接到您的网络

### 恢复出厂设置

⚠️ **重要说明**：由于 GPIO0 被用作摄像头 XCLK 引脚，无法可靠检测 BOOT 按钮按下。请使用 `POST /api/reset` API 端点来恢复出厂设置。

## REST API

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/status` | 获取设备状态 JSON |
| GET | `/api/config` | 获取当前配置 JSON |
| POST | `/api/config` | 更新设备配置 |
| POST | `/api/reset` | 恢复出厂设置 |
| POST | `/api/reboot` | 重启设备 |
| GET | `/capture` | 单张 JPEG 照片拍摄 |
| GET | `/stream` | MJPEG 实时视频流 |
| GET | `/metrics` | Prometheus 系统指标 |
| GET | `/api/files` | 列出 SD 卡照片文件 |
| GET | `/api/download?name=xxx` | 下载指定照片 |
|
| POST | `/api/record?action=start|stop` | 录制控制（开始/停止） |
| GET | `/api/record` | 录制状态和信息 |
| GET | `/api/storage` | 存储使用情况和清理状态 |

## 项目结构

```
ai-thinker-esp32-cam/
├── main/
│   ├── main.c              # 系统入口，19步启动序列
│   ├── camera_driver.c/h   # OV2640 摄像头驱动（DMA 优化）
│   ├── wifi_manager.c/h    # WiFi AP/STA 管理（带回调）
│   ├── config_manager.c/h  # NVS 配置持久化（版本 9）
│   ├── web_server.c/h      # HTTP 服务器 + REST API + SPIFFS
│   ├── mjpeg_streamer.c/h  # MJPEG 实时流传输（异步）
│   ├── storage_manager.c/h # SD 卡存储（热插拔检测）
│   ├── motion_detect.c/h   # 运动检测
│   ├── status_led.c/h      # 状态 LED 驱动
│   ├── time_sync.c/h       # NTP 时间同步
│   ├── health_monitor.c/h  # Prometheus 指标收集
│   ├── video_recorder.c/h  # AVI 视频录制（3种模式）
│   ├── timelapse.c/h       # 延时摄影功能
│   ├── flash_led.c/h       # 闪光灯控制
│   ├── serial_config.c/h   # 串口配置接口
│   └── web_ui/            # Web 界面文件
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/idf_component.yml # 组件依赖
└── .github/workflows/    # CI/CD 流水线
```

## 启动序列

固件采用 **19 步启动序列**：

1. **NVS 闪存初始化** — 配置存储准备
2. **配置加载** — 从 NVS 加载设置（版本 9）
3. **状态 LED 初始化** — GPIO33 系统状态指示
4. **SPIFFS 挂载** — Web 界面文件系统准备
5. **SD SPI 总线释放** — 释放 GPIO14 给摄像头和 WiFi
6. **WiFi 子系统初始化** — 网络接口和事件循环
7. **WiFi 状态回调注册** — 网络状态变化监听
8. **健康监控初始化** — 系统指标收集
9. **WiFi 模式选择** — STA 已配置则连接，否则启动 AP
10. **MJPEG 流传输器初始化** — 实时视频流准备（STA 模式延迟到 WiFi 连接后）
11. **时间同步初始化** — NTP 客户端（延迟到 WiFi 连接后）
12. **Web 服务器启动** — HTTP 服务和 API 端点
13. **运动检测启动** — 帧监控服务（延迟到 WiFi 连接后）
14. **SD 卡初始化** — 存储接口（摄像头之后，GPIO14 冲突）
15. **串口配置启动** — AT 命令配置接口
16. **NAS 上传** — *已移除（未实现）*
17. **BOOT 按钮** — *已禁用（GPIO0 = 摄像头 XCLK）*
18. **视频录制器初始化** — AVI 录制系统（延迟 STA 服务任务）
19. **延时摄影和闪光灯** — 拍摄服务和 PWM 闪光（延迟 STA 服务任务）

### 重要说明

- **GPIO14 共享限制**：SD 卡 CLK 和摄像头 XCLK 共享 GPIO14，初始化顺序非常关键
- 必须先释放 SD 卡，再初始化摄像头，最后重新初始化 SD 卡以避免时序冲突
- **摄像头初始化被延迟到 WiFi STA 连接后**，这是已知的 ESP32 DMA 冻结问题的 workaround（esp32-camera#620）
- 在 STA 模式下，MJPEG、时间同步、Web 服务器和运动检测等功能将在 WiFi 连接完成后异步启动
- 项目中 **不包含 NAS 上传和 WebDAV 客户端功能**，只能通过 SD 卡手动下载照片
- 项目中 **不包含 ONVIF、WebSocket、Webhook 等功能**

## 配置

所有设置存储在 NVS 中（配置版本 9），可通过 Web 界面或 REST API 访问。

### WiFi 配置
- **ssid** — WiFi 网络名称
- **wifi_pass** — WiFi 密码（API 出于安全考虑不会返回此字段）
- **dual_ssid** — 第二 WiFi 网络名称（可选）
- **dual_pass** — 第二 WiFi 密码（可选）

### 设备配置
- **device_name** — 仪表板显示的设备名称（默认：`MiBeeCam`）
- **web_password** — 可选的 Web 界面访问密码（留空 = 无需认证）

### 摄像头配置
- **resolution** — 图像分辨率：0=VGA, 1=SVGA, 2=XGA, 3=UXGA（默认：SVGA）
- **quality** — JPEG 质量 1-63，数值越低质量越好（默认：10）

### 运动检测
- **motion_enabled** — 启用/禁用运动检测（默认：启用）
- **motion_threshold**（1-100）— 触发运动的帧差异百分比（默认：30）
- **motion_cooldown**（1-60 秒）— 连续触发之间的冷却时间（默认：5）

### 亮度检测与智能闪光
固件使用**灰度像素探测**检测场景亮度——这是 OV2640 传感器最可靠的方法。当在暗场景中检测到运动时，闪光灯自动激活拍照。

- **flash_threshold**（0-100）— 触发闪光的亮度百分比阈值（默认：40）
  - **0** = 闪光禁用
  - **34-40** = 推荐室内使用
  - **100** = 每次都触发闪光

### 时间同步
- **timezone** — 时区字符串（如 `CST-8` 表示北京时间）

### 延时摄影
- **timelapse_enabled** — 启用/禁用延时摄影（默认：禁用）
- **timelapse_interval_s** — 拍摄间隔秒数（默认：30）
- **timelapse_burst_count** — 每次拍摄的图片数量（默认：3）

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

# 开始视频录制
curl -X POST http://DEVICE_IP/api/record?action=start
# 停止视频录制
curl -X POST http://DEVICE_IP/api/record?action=stop
# 查看录制状态
curl http://DEVICE_IP/api/record
```

## 重要注意事项

1. **GPIO0 功能限制**：GPIO0 被用作摄像头 XCLK 引脚，无法用作 BOOT 按钮输入
2. **摄像头初始化时序**：为了避免 ESP32 WiFi STA 启动时的 DMA 冻结问题，摄像头初始化被延迟到 WiFi 连接完成后
3. **SD 卡与摄像头共享**：GPIO14 在 SD 卡 CLK 和摄像头 XCLK 之间共享，初始化顺序非常重要
4. **OTA 升级**：固件支持网页 OTA 升级：
   - 固件升级：设置 → 固件升级 → 上传 `build/mibee_cam.bin`
   - Web UI 升级：设置 → Web UI 升级 → 上传 `build/spiffs.bin`
   - 首次烧录需要串口：`idf.py -p /dev/ttyUSB0 flash`

## 更新日志

### v0.2.0 (2026-07-18)

**新功能：**
- **文件管理页**：显示 SD 卡所有文件（照片+录像），支持类型筛选
- **Web UI 升级**：通过网页上传 SPIFFS 镜像更新界面，无需串口
- **固件 OTA**：网页上传固件，支持进度条和自动重启
- **自动版本号**：固件版本从 git 标签自动生成

**改进：**
- **导航重构**：仪表盘 → 实时预览 → 文件 → 设置
- **设置页折叠**：所有设置区域默认折叠，减少滚动
- **固件升级嵌入**：OTA 上传移入设置页
- **文件列表 API**：`/api/files` 支持 `type` 参数筛选
- **下载 API**：`/api/download` 支持照片和录像

**修复：**
- 修复文件列表缓存在首次查询后损坏的问题

### v0.1.0

- 初始版本，支持 MJPEG 流、运动检测、SD 卡存储
- 延时摄影和连拍模式
- 双 WiFi 故障转移配置
- REST API 完整控制
- Prometheus 指标端点

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request 来改进这个项目！请确保：

1. 代码符合项目的编码规范
2. 添加适当的注释和文档
3. 测试您的更改
4. 更新相关的 README 文档

## 支持

如果您遇到问题或有建议，请：

1. 查看 [文档](docs/zh/) 目录
2. 提交 GitHub Issue
3. 查看已知问题和解决方案