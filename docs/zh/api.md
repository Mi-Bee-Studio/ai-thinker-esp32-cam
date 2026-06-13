[![ESP32](https://img.shields.io/badge/ESP32-Esp32--cam-blue.svg)](https://github.com/espressif/esp-idf) [![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/) [![OV2640](https://img.shields.io/badge/Camera-OV2640-orange.svg)](https://www.ovt.com/products/ov2640.html) [![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) 

> 🌐 [English Documentation](../en/api.md)

# API 参考文档

本文档提供 MiBee Cam 固件 REST API 的完整参考，包括所有端点、参数和示例。

## 概述

MiBee Cam 提供 RESTful API 接口，允许通过 HTTP 请求控制和监控系统。API 支持以下功能：

- **设备状态监控**：获取系统健康信息、网络状态和指标
- **配置管理**：更新 WiFi、摄像头和其他设置
- **文件管理**：SD 卡照片浏览、下载和删除
- **视频流**：实时 MJPEG 流传输
- **录像控制**：启动/停止 AVI 视频录制
- **NAS 上传**：WebDAV 云存储备份
- **运动检测**：帧差分运动检测和拍照

## API 基础信息

### 基础 URL

- **STA 模式**：`http://<设备-ip>/`
- **AP 模式**：`http://192.168.4.1/`

### 重要说明

⚠️ **摄像头初始化工作流程**：为了解决 ESP32 已知的 DMA 冻结问题，固件采用"先启动 WiFi STA 模式，在回调中初始化摄像头"的策略。启动时会延迟 1-2 秒进行摄像头初始化。

### 认证

某些需要身份验证的操作：
```http
X-Password: admin
```

### 响应格式
- **成功响应**：HTTP 200 + JSON 数据
- **错误响应**：HTTP 400/500 + 错误消息
- **流响应**：HTTP 200 + multipart/x-mixed-replace 数据

### 常见错误代码

| 代码 | 描述 |
|------|-------------|
| 200 | 成功 |
| 400 | 请求参数错误 |
| 401 | 认证失败 |
| 404 | 资源未找到 |
| 500 | 服务器内部错误 |

## 设备状态 API

### `/api/status` - 获取设备状态

获取设备完整状态信息。

**请求**
```http
GET /api/status
Host: 192.168.1.100
```

**响应**
```json
{
  "status": "running",
  "hostname": "MiBeeCam",
  "uptime": 86400,
  "wifi": {
    "mode": "STA",
    "connected": true,
    "ssid": "MyWiFi",
    "bssid": "AA:BB:CC:DD:EE:FF",
    "ip": "192.168.1.100",
    "signal": -45,
    "connected_time": 3600
  },
  "camera": {
    "resolution": "VGA",
    "fps": 15,
    "quality": 12,
    "format": "JPEG",
    "buffer": 0.6,
    "status": "active"
  },
  "storage": {
    "sd_card": {
      "present": true,
      "total": 15892485120,
      "used": 5242880,
      "available": 15887252240,
      "percent_used": 0.033
    }
  },
  "motion": {
    "enabled": true,
    "detected": false,
    "last_event": "2024-12-30T14:30:25Z",
    "total_events": 125
  },
  "system": {
    "temperature": 45.2,
    "heap": 45012,
    "stack_watermark": 2048,
    "psram": 0,
    "time": "2024-12-30T14:35:42Z",
    "timezone": "CST-8",
    "config_version": 8,
    "config_magic": 2864434392
  }
}
```

**字段说明**
- `status`: 设备状态 (running, ap_mode, error)
- `uptime`: 运行时间（秒）
- `wifi`: WiFi 连接详细信息
- `camera`: 摄像头状态信息
- `storage`: 存储使用情况
- `motion`: 运动检测状态
- `system`: 系统指标和时间

## 配置 API

### `/api/config` - 获取配置

获取当前系统配置。

**请求**
```http
GET /api/config
Host: 192.168.1.100
```

**响应**
```json
{
  "wifi_ssid": "MyWiFi",
  "device_name": "MiBeeCam",
  "resolution": 0,
  "fps": 15,
  "jpeg_quality": 12,
  "web_password": "",
  "timezone": "CST-8",
  "motion_threshold": 30,
  "motion_cooldown": 5,
  "vflip": 0,
  "wifi_tx_power": 80,
  "wifi_power_save": 0,
  "flash_threshold": 40,
  "timelapse_enabled": 0,
  "timelapse_interval_s": 60,
  "timelapse_burst_count": 3,
  "record_mode": 0,
  "webdav_url": "",
  "webdav_user": "",
  "alert_webhook_url": "",
  "alert_webhook_events": 0,
  "cleanup_photo_pct": 20,
  "cleanup_video_pct": 10
}
```

**注意**：密码字段不会在响应中返回。

### `/api/config` - 更新配置

更新系统配置（需要认证）。

**请求**
```http
POST /api/config
Host: 192.168.1.100
X-Password: admin
Content-Type: application/json

{
  "wifi_ssid": "NewNetwork",
  "wifi_pass": "NewPassword",
  "resolution": 1,
  "fps": 20,
  "motion_threshold": 25
}
```

**参数**
- `wifi_ssid`: WiFi 网络名称
- `wifi_pass`: WiFi 密码
- `device_name`: 设备主机名
- `resolution`: 分辨率 (0=VGA, 1=SVGA, 2=XGA, 3=UXGA)
- `fps`: 帧率目标
- `jpeg_quality`: JPEG 质量 (0-63，数字越小质量越高)
- `web_password`: Web 访问密码
- `timezone`: 时区字符串
- `motion_threshold`: 运动检测阈值 (1-100)
- `motion_cooldown`: 运动检测冷却时间（秒）
- `vflip`: 垂直翻转 (0=关闭, 1=开启)
- `wifi_tx_power`: WiFi 发射功率 (0-80，80=20dBm)
- `wifi_power_save`: 省电模式 (0=关闭, 1=最小调制解调器)
- `flash_threshold`: 闪光阈值 (0-100)
- `record_mode`: 录像模式 (0=关闭, 1=连续, 2=延时, 3=动态)
- `webdav_url`: WebDAV 服务器 URL
- `webdav_user`: WebDAV 用户名
- `webdav_pass`: WebDAV 密码
- `alert_webhook_url`: Webhook HTTP POST URL
- `alert_webhook_events`: Webhook 事件位掩码
- `cleanup_photo_pct`: 照片清理阈值百分比
- `cleanup_video_pct`: 视频清理阈值百分比

**响应**
```json
{
  "success": true,
  "message": "Configuration updated successfully",
  "needs_reboot": true
}
```

### `/api/auth` - 验证密码

验证 Web 访问密码。

**请求**
```http
GET /api/auth
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "message": "Password verified"
}
```

## 摄像头 API

### `/capture` - 捕获照片

捕获单张 JPEG 照片。

**请求**
```http
GET /capture
Host: 192.168.1.100
```

**响应**
```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Content-Length: 45232

[JPEG 二进制数据]
```

### `/stream` - MJPEG 流

提供实时 MJPEG 视频流。

**请求**
```http
GET /stream
Host: 192.168.1.100
```

**响应**
```http
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace; boundary=frame
Connection: keep-alive

--frame
Content-Type: image/jpeg

[JPEG 帧数据 1]
--frame
Content-Type: image/jpeg

[JPEG 帧数据 2]
--frame
Content-Type: image/jpeg

[JPEG 帧数据 3]
```

## 文件管理 API

### `/api/files` - 列出文件

获取 SD 卡上的照片文件列表。

**请求**
```http
GET /api/files?limit=10&offset=0
Host: 192.168.1.100
```

**参数**
- `limit`: 返回的文件数量（可选，默认 20）
- `offset`: 分页偏移量（可选，默认 0）

**响应**
```json
{
  "files": [
    {
      "name": "2024-12-30_14-35-42.jpg",
      "size": 45232,
      "timestamp": 1735565742
    },
    {
      "name": "2024-12-30_14-30-25.jpg",
      "size": 38421,
      "timestamp": 1735565425
    }
  ],
  "total": 125,
  "limit": 10,
  "offset": 0
}
```

### `/api/files` - 删除文件

删除 SD 卡上的照片文件（需要认证）。

**请求**
```http
DELETE /api/files?name=2024-12-30_14-35-42.jpg
Host: 192.168.1.100
X-Password: admin
```

**参数**
- `name`: 要删除的文件名（必需）

**响应**
```json
{
  "success": true,
  "message": "File deleted successfully"
}
```

### `/api/download` - 下载文件

下载 SD 卡上的照片文件。

**请求**
```http
GET /api/download?name=2024-12-30_14-35-42.jpg
Host: 192.168.1.100
```

**参数**
- `name`: 要下载的文件名（必需）

**响应**
```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Content-Disposition: attachment; filename="2024-12-30_14-35-42.jpg"
Content-Length: 45232

[JPEG 二进制数据]
```

## 录像控制 API

### `/api/record` - 启动/停止录像

启动或停止视频录制（需要认证）。

**请求**
```http
POST /api/record?action=start
Host: 192.168.1.100
X-Password: admin
```

**参数**
- `action`: `start` 或 `stop`（必需）

**响应**
```json
{
  "success": true,
  "message": "Recording started",
  "mode": "continuous"
}
```

**Curl 示例**
```bash
# 启动连续录像
curl -X POST "http://192.168.1.100/api/record?action=start" \
  -H "X-Password: admin"

# 停止录像
curl -X POST "http://192.168.1.100/api/record?action=stop" \
  -H "X-Password: admin"
```

### `/api/record` - 获取录像状态

返回录像状态和信息。

**请求**
```http
GET /api/record
Host: 192.168.1.100
```

**响应**
```json
{
  "recording": true,
  "mode": "continuous",
  "duration": 3600,
  "segments": 12,
  "total_size": 125829120,
  "current_file": "rec_2024-12-30_14-30-25.avi"
}
```

## 存储管理 API

### `/api/storage` - 获取存储状态

返回存储使用情况和清理状态。

**请求**
```http
GET /api/storage
Host: 192.168.1.100
```

**响应**
```json
{
  "sd_card": {
    "available": true,
    "total": 31457280,
    "free": 15728640,
    "used": 15728640,
    "photo_count": 25,
    "video_count": 3
  },
  "cleanup": {
    "photo_threshold": 20,
    "video_threshold": 10,
    "last_cleanup": "2024-12-30T14:30:00Z"
  }
}
```

## NAS 上传 API

### `/api/nas/test` - 测试 NAS 连接

测试 NAS 连接（需要认证）。

**请求**
```http
POST /api/nas/test
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "message": "NAS connection successful",
  "server": "nas.example.com",
  "protocol": "webdav"
}
```

**Curl 示例**
```bash
# 测试 NAS 连接
curl -X POST http://192.168.1.100/api/nas/test \
  -H "X-Password: admin"
```

### `/api/nas` - 获取 NAS 状态

返回 NAS 上传状态和队列。

**请求**
```http
GET /api/nas
Host: 192.168.1.100
```

**响应**
```json
{
  "configured": true,
  "protocol": "webdav",
  "server": "nas.example.com",
  "queue_size": 5,
  "uploaded": 120,
  "failed": 2,
  "last_upload": "2024-12-30T14:30:00Z"
}
```

**Curl 示例**
```bash
# 获取 NAS 状态
curl http://192.168.1.100/api/nas
```

## 延时摄影 API

### `/api/timelapse/start` - 启动延时摄影

启动延时摄影（需要认证）。

**请求**
```http
POST /api/timelapse/start
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "message": "Timelapse started",
  "interval": 60,
  "burst_count": 3
}
```

### `/api/timelapse/stop` - 停止延时摄影

停止延时摄影（需要认证）。

**请求**
```http
POST /api/timelapse/stop
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "message": "Timelapse stopped"
}
```

### `/api/timelapse/status` - 获取延时摄影状态

返回延时摄影状态。

**请求**
```http
GET /api/timelapse/status
Host: 192.168.1.100
```

**响应**
```json
{
  "enabled": true,
  "running": true,
  "interval": 60,
  "burst_count": 3,
  "photos_taken": 45,
  "last_photo": "2024-12-30_14-30-25.jpg"
}
```

## 系统管理 API

### `/api/reboot` - 重启设备

重启设备（需要认证）。

**请求**
```http
POST /api/reboot
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "message": "Rebooting device",
  "restart_time": 5
}
```

### `/api/reset` - 恢复出厂设置

恢复出厂设置（需要认证）。

**注意**：BOOT 按钮工厂重置功能已禁用，因为 GPIO0 被用作摄像头 XCLK 引脚。请使用此 API 端点进行重置。

**请求**
```http
POST /api/reset
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "message": "Factory reset completed",
  "restart_time": 3
}
```

## 监控和指标 API

### `/metrics` - Prometheus 指标

提供兼容 Prometheus 格式的系统指标。

**请求**
```http
GET /metrics
Host: 192.168.1.100
```

**响应**
```prometheus
# HELP esp32_heap_free Heap memory free bytes
# TYPE esp32_heap_free gauge
esp32_heap_free 45012

# HELP esp32_stack_watermark Stack watermark in bytes
# TYPE esp32_stack_watermark gauge
esp32_stack_watermark 2048

# HELP esp32_temperature Temperature in Celsius
# TYPE esp32_temperature gauge
esp32_temperature 45.2

# HELP wifi_signal_strength WiFi signal strength in dBm
# TYPE wifi_signal_strength gauge
wifi_signal_strength -45

# HELP esp32_uptime System uptime in seconds
# TYPE esp32_uptime counter
esp32_uptime 86400

# HELP motion_events_total Total motion detection events
# TYPE motion_events_total counter
motion_events_total 125

# HELP photos_taken_total Total photos taken
# TYPE photos_taken_total counter
photos_taken_total 245
```

## cURL 使用示例

### 基本状态检查
```bash
# 获取设备状态
curl -s http://192.168.1.100/api/status | jq .

# 获取配置
curl -s http://192.168.1.100/api/config | jq .
```

### 配置管理
```bash
# 更新配置
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"MyPass","resolution":1}'

# 验证密码
curl -H "X-Password: admin" http://192.168.1.100/api/auth
```

### 摄像头控制
```bash
# 捕获照片
curl http://192.168.1.100/capture -o photo.jpg

# 查看流（在浏览器中打开）
# http://192.168.1.100/stream
```

### 文件管理
```bash
# 列出文件
curl -s "http://192.168.1.100/api/files?limit=5" | jq .

# 下载文件
curl -o photo.jpg "http://192.168.1.100/api/download?name=2024-12-30_14-35-42.jpg"

# 删除文件
curl -X DELETE "http://192.168.1.100/api/files?name=2024-12-30_14-35-42.jpg" \
  -H "X-Password: admin"
```

### 录像控制
```bash
# 启动录像
curl -X POST "http://192.168.1.100/api/record?action=start" \
  -H "X-Password: admin"

# 停止录像
curl -X POST "http://192.168.1.100/api/record?action=stop" \
  -H "X-Password: admin"

# 查看录像状态
curl -s http://192.168.1.100/api/record | jq .
```

### NAS 管理
```bash
# 测试 NAS 连接
curl -X POST http://192.168.1.100/api/nas/test \
  -H "X-Password: admin"

# 查看 NAS 状态
curl -s http://192.168.1.100/api/nas | jq .
```

### 延时摄影
```bash
# 启动延时摄影
curl -X POST http://192.168.1.100/api/timelapse/start \
  -H "X-Password: admin"

# 停止延时摄影
curl -X POST http://192.168.1.100/api/timelapse/stop \
  -H "X-Password: admin"

# 查看延时状态
curl -s http://192.168.1.100/api/timelapse/status | jq .
```

### 系统管理
```bash
# 重启设备
curl -X POST http://192.168.1.100/api/reboot \
  -H "X-Password: admin"

# 恢复出厂设置
curl -X POST http://192.168.1.100/api/reset \
  -H "X-Password: admin"

# 获取 Prometheus 指标
curl -s http://192.168.1.100/metrics
```

## 错误处理

### 常见错误及解决方法

**400 Bad Request**
- 原因：请求参数错误
- 解决：检查请求格式和参数

**401 Unauthorized**
- 原因：认证失败
- 解决：检查 `X-Password` 头部

**404 Not Found**
- 原因：端点不存在
- 解决：检查 API 路径

**500 Internal Server Error**
- 原因：服务器内部错误
- 解决：查看设备日志，检查资源使用情况

### 错误响应格式
```json
{
  "success": false,
  "error": "Error message",
  "code": "ERROR_CODE",
  "details": "Additional error details"
}
```

## API 限制

- **并发连接数**：最多 5 个 HTTP 连接
- **MJPEG 流**：最多 2 个并发流
- **请求大小**：最大 1MB
- **响应大小**：未限制（流除外）

## 安全注意事项

1. **Web 密码**：在生产环境中更改默认密码
2. **HTTPS**：当前不支持，建议在专用网络中使用
3. **网络隔离**：将设备放在专用网络中
4. **访问控制**：限制 API 访问 IP 范围
5. **定期更新**：保持固件最新版本以修复安全漏洞

### GPIO14 共享引脚说明

⚠️ **重要提醒**：GPIO14 引脚在 MiBee Cam 上被摄像头和 SD 卡共享使用（摄像头 XCLK 和 SD 卡 CLK）。固件采用严格的初始化顺序来解决冲突：SD 卡初始化 → 摄像头初始化 → SD 卡重新初始化。如果遇到相关功能异常，请检查此引脚连接。
