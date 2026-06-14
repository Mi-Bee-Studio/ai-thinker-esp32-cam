#XT|# MiBee Cam 固件
#KM|
#QT|[![ESP32](https://img.shields.io/badge/ESP32-ESP32_D0WD_V3-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
#YM|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
#NW|[![OV2640](https://img.shields.io/badge/Camera-OV2640-orange.svg)](https://www.ovti.com/products/imaging-sensors/ov2640)
#XM|[![Resolution](https://img.shields.io/badge/Resolution-VGA--UXGA-purple.svg)](https://en.wikipedia.org/wiki/Display_resolution)
#YV|[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
#WR|[![Build](https://img.shields.io/badge/Build-Working-brightgreen.svg)](https://github.com/Mi-Bee-Studio/mibee-cam)
#JT|
#KJ|---
#TJ|
#PX|🚀 **高性能 ESP32-CAM 固件** - 基于 ESP-IDF v6.0.1 开发的开源固件，为 MiBee Cam 板卡提供完整的摄像头解决方案。
#BQ|
#BK|## 功能特性
#RJ|
#ZV|- **📷 摄像头捕获** — OV2640 传感器，支持 VGA/SVGA/XGA/UXGA 分辨率，JPEG 压缩输出
#JW|- **📡 WiFi 管理** — STA 模式自动重连，首次配置 AP 热点回退模式
#JB|- **🌐 Web 界面** — 直观的仪表板、实时 MJPEG 预览、配置管理页面
#KM|- **🎥 MJPEG 流传输** — 通过 `/stream` 端点实现高达 15 FPS 的实时视频流
#VB|- **🔍 运动检测** — 帧差算法，可配置触发阈值和冷却时间
#SN|- **💡 智能闪光** — 基于亮度检测的自动闪光，暗场景自动拍照
#HT|- **💾 SD 卡存储** — 自动保存 JPEG 照片到 TF 卡，支持热插拔检测
#KR|- **📊 健康监控** — 兼容 Prometheus 的 `/metrics` 端点，系统状态监控
#RQ|- **🔴 状态 LED** — GPIO33 LED 指示系统运行状态
#ZJ|- **⏰ 时间同步** — NTP 时间同步，支持自定义时区
#XW|- **🎯 延时摄影** — 爆发拍摄模式，可配置间隔和拍摄数量
#JJ|
#BX|## 硬件要求
#ZR|
#PK|- **主板**: MiBee Cam (ESP32-D0WD-V3)
#VS|- **摄像头**: OV2640 摄像头模块
#QM|- **存储**: TF 卡（可选，用于照片存储）
#TB|- **闪存**: 4MB
#RM|- **PSRAM**: 8MB（板载，4MB 可用）
#TX|
#VV|## 技术栈
#RB|
#QZ|| 组件 | 描述 |
#YH||------|------|
#JQ|| **ESP-IDF v6.0.1** | Espressif IoT 开发框架 |
#QZ|| **ESP32 Camera** | OV2640 驱动，支持 PSRAM DMA 传输 |
#XR|| **ESP32 WiFi** | 802.11 b/g/n，STA/AP 模式切换 |
#HY|| **SPIFFS** | 文件系统，Web 界面资源存储 |
#VH|| **FatFS** | SD 卡 FAT 文件系统支持 |
#VJ|| **cJSON** | 嵌入式 JSON 解析器 |
#BN|| **FreeRTOS** | 实时操作系统，任务调度 |
#QP|| **LwIP** | 轻量级 TCP/IP 协议栈 |
#BZ|| **Prometheus** | 系统指标监控协议 |
#BN|
#YJ|## 引脚映射
#PZ|
#XW|| 引脚 | GPIO | 功能 |
#PT||------|------|----------|
#VQ|| XCLK | 0 | 摄像头主时钟（与 SD 卡共享） |
#WH|| SIOD | 26 | I2C 数据线（SDA） |
#NX|| SIOC | 27 | I2C 时钟线（SCL） |
#ZK|| D0 | 5 | 并行数据线 |
#TK|| D1 | 18 | 并行数据线 |
#HM|| D2 | 19 | 并行数据线 |
#YV|| D3 | 21 | 并行数据线 |
#QB|| D4 | 36 | 并行数据线（仅输入） |
#XR|| D5 | 39 | 并行数据线（仅输入） |
#RW|| D6 | 34 | 并行数据线（仅输入） |
#HW|| D7 | 35 | 并行数据线（仅输入） |
#JZ|| VSYNC | 25 | 垂直同步信号 |
#QN|| HREF | 23 | 水平参考信号 |
#YM|| PCLK | 22 | 像素时钟信号 |
#MN|| PWDN | 32 | 摄像头断电控制 |
#YT|| SD_CS | 13 | SD 卡片选信号 |
#JK|| SD_CLK | 14 | SD 卡时钟（与摄像头 XCLK 共享） |
#JH|| SD_MOSI | 15 | SD 卡 MOSI 信号 |
#PQ|| SD_MISO | 2 | SD 卡 MISO 信号 |
#VM|| LED_STATUS | 33 | 状态 LED（低电平有效） |
#SV|| LED_FLASH | 4 | 闪光灯 LED（PWM 控制） |
#SZ|
#XS|## 快速开始
#VB|
#NR|### 前置要求
#BR|
#QY|- ESP-IDF v6.0.1 已安装
#SM|- Python 3.8+
#YY|- esptool.py
#SY|- MiBee Cam 主板
#BX|- OV2640 摄像头模块
#SR|
#JH|### 构建
#XB|
#BV|```bash
#ZB|# 设置目标芯片
#ZB|idf.py set-target esp32
#RT|
#XK|# 编译固件
#PM|idf.py build
#BJ|```
#MS|
#VR|### 烧录
#ZT|
#BV|```bash
#BS|# 烧录固件并启动监视器
#ZJ|idf.py -p /dev/ttyUSB0 flash monitor
#ZR|```
#PJ|
#QK|将 `/dev/ttyUSB0` 替换为您的串口设备。
#NJ|
#TK|### 首次配置
#HT|
#PW|首次启动时，设备进入 AP 模式：
#YQ|
#HT|1. 连接 WiFi 网络 **MiBeeCam**（密码：`12345678`）
#XB|2. 在浏览器中打开 **http://192.168.4.1**
#ZP|3. 进入配置页面，输入您的 WiFi SSID 和密码
#NJ|4. 保存配置 — 设备重启并以 STA 模式连接到您的网络
#PP|
#KT|### 恢复出厂设置
#PV|
#JB|⚠️ **重要说明**：由于 GPIO0 被用作摄像头 XCLK 引脚，无法可靠检测 BOOT 按钮按下。请使用 `POST /api/reset` API 端点来恢复出厂设置。
#BQ|
#BM|#YK|## REST API
#SQ|#YR|
#JS|#VB|| 方法 | 路径 | 描述 |
#ZZ|#HY||------|------|------|
#XR|#YB|| GET | `/api/status` | 获取设备状态 JSON |
#XW|#YP|| GET | `/api/config` | 获取当前配置 JSON |
#XY|#ZX|| POST | `/api/config` | 更新设备配置 |
#TR|#PZ|| POST | `/api/reset` | 恢复出厂设置 |
#JY|#YN|| POST | `/api/reboot` | 重启设备 |
#MX|#MP|| GET | `/capture` | 单张 JPEG 照片拍摄 |
#ZY|#YW|| GET | `/stream` | MJPEG 实时视频流 |
#QY|#YX|| GET | `/metrics` | Prometheus 系统指标 |
#QW|#MH|| GET | `/api/files` | 列出 SD 卡照片文件 |
#QW|#WH|| GET | `/api/download?name=xxx` | 下载指定照片 |
#BK|#BN||
#JX|#QW|| POST | `/api/record?action=start|stop` | 录制控制（开始/停止） |
#WZ|#QP|| GET | `/api/record` | 录制状态和信息 |
#NW|#QW|| GET | `/api/storage` | 存储使用情况和清理状态 |
#KR|
#RQ|## 项目结构
#XS|
#XZ|```
#HR|mibee-cam/
#VS|├── main/
#RH|│   ├── main.c              # 系统入口，16步启动序列
#RS|│   ├── camera_driver.c/h   # OV2640 摄像头驱动（DMA 优化）
#TX|│   ├── wifi_manager.c/h    # WiFi AP/STA 管理（带回调）
#XZ|│   ├── config_manager.c/h  # NVS 配置持久化（版本 9）
#KT|│   ├── web_server.c/h      # HTTP 服务器 + REST API + SPIFFS
#RH|│   ├── mjpeg_streamer.c/h  # MJPEG 实时流传输（异步）
#WR|│   ├── storage_manager.c/h # SD 卡存储（热插拔检测）
#JM|│   ├── motion_detect.c/h   # 运动检测
#HX|│   ├── status_led.c/h      # 状态 LED 驱动
#SQ|│   ├── time_sync.c/h       # NTP 时间同步
#BY|│   ├── health_monitor.c/h  # Prometheus 指标收集
#ZW|│   ├── video_recorder.c/h  # AVI 视频录制（3种模式）
#JT|│   ├── timelapse.c/h       # 延时摄影功能
#RV|│   ├── flash_led.c/h       # 闪光灯控制
#XZ|│   ├── serial_config.c/h   # 串口配置接口
#WR|│   └── web_ui/            # Web 界面文件
#HP|├── CMakeLists.txt
#JH|├── sdkconfig.defaults
#TP|├── partitions.csv
#MB|├── main/idf_component.yml # 组件依赖
#VP|└── .github/workflows/    # CI/CD 流水线
#TN|```
#NV|
#XM|## 启动序列
#TT|
#VS|固件采用 **16 步启动序列**：
#BN|
#YB|1. **NVS 闪存初始化** — 配置存储准备
#BK|2. **配置加载** — 从 NVS 加载设置（版本 9）
#QB|3. **状态 LED 初始化** — GPIO33 系统状态指示
#SN|4. **SPIFFS 挂载** — Web 界面文件系统准备
#YS|5. **WiFi 子系统初始化** — 网络接口和事件循环
#WK|6. **WiFi 状态回调注册** — 网络状态变化监听
#ZP|7. **健康监控初始化** — 系统指标收集
#NP|8. **WiFi 模式选择** — STA 已配置则连接，否则启动 AP
#BT|9. **MJPEG 流传输器初始化** — 实时视频流准备
#NQ|10. **Web 服务器启动** — HTTP 服务和 API 端点
#TW|11. **时间同步初始化** — NTP 客户端（仅 STA 模式）
#WS|12. **运动检测启动** — 帧监控服务（仅 STA 模式）
#XR|13. **视频录制器初始化** — AVI 录制系统启动
#JH|14. **延时摄影启动** — 爆发拍摄服务
#XP|15. **闪光灯控制初始化** — PWM 闪光灯驱动
#VJ|16. **串口配置启动** — 串行配置接口
#JZ|
#TN|### 重要说明
#MH|
#QW|- **GPIO14 共享限制**：SD 卡 CLK 和摄像头 XCLK 共享 GPIO14，初始化顺序非常关键
#ZW|- 必须先释放 SD 卡，再初始化摄像头，最后重新初始化 SD 卡以避免时序冲突
#KN】- **摄像头初始化被延迟到 WiFi STA 连接后**，这是已知的 ESP32 DMA 冻结问题的 workaround（esp32-camera#620）
#WX】- 在 STA 模式下，MJPEG、时间同步、Web 服务器和运动检测等功能将在 WiFi 连接完成后异步启动
#JM|- 项目中 **不包含 NAS 上传和 WebDAV 客户端功能**，只能通过 SD 卡手动下载照片
#JM|- 项目中 **不包含 ONVIF、WebSocket、Webhook 等功能**
#JM|
#YW|## 配置
#PX|
#NJ|#HX|所有设置存储在 NVS 中（配置版本 9），可通过 Web 界面或 REST API 访问。
#XQ|
#MW|### WiFi 配置
#ST|- **ssid** — WiFi 网络名称
#VP|- **wifi_pass** — WiFi 密码（API 出于安全考虑不会返回此字段）
#ZX|- **dual_ssid** — 第二 WiFi 网络名称（可选）
#VP|- **dual_pass** — 第二 WiFi 密码（可选）
#ZV|
#RH|### 设备配置
#ZY|- **device_name** — 仪表板显示的设备名称（默认：`MiBeeCam`）
#JB|- **web_password** — 可选的 Web 界面访问密码（留空 = 无需认证）
#KZ|
#WK|### 摄像头配置
#YB|- **resolution** — 图像分辨率：0=VGA, 1=SVGA, 2=XGA, 3=UXGA（默认：SVGA）
#YP|- **quality** — JPEG 质量 1-63，数值越低质量越好（默认：10）
#QV|
#WX|### 运动检测
#SX|- **motion_enabled** — 启用/禁用运动检测（默认：启用）
#PM|- **motion_threshold**（1-100）— 触发运动的帧差异百分比（默认：30）
#JR|- **motion_cooldown**（1-60 秒）— 连续触发之间的冷却时间（默认：5）
#MK|
#WX|### 亮度检测与智能闪光
#ZS|固件使用**灰度像素探测**检测场景亮度——这是 OV2640 传感器最可靠的方法。当在暗场景中检测到运动时，闪光灯自动激活拍照。
#XJ|
#SQ|- **flash_threshold**（0-100）— 触发闪光的亮度百分比阈值（默认：40）
#HB|  - **0** = 闪光禁用
#BZ|  - **34-40** = 推荐室内使用
#ZV|  - **100** = 每次都触发闪光
#NQ|
#BP|### 时间同步
#PN|- **timezone** — 时区字符串（如 `CST-8` 表示北京时间）
#MY|
#ST|### 延时摄影
#QJ|- **timelapse_enabled** — 启用/禁用延时摄影（默认：禁用）
#NW|- **timelapse_interval_s** — 拍摄间隔秒数（默认：30）
#BS|- **timelapse_burst_count** — 每次拍摄的图片数量（默认：3）
#HS|
#YZ|### API 使用示例
#QW|
#BV|```bash
#KX|# 读取当前配置
#WP|curl http://DEVICE_IP/api/config
#VJ|
#JK|# 设置闪光阈值为 60（对较暗光线更敏感）
#QQ|curl -X POST http://DEVICE_IP/api/config \
#TM|  -H 'Content-Type: application/json' \
#XP|  -d '{"flash_threshold":60}'
#MT|
#TX|# 查看亮度状态
#TM|curl http://DEVICE_IP/api/status | jq '.data | {brightness_pct, brightness_method, scene_dark}'
#MX|
#KM|# 查看 Prometheus 指标
#HJ|curl http://DEVICE_IP/metrics | grep brightness
#XK|
#RX|# 开始视频录制
#HT|curl -X POST http://DEVICE_IP/api/record?action=start
#VW|# 停止视频录制
#WY|curl -X POST http://DEVICE_IP/api/record?action=stop
#YH|# 查看录制状态
#TJ|curl http://DEVICE_IP/api/record
#MY|```
#RY|
#BZ|## 重要注意事项
#ZB|
#ST|1. **GPIO0 功能限制**：GPIO0 被用作摄像头 XCLK 引脚，无法用作 BOOT 按钮输入
#PB|2. **摄像头初始化时序**：为了避免 ESP32 WiFi STA 启动时的 DMA 冻结问题，摄像头初始化被延迟到 WiFi 连接完成后
#HV|3. **SD 卡与摄像头共享**：GPIO14 在 SD 卡 CLK 和摄像头 XCLK 之间共享，初始化顺序非常重要
#VN|4. **无 NAS 支持**：当前版本不支持自动上传到 NAS，只能通过 SD 卡手动下载照片
#QY|5. **无测试覆盖**：目前项目没有任何单元测试或集成测试，所有验证依赖运行时检查
#MH|
#BH|## 许可证
#NB|
#ZM|MIT License
#WY|
#KT|## 贡献
#QT|
#ZT|欢迎提交 Issue 和 Pull Request 来改进这个项目！请确保：
#XQ|
#HS|1. 代码符合项目的编码规范
#RN|2. 添加适当的注释和文档
#BT|3. 测试您的更改
#VT|4. 更新相关的 README 文档
#XR|
#WH|## 支持
#BV|
#NH|如果您遇到问题或有建议，请：
#VK|
#MN|1. 查看 [文档](docs/zh/) 目录
#QZ|2. 提交 GitHub Issue
#HS|3. 查看已知问题和解决方案