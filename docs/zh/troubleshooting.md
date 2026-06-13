> 🌐 [English Documentation](../en/troubleshooting.md)

# 故障排除指南

本指南提供 MiBee Cam 固件的常见问题诊断和解决方案。按照分步说明解决连接、摄像头、存储和网络问题。

## 快速诊断

### 基本检查清单

在开始详细故障排除之前，请检查这些基本项目：

- ✅ 电源供应：5V USB 电源，电流至少 1A
- ✅ 连接：USB 线缆牢固，无松动
- ✅ SD 卡：已正确插入并格式化为 FAT32
- ✅ 摄像头模块：已正确安装且引脚无损坏
- ✅ 网络路由器：2.4GHz WiFi 支持，WPA/WPA2 加密

### LED 状态诊断

使用状态 LED 进行快速故障诊断：

| LED 状态 | 可能原因 | 推荐操作 |
|---------|---------|---------|
| **常亮红色** | 摄像头初始化失败 | 检查摄像头连接 |
| **快速闪烁 (0.5秒)** | WiFi 连接失败 | 检查 WiFi 设置 |
**慢速闪烁 (2秒)** | AP 模式激活 | 连接到 MiBeeCam |
| **红色闪烁 (0.2秒亮/0.8秒灭)** | 系统错误 | 查看串口日志 |
| **无指示灯** | 电源问题 | 检查 USB 电源 |

## 构建和烧录问题

### 构建失败

#### 问题：找不到 ESP-IDF 工具链

**错误消息**：
```
Command "xtensa-esp32-elf-gcc" not found
```

**解决方案**：
1. 验证 ESP-IDF 安装：
   ```bash
   idf.py --version
   ```

2. 如果命令未找到，运行环境设置脚本：
   ```bash
   # Windows
   C:\Espressif\esp-idf> export.bat
   
   # Linux/Mac
   source ~/esp/esp-idf/export.sh
   ```

3. 重启命令提示符/终端后再试

#### 问题：编译错误 - 目标未设置

**错误消息**：
```
Error: Target not set. Please use `idf.py set-target esp32`
```

**解决方案**：
```bash
idf.py set-target esp32
idf.py build
```

#### 问题：ESP-IDF 版本不兼容

**错误消息**：
```
Error: This project requires ESP-IDF v6.0 or later
```

**解决方案**：
1. 检查 ESP-IDF 版本：
   ```bash
   idf.py --version
   ```

2. 如果版本过低，下载并安装 v6.0：
   ```bash
   # 删除旧版本
   rm -rf ~/esp/esp-idf
   
   # 下载 ESP-IDF v6.0
   cd ~
   mkdir esp
   cd esp
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   git checkout v6.0
   
   # 设置环境
   ./install.sh all
   source export.sh
   ```

#### 问题：依赖项下载失败

**错误消息**：
```
Error: Failed to download component...
```

**解决方案**：
1. 检查网络连接
2. 手动下载依赖项：
   ```bash
   # 进入项目目录
   cd mibee-cam
   
   # 清理并重新构建
   idf.py clean
   idf.py build -v  # 详细输出
   ```

3. 如果问题持续，使用本地缓存：

### 烧录问题

#### 问题：找不到串口设备

**错误消息**：
```
Error: Serial port COM8 not found
```

**解决方案**：

**Windows**：
1. 打开设备管理器
2. 检查 "端口 (COM 和 LPT)" 下的 COM 端口
3. 如果未显示，安装 USB 驱动程序
4. 更新串口号：`idf.py -p COM9 flash monitor`

**Linux**：
```bash
# 查找设备
ls /dev/ttyUSB*
ls /dev/ttyACM*

# 常见设备
# /dev/ttyUSB0 - CP2102/CH340 适配器
# /dev/ttyACM0 - 原生 USB 连接

# 使用正确的设备
idf.py -p /dev/ttyUSB0 flash monitor
```

