[![GitHub release](https://img.shields.io/github/v/release/Mi-Bee-Studio/ai-thinker-esp32-cam?include_prereleases&style=flat-square)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/releases)
[![GitHub stars](https://img.shields.io/github/stars/Mi-Bee-Studio/ai-thinker-esp32-cam?style=flat-square)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam)
[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?branch=main&style=flat-square)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue?style=flat-square)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)

> 🌐 [English Documentation](../en/getting-started.md)

# 入门指南

本指南将帮助您快速上手 AI_Thinker ESP32-CAM 固件。请按照以下步骤构建、烧录和配置您的设备。

## 前置要求

### 硬件
- AI_Thinker ESP32-CAM 主板
- OV2640 摄像头模块
- TF 卡（可选，用于照片存储）
- USB 电源和数据线
- 带有 USB 端口的计算机

### 软件
- **ESP-IDF v6.0.1** — [安装指南](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/index.html) (**必需版本**，固件专为 ESP-IDF v6.0.1 构建)
- **Python 3.8 或更高版本**
- **Git**
- **esptool.py**（通常随 ESP-IDF 一起提供）

## 安装步骤

### 1. 克隆仓库

```bash
git clone https://github.com/your-username/AI_Thinker-ESP32-cam.git
cd AI_Thinker-ESP32-cam
```

### 2. 设置 ESP-IDF 环境

```bash
# Windows (命令提示符)
C:\Espressif\esp-idf> export.bat

# Linux/Mac
source ~/esp/esp-idf/export.sh
```

### 3. 配置目标

```bash
idf.py set-target esp32
```

这会将目标设置为原始 ESP32（不是 ESP32-S3）。

## 构建固件

### 首次构建（推荐清理）
```bash
idf.py clean
idf.py build
```

### 增量构建
```bash
idf.py build
```

固件将在 `build/` 目录中构建。查找最终二进制文件：
- `build/ai-thinker-cam.bin` — 主固件
- `bootloader.bin` — 引导加载程序
- `build/partition_table.bin` — 分区表

## 烧录固件

### 串口设置

1. 通过 USB 将您的 AI_Thinker ESP32-CAM 连接到计算机
2. 识别串口：
   - **Windows**: 在设备管理器的 "端口 (COM 和 LPT)" 下查看
   - **Linux**: 通常是 `/dev/ttyUSB0`、`/dev/ttyACM0` 或类似设备
   - **Mac**: 通常是 `/dev/tty.usbserial-XXXX` 或 `/dev/tty.usbmodemXXXX`

### 烧录固件

```bash
idf.py -p COM8 flash monitor
```

将 `COM8` 替换为您的实际串口。`flash` 命令将：
1. 擦除闪存
2. 写入引导加载程序、固件和分区表
3. 重置设备

### 串口监控

烧录完成后，串口监视器将自动启动，显示启动序列：

```
I (30) boot: ESP-IDF v6.0 第二阶段引导加载程序
I (30) boot: 编译时间 12:00:00
I (30) boot: 芯片版本: v0.2
...
I (1234) main: 启动步骤 1: NVS 闪存初始化
I (1235) main: 启动步骤 2: 从 NVS 加载配置
...
I (2345) main: 系统启动完成
```

## 首次配置

### AP 模式首次启动

当设备首次启动时，由于没有存储 WiFi 凭据，它将进入 AP 模式：

1. **连接 WiFi**：连接到网络 **ai-thinker-cam**（密码：`12345678`）
2. **访问 Web 界面**：在 Web 浏览器中打开 **http://192.168.4.1**
3. **配置 WiFi**：
   - 进入 "配置" 页面
   - 输入您的 WiFi SSID 和密码
   - 设置设备名称（可选）
   - 根据需要配置其他设置
4. **保存更改**：点击 "保存" 重启设备

### STA 模式操作

成功配置后，设备将重启并以 STA 模式连接到您的 WiFi 网络：

1. 设备将尝试连接到您配置的 WiFi 网络
2. 如果成功，它将从路由器获取 IP 地址
3. 现在您可以在设备 IP 地址（在串口监视器中显示）访问 Web 界面
4. 状态 LED 将指示连接过程中的不同状态

## 验证安装

### 检查 Web 界面
在 Web 浏览器中打开设备的 IP 地址。您应该看到：
- 带有设备状态的仪表板
- 摄像头预览
- 配置选项

### 测试 API 端点

使用 curl 或类似工具测试 API：

```bash
# 获取设备状态
curl http://<设备-ip>/api/status

# 测试 MJPEG 流
curl -s http://<设备-ip>/stream | head -c 100

# 获取指标
curl http://<设备-ip>/metrics
```

### 测试摄像头捕获

```bash
# 下载测试 JPEG
curl -o test.jpg http://<设备-ip>/capture
```

## 常见问题

### 构建错误
- **目标未设置**：首先运行 `idf.py set-target esp32`
- **缺少组件**：ESP-IDF 将自动下载依赖项
- **编译器错误**：检查代码中的语法错误

### 烧录问题
- **找不到串口**：检查设备驱动程序或尝试不同的电缆
- **权限被拒绝**：以管理员/ sudo 权限运行
- **设备无响应**：检查启动模式（应为正常启动，不是下载模式）

### WiFi 连接问题
- **密码错误**：仔细检查 WiFi 凭据
- **2.4GHz 要求**：ESP32 仅支持 2.4GHz WiFi
- **路由器设置**：确保 WPA/WPA2 安全性（不要使用 WPA3）
- **AP 模式回退**：如果 STA 失败，设备自动进入 AP 模式

### 摄像头问题
- **无视频**：检查摄像头模块是否正确安装
- **引脚冲突**：GPIO14 在摄像头和 SD 卡之间共享
- **分辨率过高**：从 VGA (640x480) 开始以获得稳定性

## 下一步

设备运行后，您可以：
- 探索 [用户指南](user-guide.md) 了解详细使用方法
- 了解 [硬件](hardware.md) 规格和引脚映射
- 配置运动检测和延时摄影功能
- 查看 [架构](architecture.md) 了解系统详情
- 检查 [故障排除](troubleshooting.md) 指南以获取解决方案
- 探索 [API](api.md) 进行编程控制