#MT|![Build Status](https://github.com/Mi-Bee-Studio/mibee-cam/workflows/Release/badge.svg)
#MP|![Platform](https://img.shields.io/badge/platform-ESP32-blue)
#TY|![Camera](https://img.shields.io/badge/camera-OV2640-green)
#XV|![License](https://img.shields.io/badge/license-MIT-orange)
#BT|
#QT|# Troubleshooting
#HN|
#JM|This guide provides solutions for common issues encountered with the MiBee Cam firmware. Follow the systematic approach to diagnose and resolve problems.
#JT|
#QN|## Systematic Diagnosis
#TJ|
#PQ|### Step 1: Check Basic Status
#BV|```bash
#BP|# Get device status
#TR|curl http://<device-ip>/api/status
#VP|
#JZ|# Check serial monitor for boot sequence
#TX|idf.py -p COM8 monitor
#VN|```
#YQ|
#NY|### Step 2: Verify Hardware Connections
#ZP|
#NK|- Ensure camera module is properly seated
#JX|- Check TF card insertion (if using)
#PQ|- **GPIO14 shared pin**: SD card CLK and camera XCLK use same GPIO - ensure proper initialization order
#BX|- Verify power supply (5V, 1A recommended)
#BS|- Inspect USB cable and connections
#HQ|
#KX|### Step 3: Review Serial Output
#NN|Look for these key indicators:
#MZ|- "System startup complete" - Successful boot
#RY|- "Camera initialized" - Camera working
#JV|- "WiFi connected" - Network connected
#MV|- "SD card mounted" - Storage ready
#TX|
#VM|## Common Issues and Solutions
#RB|
#ZB|### 1. Build and Flash Issues
#MS|
#TY|#### Build Failures
#JW|**Problem**: `idf.py build` fails with errors
#XN|
#ZM|**Solutions**:
#BV|```bash
#MS|# Clean and rebuild
#KY|idf.py clean
#PM|idf.py build
#BY|
#YS|# Check ESP-IDF environment
#VH|echo $IDF_PATH
#PZ|
#NZ|# Verify target is correct
#PV|idf.py get-target
#BP|```
#TW|
#YY|**Common Build Errors**:
#BT|- **Target not set**: Run `idf.py set-target esp32`
#VW|- **Missing dependencies**: Run `idf.py install-deps`
#RZ|- **Syntax errors**: Check code for ESP-IDF v6.0 compatibility
#HQ|
#NH|#### Flashing Failures
#QY|**Problem**: `idf.py flash` fails or device doesn't respond
#JN|
#ZM|**Solutions**:
#BV|```bash
#SS|# Check serial port permissions (Linux)
#WM|sudo usermod -aG dialout $USER
#YP|# Then log out and back in
#KB|
#NK|# Try different baud rates
#YB|idf.py -p COM8 flash -b 115200
#SV|
#RV|# Check boot mode (should be normal, not download)
#KJ|```
#SZ|
#XP|**Common Flashing Errors**:
#KB|- **Serial port not found**: Check Device Manager or `ls /dev/tty*`
#BM|- **Permission denied**: Run with administrator/sudo
#JY|- **Connection timeout**: Try different USB cable/port
#KB|
#MW|### 2. Camera Issues
#YR|
#TK|#### Camera Initialization Failed
#RK|**Problem**: Camera fails to initialize, error messages in serial
#SR|
#PY|**Symptoms**:
#HB|- "Camera init failed" in serial output
#NM|- Black screen in web interface
#XR|- Error codes in API response
#QT|
#ZM|**Solutions**:
#BV|```bash
#MV|# Check camera module seating
#PW|# Verify camera is OV2640 (not OV3660 or other)
#HJ|# Check GPIO14 conflict (ensure SD card not in use)
#ZT|
#NM|# ESP32 DMA freeze workaround:
#RH|# - Camera initialization is deferred after WiFi connection
#WQ|# - System waits for WiFi STA connection before camera init
#XN|# - If camera fails, automatic retry up to 3 times
#ZR|```
#PJ|
#HM|**Root Causes**:
#WM|- **Wrong camera model**: Only OV2640 supported
#KW|- **Pin conflicts**: GPIO14 shared with SD card
#XV|- **PSRAM issues**: Ensure 4MB PSRAM installed
#ZN|- **I2C communication**: Check SIOD/SIOC connections
#PV|
#TJ|#### No Video Output
#BJ|**Problem**: Camera initializes but no video stream
#RM|
#ZM|**Solutions**:
#BV|```bash
#YR|# Test with different resolution
#MJ|curl -X POST http://<ip>/api/config \
#VQ|  -H "Content-Type: application/json" \
#QZ|  -d '{"resolution":0}'
#SH|```
#WV|
#HW|**Check These**:
#YR|- **Camera lens**: Clean and unobstructed
#PT|- **Lighting**: Adequate illumination (>50 lux)
#VM|- **Resolution**: Start with VGA (640x480)
#RP|- **Frame rate**: Reduce to 10 FPS if unstable
#QZ|
#RP|#### Distorted or Corrupted Video
#VZ|**Problem**: Video output is garbled or artifacts
#QX|
#NJ|**Possible Causes**:
#QR|- **Power instability**: Use 5V/1A power supply
#WT|- **Clock issues**: XCLK frequency may need adjustment
#NW|- **Memory corruption**: Check PSRAM integrity
#BV|- **USB interference**: Keep USB cables away from antenna
#WX|
#YQ|### 3. WiFi Connection Issues
#RS|
#RJ|#### Won't Connect to WiFi
#JP|**Problem**: Device fails to connect to configured WiFi network
#JM|
#XB|**Diagnostic Steps**:
#BV|```bash
#VR|# Check current WiFi state
#VP|curl http://<ip>/api/status | grep wifi
#HV|
#TW|# Serial monitor for connection attempts
#TX|idf.py -p COM8 monitor
#QW|```
#NT|
#ZM|**Solutions**:
#BV|```bash
#QX|# Verify 2.4GHz support
#ZV|# Check SSID/password accuracy
#XB|# Try closer to router
#VX|# Use WPA2 security (no WPA3)
#ZS|```
#VQ|
#PP|**Common Issues**:
#PN|- **5GHz networks**: ESP32 only supports 2.4GHz
#XH|- **Signal strength**: Move closer to router
#XJ|- **Security type**: Use WPA2-PSK (AES)
#ZY|- **Channel interference**: Use channels 1, 6, or 11
#PN|
#MB|#### AP Mode Fallback Issues
#TT|**Problem**: Device stuck in AP mode, won't connect to STA
#VK|
#ZM|**Solutions**:
#BV|```bash
#BK|# Reset configuration manually
#BT|curl -X POST http://<ip>/api/reset
#HM|
#PN|# Reconfigure via web interface
#VV|# Check WiFi credentials
#WR|```
#WS|
#QY|**Check**:
#MW|- **Router settings**: Enable 2.4GHz band
#XK|- **MAC filtering**: Allow device MAC address
#YV|- **DHCP scope**: Ensure IP available
#JY|- **WiFi password**: Special characters may cause issues
#YV|
#NV|#### Intermittent WiFi Disconnects
#JK|**Problem**: WiFi disconnects frequently
#JM|
#ZM|**Solutions**:
#WS|- **Reduce distance** from router
#RV|- **Check power supply**: Use dedicated 5V/1A
#SW|- **Disable power saving** on router
#RM|- **Update firmware** with latest WiFi driver
#ZR|
#KN|### 4. SD Card Problems
#JR|
#BV|#### SD Card Not Detected
#NJ|**Problem**: "SD card not available" in web interface
#PZ|
#XB|**Diagnostic Steps**:
#BV|```bash
#QN|# Check SD card status
#WJ|curl http://<ip>/api/status | grep storage
#XQ|
#JP|# Serial monitor for mount attempts
#KW|```
#ZK|
#ZM|**Solutions**:
#BV|```bash
#WT|# Try different SD card (Class 10 recommended)
#XX|# Format as FAT32
#VX|# Ensure proper insertion
#MB|# Check GPIO14 availability (not in use by camera)
#NB|```
#BB|
#RQ|**SD Card Requirements**:
#QZ|- **Capacity**: 8GB-32GB (Class 10+)
#MK|- **Format**: FAT32 (not exFAT)
#ZT|- **Brand**: Use reputable brands (Samsung, SanDisk, etc.)
#KS|- **Insertion**: Push until click, ensure gold contacts clean
#MK|
#KP|#### Read/Write Errors
#JZ|**Problem**: SD card mount fails during read/write operations
#XJ|
#ZM|**Solutions**:
#BV|```bash
#JZ|# Unmount and remount
#ZB|curl -X POST http://<ip>/api/reboot
#NQ|
#BT|# Try different SD card
#JP|# Check power supply stability
#YT|```
#WJ|
#PP|**Common Issues**:
#YN|- **Power fluctuations**: Use stable power supply
#HM|- **SD card quality**: Avoid counterfeit cards
#RX|- **File system corruption**: Reformat as FAT32
#PT|- **Physical damage**: Replace if connectors bent
#QW|
#RQ|#### Storage Full
#NP|**Problem**: "Storage full" errors, can't save photos
#VY|
#ZM|**Solutions**:
#BV|```bash
#RV|# Check usage
#VW|curl http://<ip>/api/status | grep free_space
#JX|
#KV|# Delete old photos via web interface
#MR|# Increase SD card capacity
#YX|# Enable automatic cleanup in config
#JS|```
#NZ|
#HQ|### 5. Motion Detection Issues
#XK|
#PH|#### No Motion Detection
#XJ|**Problem**: Motion detection doesn't trigger
#WQ|
#ZM|**Solutions**:
#BV|```bash
#KH|# Adjust threshold settings
#MJ|curl -X POST http://<ip>/api/config \
#VQ|  -H "Content-Type: application/json" \
#TH|  -d '{"motion_threshold":3}'
#ZZ|```
#QQ|
#QY|**Check**:
#QY|- **Motion threshold**: Lower values = more sensitive
#JV|- **Lighting conditions**: Ensure adequate and stable lighting
#NK|- **Camera focus**: Verify lens is focused properly
#ZX|- **Scene changes**: Large movements work better than small ones
#KQ|
#MW|#### False Triggers
#JQ|**Problem**: Motion detects movement when there shouldn't be any
#MH|
#ZM|**Solutions**:
#BV|```bash
#RY|# Increase threshold
#MJ|curl -X POST http://<ip>/api config \
#VQ|  -H "Content-Type: application/json" \
#NY|  -d '{"motion_threshold":15}'
#HJ|```
#XQ|
#BP|**Adjust**:
#TK|- **Threshold**: Increase to reduce sensitivity
#RK|- **Cooldown**: Set longer time between triggers
#RS|- **Camera settings**: Reduce resolution or frame rate
#XR|
#QN|#### Motion but No Photo
#YW|**Problem**: Motion detected but no photo saved
#RT|
#ZM|**Solutions**:
#BV|```bash
#MY|# Check motion events in serial
#NS|# Verify SD card is mounted
#MP|# Check storage space
#BT|```
#NX|
#XK|**Check Settings**:
#ZM|- **Save photos**: Enabled in motion detection config
#ZV|- **Storage space**: SD card has free space
#KM|- **Network connection**: WiFi accessible
#PN|
#JH|### 6. Web Interface Issues
#PV|
#BN|#### Web Interface Not Loading
#VS|**Problem**: Browser can't access web interface
#RN|
#ZM|**Solutions**:
#BV|```bash
#PN|# Check IP address
#MM|curl http://<ip>/api/status
#JS|
#WQ|# Try HTTP:// instead of HTTPS://
#QZ|# Clear browser cache
#WK|# Try different browser
#BT|```
#JW|
#PP|**Common Issues**:
#SR|- **Wrong IP**: Check DHCP assignment or use AP mode (192.168.4.1)
#PT|- **Browser cache**: Clear cache and cookies
#VZ|- **Firewall**: Disable temporarily to test
#TN|- **Network**: Ensure device and computer on same network
#QR|
#YN|#### Web Interface Shows Errors
#PT|**Problem**: JavaScript errors or missing content
#MB|
#ZM|**Solutions**:
#BV|```bash
#JP|# Check SPIFFS mount
#SJ|curl http://<ip>/api/status | grep spiffs
#NZ|
#RJ|# Rebuild and flash firmware
#XH|```
#XN|
#QY|**Check**:
#SP|- **SPIFFS partition**: Ensure properly flashed
#BR|- **JavaScript**: Enable in browser
#PY|- **Browser compatibility**: Use modern browsers (Chrome, Firefox, Edge)
#QB|
#KZ|#### API Not Responding
#WS|**Problem**: API endpoints return errors or no response
#PV|
#ZM|**Solutions**:
#BV|```bash
#TP|# Test basic API
#MM|curl http://<ip>/api/status
#HN|
#KJ|# Check serial monitor for errors
#SB|# Verify web server is running
#TT|```
#PR|
#TB|**API Troubleshooting**:
#WQ|- **Server running**: Check "Web server started" in serial
#BJ|- **Authentication**: Some endpoints require password
#JQ|- **CORS**: Check browser console for CORS errors
#XT|- **Content-Type**: Ensure proper headers for POST requests
#BR|
#YJ|### 7. Performance Issues
#SV|
#PS|#### Slow Performance
#JR|**Problem**: System responds slowly or lags
#MB|
#XB|**Diagnostic Steps**:
#BV|```bash
#SR|# Check memory usage
#QH|curl http://<ip>/metrics | grep heap
#MZ|
#RR|# Monitor CPU usage via serial
#KS|```
#HM|
#YT|**Optimizations**:
#BV|```bash
#TJ|# Reduce resolution
#MJ|curl -X POST http://<ip>/api config \
#VQ|  -H "Content-Type: application/json" \
#QZ|  -d '{"resolution":0}'
#XS|
#XQ|# Reduce frame rate
#MJ|curl -X POST http://<ip>/api config \
#VQ|  -H "Content-Type: application/json" \
#KX|  -d '{"fps":10}'
#XV|```
#JV|
#RW|**Performance Tips**:
#BW|- **Resolution**: Use VGA instead of higher resolutions
#HB|- **Frame rate**: Reduce to 10 FPS for better stability
#KJ|- **LED indicators**: Disable status LED for power savings
#MR|- **Background tasks**: Reduce frequency of monitoring tasks
#WV|
#PT|#### Memory Exhaustion
#YW|**Problem**: Low memory warnings or crashes
#XH|
#ZM|**Solutions**:
#BV|```bash
#ZV|# Check free heap
#BT|curl http://<ip>/metrics | grep heap_free
#MJ|
#BH|# Reduce memory usage
#SQ|# Disable unnecessary features
#KY|# Reduce frame buffer count
#BW|```
#XT|
#QQ|**Memory Management**:
#SY|- **PSRAM usage**: Monitor camera memory usage
#TM|- **Heap allocation**: Avoid large allocations in main task
#YJ|- **Stack size**: Ensure adequate stack for all tasks
#YZ|- **Garbage collection**: Free unused memory promptly
#XY|
#PY|### 8. Power and Hardware Issues
#QM|
#NV|#### Brownout Reset
#MQ|**Problem**: Device reboots unexpectedly
#ZQ|
#PY|**Symptoms**:
#HX|- "Brownout detected" in serial output
#KN|- Random reboots during operation
#RT|- Voltage drops below 3.0V
#XH|
#ZM|**Solutions**:
#BV|```bash
#VR|# Use stable 5V/1A power supply
#SK|# Check USB cable quality (use data cable, not charge-only)
#QY|# Avoid voltage spikes from other devices
#TB|```
#QS|
#PR|**Power Requirements**:
#RZ|- **Input voltage**: 5.0V ± 0.25V
#VR|- **Current**: 500mA minimum (1A recommended)
#NQ|- **USB quality**: Use USB 2.0 or higher data cables
#TX|- **Power source**: Computer USB port or dedicated adapter
#MS|
#ZM|#### Temperature Issues
#VP|**Problem**: Device overheats during operation
#HQ|
#ZM|**Solutions**:
#XR|- **Ventilation**: Ensure adequate airflow around device
#HS|- **Ambient temperature**: Keep below 40°C
#VY|- **Operation time**: Limit continuous streaming sessions
#JB|- **Power management**: Enable dynamic frequency scaling
#MQ|
#BM|#### Boot Button Issues
#XW|**Problem**: Factory reset not working via BOOT button
#SV|
#PW|**Important**: BOOT button is not functional on this firmware (GPIO0 = camera XCLK). Use these alternatives instead:
#TS|
#ZM|**Solutions**:
#KQ|- **Web interface**: Go to Configuration page → Reset to Defaults
#KN|- **API endpoint**: `curl -X POST http://<device-ip>/api/reset -H "X-Password: admin"`
#JQ|- **Serial command**: Type `reset` in serial monitor
#TS|
#YT|**Note**: GPIO0 serves as camera master clock (XCLK) and cannot be used as a general-purpose button.
#VJ|## Advanced Troubleshooting
#TH|
#RZ|### Debug Commands via Serial
#BV|```bash
#KP|# Get help
#NS|help
#HM|
#NY|# Show configuration
#WK|get config
#SH|
#YM|# Show status
#MQ|get status
#PW|
#YP|# Reboot
#XZ|reboot
#MX|
#QB|# Factory reset
#YS|reset
#XV|
#RW|# Get WiFi info
#RW|get wifi
#VB|
#BV|# Get memory usage
#NT|get memory
#NT|```
#BV|
#YH|### Log Analysis
#WJ|Check these log patterns in serial output:
#KB|
#RJ|**Normal Boot Sequence**:
#SM|```
#ZW|I (1234) main: Boot step 1: NVS flash initialization
#YM|I (1235) main: Boot step 2: Configuration load from NVS
#TK|...
#ZM|I (2345) main: System startup complete
#BZ|```
#HK|
#YR|**Camera Issues**:
#NR|```
#TT|E (3456) camera: Camera initialization failed
#YN|E (3457) camera: I2C communication error
#QQ|```
#NT|
#MK|**WiFi Issues**:
#HV|```
#PH|E (4567) wifi: Connection failed - Authentication failed
#JH|W (4568) wifi: Auto-reconnect in 5 seconds
#SW|```
#MW|
#HV|### Performance Monitoring
#BV|```bash
#NX|# Monitor system metrics
#KX|curl http://<ip>/metrics
#RJ|
#WM|# Check CPU temperature (if available)
#JB|curl http://<ip>/api/status | grep temperature
#MV|
#SY|# Monitor memory usage
#BS|curl http://<ip>/api/status | grep heap
#QB|```
#MK|
#NX|### Factory Reset Procedure
#SQ|1. **Via Web Interface**: 
#XV|   - Go to Configuration page
#HZ|   - Click "Reset to Defaults"
#HW|   - Confirm and reboot
#TH|
#JQ|2. **Via Serial**:
#PJ|   ```bash
#RV|   reset
#WV|   ```
#PH|
#JS|3. **Via Boot Button**:
#VJ|   - Hold BOOT button for 5 seconds
#NB|   - Watch for LED pattern (fast blink)
#MX|   - Release when LED changes pattern
#JR|
#JZ|### Creating Bug Reports
#BV|When reporting issues, include:
#YN|1. **System Info**: Firmware version, hardware version
#BS|2. **Serial Output**: Full boot sequence and error messages
#PN|3. **Network Info**: WiFi SSID, signal strength, IP address
#PK|4. **Configuration**: Current settings (can redact sensitive info)
#TH|5. **Reproduction Steps**: What you did when problem occurred
#QH|6. **Expected vs Actual**: What should happen vs what happened
#BR|
#BR|## Contact Support
#JV|
#ZX|If you continue to experience issues:
#PH|1. Check [GitHub Issues](https://github.com/Mi-Bee-Studio/mibee-cam/issues) for similar problems
#RK|2. Search [Documentation](../) for additional troubleshooting guides
#PR|3. Create a new issue with detailed information as above
#HV|4. Include serial logs and configuration dumps if possible