**Mac**：
```bash
# 查找设备
ls /dev/tty.usbserial-*
ls /dev/tty.usbmodem*

# 常见设备
# /dev/tty.usbserial-XXXX - CP2102
# /dev/tty.usbmodemXXXX - FTDI

# 使用正确的设备
idf.py -p /dev/tty.usbserial-XXXX flash monitor
```

#### 问题：权限被拒绝

**错误消息**：
```
Error: Permission denied
```

**解决方案**：

**Linux/Mac**：
```bash
# 添加用户到 dialout 组
sudo usermod -a -G dialout $USER

# 或设置串口权限
sudo chmod a+rw /dev/ttyUSB0

# 或使用 sudo
sudo idf.py -p /dev/ttyUSB0 flash monitor
```

**Windows**：
1. 以管理员身份运行命令提示符
2. 或重新安装 USB 驱动程序

#### 问题：烧录超时

**错误消息**：
```
Error: Flash timeout
```

**解决方案**：
1. **连接问题**：更换 USB 电缆，确保 5V 电源
2. **引脚问题**：确保 BOOT 按钮未卡住
3. **重试**：再次尝试烧录
4. **手动引导**：
   - 按住 BOOT 按钮
   - 按下 RST 按钮
   - 释放 BOOT 按钮
   - 释放 RST 按钮
   - 立即运行烧录命令

#### 问题：无法进入下载模式

**症状**：设备持续运行，不接受烧录

**解决方案**：
```bash
# 强制进入下载模式
idf.py -p /dev/ttyUSB0 --before default_reset --after hard_reset flash monitor
```

或使用 esptool.py：
```bash
# 查找串口
esptool.py --port COM8 list_ports

# 烧录固件
esptool.py --port COM8 --baud 460800 write_flash 0x10000 build/mibee-cam.bin
```

## WiFi 连接问题

### AP 模式连接

#### 问题：无法连接到 "MiBeeCam" 网络

**症状**：WiFi 网络列表中未显示 MiBeeCam

**解决方案**：
1. **信号强度**：确保设备距离路由器 < 10 米
2. **2.4GHz 要求**：仅支持 2.4GHz 频段
3. **信道冲突**：切换到不同信道：
   ```bash
   # 重置设备到 AP 模式
   curl -X POST http://192.168.4.1/api/reboot
   ```

4. **电源不足**：使用 5V/1A 电源适配器

#### 问题：连接后无互联网访问

**症状**：连接到 MiBeeCam 但无法访问浏览器

**解决方案**：
1. **IP 地址**：尝试手动访问：
   ```bash
   # 或 192.168.4.1
   http://192.168.4.1/
   ```

2. **浏览器兼容性**：使用 Chrome、Firefox 或 Edge
3. **防火墙**：暂时禁用本地防火墙
4. **DNS 问题**：设置静态 IP 或使用 IP 地址访问

### STA 模式连接

#### 问题：WiFi 连接失败

**症状**：设备启动但不连接到 WiFi，停留在 AP 模式

**解决方案**：

**检查 WiFi 设置**：
```bash
# 扫描可用网络
curl -s http://192.168.4.1/api/config/wifi/scan | jq .
```

**验证凭据**：
- **SSID**：确保网络名称正确（区分大小写）
- **密码**：验证 WPA2 密码
- **隐藏网络**：如果隐藏，使用完整 SSID

**重置配置**：
```bash
# 通过串口重置
# 连接到 COM8，波特率 115200
# 发送命令：reset wifi
```

#### 问题：连接后频繁断开

**症状**：WiFi 连接建立但频繁断开

**解决方案**：
1. **信号强度**：
   ```bash
   # 检查信号强度
   curl -s http://192.168.1.100/api/status | jq '.wifi.signal'
   ```

   - < -60dBm：弱信号，移近路由器
   - -60 到 -80dBm：中等信号
   - > -80dBm：重新定位设备

2. **信道干扰**：在路由器设置中固定信道
3. **电源问题**：使用 5V/2A 电源
4. **固件更新**：确保使用最新固件

#### 问题：无法获取 IP 地址

**症状**：连接 WiFi 但无 IP 地址

