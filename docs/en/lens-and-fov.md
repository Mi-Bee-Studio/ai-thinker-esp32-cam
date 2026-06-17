> 🌐 [中文文档](../zh/lens-and-fov.md)

# Lens and Field of View (FOV)

This document explains the OV2640 camera module's physical lens parameters, field of view, "view" controls in firmware, and how to extend wide-angle/macro capabilities.

> **Key fact**: The firmware has **no** software zoom/focus/aperture control. Lens focal length is a **physical property** — swapping lenses requires no code changes.

## OV2640 Sensor Physical Parameters

| Parameter | Value |
|-----------|-------|
| Sensor size | **1/4 inch** (~3.6mm diagonal) |
| Pixels | 2MP (1600×1200 UXGA) |
| Pixel size | 2.2µm × 2.2µm |
| Dynamic range | 50 dB |
| SNR | 40 dB |
| Output format | **JPEG** (compressed by OV2640 internal ISP) |
| Sensitivity | 0.6 V/Lux-sec |

> ⚠️ Note: `camera_driver.c` does **NOT** call AEC (auto exposure) / AGC (auto gain) / AWB (auto white balance) / LENC (lens shading correction) register configuration. OV2640 runs on factory defaults, which may cause overexposure or color cast in some lighting conditions.

## Hardcoded Camera Config in Firmware

```c
// main/camera_driver.c
.xclk_freq_hz = 20000000,        // 20MHz master clock
.pixel_format = PIXFORMAT_JPEG,   // Hardcoded JPEG output
.fb_location  = CAMERA_FB_IN_PSRAM,
.grab_mode    = CAMERA_GRAB_LATEST,
.fb_count     = 2                 // Dual buffer
```

