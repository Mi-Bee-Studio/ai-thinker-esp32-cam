#SX|[![GitHub release](https://img.shields.io/github/v/release/Mi-Bee-Studio/mibee-cam?include_prereleases&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/releases)
#RZ|[![GitHub stars](https://img.shields.io/github/stars/Mi-Bee-Studio/mibee-cam?style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam)
#ZV|[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/actions)
#WP|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue?style=flat-square)](https://docs.espressif.com/projects/esp-idf/en/latest/)
#WR|[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)
#SY|
#KW|> 🌐 [English Documentation](../en/architecture.md)
#XW|
#KQ|# 架构
#SK|
#QJ|本文档提供 MiBee Cam 固件的全面架构概述，包括模块组织、启动序列、数据流和系统设计。
#TX|
#NP|## 系统概述
#BY|
#RW|MiBee Cam 固件是一个专为摄像头应用程序设计的实时嵌入式系统，具有 WiFi 连接和存储功能。架构采用模块化设计，具有清晰的关注点分离。
#VP|
#XN|### 关键特性
#KS|
#QJ|- **实时处理**：并发摄像头捕获和流传输
#NX|- **资源受限**：针对 4MB 闪存 + 4MB PSRAM 进行优化
#QR|- **联网**：带有 AP/STA 双模式的 WiFi 连接性
#MS|- **模块化**：14 个独立模块，具有定义的接口
#RN|- **事件驱动**：具有状态机的异步操作
#KW|
#JS|## 模块架构
#HK|
#JZ|```
#PZ|┌─────────────────────────────────────────────────────────────┐
#ZQ|│                    应用程序层                              │
#JN|├─────────────────────────────────────────────────────────────┤
#TZ|│  Web 服务器    │ MJPEG 流传输器 │ 运动检测 │     │
#NH|│  (HTTP + API)  │   (流传输)    │   (捕获) │     │
#MZ|├─────────────────────────────────────────────────────────────┤
#NP|│                    硬件接口层                             │
#SK|├─────────────────────────────────────────────────────────────┤
#SH|│  摄像头驱动器  │  存储管理器  │   WiFi 管理器 │  时间同步  │
#PM|│    (OV2640)     │  (SD 卡)     │ (AP/STA)    │   (NTP)    │
#QN|├─────────────────────────────────────────────────────────────┤
#TT|│                    系统核心层                             │
#JQ|├─────────────────────────────────────────────────────────────┤
#ZB|│  配置管理器    │ 健康监控器   │ 状态 LED │     主程序   │
#SR|│    (NVS)       │   (指标)     │   (GPIO) │ (启动序列) │
#MX|├─────────────────────────────────────────────────────────────┤
#RX|│                  ESP-IDF 框架                            │
#KW|├─────────────────────────────────────────────────────────────┤
#NP|│  FreeRTOS      │ LWIP         │ SPIFFS   │ NVS        │
#TR|│  (任务/RTOS)   │  (网络)      │ (存储)   │ (配置)     │
#KX|└─────────────────────────────────────────────────────────────┘
#SY|```
#NM|
#ZH|## 详细模块描述
#YJ|
#MZ|### 1. 主模块（`main.c`）
#KT|**职责**：系统协调和启动序列
#WS|- **功能**：16 步初始化，任务协调
#XM|- **文件**：`main.c`
#QR|- **依赖项**：所有其他模块
#BZ|- **内存**：堆栈大小 8KB
#BB|- **优先级**：高（任务创建和协调）
#RJ|
#XR|### 2. 摄像头驱动器（`camera_driver.c/h`）
#XQ|**职责**：OV2640 摄像头控制和帧捕获
#VR|- **功能**：初始化、JPEG 捕获、分辨率控制
#TK|- **内存**：PSRAM 用于帧缓冲
#HZ|- **关键**：必须在 WiFi 之前初始化（I2C 总线冲突）
#JP|- **GPIO**：使用摄像头特定引脚（XCLK=0 等）
#KB|
#SH|### 3. WiFi 管理器（`wifi_manager.c/h`）
#YS|**职责**：WiFi 连接管理
#KZ|- **功能**：AP/STA 模式、自动重连、回调注册
#SW|- **网络**：处理 IP 分配和连接性
#XV|- **模式**：操作使用 STA，回退使用 AP
#TH|- **事件**：状态变化触发其他模块
#JW|
#QZ|### 4. 配置管理器（`config_manager.c/h`）
#VB|**职责**：配置持久化和管理
#PY|- **存储**：NVS 键值存储
#KR|- **迁移**：版本间自动迁移
#KB|- **默认值**：首次启动的合理默认值
#QX|- **验证**：配置完整性检查
#YX|
#VT|### 5. Web 服务器（`web_server.c/h`）
#ZB|**职责**：HTTP 服务器和 REST API
#BX|- **端口**：TCP 端口 80
#XV|- **端点**：状态、配置、捕获、流、指标、文件
#TX|- **静态文件**：从 SPIFFS 分区提供服务
#VZ|- **认证**：写操作的密码保护（可选）
#QT|
#BW|### 6. MJPEG 流传输器（`mjpeg_streamer.c/h`）
#MJ|**职责**：实时视频流传输
#JW|- **协议**：multipart/x-mixed-replace
#XQ|- **帧率**：目标 15 FPS，带限流
#PQ|- **客户端**：最多 2 个并发流
#QP|- **缓冲**：基于帧的双缓冲
#ZT|
#HN|### 7. 存储管理器（`storage_manager.c/h`）
#SW|**职责**：SD 卡照片存储
#BS|- **接口**：SPI（SDSPI）
#TS|- **文件系统**：通过 FatFs 的 FAT32
#ZY|- **组织**：基于日期的目录
#JQ|- **清理**：当可用空间 < 20% 时循环清理
#NJ|
#MY|### 8. 运动检测（`motion_detect.c/h`）
#QV|**职责**：运动检测、亮度感知和自动闪光
#ZZ|- **算法**：基于 JPEG 帧差，可配置阈值
#TS|- **亮度检测**：灰度像素探测（主要）+ JPEG 大小回退
#TK|- **自动闪光**：GPIO4 LEDC PWM（~80% 占空比，MiBee 硬件安全值）
#HW|- **触发**：保存照片到 SD 卡
#BM|- **可配置**：阈值、冷却时间、flash_threshold（亮度百分比）
#QJ|
#MS|### 9. 状态 LED（`status_led.c/h`）
#KM|**职责**：系统状态指示
#TX|- **GPIO**：GPIO33（低电平有效）
#ZS|- **模式**：每种状态的不同闪烁模式
#HN|- **状态**：启动、WiFi 连接、运行、错误、AP 模式
#RM|
#JW|### 10. 时间同步（`time_sync.c/h`）
#WY|**职责**：时间和日期管理
#ZR|- **协议**：使用 NTP 服务器的 SNTP
#RT|- **时区**：可配置的 POSIX 时区
#QH|- **准确性**：在 1 秒内同步
#JQ|- **用例**：照片和日志的时间戳
#YB|
#MT|### 11. 健康监控器（`health_monitor.c/h`）
#QN|**职责**：系统健康跟踪
#VQ|- **指标**：堆栈、PSRAM、任务堆栈使用情况
#VP|- **输出**：兼容 Prometheus 的 /metrics 端点
#XS|- **间隔**：60 秒监控
#RN|- **日志记录**：定期健康报告
#QZ|
#BQ|### 12. 视频录制器（`video_recorder.c/h`）
#MW|**职责**：AVI 视频录制，支持多种模式
#YN|- **模式**：连续、延时、动态
#HX|- **存储**：SD 卡上的分段 AVI 文件
#WK|- **配置**：录制模式、分段时长
#KK|
#MQ|### 13. 延时摄影（`timelapse.c/h`）
#JT|**职责**：延时摄影功能实现
#ZS|- **模式**：连续定时拍摄、连拍模式
#SM|- **存储**：照片保存到 SD 卡
#RS|- **配置**：间隔时间、连拍数量
#NV|
#NV|### 14. Common（`common.h`）
#JX|**职责**：共享类型、引脚定义、配置结构体
#XS|- **引脚映射**：CAM_PIN_*、SD_PIN_*
#TV|- **配置结构体**：cam_config_t
#QW|- **枚举**：wifi_state_t、led_state_t
#TJ|
#NV|## 启动序列
#BT|
#XM|固件遵循仔细排序的 **16 步启动序列**，以避免硬件冲突并确保正确初始化：
#BH|
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
#TT|14. **延时摄影初始化** — 延时拍摄功能启动
#ZY|15. **延时摄影启动** — 延时拍摄服务启动
#VM|16. **系统监控和服务** — 后台任务和服务启动
#MV|
#JS|### 启动步骤详情
#WB|
#SM|**步骤 1-4：基础**
#NQ|- NVS 初始化用于配置
#SX|- LED 设置用于视觉反馈
#HM|- SPIFFS 挂载用于 Web 界面
#JZ|
#HP|**步骤 5-8：硬件设置**
#VR|- WiFi 子系统设置
#SV|- 健康监控启动
#JX|- WiFi 模式选择（如果已配置则 STA，否则 AP）
#MK|
#VX|**步骤 9-12：网络服务**
#WQ|- MJPEG 流传输服务
#HR|- Web 服务器启动（端口 80）
#PJ|- 时间同步（仅 STA 模式）
#WW|- 运动检测（仅 STA 模式）
#TZ|
#NK|**步骤 13-16：应用程序服务**
#ZT|- 视频录制器初始化
#JH|- 延时摄影初始化
#TW|- 延时摄影启动
#NM|- 系统监控和服务
#NV|
#SY|## 数据流架构
#KQ|
#MK|### 摄像头数据流
#ZH|```
#SN|OV2640 摄像头 → 摄像头驱动器 → 帧缓冲
#RX|    ↓
#YT|JPEG 编码 → MJPEG 流传输器 → 网络客户端
#ZP|    ↓
#ZW|存储管理器 → SD 卡（照片）
#RJ|    ↓
#NX|运动检测 → SD 卡存储（照片）
#RZ|    ↓
#TM|延时摄影 → SD 卡（定时照片）
#WH|```
#SV|
#JS|### 网络数据流
#BB|```
#SS|WiFi 接口 → LWIP 栈 → Web 服务器
#XQ|    ↓
#NK|HTTP 请求 → REST API → 模块处理器
#QH|    ↓
#YK|响应 → 客户端浏览器/移动设备
#WB|```
#ZQ|
#KW|### 配置数据流
#RS|```
#TB|Web 界面 → HTTP POST → Web 服务器 → 配置管理器
#RX|    ↓
#WQ|NVS 存储 → 配置管理器 → 运行时模块
#YR|    ↓
#HK|运行时更改 → 配置管理器 → NVS 更新
#KV|```
#PX|
#HY|## 内存架构
#YZ|
#MS|### 闪存（4MB）
#NK|```
#YX|0x00000-0x0FFFF:    引导加载程序 (92KB)
#ZP|0x10000-0x25FFFF:   应用程序 (~2.5MB)
#YH|0x260000-0x3DFFFF:  SPIFFS (~1.2MB)
#ZB|0x3E0000-0x3FFFFF:  NVS (24KB)
#MQ|```
#QY|
#QX|### PSRAM（4MB）
#TH|```
#TM|0x000000-0x1EFFFF:  摄像头帧缓冲区 (2MB)
#YB|0x1F0000-0x3FFFFF:  其他使用可用 (2MB)
#QB|```
#BX|
#VW|### 堆栈内存
#PP|- **主任务**：8KB（启动序列）
#WH|- **WiFi 任务**：8KB（网络操作）
#VP|- **摄像头任务**：8KB（捕获操作）
#ZS|- **后台任务**：每个 4KB（监控等）
#BV|
#QZ|## 并发模型
#VK|
#VT|### 任务概述
#MB|```c
#VM|// 主任务（main.c）
#TB|- 启动序列协调
#SM|- 模块初始化
#WP|- 系统协调
#NX|
#ZQ|// WiFi 任务（wifi_manager.c）
#WN|- 网络连接管理
#XH|- 后台操作
#VX|
#XN|// 摄像头任务（camera_driver.c）
#YR|- 摄像头捕获操作
#JV|- 帧处理
#PV|
#MN|// HTTP 服务器任务（web_server.c）
#VP|- HTTP 请求处理
#JX|- Web 界面服务
#NW|
#NZ|// 后台任务
#ZN|- 健康监控（60 秒间隔）
#WN|- 运动检测（连续）
#KH|- SD 卡监控（热插拔检测）
#KH|- SD 卡监控（热插拔检测）
#XB|```
#BJ|
#VN|### 同步原语
#JW|
#YT|#### 互斥锁
#WW|- **摄像头访问**：保护帧缓冲访问
#SP|- **SD 卡访问**：防止 GPIO14 冲突
#YY|- **配置访问**：线程安全的配置访问
#MV|
#MJ|#### 信号量
#MY|- **流客户端**：限制并发流（最多 2 个）
#VT|- **上传队列**：同步上传处理
#TB|- **WiFi 事件**：同步状态变化
#TT|
#SM|#### FreeRTOS 功能
#SR|- **队列**：任务间通信
#RJ|- **定时器**：定期任务执行
#MY|- **事件组**：异步事件处理
#NN|
#NB|## 错误处理和恢复
#XN|
#PK|### 错误类别
#KQ|1. **硬件错误**：摄像头初始化失败、SD 卡错误
#PQ|2. **网络错误**：WiFi 断开连接、上传失败
#KX|3. **资源错误**：内存耗尽、堆栈溢出
#ZX|4. **配置错误**：无效设置、NVS 损坏
#TY|
#MS|### 恢复策略
#BZ|- **WiFi**：指数退避的自动重连
#KJ|- **上传**：3 次尝试的重试机制
#NH|- **摄像头**：软复位和重新初始化
#WN|- **系统**：用于任务恢复的看门狗定时器
#XK|
#SR|### 错误指示器
#VB|- **LED 模式**：不同错误类型的不同闪烁序列
#XX|- **串口日志**：带时间戳的详细错误消息
#JM|- **Web 仪表板**：错误状态指示器
#RZ|- **API 响应**：带有详细信息的 HTTP 错误代码
#XJ|
#VP|## 性能特征
#SQ|
#VR|### CPU 使用率
#BQ|- **空闲**：约 2% CPU 利用率
#NB|- **流传输**：VGA 分辨率下 15 FPS，约 40% CPU
#KR|- **运动检测**：持续约 5% CPU
#HT|- **上传**：传输期间约 15% CPU
#JS|
#PB|### 内存使用
#VV|- **空闲堆栈**：需要最少 20KB
#YM|- **PSRAM 使用**：根据分辨率摄像头使用 600KB-2.4MB
#VV|- **堆栈**：主任务 8KB，其他任务 4KB-8KB
#VR|- **静态**：编译代码约 50KB
#SW|
#MJ|### 网络性能
#HW|- **MJPEG 流**：15 FPS VGA，约 500KB/s
#QN|- **HTTP API**：<100ms 响应时间
#XN|- **WiFi 吞吐量**：约 1-2 Mbps 持续
#HN|- **上传速度**：取决于网络条件
#RR|
#HQ|## GPIO 和硬件约束
#ZS|
#YV|### 关键约束
#NV|1. **GPIO14 共享**：摄像头 XCLK / SD 卡 CLK 需要时间复用
#XS|2. **I2C 总线冲突**：摄像头和 WiFi 不能同时使用 I2C
#HS|3. **仅输入 GPIO**：34,35,36,39 不能用作输出
#HN|4. **LED 极性**：GPIO33 是低电平有效
#ZY|
#KX|### 硬件资源管理
#KY|- **PSRAM**：专用于摄像头帧缓冲
#TS|- **闪存**：为固件 + 存储仔细分区
#YS|- **SPI 总线**：在摄像头和 SD 卡之间共享（时间复用）
#BP|- **电源管理**：动态频率调节以节省功率
#HR|
#PB|## 可扩展性和自定义
#TN|
#HN|### 模块接口
#TP|所有模块遵循一致的接口模式：
#ZS|- **初始化/反初始化**：标准生命周期方法
#TM|- **配置**：集中配置管理
#PW|- **错误处理**：一致的错误报告
#XH|- **日志记录**：带级别的结构化日志
#JH|
#BT|### 配置选项
#ZY|- **功能门**：基于 Kconfig 的功能选择
#WT|- **运行时配置**：动态参数调整
#HK|- **持久设置**：基于 NVS 的配置持久化
#WP|- **Web 界面**：用户友好的配置管理
#KZ|
#MY|### 扩展点
#JX|- **额外摄像头**：不同传感器的接口抽象
#KY|- **存储系统**：不同后端的插件架构
#MN|- **网络协议**：可扩展的上传框架
#KJ|- **Web 界面**：模块化的 HTML/CSS/JavaScript 结构