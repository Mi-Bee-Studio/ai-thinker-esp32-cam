> 🌐 [中文文档](../zh/troubleshooting.md)

# Troubleshooting

This guide provides solutions for common issues encountered with the AI_Thinker ESP32-CAM firmware. Follow the systematic approach to diagnose and resolve problems.

## Systematic Diagnosis

### Step 1: Check Basic Status
```bash
# Get device status
curl http://<device-ip>/api/status

# Check serial monitor for boot sequence
idf.py -p COM8 monitor
```

### Step 2: Verify Hardware Connections
- Ensure camera module is properly seated
- Check TF card insertion (if using)
- Verify power supply (5V, 1A recommended)
- Inspect USB cable and connections

### Step 3: Review Serial Output
Look for these key indicators:
- "System startup complete" - Successful boot
- "Camera initialized" - Camera working
- "WiFi connected" - Network connected
- "SD card mounted" - Storage ready

## Common Issues and Solutions

### 1. Build and Flash Issues

#### Build Failures
**Problem**: `idf.py build` fails with errors

**Solutions**:
```bash
# Clean and rebuild
idf.py clean
idf.py build

# Check ESP-IDF environment
echo $IDF_PATH

# Verify target is correct
idf.py get-target
```

**Common Build Errors**:
- **Target not set**: Run `idf.py set-target esp32`
- **Missing dependencies**: Run `idf.py install-deps`
- **Syntax errors**: Check code for ESP-IDF v6.0 compatibility

#### Flashing Failures
**Problem**: `idf.py flash` fails or device doesn't respond

**Solutions**:
```bash
# Check serial port permissions (Linux)
sudo usermod -aG dialout $USER
# Then log out and back in

# Try different baud rates
idf.py -p COM8 flash -b 115200

# Check boot mode (should be normal, not download)
```

**Common Flashing Errors**:
- **Serial port not found**: Check Device Manager or `ls /dev/tty*`
- **Permission denied**: Run with administrator/sudo
- **Connection timeout**: Try different USB cable/port

### 2. Camera Issues

#### Camera Initialization Failed
**Problem**: Camera fails to initialize, error messages in serial

**Symptoms**:
- "Camera init failed" in serial output
- Black screen in web interface
- Error codes in API response

**Solutions**:
```bash
# Check camera module seating
# Verify camera is OV2640 (not OV3660 or other)
# Check GPIO14 conflict (ensure SD card not in use)
```

**Root Causes**:
- **Wrong camera model**: Only OV2640 supported
- **Pin conflicts**: GPIO14 shared with SD card
- **PSRAM issues**: Ensure 4MB PSRAM installed
- **I2C communication**: Check SIOD/SIOC connections

#### No Video Output
**Problem**: Camera initializes but no video stream

**Solutions**:
```bash
# Test with different resolution
curl -X POST http://<ip>/api/config \
  -H "Content-Type: application/json" \
  -d '{"resolution":0}'
```

**Check These**:
- **Camera lens**: Clean and unobstructed
- **Lighting**: Adequate illumination (>50 lux)
- **Resolution**: Start with VGA (640x480)
- **Frame rate**: Reduce to 10 FPS if unstable

#### Distorted or Corrupted Video
**Problem**: Video output is garbled or artifacts

**Possible Causes**:
- **Power instability**: Use 5V/1A power supply
- **Clock issues**: XCLK frequency may need adjustment
- **Memory corruption**: Check PSRAM integrity
- **USB interference**: Keep USB cables away from antenna

### 3. WiFi Connection Issues

#### Won't Connect to WiFi
**Problem**: Device fails to connect to configured WiFi network

**Diagnostic Steps**:
```bash
# Check current WiFi state
curl http://<ip>/api/status | grep wifi

# Serial monitor for connection attempts
idf.py -p COM8 monitor
```

**Solutions**:
```bash
# Verify 2.4GHz support
# Check SSID/password accuracy
# Try closer to router
# Use WPA2 security (no WPA3)
```

**Common Issues**:
- **5GHz networks**: ESP32 only supports 2.4GHz
- **Signal strength**: Move closer to router
- **Security type**: Use WPA2-PSK (AES)
- **Channel interference**: Use channels 1, 6, or 11

#### AP Mode Fallback Issues
**Problem**: Device stuck in AP mode, won't connect to STA

**Solutions**:
```bash
# Reset configuration manually
curl -X POST http://<ip>/api/reset

# Reconfigure via web interface
# Check WiFi credentials
```

**Check**:
- **Router settings**: Enable 2.4GHz band
- **MAC filtering**: Allow device MAC address
- **DHCP scope**: Ensure IP available
- **WiFi password**: Special characters may cause issues