Camera controls **not exposed** by firmware:
- ❌ Focus control (OV2640 doesn't support)
- ❌ Exposure compensation
- ❌ White balance modes
- ❌ Gain
- ❌ Lens shading correction (LENC)
- ❌ Digital zoom ROI
- ❌ Horizontal flip (only `vflip` for vertical)
- ❌ Sharpness / noise reduction / color matrix tuning

## Stock MiBee Lens

The stock lens on MiBee Cam is an **M12 mount fixed-focus lens** (unscrewable but requires desoldering):

| Parameter | Typical Value |
|-----------|---------------|
| Focal length | **3.6mm** (also available in 2.8mm / 6mm variants) |
| Aperture | F2.0 |
| **Field of View (FOV)** | **60-68° horizontal** (normal/standard range) |
| IR-cut filter | Yes (650nm IR-cut) |
| Minimum focus distance | **~60cm** |
| Maximum | Theoretically infinite |
| Distortion | ~3% barrel (not severe) |
| Autofocus | **No** |
| Mount | M12 × 0.5 (standard) |

> The stock lens is **normal focal length** — neither wide-angle (typically >90°) nor macro (typically <10cm min focus).

## FOV Variation by Resolution

OV2640 uses **internal ISP scaling/windowing** for lower resolutions — not software cropping. This means **lower resolution → narrower FOV**:

| Selected | FOV Relative | FOV Absolute (3.6mm lens) | Equivalent Behavior |
|----------|--------------|----------------------------|---------------------|
| **UXGA** | 100% | ~68° horizontal | Widest (full sensor) |
| XGA | ~81% | ~55° | Center crop |
| SVGA | ~63% | ~43° | Medium |
| **VGA** | **50%** | **~34°** | **2× digital zoom effect** |

⚠️ Counter-intuitive: **choosing lower resolution → narrower FOV**. For widest FOV, you must select UXGA.

## Wide-Angle Capability

### Software Cannot Do It

The firmware has no FOV / focal length / wide-angle config. `cam_config_t` does not contain these fields. Wide-angle **requires physical lens replacement**.

### Lens Selection Reference

| Focal Length | FOV (diagonal) | FOV (horizontal, 4:3) | Distortion | Price | Use |
|--------------|----------------|------------------------|------------|-------|-----|
| **1.6mm F2.0** | ~180° | ~160° | Severe fisheye | $1-2 | Extreme wide-angle |
| **2.1mm F2.0** | ~145° | ~130° | Moderate fisheye | $1-2 | Wide-angle monitoring |
| **2.8mm F2.0** | ~110° | ~95° | Slight distortion | $1-2 | Mild wide-angle |
| **3.6mm F2.0** | ~78° | ~68° | ~3% | $1 (**stock**) | Standard |
| **6mm F2.0** | ~50° | ~42° | Negligible | $1-2 | Telephoto |
| **8mm F2.0** | ~38° | ~32° | Negligible | $1-3 | Long telephoto |
| **12mm F2.0** | ~25° | ~21° | Negligible | $2-3 | Super telephoto |

**M12 mount is universal**, but requires desoldering the old lens. **Firmware needs no changes** — OV2640 auto-adapts to any M12 focal length.

### Wide-Angle Side Effects

1. **Barrel distortion**: Worse with shorter focal length; 1.6mm fisheye needs software correction
2. **Vignetting**: Insufficient peripheral light, corners darken
3. **Edge sharpness drops**: Limited by lens optical resolution
4. **Min focus distance unchanged**: Still ~60cm (limited by lens structure)
5. **Depth of field changes**: Wide-angle has deeper DOF, telephoto has shallower

## Macro Capability

### Stock Lens Macro Limitations

- Minimum focus distance **~60cm**
- Subject < 30cm will be blurry (fixed focus is there)
- Want 5-10cm distance detail → **impossible**

### Solutions (All Hardware)

| Solution | Cost | Difficulty | Result |
|----------|------|------------|--------|
| Swap long-focal lens (6/8/12mm) | $1-3 | Medium (desolder) | Min focus 20-50cm |
| Stack close-up lens (+1/+2/+4 diopter) | $1-4 | Easy (screw on) | Min focus 5-15cm |
| Reverse-mount lens | Free | Easy | Sacrifices aperture, min focus 1-3cm |
| Add extension tube | $1 | Easy | Min focus reduced 50% |

**Close-up lens diopter conversion**:
- **+1 diopter** → Min focus ~100cm
- **+2 diopter** → Min focus ~50cm
- **+4 diopter** → Min focus ~25cm
- **+10 diopter** → Min focus ~10cm (true macro)

### Software Pseudo-Macro

- Use UXGA full resolution + post-crop center 1/4 → equivalent 2× digital zoom
- **But this is software zoom, essentially interpolation, quality degrades**
- Firmware has **no** ROI (region of interest) / digital zoom interface exposed to user

## IR / Night Vision Versions

| Lens Type | Daytime | Nighttime | With IR LEDs |
|-----------|---------|-----------|---------------|
| **IR-cut 650nm** (**stock**) | Normal color | Pitch black | No |
| **No IR-cut** (IR lens) | Red cast (needs software correction) | Can see IR scenes | ✅ Required |

### IR Lens + 850nm IR LEDs

- The firmware's GPIO4 "flash LED" is actually a **LEDC PWM white LED**
- IR LEDs need separate GPIO and hardware, **firmware does not directly support**
- But GPIO4 can be used to drive white light for fill (no IR effect)

## Lens Replacement Procedure

⚠️ Requires soldering iron. Beginners please be careful.

1. **Remove case**: MiBee board has plastic case
2. **Remove stock lens**: M12 mount, rotate counter-clockwise
3. **Desolder 22pF capacitor on lens mount** (some mounts have it)
4. **Solder new lens**: Align threads carefully
5. **Adjust focus**: Power on temporarily, rotate lens until image is sharpest
6. **Secure**: Apply UV glue or hot glue

**Zero firmware changes** — OV2640 auto-adapts.

## Other Lens-Related Phenomena

### Vignetting
- Shorter focal length (wide-angle) → more peripheral vignetting
- UXGA full resolution on 3.6mm stock lens → slight corner darkening
- Optical phenomenon, **LENC correction not enabled** (`camera_driver.c` doesn't call `set_lenc`)

### IR Reflection
- Strong sunlight on glass / water surface → possible IR artifacts
- IR-cut lens filters 650nm+, **should eliminate this**

### Dust
- Dust inside M12 mount → black dots in image
- Be careful about dust when removing lens

## Summary

| Dimension | Current State |
|-----------|---------------|
| Wide-angle support | ❌ No software; only via 1.6-2.8mm physical lens swap |
| Macro support | ❌ No software; only via long-focal or close-up lens |
| FOV (default) | 60-68° horizontal (3.6mm stock) |
| FOV (lowest res VGA) | **~34° horizontal** (2× zoom effect) |
| FOV (highest res UXGA) | **~68° horizontal** (widest) |
| Digital zoom | ❌ No ROI interface in firmware |
| Autofocus | ❌ OV2640 doesn't support |
| Distortion correction (LENC) | ❌ Not enabled |
| Mirror flip | ✅ `vflip` config |
| 180° rotation | ❌ Vertical flip only |
| IR lens | ⚠️ Hardware supported, no IR LED control in firmware |

**In short**: The firmware is a "dumb camera driver" for OV2640. For wide-angle or macro, just spend $1-3 on a new M12 lens and screw it on — **zero code changes needed**.

## Related Documentation

- [`hardware.md`](./hardware.md) - Board-level hardware specs
- [`performance.md`](./performance.md) - Resolution vs FPS
- [`capabilities.md`](./capabilities.md) - Feature overview