#RJ|> 🌐 [中文文档](../zh/lens-and-fov.md)
#KM|
#SQ|# Lens and Field of View (FOV)
#RW|
#YQ|This document explains the OV2640 camera module's physical lens parameters, field of view, "view" controls in firmware, and how to extend wide-angle/macro capabilities.
#SY|
#VW|> **Key fact**: The firmware has **no** software zoom/focus/aperture control. Lens focal length is a **physical property** — swapping lenses requires no code changes.
#XW|
#RM|## OV2640 Sensor Physical Parameters
#SK|
#BN|| Parameter | Value |
#ZS||-----------|-------|
#WP|| Sensor size | **1/4 inch** (~3.6mm diagonal) |
#VN|| Pixels | 2MP (1600×1200 UXGA) |
#JB|| Pixel size | 2.2µm × 2.2µm |
#NT|| Dynamic range | 50 dB |
#VM|| SNR | 40 dB |
#JZ|| Output format | **JPEG** (compressed by OV2640 internal ISP) |
#RB|| Sensitivity | 0.6 V/Lux-sec |
#YQ|
#BP|> ⚠️ Note: `camera_driver.c` does **NOT** call AEC (auto exposure) / AGC (auto gain) / AWB (auto white balance) / LENC (lens shading correction) register configuration. OV2640 runs on factory defaults, which may cause overexposure or color cast in some lighting conditions.
#ZP|
#BV|## Hardcoded Camera Config in Firmware
#KW|
#MB|```c
#TH|// main/camera_driver.c
#XQ|.xclk_freq_hz = 20000000,        // 20MHz master clock
#QQ|.pixel_format = PIXFORMAT_JPEG,   // Hardcoded JPEG output
#MR|.fb_location  = CAMERA_FB_IN_PSRAM,
#ZZ|.grab_mode    = CAMERA_GRAB_LATEST,
#VK|.fb_count     = 2                 // Dual buffer
#VP|```
#QY|
#NK|Camera controls **not exposed** by firmware:
#PW|- ❌ Focus control (OV2640 doesn't support)
#KM|- ❌ Exposure compensation
#WX|- ❌ White balance modes
#XP|- ❌ Gain
#RH|- ❌ Lens shading correction (LENC)
#QM|- ❌ Digital zoom ROI
#TT|- ❌ Horizontal flip (only `vflip` for vertical)
#BY|- ❌ Sharpness / noise reduction / color matrix tuning
#QB|
#XZ|## Stock MiBee Lens
#KT|
#TN|The stock lens on MiBee Cam is an **M12 mount fixed-focus lens** (unscrewable but requires desoldering):
#VJ|
#MW|| Parameter | Typical Value |
#VH||-----------|---------------|
#ZX|| Focal length | **3.6mm** (also available in 2.8mm / 6mm variants) |
#YT|| Aperture | F2.0 |
#XP|| **Field of View (FOV)** | **60-68° horizontal** (normal/standard range) |
#QQ|| IR-cut filter | Yes (650nm IR-cut) |
#SX|| Minimum focus distance | **~60cm** |
#RP|| Maximum | Theoretically infinite |
#YT|| Distortion | ~3% barrel (not severe) |
#NV|| Autofocus | **No** |
#NH|| Mount | M12 × 0.5 (standard) |
#QH|
#HB|> The stock lens is **normal focal length** — neither wide-angle (typically >90°) nor macro (typically <10cm min focus).
#VW|
#JK|## FOV Variation by Resolution
#JN|
#PB|OV2640 uses **internal ISP scaling/windowing** for lower resolutions — not software cropping. This means **lower resolution → narrower FOV**:
#PZ|
#WT|| Selected | FOV Relative | FOV Absolute (3.6mm lens) | Equivalent Behavior |
#MB||----------|--------------|----------------------------|---------------------|
#WM|| **UXGA** | 100% | ~68° horizontal | Widest (full sensor) |
#WY|| XGA | ~81% | ~55° | Center crop |
#PW|| SVGA | ~63% | ~43° | Medium |
#QX|| **VGA** | **50%** | **~34°** | **2× digital zoom effect** |
#SV|
#PX>⚠️ Counter-intuitive: **choosing lower resolution → narrower FOV**. For widest FOV, you must select UXGA.
#HQ|
#KN>## Wide-Angle Capability
#JW>
#TQ>### Software Cannot Do It
#PX>
#YM>The firmware has no FOV / focal length / wide-angle config. `cam_config_t` does not contain these fields. Wide-angle **requires physical lens replacement**.
#KB>
#VQ>### Lens Selection Reference
#YR>
#VX|| Focal Length | FOV (diagonal) | FOV (horizontal, 4:3) | Distortion | Price | Use |
#VX||--------------|----------------|------------------------|------------|-------|-----|
#BK|| **1.6mm F2.0** | ~180° | ~160° | Severe fisheye | $1-2 | Extreme wide-angle |
#HJ|| **2.1mm F2.0** | ~145° | ~130° | Moderate fisheye | $1-2 | Wide-angle monitoring |
#YK|| **2.8mm F2.0** | ~110° | ~95° | Slight distortion | $1-2 | Mild wide-angle |
#PV|| **3.6mm F2.0** | ~78° | ~68° | ~3% | $1 (**stock**) | Standard |
#ZM|| **6mm F2.0** | ~50° | ~42° | Negligible | $1-2 | Telephoto |
#RY|| **8mm F2.0** | ~38° | ~32° | Negligible | $1-3 | Long telephoto |
#XT|| **12mm F2.0** | ~25° | ~21° | Negligible | $2-3 | Super telephoto |
#JZ|
#ZH>**M12 mount is universal**, but requires desoldering the old lens. **Firmware needs no changes** — OV2640 auto-adapts to any M12 focal length.
#MS>
#ZS>### Wide-Angle Side Effects
#ZT>
#TT>1. **Barrel distortion**: Worse with shorter focal length; 1.6mm fisheye needs software correction
#PV>2. **Vignetting**: Insufficient peripheral light, corners darken
#PP>3. **Edge sharpness drops**: Limited by lens optical resolution
#PY>4. **Min focus distance unchanged**: Still ~60cm (limited by lens structure)
#TN>5. **Depth of field changes**: Wide-angle has deeper DOF, telephoto has shallower
#PJ>
#YT>## Macro Capability
#NJ>
#HH>### Stock Lens Macro Limitations
#HT>
#SM>- Minimum focus distance **~60cm**
#ST>- Subject < 30cm will be blurry (fixed focus is there)
#ZQ>- Want 5-10cm distance detail → **impossible**
#WY>
#QY>### Solutions (All Hardware)
#QJ>
#TK|| Solution | Cost | Difficulty | Result |
#XK||----------|------|------------|--------|
#PQ|| Swap long-focal lens (6/8/12mm) | $1-3 | Medium (desolder) | Min focus 20-50cm |
#ZQ|| Stack close-up lens (+1/+2/+4 diopter) | $1-4 | Easy (screw on) | Min focus 5-15cm |
#BS|| Reverse-mount lens | Free | Easy | Sacrifices aperture, min focus 1-3cm |
#RN|| Add extension tube | $1 | Easy | Min focus reduced 50% |
#YR>
#TN>**Close-up lens diopter conversion**:
#VV>- **+1 diopter** → Min focus ~100cm
#WS>- **+2 diopter** → Min focus ~50cm
#ZN>- **+4 diopter** → Min focus ~25cm
#RJ>- **+10 diopter** → Min focus ~10cm (true macro)
#YB>
#KY>### Software Pseudo-Macro
#XB>
#BB>- Use UXGA full resolution + post-crop center 1/4 → equivalent 2× digital zoom
#BN>- **But this is software zoom, essentially interpolation, quality degrades**
#TQ>- Firmware has **no** ROI (region of interest) / digital zoom interface exposed to user
#WP>
#KN>## IR / Night Vision Versions
#BM>
#QZ|| Lens Type | Daytime | Nighttime | With IR LEDs |
#BQ||-----------|---------|-----------|---------------|
#QM|| **IR-cut 650nm** (**stock**) | Normal color | Pitch black | No |
#ZP|| **No IR-cut** (IR lens) | Red cast (needs software correction) | Can see IR scenes | ✅ Required |
#KK>
#XN>### IR Lens + 850nm IR LEDs
#XS>
#SW>- The firmware's GPIO4 "flash LED" is actually a **LEDC PWM white LED**
#VK>- IR LEDs need separate GPIO and hardware, **firmware does not directly support**
#HV>- But GPIO4 can be used to drive white light for fill (no IR effect)
#BT>
#PZ>## Lens Replacement Procedure
#JM>
#NT>⚠️ Requires soldering iron. Beginners please be careful.
#SS>
#WT>1. **Remove case**: MiBee board has plastic case
#BT>2. **Remove stock lens**: M12 mount, rotate counter-clockwise
#HH>3. **Desolder 22pF capacitor on lens mount** (some mounts have it)
#MN>4. **Solder new lens**: Align threads carefully
#HM>5. **Adjust focus**: Power on temporarily, rotate lens until image is sharpest
#KK>6. **Secure**: Apply UV glue or hot glue
#NT>
#YP>**Zero firmware changes** — OV2640 auto-adapts.
#HJ>
#QZ>## Other Lens-Related Phenomena
#XK>
#ZP>### Vignetting
#KY>- Shorter focal length (wide-angle) → more peripheral vignetting
#SP>- UXGA full resolution on 3.6mm stock lens → slight corner darkening
#TX>- Optical phenomenon, **LENC correction not enabled** (`camera_driver.c` doesn't call `set_lenc`)
#SK>
#TK>### IR Reflection
#KH>- Strong sunlight on glass / water surface → possible IR artifacts
#PZ>- IR-cut lens filters 650nm+, **should eliminate this**
#BT>
#BK>### Dust
#QX>- Dust inside M12 mount → black dots in image
#BR>- Be careful about dust when removing lens
#VK>
#WQ>## Summary
#RT>
#TH|| Dimension | Current State |
#ZX||-----------|---------------|
#WP|| Wide-angle support | ❌ No software; only via 1.6-2.8mm physical lens swap |
#YW|| Macro support | ❌ No software; only via long-focal or close-up lens |
#WN|| FOV (default) | 60-68° horizontal (3.6mm stock) |
#ZH|| FOV (lowest res VGA) | **~34° horizontal** (2× zoom effect) |
#PZ|| FOV (highest res UXGA) | **~68° horizontal** (widest) |
#PV|| Digital zoom | ❌ No ROI interface in firmware |
#NK|| Autofocus | ❌ OV2640 doesn't support |
#KR|| Distortion correction (LENC) | ❌ Not enabled |
#ZR|| Mirror flip | ✅ `vflip` config |
#PZ|| 180° rotation | ❌ Vertical flip only |
#SK|| IR lens | ⚠️ Hardware supported, no IR LED control in firmware |
#XH>
#RJ>**In short**: The firmware is a "dumb camera driver" for OV2640. For wide-angle or macro, just spend $1-3 on a new M12 lens and screw it on — **zero code changes needed**.
#JM>
#NH>## Related Documentation
#KM>
#TT>- [`hardware.md`](./hardware.md) - Board-level hardware specs
#WX|- [`performance.md`](./performance.md) - Resolution vs FPS
#KB|- [`capabilities.md`](./capabilities.md) - Feature overview