#SX|[![GitHub release](https://img.shields.io/github/v/release/Mi-Bee-Studio/mibee-cam?include_prereleases&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/releases)
#RZ|[![GitHub stars](https://img.shields.io/github/stars/Mi-Bee-Studio/mibee-cam?style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam)
#ZV|[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/actions)
#WP|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue?style=flat-square)](https://docs.espressif.com/projects/esp-idf/en/latest/)
#WR|[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)
#SY|
#QR|> 🌐 [English Documentation](../en/hardware.md)
#XW|
#PB|# 硬件说明
#SK|
#WB|本文档提供 MiBee Cam 固件的详细硬件规格、引脚映射和技术信息。
#TX|
#KX|## MiBee Cam 主板
#BY|
#WW|MiBee Cam 是一款专为摄像头应用设计的开发板，带有 ESP32 芯片和摄像头接口。
#VP|
#NZ|### 主板规格
#KS|
#YM|| 参数 | 规格 |
#MZ||-----------|---------------|
#YQ|| **主控制器** | ESP32（原始版，不是 ESP32-S3） |
#ZN|| **CPU** | 双核 Tensilca Xtensa LX6 @ 240 MHz |
#RY|| **闪存** | 4MB（4 MB 闪存） |
#KT|| **PSRAM** | 4MB（4 MB PSRAM，八线 SPI） |
#PT|| **WiFi** | 2.4 GHz（802.11 b/g/n） |
#ZQ|| **蓝牙** | v4.2 BR/EDR |
#MK|| **GPIO** | 33 个 GPIO 引脚 |
#WK|| **ADC** | 12 位，18 通道 |
#WM|| **DAC** | 2 通道 |
#JT|| **摄像头接口** | 8 位并行 + 控制信号 |
#YK|| **USB** | USB-C 用于供电和编程 |
#JQ|
#SM|### 摄像头接口
#WV|
#SN|该板包含 OV2640 摄像头接口，引脚配置如下：
#MV|
#NR|| 引脚 | GPIO | 功能 | 描述 |
#XB||-----|------|----------|-------------|
#HS|| XCLK | 0 | 主时钟 | 摄像头主时钟，20 MHz |
#BW|| SIOD | 26 | I2C 数据 | 摄像头 I2C 数据线（SDA） |
#MB|| SIOC | 27 | I2C 时钟 | 摄像头 I2C 时钟线（SCL） |
#KH|| D0 | 5 | 并行数据 | 摄像头并行数据位 0 |
#NH|| D1 | 18 | 并行数据 | 摄像头并行数据位 1 |
#QB|| D2 | 19 | 并行数据 | 摄像头并行数据位 2 |
#HZ|| D3 | 21 | 并行数据 | 摄像头并行数据位 3 |
#JH|| D4 | 36 | 并行数据 | 摄像头并行数据位 4（仅输入） |
#PQ|| D5 | 39 | 并行数据 | 摄像头并行数据位 5（仅输入） |
#TS|| D6 | 34 | 并行数据 | 摄像头并行数据位 6（仅输入） |
#RR|| D7 | 35 | 并行数据 | 摄像头并行数据位 7（仅输入） |
#YT|| VSYNC | 25 | 垂直同步 | 垂直同步信号 |
#QH|| HREF | 23 | 水平参考 | 水平参考信号 |
#HK|| PCLK | 22 | 像素时钟 | 摄像头像素时钟输出 |
#NT|| PWDN | 32 | 断电 | 摄像头电源控制（高电平有效） |
#NR|| RESET | -1 | 复位 | 摄像头复位（未连接） |
#TW|
#MQ|### SD 卡接口
#WH|
#YZ|该板通过 SPI 接口支持 SD 卡存储：
#QH|
#NR|| 引脚 | GPIO | 功能 | 描述 |
#PR||-----|------|----------|-------------|
#NS|| SD_CS | 13 | 片选 | SD 卡 SPI 片选 |
#BP|| SD_CLK | 14 | SPI 时钟 | SD 卡 SPI 时钟线（与摄像头共享） |
#NB|| SD_MOSI | 15 | SPI MOSI | SD 卡 SPI 输出 |
#SS|| SD_MISO | 2 | SPI MISO | SD 卡 SPI 输入 |
#JQ|
#PK|### GPIO 约束和共享
#RT|
#MZ|#### 关键 GPIO14 约束
#YY|
#RM|**GPIO14 (SD_CLK)** 在摄像头和 SD 卡接口之间共享，需要仔细的时间管理：
#SV|
#JM|- **摄像头使用**：当摄像头激活（捕获/初始化）时，GPIO14 用作 SD_CLK
#SZ|- **SD 卡使用**：当访问 SD 卡时，GPIO14 用作摄像头 XCLK
#ZR|- **时间复用**：固件通过以下方式处理：
#TP|  1. 在摄像头操作前取消初始化 SD 卡
#TN|  2. 在摄像头操作完成后重新初始化 SD 卡
#HN|- **影响**：这意味着摄像头和 SD 卡不能同时使用
#JH|- **后果**：不能进行 AVI 视频录制（需要同时使用摄像头和 SD 卡）
#KB|
#SZ|#### 仅输入 GPIO 引脚
#YR|
#RN|以下 GPIO 引脚仅用于输入，不能用作输出：
#WR|
#KW|| GPIO | 引脚号 | 典型用途 |
#PQ||------|------------|-------------|
#BS|| 34 | GPIO34 | 摄像头 D4（并行数据） |
#RM|| 35 | GPIO35 | 摄像头 D6（并行数据） |
#MR|| 36 | GPIO36 | 摄像头 D4（并行数据） |
#MS|| 39 | GPIO39 | 摄像头 D5（并行数据） |
#RT|
#TQ|### LED 指示灯
#BX|
#PZ|该板包含两个 LED 指示灯：
#MS|
#ZW|| LED | GPIO | 类型 | 功能 |
#KW||-----|------|------|----------|
#VS|| 状态 LED | 33 | 输出（低电平有效） | 系统状态指示器 |
#QH|| 闪光灯 LED | 4 | PWM | 摄像头闪光控制 |
#SR|
#TZ|#### 状态 LED 状态
#PJ|
#YR|| 状态 | 模式 | 描述 |
#YV||-------|---------|-------------|
#RR|| 启动中 | 常亮 | 系统启动 |
#YR|| WiFi 连接中 | 闪烁 | 连接到 WiFi |
#JR|| 运行中 | 常亮 | 系统正常运行 |
#YN|| 错误 | 快速闪烁 | 错误条件 |
#PB|| AP 模式 | 慢速闪烁 | AP 模式激活 |
#WY|
#MB|### 电源要求
#QJ|
#YM|| 参数 | 规格 |
#HP||-----------|---------------|
#SJ|| **输入电压** | 5V DC |
#YS|| **工作电压** | 3.3V |
#RH|| **最大电流** | 500mA（摄像头操作期间峰值） |
#SY|| **推荐电源** | 5V/1A USB 适配器 |
#YR|
#ZJ|### 启动按钮
#QY|
#PK|| 引脚 | 功能 | 描述 |
#JS||-----|----------|-------------|
#HJ|GPIO0 | 启动按钮 | ⚠️ MiBee 板上物理按钮功能**已禁用**（GPIO0 = 摄像头 XCLK，按下检测不可靠）。出厂重置请改用 `POST /api/reset`。GPIO0 启动模式选择（引导加载程序进入）在硬件层仍有效。 |
#YB|
#SJ|## OV2640 摄像头模块
#XB|
#RM|固件支持 OV2640 摄像头传感器，这是一个 200 万像素的摄像头模块。
#HP|
#XZ|### OV2640 规格
#WP|
#YM|| 参数 | 规格 |
#JQ||-----------|---------------|
#NZ|| **分辨率** | 200 万像素（1600×1200） |
#KP|| **色彩深度** | RGB/YUV |
#SP|| **输出格式** | JPEG、原始 RGB、YUV420 |
#YJ|| **镜头类型** | 固定焦距 |
#HX|| **传感器尺寸** | 1/4 英寸 |
#PR|| **帧率** | 较低分辨率下最高 30 FPS |
#XS|
#SY|### 支持的分辨率
#HQ|
#TT|| 分辨率 | 代码 | 尺寸 | 推荐帧率 |
#RJ||------------|------|------------|----------------|
#YP|| VGA | 0 | 640×480 | 15-30 |
#NX|| SVGA | 1 | 800×600 | 10-25 |
#BS|| XGA | 2 | 1024×768 | 8-20 |
#WV|| UXGA | 3 | 1600×1200 | 3-10 |
#TJ|
#TP|### 内存要求
#HV|
#HN|摄像头需要 PSRAM 进行帧缓冲：
#VX|
#BM|| 分辨率 | 帧大小 | 缓冲数量 | 总内存 |
#VN||------------|------------|-------------|--------------|
#HV|| VGA (640×480) | ~300KB | 2 | ~600KB |
#PW|| SVGA (800×600) | ~450KB | 2 | ~900KB |
#KB|| XGA (1024×768) | ~700KB | 2 | ~1.4MB |
#QR|| UXGA (1600×1200) | ~1.2MB | 2 | ~2.4MB |
#ZB|
#NK|**总 PSRAM 可用**：4MB  
#NX|**摄像头使用 PSRAM**：UXGA 分辨率最多 2.4MB  
#NP|**其他使用可用**：最少 1.6MB
#SK|
#VN|## 闪存布局
#QB|
#MH|4MB 闪存组织如下：
#BT|
#MX|| 分区 | 类型 | 偏移量 | 大小 | 描述 |
#VQ||-----------|------|--------|------|-------------|
#PB|| **nvs** | data/nvs | 0x9000 | 24KB | NVS 键值存储 |
#NB|| **phy_init** | data/phy | 0xf000 | 4KB | PHY 初始化数据 |
#KN|| **factory** | app/factory | 0x10000 | ~2.5MB | 主应用程序固件 |
#KN|| **spiffs** | data/spiffs | 0x260000 | ~1.2MB | Web 界面和照片元数据 |
#BN|
#YW|### 固件大小考虑
#HM|
#MZ|| 组件 | 大小 | 说明 |
#MJ||-----------|------|-------|
#RS|| 应用程序 | ~1.97MB | 主固件（PSRAM 摄像头支持） |
#TM|| 引导加载程序 | ~92KB | ESP-IDF 标准引导加载程序 |
#SB|| 分区表 | ~0.4KB | 闪存布局定义 |
#MJ|| NVS 存储 | 24KB | 配置存储 |
#YM|| SPIFFS | ~1.2MB | Web 界面，缓存照片元数据 |
#TK|| **总使用** | **~3.3MB** | **13MB 可用** |
#QY|| **空闲空间** | **~0.7MB** | **可用于扩展** |
#YV|
#JJ|## 电源管理
#RS|
#SN|### 电源消耗模式
#BH|
#KP|| 模式 | 电流 | 描述 |
#NJ||------|---------|-------------|
#BH|| 深度睡眠 | ~10µA | WiFi 关闭，最小功耗 |
#HW|| 浅度睡眠 | ~150µA | WiFi 睡眠模式 |
#BB|| 空闲 | ~20mA | 系统空闲，WiFi 连接 |
#WR|| 摄像头激活 | ~250mA | 摄像头操作 |
#KH|| 流传输 | ~300mA | MJPEG 流传输 |
#BN|
#PN|### 电压监控
#PZ|
#TT|固件包括电压监控功能：
#KM|- 工作电压范围：3.0V - 3.6V
#MS|- 欠压检测已启用
#WT|- 欠压复位保护
#XQ|
#BY|## 温度考虑因素
#NZ|
#YM|| 参数 | 规格 |
#QZ||-----------|---------------|
#BS|| **工作温度** | -40°C 到 +85°C |
#QW|| **存储温度** | -40°C 到 +125°C |
#PM|| **热节流** | 在 85°C CPU 温度时开始 |
#YQ|| **温度监控** | 通过 `/metrics` 端点可用 |
#XJ|
#KT|## 引脚映射总结
#BB|
#NJ|### 摄像头引脚（固定）
#QK|```
#HP|XCLK  → GPIO0
#YJ|SIOD  → GPIO26
#BW|SIOC  → GPIO27
#MH|D0-D3 → GPIO5,18,19,21
#BY|D4-D7 → GPIO36,39,34,35
#PM|VSYNC → GPIO25
#JQ|HREF  → GPIO23
#QX|PCLK  → GPIO22
#TY|PWDN  → GPIO32
#SM|RESET → NC（未连接）
#HH|```
#NQ|
#WT|### SD 卡引脚（SPI 接口）
#XT|```
#BR|CS   → GPIO13
#NY|CLK  → GPIO14（与摄像头 XCLK 共享）
#WQ|MOSI → GPIO15
#MH|MISO → GPIO2
#NB|```
#HS|
#MK|### 控制和状态引脚
#YS|```
#MQ|状态 LED → GPIO33（低电平有效）
#QY|闪光灯 LED → GPIO4（PWM）
#ZT|启动按钮 → GPIO0
#BZ|```
#PT|
#TN|### 重要说明
#RH|1. **GPIO14 共享**：在摄像头和 SD 卡之间共享 - 时间复用固件处理
#VP|2. **无同时摄像头+SD 操作** - 防止 AVI 录制
#NH|3. **需要 PSRAM** - 摄像头操作需要 - 不能禁用
#BS|4. **仅输入 GPIO**：34,35,36,39 不能用作输出
#SM|5. **UART0** 用于串行通信（GPIO1=TX，GPIO3=RX）