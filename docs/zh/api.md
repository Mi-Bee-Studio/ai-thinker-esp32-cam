> 🌐 [English Documentation](../en/api.md)

# API 参考文档

本文档提供 AI_Thinker ESP32-CAM 固件 REST API 的完整参考，包括所有端点、参数和示例。

## 概述

AI_Thinker ESP32-CAM 提供 RESTful API 接口，允许通过 HTTP 请求控制和监控系统。API 支持以下功能：

- **设备状态监控**：获取系统健康信息、网络状态和指标
- **配置管理**：更新 WiFi、摄像头和其他设置
- **照片捕获**：手动和自动照片捕获
- **视频流**：实时 MJPEG 流传输
- **文件管理**：SD 卡照片浏览、下载和上传
- **系统控制**：重启、重置等管理操作

## API 基础信息

### 基础 URL
- **STA 模式**：`http://<设备-ip>/`
- **AP 模式**：`http://192.168.4.1/`

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
  "hostname": "ai-thinker-cam",
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
    "timezone": "CST-8"
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

### `/api/status/simplified` - 获取简化状态

获取仅包含基本信息的设备状态。

**请求**
```http
GET /api/status/simplified
Host: 192.168.1.100
```

**响应**
```json
{
  "status": "running",
  "hostname": "ai-thinker-cam",
  "ip": "192.168.1.100",
  "signal": -45,
  "uptime": 86400
}
```

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
  "wifi": {
    "mode": "STA",
    "ssid": "MyWiFi",
    "password": "MyPassword",
    "device_name": "ai-thinker-cam",
    "scan_results": []
  },
  "camera": {
    "resolution": 0,
    "fps": 15,
    "jpeg_quality": 12,
    "flash_brightness": 0
  },
  "motion": {
    "enabled": true,
    "threshold": 5,
    "cooldown_time": 10,
    "save_photo": true,
    "upload_photo": true
  },
  "nas": {
    "enabled": false,
    "protocol": "HTTP",
    "url": "",
    "port": 80,
    "username": "",
    "password": "",
    "path": "/photos",
    "retries": 3,
    "upload_motion": true
  },
  "time": {
    "timezone": "CST-8",
    "ntp_server": "pool.ntp.org"
  },
  "system": {
    "web_password": "",
    "led_enabled": true
  }
}
```

### `/api/config/update` - 更新配置

更新系统配置（需要认证）。

**请求**
```http
POST /api/config/update
Host: 192.168.1.100
X-Password: admin
Content-Type: application/json

{
  "wifi": {
    "ssid": "NewNetwork",
    "password": "NewPassword"
  },
  "camera": {
    "resolution": 1,
    "fps": 20
  }
}
```

**参数**
- `wifi`: WiFi 设置 (ssid, password, device_name)
- `camera`: 摄像头设置 (resolution, fps, jpeg_quality, flash_brightness)
- `motion`: 运动检测设置 (enabled, threshold, cooldown_time, save_photo, upload_photo)
- `nas`: NAS 设置 (enabled, protocol, url, port, username, password, path, retries, upload_motion)
- `time`: 时间设置 (timezone, ntp_server)
- `system`: 系统设置 (web_password, led_enabled)

**响应**
```json
{
  "success": true,
  "message": "Configuration updated successfully",
  "needs_reboot": true
}
```

### `/api/config/wifi/scan` - 扫描 WiFi 网络

扫描可用的 WiFi 网络。

**请求**
```http
GET /api/config/wifi/scan
Host: 192.168.1.100
```

**响应**
```json
[
  {
    "ssid": "MyWiFi",
    "bssid": "AA:BB:CC:DD:EE:FF",
    "channel": 6,
    "rssi": -45,
    "security": 3,
    "hidden": false
  },
  {
    "ssid": "AnotherNetwork",
    "bssid": "FF:EE:DD:CC:BB:AA",
    "channel": 11,
    "rssi": -65,
    "security": 3,
    "hidden": false
  }
]
```

**字段说明**
- `ssid`: 网络名称
- `bssid`: MAC 地址
- `channel`: 信道
- `rssi`: 信号强度 (dBm)
- `security`: 安全类型 (0=None, 1=WEP, 2=WPA, 3=WPA2)
- `hidden`: 是否为隐藏网络

## 摄像头 API

### `/api/capture` - 捕获照片

捕获单张照片并返回文件名。

**请求**
```http
POST /api/capture
Host: 192.168.1.100
Content-Type: application/json