#### Intermittent WiFi Disconnects
**Problem**: WiFi disconnects frequently

**Solutions**:
- **Reduce distance** from router
- **Check power supply**: Use dedicated 5V/1A
- **Disable power saving** on router
- **Update firmware** with latest WiFi driver

### 4. SD Card Problems

#### SD Card Not Detected
**Problem**: "SD card not available" in web interface

**Diagnostic Steps**:
```bash
# Check SD card status
curl http://<ip>/api/status | grep storage

# Serial monitor for mount attempts
```

**Solutions**:
```bash
# Try different SD card (Class 10 recommended)
# Format as FAT32
# Ensure proper insertion
# Check GPIO14 availability (not in use by camera)
```

**SD Card Requirements**:
- **Capacity**: 8GB-32GB (Class 10+)
- **Format**: FAT32 (not exFAT)
- **Brand**: Use reputable brands (Samsung, SanDisk, etc.)
- **Insertion**: Push until click, ensure gold contacts clean

#### Read/Write Errors
**Problem**: SD card mount fails during read/write operations

**Solutions**:
```bash
# Unmount and remount
curl -X POST http://<ip>/api/reboot

# Try different SD card
# Check power supply stability
```

**Common Issues**:
- **Power fluctuations**: Use stable power supply
- **SD card quality**: Avoid counterfeit cards
- **File system corruption**: Reformat as FAT32
- **Physical damage**: Replace if connectors bent

#### Storage Full
**Problem**: "Storage full" errors, can't save photos

**Solutions**:
```bash
# Check usage
curl http://<ip>/api/status | grep free_space

# Delete old photos via web interface
# Increase SD card capacity
# Enable automatic cleanup in config
```

### 5. Motion Detection Issues

#### No Motion Detection
**Problem**: Motion detection doesn't trigger

**Solutions**:
```bash
# Adjust threshold settings
curl -X POST http://<ip>/api/config \
  -H "Content-Type: application/json" \
  -d '{"motion_threshold":3}'
```

**Check**:
- **Motion threshold**: Lower values = more sensitive
- **Lighting conditions**: Ensure adequate and stable lighting
- **Camera focus**: Verify lens is focused properly
- **Scene changes**: Large movements work better than small ones

#### False Triggers
**Problem**: Motion detects movement when there shouldn't be any

**Solutions**:
```bash
# Increase threshold
curl -X POST http://<ip>/api/config \
  -H "Content-Type: application/json" \
  -d '{"motion_threshold":15}'
```

**Adjust**:
- **Threshold**: Increase to reduce sensitivity
- **Cooldown**: Set longer time between triggers
- **Camera settings**: Reduce resolution or frame rate

#### Motion but No Photo/Upload
**Problem**: Motion detected but no photo saved or uploaded

**Solutions**:
```bash
# Check motion events in serial
# Verify SD card is mounted
# Check NAS configuration
# Test upload manually
```

**Check Settings**:
- **Save photos**: Enabled in motion detection config
- **Upload enabled**: NAS upload configured
- **Storage space**: SD card has free space
- **Network connection**: WiFi and NAS server accessible

### 6. Web Interface Issues

#### Web Interface Not Loading
**Problem**: Browser can't access web interface

**Solutions**:
```bash
# Check IP address
curl http://<ip>/api/status

# Try HTTP:// instead of HTTPS://
# Clear browser cache
# Try different browser
```

**Common Issues**:
- **Wrong IP**: Check DHCP assignment or use AP mode (192.168.4.1)
- **Browser cache**: Clear cache and cookies
- **Firewall**: Disable temporarily to test
- **Network**: Ensure device and computer on same network

#### Web Interface Shows Errors
**Problem**: JavaScript errors or missing content

**Solutions**:
```bash
# Check SPIFFS mount
curl http://<ip>/api/status | grep spiffs

# Rebuild and flash firmware
```

**Check**:
- **SPIFFS partition**: Ensure properly flashed
- **JavaScript**: Enable in browser
- **Browser compatibility**: Use modern browsers (Chrome, Firefox, Edge)

#### API Not Responding
**Problem**: API endpoints return errors or no response

**Solutions**:
```bash
# Test basic API
curl http://<ip>/api/status

# Check serial monitor for errors
# Verify web server is running
```

**API Troubleshooting**:
- **Server running**: Check "Web server started" in serial
- **Authentication**: Some endpoints require password
- **CORS**: Check browser console for CORS errors
- **Content-Type**: Ensure proper headers for POST requests

### 7. Performance Issues

#### Slow Performance
**Problem**: System responds slowly or lags

