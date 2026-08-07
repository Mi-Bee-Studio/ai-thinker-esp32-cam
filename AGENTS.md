# PROJECT KNOWLEDGE BASE

ESP32-CAM firmware (mibee-cam) — OV2640 camera with MJPEG streaming, motion detection, video recording, SD storage, and serial AT config. Built with ESP-IDF v6.0.1 for MiBee Cam board (ESP32-D0WD-V3, 4MB flash, 8MB PSRAM).

**Sister project:** `Mi-Bee-Studio/seeed-esp32s3-cam` — ESP32-S3 variant, same team, same WiFi config patterns but better RF. Reference it when comparing WiFi behavior.

## STRUCTURE

15 firmware modules (1:1 `.c`/`.h`) + vendored cJSON in `main/`. Web UI in `main/web_ui/` (SPIFFS). Docs in `docs/{en,zh}/`.

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Boot sequence / entry | `main/main.c` | 19-step boot; WiFi callback → deferred STA services task (camera, MJPEG, web, motion, recorder) |
| Pin mappings, config struct, enums | `main/common.h` | `CAM_PIN_*`, `SD_PIN_*`, `cam_config_t` (version 9, magic `0xA5B6C7D8`), `wifi_state_t` |
| NVS config persistence + SD override | `main/config_manager.c/h` | Version 9, migration support; `config_load_from_sd()` reads `/sdcard/config.txt` |
| WiFi AP/STA + callbacks | `main/wifi_manager.c/h` | Max 4 callbacks, state machine, reason-code logging, dual-network failover |
| OV2640 camera driver | `main/camera_driver.c/h` | PSRAM, GPIO14 sharing, DMA freeze workaround |
| Video recording (AVI segmented) | `main/video_recorder.c/h` | Init in deferred STA services task |
| REST API + SPIFFS static | `main/web_server.c/h` | 10+ endpoints, WebSocket support |
| MJPEG streaming | `main/mjpeg_streamer.c/h` | 2-client limit, async mode |
| Motion detection | `main/motion_detect.c/h` | Frame-diff, brightness flash trigger |
| SD card storage | `main/storage_manager.c/h` | FAT, hot-plug monitor, photo cache warming |
| Serial AT config | `main/serial_config.c/h` | AT+ commands for WiFi setup over UART |
| Prometheus metrics | `main/health_monitor.c/h` | `/metrics` endpoint, 10s interval |
| Timelapse | `main/timelapse.c/h` | Burst capture, configurable interval |
| Partition layout | `partitions.csv` | nvs 24KB + phy_init 4KB + factory 3.5MB + otadata + spiffs 432KB |
| SDK defaults | `sdkconfig.defaults` | PSRAM, legacy I2C, size opt, WiFi PHY full cal, stack tuning |
| Build dependencies | `main/idf_component.yml` | `espressif/esp32-camera ^2.1.6` |
| CI pipeline | `.github/workflows/release.yml` | `espressif/idf:v6.0` container, merged binary |

## REST API Endpoints

