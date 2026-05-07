> 🌐 [中文文档](../zh/hardware.md)

# Hardware

This document provides detailed hardware specifications, pin mappings, and technical information for the AI_Thinker ESP32-CAM firmware.

## AI_Thinker ESP32-CAM Board

The AI_Thinker ESP32-CAM is a development board featuring an ESP32 chip with camera interface, designed specifically for camera applications.

### Board Specifications

| Parameter | Specification |
|-----------|---------------|
| **Main Controller** | ESP32 (original, not ESP32-S3) |
| **CPU** | Dual-core Tensilica Xtensa LX6 @ 240 MHz |
| **Flash Memory** | 4MB (4 MB flash) |
| **PSRAM** | 4MB (4 MB PSRAM, Octal SPI) |
| **WiFi** | 2.4 GHz (802.11 b/g/n) |
| **Bluetooth** | v4.2 BR/EDR |
| **GPIO** | 33 GPIO pins |
| **ADC** | 12-bit, 18 channels |
| **DAC** | 2 channels |
| **Camera Interface** | 8-bit parallel + control signals |
| **USB** | USB-C for power and programming |

### Camera Interface

The board includes an OV2640 camera interface with the following pin configuration:

| Pin | GPIO | Function | Description |
|-----|------|----------|-------------|
| XCLK | 0 | Master Clock | 20 MHz master clock for camera |
| SIOD | 26 | I2C Data | Camera I2C data line (SDA) |
| SIOC | 27 | I2C Clock | Camera I2C clock line (SCL) |
| D0 | 5 | Parallel Data | Camera parallel data bit 0 |
| D1 | 18 | Parallel Data | Camera parallel data bit 1 |
| D2 | 19 | Parallel Data | Camera parallel data bit 2 |
| D3 | 21 | Parallel Data | Camera parallel data bit 3 |
| D4 | 36 | Parallel Data | Camera parallel data bit 4 (input only) |
| D5 | 39 | Parallel Data | Camera parallel data bit 5 (input only) |
| D6 | 34 | Parallel Data | Camera parallel data bit 6 (input only) |
| D7 | 35 | Parallel Data | Camera parallel data bit 7 (input only) |
| VSYNC | 25 | Vertical Sync | Vertical synchronization signal |
| HREF | 23 | Horizontal Reference | Horizontal reference signal |
| PCLK | 22 | Pixel Clock | Pixel clock output from camera |
| PWDN | 32 | Power Down | Camera power control (active HIGH) |
| RESET | -1 | Reset | Camera reset (not connected) |

### SD Card Interface

The board supports SD card storage through SPI interface:

| Pin | GPIO | Function | Description |
|-----|------|----------|-------------|
| SD_CS | 13 | Chip Select | SD card SPI chip select |
| SD_CLK | 14 | SPI Clock | SD card SPI clock line (SHARED with camera) |
| SD_MOSI | 15 | SPI MOSI | SD card SPI data out |
| SD_MISO | 2 | SPI MISO | SD card SPI data in |

### GPIO Constraints and Sharing

#### Critical GPIO14 Constraint

**GPIO14 (SD_CLK)** is shared between the camera and SD card interface and requires careful timing management:

- **Camera Usage**: When camera is active (capturing/initializing), GPIO14 serves as SD_CLK
- **SD Card Usage**: When SD card is being accessed, GPIO14 serves as camera XCLK
- **Time-Multiplexing**: The firmware handles this by:
  1. Deinitializing SD card before camera operations
  2. Reinitializing SD card after camera operations complete
  - **Impact**: This means camera and SD card cannot be used simultaneously
  - **Consequence**: No AVI video recording (requires simultaneous camera and SD access)

#### Input-Only GPIO Pins

The following GPIO pins are input-only and cannot be used as outputs:

| GPIO | Pin Number | Typical Use |
|------|------------|-------------|
| 34 | GPIO34 | Camera D4 (parallel data) |
| 35 | GPIO35 | Camera D6 (parallel data) |
| 36 | GPIO36 | Camera D4 (parallel data) |
| 39 | GPIO39 | Camera D5 (parallel data) |

### LED Indicators

The board includes two LED indicators:

| LED | GPIO | Type | Function |
|-----|------|------|----------|
| Status LED | 33 | Output (Active LOW) | System status indicator |
| Flash LED | 4 | PWM | Camera flash control |

#### Status LED States

| State | Pattern | Description |
|-------|---------|-------------|
| STARTING | Solid | System booting up |
| WIFI_CONNECTING | Blinking | Connecting to WiFi |
| RUNNING | Solid | System operational |
| ERROR | Fast Blink | Error condition |
| AP_MODE | Slow Blink | AP mode active |

### Power Requirements

| Parameter | Specification |
|-----------|---------------|
| **Input Voltage** | 5V DC |
| **Operating Voltage** | 3.3V |
| **Max Current** | 500mA (peak during camera operation) |
| **Recommended Power** | 5V/1A USB adapter |

