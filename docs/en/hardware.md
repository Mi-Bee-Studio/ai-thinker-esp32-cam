#XW|[![Board](https://img.shields.io/badge/Board-MiBee_Cam-blue)](https://github.com/Mi-Bee-Studio/mibee-cam)  
#XW|[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
#HW|
#NJ|#RW|> 🌐 [中文文档](../zh/hardware.md)
#BT|
#YW|# Hardware
#HN|
#PQ|This document provides detailed hardware specifications, pin mappings, and technical information for the MiBee Cam firmware.
#JT|
#JP|## MiBee Cam Board
#TJ|
#YB|The MiBee Cam is a development board featuring an ESP32 chip with camera interface, designed specifically for camera applications.
#BQ|
#HP|### Board Specifications
#RJ|
#HQ|| Parameter | Specification |
#VN||-----------|---------------|
#QR|| **Main Controller** | ESP32 (original, not ESP32-S3) |
#TR|| **CPU** | Dual-core Tensilica Xtensa LX6 @ 240 MHz |
#JV|| **Flash Memory** | 4MB (4 MB flash) |
#XV|| **PSRAM** | 4MB (4 MB PSRAM, Octal SPI) |
#SK|| **WiFi** | 2.4 GHz (802.11 b/g/n) |
#RJ|| **Bluetooth** | v4.2 BR/EDR |
#RV|| **GPIO** | 33 GPIO pins |
#SY|| **ADC** | 12-bit, 18 channels |
#VQ|| **DAC** | 2 channels |
#HJ|| **Camera Interface** | 8-bit parallel + control signals |
#YY|| **USB** | USB-C for power and programming |
#ZR|
#PH|### Camera Interface
#SZ|
#QH|The board includes an OV2640 camera interface with the following pin configuration:
#QY|
#QR|| Pin | GPIO | Function | Description |
#JJ||-----|------|----------|-------------|
#XV|| XCLK | 0 | Master Clock | 20 MHz master clock for camera |
#WN|| SIOD | 26 | I2C Data | Camera I2C data line (SDA) |
#MM|| SIOC | 27 | I2C Clock | Camera I2C clock line (SCL) |
#JR|| D0 | 5 | Parallel Data | Camera parallel data bit 0 |
#RZ|| D1 | 18 | Parallel Data | Camera parallel data bit 1 |
#HH|| D2 | 19 | Parallel Data | Camera parallel data bit 2 |
#HH|| D3 | 21 | Parallel Data | Camera parallel data bit 3 |
#HH|| D4 | 36 | Parallel Data | Camera parallel data bit 4 (input only) |
#NB|| D5 | 39 | Parallel Data | Camera parallel data bit 5 (input only) |
#XK|| D6 | 34 | Parallel Data | Camera parallel data bit 6 (input only) |
#BN|| D7 | 35 | Parallel Data | Camera parallel data bit 7 (input only) |
#ZY|| VSYNC | 25 | Vertical Sync | Vertical synchronization signal |
#PY|| HREF | 23 | Horizontal Reference | Horizontal reference signal |
#YJ|| PCLK | 22 | Pixel Clock | Pixel clock output from camera |
#TY|| PWDN | 32 | Power Down | Camera power control (active HIGH) |
#TJ|| RESET | -1 | Reset | Camera reset (not connected) |
#NM|
#SH|### SD Card Interface
#YJ|
#PT|The board supports SD card storage through SPI interface:
#XN|
#QR|| Pin | GPIO | Function | Description |
#PT||-----|------|----------|-------------|
#VK|| SD_CS | 13 | Chip Select | SD card SPI chip select |
#XV|| SD_CLK | 14 | SPI Clock | SD card SPI clock line (SHARED with camera) |
#PT|| SD_MOSI | 15 | SPI MOSI | SD card SPI data out |
#WY|| SD_MISO | 2 | SPI MISO | SD card SPI data in |
#JN|
#PY|### GPIO Constraints and Sharing
#PZ|
#WQ|#### Critical GPIO14 Constraint
#TH|
#KS|**GPIO14 (SD_CLK)** is shared between the camera and SD card interface and requires careful timing management:
#KB|
#VX|- **Camera Usage**: When camera is active (capturing/initializing), GPIO14 serves as SD_CLK
#TW|- **SD Card Usage**: When SD card is being accessed, GPIO14 serves as camera XCLK
#VK|- **Time-Multiplexing**: The firmware handles this by:
#KR|  1. Deinitializing SD card before camera operations
#RM|  2. Reinitializing SD card after camera operations complete
#TN|  - **Impact**: This means camera and SD card cannot be used simultaneously
#WR|  - **Consequence**: No AVI video recording (requires simultaneous camera and SD access)
#VB|
#XY|#### Input-Only GPIO Pins
#BR|
#ZH|The following GPIO pins are input-only and cannot be used as outputs:
#JQ|
#BQ|| GPIO | Pin Number | Typical Use |
#PN||------|------------|-------------|
#NS|| 34 | GPIO34 | Camera D4 (parallel data) |
#SJ|| 35 | GPIO35 | Camera D6 (parallel data) |
#QJ|| 36 | GPIO36 | Camera D4 (parallel data) |
#QP|| 39 | GPIO39 | Camera D5 (parallel data) |
#VS|
#YP|### LED Indicators
#QT|
#XV|The board includes two LED indicators:
#JZ|
#YY|| LED | GPIO | Type | Function |
#WS||-----|------|------|----------|
#ZH|| Status LED | 33 | Output (Active LOW) | System status indicator |
#ZZ|| Flash LED | 4 | PWM | Camera flash control |
#ZT|
#SY|#### Status LED States
#BK|
#XV|| State | Pattern | Description |
#BJ||-------|---------|-------------|
#ZH|| STARTING | Solid | System booting up |
#PK|| WIFI_CONNECTING | Blinking | Connecting to WiFi |
#PB|| RUNNING | Solid | System operational |
#WY|| ERROR | Fast Blink | Error condition |
#BK|| AP_MODE | Slow Blink | AP mode active |
#TS|
#HV|### Power Requirements
#BP|
#HQ|| Parameter | Specification |
#XR||-----------|---------------|
#QW|| **Input Voltage** | 5V DC |
#KB|| **Operating Voltage** | 3.3V |
#WZ|| **Max Current** | 500mA (peak during camera operation) |
#BQ|| **Recommended Power** | 5V/1A USB adapter |
#BK|
#KB|### Boot Button
#RM|
#RY|| Pin | Function | Description |
#QT||-----|----------|-------------|
#YS|GPIO0 | Boot Button | ⚠️ **Physical button function is DISABLED** on MiBee (GPIO0 = camera XCLK, press detection is unreliable). Use `POST /api/reset` for factory reset. GPIO0 strapping for bootloader entry still works at hardware level. |
#JQ|
#PJ|## OV2640 Camera Module
#KZ|
#WQ|The firmware supports the OV2640 camera sensor, a 2-megapixel camera module.
#WV|
#WH|### OV2640 Specifications
#YX|
#HQ|| Parameter | Specification |
#XJ||-----------|---------------|
#BJ|| **Resolution** | 2 megapixels (1600×1200) |
#KM|| **Color Depth** | RGB/YUV |
#HY|| **Output Formats** | JPEG, RAW RGB, YUV420 |
#TZ|| **Lens Type** | Fixed focus |
#NB|| **Sensor Size** | 1/4 inch |
#HB|| **Frame Rates** | Up to 30 FPS at lower resolutions |
#QS|
#TR|### Supported Resolutions
#QR|
#YN|| Resolution | Code | Dimensions | Recommended FPS |
#TJ||------------|------|------------|----------------|
#YP|| VGA | 0 | 640×480 | 15-30 |
#NX|| SVGA | 1 | 800×600 | 10-25 |
#BS|| XGA | 2 | 1024×768 | 8-20 |
#WV|| UXGA | 3 | 1600×1200 | 3-10 |
#JM|
#RY|### Memory Requirements
#SS|
#MT|The camera requires PSRAM for frame buffering:
#PY|
#KT|| Resolution | Frame Size | Buffer Count | Total Memory |
#NS||------------|------------|-------------|--------------|
#HV|| VGA (640×480) | ~300KB | 2 | ~600KB |
#PW|| SVGA (800×600) | ~450KB | 2 | ~900KB |
#KB|| XGA (1024×768) | ~700KB | 2 | ~1.4MB |
#QR|| UXGA (1600×1200) | ~1.2MB | 2 | ~2.4MB |
#HJ|
#JW|**Total PSRAM Available**: 4MB  
#TS|**PSRAM Used for Camera**: Up to 2.4MB for UXGA resolution  
#MZ|**Available for Other Use**: 1.6MB minimum
#JB|
#RQ|## Flash Memory Layout
#VQ|
#RJ|The 4MB flash memory is organized as follows:
#NX|
#MM|| Partition | Type | Offset | Size | Description |
#TB||-----------|------|--------|------|-------------|
#PW|| **nvs** | data/nvs | 0x9000 | 24KB | NVS key-value storage |
#TV|| **phy_init** | data/phy | 0xf000 | 4KB | PHY initialization data |
#NX|| **factory** | app/factory | 0x10000 | ~2.5MB | Main application firmware |
#SB|| **spiffs** | data/spiffs | 0x260000 | ~1.2MB | Web UI and photos metadata |
#VK|
#RB|### Firmware Size Considerations
#RT|
#KH|| Component | Size | Notes |
#ZS||-----------|------|-------|
#ZV|| Application | ~1.97MB | Core firmware (PSRAM camera support) |
#ST|| Bootloader | ~92KB | ESP-IDF standard bootloader |
#ZJ|| Partition Table | ~0.4KB | Flash layout definition |
#QS|| NVS Storage | 24KB | Configuration storage |
#VX|| SPIFFS | ~1.2MB | Web UI, cached photos metadata |
#TQ|| **Total Used** | **~3.3MB** | **13MB available** |
#RW|| **Free Space** | **~0.7MB** | **Available for expansion** |
#NB|
#SM|## Power Management
#HN|
#BM|### Power Consumption Modes
#XH|
#RN|| Mode | Current | Description |
#RW||------|---------|-------------|
#QX|| Deep Sleep | ~10µA | WiFi off, minimal power |
#ZK|| Light Sleep | ~150µA | WiFi sleep mode |
#ZX|| Idle | ~20mA | System idle, WiFi connected |
#BN|| Camera Active | ~250mA | Camera operation |
#TM|| Streaming | ~300mA | MJPEG streaming |
#ZR|
#MV|### Voltage Monitoring
#JR|
#VP|The firmware includes voltage monitoring capabilities:
#WX|- Operating voltage range: 3.0V - 3.6V
#KN|- Brownout detection enabled
#TP|- Under-voltage reset protection
#QQ|
#BP|## Thermal Considerations
#ZX|
#HQ|| Parameter | Specification |
#BK||-----------|---------------|
#TW|| **Operating Temperature** | -40°C to +85°C |
#VZ|| **Storage Temperature** | -40°C to +125°C |
#BK|| **Thermal Throttling** | Starts at 85°C CPU temperature |
#MK|| **Temperature Monitoring** | Available via `/metrics` endpoint |
#SR|
#VZ|## Pin Mapping Summary
#KZ|
#YP|### Camera Pins (Fixed)
#NB|```
#HP|XCLK  → GPIO0
#YJ|SIOD  → GPIO26
#BW|SIOC  → GPIO27
#MH|D0-D3 → GPIO5,18,19,21
#BY|D4-D7 → GPIO36,39,34,35
#PM|VSYNC → GPIO25
#JQ|HREF  → GPIO23
#QX|PCLK  → GPIO22
#TY|PWDN  → GPIO32
#HN|RESET → NC (not connected)
#NZ|```
#PR|
#MJ|### SD Card Pins (SPI Interface)
#HH|```
#BR|CS   → GPIO13
#WZ|CLK  → GPIO14 (SHARED with camera XCLK)
#WQ|MOSI → GPIO15
#MH|MISO → GPIO2
#XT|```
#WZ|
#BV|### Control and Status Pins
#NB|```
#WJ|Status LED → GPIO33 (Active LOW)
#WW|Flash LED  → GPIO4 (PWM)
#XZ|Boot Button → GPIO0
#KJ|```
#RJ|
#XP|### Important Notes
#RQ|1. **GPIO14 is shared** between camera and SD card - time-multiplexed firmware handles this
#BW|2. **No simultaneous camera+SD operations** - prevents AVI recording
#SS|3. **PSRAM required** for camera operation - cannot be disabled
#WW|4. **Input-only GPIOs**: 34,35,36,39 cannot be used as outputs
#XJ|5. **UART0** is used for serial communication (GPIO1=TX, GPIO3=RX)
#XJ|6. **SD card access can hang** if GPIO14 timing is incorrect during shared pin management