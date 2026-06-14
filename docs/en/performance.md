#MT|![Build Status](https://github.com/Mi-Bee-Studio/mibee-cam/workflows/Release/badge.svg)
#MP|![Platform](https://img.shields.io/badge/platform-ESP32-blue)
#TY|![Camera](https://img.shields.io/badge/camera-OV2640-green)
#XV|![License](https://img.shields.io/badge/license-MIT-orange)
#BT|
#TR|# Performance and Latency
#HN|
#HQ|> Data based on MiBee Cam (ESP32 dual-core 240MHz, 4MB PSRAM, OV2640), firmware `main` branch.
#JT|
#RW|## Core Hardware Constraints
#TJ|
#VK|| Constraint | Impact |
#HS||------------|--------|
#JW|| **Single-core ESP32 (not S3)** | No H.264 hardware encoder; JPEG encoding done by OV2640 internal ISP, ESP32 only moves data |
#KR|| **4MB PSRAM** | Frame buffer in PSRAM (`fb_location = CAMERA_FB_IN_PSRAM`) |
#MK|| **fb_count = 2** (dual buffer) | Cannot grab next frame while one is held; UXGA uses ~2.4MB PSRAM |
#SV|| **20MHz XCLK** | Hardcoded (`camera_driver.c:90`) |
#PR|| **2.4GHz WiFi 802.11 b/g/n** | Effective bandwidth ~20-30 Mbps (affected by signal quality / interference) |
#HZ|| **Single-threaded HTTP server** | `HTTPD_DEFAULT_CONFIG` default; one slow request blocks all others |
#YQ|
#JY|> ⚠️ ESP32 in STA mode has a known camera DMA freeze issue. The firmware uses "start WiFi first → init camera in callback + 3x retry" workaround (`main.c:54-72`).
#HN|## Resolution vs Achievable FPS
#KW|
#KY|| Resolution | Pixels | Achievable FPS (streaming) | JPEG Encode Time | Recommended Use |
#KT||------------|--------|----------------------------|--------------------|-----------------|
#ZQ|| **VGA** 640×480 | 307,200 | **15-20** | ~50-100ms | Live preview |
#BT|| **SVGA** 800×600 | 480,000 | 12-15 | ~80-130ms | Medium quality stream |
#MN|| **XGA** 1024×768 | 786,432 | 6-10 | ~150-250ms | Not recommended for streaming |
#RN|| **UXGA** 1600×1200 | 1,920,000 | **2-5** | ~300-600ms | Single frame capture |
#SZ|
#TH|> Streaming FPS is capped by hardcoded `STREAM_FPS = 15` (`mjpeg_streamer.c:34`), even if hardware can do 20 FPS.
#QY|
#RH|### Counter-intuitive Resolution → FOV Relationship
#TX|
#HT|OV2640 uses **internal ISP scaling/windowing** for lower resolutions, not software cropping:
#RB|
#VR|| Selected | Field of View (relative to full sensor) | Equivalent Optical Behavior |
#WH||----------|----------------------------------------|------------------------------|
#BJ|| **UXGA** | 100% | Widest |
#YV|| XGA | ~81% | Center crop |
#QQ|| SVGA | ~63% | Medium |
#ZW|| **VGA** | **50%** (center 1/4 area) | **2× digital zoom** |
#PB|
#XH|⚠️ Choosing VGA does NOT give wider FOV — it's the **narrowest** FOV with 2× zoom effect.
#TJ|
#RP|## JPEG Size Estimates
#BY|
#NW|Actual JPEG size depends on scene complexity. The table below shows typical values (based on OV2640 measured statistics):
#QW|
#KN|| Resolution | Quality 0-10 (high) | Quality 11-20 | Quality 21-40 | Quality 41-63 (low) |
#RS||------------|---------------------|----------------|----------------|----------------------|
#NB|| VGA | 25-50 KB | 15-25 KB | 8-15 KB | 4-8 KB |
#HQ|| SVGA | 40-80 KB | 20-40 KB | 12-20 KB | 6-12 KB |
#KK|| XGA | 70-130 KB | 35-70 KB | 18-35 KB | 9-18 KB |
#BQ|| **UXGA** | **120-250 KB** | 60-120 KB | 30-60 KB | 15-30 KB |
#WH|
#SJ|**Important**:
#QY|- In dark scenes, JPEG size **shrinks significantly** (encoder produces more zero blocks). This is the basis for brightness detection (`flash_led.c` uses 12-22KB range as threshold).
#WS|- Simple scenes (solid color wall, document) may be only 30-50% of these ranges.
#JP|- Complex scenes (foliage, crowds) may reach 1.5× the upper bound.
#RJ|
#NH|## Single `/capture` End-to-End Latency
#XZ|
#NN|| Scenario | Encode + Capture | WiFi Send (4KB chunk) | Total Latency |
#SR||----------|-------------------|------------------------|----------------|
#NY|| VGA quality 10 | ~80ms | ~50ms (10-20KB) | **~130-200ms** |
#KV|| SVGA quality 10 | ~120ms | ~70ms | ~200-300ms |
#RM|| XGA quality 10 | ~200ms | ~120ms | ~400-500ms |
#BT|| **UXGA quality 0** | ~500ms | **~250ms** (80KB ÷ 4KB) | **~1-1.5 seconds** |
#PR|
#BY|> `/capture` uses camera mutex (3-second timeout, `camera_driver.c:218`). **It will be blocked** if a stream is running.
#HV|
#TW|## MJPEG Streaming Latency
#SZ|
#PM|Hardcoded in code:
#TK|- `STREAM_FPS = 15` (`mjpeg_streamer.c:34`)
#VW|- `STREAM_FRAME_DELAY = 1000/15 ≈ 66ms` (per-frame delay)
#QP|- `CHUNK_SIZE = 4096` (4KB chunked transfer)
#KB|
#SX|```
#KS|Actual latency ≈ frame delay (66ms) + encode time + network send + browser decode/render
#WM|              = 66ms + 80-150ms + 30-100ms + 50-100ms (browser)
#SW|              ≈ 250-500ms (typical VGA measurement)
#KZ|```
#KR|
#NH|| Resolution | Stream Latency | Notes |
#SW||------------|----------------|-------|
#WK|| VGA quality 10-12 | **300-500ms** | Near real-time |
#YH|| SVGA quality 10-12 | 500-800ms | Acceptable |
#HZ|| XGA quality 10-12 | 800-1500ms | Noticeable lag |
#JK|| **UXGA quality 0** | **1500-2500ms** | Impractical |
#BX|
#BV|## Motion Detection Latency
#MS|
#KV|`motion_detect.c:42` defines `CAPTURE_INTERVAL_MS = 500`:
#ZT|
#ZV|```
#MJ|1. Capture reference frame A (~80ms)
#TY|2. Wait 500ms
#RW|3. Capture comparison frame B (~80ms)
#WP|4. JPEG byte sampling diff (~10ms)
#HB|5. Trigger + capture photo (~150ms incl. SD write)
#SZ|─────────────
#WN|Total: ~700-1500ms
#ZX|```
#TS|
#RP|With double-confirmation debouncing, trigger response is stable at **1-2 second range**. Far from "real-time".
#BP|
#WJ|## WiFi Bandwidth Calculation
#YX|
#VK|MJPEG stream = frame rate × per-frame bytes:
#PP|
#TV|| Resolution | Quality | Per-Frame Size | 15 FPS Required | Notes |
#PK||------------|---------|----------------|------------------|-------|
#TH|| VGA | 12 | 12 KB | **1.4 Mbps** | Single client easy |
#KV|| VGA | 0 | 30 KB | 3.6 Mbps | 2 clients full = 7.2 Mbps |
#VQ|| SVGA | 12 | 20 KB | 2.4 Mbps | Near limit |
#QN|| XGA | 12 | 35 KB | 4.2 Mbps | 2 clients will lag |
#SY|| UXGA | 0 | 120 KB | **14.4 Mbps** | **WiFi impossible** |
#QY|
#RJ|> Measured ESP32 2.4GHz WiFi at -60dBm RSSI gives single-client stable bandwidth ~5-8 Mbps, halved with 2 clients.
#WY|
#BW|## Key Latency Bottlenecks (Code References)
#YB|
#JQ|| Bottleneck | File:Line | Impact |
#XS||------------|-----------|--------|
#KQ|| Single-thread HTTP | `web_server.c:887` | One slow request blocks all others |
#PS|| Camera mutex 3s timeout | `camera_driver.c:218` | Stream and `/capture` are mutually exclusive |
#SW|| Async stream handler | `mjpeg_streamer.c:185` | ✅ Solves "stream blocks other" problem |
#JK|| PSRAM DMA disabled | `sdkconfig.defaults` | ESP32 DMA bug workaround |
#MQ|| 4KB chunked send | `mjpeg_streamer.c:33` | Slow but stable |
#ST|| `recv_wait_timeout=10s`, `send_wait_timeout=30s` | `web_server.c:890-891` | WiFi slowness tolerance |
#TX|| Timelapse blocking | `timelapse.c` | Stream freezes hundreds of ms during capture |
#BN|| WiFi boot defers camera | `main.c:54` | +1-2s boot delay |
#TX|| `vTaskDelay(STREAM_FRAME_DELAY)` | `mjpeg_streamer.c:143` | 66ms hardcoded frame interval |
#QS|
#BQ|## Memory Usage
#QR|
#YZ|| Module | PSRAM Usage | Notes |
#KR||--------|-------------|-------|
#XK|| Camera frame buffer (UXGA dual) | ~2.4MB | Worst case |
#TJ|| Camera frame buffer (VGA dual) | ~0.6MB | Minimum case |
#MJ|| SD filename buffer (`/api/files`) | 32KB | Temporary malloc |
#MV|| SD list cache | 0-32KB | 30s TTL |
#HJ|| Motion detection sample buffer | frame_len/10 | E.g., UXGA 100KB → 10KB sample |
#QK|| Web server LRU buffer | Default 4KB | 5 file descriptors |
#ZR|| **Available Remaining** | **~1.6MB (under UXGA)** | Cannot allocate more |
#TJ|
#SZ|## Configuration Recommendations
#HV|
#QH|### For "Near Real-Time" Stream
#MN|- Resolution: **VGA 640×480**
#BM|- JPEG quality: 15-20
#RB|- FPS: 15 (default)
#JY|- Expected latency: 300-500ms
#WV|- Use case: Remote monitoring, pet watching
#TV|
#XT|### For "High Quality Snapshot"
#KX|- Mode: Single frame `/capture`
#JJ|- Resolution: **UXGA 1600×1200**
#PX|- JPEG quality: 0
#VQ|- Expected latency: 1-1.5s
#RJ|- Use case: Timed capture, document photos
#NX|
#PY|### For "Long-Duration Video Monitoring"
#XJ|- Use timelapse instead of streaming
#JJ|- Interval: 60-300 seconds
#VW|- Files organized by month, periodically clean SD
#HM|
#HP|### For "Nighttime Monitoring"
#YQ|- `flash_threshold` set to 60-70 (more sensitive)
#MM|- Flash LED on GPIO4 (PWM)
#KH|- Note: Flash range limited (~2 meters)
#BN|
#VW|## Performance Trade-offs
#HM|
#KQ|### Key Limitations
#TS|- **2-client limit**: MJPEG streaming supports maximum 2 concurrent clients due to memory constraints
#PR|- **Single-threaded HTTP server**: One slow request blocks all other API calls
#WX|- **Camera mutex contention**: Streaming and `/capture` endpoints are mutually exclusive
#NT|- **PSRAM limitations**: Frame buffer size limits resolution choices
#VB|
#NH|### Optimization Recommendations
#MQ|**For best performance:**
#NT|- Use VGA resolution (640×480) for streaming
#YM|- Limit to 10-15 FPS for stable operation
#MW|- Avoid concurrent camera operations during streaming
#NX|- Monitor free heap memory (should remain >20KB)
#HR|- Disable status LED if not needed for power savings
#BH|
#RM|**For high-quality snapshots:**
#JW|- Use UXGA resolution (1600×1200) with quality 0
#KJ|- Accept higher latency (1-1.5 seconds)
#ZP|- Avoid streaming during capture operations
#QQ|## Things NOT to Do
#MH|
#RR|| Bad Practice | Consequence |
#MP||--------------|-------------|
#SP|| Frequent `/capture` during streaming | Camera mutex conflict, stream stutters |
#MR|| Stream UXGA | WiFi bandwidth insufficient, 2s+ latency |
#KP|| Enable motion + timelapse + stream simultaneously | Three tasks compete for camera, timing unpredictable |
#QZ|| Stream with RSSI < -75dBm | Severe frame loss |
#RX|| UXGA quality 0 streaming | 200KB/frame × 2 clients = 32 Mbps, physically impossible |
#ZX|
#ZY|## Related Code Locations
#NH|
#MR|- Camera parameters: `main/camera_driver.c:90-127`
#TZ|- Stream config: `main/mjpeg_streamer.c:32-37`
#KP|- Motion detection interval: `main/motion_detect.c:42`
#VY|- HTTP server config: `main/web_server.c:886-893`
#SR|- Camera mutex: `main/camera_driver.c:218`
#XB|- Flash brightness threshold: `main/flash_led.c` (12-22KB range judgment)
#KZ|
#NH|## Related Documentation
#RZ|
#KB|- [`capabilities.md`](./capabilities.md) - Feature overview
#TT|- [`hardware.md`](./hardware.md) - Board-level hardware specs
#NQ|- [`lens-and-fov.md`](./lens-and-fov.md) - Lens parameters
#ZB|- [`troubleshooting.md`](./troubleshooting.md) - Common issues