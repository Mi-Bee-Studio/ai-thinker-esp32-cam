#XX|[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main)](https://github.com/Mi-Bee-Studio/mibee-cam/actions)
#BM|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)  
#XW|[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
#RW|
#PY|#SX|> 🌐 [中文文档](../zh/getting-started.md)
#SY|
#PW|# Getting Started
#XW|
#WJ|This guide will help you get the MiBee Cam firmware up and running. Follow these steps to build, flash, and configure your device.
#SK|
#NH|## Prerequisites
#TX|
#RV|### Hardware
#VB|- MiBee Cam board
#TZ|- OV2640 camera module
#SN|- TF card (optional, for photo storage)
#ZN|- USB cable for power and programming
#YT|- Computer with USB port
#MX|
#MX|### Software
#KQ|- **ESP-IDF v6.0.1** — [Installation Guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/get-started/index.html)
#SW|- **Python 3.8 or later**
#PQ|- **Git**
#HB|- **esptool.py** (usually included with ESP-IDF)
#XW|
#RW|## Installation
#JJ|
#RH|### 1. Clone the Repository
#ZR|
#BV|```bash
#BB|git clone https://github.com/Mi-Bee-Studio/mibee-cam.git
#TN|cd mibee-cam
#VX|```
#WV|
#KT|### 2. Set up ESP-IDF Environment
#MV|
#BV|```bash
#WT|# Windows (Command Prompt)
#KB|C:\Espressif\esp-idf> export.bat
#ZK|
#RR|# Linux/Mac
#YM|source ~/esp/esp-idf/export.sh
#QB|
#SS|# Set up Python virtual environment (first time only)
#WV|python3 $IDF_PATH/tools/idf_tools.py install-python-env
#TK|# Creates: ~/.espressif/python_env/idf6.0_py3.12_env/
#YM|source ~/esp/esp-idf/export.sh
#RK|```
#BN|
#PQ|### 3. Configure Target
#PZ|
#BV|```bash
#ZB|idf.py set-target esp32
#BP|```
#TW|
#WS|This sets the target to the original ESP32 (not ESP32-S3).
#WH|
#YH|## Building the Firmware
#QH|
#VV|### Clean Build (Recommended First Time)
#BV|```bash
#KY|idf.py clean
#PM|idf.py build
#WV|```
#PZ|
#WQ|### Build Without Cleaning
#BV|```bash
#PM|idf.py build
#KQ|```
#YY|
#TJ|The firmware will be built in the `build/` directory. Look for the final binaries:
#ZN|- `build/mibee-cam.bin` — Main firmware
#WB|- `build/bootloader.bin` — Bootloader
#XP|- `build/partition_table.bin` — Partition table
#SZ|
#HS|## Flashing the Firmware
#VB|
#JJ|### Serial Port Setup
#BR|
#PY|1. Connect your MiBee Cam to your computer via USB
#XY|2. Identify the serial port:
#ZR|   - **Windows**: Look in Device Manager under "Ports (COM & LPT)"
#WN|   - **Linux**: Usually `/dev/ttyUSB0`, `/dev/ttyACM0`, or similar
#RM|   - **Mac**: Usually `/dev/tty.usbserial-XXXX` or `/dev/tty.usbmodemXXXX`
#SR|
#QN|### Flash the Firmware
#XB|
#BV|```bash
#SY|idf.py -p COM8 flash monitor
#ZV|```
#RT|
#BR|Replace `COM8` with your actual serial port. The `flash` command will:
#ZH|1. Erase the flash memory
#QM|2. Write the bootloader, firmware, and partition table
#YB|3. Reset the device
#ZT|
#WV|### Monitor Serial Output
#BP|
#ZZ|After flashing, the serial monitor will start automatically, showing the boot sequence:
#SR|
#ZR|```
#XK|I (30) boot: ESP-IDF v6.0 2nd stage bootloader
#HV|I (30) boot: compile time 12:00:00
#VR|I (30) boot: chip revision: v0.2
#TZ|...
#ZW|I (1234) main: Boot step 1: NVS flash initialization
#YM|I (1235) main: Boot step 2: Configuration load from NVS
#NP|...
#ZM|I (2345) main: System startup complete
#TM|```
#YX|
#PM|## First-time Configuration
#PP|
#QS|### AP Mode First Boot
#PV|
#BQ|When the device boots for the first time, it will enter AP mode because no WiFi credentials are stored:
#BQ|
#HH|1. **Connect to WiFi**: Connect to the network **MiBeeCam** (password: `12345678`)
#JZ|2. **Access Web Interface**: Open your web browser and navigate to **http://192.168.4.1**
#MJ|3. **Configure WiFi**: 
#SY|   - Go to the "Configuration" page
#XX|   - Enter your WiFi SSID and password
#HR|   - Set your device name (optional)
#TR|   - Configure other settings as needed
#WR|4. **Save Changes**: Click "Save" to restart the device
#WV|
#KK|### STA Mode Operation
#YX|
#NB|After successful configuration, the device will reboot and connect to your WiFi network in STA mode:
#PX|
#YM|1. The device will attempt to connect to your configured WiFi network
#YS|2. If successful, it will get an IP address from your router
#BM|3. You can now access the web interface at the device's IP address (shown in serial monitor)
#VT|4. The status LED will indicate different states during connection
#QX|
#ST|## Verify Installation
#QS|
#RW|### Check Web Interface
#JM|Open a web browser and navigate to your device's IP address. You should see:
#WH|- Dashboard with device status
#TX|- Camera preview
#WS|- Configuration options
#RS|
#HS|### Test API Endpoints
#VM|
#ZS|Use curl or a similar tool to test the API:
#PT|
#BV|```bash
#BP|# Get device status
#TR|curl http://<device-ip>/api/status
#HV|
#MY|# Test MJPEG stream
#BN|curl -s http://<device-ip>/stream | head -c 100
#QH|
#PQ|# Get metrics
#ZK|curl http://<device-ip>/metrics
#WT|```
#TV|
#WS|### Test Camera Capture
#ZB|
#BV|```bash
#JV|# Download a test JPEG
#JP|curl -o test.jpg http://<device-ip>/capture
#QM|```
#NX|
#JB|## Common Issues
#QZ|
#VZ|### Build Errors
#TV|- **Target not set**: Run `idf.py set-target esp32` first
#NJ|- **Missing components**: ESP-IDF will automatically download dependencies
#TZ|- **Compiler errors**: Check for syntax errors in your code
#VK|
#PY|### Flashing Issues
#KN|- **Serial port not found**: Check device drivers or try a different cable
#TR|- **Permission denied**: Run with administrator/sudo privileges
#VY|- **Device not responding**: Check boot mode (should be normal boot, not download mode)
#HM|
#PP|### WiFi Connection Problems
#HS|- **Wrong password**: Double-check WiFi credentials
#KJ|- **2.4 GHz requirement**: ESP32 only supports 2.4 GHz WiFi
#VY|- **Router settings**: Ensure WPA/WPA2 security (no WPA3)
#HM|- **AP mode fallback**: If STA fails, device automatically enters AP mode
#VB|
#VW|### Camera Issues
#ZY|- **No video**: Check camera module is properly seated
#SV|- **Pin conflicts**: GPIO14 is shared between camera and SD card
#QZ|- **Resolution too high**: Start with VGA (640x480) for stability
#XH|
#TM|## Next Steps
#JM|
#RJ|Once your device is running, you can:
#ZK|- Explore the [User Guide](user-guide.md) for detailed usage
#SR|- Learn about [Hardware](hardware.md) specifications and pin mappings
#HV|- Configure motion detection and SD storage
#XX|- Review the [Architecture](architecture.md) for system details
#PX|- Check the [Troubleshooting](troubleshooting.md) guide for solutions
#ZZ|- Explore the [API](api.md) for programmatic control