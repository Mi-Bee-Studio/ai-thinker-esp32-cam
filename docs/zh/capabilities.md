[![GitHub release](https://img.shields.io/github/v/release/Mi-Bee-Studio/ai-thinker-esp32-cam?include_prereleases&style=flat-square)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/releases)
[![GitHub stars](https://img.shields.io/github/stars/Mi-Bee-Studio/ai-thinker-esp32-cam?style=flat-square)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam)
[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?branch=main&style=flat-square)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue?style=flat-square)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)

> 🌐 [English Documentation](../en/capabilities.md)

# 项目功能与能力

本文档汇总 AI_Thinker ESP32-CAM 固件当前支持的全部功能、运行模式以及已移除的特性。

**数据基于当前 main 分支（`CONFIG_VERSION = 6`，魔数 `0xA5B6C7D8`）。**

## 概览

固件是一个**纯 JPEG 拍照 + MJPEG 推流**的 ESP32-CAM 固件，目标硬件：

- **主控**：ESP32（双核 Xtensa LX6 @ 240MHz，**非 ESP32-S3**）
- **存储**：4MB Flash + 4MB PSRAM
- **摄像头**：OV2640（2MP，定焦定光圈镜头）
- **网络**：2.4GHz 802.11 b/g/n WiFi

## 支持的功能

### 📷 视频与拍照

| 功能 | 接口 | 限制 |
|------|------|------|
| MJPEG 实时流 | `GET /stream` | 最多 2 并发客户端，15 FPS 硬编码 |
| 单帧抓拍 | `GET /capture` | 返回单张 JPEG，分辨率/质量按当前配置 |
| 多分辨率输出 | 配置文件 | VGA / SVGA / XGA / UXGA |
| JPEG 质量可调 | 配置文件 | 0-63（数字越小质量越高） |
| 垂直翻转 | 配置项 `vflip` | 调用 OV2640 `set_vflip` 寄存器 |

### 🎬 自动化拍摄

| 功能 | 默认行为 | 配置项 |
|------|----------|--------|
| 运动检测 | 500ms 双帧差分（基于 JPEG 字节采样） | `motion_threshold`, `motion_cooldown` |
| 运动触发拍照 | 检测到变化后保存到 SD | 自动 |
| 自动闪光（暗光场景） | 基于 JPEG 体积判断亮度，触发时开 LED | `flash_threshold` |
| 延时摄影 | 按设定间隔拍一张到 SD | `timelapse_enabled`, `timelapse_interval_s` |
| 延时 + 突发连拍 | 运动触发时连拍 N 张 | `timelapse_burst_count` |
| SD 空间自动清理 | 剩余 < 20% 时删最旧文件，删到 30% 停 | 自动 |

### 🌐 网络与 Web

| 功能 | 说明 |
|------|------|
| **WiFi STA 模式** | 配置 SSID/密码后自动连接，支持断线重连 |
| **WiFi AP 模式** | 首次未配网时进入，SSID `ai-thinker-cam`，密码 `12345678` |
| **TX 功率可调** | 0-80（0.25dBm 单位，80=20dBm） |
| **省电模式** | `WIFI_PS_MIN_MODEM` |
| **Web UI 仪表盘** | `/`（自动刷新 10s） |
| **Web UI 实时预览** | `/preview.html`（带分辨率/质量控件） |
| **Web UI 配置** | `/config.html`（WiFi / 摄像头 / 运动 / 延时） |
| **Web UI 文件浏览** | `/files.html`（分页 + 下载/删除） |
| **NTP 时间同步** | 可配时区，默认 `CST-8` |
| **Prometheus 指标** | `GET /metrics`（堆/RSSI/SD/温度/事件计数） |

### 💾 存储

| 功能 | 说明 |
|------|------|
| **SD 卡 SPI 模式** | CLK=GPIO14, MOSI=GPIO15, MISO=GPIO2, CS=GPIO13 |
| **FAT32 文件系统** | 5 次重试挂载 |
| **按月分目录** | `/sdcard/photos/YYYY-MM/` |
| **文件名带时间戳** | `motion_2026-06-02_14-30-25.jpg` |
| **照片列表缓存** | 30s TTL，避免每次 HTTP 请求都遍历 SD |
| **热插拔检测** | 10s 轮询一次，自动重挂载 |
| **NVS 配置** | 命名空间 `camcfg`，V1→V6 自动迁移 |

### 🔌 REST API

完整端点列表（详见 [`api.md`](./api.md)）：

| Method | Path | 说明 |
|--------|------|------|
| GET    | `/api/status` | 设备状态 JSON |
| GET    | `/api/config` | 当前配置（密码字段不返回） |
| POST   | `/api/config` | 更新配置（需 `X-Password` 头） |
| POST   | `/api/reset` | 出厂重置（需 `X-Password` 头） |
| POST   | `/api/reboot` | 重启设备（需 `X-Password` 头） |
| GET    | `/capture` | 单帧 JPEG |
| GET    | `/stream` | MJPEG 实时流 |
| GET    | `/metrics` | Prometheus 指标 |
| GET    | `/api/files` | 列出 SD 照片（分页） |
| DELETE | `/api/files?name=xxx` | 删除照片 |
| GET    | `/api/download?name=xxx` | 下载照片 |
| GET    | `/api/auth` | 验证 web 密码 |
| POST   | `/api/timelapse/start` | 启动延时 |
| POST   | `/api/timelapse/stop` | 停止延时 |
| GET    | `/api/timelapse/status` | 延时状态 |

## 不支持的功能（明确列出）

- ❌ **真正的连续录像**：无 H.264 硬件编码，无 mp4/mov/avi 容器封装
- ❌ **音视频录制**：无麦克风输入，无音频编码
- ❌ **自动对焦 / 光学变焦**：OV2640 是定焦镜头
- ❌ **水平翻转 / 180° 旋转**：固件仅暴露 `vflip`（垂直翻转）
- ❌ **ROI 数字缩放**：固件没有兴趣区域裁切接口
- ❌ **OTA 升级**：分区表只有 `otadata`，没有 `ota_0/ota_1` 槽位
- ❌ **HTTP 认证以外的鉴权**：只有 `X-Password` 头
- ❌ **HTTPS**：HTTP 服务器仅支持明文

## 已移除的功能

| 功能 | 移除时间 | 原因 |
|------|----------|------|

| **FTP 客户端** | v2 早期 | 太重，资源紧张 |
| **TF 卡 WiFi 配置（config.txt）** | 保留 | SD `/config.txt` 仍可启动时导入 WiFi |
|| **BOOT 按钮出厂重置** | 始终禁用 | GPIO0 = 摄像头 XCLK，按下检测不可靠，改用 `POST /api/reset` |
| **PSRAM DMA 模式** | 一直禁用 | ESP32 原版 DMA bug，固件 `sdkconfig.defaults` 强制 `CAMERA_PSRAM_DMA=n` |
| **新 SCCB/I2C 驱动** | 一直禁用 | `CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY=y` 兼容 OV2640 |

## 启动序列

`app_main()` 中的 16 步启动序列（部分步骤在 STA 模式下被推迟到 WiFi 回调）：

1. NVS flash 初始化
2. 配置从 NVS 加载（V1→V6 迁移）
3. 状态 LED 初始化（`LED_STARTING`）
4. SPIFFS 挂载（Web UI）
5. 释放 SD SPI 总线
6. WiFi 子系统初始化
7. 注册 WiFi 状态回调
8. 健康监控初始化
9. WiFi 模式选择（STA 或 AP）
10. MJPEG 流模块初始化
11. NTP 时间同步初始化
12. Web 服务器启动（端口 80）
13. 运动检测启动
14. SD 卡初始化（在摄像头之后，GPIO14 复用）


## 配置结构

`cam_config_t`（NVS blob，大小约 120 字节）：

| 字段 | 类型 | 默认值 | 用途 |
|------|------|--------|------|
| `wifi_ssid` | char[33] | "" | STA 模式 SSID |
| `wifi_pass` | char[65] | "" | STA 模式密码 |
| `device_name` | char[33] | "ai-thinker-cam" | 主机名 |
| `resolution` | uint8 | 0 (VGA) | 0=VGA, 1=SVGA, 2=XGA, 3=UXGA |
| `fps` | uint8 | 15 | 帧率目标 |
| `jpeg_quality` | uint8 | 12 | 0-63（低=高质） |
| `web_password` | char[33] | "" | 可选 Web 访问密码 |
| `timezone` | char[33] | "CST-8" | 时区字符串 |
| `motion_threshold` | uint8 | 30 | 差分百分比阈值（1-100） |
| `motion_cooldown` | uint8 | 5 | 触发冷却（秒） |
| `vflip` | uint8 | 0 | 垂直翻转开关 |
| `wifi_tx_power` | uint8 | 80 | 0.25dBm 单位（80=20dBm） |
| `wifi_power_save` | uint8 | 0 | 0=None, 1=MIN_MODEM |
| `flash_threshold` | uint8 | 40 | 暗光判定阈值（0-100） |
| `timelapse_enabled` | uint8 | 0 | 延时总开关 |
| `timelapse_interval_s` | uint16 | 60 | 延时间隔（秒） |
| `timelapse_burst_count` | uint8 | 3 | 突发连拍张数 |
| `magic` + `version` | uint32 | `0xA5B6C7D8` / 6 | 魔数+版本号（用于迁移） |

## 故障转移行为

- **STA 模式连接失败**：连续重连 N 次后回退到 AP 模式
- **SD 卡挂载失败**：继续运行，只是不能保存照片（web 仍可用）
- **摄像头初始化失败（STA 模式下）**：重试 3 次，每次间隔 500ms（ESP32 DMA 冻结问题解决方法，固件在 WiFi STA 模式下延迟摄像头初始化）
- **WiFi 中途断开**：触发 `WIFI_STATE_STA_DISCONNECTED`，健康监控记录，LED 闪烁
- **TF 卡热拔**：10s 轮询检测到，自动卸载并通知
- **TF 卡容量 < 20%**：自动从最旧文件开始删
- **配置版本不匹配**：blob 大小比较 → 用旧字段填充 + 新字段默认值

## 相关文档

- [`getting-started.md`](./getting-started.md) - 编译、烧录、首次配置
- [`hardware.md`](./hardware.md) - 板级硬件规格
- [`performance.md`](./performance.md) - 各分辨率的 FPS / 延迟 / JPEG 体积
- [`lens-and-fov.md`](./lens-and-fov.md) - 镜头参数、视场角、广角与微距选项
- [`architecture.md`](./architecture.md) - 系统架构
- [`api.md`](./api.md) - REST API 完整参考
- [`troubleshooting.md`](./troubleshooting.md) - 常见问题
