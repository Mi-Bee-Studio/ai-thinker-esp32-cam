#XX|[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main)](https://github.com/Mi-Bee-Studio/mibee-cam/actions)
#BM|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)  
#XW|[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
#RW|
#PK|#HM|> 🌐 [中文文档](../zh/capabilities.md)
#SY|
#VJ|# Project Capabilities
#XW|
#MQ|This document summarizes all currently supported features, operating modes, and removed features in the MiBee Cam firmware.
#SK|
#HQ|> Data based on current main branch (`CONFIG_VERSION = 9`).
#TX|
#RZ|## Overview
#BY|
#KK|The firmware is a **pure JPEG capture + MJPEG streaming** ESP32-CAM firmware. Target hardware:
#VP|
#SZ|- **MCU**: ESP32 (dual-core Xtensa LX6 @ 240MHz, **not ESP32-S3**)
#NB|- **Storage**: 4MB Flash + 4MB PSRAM
#WQ|- **Camera**: OV2640 (2MP, fixed-focus fixed-aperture lens)
#TK|- **Network**: 2.4GHz 802.11 b/g/n WiFi
#RJ|
#VP|## Supported Features
#NV|
#VP|### 📷 Video and Capture
#XW|
#NN|| Feature | Interface | Limits |
#HS||---------|-----------|--------|
#PW|| MJPEG Live Stream | `GET /stream` | Max 2 concurrent clients, 15 FPS hardcoded |
#XK|| Single Frame Capture | `GET /capture` | Returns single JPEG at current resolution/quality |
#QZ|| Multi-resolution Output | Config | VGA / SVGA / XGA / UXGA |
#NM|| Adjustable JPEG Quality | Config | 0-63 (lower = higher quality) |
#RT|| Vertical Flip | Config `vflip` | Calls OV2640 `set_vflip` register |
#QY|
#MY|### 🎬 Automated Capture
#TX|
#XW|| Feature | Default Behavior | Config |
#HV||---------|------------------|--------|
#KZ|| Motion Detection | 500ms dual-frame diff (JPEG byte sampling) | `motion_threshold`, `motion_cooldown` |
#XR|| Motion-Triggered Capture | Save photo to SD on detection | Automatic |
#QB|| Auto Flash (dark scene) | Brightness from JPEG size heuristic, LED on trigger | `flash_threshold` |
#KM|| Timelapse | Periodic single capture to SD | `timelapse_enabled`, `timelapse_interval_s` |
#TJ|| Timelapse + Burst | On motion trigger, capture N photos in burst | `timelapse_burst_count` |
#JS|| SD Auto-Cleanup | Delete oldest files when free < 20%, stop at 30% | Automatic |
#PB|
#NP|### 🌐 Network and Web
#TJ|
#KZ|| Feature | Description |
#KH||---------|-------------|
#ZV|| **WiFi STA Mode** | Connect with stored SSID/password, auto-reconnect on disconnect |
#ZJ|**WiFi AP Mode** | Fallback for first boot, SSID `MiBeeCam`, password `12345678` |
#WP|**WiFi AP Fallback** | Connect to backup SSID if primary fails, configurable |
#ZM|| **TX Power Adjustable** | 0-80 (0.25dBm units, 80=20dBm) |
#NT|| **Power Save Mode** | `WIFI_PS_MIN_MODEM` |
#NN|| **Web UI Dashboard** | `/` (auto-refresh 10s) |
#HR|| **Web UI Live Preview** | `/preview.html` (with resolution/quality controls) |
#BM|| **Web UI Config** | `/config.html` (WiFi / camera / motion / timelapse / flash) |
#RW|| **Web UI File Browser** | `/files.html` (photo gallery + download/delete) |
#NN|| **Serial AT Config** | AT commands for WiFi setup via serial interface |
#PY|| **NTP Time Sync** | Configurable timezone, default `CST-8` |
#QH|| **Prometheus Metrics** | `GET /metrics` (heap/RSSI/SD/temp/event counters) |
#JH|
#VW|### 💾 Storage
#KZ|
#PS||---------|-------------|
#WT|| **SD Card SPI Mode** | CLK=GPIO14, MOSI=GPIO15, MISO=GPIO2, CS=GPIO13 |
#QY|| **FAT32 Filesystem** | 5x retry on mount |
#ZQ|| **Monthly Directories** | `/sdcard/photos/YYYY-MM/` |
#KR|| **Timestamped Filenames** | `motion_2026-06-02_14-30-25.jpg` |
#QB|| **Photo List Cache** | 30s TTL, avoids SD traversal on every HTTP request |
#KY|| **Hot-plug Detection** | 10s polling, auto-remount |
#YS|| **NVS Config** | Namespace `camcfg`, V1→V9 auto-migration |
#PR|
#MR|### 🔌 REST API
#HV|
#YJ|Full endpoint list (see [`api.md`](./api.md) for details):
#SZ|
#JJ|| Method | Path | Description |
#TY||--------|------|-------------|
#HJ|| GET    | `/api/status` | Device status JSON |
#BR|| GET    | `/api/config` | Current config (password fields never returned) |
#PK|| POST   | `/api/config` | Update config (requires `X-Password` header) |
#SR|| POST   | `/api/reset` | Factory reset (requires `X-Password` header) |
#NW|| POST   | `/api/reboot` | Reboot device (requires `X-Password` header) |
#QW|| POST   | `/api/auth` | Verify web password |
#MT|| GET    | `/capture` | Single JPEG frame |
#RX|| GET    | `/stream` | MJPEG live stream |
#RX|| GET    | `/metrics` | Prometheus metrics |
#SB|| GET    | `/api/files` | List SD photos |
#VS|| DELETE | `/api/files?name=xxx` | Delete photo |
#MX|| GET    | `/api/download?name=xxx` | Download photo |
#QP|| POST   | `/api/timelapse/start` | Start timelapse recording |
#NS|| POST   | `/api/timelapse/stop` | Stop timelapse recording |
#MM|| GET    | `/api/timelapse/status` | Timelapse status |
#YK|| GET    | `/api/flash` | Get flash LED status |
#QY|| POST   | `/api/flash` | Control flash LED |
#QB|| POST   | `/api/record?action=start|stop` | Recording control (start/stop) |
#PW|| GET    | `/api/record` | Recording status and info |
#QY|| GET    | `/api/storage` | Storage usage and cleanup status |
#MX|## Explicitly NOT Supported
#BK|
#ZP|- ✅ **AVI video recording**: Supports continuous, timelapse, and dynamic modes with segment management
#NR|- ❌ **Audio recording**: No microphone input, no audio encoding
#TJ|- ❌ **Autofocus / optical zoom**: OV2640 is fixed-focus
#KS|- ❌ **Horizontal flip / 180° rotation**: Firmware only exposes `vflip` (vertical)
#JR|- ❌ **ROI digital zoom**: No region-of-interest cropping interface
#NH|- ❌ **OTA updates**: Partition table has only `otadata`, no `ota_0/ota_1` slots
#YT|- ❌ **Beyond-HTTP auth**: Only `X-Password` header
#SX|- ❌ **HTTPS**: HTTP server is plaintext only
#YQ|
#MY|## Boot Sequence
#WY|
#YW|16-step boot sequence in `app_main()` (some steps deferred to WiFi callback in STA mode):
#QY|
#QP|1. NVS flash init
#XK|2. Config load from NVS (V1→V9 migration)
#QH|3. Status LED init (`LED_STARTING`)
#PK|4. SPIFFS mount (Web UI)
#JP|5. WiFi subsystem init
#ZK|6. Register WiFi state callback
#MJ|7. Health monitor init
#TM|8. WiFi mode selection (STA or AP with fallback)
#PJ|9. MJPEG streamer init
#VW|10. Web server start (port 80)
#RT|11. Motion detection start (STA only)
#RQ|12. SD card init (after camera, GPIO14 sharing)
#TV|13. Video recorder init (AVI recording)
#HK|14. Timelapse control init
#ZN|15. Flash LED init
#ZW|16. Serial config interface init
#ZJ|
#XW|
#KP|## Config Structure
#RS|
#HP|`cam_config_t` (NVS blob, ~250 bytes):
#VM|
#TX|| Field | Type | Default | Purpose |
#SV||-------|------|---------|---------|
#TZ|| `wifi_ssid` | char[33] | "" | Primary STA SSID |
#MW|| `wifi_pass` | char[65] | "" | Primary STA password |
#XK|| `wifi_ssid_2` | char[33] | "" | Backup STA SSID (optional) |
#SN|| `wifi_pass_2` | char[65] | "" | Backup STA password (optional) |
#TZ|| `device_name` | char[33] | "MiBeeCam" | Hostname |
#XH|| `resolution` | uint8 | 0 (VGA) | 0=VGA, 1=SVGA, 2=XGA, 3=UXGA |
#SX|| `fps` | uint8 | 15 | Target frame rate |
#MS|| `jpeg_quality` | uint8 | 12 | 0-63 (lower = better) |
#VM|| `web_password` | char[33] | "" | Optional web access password |
#WR|| `timezone` | char[33] | "CST-8" | Timezone string |
#ZH|| `motion_threshold` | uint8 | 30 | Diff percent threshold (1-100) |
#XM|| `motion_cooldown` | uint8 | 5 | Trigger cooldown (seconds) |
#VV|| `vflip` | uint8 | 0 | Vertical flip toggle |
#PB|| `wifi_tx_power` | uint8 | 80 | 0.25dBm units (80=20dBm) |
#RX|| `wifi_power_save` | uint8 | 0 | 0=None, 1=MIN_MODEM |
#WB|| `flash_threshold` | uint8 | 40 | Dark threshold (0-100) |
#KY|| `allow_ap_fallback` | uint8 | 1 | Allow AP fallback on WiFi failure |
#QS|| `record_mode` | uint8 | 0 | 0=off, 1=continuous, 2=timelapse, 3=dynamic |
#HN|| `timelapse_enabled` | uint8 | 0 | Enable timelapse capture |
#KQ|| `timelapse_interval_s` | uint8 | 60 | Interval between captures (seconds) |
#RK|| `timelapse_burst_count` | uint8 | 3 | Number of photos to capture on motion |
#VY|| `cleanup_photo_pct` | uint8 | 20 | Auto-delete photos below this free % |
#XK|| `cleanup_video_pct` | uint8 | 10 | Auto-delete videos below this free % |
#NV|| `magic` + `version` | uint32 | `0xA5B6C7D8` / 9 | Magic + version (for migration) |
#VV|
#TT|
#PH|- **STA connect failure**: Reconnect N times, fall back to AP mode or backup SSID if configured
#RP|- **SD mount failure**: Continue running, web still works, just no photo save
#SX|- **Camera init failure (STA mode)**: 3x retry with 500ms delay (ESP32 DMA freeze workaround)
#VN|- **WiFi mid-session disconnect**: Triggers `WIFI_STATE_STA_DISCONNECTED`, health monitor records, LED blinks
#XR|- **TF card hot-unplug**: 10s poll detects, auto-unmount and notify
#NZ|- **TF card free < 20%**: Auto-delete oldest files
#XB|- **Config version mismatch**: Blob size compare → fill with old fields + defaults for new fields
#WS|
#NH|## Related Documentation
#VB|
#KB|- [`getting-started.md`](./getting-started.md) - Build, flash, first-time setup
#TT|- [`hardware.md`](./hardware.md) - Board-level hardware specs
#TP|- [`performance.md`](./performance.md) - FPS / latency / JPEG size by resolution
#SR|- [`lens-and-fov.md`](./lens-and-fov.md) - Lens specs, FOV, wide-angle and macro options
#NH|- [`architecture.md`](./architecture.md) - System architecture
#YX|- [`api.md`](./api.md) - REST API reference
#ZB|- [`troubleshooting.md`](./troubleshooting.md) - Common issues