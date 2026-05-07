> 🌐 [中文文档](../zh/user-guide.md)

# User Guide

This guide explains how to use the AI_Thinker ESP32-CAM firmware, including configuration, web interface usage, and feature settings.

## Web Interface Access

### Connection Methods

#### STA Mode (Recommended)
1. Device connects to your WiFi network automatically
2. Find the device IP from your router's device list or check serial monitor
3. Access: `http://<device-ip>/`

#### AP Mode (First-time Setup)
1. Connect to WiFi network: **ai-thinker-cam** (password: `12345678`)
2. Access: `http://192.168.4.1/`

### Web Interface Pages

#### 1. Dashboard (`/`)
The main dashboard shows real-time device status:

- **Device Information**: Name, uptime, WiFi status
- **Camera Status**: Resolution, FPS, JPEG quality
- **Storage Information**: SD card status, free space
- **Motion Detection**: Current status, events count
- **System Health**: CPU temperature, memory usage
- **Network Information**: IP address, signal strength

#### 2. Preview (`/preview.html`)
Live MJPEG video preview with controls:

- **Live Stream**: Real-time camera feed
- **Capture Button**: Take a single photo
- **Resolution Selector**: Change camera resolution (VGA/SVGA/XGA/UXGA)
- **Quality Slider**: Adjust JPEG quality (1-63, lower = better quality)
- **Download Button**: Save current frame to your computer

#### 3. Configuration (`/config.html`)
Full device configuration interface:

- **WiFi Settings**: SSID, password, device name
- **Camera Settings**: Resolution, frame rate, JPEG quality
- **Motion Detection**: Threshold, cooldown period
- **NAS Upload**: Protocol, server settings
- **Time Settings**: Timezone, NTP server
- **Security**: Web password (optional)

#### 4. Files (`/files.html`)
Photo file management:

- **Photo Gallery**: List of saved photos organized by date
- **Download**: Individual photo download
- **Delete**: Remove photos
- **Upload to NAS**: Send photos to NAS server

## Configuration Options

### WiFi Configuration

| Setting | Description | Default | Range |
|---------|-------------|---------|-------|
| **WiFi Mode** | STA or AP mode | STA | STA/AP |
| **SSID** | Network name (STA mode) | Empty | String |
| **Password** | Network password | Empty | String |
| **Device Name** | Hostname/identification | ai-thinker-cam | 32 chars |

### Camera Configuration

| Setting | Description | Default | Range |
|---------|-------------|---------|-------|
| **Resolution** | Camera resolution | VGA (640×480) | 0=VGA,1=SVGA,2=XGA,3=UXGA |
| **Frame Rate** | FPS target | 15 | 1-30 |
| **JPEG Quality** | Image quality (lower = better) | 12 | 1-63 |
| **Flash Control** | LED flash brightness | 0 | 0-255 (PWM) |

### Motion Detection

| Setting | Description | Default | Range |
|---------|-------------|---------|-------|
| **Motion Threshold** | Sensitivity level | 5 | 1-255 |
| **Motion Cooldown** | Minimum interval between triggers | 10 seconds | 1-255 seconds |
| **Save Photos** | Save motion photos to SD card | Enabled | Yes/No |
| **Upload Photos** | Upload motion photos to NAS | Enabled | Yes/No |

### NAS Upload Settings

| Setting | Description | Default | Range |
|---------|-------------|---------|-------|
| **Upload Protocol** | HTTP or WebDAV | HTTP | HTTP/WebDAV |
| **Server URL** | NAS server address | Empty | String |
| **Port** | Server port | 80/21/5005 | 1-65535 |
| **Username** | Authentication username | Empty | 32 chars |
| **Password** | Authentication password | Empty | 64 chars |
| **Upload Path** | Destination folder | /photos | String |
| **Retry Count** | Upload retry attempts | 3 | 1-10 |

### Time Configuration

| Setting | Description | Default | Range |
|---------|-------------|---------|-------|
| **Timezone** | POSIX timezone string | CST-8 | String |
| **NTP Server** | Time server | pool.ntp.org | Domain/IP |

## LED Status Guide

The status LED (GPIO33) indicates different system states:

| Pattern | State | Description |
|---------|-------|-------------|
| **Solid ON** | STARTING | System booting up |
| **Fast Blink (0.5s on, 0.5s off)** | WIFI_CONNECTING | Connecting to WiFi |
| **Slow Blink (2s on, 2s off)** | WIFI_CONNECTED | WiFi connected, operational |
| **Medium Blink (1s on, 1s off)** | AP_MODE | Access point mode active |
| **Red Fast Blink (0.2s on, 0.8s off)** | ERROR | System error condition |

## SD Card Usage

### Supported Formats
- **File System**: FAT16/FAT32
- **Capacity**: Up to 32GB (recommended 8GB-16GB)
- **Speed**: Class 10 or higher recommended

### File Organization
Photos are stored in the following directory structure:
```
/sdcard/photos/
├── 2024-12-30/
│   ├── 14-30-25.jpg
│   ├── 14-32-10.jpg
│   └── 14-35-42.jpg
├── 2024-12-31/
│   ├── 09-15-33.jpg
│   └── 09-18-05.jpg
└── motion_events/
    ├── 2024-12-30/
    └── 2024-12-31/
```

