<!-- OMO_INTERNAL_INITIATOR -->
<p align="center">
  <img src="https://img.shields.io/badge/ESP32--CAM-Firmware-blue?style=for-the-badge&logo=esp32&logoColor=white" alt="ESP32-CAM Firmware">
  <img src="https://img.shields.io/badge/ESP--IDF-v6.0.1-green?style=for-the-badge&logo=espressif&logoColor=white" alt="ESP-IDF v6.0.1">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge" alt="MIT License">
  <img src="https://img.shields.io/badge/Platform-ESP32-orange?style=for-the-badge&logo=arduino&logoColor=white" alt="ESP32 Platform">
  <img src="https://img.shields.io/badge/Language-C-red?style=for-the-badge&logo=c&logoColor=white" alt="C Language">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Production--Ready-brightgreen?style=for-the-badge" alt="Production Ready">
  <img src="https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?style=for-the-badge&logo=github&logoColor=white" alt="Build Status">
  <img src="https://img.shields.io/github/release/Mi-Bee-Studio/mibee-cam?style=for-the-badge&logo=github&logoColor=white" alt="Latest Release">
</p>

<h1 align="center">MiBee Cam Firmware</h1>

<p align="center">
  <strong>Professional-grade camera firmware for MiBee Cam boards with OV2640 streaming, motion detection, and REST API</strong>
</p>

<p align="center">
  🌐 <a href="README.zh.md">中文文档</a> • 📚 <a href="docs/en/">English Documentation</a> • 🔧 <a href="docs/en/getting-started.md">Getting Started</a>
</p>

---

#XS|## ✨ Features
#ZR|
#SX|- **📸 High-Quality Camera Capture** — OV2640 camera with VGA/SVGA/XGA/UXGA resolutions and configurable JPEG quality
#NX|- **📶 Smart WiFi Management** — STA mode with auto-reconnect and AP fallback for seamless deployment  
#JZ|- **🌐 Web Dashboard** — Modern responsive interface with live MJPEG preview and configuration management
#KZ|- **🎥 Real-Time MJPEG Streaming** — Smooth video at up to 15 FPS via `/stream` endpoint
#SR|- **🔍 Intelligent Motion Detection** — Frame-difference algorithm with configurable sensitivity and cooldown
#YN|- **💡 Automatic Flash Control** — Sensor-based brightness detection with intelligent LED activation
#QX|- **💾 SD Card Storage** — Automatic JPEG photo saving to TF card with management APIs
#JN|- **📡 REST API** — Comprehensive API for status, configuration, file operations, and camera control
#TQ|- **📊 Prometheus Monitoring** — Compatible `/metrics` endpoint for system health tracking
#QT|- **🎯 Status LED** — GPIO33 LED provides real-time system status feedback
#TP|- **⏰ NTP Time Sync** — Automatic time synchronization with configurable timezone support
#BV|- **🎬 AVI Video Recording** — Continuous, timelapse, and dynamic recording modes with segment management
#MR|- **📡 NAS Upload via WebDAV** — Automatic cloud backup with retry logic and configurable destinations
#ZB|- **🔍 ONVIF WS-Discovery** — Auto-discovery for NVR integration with Synology/Milestone compatibility
#YP|- **💬 WebSocket Real-Time Push** — Live status updates for motion, recording, and system events
#NB|- **📢 Webhook HTTP POST Alerts** — Customizable notifications for motion, recording, and system events
#KW|- **🔄 Circular Storage Cleanup** — Auto-management when thresholds reached, configurable percentages
#ZJ|- **🚀 Setup Wizard** — First-time WiFi configuration interface for easy deployment
---

## 🏗️ Hardware Requirements

| Component | Specification |
|-----------|----------------|
| **Board** | MiBee Cam (ESP32-D0WD-V3) |
| **Camera** | OV2640 camera module (4-bit parallel interface) |
| **Storage** | TF card (optional, FAT formatted for photo storage) |
| **Flash Memory** | 4MB (3.5MB available for firmware) |
| **PSRAM** | 8MB (4MB usable for frame buffers) |

---

## 🔌 Pin Mapping

| Pin | GPIO | Function | Notes |
|-----|------|----------|-------|
| XCLK | 0 | Master Clock | **Camera only** - cannot be used for BOOT button |
| SIOD | 26 | I2C Data (SDA) | Camera control interface |
| SIOC | 27 | I2C Clock (SCL) | Camera control interface |
| D0-D7 | 5,18,19,21,36,39,34,35 | Parallel Data | Camera sensor input (D4-D7 input only) |
| VSYNC | 25 | Vertical Sync | Camera timing |
| HREF | 23 | Horizontal Reference | Camera timing |
| PCLK | 22 | Pixel Clock | Camera timing |
| PWDN | 32 | Power Down | Camera enable |
| SD_CS | 13 | SD Card Chip Select | Storage interface |
| SD_CLK | 14 | SD Card Clock | **Shared with camera XCLK** - critical timing |
| SD_MOSI | 15 | SD Card MOSI | Storage interface |
| SD_MISO | 2 | SD Card MISO | Storage interface |
| LED_STATUS | 33 | Status LED | Active LOW (system status) |
| LED_FLASH | 4 | Flash LED | PWM brightness control |

