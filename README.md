<!-- OMO_INTERNAL_INITIATOR -->
<p align="center">
  <img src="https://img.shields.io/badge/ESP32-CAM-Firmware-blue?style=for-the-badge&logo=esp32&logoColor=white" alt="ESP32-CAM Firmware">
  <img src="https://img.shields.io/badge/ESP-IDF-v6.0.1-green?style=for-the-badge&logo=espressif&logoColor=white" alt="ESP-IDF v6.0.1">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge" alt="MIT License">
  <img src="https://img.shields.io/badge/Platform-ESP32-orange?style=for-the-badge&logo=arduino&logoColor=white" alt="ESP32 Platform">
  <img src="https://img.shields.io/badge/Language-C-red?style=for-the-badge&logo=c&logoColor=white" alt="C Language">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Production--Ready-brightgreen?style=for-the-badge" alt="Production Ready">
  <img src="https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?style=for-the-badge&logo=github&logoColor=white" alt="Build Status">
  <img src="https://img.shields.io/github/release/Mi-Bee-Studio/ai-thinker-esp32-cam?style=for-the-badge&logo=github&logoColor=white" alt="Latest Release">
</p>

<h1 align="center">AI-Thinker ESP32-CAM Firmware</h1>

<p align="center">
  <strong>Professional-grade camera firmware for AI-Thinker ESP32-CAM boards with OV2640 streaming, motion detection, and REST API</strong>
</p>

<p align="center">
  🌐 <a href="README.zh.md">中文文档</a> • 📚 <a href="docs/en/">English Documentation</a> • 🔧 <a href="docs/en/getting-started.md">Getting Started</a>
</p>

---

## ✨ Features

- **📸 High-Quality Camera Capture** — OV2640 camera with VGA/SVGA/XGA/UXGA resolutions and configurable JPEG quality
- **📶 Smart WiFi Management** — STA mode with auto-reconnect and AP fallback for seamless deployment  
- **🌐 Web Dashboard** — Modern responsive interface with live MJPEG preview and configuration management
- **🎥 Real-Time MJPEG Streaming** — Smooth video at up to 15 FPS via `/stream` endpoint
- **🔍 Intelligent Motion Detection** — Frame-difference algorithm with configurable sensitivity and cooldown
- **💡 Automatic Flash Control** — Sensor-based brightness detection with intelligent LED activation
- **💾 SD Card Storage** — Automatic JPEG photo saving to TF card with management APIs
- **📡 REST API** — Comprehensive API for status, configuration, file operations, and camera control
- **📊 Prometheus Monitoring** — Compatible `/metrics` endpoint for system health tracking
- **🎯 Status LED** — GPIO33 LED provides real-time system status feedback
- **⏰ NTP Time Sync** — Automatic time synchronization with configurable timezone support

---

## 🏗️ Hardware Requirements

| Component | Specification |
|-----------|----------------|
| **Board** | AI-Thinker ESP32-CAM (ESP32-D0WD-V3) |
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
- AI-Thinker ESP32-CAM board with OV2640 camera
- Serial USB-to-TTL adapter (for flashing)

### Setup Instructions

```bash
# Clone the repository
git clone https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam.git
cd ai-thinker-esp32-cam

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

1. **Connect to AP Mode**: On first boot, the device creates WiFi network **ai-thinker-cam** (password: `12345678`)
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

## 🌐 REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/status` | Device status and sensor data |
| `GET` | `/api/config` | Current configuration JSON |
| `POST` | `/api/config` | Update configuration |
| `POST` | `/api/reset` | Factory reset to defaults |
| `POST` | `/api/reboot` | Reboot device |
| `GET` | `/capture` | Single JPEG capture |
| `GET` | `/stream` | MJPEG video stream |
| `GET` | `/metrics` | Prometheus metrics |
| `GET` | `/api/files` | List SD card photos |
| `GET` | `/api/download?name=xxx.jpg` | Download photo |
| `POST` | `/api/upload` | Trigger manual photo upload |

---

## 🛠️ Tech Stack

| Component | Version | Purpose |
|-----------|---------|---------|
| **ESP-IDF** | v6.0.1 | Embedded development framework |
| **OV2640 Driver** | 2.1.6 | Camera sensor interface |
| **SPIFFS** | Latest | Web filesystem for UI assets |
| **FATFS** | Latest | SD card file system |
| **cJSON** | Embedded | JSON parsing for API |
| **LwIP** | Latest | TCP/IP networking stack |
| **FreeRTOS** | Latest | Real-time operating system |
| **Prometheus** | Latest | Metrics collection endpoint |

---

## 📁 Project Structure

```
ai-thinker-esp32-cam/
├── main/
│   ├── main.c              # System entry point (12-step boot sequence)
│   ├── camera_driver.c/h   # OV2640 camera driver with DMA optimization
│   ├── wifi_manager.c/h    # WiFi AP/STA management with callbacks
│   ├── config_manager.c/h  # NVS configuration persistence (v6)
│   ├── web_server.c/h      # HTTP server + REST API + SPIFFS
│   ├── mjpeg_streamer.c/h  # MJPEG real-time streaming (async)
│   ├── storage_manager.c/h # SD card storage with hot-plug detection
│   ├── motion_detect.c/h   # Frame-difference motion detection
│   ├── status_led.c/h      # Status LED driver
│   ├── time_sync.c/h       # NTP time synchronization
│   ├── health_monitor.c/h  # Prometheus metrics collection
│   └── web_ui/            # Responsive web interface assets
├── docs/en/               # English documentation
├── docs/zh/               # Chinese documentation
├── partitions.csv        # Custom partition layout (3.5MB + 448KB SPIFFS)
├── sdkconfig.defaults    # SDK configuration defaults
├── main/idf_component.yml # External component dependencies
└── .github/workflows/    # CI/CD pipeline
```

---

## 🔄 Boot Sequence

The firmware follows a carefully orchestrated 12-step initialization process:

1. **NVS Flash Initialization** — Configuration storage system
2. **Configuration Load** — Retrieve persisted settings (version 6)
3. **Status LED Setup** — Initialize GPIO33 system indicator
4. **SPIFFS Mount** — Web UI filesystem preparation
5. **WiFi Subsystem** — Network interface initialization
6. **WiFi State Registration** — Callback system for connection events
7. **Health Monitor** — System metrics collection daemon
8. **WiFi Mode Selection** — STA if configured, AP otherwise
9. **MJPEG Streamer** — Real-time video server initialization
10. **Web Server Start** — HTTP server on port 80 with REST API
11. **Time Sync Init** — NTP client (STA mode only)
12. **Motion Detection** — Frame monitoring service (STA mode only)

> **Important**: Camera initialization occurs after WiFi STA connection to prevent ESP32 DMA freeze issues. SD card initialization handles GPIO14 shared clock between camera and storage.

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
  "device_name": "ai-thinker-cam",
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

```bash
# Read current configuration
curl http://DEVICE_IP/api/config

# Adjust flash sensitivity
curl -X POST http://DEVICE_IP/api/config \
  -H 'Content-Type: application/json' \
  -d '{"flash_threshold":60}'

# Check system status
curl http://DEVICE_IP/api/status | jq '.data'

# Monitor brightness levels
curl http://DEVICE_IP/metrics | grep brightness

# Capture single photo
curl http://DEVICE_IP/capture -o photo.jpg
```

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
3. Test thoroughly on AI-Thinker ESP32-CAM hardware
4. Submit a pull request with detailed testing notes

---

<p align="center">
  <strong>Built with ❤️ for the ESP32 community</strong><br>
  <em>AI-Thinker ESP32-CAM Professional Firmware</em>
</p>