{
  "quality": 12,
  "resolution": 0
}
```

**参数**
- `quality`: JPEG 质量 (1-63, 默认 12)
- `resolution`: 分辨率 (0=VGA, 1=SVGA, 2=XGA, 3=UXGA, 默认 0)

**响应**
```json
{
  "success": true,
  "filename": "2024-12-30_14-35-42.jpg",
  "path": "/sdcard/photos/2024-12-30/2024-12-30_14-35-42.jpg",
  "size": 45232,
  "resolution": "640x480",
  "quality": 12
}
```

### `/api/capture/download` - 下载捕获的照片

直接下载 JPEG 照片文件。

**请求**
```http
GET /api/capture/download?filename=2024-12-30_14-35-42.jpg
Host: 192.168.1.100
```

**参数**
- `filename`: 照片文件名（可选，默认最新照片）

**响应**
```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Content-Disposition: attachment; filename="2024-12-30_14-35-42.jpg"
Content-Length: 45232

[JPEG 二进制数据]
```

### `/api/camera/status` - 获取摄像头状态

获取摄像头当前状态信息。

**请求**
```http
GET /api/camera/status
Host: 192.168.1.100
```

**响应**
```json
{
  "status": "active",
  "resolution": 0,
  "resolution_name": "VGA",
  "resolution_dimensions": "640x480",
  "fps": 15,
  "fps_actual": 14.7,
  "jpeg_quality": 12,
  "format": "JPEG",
  "psram_usage": 600,
  "buffer_count": 2,
  "last_capture": "2024-12-30T14:35:42Z",
  "error_code": 0
}
```

## 运动检测 API

### `/api/motion/status` - 获取运动检测状态

获取运动检测配置和状态。

**请求**
```http
GET /api/motion/status
Host: 192.168.1.100
```

**响应**
```json
{
  "enabled": true,
  "threshold": 5,
  "cooldown_time": 10,
  "save_photo": true,
  "upload_photo": true,
  "detected": false,
  "last_event": "2024-12-30T14:30:25Z",
  "total_events": 125,
  "recent_events": [
    {
      "timestamp": "2024-12-30T14:30:25Z",
      "photo": "2024-12-30_14-30-25.jpg",
      "uploaded": true
    }
  ]
}
```

### `/api/motion/toggle` - 切换运动检测

启用/禁用运动检测（需要认证）。

**请求**
```http
POST /api/motion/toggle?enabled=true
Host: 192.168.1.100
X-Password: admin
```

**参数**
- `enabled`: 是否启用 (true/false)

**响应**
```json
{
  "success": true,
  "enabled": true,
  "message": "Motion detection enabled"
}
```

### `/api/motion/test` - 测试运动检测

临时启用运动检测并发送测试事件。

**请求**
```http
POST /api/motion/test
Host: 192.168.1.100
X-Password: admin
```

**响应**
```json
{
  "success": true,
  "test_started": true,
  "message": "Motion detection test started for 30 seconds"
}
```

## 存储和文件 API

### `/api/files` - 列出文件

获取 SD 卡上的照片文件列表。

**请求**
```http
GET /api/files?directory=/photos&limit=10
Host: 192.168.1.100
```

**参数**
- `directory`: 目录路径（可选，默认 `/photos`）
- `limit`: 返回的文件数量（可选，默认 20）

**响应**
```json
{
  "directory": "/photos",
  "total_files": 125,
  "total_size": 6710886,
  "files": [
    {
      "name": "2024-12-30_14-35-42.jpg",
      "path": "/photos/2024-12-30/2024-12-30_14-35-42.jpg",
      "size": 45232,
      "timestamp": "2024-12-30T14:35:42Z",
      "date": "2024-12-30",
      "time": "14-35-42"
    },
    {
      "name": "2024-12-30_14-30-25.jpg",
      "path": "/photos/2024-12-30/2024-12-30_14-30-25.jpg",
      "size": 38421,
      "timestamp": "2024-12-30T14:30:25Z",
      "date": "2024-12-30",
      "time": "14-30-25"
    }
  ]
}
```

### `/api/files/upload` - 上传文件到 NAS

将照片上传到 NAS 服务器（需要认证）。

**请求**
```http
POST /api/files/upload
Host: 192.168.1.100
X-Password: admin
Content-Type: application/json

{
  "filename": "2024-12-30_14-35-42.jpg",
  "path": "/photos/2024-12-30/2024-12-30_14-35-42.jpg",
  "overwrite": false
}
```

**参数**
- `filename`: 文件名
- `path`: 文件路径
- `overwrite`: 是否覆盖现有文件（可选，默认 false）

**响应**
```json
{
  "success": true,
  "url": "http://nas-server.photos/2024-12-30_14-35-42.jpg",
  "size": 45232,
  "upload_time": 2.5,
  "protocol": "HTTP"
}
```

### `/api/files/cleanup` - 清理文件

清理旧照片以释放存储空间（需要认证）。

**请求**
```http
POST /api/files/cleanup?keep_days=30
Host: 192.168.1.100
X-Password: admin
```

**参数**
- `keep_days`: 保留天数（可选，默认 7）

**响应**
```json
{
  "success": true,
  "files_removed": 45,
  "space_freed": 2345678,
  "message": "Cleaned 45 files, freed 2.3 MB"
}
```

## 流媒体 API

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

**参数**
- `resolution`: 分辨率参数（可选）
- `quality`: JPEG 质量（可选）
- `fps`: 帧率（可选）

### `/stream/snapshot` - 流媒体快照

从当前流捕获单帧。

**请求**
```http
GET /stream/snapshot
Host: 192.168.1.100
```

**响应**
```http
HTTP/1.1 200 OK
Content-Type: image/jpeg