All business endpoints use the `/api/` prefix. Returns JSON envelope `{"ok":true,"data":...}` on success, `{"ok":false,"error":"..."}` on failure.

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/api/status` | open | Device status (WiFi, camera, system, storage) |
| GET | `/api/config` | open | Current configuration (passwords masked) |
| POST | `/api/config` | write | Partial config update; first-time password setup when `web_password` is empty |
| GET | `/api/capabilities` | open | Board capability flags (12 booleans) |
| GET | `/api/capture` | open | Single JPEG snapshot (`image/jpeg`, not JSON) |
| GET | `/api/scan` | open | WiFi AP scan |
| POST | `/api/reset` | write | Factory reset config to defaults |
| POST | `/api/reboot` | write | Reboot device |
| GET | `/api/record` | open | Recording status |
| POST | `/api/record` | write | Start/stop recording (`?action=start|stop`) |
| GET | `/api/files` | open | List SD files |
| DELETE | `/api/files` | write | Delete a file (`?name=...`) |
| GET | `/api/download` | open | Download file (`?name=...&type=photo|recording`) |
| POST | `/api/format` | write | Format SD card |
| GET | `/api/ota/info` | open | OTA status/info |
| POST | `/api/ota/upload` | write | Upload firmware binary |
| POST | `/api/ota/spiffs` | write | Upload SPIFFS image |
| POST | `/api/led` | write | Flash LED control (`?action=on|off|toggle`) |
| GET | `/api/led` | open | Flash LED state |
| POST | `/api/timelapse/start` | write | Start timelapse |
| POST | `/api/timelapse/stop` | write | Stop timelapse |
| GET | `/api/timelapse/status` | open | Timelapse status |
| OPTIONS | `/*` | — | CORS preflight (204 No Content) |

**Auth:** `X-Password` header for write operations. When `web_password` is empty (first boot), all writes return 401 `SET_PASSWORD_FIRST` except `POST /api/config` with a `web_password` field (first-time setup).

**MJPEG stream:** Separate TCP server on port `:81` (independent of main web server on port 80).

**Exempt paths:** `/metrics` (Prometheus), `/onvif/*` (SOAP) are not under `/api/`.

## Web UI

Single-page application served from SPIFFS. Four files:
- `index.html` — page structure
- `app.js` — logic and API calls
- `style.css` — light/dark theme styles
- `i18n.js` — zh/en bilingual translations (auto-detect, persisted in localStorage)

Controls are shown or hidden based on `GET /api/capabilities`. The SPA baseline originates from `esp32s3-n16r8-cam` and is ported to all boards.

## Capabilities

This board returns the following from `GET /api/capabilities`:

| Capability | Supported |
|------------|-----------|
| ai | ❌ |
| sd | ✅ |
| audio | ❌ |
| ota | ✅ |
| mic | ❌ |
| flash_led | ✅ |
| recording | ✅ |
| timelapse | ✅ |
| onvif | ✅ |
| rtsp | ❌ |
| websocket | ❌ |
| mdns | ❌ |

## ESP-IDF BUILD

### Environment

ESP-IDF v6.0.1 at `~/.espressif/v6.0.1/esp-idf/`. Other versions: v5.4.4, v5.5.4.

```bash
export IDF_PATH=~/.espressif/v6.0.1/esp-idf
. $IDF_PATH/export.sh
idf.py --version   # → ESP-IDF v6.0.1
```

### Commands

```bash
idf.py set-target esp32          # ESP32 only (NOT S2/S3)
idf.py build                      # Incremental build
idf.py -p /dev/ttyUSB0 flash      # Flash via serial
idf.py -p /dev/ttyUSB0 monitor    # Serial monitor
idf.py fullclean                  # Removes build/ + managed_components/

# Regenerate sdkconfig from defaults (REQUIRED after editing sdkconfig.defaults):
rm sdkconfig && idf.py set-target esp32 && idf.py build
```

**`sdkconfig` is generated and gitignored.** A stale `sdkconfig` silently overrides `sdkconfig.defaults` — always delete it after changing defaults.

### Flash output

- App: `build/mibee_cam.bin` (~1.06 MB, ~30% of 3.5MB factory partition)
- Also: `bootloader.bin`, `partition-table.bin`, `spiffs.bin`, `ota_data_initial.bin`
- `build/flash_args` has pre-computed esptool offsets

### Booting after flash (RTS trap)

`idf.py flash` resets the chip via RTS after writing, but on this board the post-flash reset does **not** reliably boot the new firmware — the chip can sit with the RTS line held in a state where nothing runs. Symptom: flash reports success, but the device is silent on serial and the web UI never comes up.

Explicitly boot the freshly-flashed image with:

```bash
esptool --chip esp32 -p /dev/ttyUSB0 --no-stub run
```

`--no-stub` is required: the default stub soft-reset leaves the chip in the same stuck state. `run` releases the reset lines so the bootloader actually executes.

This only matters after `flash`. `idf.py monitor` alone also won't recover it — you need the `esptool ... run` invocation.

### SPIFFS

`spiffs_create_partition_image(spiffs main/web_ui FLASH_IN_PROJECT)` in root `CMakeLists.txt` auto-packages `main/web_ui/` into `build/spiffs.bin` at build time.

## WiFi (CRITICAL GOTCHAS)

This board has **marginal WiFi RF** (AI-Thinker-class PCB antenna + distance to router). Several non-obvious config choices compensate:

### PHY calibration — full every boot

`sdkconfig.defaults` forces `CONFIG_ESP_PHY_RF_CAL_FULL=y` + `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n`. The default (partial cal + stored NVS data) produces unreliable results on this board — symptoms: `AUTH_EXPIRE` (reason=2), `NO_AP_FOUND` (reason=201), intermittent connect/fail across reboots.

If WiFi won't connect after a change, **erase the phy_init partition** to force fresh calibration:
```bash
python3 -m esptool --port /dev/ttyUSB0 erase_region 0xf000 0x1000
```

### STA config (`wifi_manager.c: wifi_start_sta`)

- `threshold.authmode = WIFI_AUTH_OPEN` — auto-negotiate WPA2/WPA3/mixed/WPA/OPEN
- `pmf_cfg.capable = true, .required = false` — PMF capable for WPA3 transition APs
- `sae_pwe_h2e = WPA3_SAE_PWE_BOTH` — WPA3 SAE compatibility
- `WIFI_BW20` (HT20) — ~3dB better RX sensitivity than HT40 for weak signals
- TX power: 20 dBm (80 quarter-dBm) — do NOT lower; marginal antenna needs full power

### Reason code diagnostics

`wifi_disconnect_reason_str()` in `wifi_manager.c` maps reason codes to readable strings. Key codes:
- `reason=2 AUTH_EXPIRE` → AP can't hear ESP32 TX (signal/calibration, NOT password)
- `reason=15/204` → 4-way handshake timeout (likely wrong password)
- `reason=201 NO_AP_FOUND` → wrong SSID or out of range
- `reason=211` → authmode threshold mismatch

### Stack overflow risks

- `esp_timer` task (6144 bytes) — overflows under WiFi reconnect stress. If adding WiFi-related timers, may need 8192+.
- `warm_cache` task (8192 bytes) — overflows with 700+ SD photos at 4096. Any task doing fatfs directory traversal needs ≥8192.

## SERIAL DEBUGGING

Serial port: `/dev/ttyUSB0` (CH340 USB-serial). User must be in `dialout` group.

To capture boot log with hardware reset (ESP32 EN on RTS, GPIO0 on DTR):
```python
import serial, time, sys
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.5)
ser.reset_input_buffer()
ser.setDTR(False); time.sleep(0.05)   # GPIO0 high = normal boot
ser.setRTS(True);  time.sleep(0.1)    # EN low = reset
ser.setRTS(False)                      # release → firmware boots
# then read for N seconds
```

For panic/backtrace analysis: the ELF at `build/mibee_cam.elf` + `xtensa-esp32-elf-addr2line` resolves addresses. Note: corrupted backtraces (`|<-CORRUPTED`) usually mean stack overflow, not the displayed call chain.

## CONVENTIONS

- **Naming**: `snake_case` functions/vars, `snake_case_t` typedefs, `UPPER_CASE` pin/constant macros
- **Module pattern**: 1:1 `.c`/`.h`, `module_init()` / `module_start()` / `module_get_*()` / `module_set_*()`
- **Static prefix**: `s_` for module-level static vars
- **Logging**: `ESP_LOGx(TAG, ...)` exclusively, errors include `esp_err_to_name(ret)`
- **Error handling**: `esp_err_t` returns, check `!= ESP_OK`, log + return
- **Comments**: Doxygen on public functions, `/* ── Section ── */` dividers, `/* */` for workaround rationale
- **Config struct**: `cam_config_t` in `common.h`, persisted in NVS, magic+version validated on load

## GOTCHAS

- **GPIO14 shared**: SD card CLK and camera XCLK share GPIO14. Init order: release SD bus → camera init → SD init. After camera init, some fatfs operations (`f_getfree`, `stat`, `opendir`) may hang — only `mkdir`/`fopen`/`fwrite`/`fclose` are reliable.
- **Camera init deferred**: Must happen AFTER WiFi STA connection (ESP32 DMA freeze workaround, esp32-camera#620).
- **BOOT button unusable**: GPIO0 is camera XCLK — cannot use for factory reset. Use `POST /api/reset`.
- **No OTA**: Single factory partition. Firmware updates via serial flash only.
- **cJSON vendored in `main/`**: 3rd-party (known issues: O(n²) comparison, int overflow). Not a managed dependency.
- **No test infrastructure**: All validation is runtime error-checking on hardware.
- **`nas_uploader.c/h` referenced in old docs but does not exist** in this project.

## NOTES

- **Config version 9** — if adding fields to `cam_config_t`, bump `CONFIG_VERSION` in `common.h` and add migration logic in `config_manager.c` (`config_load()` handles version-skipped migration).
- **PSRAM required** for camera frame buffers. Board has 8MB PSRAM, 4MB usable.
- **Hardware-specific**: MiBee Cam only. Pins differ on ESP32-S3 (see sister project `seeed-esp32s3-cam`).
- **WiFi antenna**: Board has PCB antenna + optional IPEX external antenna (requires soldering 0Ω select resistor). PCB antenna has marginal range.