### Boot Button

| Pin | Function | Description |
|-----|----------|-------------|
| GPIO0 | Boot Button | Used for: |
| | | - Factory reset (5-second press) |
| | | - Bootloader entry (when pressed during reset) |

## OV2640 Camera Module

The firmware supports the OV2640 camera sensor, a 2-megapixel camera module.

### OV2640 Specifications

| Parameter | Specification |
|-----------|---------------|
| **Resolution** | 2 megapixels (1600×1200) |
| **Color Depth** | RGB/YUV |
| **Output Formats** | JPEG, RAW RGB, YUV420 |
| **Lens Type** | Fixed focus |
| **Sensor Size** | 1/6 inch |
| **Frame Rates** | Up to 30 FPS at lower resolutions |

### Supported Resolutions

| Resolution | Code | Dimensions | Recommended FPS |
|------------|------|------------|----------------|
| VGA | 0 | 640×480 | 15-30 |
| SVGA | 1 | 800×600 | 10-25 |
| XGA | 2 | 1024×768 | 8-20 |
| UXGA | 3 | 1600×1200 | 3-10 |

### Memory Requirements

The camera requires PSRAM for frame buffering:

| Resolution | Frame Size | Buffer Count | Total Memory |
|------------|------------|-------------|--------------|
| VGA (640×480) | ~300KB | 2 | ~600KB |
| SVGA (800×600) | ~450KB | 2 | ~900KB |
| XGA (1024×768) | ~700KB | 2 | ~1.4MB |
| UXGA (1600×1200) | ~1.2MB | 2 | ~2.4MB |

**Total PSRAM Available**: 4MB  
**PSRAM Used for Camera**: Up to 2.4MB for UXGA resolution  
**Available for Other Use**: 1.6MB minimum

## Flash Memory Layout

The 4MB flash memory is organized as follows:

| Partition | Type | Offset | Size | Description |
|-----------|------|--------|------|-------------|
| **nvs** | data/nvs | 0x9000 | 24KB | NVS key-value storage |
| **phy_init** | data/phy | 0xf000 | 4KB | PHY initialization data |
| **factory** | app/factory | 0x10000 | ~2.5MB | Main application firmware |
| **spiffs** | data/spiffs | 0x260000 | ~1.2MB | Web UI and photos metadata |

### Firmware Size Considerations

| Component | Size | Notes |
|-----------|------|-------|
| Application | ~1.97MB | Core firmware (PSRAM camera support) |
| Bootloader | ~92KB | ESP-IDF standard bootloader |
| Partition Table | ~0.4KB | Flash layout definition |
| NVS Storage | 24KB | Configuration storage |
| SPIFFS | ~1.2MB | Web UI, cached photos metadata |
| **Total Used** | **~3.3MB** | **13MB available** |
| **Free Space** | **~0.7MB** | **Available for expansion** |

## Power Management

### Power Consumption Modes

| Mode | Current | Description |
|------|---------|-------------|
| Deep Sleep | ~10µA | WiFi off, minimal power |
| Light Sleep | ~150µA | WiFi sleep mode |
| Idle | ~20mA | System idle, WiFi connected |
| Camera Active | ~250mA | Camera operation |
| Streaming | ~300mA | MJPEG streaming |

### Voltage Monitoring

The firmware includes voltage monitoring capabilities:
- Operating voltage range: 3.0V - 3.6V
- Brownout detection enabled
- Under-voltage reset protection

## Thermal Considerations

| Parameter | Specification |
|-----------|---------------|
| **Operating Temperature** | -40°C to +85°C |
| **Storage Temperature** | -40°C to +125°C |
| **Thermal Throttling** | Starts at 85°C CPU temperature |
| **Temperature Monitoring** | Available via `/metrics` endpoint |

## Pin Mapping Summary

### Camera Pins (Fixed)
```
XCLK  → GPIO0
SIOD  → GPIO26
SIOC  → GPIO27
D0-D3 → GPIO5,18,19,21
D4-D7 → GPIO36,39,34,35
VSYNC → GPIO25
HREF  → GPIO23
PCLK  → GPIO22
PWDN  → GPIO32
RESET → NC (not connected)
```

### SD Card Pins (SPI Interface)
```
CS   → GPIO13
CLK  → GPIO14 (SHARED with camera XCLK)
MOSI → GPIO15
MISO → GPIO2
```

### Control and Status Pins
```
Status LED → GPIO33 (Active LOW)
Flash LED  → GPIO4 (PWM)
Boot Button → GPIO0
```

### Important Notes
1. **GPIO14 is shared** between camera and SD card - time-multiplexed firmware handles this
2. **No simultaneous camera+SD operations** - prevents AVI recording
3. **PSRAM required** for camera operation - cannot be disabled
4. **Input-only GPIOs**: 34,35,36,39 cannot be used as outputs
5. **UART0** is used for serial communication (GPIO1=TX, GPIO3=RX)