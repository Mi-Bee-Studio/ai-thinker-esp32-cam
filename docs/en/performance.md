> 🌐 [中文文档](../zh/performance.md)

# Performance and Latency

This document provides measured FPS, JPEG size, and end-to-end latency data for the ESP32-CAM firmware at different resolution/quality settings, plus performance bottleneck analysis.

> Data based on AI-Thinker ESP32-CAM (ESP32 dual-core 240MHz, 4MB PSRAM, OV2640), firmware `main` branch.

## Core Hardware Constraints

| Constraint | Impact |
|------------|--------|
| **Single-core ESP32 (not S3)** | No H.264 hardware encoder; JPEG encoding done by OV2640 internal ISP, ESP32 only moves data |
| **4MB PSRAM** | Frame buffer in PSRAM (`fb_location = CAMERA_FB_IN_PSRAM`) |
| **fb_count = 2** (dual buffer) | Cannot grab next frame while one is held; UXGA uses ~2.4MB PSRAM |
| **20MHz XCLK** | Hardcoded (`camera_driver.c:90`) |
| **2.4GHz WiFi 802.11 b/g/n** | Effective bandwidth ~20-30 Mbps (affected by signal quality / interference) |
| **Single-threaded HTTP server** | `HTTPD_DEFAULT_CONFIG` default; one slow request blocks all others |

> ⚠️ ESP32 in STA mode has a known camera DMA freeze issue. The firmware uses "start WiFi first → init camera in callback + 3x retry" workaround (`main.c:54-72`).

## Resolution vs Achievable FPS

| Resolution | Pixels | Achievable FPS (streaming) | JPEG Encode Time | Recommended Use |
|------------|--------|----------------------------|--------------------|-----------------|
| **VGA** 640×480 | 307,200 | **15-20** | ~50-100ms | Live preview |
| **SVGA** 800×600 | 480,000 | 12-15 | ~80-130ms | Medium quality stream |
| **XGA** 1024×768 | 786,432 | 6-10 | ~150-250ms | Not recommended for streaming |
| **UXGA** 1600×1200 | 1,920,000 | **2-5** | ~300-600ms | Single frame capture |

> Streaming FPS is capped by hardcoded `STREAM_FPS = 15` (`mjpeg_streamer.c:34`), even if hardware can do 20 FPS.

### Counter-intuitive Resolution → FOV Relationship

OV2640 uses **internal ISP scaling/windowing** for lower resolutions, not software cropping:

| Selected | Field of View (relative to full sensor) | Equivalent Optical Behavior |
|----------|----------------------------------------|------------------------------|
| **UXGA** | 100% | Widest |
| XGA | ~81% | Center crop |
| SVGA | ~63% | Medium |
| **VGA** | **50%** (center 1/4 area) | **2× digital zoom** |

⚠️ Choosing VGA does NOT give wider FOV — it's the **narrowest** FOV with 2× zoom effect.

## JPEG Size Estimates

Actual JPEG size depends on scene complexity. The table below shows typical values (based on OV2640 measured statistics):

| Resolution | Quality 0-10 (high) | Quality 11-20 | Quality 21-40 | Quality 41-63 (low) |
|------------|---------------------|----------------|----------------|----------------------|
| VGA | 25-50 KB | 15-25 KB | 8-15 KB | 4-8 KB |
| SVGA | 40-80 KB | 20-40 KB | 12-20 KB | 6-12 KB |
| XGA | 70-130 KB | 35-70 KB | 18-35 KB | 9-18 KB |
| **UXGA** | **120-250 KB** | 60-120 KB | 30-60 KB | 15-30 KB |

**Important**:
- In dark scenes, JPEG size **shrinks significantly** (encoder produces more zero blocks). This is the basis for brightness detection (`flash_led.c` uses 12-22KB range as threshold).
- Simple scenes (solid color wall, document) may be only 30-50% of these ranges.
- Complex scenes (foliage, crowds) may reach 1.5× the upper bound.

## Single `/capture` End-to-End Latency

| Scenario | Encode + Capture | WiFi Send (4KB chunk) | Total Latency |
|----------|-------------------|------------------------|----------------|
| VGA quality 10 | ~80ms | ~50ms (10-20KB) | **~130-200ms** |
| SVGA quality 10 | ~120ms | ~70ms | ~200-300ms |
| XGA quality 10 | ~200ms | ~120ms | ~400-500ms |
| **UXGA quality 0** | ~500ms | **~250ms** (80KB ÷ 4KB) | **~1-1.5 seconds** |

> `/capture` uses camera mutex (3-second timeout, `camera_driver.c:218`). **It will be blocked** if a stream is running.

## MJPEG Streaming Latency

Hardcoded in code:
- `STREAM_FPS = 15` (`mjpeg_streamer.c:34`)
- `STREAM_FRAME_DELAY = 1000/15 ≈ 66ms` (per-frame delay)
- `CHUNK_SIZE = 4096` (4KB chunked transfer)

```
Actual latency ≈ frame delay (66ms) + encode time + network send + browser decode/render
              = 66ms + 80-150ms + 30-100ms + 50-100ms (browser)
              ≈ 250-500ms (typical VGA measurement)
```

