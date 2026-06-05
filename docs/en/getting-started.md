[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/ai-thinker-esp32-cam/release.yml?branch=main)](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP-IDF-v6.0.1-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)  
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

#SX|> 🌐 [中文文档](../zh/getting-started.md)

# Getting Started

This guide will help you get the AI_Thinker ESP32-CAM firmware up and running. Follow these steps to build, flash, and configure your device.

## Prerequisites

### Hardware
- AI_Thinker ESP32-CAM board
- OV2640 camera module
- TF card (optional, for photo storage)
- USB cable for power and programming
- Computer with USB port

### Software
- **ESP-IDF v6.0.1** — [Installation Guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/get-started/index.html)
- **Python 3.8 or later**
- **Git**
- **esptool.py** (usually included with ESP-IDF)

## Installation

### 1. Clone the Repository

```bash
git clone https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam.git
cd AI_Thinker-ESP32-cam
```

### 2. Set up ESP-IDF Environment

```bash
# Windows (Command Prompt)
C:\Espressif\esp-idf> export.bat

# Linux/Mac
source ~/esp/esp-idf/export.sh

# Set up Python virtual environment (first time only)
python3 $IDF_PATH/tools/idf_tools.py install-python-env
# Creates: ~/.espressif/python_env/idf6.0_py3.12_env/
source ~/esp/esp-idf/export.sh
```

### 3. Configure Target

```bash
idf.py set-target esp32
```

This sets the target to the original ESP32 (not ESP32-S3).

## Building the Firmware

### Clean Build (Recommended First Time)
```bash
idf.py clean
idf.py build
```

### Build Without Cleaning
```bash
idf.py build
```

The firmware will be built in the `build/` directory. Look for the final binaries:
- `build/ai-thinker-cam.bin` — Main firmware
- `build/bootloader.bin` — Bootloader
- `build/partition_table.bin` — Partition table

## Flashing the Firmware

### Serial Port Setup

1. Connect your AI_Thinker ESP32-CAM to your computer via USB
2. Identify the serial port:
   - **Windows**: Look in Device Manager under "Ports (COM & LPT)"
   - **Linux**: Usually `/dev/ttyUSB0`, `/dev/ttyACM0`, or similar
   - **Mac**: Usually `/dev/tty.usbserial-XXXX` or `/dev/tty.usbmodemXXXX`

### Flash the Firmware

```bash
idf.py -p COM8 flash monitor
```

Replace `COM8` with your actual serial port. The `flash` command will:
1. Erase the flash memory
2. Write the bootloader, firmware, and partition table
3. Reset the device

### Monitor Serial Output

After flashing, the serial monitor will start automatically, showing the boot sequence:

```
I (30) boot: ESP-IDF v6.0 2nd stage bootloader
I (30) boot: compile time 12:00:00
I (30) boot: chip revision: v0.2
...
I (1234) main: Boot step 1: NVS flash initialization
I (1235) main: Boot step 2: Configuration load from NVS
...
I (2345) main: System startup complete
```

## First-time Configuration

### AP Mode First Boot

When the device boots for the first time, it will enter AP mode because no WiFi credentials are stored:

1. **Connect to WiFi**: Connect to the network **ai-thinker-cam** (password: `12345678`)
2. **Access Web Interface**: Open your web browser and navigate to **http://192.168.4.1**
3. **Configure WiFi**: 
   - Go to the "Configuration" page
   - Enter your WiFi SSID and password
   - Set your device name (optional)
   - Configure other settings as needed
4. **Save Changes**: Click "Save" to restart the device

### STA Mode Operation

After successful configuration, the device will reboot and connect to your WiFi network in STA mode:

1. The device will attempt to connect to your configured WiFi network
2. If successful, it will get an IP address from your router
3. You can now access the web interface at the device's IP address (shown in serial monitor)
4. The status LED will indicate different states during connection

## Verify Installation

### Check Web Interface
Open a web browser and navigate to your device's IP address. You should see:
- Dashboard with device status
- Camera preview
- Configuration options

### Test API Endpoints

Use curl or a similar tool to test the API:

```bash
# Get device status
curl http://<device-ip>/api/status

# Test MJPEG stream
curl -s http://<device-ip>/stream | head -c 100

# Get metrics
curl http://<device-ip>/metrics
```

### Test Camera Capture

```bash
# Download a test JPEG
curl -o test.jpg http://<device-ip>/capture
```

## Common Issues

### Build Errors
- **Target not set**: Run `idf.py set-target esp32` first
- **Missing components**: ESP-IDF will automatically download dependencies
- **Compiler errors**: Check for syntax errors in your code

### Flashing Issues
- **Serial port not found**: Check device drivers or try a different cable
- **Permission denied**: Run with administrator/sudo privileges
- **Device not responding**: Check boot mode (should be normal boot, not download mode)

### WiFi Connection Problems
- **Wrong password**: Double-check WiFi credentials
- **2.4 GHz requirement**: ESP32 only supports 2.4 GHz WiFi
- **Router settings**: Ensure WPA/WPA2 security (no WPA3)
- **AP mode fallback**: If STA fails, device automatically enters AP mode

### Camera Issues
- **No video**: Check camera module is properly seated
- **Pin conflicts**: GPIO14 is shared between camera and SD card
- **Resolution too high**: Start with VGA (640x480) for stability

## Next Steps

Once your device is running, you can:
- Explore the [User Guide](user-guide.md) for detailed usage
- Learn about [Hardware](hardware.md) specifications and pin mappings
- Configure motion detection and SD storage
- Review the [Architecture](architecture.md) for system details
- Check the [Troubleshooting](troubleshooting.md) guide for solutions
- Explore the [API](api.md) for programmatic control