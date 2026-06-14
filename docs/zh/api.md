#MW|[![ESP32](https://img.shields.io/badge/ESP32-Esp32--cam-blue.svg)](https://github.com/espressif/esp-idf) [![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/) [![OV2640](https://img.shields.io/badge/Camera-OV2640-orange.svg)](https://www.ovt.com/products/ov2640.html) [![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) 
#KM|
#WQ|> 🌐 [English Documentation](../en/api.md)
#RW|
#NX|# API 参考文档
#SY|
#TQ|本文档提供 MiBee Cam 固件 REST API 的完整参考，包括所有端点、参数和示例。
#XW|
#SN|## 概述
#SK|
#RX|MiBee Cam 提供 RESTful API 接口，允许通过 HTTP 请求控制和监控系统。API 支持以下功能：
#TX|
#KH|- **设备状态监控**：获取系统健康信息、网络状态和指标
#WX|- **配置管理**：更新 WiFi、摄像头和其他设置
#ZY|- **文件管理**：SD 卡照片浏览、下载和删除
#SZ|- **视频流**：实时 MJPEG 流传输
#HV|- **录像控制**：启动/停止 AVI 视频录制
#KY|- **运动检测**：帧差分运动检测和拍照
#YQ|
#WQ|## API 基础信息
#ZP|
#WX|### 基础 URL
#KW|
#QZ|- **STA 模式**：`http://<设备-ip>/`
#BV|- **AP 模式**：`http://192.168.4.1/`
#JJ|
#TN|### 重要说明
#ZR|
#VX|⚠️ **摄像头初始化工作流程**：为了解决 ESP32 已知的 DMA 冻结问题，固件采用"先启动 WiFi STA 模式，在回调中初始化摄像头"的策略。启动时会延迟 1-2 秒进行摄像头初始化。
#SZ|
#SB|### 认证
#QY|
#HB|某些需要身份验证的操作：
#YW|```http
#KK|X-Password: admin
#RJ|```
#BN|
#WT|### 响应格式
#RZ|- **成功响应**：HTTP 200 + JSON 数据
#HM|- **错误响应**：HTTP 400/500 + 错误消息
#KS|- **流响应**：HTTP 200 + multipart/x-mixed-replace 数据
#QB|
#MQ|### 常见错误代码
#KT|
#BX|| 代码 | 描述 |
#HY||------|-------------|
#ZN|| 200 | 成功 |
#ZT|| 400 | 请求参数错误 |
#BS|| 401 | 认证失败 |
#MS|| 404 | 资源未找到 |
#JX|| 500 | 服务器内部错误 |
#NB|
#JP|## 设备状态 API
#TW|
#ZH|### `/api/status` - 获取设备状态
#WH|
#MH|获取设备完整状态信息。
#QH|
#NZ|**请求**
#YW|```http
#WX|GET /api/status
#HT|Host: 192.168.1.100
#WV|```
#PZ|
#BP|**响应**
#YP|```json
#WT|{
#RX|  "status": "running",
#JH|  "hostname": "MiBeeCam",
#RP|  "uptime": 86400,
#JW|  "wifi": {
#RP|    "mode": "STA",
#JP|    "connected": true,
#QM|    "ssid": "MyWiFi",
#HR|    "bssid": "AA:BB:CC:DD:EE:FF",
#NJ|    "ip": "192.168.1.100",
#SB|    "signal": -45,
#VZ|    "connected_time": 3600,
#QR|    "ssid_2": "",
#WB|    "pass_2": "",
#YJ|    "allow_ap_fallback": false
#QY|  },
#RK|  "camera": {
#HR|    "resolution": "VGA",
#ZQ|    "fps": 15,
#PX|    "quality": 12,
#JX|    "format": "JPEG",
#SQ|    "buffer": 0.6,
#PT|    "status": "active"
#YP|  },
#WT|  "storage": {
#MN|    "sd_card": {
#VY|      "present": true,
#XT|      "total": 15892485120,
#HP|      "used": 5242880,
#PH|      "available": 15887252240,
#XV|      "percent_used": 0.033
#BZ|    }
#JJ|  },
#KX|  "motion": {
#QP|    "enabled": true,
#XH|    "detected": false,
#PH|    "last_event": "2024-12-30T14:30:25Z",
#PZ|    "total_events": 125
#BN|  },
#TJ|  "system": {
#JZ|    "temperature": 45.2,
#RT|    "heap": 45012,
#SN|    "stack_watermark": 2048,
#QS|    "psram": 0,
#MS|    "time": "2024-12-30T14:35:42Z",
#HP|    "timezone": "CST-8",
#TW|    "config_version": 9,
#ZS|    "config_magic": 2864434392
#TR|  }
#KW|}
#PV|```
#BK|
#HJ|**字段说明**
#KM|- `status`: 设备状态 (running, ap_mode, error)
#PZ|- `uptime`: 运行时间（秒）
#HB|- `wifi`: WiFi 连接详细信息
#RH|- `camera`: 摄像头状态信息
#RN|- `storage`: 存储使用情况
#SN|- `motion`: 运动检测状态
#ZB|- `system`: 系统指标和时间
#YB|
#WT|## 配置 API
#XB|
#HX|### `/api/config` - 获取配置
#HP|
#ZH|获取当前系统配置。
#WP|
#NZ|**请求**
#YW|```http
#QS|GET /api/config
#HT|Host: 192.168.1.100
#JQ|```
#QS|
#BP|**响应**
#YP|```json
#MN|{
#NQ|  "wifi_ssid": "MyWiFi",
#ZY|  "wifi_pass": "MyPassword",
#NB|  "device_name": "MiBeeCam",
#ST|  "resolution": 0,
#BZ|  "fps": 15,
#VX|  "jpeg_quality": 12,
#QW|  "web_password": "",
#ZM|  "timezone": "CST-8",
#MP|  "motion_threshold": 30,
#PT|  "motion_cooldown": 5,
#MB|  "vflip": 0,
#TJ|  "wifi_tx_power": 80,
#JV|  "flash_threshold": 40,
#WH|  "timelapse_enabled": 0,
#NM|  "timelapse_interval_s": 60,
#WB|  "timelapse_burst_count": 3,
#VQ|  "record_mode": 0,
#TM|  "allow_ap_fallback": false
#QX|}
#KH|```
#QB|
#HZ|**注意**：密码字段不会在响应中返回。
#BT|
#BJ|### `/api/config` - 更新配置
#HM|
#YT|更新系统配置（需要认证）。
#VK|
#NZ|**请求**
#YW|```http
#XB|POST /api/config
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#XN|Content-Type: application/json
#JZ|
#JM|{
#HJ|  "wifi_ssid": "NewNetwork",
#BS|  "wifi_pass": "NewPassword",
#ZK|  "resolution": 1,
#TJ|  "motion_threshold": 25
#JM|}
#JP|```
#XH|
#WY|**参数**
#SP|- `wifi_ssid`: WiFi 网络名称
#WY|- `wifi_pass`: WiFi 密码
#ZZ|- `device_name`: 设备主机名
#ZW|- `resolution`: 分辨率 (0=VGA, 1=SVGA, 2=XGA, 3=UXGA)
#QJ|- `fps`: 帧率目标
#YY|- `jpeg_quality`: JPEG 质量 (0-63，数字越小质量越高)
#NP|- `web_password`: Web 访问密码
#RN|- `timezone`: 时区字符串
#YH|- `motion_threshold`: 运动检测阈值 (1-100)
#MJ|- `motion_cooldown`: 运动检测冷却时间（秒）
#WV|- `vflip`: 垂直翻转 (0=关闭, 1=开启)
#WY|- `wifi_tx_power`: WiFi 发射功率 (0-80，80=20dBm)
#PQ|- `flash_threshold`: 闪光阈值 (0-100)
#PR|- `record_mode`: 录像模式 (0=关闭, 1=连续, 2=延时, 3=动态)
#TM|- `allow_ap_fallback`: 是否允许 AP 模式回退
#SR|
#BP|**响应**
#YP|```json
#TZ|{
#TB|  "success": true,
#JK|  "message": "Configuration updated successfully",
#MH|  "needs_reboot": true
#KK|}
#VX|```
#HT|
#BM|### `/api/auth` - 验证密码
#MK|
#YK|验证 Web 访问密码。
#MJ|
#NZ|**请求**
#YW|```http
#TH|GET /api/auth
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#PV|```
#WV|
#BP|**响应**
#YP|```json
#VX|{
#TB|  "success": true,
#YW|  "message": "Password verified"
#ZW|}
#PS|```
#XP|
#VP|## 摄像头 API
#TK|
#MV|### `/capture` - 捕获照片
#VY|
#MX|捕获单张 JPEG 照片。
#PT|
#NZ|**请求**
#YW|```http
#VR|GET /capture
#HT|Host: 192.168.1.100
#HT|```
#TZ|
#BP|**响应**
#YW|```http
#JY|HTTP/1.1 200 OK
#PZ|Content-Type: image/jpeg
#JW|Content-Length: 45232
#PY|
#ZH|[JPEG 二进制数据]
#SY|```
#YM|
#SM|### `/stream` - MJPEG 流
#WJ|
#RX|提供实时 MJPEG 视频流。
#SV|
#NZ|**请求**
#YW|```http
#NN|GET /stream
#HT|Host: 192.168.1.100
#NQ|```
#YZ|
#BP|**响应**
#YW|```http
#JY|HTTP/1.1 200 OK
#XS|Content-Type: multipart/x-mixed-replace; boundary=frame
#SV|Connection: keep-alive
#WQ|
#HN|--frame
#PZ|Content-Type: image/jpeg
#WY|
#YT|[JPEG 帧数据 1]
#HN|--frame
#PZ|Content-Type: image/jpeg
#XQ|
#NW|[JPEG 帧数据 2]
#HN|--frame
#PZ|Content-Type: image/jpeg
#XS|
#HT|[JPEG 帧数据 3]
#VM|```
#BV|
#ZY|## 文件管理 API
#VK|
#TQ|### `/api/files` - 列出文件
#NP|
#QQ|获取 SD 卡上的照片文件列表。
#QN|
#NZ|**请求**
#YW|```http
#XB|GET /api/files?limit=10&offset=0
#HT|Host: 192.168.1.100
#RR|```
#NK|
#WY|**参数**
#ZW|- `limit`: 返回的文件数量（可选，默认 20）
#BM|- `offset`: 分页偏移量（可选，默认 0）
#RB|
#BP|**响应**
#YP|```json
#SS|{
#ZN|  "files": [
#SR|    {
#YN|      "name": "2024-12-30_14-35-42.jpg",
#HN|      "size": 45232,
#XX|      "timestamp": 1735565742
#MN|    },
#WS|    {
#KN|      "name": "2024-12-30_14-30-25.jpg",
#NH|      "size": 38421,
#QH|      "timestamp": 1735565425
#WP|    }
#JY|  ],
#NK|  "total": 125,
#MH|  "limit": 10,
#BS|  "offset": 0
#TM|}
#WT|```
#QJ|
#XN|### `/api/files` - 删除文件
#MB|
#YS|删除 SD 卡上的照片文件（需要认证）。
#YP|
#NZ|**请求**
#YW|```http
#YZ|DELETE /api/files?name=2024-12-30_14-35-42.jpg
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#XQ|```
#SQ|
#WY|**参数**
#BM|- `name`: 要删除的文件名（必需）
#ZH|
#BP|**响应**
#YP|```json
#BQ|{
#TB|  "success": true,
#PT|  "message": "File deleted successfully"
#WT|}
#MV|```
#XK|
#TJ|### `/api/download` - 下载文件
#NX|
#YY|下载 SD 卡上的照片文件。
#RR|
#NZ|**请求**
#YW|```http
#BJ|GET /api/download?name=2024-12-30_14-35-42.jpg
#HT|Host: 192.168.1.100
#PM|```
#ZK|
#WY|**参数**
#PJ|- `name`: 要下载的文件名（必需）
#SV|
#BP|**响应**
#YW|```http
#JY|HTTP/1.1 200 OK
#PZ|Content-Type: image/jpeg
#PZ|Content-Disposition: attachment; filename="2024-12-30_14-35-42.jpg"
#JW|Content-Length: 45232
#SW|
#ZH|[JPEG 二进制数据]
#WV|```
#BM|
#SH|## 录像控制 API
#SS|
#SW|### `/api/record` - 启动/停止录像
#VN|
#PK|启动或停止视频录制（需要认证）。
#MY|
#NZ|**请求**
#YW|```http
#ZK|POST /api/record?action=start
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#RT|```
#MZ|
#WY|**参数**
#NQ|- `action`: `start` 或 `stop`（必需）
#PN|
#BP|**响应**
#YP|```json
#YJ|{
#TB|  "success": true,
#NW|  "message": "Recording started",
#KK|  "mode": "continuous"
#QB|}
#JN|```
#ZT|
#XB|**Curl 示例**
#BV|```bash
#TT|# 启动连续录像
#QH|curl -X POST "http://192.168.1.100/api/record?action=start" \
#XN|  -H "X-Password: admin"
#WT|
#SS|# 停止录像
#WW|curl -X POST "http://192.168.1.100/api/record?action=stop" \
#XN|  -H "X-Password: admin"
#NH|```
#XK|
#VZ|### `/api/record` - 获取录像状态
#PN|
#HN|返回录像状态和信息。
#RZ|
#NZ|**请求**
#YW|```http
#ZT|GET /api/record
#HT|Host: 192.168.1.100
#HV|```
#WY|
#BP|**响应**
#YP|```json
#WZ|{
#VY|  "recording": true,
#TK|  "mode": "continuous",
#JV|  "duration": 3600,
#BP|  "segments": 12,
#ZR|  "total_size": 125829120,
#JN|  "current_file": "rec_2024-12-30_14-30-25.avi"
#ZN|}
#JR|```
#WR|
#TP|## 存储管理 API
#SW|
#KV|### `/api/storage` - 获取存储状态
#MS|
#MJ|返回存储使用情况和清理状态。
#YY|
#NZ|**请求**
#YW|```http
#HJ|GET /api/storage
#HT|Host: 192.168.1.100
#WB|```
#SB|
#BP|**响应**
#YP|```json
#VK|{
#YN|  "sd_card": {
#QP|    "available": true,
#HJ|    "total": 31457280,
#RS|    "free": 15728640,
#YX|    "used": 15728640,
#TT|    "photo_count": 25,
#NZ|    "video_count": 3
#VM|  },
#WY|  "cleanup": {
#VQ|    "photo_threshold": 20,
#VY|    "video_threshold": 10,
#RB|    "last_cleanup": "2024-12-30T14:30:00Z"
#ZN|  }
#NZ|}
#KT|```
#HM|
#BM|## 延时摄影 API
#YX|
#TK|### `/api/timelapse/start` - 启动延时摄影
#PN|
#JM|启动延时摄影（需要认证）。
#PH|
#NZ|**请求**
#YW|```http
#WP|POST /api/timelapse/start
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#BP|```
#YH|
#BP|**响应**
#YP|```json
#TZ|{
#TB|  "success": true,
#SY|  "message": "Timelapse started",
#HH|  "interval": 60,
#BT|  "burst_count": 3
#TJ|}
#QV|```
#HR|
#BV|### `/api/timelapse/stop` - 停止延时摄影
#HB|
#YK|停止延时摄影（需要认证）。
#QJ|
#NZ|**请求**
#YW|```http
#QJ|POST /api/timelapse/stop
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#WH|```
#QS|
#BP|**响应**
#YP|```json
#MS|{
#TB|  "success": true,
#PR|  "message": "Timelapse stopped"
#BT|}
#YN|```
#VJ|
#TS|### `/api/timelapse/status` - 获取延时摄影状态
#MK|
#PJ|返回延时摄影状态。
#HR|
#NZ|**请求**
#YW|```http
#MQ|GET /api/timelapse/status
#HT|Host: 192.168.1.100
#QQ|```
#VN|
#BP|**响应**
#YP|```json
#RJ|{
#BK|  "enabled": true,
#WY|  "running": true,
#HH|  "interval": 60,
#PR|  "burst_count": 3,
#KT|  "photos_taken": 45,
#XH|  "last_photo": "2024-12-30_14-30-25.jpg"
#HS|}
#HZ|```
#XN|
#NP|## 系统管理 API
#JZ|
#BY|### `/api/flash` - 控制闪光灯
#SB|
#YT|控制 LED 闪光灯（需要认证）。
#XJ|
#NZ|**请求**
#YW|```http
#KH|POST /api/flash
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#KZ|```
#RM|
#BP|**响应**
#YP|```json
#TS|{
#TB|  "success": true,
#SJ|  "message": "Flash control executed"
#TV|  "flash_state": "auto"
#BS|}
#JZ|```
#PK|
#PM|### `/api/reboot` - 重启设备
#JS|
#PJ|重启设备（需要认证）。
#SW|
#NZ|**请求**
#YW|```http
#QN|POST /api/reboot
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#ZZ|```
#QY|
#BP|**响应**
#YP|```json
#RZ|{
#TB|  "success": true,
#MX|  "message": "Rebooting device",
#BT|  "restart_time": 5
#YY|}
#KV|```
#SK|
#BX|### `/api/reset` - 恢复出厂设置
#KW|
#MB|恢复出厂设置（需要认证）。
#SS|
#WJ|**注意**：BOOT 按钮工厂重置功能已禁用，因为 GPIO0 被用作摄像头 XCLK 引脚。请使用此 API 端点进行重置。
#MH|
#NZ|**请求**
#YW|```http
#QN|POST /api/reset
#HT|Host: 192.168.1.100
#KK|X-Password: admin
#ZZ|```
#QY|
#BP|**响应**
#YP|```json
#RZ|{
#TB|  "success": true,
#MX|  "message": "Factory reset completed",
#BT|  "restart_time": 3
#YY|}
#KV|```
#SK|
#QT|## 监控和指标 API
#MB|
#TK|### `/api/metrics` - Prometheus 指标
#JT|
#ZH|提供兼容 Prometheus 格式的系统指标。
#PV|
#NZ|**请求**
#YW|```http
#ZX|GET /api/metrics
#HT|Host: 192.168.1.100
#ZT|```
#RY|
#BP|**响应**
#TY|```prometheus
#RH|# HELP esp32_heap_free Heap memory free bytes
#NS|# TYPE esp32_heap_free gauge
#HN|esp32_heap_free 45012
#KJ|
#MX|# HELP esp32_stack_watermark Stack watermark in bytes
#ZZ|# TYPE esp32_stack_watermark gauge
#SW|esp32_stack_watermark 2048
#VP|
#BY|# HELP esp32_temperature Temperature in Celsius
#RZ|# TYPE esp32_temperature gauge
#XV|esp32_temperature 45.2
#KW|
#XZ|# HELP wifi_signal_strength WiFi signal strength in dBm
#YH|# TYPE wifi_signal_strength gauge
#PX|wifi_signal_strength -45
#RQ|
#RM|# HELP esp32_uptime System uptime in seconds
#TJ|# TYPE esp32_uptime counter
#YZ|esp32_uptime 86400
#YH|
#HQ|# HELP motion_events_total Total motion detection events
#WZ|# TYPE motion_events_total counter
#JJ|motion_events_total 125
#SJ|
#JJ|# HELP photos_taken_total Total photos taken
#PZ|# TYPE photos_taken_total counter
#ZW|photos_taken_total 245
#PP|```
#BS|
#QT|## cURL 使用示例
#PB|
#KX|### 基本状态检查
#BV|```bash
#HQ|# 获取设备状态
#QM|curl -s http://192.168.1.100/api/status | jq .
#JP|
#QK|# 获取配置
#JY|curl -s http://192.168.1.100/api/config | jq .
#HY|```
#HY|
#QP|### 配置管理
#BV|```bash
#QS|# 更新配置
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#JZ|  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"MyPass","resolution":1}'
#RZ|
#WZ|# 验证密码
#MZ|curl -H "X-Password: admin" http://192.168.1.100/api/auth
#QJ|```
#ZV|
#MH|### 摄像头控制
#BV|```bash
#BH|# 捕获照片
#WR|curl http://192.168.1.100/capture -o photo.jpg
#TY|
#RT|# 查看流（在浏览器中打开）
#ST|# http://192.168.1.100/stream
#BY|```
#WX|
#NX|### 文件管理
#BV|```bash
#XZ|# 列出文件
#VM|curl -s "http://192.168.1.100/api/files?limit=5" | jq .
#MR|
#SW|# 下载文件
#PR|curl -o photo.jpg "http://192.168.1.100/api/download?name=2024-12-30_14-35-42.jpg"
#VV|
#YX|# 删除文件
#SX|curl -X DELETE "http://192.168.1.100/api/files?name=2024-12-30_14-35-42.jpg" \
#XN|  -H "X-Password: admin"
#TY|```
#VM|
#VK|### 录像控制
#BV|```bash
#RT|# 启动录像
#QH|curl -X POST "http://192.168.1.100/api/record?action=start" \
#XN|  -H "X-Password: admin"
#QB|
#SS|# 停止录像
#WW|curl -X POST "http://192.168.1.100/api/record?action=stop" \
#XN|  -H "X-Password: admin"
#HY|
#MM|# 查看录像状态
#RR|curl -s http://192.168.1.100/api/record | jq .
#XK|```
#ZQ|
#ST|### 延时摄影
#BV|```bash
#HP|# 启动延时摄影
#BJ|curl -X POST http://192.168.1.100/api/timelapse/start \
#XN|  -H "X-Password: admin"
#RN|
#KJ|# 停止延时摄影
#BY|curl -X POST http://192.168.1.100/api/timelapse/stop \
#XN|  -H "X-Password: admin"
#TH|
#PX|# 查看延时状态
#XZ|curl -s http://192.168.1.100/api/timelapse/status | jq .
#VM|```
#RN|
#ZN|### 系统管理
#BV|```bash
#YK|# 控制闪光灯
#ZM|curl -X POST http://192.168.1.100/api/flash \
#XN|  -H "X-Password: admin"
#TY|
#HB|# 重启设备
#VT|curl -X POST http://192.168.1.100/api/reboot \
#XN|  -H "X-Password: admin"
#TY|
#HB|# 恢复出厂设置
#VT|curl -X POST http://192.168.1.100/api/reset \
#XN|  -H "X-Password: admin"
#QR|
#XV|# 获取 Prometheus 指标
#KN|curl -s http://192.168.1.100/api/metrics
#PN|```
#SN|
#MS|## 错误处理
#RN|
#WJ|### 常见错误及解决方法
#PX|
#HB|**400 Bad Request**
#SQ|- 原因：请求参数错误
#BB|- 解决：检查请求格式和参数
#WY|
#BX|**401 Unauthorized**
#WM|- 原因：认证失败
#BK|- 解决：检查 `X-Password` 头部
#NT|
#WV|**404 Not Found**
#RX|- 原因：端点不存在
#PR|- 解决：检查 API 路径
#VN|
#TW|**500 Internal Server Error**
#QN|- 原因：服务器内部错误
#RH|- 解决：查看设备日志，检查资源使用情况
#KJ|
#SP|### 错误响应格式
#YP|```json
#WT|{
#WJ|  "success": false,
#KP|  "error": "Error message",
#QB|  "code": "ERROR_CODE",
#PV|  "details": "Additional error details"
#ZW|}
#TK|```
#PK|
#NZ|## 安全注意事项
#KQ|
#PK|- **Web 密码**：在生产环境中更改默认密码
#KY|- **HTTPS**：当前不支持，建议在专用网络中使用
#YT|- **网络隔离**：将设备放在专用网络中
#YQ|- **访问控制**：限制 API 访问 IP 范围
#ZX|- **定期更新**：保持固件最新版本以修复安全漏洞
#JM|
#VJ|### GPIO14 共享引脚说明
#YR|
#NJ|⚠️ **重要提醒**：GPIO14 引脚在 MiBee Cam 上被摄像头和 SD 卡共享使用（摄像头 XCLK 和 SD 卡 CLK）。固件采用严格的初始化顺序来解决冲突：SD 卡初始化 → 摄像头初始化 → SD 卡重新初始化。如果遇到相关功能异常，请检查此引脚连接。

---

## API 终端列表

| 终端 | 方法 | 描述 |
|------|------|------|
| `/api/status` | GET | 设备状态和传感器数据 |
| `/api/config` | GET | 当前配置 JSON |
| `/api/config` | POST | 更新配置 |
| `/api/auth` | GET | 验证密码 |
| `/api/flash` | POST | 控制 LED 闪光灯 |
| `/api/reboot` | POST | 重启设备 |
| `/api/reset` | POST | 恢复出厂设置 |
| `/api/metrics` | GET | Prometheus 指标 |
| `/capture` | GET | 单 JPEG 捕获 |
| `/stream` | GET | MJPEG 视频流 |
| `/api/files` | GET | 列出 SD 卡照片 |
| `/api/download` | GET | 下载照片 |
| `/api/files` | DELETE | 删除照片 |
| `/api/record` | POST | 录像控制（启动/停止） |
| `/api/record` | GET | 录像状态和信息 |
| `/api/storage` | GET | 存储使用情况 |
| `/api/timelapse/start` | POST | 启动延时摄影 |
| `/api/timelapse/stop` | POST | 停止延时摄影 |
| `/api/timelapse/status` | GET | 延时摄影状态 |