[JPEG 帧数据]
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

**请求**
```http
POST /api/reset?mode=full
Host: 192.168.1.100
X-Password: admin
```

**参数**
- `mode`: 重置模式 (network-full, camera-full, full)

**响应**
```json
{
  "success": true,
  "message": "Factory reset completed",
  "restart_time": 3
}
```

### `/api/led` - 控制 LED

控制状态 LED（需要认证）。

**请求**
```http
POST /api/led?state=on
Host: 192.168.1.100
X-Password: admin
```

**参数**
- `state`: LED 状态 (on, off, blink)
- `delay`: 闪烁间隔（毫秒，仅适用于 blink）

**响应**
```json
{
  "success": true,
  "state": "on",
  "previous_state": "blink"
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

# HELP upload_success_total Successful uploads
# TYPE upload_success_total counter
upload_success_total 120
```

### `/api/health` - 健康检查

执行系统健康检查。

**请求**
```http
GET /api/health
Host: 192.168.1.100
```

**响应**
```json
{
  "status": "healthy",
  "checks": {
    "camera": {
      "status": "ok",
      "response_time": 45
    },
    "wifi": {
      "status": "ok",
      "signal": -45
    },
    "storage": {
      "status": "ok",
      "space_available": "1.2GB"
    },
    "memory": {
      "status": "ok",
      "heap": "44KB",
      "psram": "1.6MB"
    }
  },
  "timestamp": "2024-12-30T14:35:42Z"
}
```

## cURL 使用示例

### 基本状态检查
```bash
# 获取设备状态
curl -s http://192.168.1.100/api/status | jq .

# 获取简化的状态
curl -s http://192.168.1.100/api/status/simplified | jq .
```

### 配置管理
```bash
# 扫描 WiFi 网络
curl -s http://192.168.1.100/api/config/wifi/scan | jq .

# 更新 WiFi 配置
curl -X POST http://192.168.1.100/api/config/update \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"wifi":{"ssid":"MyNetwork","password":"MyPass"}}'

# 获取当前配置
curl -s http://192.168.1.100/api/config | jq .
```

### 摄像头控制
```bash
# 捕获照片
curl -X POST http://192.168.1.100/api/capture \
  -H "Content-Type: application/json" \
  -d '{"quality":10,"resolution":1}'

# 下载照片
curl -o photo.jpg http://192.168.1.100/capture

# 查看摄像头状态
curl -s http://192.168.1.100/api/camera/status | jq .
```

### 运动检测
```bash
# 查看运动检测状态
curl -s http://192.168.1.100/api/motion/status | jq .

# 切换运动检测
curl -X POST http://192.168.1.100/api/motion/toggle?enabled=true \
  -H "X-Password: admin"

# 测试运动检测
curl -X POST http://192.168.1.100/api/motion/test \
  -H "X-Password: admin"
```

### 文件管理
```bash
# 列出文件
curl -s "http://192.168.1.100/api/files?limit=5" | jq .

# 上传文件到 NAS
curl -X POST http://192.168.1.100/api/files/upload \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"filename":"2024-12-30_14-35-42.jpg","path":"/photos/2024-12-30/2024-12-30_14-35-42.jpg"}'

# 清理旧文件
curl -X POST "http://192.168.1.100/api/files/cleanup?keep_days=30" \
  -H "X-Password: admin"
```

### 系统管理
```bash
# 重启设备
curl -X POST http://192.168.1.100/api/reboot \
  -H "X-Password: admin"

# 恢复出厂设置
curl -X POST http://192.168.1.100/api/reset \
  -H "X-Password: admin"

# 控制 LED
curl -X POST "http://192.168.1.100/api/led?state=blink&delay=1000" \
  -H "X-Password: admin"
```

### 监控和指标
```bash
# 获取 Prometheus 指标
curl -s http://192.168.1.100/metrics

# 健康检查
curl -s http://192.168.1.100/api/health | jq .
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
- **上传队列**：最多 50 个文件排队
- **请求大小**：最大 1MB
- **响应大小**：未限制（流除外）

## 安全注意事项

1. **Web 密码**：在生产环境中更改默认密码
2. **HTTPS**：建议在支持的环境中启用
3. **网络隔离**：将设备放在专用网络中
4. **访问控制**：限制 API 访问 IP 范围
5. **定期更新**：保持固件最新版本以修复安全漏洞