**解决方案**：
1. **DHCP 问题**：重启路由器
2. **静态 IP**：配置静态 IP：
   ```json
   {
     "wifi": {
       "mode": "STA",
       "ssid": "MyNetwork",
       "password": "MyPassword",
       "static_ip": "192.168.1.150",
       "gateway": "192.168.1.1",
       "netmask": "255.255.255.0"
     }
   }
   ```

3. **MAC 地址冲突**：配置不同的设备名称

## 摄像头问题

### 摄像头初始化失败

#### 问题：无图像输出

**症状**：Web 界面显示黑屏或灰色区域

**解决方案**：

**硬件检查**：
1. **摄像头连接**：
   - 重新安装摄像头模块
   - 检查排线是否牢固
   - 确保摄像头类型为 OV2640

2. **PSRAM 问题**：
   - 某些 ESP32-CAM 版本没有 PSRAM
   - 检查型号是否支持 PSRAM
   - 如果无 PSRAM，仅支持低分辨率

3. **电压问题**：
   - 确保电源电压稳定在 5V
   - 使用高质量的 USB 电源

**软件配置**：
```bash
# 检查摄像头状态
curl -s http://192.168.1.100/api/camera/status | jq .
```

**重置摄像头**：
```bash
# 通过 Web 界面重置
# 或通过串口发送：reset camera
```

#### 问题：图像质量差

**症状**：图像模糊、噪点多或色彩异常

**解决方案**：
1. **调整分辨率**：
   - 从 VGA (640x480) 开始
   - 逐步提高分辨率

2. **调整 JPEG 质量**：
   ```json
   {
     "camera": {
       "jpeg_quality": 12  // 1-63，值越低质量越好
     }
   }
   ```

3. **镜头清洁**：使用无绒布清洁摄像头镜头
4. **环境光线**：确保充足的光线

### 运动检测问题

#### 问题：运动检测过于灵敏

**症状**：轻微移动就触发事件

**解决方案**：
1. **提高阈值**：
   ```json
   {
     "motion": {
       "threshold": 15  // 1-255，值越高越不灵敏
     }
   }
   ```

2. **调整检测区域**：
   - 检测画面中心区域
   - 减少背景复杂度

3. **设置冷却时间**：
   ```json
   {
     "motion": {
       "cooldown_time": 30  // 秒
     }
   }
   ```

#### 问题：运动检测不灵敏

**症状**：明显移动但不触发

**解决方案**：
1. **降低阈值**：
   ```json
   {
     "motion": {
       "threshold": 3  // 1-255，值越低越灵敏
     }
   }
   ```

2. **改善照明**：确保检测区域光线充足
3. **减少背景**：移除快速移动的背景元素
4. **调整帧率**：
   ```json
   {
     "camera": {
       "fps": 30  // 更高帧率提高检测准确性
     }
   }
   ```

## 存储问题

### SD 卡检测失败

#### 问题：SD 卡未检测到

**症状**：Web 界面显示 "No SD card"，无法保存照片

**解决方案**：

**硬件检查**：
1. **SD 卡类型**：使用 Class 10 或更高速度的 SD 卡
2. **容量限制**：最大 32GB，推荐 8GB-16GB
3. **重新插入**：重新插拔 SD 卡
4. **更换 SD 卡**：尝试不同的 SD 卡

**格式化**：
1. **格式化为 FAT32**：
   - Windows：右键 SD 卡 → 格式化 → FAT32
   - Linux：`sudo mkfs.vfat /dev/sdX1`
   - macOS：磁盘工具 → 格式化 → MS-DOS (FAT)

2. **检查分区**：
   - 确保 SD 卡只有一个分区
   - 分区应标记为可启动

**软件诊断**：
```bash
# 检查 SD 卡状态
curl -s http://192.168.1.100/api/status | jq '.storage'

# 尝试重新初始化
curl -X POST http://192.168.1.100/api/storage/init
```

### 文件系统错误

#### 问题：写入失败

**症状**：照片无法保存到 SD 卡