---

## 🚀 Quick Start

### Prerequisites

- ESP-IDF v6.0.1 installed
- Python 3.8+ with pip
- MiBee Cam board with OV2640 camera
- Serial USB-to-TTL adapter (for flashing)

### Setup Instructions

```bash
# Clone the repository
git clone https://github.com/Mi-Bee-Studio/mibee-cam.git
cd mibee-cam

# Configure ESP-IDF environment
export IDF_PATH=~/.espressif/v6.0.1/esp-idf
. $IDF_PATH/export.sh

# Build the firmware
idf.py set-target esp32
idf.py build

# Flash to your device
idf.py -p /dev/ttyUSB0 flash monitor
```

### First-time Configuration

1. **Connect to AP Mode**: On first boot, the device creates WiFi network **MiBeeCam** (password: `12345678`)
2. **Access Web Interface**: Open `http://192.168.4.1` in your browser
3. **Configure WiFi**: Enter your WiFi SSID and password in the settings page
4. **Apply Changes**: Save configuration — device reboots and connects in STA mode

### Factory Reset

Send `POST` request to reset configuration:
```bash
curl -X POST http://DEVICE_IP/api/reset
```

> **Note**: GPIO0 is reserved for camera XCLK timing and cannot be used for BOOT button reset.

---

#HQ|## 🌐 REST API
#KZ|
#JY|| Method | Endpoint | Description |
#SK||--------|----------|-------------|
#JH|| `GET` | `/api/status` | Device status and sensor data |
#RY|| `GET` | `/api/config` | Current configuration JSON |
#PB|| `POST` | `/api/config` | Update configuration |
#NW|| `POST` | `/api/reset` | Factory reset to defaults |
#NK|| `POST` | `/api/reboot` | Reboot device |
#JK|| `GET` | `/capture` | Single JPEG capture |
#WP|| `GET` | `/stream` | MJPEG video stream |
#ZZ|| `GET` | `/metrics` | Prometheus metrics |
#ZS|| `GET` | `/api/files` | List SD card photos |
#NT|| `GET` | `/api/download?name=xxx.jpg` | Download photo |
#BK|| `POST` | `/api/upload` | Trigger manual photo upload |
#QW|| `POST` | `/api/record?action=start|stop` | Recording control (start/stop) |
#QP|| `GET` | `/api/record` | Recording status and info |
#QW|| `GET` | `/api/storage` | Storage usage and cleanup status |
#RP|| `POST` | `/api/nas/test` | Test NAS connection |
#RP|| `GET` | `/api/nas` | NAS upload status and queue |
| **cJSON** | Embedded | JSON parsing for API |
| **LwIP** | Latest | TCP/IP networking stack |
| **FreeRTOS** | Latest | Real-time operating system |
| **Prometheus** | Latest | Metrics collection endpoint |

---

#KX|## 📁 Project Structure
#HJ|
#QH|```
#HR|mibee-cam/
#VS|├── main/
#PB|│   ├── main.c              # System entry point (19-step boot sequence)
#JV|│   ├── camera_driver.c/h   # OV2640 camera driver with DMA optimization
#BN|│   ├── wifi_manager.c/h    # WiFi AP/STA management with callbacks
#PT|│   ├── config_manager.c/h  # NVS configuration persistence (v8)
#BN|│   ├── web_server.c/h      # HTTP server + REST API + SPIFFS
#SS|│   ├── mjpeg_streamer.c/h  # MJPEG real-time streaming (async)
#TP|│   ├── storage_manager.c/h # SD card storage with hot-plug detection
#TZ|│   ├── motion_detect.c/h   # Frame-difference motion detection
#NP|│   ├── status_led.c/h      # Status LED driver
#TY|│   ├── time_sync.c/h       # NTP time synchronization
#RX|│   ├── health_monitor.c/h  # Prometheus metrics collection
#RV|│   ├── video_recorder.c/h  # AVI video recording (3 modes)
#TQ|│   ├── frame_broadcaster.c/h # Frame buffer distribution
#NB|│   ├── nas_uploader.c/h    # NAS upload via WebDAV/HTTP
#BN|│   ├── webdav_client.c/h   # WebDAV client for cloud sync
#BN|│   ├── onvif_discovery.c/h # ONVIF WS-Discovery service
#BN|│   ├── onvif_service.c/h   # ONVIF SOAP service handlers
#BN|│   ├── webhook.c/h         # HTTP POST event notifications
#BN|│   ├── ws_server.c/h       # WebSocket real-time push
#BN|│   ├── sha256.c/h          # SHA256 for file verification
#BN|│   └── web_ui/            # Responsive web interface assets
#SP|├── docs/en/               # English documentation
#ST|├── docs/zh/               # Chinese documentation
#QM|├── partitions.csv        # Custom partition layout (3.5MB + 448KB SPIFFS)
#RN|├── sdkconfig.defaults    # SDK configuration defaults
#PM|├── main/idf_component.yml # External component dependencies
#BB|└── .github/workflows/    # CI/CD pipeline
#MY|```

