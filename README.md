# AI_Thinker ESP32-CAM Firmware

Open-source ESP32-CAM firmware with web UI, MJPEG streaming, motion detection, and NAS upload support.

## Features

- **Camera Capture** — OV2640 camera, VGA/SVGA/XGA/UXGA resolutions, JPEG output
- **WiFi Management** — STA mode with auto-reconnect, AP hotspot fallback for first-time setup  
- **Web Interface** — Dashboard, live MJPEG preview, configuration pages
- **MJPEG Streaming** — Real-time video at up to 15 FPS via `/stream` endpoint
- **Motion Detection** — Frame-difference algorithm with configurable threshold and cooldown
- **SD Card Storage** — Automatic JPEG photo saving to TF card (photo storage only)
- **NAS Upload** — Automatic upload on motion trigger (FTP/WebDAV/HTTP POST)
- **REST API** — Full API for status, configuration, file management, and capture
- **Health Monitoring** — Prometheus-compatible `/metrics` endpoint
- **Status LED** — GPIO33 LED indicates system state
- **Time Sync** — NTP time synchronization with configurable timezone

## Hardware Requirements

- **Board**: AI_Thinker ESP32-CAM
- **Camera**: OV2640 camera module 
- **Storage**: TF card (optional, for photo storage)
- **Flash**: 4MB
- **PSRAM**: 4MB

## Pin Mapping

| Pin | GPIO | Function |
|-----|------|----------|
| XCLK | 0 | Master clock |
| SIOD | 26 | I2C data (SDA) |
| SIOC | 27 | I2C clock (SCL) |
| D0 | 5 | Parallel data |
| D1 | 18 | Parallel data |
| D2 | 19 | Parallel data |
| D3 | 21 | Parallel data |
| D4 | 36 | Parallel data (input only) |
| D5 | 39 | Parallel data (input only) |
| D6 | 34 | Parallel data (input only) |
| D7 | 35 | Parallel data (input only) |
| VSYNC | 25 | Vertical sync |
| HREF | 23 | Horizontal reference |
| PCLK | 22 | Pixel clock |
| PWDN | 32 | Power down |
| SD_CS | 13 | SD card chip select |
| SD_CLK | 14 | SD card clock (shared with camera) |
| SD_MOSI | 15 | SD card MOSI |
| SD_MISO | 2 | SD card MISO |
| LED_STATUS | 33 | Status LED (active LOW) |
| LED_FLASH | 4 | Flash LED (PWM) |
| BOOT_BTN | 0 | Factory reset button |

## Quick Start

### Prerequisites

- ESP-IDF v6.0 installed
- Python 3.8+
- esptool.py
- AI_Thinker ESP32-CAM board
- OV2640 camera module

### Build

```bash
idf.py set-target esp32
idf.py build
```

### Flash

```bash
idf.py -p COM8 flash monitor
```

Replace `COM8` with your serial port.

### First-time Configuration

On first boot, the device enters AP mode:

1. Connect to WiFi network **ai-thinker-cam** (password: `12345678`)
2. Open **http://192.168.4.1** in a browser
3. Go to the configuration page and enter your WiFi SSID and password
4. Save — the device reboots and connects in STA mode

### Factory Reset

Hold the **BOOT** button (GPIO 0) for **5 seconds** to reset configuration to defaults.

## REST API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Device status JSON |
| GET | `/api/config` | Current configuration JSON |
| POST | `/api/config` | Update configuration |
| POST | `/api/reset` | Reset to defaults |
| POST | `/api/reboot` | Reboot device |
| GET | `/capture` | Single JPEG capture |
| GET | `/stream` | MJPEG video stream |
| GET | `/metrics` | Prometheus metrics |
| GET | `/api/files` | List SD card photos |
| GET | `/api/download?name=xxx` | Download photo |
| POST | `/api/upload` | Trigger manual upload |

## Project Structure

```
AI_Thinker-ESP32-cam/
├── main/
│   ├── main.c              # System entry, 16-step boot sequence
│   ├── camera_driver.c/h   # OV2640 camera driver
│   ├── wifi_manager.c/h    # WiFi AP/STA management
│   ├── config_manager.c/h  # NVS configuration persistence
│   ├── web_server.c/h      # HTTP server + REST API
│   ├── mjpeg_streamer.c/h  # MJPEG real-time streaming
│   ├── storage_manager.c/h # SD card storage manager
│   ├── motion_detect.c/h   # Motion detection
│   ├── nas_uploader.c/h   # NAS upload scheduler
│   ├── ftp_client.c/h     # FTP protocol client
│   ├── webdav_client.c/h  # WebDAV protocol client
│   ├── status_led.c/h     # Status LED driver
│   ├── time_sync.c/h      # Time synchronization
│   ├── health_monitor.c/h # System health monitoring
│   └── web_ui/            # Web UI files
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/common.h          # Shared types and constants
└── main/idf_component.yml # Component dependencies
```

## Boot Sequence

The firmware follows a 16-step initialization:

1. NVS flash initialization
2. Configuration load from NVS
3. Status LED initialization (GPIO33)
4. SPIFFS mount for Web UI
5. Camera initialization (BEFORE WiFi to avoid I2C conflict)
6. WiFi subsystem initialization
7. WiFi state callback registration
8. Health monitor initialization
9. WiFi mode selection (STA if configured, AP otherwise)
10. MJPEG streamer initialization
11. Time sync initialization (STA mode only)
12. Web server start (port 80)
13. Motion detection start (STA mode only)
14. SD card initialization (AFTER camera to handle GPIO14 sharing)
15. NAS uploader initialization
16. BOOT button monitor task (5s hold = factory reset)

## Configuration

All settings are stored in NVS and accessible via web UI or REST API:

- WiFi SSID and password
- Device name
- Camera resolution and quality
- Motion detection threshold and cooldown
- NAS server settings (FTP/WebDAV/HTTP)
- Timezone
- Web password (optional)

## License

MIT License