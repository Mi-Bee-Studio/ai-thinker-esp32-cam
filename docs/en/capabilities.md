[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?branch=main)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)  
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

> 🌐 [中文文档](../zh/capabilities.md)

# Project Capabilities

This document summarizes all currently supported features, operating modes, and removed features in the MiBee Cam firmware.

> Data based on current main branch (`CONFIG_VERSION = 9`).

## Overview

The firmware is a **pure JPEG capture + MJPEG streaming** ESP32-CAM firmware. Target hardware:

- **MCU**: ESP32 (dual-core Xtensa LX6 @ 240MHz, **not ESP32-S3**)
- **Storage**: 4MB Flash + 4MB PSRAM
- **Camera**: OV2640 (2MP, fixed-focus fixed-aperture lens)
- **Network**: 2.4GHz 802.11 b/g/n WiFi

## Supported Features

### 📷 Video and Capture

| Feature | Interface | Limits |
|---------|-----------|--------|
| MJPEG Live Stream | `GET /stream` | Max 2 concurrent clients, 15 FPS hardcoded |
| Single Frame Capture | `GET /capture` | Returns single JPEG at current resolution/quality |
| Multi-resolution Output | Config | VGA / SVGA / XGA / UXGA |
| Adjustable JPEG Quality | Config | 0-63 (lower = higher quality) |
| Vertical Flip | Config `vflip` | Calls OV2640 `set_vflip` register |

### 🎬 Automated Capture

| Feature | Default Behavior | Config |
|---------|------------------|--------|
| Motion Detection | 500ms dual-frame diff (JPEG byte sampling) | `motion_threshold`, `motion_cooldown` |
| Motion-Triggered Capture | Save photo to SD on detection | Automatic |
| Auto Flash (dark scene) | Brightness from JPEG size heuristic, LED on trigger | `flash_threshold` |
| Timelapse | Periodic single capture to SD | `timelapse_enabled`, `timelapse_interval_s` |
| Timelapse + Burst | On motion trigger, capture N photos in burst | `timelapse_burst_count` |
| SD Auto-Cleanup | Delete oldest files when free < 20%, stop at 30% | Automatic |

### 🌐 Network and Web

| Feature | Description |
|---------|-------------|
| **WiFi STA Mode** | Connect with stored SSID/password, auto-reconnect on disconnect |
**WiFi AP Mode** | Fallback for first boot, SSID `MiBeeCam`, password `12345678` |
**WiFi AP Fallback** | Connect to backup SSID if primary fails, configurable |
| **TX Power Adjustable** | 0-80 (0.25dBm units, 80=20dBm) |
| **Power Save Mode** | `WIFI_PS_MIN_MODEM` |
| **Web UI Dashboard** | `/` (auto-refresh 10s) |
| **Web UI Live Preview** | `/preview.html` (with resolution/quality controls) |
| **Web UI Config** | `/config.html` (WiFi / camera / motion / timelapse / flash) |
| **Web UI File Browser** | `/files.html` (photo gallery + download/delete) |
| **Serial AT Config** | AT commands for WiFi setup via serial interface |
| **NTP Time Sync** | Configurable timezone, default `CST-8` |
| **Prometheus Metrics** | `GET /metrics` (heap/RSSI/SD/temp/event counters) |

### 💾 Storage

|---------|-------------|
| **SD Card SPI Mode** | CLK=GPIO14, MOSI=GPIO15, MISO=GPIO2, CS=GPIO13 |
| **FAT32 Filesystem** | 5x retry on mount |
| **Monthly Directories** | `/sdcard/photos/YYYY-MM/` |
| **Timestamped Filenames** | `motion_2026-06-02_14-30-25.jpg` |
| **Photo List Cache** | 30s TTL, avoids SD traversal on every HTTP request |
| **Hot-plug Detection** | 10s polling, auto-remount |
| **NVS Config** | Namespace `camcfg`, V1→V9 auto-migration |

### 🔌 REST API

Full endpoint list (see [`api.md`](./api.md) for details):

| Method | Path | Description |
|--------|------|-------------|
| GET    | `/api/status` | Device status JSON |
| GET    | `/api/config` | Current config (password fields never returned) |
| POST   | `/api/config` | Update config (requires `X-Password` header) |
| POST   | `/api/reset` | Factory reset (requires `X-Password` header) |
| POST   | `/api/reboot` | Reboot device (requires `X-Password` header) |
| POST   | `/api/auth` | Verify web password |
| GET    | `/capture` | Single JPEG frame |
| GET    | `/stream` | MJPEG live stream |
| GET    | `/metrics` | Prometheus metrics |
| GET    | `/api/files` | List SD photos |
| DELETE | `/api/files?name=xxx` | Delete photo |
| GET    | `/api/download?name=xxx` | Download photo |
| POST   | `/api/timelapse/start` | Start timelapse recording |
| POST   | `/api/timelapse/stop` | Stop timelapse recording |
| GET    | `/api/timelapse/status` | Timelapse status |
| GET    | `/api/flash` | Get flash LED status |
| POST   | `/api/flash` | Control flash LED |
| POST   | `/api/record?action=start|stop` | Recording control (start/stop) |
| GET    | `/api/record` | Recording status and info |
| GET    | `/api/storage` | Storage usage and cleanup status |
## Explicitly NOT Supported