### Storage Management
- **Automatic Cleanup**: When free space < 20%, oldest photos are deleted
- **Manual Cleanup**: Use web interface to delete specific photos
- **Monitoring**: Storage status shown on dashboard

## Motion Detection

### How It Works
Motion detection uses frame-difference algorithm:
1. Capture consecutive JPEG frames
2. Convert to grayscale and resize for comparison
3. Calculate pixel differences between frames
4. If differences exceed threshold, trigger motion event

### Configuration
- **Threshold**: Higher values = less sensitive (1-255)
- **Cooldown**: Minimum time between motion events (seconds)
- **Region**: Compares center region of frame (640x480 → 320x240)

### Motion Events
When motion is detected:
1. LED indicates motion (brief flash)
2. Photo saved to SD card
3. NAS upload triggered (if enabled)
4. Event logged to serial output

## NAS Upload

### Supported Protocols

#### HTTP POST
- **Endpoint**: `POST /upload`
- **Headers**: `Content-Type: image/jpeg`, `X-Device-ID: <device-name>`
- **Body**: JPEG binary data
- **Best For**: Web servers like iot-server-base

#### WebDAV
- **Method**: PUT
- **Authentication**: Basic auth
- **Best For**: WebDAV-enabled cloud storage

### Upload Workflow
1. Motion detected or manual trigger
2. Photo captured and saved to SD card
3. Added to upload queue
4. Retry mechanism: 3 attempts with 2s intervals
5. Success: File deleted from queue
6. Failure: Retry later

## Command Line Interface

### Using `curl` for API Access

#### Get Status
```bash
curl http://192.168.1.100/api/status
```

#### Update WiFi Config
```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"MyPassword"}'
```

#### Capture Photo
```bash
curl -o captured.jpg http://192.168.1.100/capture
```

#### Reboot Device
```bash
curl -X POST http://192.168.1.100/api/reboot
```

### Serial Monitor Commands
Connected via USB (COM8):

```
# Help command
help

# Get current configuration
get config

# Reboot device
reboot

# Factory reset
reset
```

## Troubleshooting Common Issues

### WiFi Connection Problems
- **Check credentials**: Verify SSID and password are correct
- **2.4GHz only**: Ensure your router supports 2.4GHz WiFi
- **Signal strength**: Move closer to router if signal is weak
- **AP mode fallback**: Device automatically enters AP mode if STA fails

### Camera Issues
- **No video**: Check camera module is properly seated
- **Distorted image**: Check camera lens and connections
- **Resolution problems**: Start with VGA for stability
- **PSRAM errors**: Ensure 4MB PSRAM is installed and working

### SD Card Problems
- **Not detected**: Try different SD card or reinsert
- **File system**: Use FAT32 format
- **Read/write errors**: Use quality SD cards (Class 10+)
- **Full storage**: Clean up old photos or use larger card

### Motion Detection
- **Too sensitive**: Increase threshold value
- **Not sensitive enough**: Decrease threshold value
- **False triggers**: Adjust cooldown period
- **No detection**: Check camera focus and lighting

## Performance Optimization

### CPU Usage
- **Streaming**: ~15 FPS at VGA resolution
- **Motion detection**: ~5% CPU when active
- **Idle**: ~2% CPU usage

### Memory Management
- **Free heap**: Should remain >20KB
- **PSRAM**: Camera uses up to 2.4MB
- **SPIFFS**: Web UI ~500KB

### Power Saving
- **Deep sleep**: Not supported (camera requires continuous power)
- **Power management**: Automatic clock scaling based on load
- **LED control**: Status LED can be disabled for power savings

## Security Considerations

### Network Security
- **WiFi encryption**: WPA2 recommended
- **Web interface**: Password protection optional but recommended
- **API access**: No authentication for read-only endpoints

### Data Security
- **Photos**: Stored locally on SD card
- **Config**: Encrypted in NVS storage
- **Upload**: HTTPS recommended for cloud uploads

### Physical Security
- **Factory reset**: BOOT button for emergency reset
- **LED status**: Visual indication of system health
- **Temperature monitoring**: Automatic thermal protection

## Tips and Best Practices

### Placement
- **Indoor use**: Protected from weather and extreme temperatures
- **Mounting**: Secure mounting with proper cable management
- **Power**: Use dedicated power supply for best performance

### Maintenance
- **Regular updates**: Check for firmware updates
- **SD card health**: Monitor storage and replace if corrupted
- **Lens cleaning**: Keep camera lens clean for best image quality

### Monitoring
- **Serial output**: Monitor for error messages
- **Web dashboard**: Check system status regularly
- **API monitoring**: Use Prometheus integration for long-term monitoring

## Support and Resources

### Documentation
- [Hardware Specifications](hardware.md)
- [Getting Started Guide](getting-started.md)
- [Architecture Overview](architecture.md)
- [API Reference](api.md)
- [Troubleshooting Guide](troubleshooting.md)

### Community
- GitHub Issues: Report bugs and request features
- Discussion forums: Share experiences and get help
- Wiki: Additional guides and examples

### Contact
- Email: project maintainer
- GitHub: Issues and pull requests
- Documentation: Latest updates and changelog