**解决方案**：
1. **空间不足**：
   ```bash
   # 检查可用空间
   curl -s http://192.168.1.100/api/files | jq '.storage'
   ```

2. **清理空间**：
   ```bash
   # 删除旧照片
   curl -X POST "http://192.168.1.100/api/files/cleanup?keep_days=7" \
     -H "X-Password: admin"
   ```

3. **文件系统修复**：
   - 使用电脑运行 `chkdsk`（Windows）或 `fsck`（Linux）
   - 重新格式化 SD 卡

#### 问题：读取失败

**症状**：无法浏览或下载照片

**解决方案**：
1. **文件系统损坏**：
   - 备份 SD 卡数据
   - 重新格式化为 FAT32
   - 重新插入设备

2. **连接问题**：
   - 重新插拔 SD 卡
   - 检查 SD 卡槽是否有异物

### 存储空间管理

#### 问题：存储空间不足

**症状**：设备无法保存新照片

**解决方案**：
1. **自动清理**：
   ```json
   {
     "storage": {
       "cleanup_threshold": 20,  // 当使用空间 < 20% 时触发
       "keep_days": 7          // 保留最近 7 天的照片
     }
   }
   ```

2. **手动清理**：
   ```bash
   # 删除所有运动检测照片
   curl -X POST "http://192.168.1.100/api/files/cleanup?type=motion" \
     -H "X-Password: admin"
   ```

3. **使用更大的 SD 卡**：
   - 推荐容量：16GB-32GB
   - 格式：FAT32
   - 速度：Class 10 或更高

## 网络和上传问题

### 📁 文件管理问题

#### 问题：文件上传功能已移除

**症状**：尝试使用上传功能时出现错误

**解决方案**：
此功能已从固件中移除。如需上传文件，请使用第三方工具或通过 Web 界面下载文件后手动上传。

---
### 🌐 API 访问问题

### API 访问问题

#### 问题：无法访问 Web 界面

**症状**：浏览器无法访问设备 IP

**解决方案**：
1. **网络防火墙**：
   - 暂时禁用本地防火墙
   - 检查路由器设置

2. **IP 地址确认**：
   ```bash
   # 通过串口获取 IP
   # 或通过 mDNS 访问：http://mibee-cam.local
   ```

3. **端口检查**：
   ```bash
   # 测试端口连通性
   telnet 192.168.1.100 80
   ```

#### 问题：API 认证失败

**症状**：API 返回 401 Unauthorized

**解决方案**：
1. **检查密码**：
   ```bash
   # 使用正确密码
   curl -H "X-Password: admin" http://192.168.1.100/api/status
   ```

2. **重置密码**：
   ```bash
   # 通过串口重置密码
   # 发送：reset password
   ```

3. **禁用认证**（开发环境）：
   ```json
   {
     "system": {
       "web_password": ""
     }
   }
   ```

## 性能优化

### 内存优化

#### 问题：内存不足

**症状**：系统运行缓慢或崩溃

**解决方案**：
1. **监控内存使用**：
   ```bash
   # 检查内存状态
   curl -s http://192.168.1.100/api/status | jq '.system'
   ```

2. **降低分辨率**：
   ```json
   {
     "camera": {
       "resolution": 0,  // VGA 而不是 UXGA
       "fps": 15         // 降低帧率
     }
   }
   ```

3. **禁用功能**：
   ```json
   {
     "motion": {
       "enabled": false  // 临时禁用运动检测
     },
     "nas": {
       "enabled": false  // 临时禁用上传
     }
   }
   ```

### 性能调优

#### 优化帧率和分辨率

```json
{
  "camera": {
    "resolution": 1,     // SVGA (800x600)
    "fps": 20,          // 平衡质量和性能
    "jpeg_quality": 15   // 质量和大小平衡
  }
}
```

#### 优化网络设置

```json
{
  "wifi": {
    "channel": 6,        // 固定信道减少干扰
    "tx_power": 17       // 17dBm 平衡传输距离和性能
  }
}
```

## 高级故障排除

### 串口调试

#### 启用详细日志