- ✅ **AVI video recording**: Supports continuous, timelapse, and dynamic modes with segment management
- ❌ **Audio recording**: No microphone input, no audio encoding
- ❌ **Autofocus / optical zoom**: OV2640 is fixed-focus
- ❌ **Horizontal flip / 180° rotation**: Firmware only exposes `vflip` (vertical)
- ❌ **ROI digital zoom**: No region-of-interest cropping interface
- ❌ **OTA updates**: Partition table has only `otadata`, no `ota_0/ota_1` slots
- ❌ **Beyond-HTTP auth**: Only `X-Password` header
- ❌ **HTTPS**: HTTP server is plaintext only

## Boot Sequence

19-step boot sequence in `app_main()` (some steps deferred to WiFi callback in STA mode):

1. NVS flash init
2. Config load from NVS (V1→V9 migration)
3. Status LED init (`LED_STARTING`)
4. SPIFFS mount (Web UI)
5. WiFi subsystem init
6. Register WiFi state callback
7. Health monitor init
8. WiFi mode selection (STA or AP with fallback)
9. MJPEG streamer init
10. Web server start (port 80)
11. Motion detection start (STA only)
12. SD card init (after camera, GPIO14 sharing)
13. Video recorder init (AVI recording)
14. Timelapse control init
15. Flash LED init
16. Serial config interface init


## Config Structure

`cam_config_t` (NVS blob, ~250 bytes):

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `wifi_ssid` | char[33] | "" | Primary STA SSID |
| `wifi_pass` | char[65] | "" | Primary STA password |
| `wifi_ssid_2` | char[33] | "" | Backup STA SSID (optional) |
| `wifi_pass_2` | char[65] | "" | Backup STA password (optional) |
| `device_name` | char[33] | "MiBeeCam" | Hostname |
| `resolution` | uint8 | 0 (VGA) | 0=VGA, 1=SVGA, 2=XGA, 3=UXGA |
| `fps` | uint8 | 15 | Target frame rate |
| `jpeg_quality` | uint8 | 12 | 0-63 (lower = better) |
| `web_password` | char[33] | "" | Optional web access password |
| `timezone` | char[33] | "CST-8" | Timezone string |
| `motion_threshold` | uint8 | 30 | Diff percent threshold (1-100) |
| `motion_cooldown` | uint8 | 5 | Trigger cooldown (seconds) |
| `vflip` | uint8 | 0 | Vertical flip toggle |
| `wifi_tx_power` | uint8 | 80 | 0.25dBm units (80=20dBm) |
| `wifi_power_save` | uint8 | 0 | 0=None, 1=MIN_MODEM |
| `flash_threshold` | uint8 | 40 | Dark threshold (0-100) |
| `allow_ap_fallback` | uint8 | 1 | Allow AP fallback on WiFi failure |
| `record_mode` | uint8 | 0 | 0=off, 1=continuous, 2=timelapse, 3=dynamic |
| `timelapse_enabled` | uint8 | 0 | Enable timelapse capture |
| `timelapse_interval_s` | uint8 | 60 | Interval between captures (seconds) |
| `timelapse_burst_count` | uint8 | 3 | Number of photos to capture on motion |
| `cleanup_photo_pct` | uint8 | 20 | Auto-delete photos below this free % |
| `cleanup_video_pct` | uint8 | 10 | Auto-delete videos below this free % |
| `magic` + `version` | uint32 | `0xA5B6C7D8` / 9 | Magic + version (for migration) |


- **STA connect failure**: Reconnect N times, fall back to AP mode or backup SSID if configured
- **SD mount failure**: Continue running, web still works, just no photo save
- **Camera init failure (STA mode)**: 3x retry with 500ms delay (ESP32 DMA freeze workaround)
- **WiFi mid-session disconnect**: Triggers `WIFI_STATE_STA_DISCONNECTED`, health monitor records, LED blinks
- **TF card hot-unplug**: 10s poll detects, auto-unmount and notify
- **TF card free < 20%**: Auto-delete oldest files
- **Config version mismatch**: Blob size compare → fill with old fields + defaults for new fields

## Related Documentation

- [`getting-started.md`](./getting-started.md) - Build, flash, first-time setup
- [`hardware.md`](./hardware.md) - Board-level hardware specs
- [`performance.md`](./performance.md) - FPS / latency / JPEG size by resolution
- [`lens-and-fov.md`](./lens-and-fov.md) - Lens specs, FOV, wide-angle and macro options
- [`architecture.md`](./architecture.md) - System architecture
- [`api.md`](./api.md) - REST API reference
- [`troubleshooting.md`](./troubleshooting.md) - Common issues