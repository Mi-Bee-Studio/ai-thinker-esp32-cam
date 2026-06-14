#MT|![Build Status](https://github.com/Mi-Bee-Studio/mibee-cam/workflows/Release/badge.svg)
#MP|![Platform](https://img.shields.io/badge/platform-ESP32-blue)
#TY|![Camera](https://img.shields.io/badge/camera-OV2640-green)
#XV|![License](https://img.shields.io/badge/license-MIT-orange)
#BT|
#MR|# User Guide
#HN|
#NT|This guide explains how to use the MiBee Cam firmware, including configuration, web interface usage, and feature settings.
#JT|
#QK|## Web Interface Access
#TJ|
#ZK|### Connection Methods
#BQ|
#VY|#### STA Mode (Recommended)
#MZ|1. Device connects to your WiFi network automatically
#YM|2. Find the device IP from your router's device list or check serial monitor
#RN|3. Access: `http://<device-ip>/`
#KS|
#ZM|#### AP Mode (First-time Setup)
#WN|1. Connect to WiFi network: **MiBeeCam** (password: `12345678`)
#BV|2. Access: `http://192.168.4.1/`
#ZP|
#YX|### Web Interface Pages
#KW|
#YM|#### 1. Dashboard (`/`)
#PB|The main dashboard shows real-time device status:
#JJ|
#ZP|- **Device Information**: Name, uptime, WiFi status
#TJ|- **Camera Status**: Resolution, FPS, JPEG quality
#HY|- **Storage Information**: SD card status, free space
#SN|- **Motion Detection**: Current status, events count
#WQ|- **System Health**: CPU temperature, memory usage
#ZR|- **Network Information**: IP address, signal strength
#WV|
#HV|#### 2. Preview (`/preview.html`)
#QP|Live MJPEG video preview with controls:
#RB|
#JB|- **Live Stream**: Real-time camera feed
#VT|- **Capture Button**: Take a single photo
#HJ|- **Resolution Selector**: Change camera resolution (VGA/SVGA/XGA/UXGA)
#ZJ|- **Quality Slider**: Adjust JPEG quality (1-63, lower = better quality)
#MK|- **Download Button**: Save current frame to your computer
#QB|
#MX|#### 3. Configuration (`/config.html`)
#BK|Full device configuration interface:
#TJ|
#NZ|- **WiFi Settings**: SSID, password, device name
#XP|- **Camera Settings**: Resolution, frame rate, JPEG quality
#VM|- **Motion Detection**: Threshold, cooldown period
#HK|- **Time Settings**: Timezone, NTP server
#YV|- **Security**: Web password (optional)
#SZ|
#HV|#### 4. Files (`/files.html`)
#KW|Photo file management:
#YJ|
#XV|- **Photo Gallery**: List of saved photos organized by date
#PS|- **Download**: Individual photo download
#QB|- **Delete**: Remove photos
#TX|
#MS|## Configuration Options
#VW|
#RV|### WiFi Configuration
#JN|
#PQ|| Setting | Description | Default | Range |
#PP||---------|-------------|---------|-------|
#TW|| **WiFi Mode** | STA or AP mode | STA | STA/AP |
#YK|| **SSID** | Network name (STA mode) | Empty | String |
#VJ|| **Password** | Network password | Empty | String |
#PN|**Device Name** | Hostname/identification | MiBeeCam | 32 chars |
#YY|
#WM|### Camera Configuration
#SV|
#PQ|| Setting | Description | Default | Range |
#YW||---------|-------------|---------|-------|
#WX|| **Resolution** | Camera resolution | VGA (640×480) | 0=VGA,1=SVGA,2=XGA,3=UXGA |
#ZZ|| **Frame Rate** | FPS target | 15 | 1-30 |
#RZ|| **JPEG Quality** | Image quality (lower = better) | 12 | 1-63 |
#HX|| **Flash Control** | LED flash brightness | 0 | 0-255 (PWM) |
#BR|
#KN|### Motion Detection
#JQ|
#PQ|| Setting | Description | Default | Range |
#KJ||---------|-------------|---------|-------|
#RS|| **Motion Threshold** | Sensitivity level | 5 | 1-255 |
#JT|| **Motion Cooldown** | Minimum interval between triggers | 10 seconds | 1-255 seconds |
#MS||| **Save Photos** | Save motion photos to SD card | Enabled | Yes/No
#XB|
#VS|
#MP|### Time Configuration
#QT|
#PQ|| Setting | Description | Default | Range |
#YB||---------|-------------|---------|-------|
#PZ|| **Timezone** | POSIX timezone string | CST-8 | String |
#BM|| **NTP Server** | Time server | pool.ntp.org | Domain/IP |
#MS|
#KM|## LED Status Guide
#ZT|
#JQ|The status LED (GPIO33) indicates different system states:
#BK|
#MN|| Pattern | State | Description |
#TS||---------|-------|-------------|
#QV|| **Solid ON** | STARTING | System booting up |
#BP|| **Fast Blink (0.5s on, 0.5s off)** | WIFI_CONNECTING | Connecting to WiFi |
#RP|| **Slow Blink (2s on, 2s off)** | WIFI_CONNECTED | WiFi connected, operational |
#KQ|| **Medium Blink (1s on, 1s off)** | AP_MODE | Access point mode active |
#YQ|| **Red Fast Blink (0.2s on, 0.8s off)** | ERROR | System error condition |
#TS|
#TH|## SD Card Usage
#BP|
#MR|### Supported Formats
#SK|- **File System**: FAT16/FAT32
#WS|- **Capacity**: Up to 32GB (recommended 8GB-16GB)
#QX|- **Speed**: Class 10 or higher recommended
#BJ|
#TV|### File Organization
#XM|Photos are stored in the following directory structure:
#WN|```
#HJ|/sdcard/photos/
#RV|├── 2024-12-30/
#YT|│   ├── 14-30-25.jpg
#PS|│   ├── 14-32-10.jpg
#PZ|│   └── 14-35-42.jpg
#HV|├── 2024-12-31/
#VT|│   ├── 09-15-33.jpg
#TK|│   └── 09-18-05.jpg
#XJ|└── motion_events/
#JX|    ├── 2024-12-30/
#TX|    └── 2024-12-31/
#RH|```
#PX|
#WT|### Storage Management
#MN|- **Automatic Cleanup**: When free space < 20%, oldest photos are deleted
#SQ|- **Manual Cleanup**: Use web interface to delete specific photos
#PM|- **Monitoring**: Storage status shown on dashboard
#QX|
#VK|## Brightness & Flash
#QS|
#MT|The firmware detects scene brightness using **grayscale pixel probing** — the most reliable method for the OV2640 sensor on MiBee boards.
#QR|
#RV|### How It Works
#WX|
#KJ|1. **Grayscale Probe (primary)**: Every 30 seconds, the camera temporarily switches to GRAYSCALE+QQVGA (160×120) mode and captures one frame. All pixel luminance values are averaged to produce an accurate brightness reading (0-100%). Two warmup frames are discarded after the mode switch to ensure stable exposure. The camera then restores JPEG mode. The entire probe takes ~1.5 seconds.
#RS|
#PW|2. **JPEG Fallback**: When grayscale probing is not possible (e.g., MJPEG streaming clients are connected), the system falls back to a JPEG frame size heuristic — smaller JPEG files indicate darker scenes. This is less accurate but always available.
#VM|
#PZ|3. **Auto Flash**: When a motion event is detected in a dark scene (`brightness_pct < flash_threshold`), the flash LED (GPIO4) automatically turns on at ~80% PWM duty (safe for the MiBee board's lack of current-limiting resistor) for the photo capture, then immediately turns off.
#PT|
#WS> **Important**: Flash is used ONLY for the final photo capture in motion detection. It is never turned on between reference/comparison frames during motion detection — doing so creates false frame differences.
#TJ|
#SK|### Why Not Sensor Registers?
#HV|
#PW>The OV2640's AEC (Auto Exposure Control) registers were tested as a brightness source but found unreliable in continuous JPEG capture mode — the `aec_value` stabilizes at maximum (671) and does not change with actual lighting conditions. Grayscale pixel sampling is the only reliable method for this sensor.
#VX|
#VK|### Algorithms & Formulas
#NT|
#VH|**Grayscale Probe (primary method):**
#HJ|
#QH|```
#XW|avg = sum(all_pixel_bytes) / total_pixels      // 0-255 grayscale average
#MT|pct = avg × 100 / 255                           // normalize to 0-100%
#HN|is_dark = (pct < flash_threshold)
#ZS|```
#VQ|
#RJ|Measured values (MiBee board, SVGA, quality=10):
#VW|- Dark room (no light): avg=32, pct=12%
#MV|- Facing ceiling light: avg=136, pct=53%
#QZ|
#TJ|**JPEG Frame Size Fallback:**
#PN|
#TN|```
#SQ|jpeg_kb = frame_size / 1024
#JB|if jpeg_kb >= 22:   pct = 100%      // bright
#RN|elif jpeg_kb <= 12: pct = 0%        // very dark
#KH|else:               pct = (jpeg_kb - 12) × 100 / 10   // linear 12-22 KB
#HN|is_dark = (pct < flash_threshold)
#SQ|```
#HM|
#VQ>Measured JPEG sizes (SVGA, quality=10):
#QS>- Dark room: ~12-14 KB
#JS>- Dim indoor: ~14-17 KB
#QM>- Facing light: ~17-25 KB
#NT>
#WM>**Flash LED PWM Control:**
#NB>
#HB>```
#HQ>// GPIO4, LEDC Timer 1 / Channel 1 (Timer 0 used by camera XCLK)
#NP>// PWM: 2 kHz, 8-bit resolution (0-255 duty)
#BB>// Duty = 205 (~80%) — safe max for MiBee (no current-limit resistor)
#RS>
#VM>ON:  ledc_set_duty(205) → wait 200ms warmup → capture photo → ledc_set_duty(0)
#MK>OFF: ledc_set_duty(0)
#PJ>```
#XN>
#NN>**Dark Scene Motion Threshold Reduction:**
#MH>
#XY>```
#RN>if dark_scene:
#MN>    effective_thresh = max(motion_threshold / 4, 5)   // 1/4 of configured, min 5%
#ZR>else:
#SY>    effective_thresh = motion_threshold               // use as-configured
#YH>```
#PX>
#MT>### Configuration
#XQ>
#PQ|| Setting | Description | Default | Range |
#XZ||---------|-------------|---------|-------|
#RX|| **flash_threshold** | Brightness % below which flash triggers on motion | 40 | 0-100 |
#ZX|
#PT>Threshold guide:
#PB>- **0** = Flash never triggers (flash disabled)
#VX>- **20-30** = Only trigger flash in very dark scenes
#NB>- **34-40** (recommended) = Trigger flash in moderately dark indoor environments
#JQ>- **60-80** = Trigger flash in dim lighting (hallways, twilight)
#XS>- **100** = Flash always triggers on every motion event
#BB>
#NS>### Monitoring
#MH>
#PQ>Brightness data is available through multiple channels:
#HT>
#MN>- **Dashboard** (`/index.html`): Auto-refreshes every 10 seconds, shows `scene_dark` status
#WM>- **Status API** (`GET /api/status`): Returns `brightness_pct`, `brightness_method` ("grayscale" or "jpeg-fallback"), `scene_dark`
#HX>- **Prometheus** (`GET /metrics`): `esp32_brightness_value`, `esp32_brightness_method{method="grayscale|jpeg-fallback"}`, `esp32_scene_dark`
#MX>- **Serial Log**: `Grayscale probe: avg=32, pct=12%, dark=YES (probe took 1490 ms)`
#XJ>
#RK>## Motion Detection
#PR>
#RV>### How It Works
#KP>Motion detection uses frame-difference algorithm:
#RZ>1. Capture consecutive JPEG frames
#ZN>2. Convert to grayscale and resize for comparison
#JR>3. Calculate pixel differences between frames
#HS>4. If differences exceed threshold, trigger motion event
#WJ>
#MT>### Configuration
#YR>- **Threshold**: Higher values = less sensitive (1-255)
#ZH>- **Cooldown**: Minimum time between motion events (seconds)
#VR>- **Region**: Compares center region of frame (640x480 → 320x240)
#XP>
#JX>### Motion Events
#TR>When motion is detected:
#HQ>1. LED indicates motion (brief flash)
#QH>2. Photo saved to SD card
#BW>3. Event logged to serial output
#KJ>
#HB>
#VK>## Command Line Interface
#MT>
#VY>### Using `curl` for API Access
#TZ>
#SY>#### Get Status
#BV>```bash
#XB|curl http://192.168.1.100/api/status
#WJ|```
#BK>
#WS>#### Update WiFi Config
#BV>```bash
#QN|curl -X POST http://192.168.1.100/api/config \
#VQ|  -H "Content-Type: application/json" \
#YN|  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"MyPassword"}'
#PK|```
#BP>
#XQ>#### Capture Photo
#BV>```bash
#NJ|curl -o captured.jpg http://192.168.1.100/capture
#KV|```
#PX>
#JR>#### Reboot Device
#BV>```bash
#NV|curl -X POST http://192.168.1.100/api/reboot
#NK|```
#WZ>
#HQ>### Serial Monitor Commands
#BP>Connected via USB (COM8):
#WQ>
#MQ>```
#PT># Help command
#NS|help
#MP>
#JW># Get current configuration
#WK|get config
#XQ>
#BZ># Reboot device
#XZ|reboot
#HB>
#QB># Factory reset
#YS|reset
#VM|```
#BV>
#KZ>## Troubleshooting Common Issues
#VK>
#PP>### WiFi Connection Problems
#WQ>- **Check credentials**: Verify SSID and password are correct
#VQ>- **2.4GHz only**: Ensure your router supports 2.4GHz WiFi
#YJ>- **Signal strength**: Move closer to router if signal is weak
#ZZ>- **AP mode fallback**: Device automatically enters AP mode if STA fails
#XN>
#VW>### Camera Issues
#ZY>- **No video**: Check camera module is properly seated
#MP>- **Distorted image**: Check camera lens and connections
#PW>- **Resolution problems**: Start with VGA for stability
#PM>- **PSRAM errors**: Ensure 4MB PSRAM is installed and working
#NN>
#RS>### SD Card Problems
#YX>- **Not detected**: Try different SD card or reinsert
#RK>- **File system**: Use FAT32 format
#JN>- **Read/write errors**: Use quality SD cards (Class 10+)
#JZ>- **Full storage**: Clean up old photos or use larger card
#RN>
#KN>### Motion Detection
#HM>- **Too sensitive**: Increase threshold value
#SB>- **Not sensitive enough**: Decrease threshold value
#RP>- **False triggers**: Adjust cooldown period
#TK>- **No detection**: Check camera focus and lighting
#MM>
#MB>## Performance Optimization
#BJ>
#JV>### CPU Usage
#QN>- **Streaming**: ~15 FPS at VGA resolution
#QK>- **Motion detection**: ~5% CPU when active
#WS>- **Idle**: ~2% CPU usage
#NW>
#KS>### Memory Management
#XQ>- **Free heap**: Should remain >20KB
#RS>- **PSRAM**: Camera uses up to 2.4MB
#XV>- **SPIFFS**: Web UI ~500KB
#RX>
#SN>### Power Saving
#YJ>- **Deep sleep**: Not supported (camera requires continuous power)
#RR>- **Power management**: Automatic clock scaling based on load
#JW>- **LED control**: Status LED can be disabled for power savings
#RY>
#WY>## Security Considerations
#NN>
#TY>### Network Security
#HQ>- **WiFi encryption**: WPA2 recommended
#RK>- **Web interface**: Password protection optional but recommended
#BP>- **API access**: No authentication for read-only endpoints
#PS>
#JN>### Data Security
#HR>- **Photos**: Stored locally on SD card
#NS>- **Config**: Encrypted in NVS storage
#VJ>- **No cloud upload**: HTTPS recommended if you add cloud upload functionality
#PV>
#YR>### Physical Security
#KB>- **Factory reset**: API endpoint for emergency reset
#ZQ>- **LED status**: Visual indication of system health
#SV>- **Temperature monitoring**: Automatic thermal protection
#HN>
#YW>## Tips and Best Practices
#JR>
#NT>### Placement
#ZN>- **Indoor use**: Protected from weather and extreme temperatures
#WH>- **Mounting**: Secure mounting with proper cable management
#ZJ>- **Power**: Use dedicated power supply for best performance
#SQ>
#ZR>### Maintenance
#VP>- **Regular updates**: Check for firmware updates
#ZS>- **SD card health**: Monitor storage and replace if corrupted
#XS>- **Lens cleaning**: Keep camera lens clean for best image quality
#SV>
#NS>### Monitoring
#MN>- **Serial output**: Monitor for error messages
#BP>- **Web dashboard**: Check system status regularly
#QJ>- **API monitoring**: Use Prometheus integration for long-term monitoring
#KX>
#PB>## Support and Resources
#SW>
#PW>### Documentation
#MR>- [Hardware Specifications](hardware.md)
#PX>- [Getting Started Guide](getting-started.md)
#BV>- [Architecture Overview](architecture.md)
#JX>- [API Reference](api.md)
#NY>- [Troubleshooting Guide](troubleshooting.md)
#VN>
#YX>### Community
#TT>- GitHub Issues: Report bugs and request features
#PV>- Discussion forums: Share experiences and get help
#ZJ>- Wiki: Additional guides and examples
#TB>
#BN>### Contact
#VM>- Email: project maintainer
#KP>- GitHub: Issues and pull requests
#YW>- Documentation: Latest updates and changelog