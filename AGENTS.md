# PROJECT KNOWLEDGE BASE

**Generated:** 2026-06-05
**Commit:** fd43e58
**Branch:** main

## OVERVIEW

ESP32-CAM firmware (mibee-cam) — OV2640 camera with MJPEG streaming, motion detection, SD storage, and NAS upload. Built with ESP-IDF v6.0.1 for MiBee Cam board (ESP32-D0WD-V3, 4MB flash, 8MB PSRAM).

## STRUCTURE

```
./
├── main/              # All firmware source (14 modules, 1:1 .c/.h)
│   └── web_ui/        # SPIFFS web assets (4 HTML files)
├── docs/en/           # English docs (getting-started, api, performance, etc.)
├── docs/zh/           # Chinese docs (same topics)
├── managed_components/ # Auto-downloaded by idf_component.yml (gitignored)
├── build/             # Build output (gitignored)
└── .github/workflows/ # CI: docker build + release on v* tags
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Boot sequence / entry | `main/main.c` | 16-step init, WiFi callback → STA services task |
| Pin mappings, config struct, enums | `main/common.h` | CAM_PIN_*, SD_PIN_*, cam_config_t, wifi_state_t |
| NVS config persistence | `main/config_manager.c/h` | Version 6, magic 0xA5B6C7D8, migration support |
| WiFi AP/STA + callbacks | `main/wifi_manager.c/h` | Max 4 callbacks, state machine |
| OV2640 camera driver | `main/camera_driver.c/h` | PSRAM, GPIO14 sharing, DMA freeze workaround |
| REST API + SPIFFS static | `main/web_server.c/h` | 11 endpoints, 10s recv timeout |
| MJPEG streaming | `main/mjpeg_streamer.c/h` | 2-client limit, async mode |
| Motion detection | `main/motion_detect.c/h` | Frame-diff, brightness flash trigger |
| SD card storage | `main/storage_manager.c/h` | FAT, hot-plug monitor, photo cache |
| Prometheus metrics | `main/health_monitor.c/h` | /metrics endpoint, 10s interval |
| Timelapse | `main/timelapse.c/h` | Burst capture, configurable interval |
| Partition layout | `partitions.csv` | factory 3.5MB + spiffs 448KB (no OTA) |
| SDK defaults | `sdkconfig.defaults` | PSRAM req, legacy I2C, size opt |
| Build dependencies | `main/idf_component.yml` | espressif/esp32-camera ^2.1.6 |
| CI pipeline | `.github/workflows/release.yml` | espressif/idf:v6.0 container, merged binary |

## ESP-IDF BUILD METHODOLOGY

### Environment Setup

ESP-IDF v6.0.1 is installed at `~/.espressif/v6.0.1/esp-idf/`.
Other available versions: v5.4.4, v5.5.4.

```bash
# Source the environment (required before any idf.py command)
export IDF_PATH=~/.espressif/v6.0.1/esp-idf
. $IDF_PATH/export.sh

# Verify
idf.py --version   # → ESP-IDF v6.0.1

# If idf.py is not found after sourcing (common in some shells):
alias idf.py='python3 $IDF_PATH/tools/idf.py'
```

On first use, create the Python virtual environment:

```bash
python3 $IDF_PATH/tools/idf_tools.py install-python-env
# Creates: ~/.espressif/python_env/idf6.0_py3.12_env/
```

### Build Commands

```bash
# 1. Set target (ESP32 only, NOT S2/S3)
idf.py set-target esp32

# 2. Build firmware (app + bootloader + partition table + SPIFFS)
idf.py build

# 3. Full clean (removes build/ + managed_components/)
idf.py fullclean

# 4. Clean + rebuild from scratch
idf.py fullclean && idf.py set-target esp32 && idf.py build
```

### What `idf.py build` Does

1. **CMake configure** — detects toolchain, resolves components, generates build files
2. **Dependency resolution** — downloads managed components via `idf_component.yml` (e.g. `espressif/esp32-camera ^2.1.6`) into `managed_components/`
3. **Build all components** — 1066+ compilation steps across all IDF components + app code
4. **SPIFFS image generation** — `spiffs_create_partition_image()` in root `CMakeLists.txt` packages `main/web_ui/` into a SPIFFS filesystem
5. **Link + binary generation** — produces `mibee_cam.bin`, `bootloader.bin`, `partition-table.bin`

### Build Output

| File | Path | Size |
|------|------|------|
| Application | `build/mibee_cam.bin` | ~1.0 MB (0xfec20 bytes) |
| Bootloader | `build/bootloader/bootloader.bin` | ~25 KB (0x6600) |
| Partition table | `build/partition_table/partition-table.bin` | ~3 KB |
| SPIFFS image | `build/spiffs.bin` | web UI filesystem (448KB partition) |
| OTA data init | `build/ota_data_initial.bin` | initial OTA state |
| Flash args | `build/flash_args` | pre-computed esptool arguments |
| ELF debug | `build/mibee_cam.elf` | full debug symbols |

App uses **~28%** of the 3.5MB factory partition (72% free).

### Flash

```bash
# Option 1: idf.py (recommended)
idf.py -p /dev/ttyUSB0 flash

