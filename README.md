> 🌐 [中文文档](README.zh.md)

# AI_Thinker ESP32-CAM Firmware

Open-source ESP32-CAM firmware with web UI, MJPEG streaming, motion detection, and NAS upload support.

## Features

- **Camera Capture** — OV2640 camera, VGA/SVGA/XGA/UXGA resolutions, JPEG output
- **WiFi Management** — STA mode with auto-reconnect, AP hotspot fallback for first-time setup  
- **Web Interface** — Dashboard, live MJPEG preview, configuration pages
- **MJPEG Streaming** — Real-time video at up to 15 FPS via `/stream` endpoint
- **Motion Detection** — Frame-difference algorithm with configurable threshold and cooldown
- **Intelligent Flash** — Sensor-based brightness detection with automatic flash for dark scenes
- **SD Card Storage** — Automatic JPEG photo saving to TF card (photo storage only)
- **NAS Upload** — Automatic upload on motion trigger (WebDAV/HTTP POST)
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
│   │   ├── webdav_client.c/h  # WebDAV protocol client
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

All settings are stored in NVS (config version 4) and accessible via web UI (`/config.html`) or REST API (`GET/POST /api/config`).

### WiFi
- **ssid** — WiFi network name
- **wifi_pass** — WiFi password (never returned by API for security)

### Device
- **device_name** — Device name shown in dashboard (default: `ai-thinker-cam`)
- **web_password** — Optional web UI access password (blank = no auth)

### Camera
- **resolution** — Image resolution: 5=VGA, 6=SVGA, 7=XGA, 8=UXGA (default: SVGA)
- **quality** — JPEG quality 1-63, lower = better (default: 10)

### Motion Detection
- **motion_enabled** — Enable/disable motion detection (default: enabled)
- **motion_threshold** (1-100) — Frame difference percentage to trigger motion (default: 30). Higher = less sensitive.
- **motion_cooldown** (1-60 seconds) — Cooldown between consecutive triggers (default: 5)

### Brightness & Flash

The firmware detects scene brightness using **grayscale pixel probing** — the most reliable method for the OV2640 sensor on AI-Thinker boards.

#### How It Works

1. **Grayscale Probe (primary)**: Every 30 seconds, the camera temporarily switches to GRAYSCALE+QQVGA (160×120) mode and captures one frame. All pixel luminance values are averaged to produce an accurate brightness reading (0-100%). Two warmup frames are discarded after the mode switch to ensure stable exposure. The camera then restores JPEG mode. The entire probe takes ~1.5 seconds.

2. **JPEG Fallback**: When grayscale probing is not possible (e.g., MJPEG streaming clients are connected, which would be disrupted by the camera mode switch), the system falls back to a JPEG frame size heuristic — smaller JPEG files indicate darker scenes. This is less accurate but always available.

3. **Auto Flash**: When a motion event is detected in a dark scene (`brightness_pct < flash_threshold`), the flash LED (GPIO4) automatically turns on at ~80% PWM duty (safe for the AI-Thinker board's lack of current-limiting resistor) for the photo capture, then immediately turns off.

4. **Dark Scene Motion Sensitivity**: In dark scenes, JPEG frames have very low byte variance, making motion harder to detect. The firmware automatically lowers the effective motion threshold to 1/4 of the configured value (minimum 5%) when a dark scene is detected.

#### Why Not Sensor Registers?

The OV2640's AEC (Auto Exposure Control) registers were tested as a brightness source but found unreliable in continuous JPEG capture mode — the `aec_value` stabilizes at maximum (671) and does not change with actual lighting conditions. Grayscale pixel sampling is the only reliable method for this sensor.

#### Algorithms & Formulas

**Grayscale Probe (primary method):**

```
avg = sum(all_pixel_bytes) / total_pixels      // 0-255 grayscale average
pct = avg × 100 / 255                           // normalize to 0-100%
is_dark = (pct < flash_threshold)
```

Measured values (AI-Thinker board, SVGA, quality=10):
- Dark room (no light): avg=32, pct=12%
- Facing ceiling light: avg=136, pct=53%

**JPEG Frame Size Fallback:**

```
jpeg_kb = frame_size / 1024
if jpeg_kb >= 22:   pct = 100%      // bright
elif jpeg_kb <= 12: pct = 0%        // very dark
else:               pct = (jpeg_kb - 12) × 100 / 10   // linear 12-22 KB
is_dark = (pct < flash_threshold)
```

Measured JPEG sizes (SVGA, quality=10):
- Dark room: ~12-14 KB
- Dim indoor: ~14-17 KB
- Facing light: ~17-25 KB

**Flash LED PWM Control:**

```
// GPIO4, LEDC Timer 1 / Channel 1 (Timer 0 used by camera XCLK)
// PWM: 2 kHz, 8-bit resolution (0-255 duty)
// Duty = 205 (~80%) — safe max for AI-Thinker (no current-limit resistor)

ON:  ledc_set_duty(205) → wait 200ms warmup → capture photo → ledc_set_duty(0)
OFF: ledc_set_duty(0)
```

Flash is used ONLY for the final photo capture in `handle_motion_event()`. It is never turned on between reference/comparison frames during motion detection — doing so creates false 80%+ frame differences.

**Dark Scene Motion Threshold Reduction:**

```
if dark_scene:
    effective_thresh = max(motion_threshold / 4, 5)   // 1/4 of configured, min 5%
else:
    effective_thresh = motion_threshold               // use as-configured
```

#### Configuration

- **flash_threshold** (0-100) — Brightness percentage below which the flash triggers on motion events (default: 40)
  - **0** = Flash never triggers (flash disabled)
  - **20-30** = Only trigger flash in very dark scenes
  - **34-40** (recommended) = Trigger flash in moderately dark indoor environments
  - **60-80** = Trigger flash in dim lighting (hallways, twilight)
  - **100** = Flash always triggers on every motion event

  Adjust via the Web UI config page (`/config.html`) or REST API.

#### Monitoring

Brightness data is available through multiple channels:

- **Dashboard** (`/index.html`): Auto-refreshes every 10 seconds, shows `scene_dark` status
- **Status API** (`GET /api/status`): Returns `brightness_pct`, `brightness_method` ("grayscale" or "jpeg-fallback"), `scene_dark`
- **Prometheus** (`GET /metrics`): `esp32_brightness_value`, `esp32_brightness_method{method="grayscale|jpeg-fallback"}`, `esp32_scene_dark`
- **Serial Log**: `Grayscale probe: avg=32, pct=12%, dark=YES (probe took 1490 ms)`
### NAS Upload
- **nas_protocol** — 0=HTTP POST, 1=WebDAV
- **nas_host** — Server IP or hostname
- **nas_port** — Server port
- **nas_user** / **nas_pass** — Authentication credentials
- **nas_path** — Remote upload path (e.g., `/photos`)

### System
- **timezone** — Timezone string (e.g., `CST-8` for Beijing time)

### Example API Usage

```bash
# Read current config
curl http://DEVICE_IP/api/config

# Set flash threshold to 60 (more sensitive to dim lighting)
curl -X POST http://DEVICE_IP/api/config \
  -H 'Content-Type: application/json' \
  -d '{"flash_threshold":60}'

# Check brightness status
curl http://DEVICE_IP/api/status | jq '.data | {brightness_pct, brightness_method, scene_dark}'

# View Prometheus metrics
curl http://DEVICE_IP/metrics | grep brightness
```
## License

MIT License