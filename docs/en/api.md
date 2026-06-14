#YV|![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main)
#MP|![Platform](https://img.shields.io/badge/platform-ESP32-blue)
#TY|![Camera](https://img.shields.io/badge/camera-OV2640-green)
#XV|![License](https://img.shields.io/badge/license-MIT-orange)
#BT|
#PQ|# API Reference
#HN|
#WV|This document provides a complete reference for the MiBee Cam REST API. All endpoints are available via HTTP and return JSON responses where applicable.
#JT|
#QK|## Base URL
#TJ|
#HB|```
#ZX|http://<device-ip>:80/
#YV|```
#RJ|
#ZZ|Replace `<device-ip>` with your device's IP address.
#HX|
#RT|## Authentication
#YT|
#JN|### Protected Endpoints
#KH|The following endpoints require authentication:
#ZP|
#TH|- `POST /api/config`
#QP|- `POST /api/reset` — Factory reset (via web interface, BOOT button not functional)
#ZK|- `POST /api/reboot` — Reboot device
#HK|- `POST /api/auth` — Verify web password
#ZK|
#RB|### Authentication Method
#QB|Use the `X-Password` header with the web password (default: "admin"):
#ZR|
#BV|```bash
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#SS|  -d '{"wifi_ssid":"MyNetwork"}'
#XS|```
#MV|
#XR|### Password Configuration
#RR|Set web password via POST /api/config:
#MS|
#YP|```json
#SH|{
#YH|  "web_password": "your-password-here"
#WQ|}
#PM|```
#HP|## API Endpoints
#TJ|
#QZ|### 1. Device Status
#BY|
#VQ|**GET /api/status**
#QW|
#KN|Returns comprehensive device status information.
#NM|
#RK|#### Response
#YP|```json
#KZ|{
#NK|  "device": {
#HS|    "name": "MiBeeCam",
#VX|    "uptime": 12345,
#KV|    "version": "1.0.0"
#JQ|  },
#RK|  "camera": {
#HW|    "status": "active",
#HR|    "resolution": "VGA",
#ZQ|    "fps": 15,
#PX|    "quality": 12,
#HP|    "last_capture": "2024-12-30T14:30:25Z"
#RY|  },
#JW|  "wifi": {
#VB|    "state": "connected",
#RP|    "mode": "STA",
#XK|    "ssid": "MyNetwork",
#NJ|    "ip": "192.168.1.100",
#HQ|    "rssi": -45,
#BS|    "bssid": "AA:BB:CC:DD:EE:FF",
#VY|    "ssid_2": "MyNetwork2",
#QX|    "allow_ap_fallback": true
#NS|  },
#WT|  "storage": {
#QP|    "available": true,
#XK|    "type": "SD_CARD",
#HJ|    "total": 31457280,
#RS|    "free": 15728640,
#YX|    "used": 15728640,
#SR|    "photo_count": 25
#SY|  },
#KX|  "motion": {
#QP|    "enabled": true,
#KP|    "threshold": 5,
#ZK|    "cooldown": 10,
#XW|    "events": 123,
#VP|    "last_event": "2024-12-30T14:28:15Z"
#PQ|  },
#TJ|  "system": {
#BY|    "heap_free": 45236,
#PW|    "psram_free": 1048576,
#PQ|    "temperature": 42.5,
#HK|    "boot_time": "2024-12-30T14:25:00Z",
#WM|    "uptime_seconds": 125,
#VY|    "flash_on": false
#PT|  }
#VQ|}
#YQ|```
#SR|
#JB|#### Curl Example
#BV|```bash
#JK|curl -s http://192.168.1.100/api/status | python -m json.tool
#YV|```
#VS|
#JJ|### 2. Authentication
#TS|
#XR|#### POST /api/auth
#BP|
#NN|Verify web password and return authentication status.
#RS|
#KV|**Request Body**
#YP|```json
#BZ|{
#BN|  "password": "admin"
#ZY|}
#JH|```
#YK|
#KV|**Response**
#YP|```json
#VV|{
#TB|  "authenticated": true,
#JK|  "message": "Authentication successful"
#TX|}
#VM|```
#VQ|
#SX|**Curl Example**
#BV|```bash
#NY|# Check authentication
#QN|curl -X POST http://192.168.1.100/api/auth \
#VQ|  -H "Content-Type: application/json" \
#VR|  -d '{"password":"admin"}'
#HM|
#VJ|# Change password
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#YM|  -d '{"web_password":"newpassword"}'
#QN|
#TT|# Verify new password
#QN|curl -X POST http://192.168.1.100/api/auth \
#VQ|  -H "Content-Type: application/json" \
#VR|  -d '{"password":"newpassword"}'
#NT|```
#ZV|
#JH|### 3. Configuration Management
#TS|
#XM|#### GET /api/config
#BP|
#RS|Returns current configuration (passwords are not included).
#YX|
#KV|**Response**
#YP|```json
#YZ|{
#BN|  "wifi_ssid": "MyNetwork",
#NN|  "wifi_pass": "[hidden]",
#ZY|  "device_name": "MiBeeCam",
#NB|  "resolution": 0,
#ST|  "fps": 15,
#BZ|  "jpeg_quality": 12,
#HK|  "web_password": "[hidden]",
#QW|  "timezone": "CST-8",
#ZZ|  "motion_threshold": 5,
#BK|  "motion_cooldown": 10,
#VY|  "wifi_ssid_2": "MyNetwork2",
#XM|  "wifi_pass_2": "[hidden]",
#WQ|  "allow_ap_fallback": true,
#MV|  "timelapse_enabled": false,
#XT|  "timelapse_interval_s": 60,
#TQ|  "timelapse_burst_count": 3,
#JK|  "config_version": 9,
#JH|  "config_magic": 2864434392
#JH|}
#YK|```
#HP|
#QR|**Curl Example**
#BV|```bash
#RM|curl -s http://192.168.1.100/api/config | python -m json.tool
#QB|```
#QZ|
#ZJ|#### POST /api/config
#NQ|
#XP|Updates device configuration. Requires authentication.
#KK|
#MS|**Request Body**
#YP|```json
#BZ|{
#BN|  "wifi_ssid": "MyNetwork",
#QN|  "wifi_pass": "MyPassword",
#ZY|  "device_name": "MiBeeCam",
#NB|  "resolution": 0,
#ST|  "fps": 15,
#BZ|  "jpeg_quality": 12,
#QW|  "timezone": "CST-8",
#ZZ|  "motion_threshold": 5,
#XZ|  "motion_cooldown": 10,
#ZY|  "wifi_ssid_2": "MyNetwork2",
#NB|  "wifi_pass_2": "MyPassword2",
#VY|  "allow_ap_fallback": true,
#MV|  "timelapse_enabled": true,
#XT|  "timelapse_interval_s": 30,
#TQ|  "timelapse_burst_count": 5
#ZK|}
#MQ|```
#VX|
#KV|**Response**
#YP|```json
#VV|{
#TB|  "success": true,
#JK|  "message": "Configuration updated successfully",
#VK|  "restart_required": false
#TX|}
#VM|```
#VQ|
#SX|**Curl Examples**
#BV|```bash
#NY|# Update WiFi credentials
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#YN|  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"MyPassword"}'
#HM|
#VJ|# Update motion detection settings
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#YM|  -d '{"motion_threshold":3,"motion_cooldown":15}'
#QN|
#TT|# Update camera quality
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#XK|  -d '{"jpeg_quality":10}'
#NT|
#MZ|# Update timelapse settings
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#ZN|  -d '{"timelapse_enabled":true,"timelapse_interval_s":60}'
#YM|```
#RS|
#YZ|### 4. System Control
#BH|
#NS|#### POST /api/reset
#XN|
#YM|Resets configuration to factory defaults. Requires authentication.
#JZ|
#WH|**Important**: BOOT button is not functional (GPIO0 = camera XCLK). Use this endpoint instead.
#MH|
#KV|**Response**
#YP|```json
#MH|{
#TB|  "success": true,
#JN|  "message": "Configuration reset to defaults",
#KN|  "restart_required": true
#HX|}
#RM|```
#XQ|
#QR|**Curl Example**
#BV|```bash
#VT|curl -X POST http://192.168.1.100/api/reset \
#XN|  -H "X-Password: admin"
#JR|```
#SR|
#KP|#### POST /api/reboot
#KZ|
#JK|Reboots the device. Requires authentication.
#RZ|
#KV|**Response**
#YP|```json
#JB|{
#TB|  "success": true,
#JB|  "message": "Device reboot initiated",
#TV|  "restart_time": 5
#TV|}
#MT|```
#MJ|
#QR|**Curl Example**
#BV|```bash
#ZM|curl -X POST http://192.168.1.100/api/reboot \
#XN|  -H "X-Password: admin"
#HH|```
#NQ|
#TK|### 5. Camera Functions
#YZ|
#MP|#### GET /capture
#WJ|
#MZ|Captures a single JPEG frame and returns it as binary data.
#ZY|
#KV|**Response**
#NN|- Content-Type: `image/jpeg`
#WH|- Body: JPEG binary data
#QW|
#SX|**Curl Examples**
#BV|```bash
#BJ|# Capture and save to file
#KQ|curl -o capture.jpg http://192.168.1.100/capture
#PT|
#TS|# Capture and display in terminal (if small)
#PY|curl -s http://192.168.1.100/capture | file -
#JX|
#JY|# Test capture without saving
#QP|curl -I http://192.168.1.100/capture
#YB|```
#MX|
#PJ|#### GET /stream
#VZ|
#TN|Provides MJPEG streaming using multipart/x-mixed-replace format.
#BK|
#KV|**Response**
#QS|- Content-Type: `multipart/x-mixed-replace; boundary=frame`
#JT|- Body: MJPEG frames with boundary markers
#YM|
#MW|**Note**: Camera initialization is deferred after WiFi connection to avoid ESP32 DMA freeze issues.
#WJ|
#SX|**Curl Examples**
#BV|```bash
#HT|# View stream in browser
#MH|http://192.168.1.100/stream
#RY|
#ZR|# Test stream connectivity
#QY|curl -s http://192.168.1.100/stream | head -c 200
#YZ|
#ZJ|# Save stream to file (first 10MB)
#VJ|curl -s http://192.168.1.100/stream | head -c 10485760 > stream.mjpeg
#WZ|
#WX|# Get stream info (headers only)
#WM|curl -I http://192.168.1.100/stream
#PB|```
#NB|
#TB|### 6. Prometheus Monitoring
#WY|
#RS|#### GET /metrics
#QT|
#JH|Returns Prometheus-compatible metrics for monitoring.
#XQ|
#KV|**Response**
#TH|- Content-Type: `text/plain`
#KW|- Body: Prometheus format metrics
#XS|
#SJ|**Response Example**
#VM|```
#TV|# HELP esp_heap_free_bytes Free heap memory in bytes
#SK|# TYPE esp_heap_free_bytes gauge
#KY|esp_heap_free_bytes 45236
#YH|
#HK|# HELP esp_psram_free_bytes Free PSRAM memory in bytes
#SH|# TYPE esp_psram_free_bytes gauge
#ZN|esp_psram_free_bytes 1048576
#HW|
#VJ|# HELP esp_uptime_seconds System uptime in seconds
#XN|# TYPE esp_uptime_seconds counter
#JB|esp_uptime_seconds 125
#BB|
#TN|# HELP esp_wifi_rssi WiFi signal strength in dBm
#PR|# TYPE esp_wifi_rssi gauge
#JM|esp_wifi_rssi -45
#PN|
#WW|# HELP esp_camera_frames_total Total camera frames captured
#MB|# TYPE esp_camera_frames_total counter
#WY|esp_camera_frames_total 250
#SZ|
#NR|# HELP esp_motion_events_total Total motion detection events
#PQ|# TYPE esp_motion_events_total counter
#PM|esp_motion_events_total 125
#RN|
#MX|# HELP esp_storage_free_bytes Free storage space in bytes
#ZH|# TYPE esp_storage_free_bytes gauge
#QW|esp_storage_free_bytes 15728640
#BV|
#ZB|# HELP esp_temperature_celsius Device temperature in Celsius
#JW|# TYPE esp_temperature_celsius gauge
#TQ|esp_temperature_celsius 42.5
#MJ|```
#HY|
#SX|**Curl Examples**
#BV|```bash
#NR|# Get health metrics
#XX|curl -s http://192.168.1.100/metrics
#QJ|
#YK|# Filter specific metrics
#TH|curl -s http://192.168.1.100/metrics | grep heap
#TT|
#XQ|# Export metrics for Prometheus
#RM|curl -s http://192.168.1.100/metrics > prometheus_metrics.txt
#RY|
#TP|# Monitor metrics in real-time
#SP|watch -n 5 "curl -s http://192.168.1.100/metrics | grep heap"
#XH|```
#XN|
#NM|### 7. SD Card File Management
#TP|
#HK|#### GET /api/files
#ZH|
#BQ|Returns list of SD card photos with metadata.
#TY|
#KV|**Response**
#YP|```json
#BM|{
#TB|  "success": true,
#MV|  "photos": [
#JZ|    {
#TX|      "name": "2024-12-30-14-30-25.jpg",
#RP|      "path": "/sdcard/photos/2024-12-30/2024-12-30-14-30-25.jpg",
#JV|      "size": 45236,
#YW|      "width": 640,
#JY|      "height": 480,
#RK|      "timestamp": "2024-12-30T14:30:25Z",
#TZ|      "type": "photo"
#TX|    },
#QZ|    {
#JN|      "name": "2024-12-30-14-32-10.jpg",
#SR|      "path": "/sdcard/photos/2024-12-30/2024-12-30-14-32-10.jpg",
#YZ|      "size": 48102,
#YW|      "width": 640,
#JY|      "height": 480,
#QV|      "timestamp": "2024-12-30T14:32:10Z",
#TZ|      "type": "photo"
#WB|    }
#RK|  ],
#VS|  "storage": {
#BY|    "total": 31457280,
#XP|    "free": 15728640,
#XT|    "used": 15728640
#PZ|  }
#KK|}
#JJ|```
#XS|
#JP|**Curl Examples**
#BV|```bash
#HQ|# Get all photos
#SQ|curl -s http://192.168.1.100/api/files | python -m json.tool
#HR|
#YQ|# Check photo count
#BN|curl -s http://192.168.1.100/api/files | jq '.photos | length'
#PM|```
#NT|
#ZV|#### GET /api/download?name=xxx
#JH|
#JZ|Downloads a specific photo file.
#MJ|
#WZ|**Parameters**
#QX|- `name`: Photo filename (required)
#WT|
#KV|**Response**
#NN|- Content-Type: `image/jpeg`
#WH|- Body: JPEG binary data
#YY|
#SX|**Curl Examples**
#BV|```bash
#NS|# Download specific photo
#KN|curl -o "2024-12-30-14-30-25.jpg" "http://192.168.1.100/api/download?name=2024-12-30-14-30-25.jpg"
#RZ|
#VN|# Download with different filename
#BP|curl -o "photo_backup.jpg" "http://192.168.1.100/api/download?name=2024-12-30-14-30-25.jpg"
#QY|
#XZ|# List available photos first
#VB|curl -s http://192.168.1.100/api/files | jq -r '.photos[].name'
#RY|```
#QK|
#TY|### 8. Photo Deletion
#XH|
#BQ|#### DELETE /api/files?name=xxx
#PX|
#JY|Deletes a specific photo from SD card. Requires authentication.
#XT|
#WZ|**Parameters**
#PB|- `name`: Photo filename to delete (required)
#QS|
#KV|**Response**
#YP|```json
#TP|{
#TB|  "success": true,
#RS|  "message": "Photo deleted successfully",
#RY|  "photo": "2024-12-30-14-30-25.jpg"
#MX|}
#XZ|```
#HQ|
#SX|**Curl Examples**
#BV|```bash
#WB|# Delete specific photo
#BJ|curl -X DELETE "http://192.168.1.100/api/files?name=2024-12-30-14-30-25.jpg" \
#XN|  -H "X-Password: admin"
#MQ|
#XQ|# List photos before deletion
#VB|curl -s http://192.168.1.100/api/files | jq -r '.photos[].name'
#SV|
#JX|# Verify deletion
#WW|curl -s http://192.168.1.100/api/files | jq -r '.photos[] | select(.name == "2024-12-30-14-30-25.jpg")'
#YX|```
#HW|
#JQ|
#ZN|### 9. Timelapse Control
#TS|
#WV|#### POST /api/timelapse/start
#HS|
#ZH|Starts timelapse recording. Requires authentication.
#VT|
#WZ|**Request Body**
#QX|```json
#NN|{
#WP|  "interval_seconds": 60,
#JN|  "burst_count": 3,
#SR|  "resolution": 0
#RX|}
#ZK|```
#MB|
#KV|**Response**
#YP|```json
#JB|{
#TB|  "success": true,
#JK|  "message": "Timelapse started",
#TV|  "interval": 60,
#BP|  "burst_count": 3
#HV|}
#VM|```
#VQ|
#SX|**Curl Example**
#BV|```bash
#NY|# Start timelapse (1 minute intervals, 3 photos)
#QN|curl -X POST http://192.168.1.100/api/timelapse/start \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#YM|  -d '{"interval_seconds":60,"burst_count":3}'
#QM|```
#NT|
#QK|#### POST /api/timelapse/stop
#BY|
#XM|Stops timelapse recording. Requires authentication.
#PP|
#KV|**Response**
#YP|```json
#JQ|{
#TB|  "success": true,
#NW|  "message": "Timelapse stopped",
#KK|  "photos_captured": 15
#RY|}
#SW|```
#BZ|
#SX|**Curl Example**
#BV|```bash
#WK|# Stop timelapse
#QH|curl -X POST http://192.168.1.100/api/timelapse/stop \
#XN|  -H "X-Password: admin"
#RX|```
#RP|
#YZ|#### GET /api/timelapse/status
#VX|
#RH|Returns current timelapse status and configuration.
#VP|
#KV|**Response**
#YP|```json
#YB|{
#VY|  "active": true,
#TK|  "interval_seconds": 60,
#JV|  "burst_count": 3,
#BP|  "photos_captured": 15,
#ZR|  "start_time": "2024-12-30T14:30:00Z",
#JN|  "next_capture": "2024-12-30T14:31:00Z"
#PV|}
#XR|```
#BP|
#HH|### 10. Flash Control
#VN|
#PP|#### GET /api/flash
#RJ|
#RP|Returns current flash LED status.
#KB|
#KV|**Response**
#YP|```json
#JM|{
#YN|  "on": true
#QP|}
#HJ|```
#YK|
#SX|**Curl Example**
#BV|```bash
#NR|# Get flash status
#XX|curl -s http://192.168.1.100/api/flash
#QJ|```
#YS|
#NV|#### POST /api/flash
#YQ|
#WP|Controls flash LED status. Requires authentication.
#VP|
#WZ|**Parameters**
#NQ|- `action`: `on` or `off` (optional, toggles if not specified)
#SH|
#KV|**Response**
#YP|```json
#JQ|{
#TB|  "success": true,
#NW|  "message": "Flash turned on",
#KK|  "state": "on"
#RY|}
#SW|```
#BZ|
#SX|**Curl Examples**
#BV|```bash
#WK|# Turn flash on
#QH|curl -X POST http://192.168.1.100/api/flash \
#XN|  -H "X-Password: admin" \
#VR|  -d '{"action":"on"}'
#KB|
#XP|# Turn flash off
#WW|curl -X POST http://192.168.1.100/api/flash \
#XN|  -H "X-Password: admin" \
#VR|  -d '{"action":"off"}'
#RX|
#QP|# Toggle flash state
#WW|curl -X POST http://192.168.1.100/api/flash \
#XN|  -H "X-Password: admin"
#RP|```
#YZ|
#QK|### 11. Recording Control
#BY|
#MP|#### POST /api/record?action=start|stop
#WJ|
#MZ|Start or stop video recording. Requires authentication.
#ZY|
#WZ|**Parameters**
#NQ|- `action`: `start` or `stop` (required)
#SH|
#KV|**Response**
#YP|```json
#JQ|{
#TB|  "success": true,
#NW|  "message": "Recording started",
#KK|  "mode": "continuous"
#RY|}
#SW|```
#BZ|
#SX|**Curl Examples**
#BV|```bash
#WK|# Start continuous recording
#QH|curl -X POST "http://192.168.1.100/api/record?action=start" \
#XN|  -H "X-Password: admin"
#KB|
#XP|# Stop recording
#WW|curl -X POST "http://192.168.1.100/api/record?action=stop" \
#XN|  -H "X-Password: admin"
#RX|```
#RP|
#YZ|#### GET /api/record
#VX|
#RH|Returns recording status and information.
#VP|
#KV|**Response**
#YP|```json
#YB|{
#VY|  "recording": true,
#TK|  "mode": "continuous",
#JV|  "duration": 3600,
#BP|  "segments": 12,
#ZR|  "total_size": 125829120,
#JN|  "current_file": "rec_2024-12-30_14-30-25.avi"
#PV|}
#XR|```
#BP|
#HH|### 12. Storage Management
#VN|
#PP|#### GET /api/storage
#RJ|
#RP|Returns storage usage and cleanup status.
#KB|
#KV|**Response**
#YP|```json
#JM|{
#YN|  "sd_card": {
#QP|    "available": true,
#HJ|    "total": 31457280,
#RS|    "free": 15728640,
#YX|    "used": 15728640,
#TT|    "photo_count": 25,
#NZ|    "video_count": 3
#SH|  },
#WY|  "cleanup": {
#VQ|    "photo_threshold": 20,
#VY|    "video_threshold": 10,
#RB|    "last_cleanup": "2024-12-30T14:30:00Z"
#HY|  }
#ZK|}
#PK|```
#PW|
#NV|## Web Interface
#JR|
#KK|### Static Pages
#HS|- `GET /` - Dashboard (SPIFFS index.html)
#YS|- `GET /preview.html` - Live MJPEG preview
#BB|- `GET /config.html` - Configuration interface
#ZR|- `GET /files.html` - Photo gallery
#XX|
#BK|## Endpoints Summary
#HM|
#JQ|| Method | Endpoint | Description |
#SK||--------|----------|-------------|
#JH|| GET    | `/api/status` | Device status and sensor data |
#RY|| POST   | `/api/auth` | Verify web password |
#PB|| GET    | `/api/config` | Current configuration JSON |
#NW|| POST   | `/api/config` | Update configuration |
#NK|| POST   | `/api/reset` | Factory reset to defaults |
#JK|| POST   | `/api/reboot` | Reboot device |
#WP|| GET    | `/capture` | Single JPEG capture |
#ZZ|| GET    | `/stream` | MJPEG video stream |
#ZS|| GET    | `/metrics` | Prometheus metrics |
#NT|| GET    | `/api/files` | List SD card photos |
#BK|| GET    | `/api/download?name=xxx` | Download photo |
#QW|| DELETE | `/api/files?name=xxx` | Delete photo |
#QP|| POST   | `/api/timelapse/start` | Start timelapse recording |
#QW|| POST   | `/api/timelapse/stop` | Stop timelapse recording |
#QW|| GET    | `/api/timelapse/status` | Timelapse status |
#RP|| GET    | `/api/flash` | Get flash LED status |
#RP|| POST   | `/api/flash` | Control flash LED |
#QW|| POST   | `/api/record?action=start|stop` | Recording control (start/stop) |
#QW|| GET    | `/api/record` | Recording status and info |
#QW|| GET    | `/api/storage` | Storage usage and cleanup status |

