#BM|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)  
#XW|[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
#HW|
#VN|#WM|> 🌐 [中文文档](../zh/architecture.md)
#BT|
#WV|# Architecture
#HN|
#JH|This document provides a comprehensive overview of the MiBee Cam firmware architecture, including module organization, boot sequence, data flow, and system design.
#JT|
#BZ|## System Overview
#TJ|
#ZR|The MiBee Cam firmware is a real-time embedded system designed for camera applications with WiFi connectivity and storage capabilities. The architecture follows a modular design with clear separation of concerns.
#BQ|
#YY|### Key Characteristics
#RJ|
#RT|- **Real-time Processing**: Concurrent camera capture and streaming
#WT|- **Resource-Constrained**: Optimized for 4MB flash + 4MB PSRAM
#JV|- **Networked**: WiFi connectivity with AP/STA dual-mode
#WH|#RR|- **Modular**: 14 separate modules with defined interfaces
#PP|- **Event-Driven**: Asynchronous operation with state machines
#RJ|
#XX|## Module Architecture
#NV|
#WS|The firmware consists of 14 interconnected modules, each with specific responsibilities:
#XW|
#HH|```
#YM|┌─────────────────────────────────────────────────────────────┐
#KB|│                    Application Layer                          │
#MM|├─────────────────────────────────────────────────────────────┤
#ZT|│  Web Server    │ MJPEG Streamer │ Motion Detect            │
#MY|│  (HTTP + API)  │   (Streaming)  │   (Capture)            │
#JN|├─────────────────────────────────────────────────────────────┤
#BQ|│                    Hardware Interface Layer                  │
#QT|├─────────────────────────────────────────────────────────────┤
#YT|│  Camera Driver │  Storage Mgr  │   WiFi Mgr   │  Time Sync  │
#QM|│    (OV2640)     │  (SD Card)    │ (AP/STA)     │   (NTP)     │
#SK|├─────────────────────────────────────────────────────────────┤
#KT|│                    System Core Layer                         │
#XZ|├─────────────────────────────────────────────────────────────┤
#TT|│  Config Manager │ Health Monitor │ Status LED │  Timelapse  │
#TN|│    (NVS)        │   (Metrics)   │   (GPIO)   │   (Control) │
#JQ|├─────────────────────────────────────────────────────────────┤
#WW|│                  ESP-IDF Framework                           │
#YJ|├─────────────────────────────────────────────────────────────┤
#ST|│  FreeRTOS      │ LWIP          │ SPIFFS      │ NVS         │
#HJ|│  (Tasks/RTOS)  │  (Networking) │ (Storage)   │ (Config)    │
#HT|└─────────────────────────────────────────────────────────────┘
#RK|```
#BN|
#ZM|## Detailed Module Descriptions
#PZ|
#VX|### 1. Main Module (`main.c`)
#TB|**Responsibility**: System orchestration and boot sequence
#WN|- **Functions**: 16-step initialization, task coordination
#PP|- **Key Files**: `main.c`
#ZP|- **Dependencies**: All other modules
#JJ|- **Memory**: Stack size 8KB
#RQ|- **Priority**: High (task creation and coordination)
#QH|
#XN|### 2. Camera Driver (`camera_driver.c/h`)
#HQ|**Responsibility**: OV2640 camera control and frame capture
#TZ|- **Functions**: Initialization, JPEG capture, resolution control
#MJ|- **Memory**: PSRAM for frame buffers
#TM|- **Critical**: Deferred after WiFi STA (DMA freeze workaround esp32-camera#620)
#VN|- **GPIO**: Uses camera-specific pins (XCLK=0, etc.)
#JQ|
#VJ|### 3. WiFi Manager (`wifi_manager.c/h`)
#RN|**Responsibility**: WiFi connectivity management
#PQ|- **Functions**: AP/STA mode, auto-reconnect, callback registration, serial AT config
#NY|- **Network**: Handles IP assignment and connectivity
#MM|- **Mode**: STA for operation, AP for fallback, configurable AP fallback behavior
#JP|- **Event**: State changes trigger other modules
#HV|
#YM|### 4. Config Manager (`config_manager.c/h`)
#PP|**Responsibility**: Configuration persistence and management
#ZT|- **Storage**: NVS key-value storage
#ZN|- **Migration**: Auto-migration between versions (V1→V9)
#QJ|- **Defaults**: Sensible defaults for first boot
#HX|- **Validation**: Configuration integrity checking
#KB|
#HM|### 5. Web Server (`web_server.c/h`)
#YV|**Responsibility**: HTTP server and REST API
#SS|- **Ports**: TCP port 80
#PN|- **Endpoints**: Status, config, capture, stream, metrics, files, timelapse, flash, recording
#BZ|- **Static Files**: Served from SPIFFS partition
#QM|- **Authentication**: Optional password protection for write operations
#XB|
#SP|### 6. MJPEG Streamer (`mjpeg_streamer.c/h`)
#NR|**Responsibility**: Real-time video streaming
#VV|- **Protocol**: multipart/x-mixed-replace
#YS|- **Frame Rate**: Target 15 FPS with throttling
#WV|- **Clients**: Max 2 concurrent streams
#HW|- **Buffering**: Frame-based with double buffering
#MS|
#SQ|### 7. Storage Manager (`storage_manager.c/h`)
#SZ|**Responsibility**: SD card photo storage
#RR|- **Interface**: SPI (SDSPI)
#JY|- **File System**: FAT32 via FatFs
#YX|- **Organization**: Date-based directories
#HK|- **Cleanup**: Circular when free space < 20%
#ZS|
#MV|### 8. Motion Detection (`motion_detect.c/h`)
#BR|**Responsibility**: Motion detection, brightness sensing, and auto-flash
#NR|- **Algorithm**: JPEG frame-difference with configurable threshold
#SS|- **Brightness**: Grayscale pixel probing (primary) + JPEG size fallback
#KW|- **Auto Flash**: LEDC PWM on GPIO4 (~80% duty, safe for MiBee hardware)
#MH|- **Trigger**: Save photo to SD card
#WX|- **Configurable**: Threshold, cooldown, flash_threshold (brightness %)
#BP|
#KZ|### 9. Status LED (`status_led.c/h`)
#ZJ|**Responsibility**: System status indication
#PX|- **GPIO**: GPIO33 (active LOW)
#KB|- **Patterns**: Different blink patterns for each state
#YB|- **States**: Starting, WiFi connecting, running, error, AP mode
#PV|
#QX|### 10. Time Sync (`time_sync.c/h`)
#SP|**Responsibility**: Time and date management
#PX|- **Protocol**: SNTP with NTP server
#WP|- **Timezone**: Configurable POSIX timezone
#VV|- **Accuracy**: Synchronized within 1 second
#QS|- **Use Case**: Timestamps for photos and logs
#JQ|
#BH|### 11. Health Monitor (`health_monitor.c/h`)
#JW|**Responsibility**: System health tracking
#PN|- **Metrics**: Heap, PSRAM, task stack usage
#XK|- **Output**: Prometheus-compatible /metrics endpoint
#MQ|- **Interval**: 60-second monitoring
#MK|- **Logging**: Periodic health reports
#HP|
#YZ|### 12. Video Recorder (`video_recorder.c/h`)
#TJ|**Responsibility**: AVI video recording with multiple modes
#NY|- **Modes**: Continuous, timelapse, dynamic
#HR|- **Storage**: Segmented AVI files on SD card
#SN|- **Config**: Record mode, segment duration
#QX|
#TZ|### 13. Timelapse Module (`timelapse.c/h`)
#VZ|**Responsibility**: Periodic and motion-triggered capture control
#BW|- **Features**: Configurable intervals, burst capture, motion integration
#XS|- **Storage**: SD card with timestamped filenames
#MB|- **API**: Start/stop/status endpoints
#HP|- **Integration**: Works with camera driver and storage manager
#ZJ|
#XM|### 14. Flash LED (`flash_led.c/h`)
#PZ|**Responsibility**: Flash LED control and state tracking
#KP|- **GPIO**: GPIO4 with PWM brightness control
#ZT|- **Modes**: Manual toggle, automatic brightness-based activation
#KX|- **API**: Get/set status endpoints
#QH|- **Integration**: Motion detection triggers auto-activation
#WR|
#JR|### 15. Serial Config (`serial_config.c/h`)
#VN|**Responsibility**: Serial AT command configuration interface
#BB|- **Commands**: AT+WIFI configuration via serial port
#TJ|- **Use Case**: Headless configuration without network access
#HT|- **Protocol**: Custom AT command parser
#KZ|
#QX|### 16. Common (`common.h`)
#VM|**Responsibility**: Shared types, pin definitions, config struct
#TJ|- **Pin Mappings**: CAM_PIN_*, SD_PIN_*
#WB|- **Config Struct**: cam_config_t (V9)
#TN|- **Enums**: wifi_state_t, led_state_t
#NB|
#KS|## Boot Sequence
#HN|
#QW|#WP|The firmware follows a carefully ordered 16-step boot sequence to avoid hardware conflicts and ensure proper initialization:
#XH|
#RK|```mermaid
#MN|graph TD
#YT|    A[1. NVS Init] --> B[2. Config Load]
#VQ|    B --> C[3. LED Init]
#JS|    C --> D[4. SPIFFS Mount]
#YJ|    D --> E[5. WiFi Init]
#ZP|    E --> F[6. WiFi Callback]
#WQ|    F --> G[7. Health Monitor]
#KS|    G --> H[8. WiFi Mode Selection]
#KR|    H --> I{STA Mode?}
#VP|    I -->|Yes| J[9. Camera Init (after WiFi STA, DMA freeze workaround esp32-camera#620)]
#VM|    I -->|No| K[9. Camera Init (after WiFi STA, DMA freeze workaround esp32-camera#620)]
#TV|    J --> L[10. MJPEG Init]
#KV|    K --> L[10. MJPEG Init]
#MV|    L --> M[11. Web Server Start]
#BJ|    M --> N[12. Motion Detect]
#XP|    N --> O[13. SD Card Init (after camera, GPIO14 sharing)]
#YH|    O --> P[14. Video Recorder Init (AVI)]
#TK|    P --> Q[15. Timelapse Init]
#TH|    Q --> R[16. Flash LED Init]
#ZX|```
#KP|
#BH|### Boot Step Details
#SR|
#NN|**Steps 1-4: Foundation**
#YW|- NVS initialization for configuration
#BS|- LED setup for visual feedback
#JJ|- SPIFFS mount for web UI
#BB|- WiFi subsystem setup
#JM|
#HH|**Steps 5-8: Hardware Setup**
#WT|- Health monitoring start
#PK|- WiFi mode selection (STA if configured, AP otherwise with fallback)
#VT|
#WY|**Steps 9-12: Network Services**
#XM|- MJPEG streaming service
#PR|- Web server start (port 80)
#QX|- Motion detection (STA mode only)
#WJ|
#BH|**Steps 13-16: Application Services**
#BP|- SD card initialization (after camera, GPIO14 sharing)
#JQ|- Video recorder startup (AVI recording)
#KX|- Timelapse control initialization
#HM|- Flash LED control initialization
#ZY|
#ZB|## Data Flow Architecture
#HS|
#TY|### Camera Data Flow
#YS|```
#RV|OV2640 Camera → Camera Driver → Frame Buffer
#RM|    ↓
#KK|JPEG Encoding → MJPEG Streamer → Network Client
#SY|    ↓
#YT|Storage Manager → SD Card (Photos)
#HW|    ↓
#RJ|    ↓
#BR|Motion Detection → SD Card Storage + Flash LED
#RZ|```
#TM|
#WR|### Network Data Flow
#JS|```
#XT|WiFi Interface → LWIP Stack → Web Server
#XQ|    ↓
#RM|HTTP Request → REST API → Module Handler
#QH|    ↓
#YS|Response → Client Browser/Mobile Device
#WB|```
#ZQ|
#ZQ|### Configuration Data Flow
#RS|```
#PT|Web UI → HTTP POST → Web Server → Config Manager
#RX|    ↓
#VZ|NVS Storage → Config Manager → Runtime Modules
#YR|    ↓
#WY|Runtime Changes → Config Manager → NVS Update
#KV|```
#PX|
#WP|## Memory Architecture
#YZ|
#MJ|### Flash Memory (4MB)
#NK|```
#PS|0x00000-0x0FFFF:    Bootloader (92KB)
#VM|0x10000-0x25FFFF:   Application (~2.5MB)
#YH|0x260000-0x3DFFFF:  SPIFFS (~1.2MB)
#ZB|0x3E0000-0x3FFFFF:  NVS (24KB)
#MQ|```
#QY|
#WH|### PSRAM (4MB)
#TH|```
#VM|0x000000-0x1EFFFF:  Camera Frame Buffers (2MB)
#WJ|0x1F0000-0x3FFFFF:  Available for Other Use (2MB)
#QB|```
#BX|
#SR|### Stack Memory
#TZ|- **Main Task**: 8KB (boot sequence)
#KY|- **WiFi Task**: 8KB (network operations)
#HH|- **Camera Task**: 8KB (capture operations)
#RX|- **Background Tasks**: 4KB each (monitoring, etc.)
#BV|
#SV|## Concurrency Model
#VK|
#TV|### Task Overview
#MB|```c
#HT|// Main Task (main.c)
#NB|- Boot sequence orchestration
#RR|- Module initialization
#WY|- System coordination
#NX|
#PK|// WiFi Task (wifi_manager.c)
#WZ|- Network connectivity management
#WB|- Background operations
#VX|
#JB|// Camera Task (camera_driver.c)
#VB|- Camera capture operations
#VM|- Frame processing
#PV|
#VW|// HTTP Server Task (web_server.c)
#NT|- HTTP request handling
#QP|- Web UI serving
#NW|
#QX|// Background Tasks
#ZV|- Health monitoring (60s interval)
#NV|- Motion detection (continuous)
#JS|
#RZ|- SD card monitoring (hot-plug detection)
#XB|- Timelapse control (periodic)
#HV|- Flash LED control (PWM)
#ZJ|- Video recording (segmented)
#KK|```
#BJ|
#TV|### Synchronization Primitives
#JW|
#WT|#### Mutexes
#SX|- **Camera Access**: Protects frame buffer access
#VN|- **SD Card Access**: Prevents GPIO14 conflicts
#BQ|- **Config Access**: Thread-safe configuration access
#MV|
#VN|#### Semaphores
#MB|- **Stream Clients**: Limits concurrent streams (max 2)
#SH|- **Upload Queue**: Synchronizes upload processing
#PW|- **WiFi Events**: Synchronizes state changes
#TT|
#YS|#### FreeRTOS Features
#RW|- **Queues**: Intertask communication
#XV|- **Timers**: Periodic task execution
#YR|- **Event Groups**: Async event handling
#NN|
#QW|## Error Handling and Recovery
#XN|
#XQ|### Error Categories
#HZ|1. **Hardware Errors**: Camera init failure, SD card errors
#SX|2. **Network Errors**: WiFi disconnection, upload failures
#VM|3. **Resource Errors**: Memory exhaustion, stack overflow
#PR|4. **Configuration Errors**: Invalid settings, NVS corruption
#TY|
#ZN|### Recovery Strategies
#MN|- **WiFi**: Auto-reconnect with exponential backoff
#JM|- **Camera**: Soft reset and reinitialization
#PQ|- **SD Card**: Remount and retry operations
#VV|- **System**: Watchdog timer for task recovery
#XK|
#KB|### Error Indicators
#XH|- **LED Patterns**: Different blink sequences for error types
#HY|- **Serial Logging**: Detailed error messages with timestamps
#BJ|- **Web Dashboard**: Error status indicators
#VH|- **API Responses**: HTTP error codes with details
#XJ|
#TP|## Performance Characteristics
#SQ|
#JV|### CPU Usage
#ZM|- **Idle**: ~2% CPU utilization
#SW|- **Streaming**: ~40% CPU at 15 FPS VGA
#TV|- **Motion Detection**: ~5% CPU continuous
#MN|- **Recording**: ~25% CPU during AVI encoding
#JS|
#BB|### Memory Usage
#WK|- **Free Heap**: Minimum 20KB required
#NJ|- **PSRAM**: Camera uses 600KB-2.4MB depending on resolution
#BT|- **Stack**: Main task 8KB, other tasks 4KB-8KB
#HH|- **Static**: ~50KB for compiled code
#SW|
#MK|### Network Performance
#RK|- **MJPEG Stream**: 15 FPS VGA (~500KB/s)
#HH|- **HTTP API**: <100ms response time
#QR|- **WiFi Throughput**: ~1-2 Mbps sustained
#SX|- **Recording**: ~1 Mbps continuous AVI output
#RR|
#XB|## GPIO and Hardware Constraints
#ZS|
#ZJ|### Critical Constraints
#XM|1. **GPIO14 Sharing**: Camera XCLK / SD Card CLK requires time-multiplexing
#SR|2. **I2C Bus Conflict**: Camera and WiFi cannot use I2C simultaneously
#HN|3. **Input-Only GPIOs**: 34,35,36,39 cannot be used as outputs
#WQ|4. **LED Polarity**: GPIO33 is active LOW, GPIO4 flash LED is active HIGH
#ZY|
#QX|### Hardware Resource Management
#HP|- **PSRAM**: Dedicated to camera frame buffers
#TP|- **Flash**: Carefully partitioned for firmware + storage
#MY|- **SPI Bus**: Shared between camera and SD card (time-multiplexed)
#RN|- **Power Management**: Dynamic clock scaling for power efficiency
#HR|
#PZ|## Extensibility and Customization
#TN|
#BS|### Module Interfaces
#BW|All modules follow consistent interface patterns:
#JN|- **Init/Deinit**: Standard lifecycle methods
#MM|- **Configuration**: Centralized config management
#YN|- **Error Handling**: Consistent error reporting
#XB|- **Logging**: Structured logging with levels
#JH|
#XK|### Configuration Options
#VN|- **Feature Gates**: Kconfig-based feature selection
#BB|- **Runtime Configuration**: Dynamic parameter adjustment
#TJ|- **Persistent Settings**: NVS-based configuration persistence
#HT|- **Web Interface**: User-friendly configuration management
#KZ|
#NT|### Extension Points
#VT|- **Additional Cameras**: Interface abstraction for different sensors
#TM|- **Storage Systems**: Plugin architecture for different backends
#BH|- **Network Protocols**: Extensible upload framework
#HP|- **Web UI**: Modular HTML/CSS/JavaScript structure