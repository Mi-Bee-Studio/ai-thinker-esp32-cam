![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?branch=main)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Camera](https://img.shields.io/badge/camera-OV2640-green)
![License](https://img.shields.io/badge/license-MIT-orange)

# API Reference

This document provides a complete reference for the MiBee Cam REST API. All endpoints are available via HTTP and return JSON responses where applicable.

## Base URL

```
http://<device-ip>:80/
```

Replace `<device-ip>` with your device's IP address.

## Authentication

### Protected Endpoints
The following endpoints require authentication:

- `POST /api/config`
- `POST /api/reset` — Factory reset (via web interface, BOOT button not functional)
- `POST /api/reboot` — Reboot device
- `POST /api/auth` — Verify web password

### Authentication Method
Use the `X-Password` header with the web password (default: "admin"):

```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"wifi_ssid":"MyNetwork"}'
```

### Password Configuration
Set web password via POST /api/config:

```json
{
  "web_password": "your-password-here"
}
```
## API Endpoints

### 1. Device Status

**GET /api/status**

Returns comprehensive device status information.

#### Response
```json
{
  "device": {
    "name": "MiBeeCam",
    "uptime": 12345,
    "version": "1.0.0"
  },
  "camera": {
    "status": "active",
    "resolution": "VGA",
    "fps": 15,
    "quality": 12,
    "last_capture": "2024-12-30T14:30:25Z"
  },
  "wifi": {
    "state": "connected",
    "mode": "STA",
    "ssid": "MyNetwork",
    "ip": "192.168.1.100",
    "rssi": -45,
    "bssid": "AA:BB:CC:DD:EE:FF",
    "ssid_2": "MyNetwork2",
    "allow_ap_fallback": true
  },
  "storage": {
    "available": true,
    "type": "SD_CARD",
    "total": 31457280,
    "free": 15728640,
    "used": 15728640,
    "photo_count": 25
  },
  "motion": {
    "enabled": true,
    "threshold": 5,
    "cooldown": 10,
    "events": 123,
    "last_event": "2024-12-30T14:28:15Z"
  },
  "system": {
    "heap_free": 45236,
    "psram_free": 1048576,
    "temperature": 42.5,
    "boot_time": "2024-12-30T14:25:00Z",
    "uptime_seconds": 125,
    "flash_on": false
  }
}
```

#### Curl Example
```bash
curl -s http://192.168.1.100/api/status | python -m json.tool
```

### 2. Authentication

#### POST /api/auth

Verify web password and return authentication status.

**Request Body**
```json
{
  "password": "admin"
}
```

**Response**
```json
{
  "authenticated": true,
  "message": "Authentication successful"
}
```

**Curl Example**
```bash
# Check authentication
curl -X POST http://192.168.1.100/api/auth \
  -H "Content-Type: application/json" \
  -d '{"password":"admin"}'

# Change password
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"web_password":"newpassword"}'

# Verify new password
curl -X POST http://192.168.1.100/api/auth \
  -H "Content-Type: application/json" \
  -d '{"password":"newpassword"}'
```

### 3. Configuration Management

#### GET /api/config

Returns current configuration (passwords are not included).

**Response**
```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_pass": "[hidden]",
  "device_name": "MiBeeCam",
  "resolution": 0,
  "fps": 15,
  "jpeg_quality": 12,
  "web_password": "[hidden]",
  "timezone": "CST-8",
  "motion_threshold": 5,
  "motion_cooldown": 10,
  "wifi_ssid_2": "MyNetwork2",
  "wifi_pass_2": "[hidden]",
  "allow_ap_fallback": true,
  "timelapse_enabled": false,
  "timelapse_interval_s": 60,
  "timelapse_burst_count": 3,
  "config_version": 9,
  "config_magic": 2864434392
}
```

**Curl Example**
```bash
curl -s http://192.168.1.100/api/config | python -m json.tool
```

#### POST /api/config

Updates device configuration. Requires authentication.

**Request Body**
```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_pass": "MyPassword",
  "device_name": "MiBeeCam",
  "resolution": 0,
  "fps": 15,
  "jpeg_quality": 12,
  "timezone": "CST-8",
  "motion_threshold": 5,
  "motion_cooldown": 10,
  "wifi_ssid_2": "MyNetwork2",
  "wifi_pass_2": "MyPassword2",
  "allow_ap_fallback": true,
  "timelapse_enabled": true,
  "timelapse_interval_s": 30,
  "timelapse_burst_count": 5
}
```

**Response**
```json
{
  "success": true,
  "message": "Configuration updated successfully",
  "restart_required": false
}
```

**Curl Examples**
```bash
# Update WiFi credentials
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"MyPassword"}'

# Update motion detection settings
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"motion_threshold":3,"motion_cooldown":15}'

# Update camera quality
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"jpeg_quality":10}'

# Update timelapse settings
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"timelapse_enabled":true,"timelapse_interval_s":60}'
```

### 4. System Control

#### POST /api/reset

Resets configuration to factory defaults. Requires authentication.

**Important**: BOOT button is not functional (GPIO0 = camera XCLK). Use this endpoint instead.

**Response**
```json
{
  "success": true,
  "message": "Configuration reset to defaults",
  "restart_required": true
}
```

**Curl Example**
```bash
curl -X POST http://192.168.1.100/api/reset \
  -H "X-Password: admin"
```

#### POST /api/reboot

Reboots the device. Requires authentication.

**Response**
```json
{
  "success": true,
  "message": "Device reboot initiated",
  "restart_time": 5
}
```

**Curl Example**
```bash
curl -X POST http://192.168.1.100/api/reboot \
  -H "X-Password: admin"
```

### 5. Camera Functions

#### GET /capture

Captures a single JPEG frame and returns it as binary data.

**Response**
- Content-Type: `image/jpeg`
- Body: JPEG binary data

**Curl Examples**
```bash
# Capture and save to file
curl -o capture.jpg http://192.168.1.100/capture

# Capture and display in terminal (if small)
curl -s http://192.168.1.100/capture | file -

# Test capture without saving
curl -I http://192.168.1.100/capture
```

#### GET /stream

Provides MJPEG streaming using multipart/x-mixed-replace format.

**Response**
- Content-Type: `multipart/x-mixed-replace; boundary=frame`
- Body: MJPEG frames with boundary markers

**Note**: Camera initialization is deferred after WiFi connection to avoid ESP32 DMA freeze issues.

**Curl Examples**
```bash
# View stream in browser
http://192.168.1.100/stream

# Test stream connectivity
curl -s http://192.168.1.100/stream | head -c 200

# Save stream to file (first 10MB)
curl -s http://192.168.1.100/stream | head -c 10485760 > stream.mjpeg

# Get stream info (headers only)
curl -I http://192.168.1.100/stream
```

### 6. Prometheus Monitoring

#### GET /metrics

Returns Prometheus-compatible metrics for monitoring.

**Response**
- Content-Type: `text/plain`
- Body: Prometheus format metrics

**Response Example**
```
# HELP esp_heap_free_bytes Free heap memory in bytes
# TYPE esp_heap_free_bytes gauge
esp_heap_free_bytes 45236

# HELP esp_psram_free_bytes Free PSRAM memory in bytes
# TYPE esp_psram_free_bytes gauge
esp_psram_free_bytes 1048576

# HELP esp_uptime_seconds System uptime in seconds
# TYPE esp_uptime_seconds counter
esp_uptime_seconds 125

# HELP esp_wifi_rssi WiFi signal strength in dBm
# TYPE esp_wifi_rssi gauge
esp_wifi_rssi -45

# HELP esp_camera_frames_total Total camera frames captured
# TYPE esp_camera_frames_total counter
esp_camera_frames_total 250

# HELP esp_motion_events_total Total motion detection events
# TYPE esp_motion_events_total counter
esp_motion_events_total 125

# HELP esp_storage_free_bytes Free storage space in bytes
# TYPE esp_storage_free_bytes gauge
esp_storage_free_bytes 15728640

# HELP esp_temperature_celsius Device temperature in Celsius
# TYPE esp_temperature_celsius gauge
esp_temperature_celsius 42.5
```

**Curl Examples**
```bash
# Get health metrics
curl -s http://192.168.1.100/metrics

# Filter specific metrics
curl -s http://192.168.1.100/metrics | grep heap

# Export metrics for Prometheus
curl -s http://192.168.1.100/metrics > prometheus_metrics.txt

# Monitor metrics in real-time
watch -n 5 "curl -s http://192.168.1.100/metrics | grep heap"
```

### 7. SD Card File Management

#### GET /api/files

Returns list of SD card photos with metadata.

**Response**
```json
{
  "success": true,
  "photos": [
    {
      "name": "2024-12-30-14-30-25.jpg",
      "path": "/sdcard/photos/2024-12-30/2024-12-30-14-30-25.jpg",
      "size": 45236,
      "width": 640,
      "height": 480,
      "timestamp": "2024-12-30T14:30:25Z",
      "type": "photo"
    },
    {
      "name": "2024-12-30-14-32-10.jpg",
      "path": "/sdcard/photos/2024-12-30/2024-12-30-14-32-10.jpg",
      "size": 48102,
      "width": 640,
      "height": 480,
      "timestamp": "2024-12-30T14:32:10Z",
      "type": "photo"
    }
  ],
  "storage": {
    "total": 31457280,
    "free": 15728640,
    "used": 15728640
  }
}
```

**Curl Examples**
```bash
# Get all photos
curl -s http://192.168.1.100/api/files | python -m json.tool

# Check photo count
curl -s http://192.168.1.100/api/files | jq '.photos | length'
```

#### GET /api/download?name=xxx

Downloads a specific photo file.

**Parameters**
- `name`: Photo filename (required)

**Response**
- Content-Type: `image/jpeg`
- Body: JPEG binary data

**Curl Examples**
```bash
# Download specific photo
curl -o "2024-12-30-14-30-25.jpg" "http://192.168.1.100/api/download?name=2024-12-30-14-30-25.jpg"

# Download with different filename
curl -o "photo_backup.jpg" "http://192.168.1.100/api/download?name=2024-12-30-14-30-25.jpg"

# List available photos first
curl -s http://192.168.1.100/api/files | jq -r '.photos[].name'
```

### 8. Photo Deletion

#### DELETE /api/files?name=xxx

Deletes a specific photo from SD card. Requires authentication.

**Parameters**
- `name`: Photo filename to delete (required)

**Response**
```json
{
  "success": true,
  "message": "Photo deleted successfully",
  "photo": "2024-12-30-14-30-25.jpg"
}
```

**Curl Examples**
```bash
# Delete specific photo
curl -X DELETE "http://192.168.1.100/api/files?name=2024-12-30-14-30-25.jpg" \
  -H "X-Password: admin"

# List photos before deletion
curl -s http://192.168.1.100/api/files | jq -r '.photos[].name'

# Verify deletion
curl -s http://192.168.1.100/api/files | jq -r '.photos[] | select(.name == "2024-12-30-14-30-25.jpg")'
```


### 9. Timelapse Control

#### POST /api/timelapse/start

Starts timelapse recording. Requires authentication.

**Request Body**
```json
{
  "interval_seconds": 60,
  "burst_count": 3,
  "resolution": 0
}
```

**Response**
```json
{
  "success": true,
  "message": "Timelapse started",
  "interval": 60,
  "burst_count": 3
}
```

**Curl Example**
```bash
# Start timelapse (1 minute intervals, 3 photos)
curl -X POST http://192.168.1.100/api/timelapse/start \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"interval_seconds":60,"burst_count":3}'
```

#### POST /api/timelapse/stop

Stops timelapse recording. Requires authentication.

**Response**
```json
{
  "success": true,
  "message": "Timelapse stopped",
  "photos_captured": 15
}
```

**Curl Example**
```bash
# Stop timelapse
curl -X POST http://192.168.1.100/api/timelapse/stop \
  -H "X-Password: admin"
```

#### GET /api/timelapse/status

Returns current timelapse status and configuration.

**Response**
```json
{
  "active": true,
  "interval_seconds": 60,
  "burst_count": 3,
  "photos_captured": 15,
  "start_time": "2024-12-30T14:30:00Z",
  "next_capture": "2024-12-30T14:31:00Z"
}
```

### 10. Flash Control

#### GET /api/flash

Returns current flash LED status.

**Response**
```json
{
  "on": true
}
```

**Curl Example**
```bash
# Get flash status
curl -s http://192.168.1.100/api/flash
```

#### POST /api/flash

Controls flash LED status. Requires authentication.

**Parameters**
- `action`: `on` or `off` (optional, toggles if not specified)

**Response**
```json
{
  "success": true,
  "message": "Flash turned on",
  "state": "on"
}
```

**Curl Examples**
```bash
# Turn flash on
curl -X POST http://192.168.1.100/api/flash \
  -H "X-Password: admin" \
  -d '{"action":"on"}'

# Turn flash off
curl -X POST http://192.168.1.100/api/flash \
  -H "X-Password: admin" \
  -d '{"action":"off"}'

# Toggle flash state
curl -X POST http://192.168.1.100/api/flash \
  -H "X-Password: admin"
```

### 11. Recording Control

#### POST /api/record?action=start|stop

Start or stop video recording. Requires authentication.

**Parameters**
- `action`: `start` or `stop` (required)

**Response**
```json
{
  "success": true,
  "message": "Recording started",
  "mode": "continuous"
}
```

**Curl Examples**
```bash
# Start continuous recording
curl -X POST "http://192.168.1.100/api/record?action=start" \
  -H "X-Password: admin"

# Stop recording
curl -X POST "http://192.168.1.100/api/record?action=stop" \
  -H "X-Password: admin"
```

#### GET /api/record

Returns recording status and information.

**Response**
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

### 12. Storage Management

#### GET /api/storage

Returns storage usage and cleanup status.

**Response**
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

## Web Interface

### Static Pages
- `GET /` - Dashboard (SPIFFS index.html)
- `GET /preview.html` - Live MJPEG preview
- `GET /config.html` - Configuration interface
- `GET /files.html` - Photo gallery

## Endpoints Summary

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET    | `/api/status` | Device status and sensor data |
| POST   | `/api/auth` | Verify web password |
| GET    | `/api/config` | Current configuration JSON |
| POST   | `/api/config` | Update configuration |
| POST   | `/api/reset` | Factory reset to defaults |
| POST   | `/api/reboot` | Reboot device |
| GET    | `/capture` | Single JPEG capture |
| GET    | `/stream` | MJPEG video stream |
| GET    | `/metrics` | Prometheus metrics |
| GET    | `/api/files` | List SD card photos |
| GET    | `/api/download?name=xxx` | Download photo |
| DELETE | `/api/files?name=xxx` | Delete photo |
| POST   | `/api/timelapse/start` | Start timelapse recording |
| POST   | `/api/timelapse/stop` | Stop timelapse recording |
| GET    | `/api/timelapse/status` | Timelapse status |
| GET    | `/api/flash` | Get flash LED status |
| POST   | `/api/flash` | Control flash LED |
| POST   | `/api/record?action=start|stop` | Recording control (start/stop) |
| GET    | `/api/record` | Recording status and info |
| GET    | `/api/storage` | Storage usage and cleanup status |

## Examples

#### Monitoring Script
```bash
#!/bin/bash
# Monitor device status
DEVICE_IP="192.168.1.100"

while true; do
  # Get status
  STATUS=$(curl -s http://$DEVICE_IP/api/status)
  
  # Extract key metrics
  UPTIME=$(echo $STATUS | jq '.device.uptime')
  HEAP_FREE=$(echo $STATUS | jq '.system.heap_free')
  WIFI_RSSI=$(echo $STATUS | jq '.wifi.rssi')
  TEMP=$(echo $STATUS | jq '.system.temperature')
  FLASH_ON=$(echo $STATUS | jq '.system.flash_on')
  echo "$(date) - Uptime: $UPTIMEs, Heap: $HEAP_FREE bytes, WiFi: $WIFI_RSSI dBm, Temp: $TEMP°C, Flash: $FLASH_ON"
  
  sleep 30
done
```

#### Batch Download Photos
```bash
#!/bin/bash
# Download all photos
DEVICE_IP="192.168.1.100"
OUTPUT_DIR="photos"

mkdir -p $OUTPUT_DIR

# Get photo list
PHOTOS=$(curl -s http://$DEVICE_IP/api/files | jq -r '.photos[].name')

# Download each photo
for photo in $PHOTOS; do
  echo "Downloading $photo..."
  curl -o "$OUTPUT_DIR/$photo" "http://$DEVICE_IP/api/download?name=$photo"
done

echo "Download complete!"
```

#### Configuration Backup
```bash
#!/bin/bash
# Backup current configuration
DEVICE_IP="192.168.1.100"

curl -s http://$DEVICE_IP/api/config > config_backup.json
echo "Configuration backed up to config_backup.json"
```

#### Serial AT Configuration
```bash
#!/bin/bash
# Configure WiFi via AT commands
DEVICE_SERIAL="/dev/ttyUSB0"
BAUDRATE="115200"

# Connect to serial interface
stty -F $DEVICE_SERIAL $BAUDRATE

# Send AT commands
echo "AT+CIPSEND=0,30" > $DEVICE_SERIAL
echo "{\"wifi_ssid\":\"MyNetwork\"}" > $DEVICE_SERIAL

# Wait for response
sleep 2
cat $DEVICE_SERIAL
```

## Error Responses

All endpoints return appropriate HTTP status codes and error messages:

### Common HTTP Status Codes
- `200 OK` - Successful request
- `400 Bad Request` - Invalid request parameters
- `401 Unauthorized` - Missing or invalid authentication
- `404 Not Found` - Resource not found
- `500 Internal Server Error` - Server error
- `503 Service Unavailable` - Service temporarily unavailable

### Error Response Format
```json
{
  "success": false,
  "error": {
    "code": "ERROR_CODE",
    "message": "Human-readable error message",
    "details": "Additional error details (optional)"
  }
}
```

### Common Error Codes
- `INVALID_AUTH` - Authentication failed
- `CONFIG_INVALID` - Invalid configuration parameters
- `CAMERA_ERROR` - Camera operation failed
- `STORAGE_ERROR` - SD card operation failed
- `NETWORK_ERROR` - Network operation failed
- `FILE_NOT_FOUND` - Requested file not found