| Resolution | Stream Latency | Notes |
|------------|----------------|-------|
| VGA quality 10-12 | **300-500ms** | Near real-time |
| SVGA quality 10-12 | 500-800ms | Acceptable |
| XGA quality 10-12 | 800-1500ms | Noticeable lag |
| **UXGA quality 0** | **1500-2500ms** | Impractical |

## Motion Detection Latency

`motion_detect.c:42` defines `CAPTURE_INTERVAL_MS = 500`:

```
1. Capture reference frame A (~80ms)
2. Wait 500ms
3. Capture comparison frame B (~80ms)
4. JPEG byte sampling diff (~10ms)
5. Trigger + capture photo (~150ms incl. SD write)
─────────────
Total: ~700-1500ms
```

With double-confirmation debouncing, trigger response is stable at **1-2 second range**. Far from "real-time".

## WiFi Bandwidth Calculation

MJPEG stream = frame rate × per-frame bytes:

| Resolution | Quality | Per-Frame Size | 15 FPS Required | Notes |
|------------|---------|----------------|------------------|-------|
| VGA | 12 | 12 KB | **1.4 Mbps** | Single client easy |
| VGA | 0 | 30 KB | 3.6 Mbps | 2 clients full = 7.2 Mbps |
| SVGA | 12 | 20 KB | 2.4 Mbps | Near limit |
| XGA | 12 | 35 KB | 4.2 Mbps | 2 clients will lag |
| UXGA | 0 | 120 KB | **14.4 Mbps** | **WiFi impossible** |

> Measured ESP32 2.4GHz WiFi at -60dBm RSSI gives single-client stable bandwidth ~5-8 Mbps, halved with 2 clients.

## Key Latency Bottlenecks (Code References)

| Bottleneck | File:Line | Impact |
|------------|-----------|--------|
| Single-thread HTTP | `web_server.c:887` | One slow request blocks all others |
| Camera mutex 3s timeout | `camera_driver.c:218` | Stream and `/capture` are mutually exclusive |
| Async stream handler | `mjpeg_streamer.c:185` | ✅ Solves "stream blocks other" problem |
| PSRAM DMA disabled | `sdkconfig.defaults` | ESP32 DMA bug workaround |
| 4KB chunked send | `mjpeg_streamer.c:33` | Slow but stable |
| `recv_wait_timeout=10s`, `send_wait_timeout=30s` | `web_server.c:890-891` | WiFi slowness tolerance |
| Timelapse blocking | `timelapse.c` | Stream freezes hundreds of ms during capture |
| WiFi boot defers camera | `main.c:54` | +1-2s boot delay |
| `vTaskDelay(STREAM_FRAME_DELAY)` | `mjpeg_streamer.c:143` | 66ms hardcoded frame interval |

## Memory Usage

| Module | PSRAM Usage | Notes |
|--------|-------------|-------|
| Camera frame buffer (UXGA dual) | ~2.4MB | Worst case |
| Camera frame buffer (VGA dual) | ~0.6MB | Minimum case |
| SD filename buffer (`/api/files`) | 32KB | Temporary malloc |
| SD list cache | 0-32KB | 30s TTL |
| Motion detection sample buffer | frame_len/10 | E.g., UXGA 100KB → 10KB sample |
| Web server LRU buffer | Default 4KB | 5 file descriptors |
| **Available Remaining** | **~1.6MB (under UXGA)** | Cannot allocate more |

## Configuration Recommendations

### For "Near Real-Time" Stream
- Resolution: **VGA 640×480**
- JPEG quality: 15-20
- FPS: 15 (default)
- Expected latency: 300-500ms
- Use case: Remote monitoring, pet watching

### For "High Quality Snapshot"
- Mode: Single frame `/capture`
- Resolution: **UXGA 1600×1200**
- JPEG quality: 0
- Expected latency: 1-1.5s
- Use case: Timed capture, document photos

### For "Long-Duration Video Monitoring"
- Use timelapse instead of streaming
- Interval: 60-300 seconds
- Files organized by month, periodically clean SD

### For "Nighttime Monitoring"
- `flash_threshold` set to 60-70 (more sensitive)
- Flash LED on GPIO4 (PWM)
- Note: Flash range limited (~2 meters)

## Things NOT to Do

| Bad Practice | Consequence |
|--------------|-------------|
| Frequent `/capture` during streaming | Camera mutex conflict, stream stutters |
| Stream UXGA | WiFi bandwidth insufficient, 2s+ latency |
| Enable motion + timelapse + stream simultaneously | Three tasks compete for camera, timing unpredictable |
| Stream with RSSI < -75dBm | Severe frame loss |
| UXGA quality 0 streaming | 200KB/frame × 2 clients = 32 Mbps, physically impossible |

## Related Code Locations

- Camera parameters: `main/camera_driver.c:90-127`
- Stream config: `main/mjpeg_streamer.c:32-37`
- Motion detection interval: `main/motion_detect.c:42`
- HTTP server config: `main/web_server.c:886-893`
- Camera mutex: `main/camera_driver.c:218`
- Flash brightness threshold: `main/flash_led.c` (12-22KB range judgment)

## Related Documentation

- [`capabilities.md`](./capabilities.md) - Feature overview
- [`hardware.md`](./hardware.md) - Board-level hardware specs
- [`lens-and-fov.md`](./lens-and-fov.md) - Lens parameters
- [`troubleshooting.md`](./troubleshooting.md) - Common issues