```bash
# 通过 Web 界面启用调试
curl -X POST "http://192.168.1.100/api/config/update" \
  -H "Content-Type: application/json" \
  -H "X-Password: admin" \
  -d '{"system":{"debug_level":3}}'
```

#### 常见串口错误消息

| 消息 | 可能原因 | 解决方案 |
|------|---------|---------|
| `Camera init failed` | 摄像头硬件问题 | 检查摄像头连接 |
| `WiFi connect timeout` | 网络问题 | 检查 WiFi 设置 |
| `SD card error` | 存储问题 | 重新格式化 SD 卡 |
| `PSRAM not found` | 内存硬件问题 | 检查 PSRAM 芯片 |
| `Heap too low` | 内存不足 | 降低分辨率/质量 |

### 系统恢复

#### 完全恢复出厂设置

```bash
# 通过串口
# 连接 COM8，波特率 115200
# 发送：reset full

# 或通过 API
curl -X POST "http://192.168.1.100/api/reset?mode=full" \
  -H "X-Password: admin"
```

#### 手动固件重刷

```bash
# 清除闪存
esptool.py --port COM8 erase_flash

# 写入新固件
esptool.py --port COM8 --baud 460800 write_flash 0x0 build/bootloader.bin 0x8000 build/partition_table.bin 0x10000 build/mibee-cam.bin
```

### 固件更新

#### 检查更新

```bash
# 检查当前版本
curl -s http://192.168.1.100/api/status | jq '.version'

# 下载最新固件
# 从 GitHub 发布页面下载
```

#### 更新固件

```bash
# 使用 esptool.py 更新
esptool.py --port COM8 --baud 460800 write_flash 0x10000 latest_firmware.bin
```

## 联系支持和资源

### 联系信息

- **GitHub Issues**：报告 bug 和功能请求
- **讨论论坛**：社区支持和帮助
- **邮件支持**：技术支持和紧急问题

### 日志收集

收集以下信息以获得更好的支持：

1. **设备日志**：串口输出（波特率 115200）
2. **系统状态**：
   ```bash
   curl -s http://192.168.1.100/api/status > status.json
   ```
3. **配置信息**：
   ```bash
   curl -s http://192.168.1.100/api/config > config.json
   ```
4. **错误截图**：Web 界面错误屏幕截图

### 调试工具包

推荐的调试工具：

- **串口监视器**：PuTTY、Tera Term、screen
- **网络分析**：Wireshark、tcpdump
- **文件管理**：Total Commander、Explorer++
- **图像编辑**：GIMP、Photoshop（用于分析图像问题）

### 常见问题参考

#### 硬件兼容性

| ESP32-CAM 版本 | PSRAM | 支持的分辨率 |
|---------------|-------|-------------|
MiBee V3 | 有 | VGA, SVGA, XGA, UXGA |
MiBee V2 | 有 | VGA, SVGA, XGA |
MiBee V1 | 无 | VGA, SVGA |
| 其他品牌 | 查看规格 | 根据具体型号 |

#### 已知限制

1. **GPIO14 共享**：摄像头和 SD 卡不能同时使用
2. **内存限制**：UXGA 分辨率需要 2.4MB PSRAM
3. **WiFi 协议**：仅支持 2.4GHz (802.11 b/g/n)
4. **文件系统**：最大支持 32GB SD 卡

### 📌 重要说明

**⚠️ DMA 冻结工作流程**：ESP32 在 STA 模式下摄像头初始化有已知的 DMA 冻结问题。固件采用"先启动 WiFi STA 模式，在回调中初始化摄像头 + 重试 3 次"的策略来解决此问题。启动时请注意等待 1-2 秒的摄像头初始化延迟。

**⚠️ GPIO14 共享问题**：MiBee Cam 上，GPIO14 引脚被摄像头（XCLK）和 SD 卡（CLK）共享使用。固件采用 SD 卡初始化 → 摄像头初始化 → SD 卡重新初始化的顺序来避免冲突。如果遇到 SD 卡或摄像头功能异常，请首先检查此引脚连接。