**Diagnostic Steps**:
```bash
# Check memory usage
curl http://<ip>/metrics | grep heap

# Monitor CPU usage via serial
```

**Optimizations**:
```bash
# Reduce resolution
curl -X POST http://<ip>/api/config \
  -H "Content-Type: application/json" \
  -d '{"resolution":0}'

# Reduce frame rate
curl -X POST http://<ip>/api/config \
  -H "Content-Type: application/json" \
  -d '{"fps":10}'
```

**Performance Tips**:
- **Resolution**: Use VGA instead of higher resolutions
- **Frame rate**: Reduce to 10 FPS for better stability
- **LED indicators**: Disable status LED for power savings
- **Background tasks**: Reduce frequency of monitoring tasks

#### Memory Exhaustion
**Problem**: Low memory warnings or crashes

**Solutions**:
```bash
# Check free heap
curl http://<ip>/metrics | grep heap_free

# Reduce memory usage
# Disable unnecessary features
# Reduce frame buffer count
```

**Memory Management**:
- **PSRAM usage**: Monitor camera memory usage
- **Heap allocation**: Avoid large allocations in main task
- **Stack size**: Ensure adequate stack for all tasks
- **Garbage collection**: Free unused memory promptly

### 8. Power and Hardware Issues

#### Brownout Reset
**Problem**: Device reboots unexpectedly

**Symptoms**:
- "Brownout detected" in serial output
- Random reboots during operation
- Voltage drops below 3.0V

**Solutions**:
```bash
# Use stable 5V/1A power supply
# Check USB cable quality (use data cable, not charge-only)
# Avoid voltage spikes from other devices
```

**Power Requirements**:
- **Input voltage**: 5.0V ± 0.25V
- **Current**: 500mA minimum (1A recommended)
- **USB quality**: Use USB 2.0 or higher data cables
- **Power source**: Computer USB port or dedicated adapter

#### Temperature Issues
**Problem**: Device overheats during operation

**Solutions**:
- **Ventilation**: Ensure adequate airflow around device
- **Ambient temperature**: Keep below 40°C
- **Operation time**: Limit continuous streaming sessions
- **Power management**: Enable dynamic frequency scaling

#### Boot Button Issues
**Problem**: Factory reset not working

**Solutions**:
- **Timing**: Hold button for exactly 5 seconds
- **Boot sequence**: Press during boot, not while running
- **Physical**: Ensure button contacts clean
- **Test**: Use `reset` command via serial if needed

## Advanced Troubleshooting

### Debug Commands via Serial
```bash
# Get help
help

# Show configuration
get config

# Show status
get status

# Reboot
reboot

# Factory reset
reset

# Get WiFi info
get wifi

# Get memory usage
get memory
```

### Log Analysis
Check these log patterns in serial output:

**Normal Boot Sequence**:
```
I (1234) main: Boot step 1: NVS flash initialization
I (1235) main: Boot step 2: Configuration load from NVS
...
I (2345) main: System startup complete
```

**Camera Issues**:
```
E (3456) camera: Camera initialization failed
E (3457) camera: I2C communication error
```

**WiFi Issues**:
```
E (4567) wifi: Connection failed - Authentication failed
W (4568) wifi: Auto-reconnect in 5 seconds
```

### Performance Monitoring
```bash
# Monitor system metrics
curl http://<ip>/metrics

# Check CPU temperature (if available)
curl http://<ip>/api/status | grep temperature

# Monitor memory usage
curl http://<ip>/api/status | grep heap
```

### Factory Reset Procedure
1. **Via Web Interface**: 
   - Go to Configuration page
   - Click "Reset to Defaults"
   - Confirm and reboot

2. **Via Serial**:
   ```bash
   reset
   ```

3. **Via Boot Button**:
   - Hold BOOT button for 5 seconds
   - Watch for LED pattern (fast blink)
   - Release when LED changes pattern

### Creating Bug Reports
When reporting issues, include:
1. **System Info**: Firmware version, hardware version
2. **Serial Output**: Full boot sequence and error messages
3. **Network Info**: WiFi SSID, signal strength, IP address
4. **Configuration**: Current settings (can redact sensitive info)
5. **Reproduction Steps**: What you did when problem occurred
6. **Expected vs Actual**: What should happen vs what happened

## Contact Support

If you continue to experience issues:
1. Check [GitHub Issues](https://github.com/Mi-Bee-Studio/ai-thinker-esp32-cam/issues) for similar problems
2. Search [Documentation](../) for additional troubleshooting guides
3. Create a new issue with detailed information as above
4. Include serial logs and configuration dumps if possible