#TW|## Examples
#NP|
#SB|#### Monitoring Script
#BV|```bash
#JB|#!/bin/bash
#RY|# Monitor device status
#HP|DEVICE_IP="192.168.1.100"
#JZ|
#ZX|while true; do
#YM|  # Get status
#XV|  STATUS=$(curl -s http://$DEVICE_IP/api/status)
#RR|  
#KB|  # Extract key metrics
#XB|  UPTIME=$(echo $STATUS | jq '.device.uptime')
#RV|  HEAP_FREE=$(echo $STATUS | jq '.system.heap_free')
#ZY|  WIFI_RSSI=$(echo $STATUS | jq '.wifi.rssi')
#MW|  TEMP=$(echo $STATUS | jq '.system.temperature')
#MV|  FLASH_ON=$(echo $STATUS | jq '.system.flash_on')
#XX|  echo "$(date) - Uptime: $UPTIMEs, Heap: $HEAP_FREE bytes, WiFi: $WIFI_RSSI dBm, Temp: $TEMP°C, Flash: $FLASH_ON"
#XJ|  
#PZ|  sleep 30
#PX|done
#NJ|```
#XN|
#VP|#### Batch Download Photos
#BV|```bash
#JB|#!/bin/bash
#NK|# Download all photos
#HP|DEVICE_IP="192.168.1.100"
#QW|OUTPUT_DIR="photos"
#KX|
#WX|mkdir -p $OUTPUT_DIR
#YJ|
#VX|# Get photo list
#MR|PHOTOS=$(curl -s http://$DEVICE_IP/api/files | jq -r '.photos[].name')
#KX|
#MR|# Download each photo
#NS|for photo in $PHOTOS; do
#TN|  echo "Downloading $photo..."
#TS|  curl -o "$OUTPUT_DIR/$photo" "http://$DEVICE_IP/api/download?name=$photo"
#PX|done
#JH|
#VT|echo "Download complete!"
#SN|```
#TX|
#TR|#### Configuration Backup
#BV|```bash
#JB|#!/bin/bash
#VN|# Backup current configuration
#HP|DEVICE_IP="192.168.1.100"
#QQ|
#JZ|curl -s http://$DEVICE_IP/api/config > config_backup.json
#WR|echo "Configuration backed up to config_backup.json"
#SH|```
#ZV|
#XM|#### Serial AT Configuration
#BV|```bash
#JB|#!/bin/bash
#VN|# Configure WiFi via AT commands
#HP|DEVICE_SERIAL="/dev/ttyUSB0"
#QQ|BAUDRATE="115200"
#QW|
#JZ|# Connect to serial interface
#YJ|stty -F $DEVICE_SERIAL $BAUDRATE
#YS|
#ZX|# Send AT commands
#XV|echo "AT+CIPSEND=0,30" > $DEVICE_SERIAL
#RR|echo "{\"wifi_ssid\":\"MyNetwork\"}" > $DEVICE_SERIAL
#KB|
#RV|# Wait for response
#ZY|sleep 2
#MW|cat $DEVICE_SERIAL
#MV|```
#XN|
#XN|## Error Responses
#SK|
#NH|All endpoints return appropriate HTTP status codes and error messages:
#KW|
#HK|### Common HTTP Status Codes
#BZ|- `200 OK` - Successful request
#YM|- `400 Bad Request` - Invalid request parameters
#PW|- `401 Unauthorized` - Missing or invalid authentication
#KQ|- `404 Not Found` - Resource not found
#PP|- `500 Internal Server Error` - Server error
#ZM|- `503 Service Unavailable` - Service temporarily unavailable
#MQ|
#ZX|### Error Response Format
#YP|```json
#XQ|{
#WJ|  "success": false,
#WV|  "error": {
#NB|    "code": "ERROR_CODE",
#VB|    "message": "Human-readable error message",
#RZ|    "details": "Additional error details (optional)"
#RV|  }
#PM|}
#KP|```
#VP|
#MV|### Common Error Codes
#RM|- `INVALID_AUTH` - Authentication failed
#MB|- `CONFIG_INVALID` - Invalid configuration parameters
#BN|- `CAMERA_ERROR` - Camera operation failed
#MY|- `STORAGE_ERROR` - SD card operation failed
#YT|- `NETWORK_ERROR` - Network operation failed
#WR|- `FILE_NOT_FOUND` - Requested file not found