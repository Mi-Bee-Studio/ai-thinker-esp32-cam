<p align="center">
  <img src="https://img.shields.io/badge/ESP32--CAM-Firmware-blue?style=for-the-badge&logo=esp32&logoColor=white" alt="ESP32-CAM Firmware">
  <img src="https://img.shields.io/badge/ESP--IDF-v6.0.1-green?style=for-the-badge&logo=espressif&logoColor=white" alt="ESP-IDF v6.0.1">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge" alt="MIT License">
  <img src="https://img.shields.io/badge/Platform-ESP32-orange?style=for-the-badge&logo=arduino&logoColor=white" alt="ESP32 Platform">
  <img src="https://img.shields.io/badge/Language-C-red?style=for-the-badge&logo=c&logoColor=white" alt="C Language">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Production--Ready-brightgreen?style=for-the-badge" alt="Production Ready">
  <img src="https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?style=for-the-badge&logo=github&logoColor=white" alt="Build Status">
  <img src="https://img.shields.io/github/release/Mi-Bee-Studio/ai-thinker-esp32-cam?style=for-the-badge&logo=github&logoColor=white" alt="Latest Release">
</p>

<h1 align="center">MiBee Cam Firmware</h1>

<p align="center">
  <strong>Professional-grade camera firmware for MiBee Cam boards with OV2640 streaming, motion detection, and REST API</strong>
</p>

<p align="center">
  🌐 <a href="README.zh.md">中文文档</a> • 📚 <a href="docs/en/">English Documentation</a> • 🔧 <a href="docs/en/getting-started.md">Getting Started</a>
</p>

---

## ✨ Features

**Key Features:**
- MJPEG video streaming with snapshot fallback (2 client limit)
- Motion detection with configurable sensitivity
- SD card storage for photos and recordings
- Timelapse and burst capture modes
- Smart Flash Control with LEDC PWM
- Dual WiFi failover with smart roaming
- File manager with type filtering (photos/recordings)
- Web-based firmware upgrade (OTA) + Web UI upgrade (SPIFFS OTA)
- Serial AT command WiFi setup
- Prometheus metrics endpoint
- REST API for complete control

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

## 🌐 REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web interface |
| GET | `/api/status` | System status |
| GET | `/api/config` | Get configuration |
| POST | `/api/config` | Update configuration |
| POST | `/api/reset` | Factory reset |
| POST | `/api/reboot` | Reboot device |
| GET | `/capture` | Single JPEG photo |
| GET | `/stream` | MJPEG live stream |
| GET | `/metrics` | Prometheus metrics |
| GET | `/api/files?type=all\|photos\|recordings` | List SD card files |
| DELETE | `/api/files?name=xxx&type=photo\|recording` | Delete file |
| GET | `/api/download?name=xxx&type=photo\|recording` | Download file |
| POST | `/api/record?action=start\|stop` | Recording control |
| GET | `/api/record` | Recording status |
| GET | `/api/storage` | Storage usage |
| GET | `/api/motion/status` | Motion detection status |
| POST | `/api/motion/enable` | Enable motion detection |
| POST | `/api/motion/disable` | Disable motion detection |
| GET | `/api/flash` | Flash status |
| POST | `/api/flash` | Toggle flash on/off |
| POST | `/api/timelapse/start` | Start timelapse |
| POST | `/api/timelapse/stop` | Stop timelapse |
| GET | `/api/timelapse/status` | Timelapse status |
| GET | `/api/ota/info` | OTA partition info |
| POST | `/api/ota/upload` | Upload firmware (OTA) |
| POST | `/api/ota/spiffs` | Upload Web UI (SPIFFS OTA) |
| GET | `/api/auth` | Verify password |

---

## 📁 Project Structure

```
ai-thinker-esp32-cam/
├── main/
│   ├── main.c              # System entry, 19-step boot sequence
│   ├── common.h            # Pin mappings, cam_config_t, enums
│   ├── camera_driver.c/h   # OV2640 camera driver (DMA optimized)
│   ├── wifi_manager.c/h    # WiFi AP/STA management (callbacks)
│   ├── config_manager.c/h  # NVS config persistence (version 9)
│   ├── web_server.c/h      # HTTP server + REST API + SPIFFS
│   ├── mjpeg_streamer.c/h  # MJPEG real-time streaming (async)
│   ├── storage_manager.c/h # SD card storage (hot-plug detect)
│   ├── motion_detect.c/h   # Motion detection (frame diff)
│   ├── status_led.c/h      # Status LED driver
│   ├── time_sync.c/h       # NTP time synchronization
│   ├── health_monitor.c/h  # Prometheus metrics collection
│   ├── video_recorder.c/h  # AVI video recording (3 modes)
│   ├── timelapse.c/h       # Timelapse capture
│   ├── flash_led.c/h       # Flash LED control (PWM)
│   ├── serial_config.c/h   # Serial AT command config
│   └── web_ui/             # Web interface files
├── docs/                   # Documentation (en + zh)
├── partitions.csv          # Flash partition table
├── sdkconfig.defaults      # SDK configuration
├── main/idf_component.yml  # Component dependencies
└── CMakeLists.txt          # Build configuration
```

---

## 🔄 Boot Sequence

The firmware follows a **16-step** initialization process. Steps marked *(deferred)* are executed in the deferred STA services task after WiFi connects.