#RX|## 🔄 Boot Sequence
#NT|
#SJ|The firmware follows a carefully orchestrated 19-step initialization process:
#HN|
#VS|1. **NVS Flash Initialization** — Configuration storage system
#ZJ|2. **Configuration Load** — Retrieve persisted settings (version 8)
#WZ|3. **Status LED Setup** — Initialize GPIO33 system indicator
#MQ|4. **SPIFFS Mount** — Web UI filesystem preparation
#HT|5. **WiFi Subsystem** — Network interface initialization
#WT|6. **WiFi State Registration** — Callback system for connection events
#HV|7. **Health Monitor** — System metrics collection daemon
#YN|8. **WiFi Mode Selection** — STA if configured, AP otherwise
#QB|9. **MJPEG Streamer** — Real-time video server initialization
#RB|10. **Web Server Start** — HTTP server on port 80 with REST API
#KP|11. **Time Sync Init** — NTP client (STA mode only)
#PS|12. **Motion Detection** — Frame monitoring service (STA mode only)
#NP|13. **Video Recorder Init** — AVI recording system startup
#RV|14. **WebSocket Server** — Real-time event push service
#NP|15. **ONVIF Discovery** — NVR auto-discovery service
#NP|16. **ONVIF Service** — SOAP service handlers
#NP|17. **WebDAV Client** — Cloud upload system initialization
#NP|18. **Webhook Service** — HTTP POST notification system
#NP|19. **NAS Uploader** — Upload queue and retry management
---

## ⚙️ Configuration

All settings are stored in NVS with version 6 schema and accessible via web UI (`/config.html`) or REST API (`GET/POST /api/config`).

### Network Configuration
```json
{
  "ssid": "your_wifi_network",
  "wifi_pass": "your_password"
}
```

### Device Settings
```json
{
  "device_name": "MiBeeCam",
  "web_password": "optional_ui_password"
}
```

### Camera Configuration
```json
{
  "resolution": 6,        // 5=VGA, 6=SVGA, 7=XGA, 8=UXGA
  "quality": 10          // JPEG quality 1-63 (lower=better)
}
```

### Motion Detection
```json
{
  "motion_enabled": true,
  "motion_threshold": 30,    // 1-100 (higher=less sensitive)
  "motion_cooldown": 5       // 1-60 seconds
}
```

### Flash Control
```json
{
  "flash_threshold": 40      // 0-100 (brightness% for trigger)
}
```
- **0** = Flash disabled
- **34-40** = Recommended for indoor use
- **100** = Flash always triggers

### System Configuration
```json
{
  "timezone": "CST-8"     // Timezone string (e.g., "EST5EDT")
}
```

---

## 💡 Usage Examples

### API Interaction

#BV|```bash
#MY|# Read current configuration
#WP|curl http://DEVICE_IP/api/config
#NB|
#NK|# Adjust flash sensitivity
#QQ|curl -X POST http://DEVICE_IP/api config \
#TM|  -H 'Content-Type: application/json' \
#XP|  -d '{"flash_threshold":60}'
#RS|
#QW|# Check system status
#VV|curl http://DEVICE_IP/api/status | jq '.data'
#QB|
#BM|# Monitor brightness levels
#HJ|curl http://DEVICE_IP/metrics | grep brightness
#XR|
#PW|# Capture single photo
#XH|curl http://DEVICE_IP/capture -o photo.jpg
#BH|
#QW|# Start video recording
#RM|curl -X POST http://DEVICE_IP/api/record?action=start
#QW|# Stop video recording
#RM|curl -X POST http://DEVICE_IP/api/record?action=stop
#QW|# Check recording status
#RM|curl http://DEVICE_IP/api/record
#QW|# Test NAS connection
#RM|curl -X POST http://DEVICE_IP/api/nas/test
#QW|# Get NAS upload status
#RM|curl http://DEVICE_IP/api/nas
#MZ|

### Prometheus Metrics

The `/metrics` endpoint provides system health data:
```bash
curl http://DEVICE_IP/metrics
```

Key metrics available:
- System uptime and memory usage
- Camera frame rates and errors
- WiFi signal strength and connection status
- Motion detection events and triggers
- Brightness measurements and flash activations

---

## 🧪 Testing

**Note**: This project currently relies on runtime error-checking for validation. There are no unit tests or integration tests implemented. All functionality is tested through manual deployment and API validation.

---

## 🔒 Important Notes

- **GPIO0 Critical**: GPIO0 serves as camera XCLK and cannot be repurposed for BOOT button
- **GPIO14 Sharing**: SD card CLK and camera XCLK share GPIO14 - initialization order is critical
- **DMA Freeze**: Camera initialization deferred after WiFi STA connection to prevent ESP32 DMA issues
- **NVS Migration**: Configuration schema includes migration support for field additions
- **PSRAM Required**: 8MB PSRAM is mandatory for camera frame buffer allocation

---

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Test thoroughly on MiBee Cam hardware
4. Submit a pull request with detailed testing notes

---

<p align="center">
  <strong>Built with ❤️ for the ESP32 community</strong><br>
  <em>MiBee Cam Professional Firmware</em>
</p>