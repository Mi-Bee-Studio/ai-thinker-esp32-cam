#SX|[![GitHub release](https://img.shields.io/github/v/release/Mi-Bee-Studio/mibee-cam?include_prereleases&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/releases)
#RZ|[![GitHub stars](https://img.shields.io/github/stars/Mi-Bee-Studio/mibee-cam?style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam)
#ZV|[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/actions)
#WP|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue?style=flat-square)](https://docs.espressif.com/projects/esp-idf/en/latest/)
#WR|[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)
#SY|
#BS|> 🌐 [English Documentation](../en/getting-started.md)
#XW|
#NS|# 入门指南
#SK|
#ZN|本指南将帮助您快速上手 MiBee Cam 固件。请按照以下步骤构建、烧录和配置您的设备。
#TX|
#HN|## 前置要求
#BY|
#NV|### 硬件
#SY|- MiBee Cam 主板
#BX|- OV2640 摄像头模块
#VJ|- TF 卡（可选，用于照片存储）
#VB|- USB 电源和数据线
#RS|- 带有 USB 端口的计算机
#RJ|
#ZW|### 软件
#HR|- **ESP-IDF v6.0.1** — [安装指南](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/index.html) (**必需版本**，固件专为 ESP-IDF v6.0.1 构建)
#WX|- **Python 3.8 或更高版本**
#PQ|- **Git**
#YZ|- **esptool.py**（通常随 ESP-IDF 一起提供）
#JJ|
#ZR|## 安装步骤
#ZR|
#NN|### 1. 克隆仓库
#SZ|
#BV|```bash
#ZK|git clone https://github.com/your-username/mibee-cam.git
#TN|cd mibee-cam
#XS|```
#MV|
#BW|### 2. 设置 ESP-IDF 环境
#BN|
#BV|```bash
#VJ|# Windows (命令提示符)
#KB|C:\Espressif\esp-idf> export.bat
#XN|
#RR|# Linux/Mac
#YM|source ~/esp/esp-idf/export.sh
#PQ|```
#TJ|
#WN|### 3. 配置目标
#BY|
#BV|```bash
#ZB|idf.py set-target esp32
#SY|```
#NM|
#ZQ|这会将目标设置为原始 ESP32（不是 ESP32-S3）。
#YJ|
#SH|## 构建固件
#XN|
#JP|### 首次构建（推荐清理）
#BV|```bash
#KY|idf.py clean
#PM|idf.py build
#VX|```
#RJ|
#HY|### 增量构建
#BV|```bash
#PM|idf.py build
#MJ|```
#TH|
#QH|固件将在 `build/` 目录中构建。查找最终二进制文件：
#NN|- `build/mibee-cam.bin` — 主固件
#XW|- `bootloader.bin` — 引导加载程序
#NS|- `build/partition_table.bin` — 分区表
#SV|
#KK|## 烧录固件
#HQ|
#YV|### 串口设置
#JW|
#XM|1. 通过 USB 将您的 MiBee Cam 连接到计算机
#JR|2. 识别串口：
#KJ|   - **Windows**: 在设备管理器的 "端口 (COM 和 LPT)" 下查看
#KH|   - **Linux**: 通常是 `/dev/ttyUSB0`、`/dev/ttyACM0` 或类似设备
#JS|   - **Mac**: 通常是 `/dev/tty.usbserial-XXXX` 或 `/dev/tty.usbmodemXXXX`
#YR|
#JR|### 烧录固件
#WR|
#BV|```bash
#SY|idf.py -p COM8 flash monitor
#WV|```
#VS|
#YB|将 `COM8` 替换为您的实际串口。`flash` 命令将：
#SJ|1. 擦除闪存
#QP|2. 写入引导加载程序、固件和分区表
#SN|3. 重置设备
#BX|
#VX|### 串口监控
#MS|
#NJ|烧录完成后，串口监视器将自动启动，显示启动序列：
#ZT|
#ZV|```
#QX|I (30) boot: ESP-IDF v6.0 第二阶段引导加载程序
#RB|I (30) boot: 编译时间 12:00:00
#RT|I (30) boot: 芯片版本: v0.2
#KH|...
#NS|I (1234) main: 启动步骤 1: NVS 闪存初始化
#VT|I (1235) main: 启动步骤 2: 从 NVS 加载配置
#TZ|...
#HM|I (2345) main: 系统启动完成
#HJ|```
#YQ|
#VW|## 首次配置
#WY|
#NS|### AP 模式首次启动
#QJ|
#HX|当设备首次启动时，由于没有存储 WiFi 凭据，它将进入 AP 模式：
#BJ|
#YX|1. **连接 WiFi**：连接到网络 **MiBeeCam**（密码：`12345678`）
#SW|2. **访问 Web 界面**：在 Web 浏览器中打开 **http://192.168.4.1**
#QZ|3. **配置 WiFi**：
#BB|   - 进入 "配置" 页面
#ZP|   - 输入您的 WiFi SSID 和密码
#SM|   - 设置设备名称（可选）
#ZR|   - 根据需要配置其他设置
#QT|4. **保存更改**：点击 "保存" 重启设备
#WY|
#HV|### STA 模式操作
#YB|
#ZW|成功配置后，设备将重启并以 STA 模式连接到您的 WiFi 网络：
#XB|
#QX|1. 设备将尝试连接到您配置的 WiFi 网络
#RZ|2. 如果成功，它将从路由器获取 IP 地址
#KV|3. 现在您可以在设备 IP 地址（在串口监视器中显示）访问 Web 界面
#JB|4. 状态 LED 将指示连接过程中的不同状态
#QZ|
#WS|## 验证安装
#QZ|
#RV|### 检查 Web 界面
#RH|在 Web 浏览器中打开设备的 IP 地址。您应该看到：
#HX|- 带有设备状态的仪表板
#HQ|- 摄像头预览
#HH|- 配置选项
#XS|
#XT|### 测试 API 端点
#HQ|
#HT|使用 curl 或类似工具测试 API：
#BT|
#BV|```bash
#HQ|# 获取设备状态
#MJ|curl http://<设备-ip>/api/status
#SS|
149#WP|# 测试 MJPEG 流
#WP|curl -s http://<设备-ip>/stream | head -c 100
#HV|
#TT|# 获取指标
#TH|curl http://<设备-ip>/api/metrics
#QW|```
#NT|
#XJ|### 测试摄像头捕获
#HJ|
#BV|```bash
#SK|# 下载测试 JPEG
#HK|curl -o test.jpg http://<设备-ip>/capture
#VM|```
#VQ|
#RX|## 常见问题
#SK|
#VK|### 构建错误
#KN|- **目标未设置**：首先运行 `idf.py set-target esp32`
#JN|- **缺少组件**：ESP-IDF 将自动下载依赖项
#JW|- **编译器错误**：检查代码中的语法错误
#PN|
#NH|### 烧录问题
#HW|- **找不到串口**：检查设备驱动程序或尝试不同的电缆
#QR|- **权限被拒绝**：以管理员/ sudo 权限运行
#SQ|- **设备无响应**：检查启动模式（应为正常启动，不是下载模式）
#RT|
#BW|### WiFi 连接问题
#XS|- **密码错误**：仔细检查 WiFi 凭据
#TM|- **2.4GHz 要求**：ESP32 仅支持 2.4GHz WiFi
#WX|- **路由器设置**：确保 WPA/WPA2 安全性（不要使用 WPA3）
#VZ|- **AP 模式回退**：如果 STA 失败，设备自动进入 AP 模式
#QS|
#RK|### 摄像头问题
#HW|- **无视频**：检查摄像头模块是否正确安装
#WK|- **引脚冲突**：GPIO14 在摄像头和 SD 卡之间共享
#ZT|- **分辨率过高**：从 VGA (640x480) 开始以获得稳定性
#HM|
#BT|## 下一步
#YV|
#BN|设备运行后，您可以：
#KQ|- 探索 [用户指南](user-guide.md) 了解详细使用方法
#HT|- 了解 [硬件](hardware.md) 规格和引脚映射
#MB|- 配置运动检测和延时摄影功能
#MK|- 查看 [架构](architecture.md) 了解系统详情
#BK|- 检查 [故障排除](troubleshooting.md) 指南以获取解决方案
#PR|- 探索 [API](api.md) 进行编程控制