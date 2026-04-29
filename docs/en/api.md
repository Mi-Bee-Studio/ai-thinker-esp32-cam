# API Reference

This document provides a complete reference for the AI_Thinker ESP32-CAM REST API. All endpoints are available via HTTP and return JSON responses where applicable.

## Base URL

```
http://<device-ip>:80/
```

Replace `<device-ip>` with your device's IP address.

## Authentication

### Protected Endpoints
The following endpoints require authentication:

- `POST /api/config`
- `POST /api/reset`
- `POST /api/reboot`

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
    "name": "ai-thinker-cam",
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
    "bssid": "AA:BB:CC:DD:EE:FF"
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
    "uptime_seconds": 125
  }
}
```

#### Curl Example
```bash
curl -s http://192.168.1.100/api/status | python -m json.tool
```

### 2. Configuration Management

#### GET /api/config

Returns current configuration (passwords are not included).

**Response**
```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_pass": "[hidden]",
  "device_name": "ai-thinker-cam",
  "resolution": 0,
  "fps": 15,
  "jpeg_quality": 12,
  "web_password": "[hidden]",
  "timezone": "CST-8",
  "motion_threshold": 5,
  "motion_cooldown": 10,
  "nas_protocol": 0,
  "nas_host": "192.168.1.50",
  "nas_port": 80,
  "nas_user": "",
  "nas_pass": "[hidden]",
  "nas_path": "/photos",
  "config_version": 1,
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
  "device_name": "ai-thinker-cam",
  "resolution": 0,
  "fps": 15,
  "jpeg_quality": 12,
  "timezone": "CST-8",
  "motion_threshold": 5,
  "motion_cooldown": 10,
  "nas_protocol": 0,
  "nas_host": "192.168.1.50",
  "nas_port": 80,
  "nas_user": "",
  "nas_pass": "",
  "nas_path": "/photos"
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

# Update NAS settings
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"nas_host":"nas.example.com","nas_port":21,"nas_user":"ftpuser","nas_pass":"ftppass"}'
```

### 3. System Control

#### POST /api/reset

Resets configuration to factory defaults. Requires authentication.

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

### 4. Camera Functions

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

### 5. Monitoring and Metrics

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
# Get metrics
curl -s http://192.168.1.100/metrics

# Filter specific metrics
curl -s http://192.168.1.100/metrics | grep heap

# Export metrics for Prometheus
curl -s http://192.168.1.100/metrics > prometheus_metrics.txt

# Monitor metrics in real-time
watch -n 5 "curl -s http://192.168.1.100/metrics | grep heap"
```

### 6. File Management

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
  "pagination": {
    "total": 25,
    "page": 1,
    "per_page": 20
  },
  "storage": {
    "total": 31457280,
    "free": 15728640,
    "used": 15728640
  }
}
```

**Query Parameters**
- `page`: Page number (default: 1)
- `per_page`: Items per page (default: 20, max: 50)

**Curl Examples**
```bash
# Get all photos
curl -s http://192.168.1.100/api/files | python -m json.tool

# Get first page of photos
curl -s "http://192.168.1.100/api/files?page=1&per_page=10" | python -m json.tool

# Filter photos by date
curl -s "http://192.168.1.100/api/files?date=2024-12-30" | python -m json.tool

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

### 7. Upload Functions

#### POST /api/upload

Triggers manual NAS upload of a specific photo. Requires authentication.

**Request Body**
```json
{
  "photo_name": "2024-12-30-14-30-25.jpg"
}
```

**Parameters**
- `photo_name`: Name of the photo to upload (optional, defaults to newest)

**Response**
```json
{
  "success": true,
  "message": "Upload started",
  "photo": "2024-12-30-14-30-25.jpg",
  "protocol": "HTTP",
  "server": "192.168.1.50",
  "status": "queued",
  "estimated_time": 5
}
```

**Curl Examples**
```bash
# Upload specific photo
curl -X POST http://192.168.1.100/api/upload \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"photo_name":"2024-12-30-14-30-25.jpg"}'

# Upload most recent photo
curl -X POST http://192.168.1.100/api/upload \
  -H "Content-Type: application/json" \
  -H "X-Password: admin"

# Check upload status (monitor response)
curl -X POST http://192.168.1.100/api/upload \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" | python -m json.tool
```

## Web Interface Endpoints

### Static Pages
- `GET /` - Dashboard (SPIFFS index.html)
- `GET /preview.html` - Live MJPEG preview
- `GET /config.html` - Configuration interface
- `GET /files.html` - Photo gallery

### Examples

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
  
  echo "$(date) - Uptime: $UPTIMEs, Heap: $HEAP_FREE bytes, WiFi: $WIFI_RSSI dBm, Temp: $TEMP°C"
  
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

## Rate Limiting

API endpoints are subject to rate limiting:
- Configuration endpoints: 5 requests per minute
- File downloads: 10 requests per minute
- Camera endpoints: 30 requests per minute
- Status/metrics: 100 requests per minute

Rate limit headers are included in responses:
- `X-RateLimit-Limit`: Request limit
- `X-RateLimit-Remaining`: Remaining requests
- `X-RateLimit-Reset`: Reset time (Unix timestamp)

## CORS Support

All API endpoints support CORS headers:
```http
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, X-Password
```

This allows cross-origin requests from web browsers.