# Option 2: idf.py flash + monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Option 3: esptool directly (from build/)
python -m esptool --chip esp32 -b 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 4MB --flash-freq 40m \
  "@build/flash_args"

# Option 4: serial monitor only
idf.py -p /dev/ttyUSB0 monitor
```

### Configuration System

| File | Purpose |
|------|---------|
| `sdkconfig.defaults` | SDK defaults committed to repo (PSRAM, I2C legacy, size opt) |
| `sdkconfig` | Generated from defaults + menuconfig; **do not commit** |
| `partitions.csv` | Custom partition layout (factory 3.5MB + spiffs 448KB) |
| `main/Kconfig.projbuild` | Project-specific Kconfig options |
| `main/idf_component.yml` | External component dependencies (esp32-camera) |

```bash
# Modify configuration
idf.py menuconfig

# Regenerate sdkconfig from defaults
rm sdkconfig && idf.py set-target esp32
```

### Build Dependencies (main/CMakeLists.txt)

```
REQUIRES: esp_wifi, nvs_flash, esp_http_server, espressif__esp32-camera,
          driver, esp_driver_i2c, spiffs, fatfs, sdmmc, esp_http_client,
          tcp_transport, mbedtls, esp_netif, esp_event, esp_timer,
          lwip, freertos, esp_system, log
```

### Build Hierarchy

```
idf.py build
├── CMake configure (toolchain detection, component resolution)
├── Bootloader (138 build steps)
│   ├── ESP-IDF bootloader components
│   └── bootloader.bin + check_sizes
├── App (1066+ build steps)
│   ├── ~120 IDF components (esp_system, esp_wifi, driver, ...)
│   ├── managed_components/ (esp32-camera 2.1.6, esp_jpeg 1.3.1)
│   ├── main/ (14 source files + cJSON)
│   ├── SPIFFS image (web_ui/ → spiffs.bin)
│   └── Link → mibee_cam.elf → mibee_cam.bin
└── Partition size check
```

### CI Pipeline

```yaml
# .github/workflows/release.yml
container: espressif/idf:v6.0        # Docker build environment
on: push tags v*                      # Release on version tags
steps:
  - checkout
  - idf.py set-target esp32 && idf.py build
  - Package: bootloader.bin + partition-table.bin + mibee_cam.bin
  - Merge binary: esptool merge_bin → mibee_cam_merged.bin
  - Create GitHub Release with firmware/* artifacts
```

### Serial Access

```bash
# Add user to dialout group (required for /dev/ttyUSB0)
sudo usermod -a -G dialout $USER
# Then logout and login again

# Verify board connection
esptool --port /dev/ttyUSB0 chip-id
# → Detecting chip type... ESP32-D0WD-V3 (revision v3.1)
# → MAC: c8:2e:18:45:d8:68
```

## CONVENTIONS

- **Naming**: `snake_case` for all functions/vars, `snake_case_t` for typedefs, `UPPER_CASE` for pin/constant macros
- **Module pattern**: 1:1 `.c`/`.h` pairing, `module_init()` / `module_start()` / `module_get_*()` / `module_set_*()`
- **Static prefix**: `s_` for module-level static vars (e.g. `s_mjpeg_started`, `s_config`)
- **Logging**: `ESP_LOGx(TAG, ...)` exclusively, `TAG = "module_name"`, errors include `esp_err_to_name(ret)`
- **Error handling**: `esp_err_t` returns, check `!= ESP_OK`, log + return pattern
- **Comments**: Doxygen `/** @brief @param @return */` on public functions, `/* ── Section ── */` dividers, `/* */` for workaround rationale
- **Config struct**: `cam_config_t` in `common.h`, persisted in NVS, magic+version validated on load

## ANTI-PATTERNS (THIS PROJECT)

- **cJSON library embedded in `main/`**: Should be a separate component or managed dependency. Contains known issues: O(n²) comparison, int overflow, deprecated `valueint` field
- **NAS uploader referenced in README but removed**: `nas_uploader.c/h` and `webdav_client.c/h` do not exist in code
- **BOOT button factory reset disabled**: GPIO0 is camera XCLK — use `POST /api/reset` instead
- **Camera init deferred after WiFi STA**: Known ESP32 DMA freeze workaround (esp32-camera#620)
- **No test infrastructure**: Zero unit/integration tests — all validation is runtime error-checking only

## UNIQUE STYLES

- `sdkconfig.defaults` documents workaround references with GitHub issue URLs
- `camera_release_sd_bus()` pattern for GPIO14 arbitration (SD CLK ↔ camera XCLK shared pin)
- `spiffs_create_partition_image(spiffs main/web_ui FLASH_IN_PROJECT)` in root CMakeLists
- Config migration: inline NVS blob read → field-by-field migration → re-save with new version

## NOTES

- **Hardware-specific:** MiBee Cam only. Pins differ on ESP32-S3 or other boards.
- **PSRAM required** for camera frame buffers. Board has 8MB PSRAM, 4MB usable.
- **GPIO14 shared** between SD card CLK and camera XCLK — init order matters: SD deinit → camera init → SD init.
- **No OTA**: Single factory partition. Firmware updates via serial flash only.
- **Config version 6** — if adding fields to `cam_config_t`, bump version and add migration logic in `config_manager.c`.