1. **NVS Flash Init** — Configuration storage system
2. **Config Load** — Retrieve persisted settings (version 13)
3. **Status LED** — Initialize GPIO33 system indicator
4. **SPIFFS Mount** — Web UI filesystem preparation
5. **SD SPI Bus Release** — Free GPIO14 for camera/WiFi init
6. **WiFi Subsystem** — Network interface and event loop
7. **WiFi Callback** — Connection state change registration
8. **Health Monitor** — System metrics collection daemon
9. **WiFi Mode Selection** — STA if configured, AP otherwise
10. **MJPEG Streamer** — Video server init *(deferred to WiFi connect)*
11. **Time Sync** — NTP client *(deferred to WiFi connect)*
12. **Web Server** — HTTP server on port 80 with REST API
13. **Motion Detection** — Frame monitoring service *(deferred to WiFi connect)*
14. **SD Card Init** — Storage interface + file list cache warm-up
15. **Serial Config** — AT command interface over UART
16. **NAS Uploader** — *Removed (not implemented)*
17. **Boot Button** — *Disabled (GPIO0 = camera XCLK)*
18. **Video Recorder** — AVI recording system *(deferred STA services)*
19. **Timelapse & Flash LED** — Capture service + PWM flash *(deferred STA services)*

---

## ⚙️ Configuration

All settings are stored in NVS with version 9 schema and accessible via web UI (`/config.html`) or REST API (`GET/POST /api/config`).

### Network Configuration
```json
{
  "wifi_ssid": "your_wifi_network",
  "wifi_pass": "your_password",
  "wifi_ssid_2": "backup_network",
  "wifi_pass_2": "backup_password",
  "allow_ap_fallback": true,
  "ap_ssid": "MiBeeCam",
  "ap_pass": "12345678"
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
  "resolution": 6,
  "quality": 10
}
```
- **Resolution**: 5=VGA, 6=SVGA (default), 7=XGA, 8=UXGA
- **Quality**: JPEG 1-63 (lower = better, default: 10)

### Motion Detection
```json
{
  "motion_enabled": true,
  "motion_threshold": 30,
  "motion_cooldown": 5
}
```

### Flash Control
```json
{
  "flash_threshold": 40
}
```
- **0** = Flash disabled
- **34-40** = Recommended for indoor use
- **100** = Flash always triggers

### System Configuration
```json
{
  "timezone": "CST-8"
}
```

---

## 💡 Usage Examples

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

# Start video recording
curl -X POST "http://DEVICE_IP/api/record?action=start"

# Stop video recording
curl -X POST "http://DEVICE_IP/api/record?action=stop"

# Check recording status
curl http://DEVICE_IP/api/record
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

- **GPIO14 Critical**: GPIO14 is shared between camera XCLK and SD card CLK. After camera initialization, certain SD card operations become unreliable:
  - `f_getfree()` and `stat()` operations may hang
  - `opendir()` may fail
  - Only basic operations work reliably: `mkdir`, `fopen`, `fwrite`, `fclose`
- **DMA Freeze**: Camera initialization deferred after WiFi STA connection to prevent ESP32 DMA freeze (esp32-camera#620)
- **WiFi RF**: Board has marginal WiFi RF (PCB antenna). Full RF calibration forced every boot (`CONFIG_ESP_PHY_RF_CAL_FULL=y`). If WiFi won't connect, erase phy_init partition: `esptool.py erase_region 0xf000 0x1000`
- **PSRAM Required**: 8MB PSRAM is mandatory for camera frame buffer allocation
- **OTA Updates**: Firmware supports web-based OTA updates:
  - Firmware: Upload `build/mibee_cam.bin` via Settings → Firmware Upgrade
  - Web UI: Upload `build/spiffs.bin` via Settings → Web UI Upgrade
  - First flash requires serial: `idf.py -p /dev/ttyUSB0 flash`

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

## 📋 Changelog

### v0.2.0 (2026-07-18)

**New Features:**
- **File Manager**: New "文件" page showing all SD card files (photos + recordings) with type filter tabs
- **Web UI Upgrade**: Upload SPIFFS image via web to update HTML/CSS/JS without serial flash (`POST /api/ota/spiffs`)
- **Firmware OTA**: Web-based firmware upload with progress bar and auto-reboot (`POST /api/ota/upload`)
- **Auto Version**: Firmware version derived from git tags via `git describe`

**Improvements:**
- **Navigation Restructured**: 仪表盘 → 实时预览 → 文件 → 设置
- **Settings Page Collapsible**: All settings sections collapsed by default, reducing scroll length
- **Firmware Upgrade Embedded**: OTA upload moved into Settings page (no separate page)
- **File List API**: `/api/files` now supports `type` parameter (all/photos/recordings) with boot-time cache
- **Download API**: `/api/download` supports both photos and recordings

**Bug Fixes:**
- Fixed file list cache corruption after first query (tab characters not restored)

### v0.1.0

- Initial release with MJPEG streaming, motion detection, SD card storage
- Timelapse and burst capture modes
- Dual WiFi failover configuration
- REST API for complete control
- Prometheus metrics endpoint

---

<p align="center">
  <strong>Built with ❤️ for the ESP32 community</strong><br>
  <em>MiBee Cam Professional Firmware</em>
</p>
