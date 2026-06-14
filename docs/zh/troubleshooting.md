#YB|> 🌐 [English Documentation](../en/troubleshooting.md)
#KM|
#TM|# 故障排除指南
#RW|
#PZ|本指南提供 MiBee Cam 固件的常见问题诊断和解决方案。按照分步说明解决连接、摄像头、存储和网络问题。
#SY|
#BH|## 快速诊断
#XW|
#XB|### 基本检查清单
#SK|
#YH|在开始详细故障排除之前，请检查这些基本项目：
#TX|
#MW|- ✅ 电源供应：5V USB 电源，电流至少 1A
#TT|- ✅ 连接：USB 线缆牢固，无松动
#WS|- ✅ SD 卡：已正确插入并格式化为 FAT32
#KZ|- ✅ 摄像头模块：已正确安装且引脚无损坏
#PT|- ✅ 网络路由器：2.4GHz WiFi 支持，WPA/WPA2 加密
#KS|
#YH|### LED 状态诊断
#YQ|
#JB|使用状态 LED 进行快速故障诊断：
#ZP|
#VS|| LED 状态 | 可能原因 | 推荐操作 |
#PN||---------|---------|---------|
#VK|| **常亮红色** | 摄像头初始化失败 | 检查摄像头连接 |
#ZN|| **快速闪烁 (0.5秒)** | WiFi 连接失败 | 检查 WiFi 设置 |
#NS|**慢速闪烁 (2秒)** | AP 模式激活 | 连接到 MiBeeCam |
#RR|| **红色闪烁 (0.2秒亮/0.8秒灭)** | 系统错误 | 查看串口日志 |
#MT|| **无指示灯** | 电源问题 | 检查 USB 电源 |
#ZM|
#HK|## 构建和烧录问题
#JQ|
#YN|### 构建失败
#WV|
#HB|#### 问题：找不到 ESP-IDF 工具链
#MV|
#HY|**错误消息**：
#SK|```
#HQ|Command "xtensa-esp32-elf-gcc" not found
#VS|```
#BH|
#WW|**解决方案**：
#PH|1. 验证 ESP-IDF 安装：
#PJ|   ```bash
#TK|   idf.py --version
#SH|   ```
#VJ|
#WY|2. 如果命令未找到，运行环境设置脚本：
#PJ|   ```bash
#YW|   # Windows
#MS|   C:\Espressif\esp-idf> export.bat
#NM|   
#WH|   # Linux/Mac
#NN|   source ~/esp/esp-idf/export.sh
#SY|   ```
#XN|
#TR|3. 重启命令提示符/终端后再试
#KR|
#XW|#### 问题：编译错误 - 目标未设置
#HQ|
#HY|**错误消息**：
#JY|```
#WS|Error: Target not set. Please use `idf.py set-target esp32`
#WV|```
#PZ|
#WW|**解决方案**：
#BV|```bash
#ZB|idf.py set-target esp32
#PM|idf.py build
#JS|```
#PR|
#PR|#### 问题：ESP-IDF 版本不兼容
#HV|
#HY|**错误消息**：
#ZY|```
#ZY|Error: This project requires ESP-IDF v6.0 or later
#NB|```
#PX|
#WW|**解决方案**：
#SP|1. 检查 ESP-IDF 版本：
#PJ|   ```bash
#TK|   idf.py --version
#SZ|   ```
#WR|
#XP|2. 如果版本过低，下载并安装 v6.0：
#PJ|   ```bash
#PX|   # 删除旧版本
#XP|   rm -rf ~/esp/esp-idf
#WY|   
#NW|   # 下载 ESP-IDF v6.0
#QB|   cd ~
#NB|   mkdir esp
#QT|   cd esp
#TS|   git clone --recursive https://github.com/espressif/esp-idf.git
#KY|   cd esp-idf
#XY|   git checkout v6.0
#ZT|   
#SP|   # 设置环境
#HK|   ./install.sh all
#HM|   source export.sh
#PZ|   ```
#PJ|
#QQ|#### 问题：依赖项下载失败
#NJ|
#HY|**错误消息**：
#ZX|```
#KX|Error: Failed to download component...
#KB|```
#BP|
#WW|**解决方案**：
#PQ|1. 检查网络连接
#ZZ|2. 手动下载依赖项：
#PJ|   ```bash
#PJ|   # 进入项目目录
#ZT|   cd mibee-cam
#BK|   
#BV|   # 清理并重新构建
#HQ|   idf.py clean
#JK|   idf.py build -v  # 详细输出
#XN|   ```
#QY|
#YM|3. 如果问题持续，使用本地缓存：
#WY|
#NH|### 烧录问题
#YB|
#NW|#### 问题：找不到串口设备
#XB|
#HY|**错误消息**：
#RH|```
#WZ|Error: Serial port COM8 not found
#JZ|```
#QZ|
#WW|**解决方案**：
#QZ|
#RM|**Windows**：
#PH|1. 打开设备管理器
#RJ|2. 检查 "端口 (COM 和 LPT)" 下的 COM 端口
#XY|3. 如果未显示，安装 USB 驱动程序
#MR|4. 更新串口号：`idf.py -p COM9 flash monitor`
#XS|
#TJ|**Linux**：
#BV|```bash
#JY|# 查找设备
#VW|ls /dev/ttyUSB*
#NQ|ls /dev/ttyACM*
#JM|
#NW|# 常见设备
#RP|# /dev/ttyUSB0 - CP2102/CH340 适配器
#QK|# /dev/ttyACM0 - 原生 USB 连接
#PY|
#XX|# 使用正确的设备
#ZJ|idf.py -p /dev/ttyUSB0 flash monitor
#XT|```
#QH|
#TZ|**Mac**：
#BV|```bash
#JY|# 查找设备
#JP|ls /dev/tty.usbserial-*
#ZT|ls /dev/tty.usbmodem*
#ZB|
#NW|# 常见设备
#JQ|# /dev/tty.usbserial-XXXX - CP2102
#TT|# /dev/tty.usbmodemXXXX - FTDI
#SK|
#XX|# 使用正确的设备
#QZ|idf.py -p /dev/tty.usbserial-XXXX flash monitor
#XW|```
#BT|
#KS|#### 问题：权限被拒绝
#HM|
#HY|**错误消息**：
#BH|```
#MS|Error: Permission denied
#VK|```
#BN|
#WW|**解决方案**：
#HM|
#PK|**Linux/Mac**：
#BV|```bash
#SQ|# 添加用户到 dialout 组
#TW|sudo usermod -a -G dialout $USER
#NT|
#XY|# 或设置串口权限
#BP|sudo chmod a+rw /dev/ttyUSB0
#HM|
#KZ|# 或使用 sudo
#JR|sudo idf.py -p /dev/ttyUSB0 flash monitor
#YM|```
#RS|
#RM|**Windows**：
#KX|1. 以管理员身份运行命令提示符
#KY|2. 或重新安装 USB 驱动程序
#XN|
#VQ|#### 问题：烧录超时
#JZ|
#HY|**错误消息**：
#ZJ|```
#YN|Error: Flash timeout
#JZ|```
#MV|
#WW|**解决方案**：
#WP|1. **连接问题**：更换 USB 电缆，确保 5V 电源
#WN|2. **引脚问题**：确保 BOOT 按钮未卡住
#JQ|3. **重试**：再次尝试烧录
#BJ|4. **手动引导**：
#ZW|   - 按住 BOOT 按钮
#XS|   - 按下 RST 按钮
#MX|   - 释放 BOOT 按钮
#ZN|   - 释放 RST 按钮
#YZ|   - 立即运行烧录命令
#PZ|
#PZ|#### 问题：无法进入下载模式
#ZP|
#WW|**症状**：设备持续运行，不接受烧录
#XJ|
#WW|**解决方案**：
#BV|```bash
#HW|# 强制进入下载模式
#XZ|idf.py -p /dev/ttyUSB0 --before default_reset --after hard_reset flash monitor
#VX|```
#HT|
#BJ|或使用 esptool.py：
#BV|```bash
#BY|# 查找串口
#RH|esptool.py --port COM8 list_ports
#XJ|
#KJ|# 烧录固件
#BB|esptool.py --port COM8 --baud 460800 write_flash 0x10000 build/mibee-cam.bin
#QN|```
#QP|
#VX|## WiFi 连接问题
#WV|
#KR|### AP 模式连接
#MY|
#KJ|#### 问题：无法连接到 "MiBeeCam" 网络
#WZ|
#VQ|**症状**：WiFi 网络列表中未显示 MiBeeCam
#NQ|
#WW|**解决方案**：
#KT|1. **信号强度**：确保设备距离路由器 < 10 米
#VJ|2. **2.4GHz 要求**：仅支持 2.4GHz 频段
#BM|3. **信道冲突**：切换到不同信道：
#PJ|   ```bash
#YX|   # 重置设备到 AP 模式
#RY|   curl -X POST http://192.168.4.1/api/reboot
#ZM|   ```
#KJ|
#HH|4. **电源不足**：使用 5V/1A 电源适配器
#JX|
#XX|#### 问题：连接后无互联网访问
#TM|
#QP|**症状**：连接到 MiBeeCam 但无法访问浏览器
#MX|
#WW|**解决方案**：
#JQ|1. **IP 地址**：尝试手动访问：
#PJ|   ```bash
#WQ|   # 或 192.168.4.1
#YK|   http://192.168.4.1/
#JP|   ```
#ZQ|
#BZ|2. **浏览器兼容性**：使用 Chrome、Firefox 或 Edge
#BP|3. **防火墙**：暂时禁用本地防火墙
#SK|4. **DNS 问题**：设置静态 IP 或使用 IP 地址访问
#BP|
#SS|### STA 模式连接
#XK|
#VQ|#### 问题：WiFi 连接失败
#RY|
#KJ|**症状**：设备启动但不连接到 WiFi，停留在 AP 模式
#ZB|
#WW|**解决方案**：
#NM|
#NH|**检查 WiFi 设置**：
#BV|```bash
#PH|# 扫描可用网络
#QV|curl -s http://192.168.4.1/api/config/wifi/scan | jq .
#PB|```
#NB|
#VB|**验证凭据**：
#TT|- **SSID**：确保网络名称正确（区分大小写）
#MJ|- **密码**：验证 WPA2 密码
#YP|- **隐藏网络**：如果隐藏，使用完整 SSID
#RS|
#VJ|**重置配置**：
#BV|```bash
#SZ|# 通过串口重置
#YV|# 连接到 COM8，波特率 115200
#SN|# 发送命令：reset wifi
#QW|```
#YM|
#NW|#### 问题：连接后频繁断开
#RT|
#WQ|**症状**：WiFi 连接建立但频繁断开
#YH|
#WW|**解决方案**：
#HH|1. **信号强度**：
#PJ|   ```bash
#HB|   # 检查信号强度
#BM|   curl -s http://192.168.1.100/api/status | jq '.wifi.signal'
#RY|   ```
#RM|
#TK|   - < -60dBm：弱信号，移近路由器
#MJ|   - -60 到 -80dBm：中等信号
#RJ|   - > -80dBm：重新定位设备
#NN|
#KZ|2. **信道干扰**：在路由器设置中固定信道
#ZN|3. **电源问题**：使用 5V/2A 电源
#MT|4. **固件更新**：确保使用最新固件
#ZT|
#PK|#### 问题：无法获取 IP 地址
#RN|
#KY|**症状**：连接 WiFi 但无 IP 地址
#XS|
#WW|**解决方案**：
#WP|1. **DHCP 问题**：重启路由器
#VQ|2. **静态 IP**：配置静态 IP：
#TW|   ```json
#XW|   {
#HM|     "wifi": {
#SN|       "mode": "STA",
#PJ|       "ssid": "MyNetwork",
#TV|       "password": "MyPassword",
#VP|       "static_ip": "192.168.1.150",
#NH|       "gateway": "192.168.1.1",
#BT|       "netmask": "255.255.255.0"
#RB|     }
#NW|   }
#XW|   ```
#RX|
#JW|3. **MAC 地址冲突**：配置不同的设备名称
#TT|
#RR|## 摄像头问题
#QV|
#VB|### 摄像头初始化失败
#NZ|
#VS|#### 问题：无图像输出
#XW|
#SP|**症状**：Web 界面显示黑屏或灰色区域
#SQ|
#WW|**解决方案**：
#PS|
#YN|**硬件检查**：
#NT|1. **摄像头连接**：
#ZW|   - 重新安装摄像头模块
#HS|   - 检查排线是否牢固
#TZ|   - 确保摄像头类型为 OV2640
#KP|
#TK|2. **PSRAM 问题**：
#PQ|   - 某些 ESP32-CAM 版本没有 PSRAM
#QH|   - 检查型号是否支持 PSRAM
#YB|   - 如果无 PSRAM，仅支持低分辨率
#NX|
#RW|3. **电压问题**：
#PS|   - 确保电源电压稳定在 5V
#ZK|   - 使用高质量的 USB 电源
#XJ|
#TZ|**软件配置**：
#BV|```bash
#KY|# 检查摄像头状态
#YK|curl -s http://192.168.1.100/api/camera/status | jq .
#KJ|```
#BY|
#XZ|**重置摄像头**：
#BV|```bash
#TR|# 通过 Web 界面重置
#JK|# 或通过串口发送：reset camera
#HR|```
#KX|
#WN|#### 问题：图像质量差
#SW|
#KB|**症状**：图像模糊、噪点多或色彩异常
#MV|
#WW|**解决方案**：
#VR|1. **调整分辨率**：
#JK|   - 从 VGA (640x480) 开始
#PQ|   - 逐步提高分辨率
#VN|
#NR|2. **调整 JPEG 质量**：
#TW|   ```json
#RJ|   {
#TH|     "camera": {
#VS|       "jpeg_quality": 12  // 1-63，值越低质量越好
#WK|     }
#MZ|   }
#BK|   ```
#MZ|
#MN|3. **镜头清洁**：使用无绒布清洁摄像头镜头
#BX|4. **环境光线**：确保充足的光线
#PN|
#MH|### 运动检测问题
#XV|
#RM|#### 问题：运动检测过于灵敏
#WV|
#YQ|**症状**：轻微移动就触发事件
#WX|
#WW|**解决方案**：
#MS|1. **提高阈值**：
#TW|   ```json
#VW|   {
#ZZ|     "motion": {
#HJ|       "threshold": 15  // 1-255，值越高越不灵敏
#ZY|     }
#RM|   }
#VM|   ```
#KZ|
#KV|2. **调整检测区域**：
#BB|   - 检测画面中心区域
#KM|   - 减少背景复杂度
#XK|
#YJ|3. **设置冷却时间**：
#TW|   ```json
#QV|   {
#ZZ|     "motion": {
#QV|       "cooldown_time": 30  // 秒
#SJ|     }
#KP|   }
#YH|   ```
#PB|
#XS|#### 问题：运动检测不灵敏
#QK|
#ZN|**症状**：明显移动但不触发
#XH|
#WW|**解决方案**：
#SZ|1. **降低阈值**：
#TW|   ```json
#VM|   {
#ZZ|     "motion": {
#ZB|       "threshold": 3  // 1-255，值越低越灵敏
#YP|     }
#ST|   }
#SQ|   ```
#WS|
#ZH|2. **改善照明**：确保检测区域光线充足
#WM|3. **减少背景**：移除快速移动的背景元素
#WK|4. **调整帧率**：
#TW|   ```json
#JT|   {
#TH|     "camera": {
#QY|       "fps": 30  // 更高帧率提高检测准确性
#BY|     }
#BR|   }
#YQ|   ```
#SB|
#VB|## 存储问题
#NP|
#PQ|### SD 卡检测失败
#SV|
#XS|#### 问题：SD 卡未检测到
#TS|
#PX|**症状**：Web 界面显示 "No SD card"，无法保存照片
#HW|
#WW|**解决方案**：
#NX|
#YN|**硬件检查**：
#RK|1. **SD 卡类型**：使用 Class 10 或更高速度的 SD 卡
#VR|2. **容量限制**：最大 32GB，推荐 8GB-16GB
#RX|3. **重新插入**：重新插拔 SD 卡
#KS|4. **更换 SD 卡**：尝试不同的 SD 卡
#SZ|
#SK|**格式化**：
#XQ|1. **格式化为 FAT32**：
#XJ|   - Windows：右键 SD 卡 → 格式化 → FAT32
#QJ|   - Linux：`sudo mkfs.vfat /dev/sdX1`
#NJ|   - macOS：磁盘工具 → 格式化 → MS-DOS (FAT)
#SH|
#ZK|2. **检查分区**：
#BX|   - 确保 SD 卡只有一个分区
#MX|   - 分区应标记为可启动
#SH|
#MJ|**软件诊断**：
#BV|```bash
#KQ|# 检查 SD 卡状态
#XW|curl -s http://192.168.1.100/api/status | jq '.storage'
#XV|
#YV|# 尝试重新初始化
#YP|curl -X POST http://192.168.1.100/api/storage/init
#SW|```
#BZ|
#XR|### 文件系统错误
#BJ|
#KP|#### 问题：写入失败
#YY|
#KN|**症状**：照片无法保存到 SD 卡
#KB|
#WW|**解决方案**：
#PH|1. **空间不足**：
#PJ|   ```bash
#PQ|   # 检查可用空间
#XS|   curl -s http://192.168.1.100/api/files | jq '.storage'
#SM|   ```
#VX|
#YZ|2. **清理空间**：
#PJ|   ```bash
#JY|   # 删除旧照片
#ZR|   curl -X POST "http://192.168.1.100/api/files/cleanup?keep_days=7" \
#XN|     -H "X-Password: admin"
#KZ|   ```
#NT|
#VV|3. **文件系统修复**：
#RP|   - 使用电脑运行 `chkdsk`（Windows）或 `fsck`（Linux）
#YT|   - 重新格式化 SD 卡
#PB|
#YH|#### 问题：读取失败
#MW|
#QH|**症状**：无法浏览或下载照片
#YS|
#WW|**解决方案**：
#WB|1. **文件系统损坏**：
#BT|   - 备份 SD 卡数据
#YX|   - 重新格式化为 FAT32
#NX|   - 重新插入设备
#MV|
#ZN|2. **连接问题**：
#TW|   - 重新插拔 SD 卡
#PB|   - 检查 SD 卡槽是否有异物
#MK|
#XR|### 存储空间管理
#VT|
#NT|#### 问题：存储空间不足
#BM|
#NM|**症状**：设备无法保存新照片
#TH|
#WW|**解决方案**：
#BY|1. **自动清理**：
#TW|   ```json
#SZ|   {
#QM|     "storage": {
#QB|       "cleanup_threshold": 20,  // 当使用空间 < 20% 时触发
#TB|       "keep_days": 7          // 保留最近 7 天的照片
#HS|     }
#RM|   }
#QW|   ```
#WY|
#WN|2. **手动清理**：
#PJ|   ```bash
#QB|   # 删除所有运动检测照片
#VX|   curl -X POST "http://192.168.1.100/api/files/cleanup?type=motion" \
#XN|     -H "X-Password: admin"
#XR|   ```
#HB|
#YT|3. **使用更大的 SD 卡**：
#PJ|   - 推荐容量：16GB-32GB
#YP|   - 格式：FAT32
#WS|   - 速度：Class 10 或更高
#WW|
#VP|## 性能优化
#PM|
#HR|### 内存优化
#ZH|
#XJ|#### 问题：内存不足
#JH|
#RJ|**症状**：系统运行缓慢或崩溃
#RP|
#WW|**解决方案**：
#HY|1. **监控内存使用**：
#PJ|   ```bash
#XW|   # 检查内存状态
#VB|   curl -s http://192.168.1.100/api/status | jq '.system'
#TY|   ```
#QQ|
#BT|2. **降低分辨率**：
#TW|   ```json
#YY|   {
#TH|     "camera": {
#YB|       "resolution": 0,  // VGA 而不是 UXGA
#BB|       "fps": 15         // 降低帧率
#NY|     }
#MY|   }
#ZP|   ```
#JT|
#PY|3. **禁用功能**：
#TW|   ```json
#MR|   {
#ZZ|     "motion": {
#TM|       "enabled": false  // 临时禁用运动检测
#KM|     }
#VY|   }
#TH|   ```
#SV|
#HS|### 性能调优
#KJ|
#BJ|#### 优化帧率和分辨率
#JX|
#YP|```json
#MV|{
#RK|  "camera": {
#VJ|    "resolution": 1,     // SVGA (800x600)
#TQ|    "fps": 20,          // 平衡质量和性能
#KV|    "jpeg_quality": 15   // 质量和大小平衡
#QH|  }
#BJ|}
#PS|```
#RQ|
#TW|#### 优化网络设置
#YX|
#YP|```json
#ST|{
#JW|  "wifi": {
#YZ|    "channel": 6,        // 固定信道减少干扰
#XP|    "tx_power": 17       // 17dBm 平衡传输距离和性能
#JQ|  }
#BV|}
#ZK|```
#PR|
#NN|## 高级故障排除
#BS|
#WP|### 串口调试
#PB|
#JJ|#### 启用详细日志
#BJ|
#BV|```bash
#WM|# 通过 Web 界面启用调试
#JW|curl -X POST "http://192.168.1.100/api/config/update" \
#VQ|  -H "Content-Type: application/json" \
#VR|  -H "X-Password: admin" \
#YH|  -d '{"system":{"debug_level":3}}'
#MK|```
#XX|
#MX|#### 常见串口错误消息
#QB|
#JM|| 消息 | 可能原因 | 解决方案 |
#PR||------|---------|---------|
#YV|| `Camera init failed` | 摄像头硬件问题 | 检查摄像头连接 |
#HJ|| `WiFi connect timeout` | 网络问题 | 检查 WiFi 设置 |
#WR|| `SD card error` | 存储问题 | 重新格式化 SD 卡 |
#BB|| `PSRAM not found` | 内存硬件问题 | 检查 PSRAM 芯片 |
#VX|| `Heap too low` | 内存不足 | 降低分辨率/质量 |
#QZ|
#PM|### 系统恢复
#KW|
#HM|#### 完全恢复出厂设置
#HV|
#BV|```bash
#SH|# 通过串口
#XM|# 连接 COM8，波特率 115200
#SW|# 发送：reset full
#KW|
#ZW|# 或通过 API
#YX|curl -X POST "http://192.168.1.100/api/reset?mode=full" \
#XN|  -H "X-Password: admin"
#PY|```
#KZ|
#MN|#### 手动固件重刷
#JM|
#BV|```bash
#QP|# 清除闪存
#XQ|esptool.py --port COM8 erase_flash
#RM|
#WR|# 写入新固件
#SV|esptool.py --port COM8 --baud 460800 write_flash 0x0 build/bootloader.bin 0x8000 build/partition_table.bin 0x10000 build/mibee-cam.bin
#VZ|```
#NB|
#VM|### 固件更新
#RT|
#SH|#### 检查更新
#VS|
#BV|```bash
#JN|# 检查当前版本
#SB|curl -s http://192.168.1.100/api/status | jq '.version'
#WM|
#JY|# 下载最新固件
#BX|# 从 GitHub 发布页面下载
#YH|```
#VN|
#WB|#### 更新固件
#NJ|
#BV|```bash
#XN|# 使用 esptool.py 更新
#XT|esptool.py --port COM8 --baud 460800 write_flash 0x10000 latest_firmware.bin
#ZP|```
#NW|
#PV|## 联系支持和资源
#SZ|
#NX|### 联系信息
#YT|
#ST|- **GitHub Issues**：报告 bug 和功能请求
#SS|- **讨论论坛**：社区支持和帮助
#PN|- **邮件支持**：技术支持和紧急问题
#MM|
#QZ|### 日志收集
#RN|
#ZH|收集以下信息以获得更好的支持：
#WP|
#RK|1. **设备日志**：串口输出（波特率 115200）
#YH|2. **系统状态**：
#PJ|   ```bash
#MV|   curl -s http://192.168.1.100/api/status > status.json
#MP|   ```
#SH|3. **配置信息**：
#PJ|   ```bash
#RJ|   curl -s http://192.168.1.100/api/config > config.json
#YJ|   ```
#QM|4. **错误截图**：Web 界面错误屏幕截图
#MQ|
#NV|### 调试工具包
#KZ|
#VK|推荐的调试工具：
#BW|
#WZ|- **串口监视器**：PuTTY、Tera Term、screen
#BT|- **网络分析**：Wireshark、tcpdump
#MM|- **文件管理**：Total Commander、Explorer++
#NH|- **图像编辑**：GIMP、Photoshop（用于分析图像问题）
#SN|
#SV|### 常见问题参考
#RN|
#BT|#### 硬件兼容性
#PX|
#JY|| ESP32-CAM 版本 | PSRAM | 支持的分辨率 |
#YV||---------------|-------|-------------|
#XT|MiBee V3 | 有 | VGA, SVGA, XGA, UXGA |
#TT|MiBee V2 | 有 | VGA, SVGA, XGA |
#BQ|MiBee V1 | 无 | VGA, SVGA |
#JX|| 其他品牌 | 查看规格 | 根据具体型号 |
#YB|
#HJ|#### 已知限制
#SR|
#MB|1. **GPIO14 共享**：摄像头和 SD 卡不能同时使用
#ZV|2. **内存限制**：UXGA 分辨率需要 2.4MB PSRAM
#SB|3. **WiFi 协议**：仅支持 2.4GHz (802.11 b/g/n)
#QR|4. **文件系统**：最大支持 32GB SD 卡
#KV|
#NJ|### 📌 重要说明
#KJ|
#WS|**⚠️ DMA 冻结工作流程**：ESP32 在 STA 模式下摄像头初始化有已知的 DMA 冻结问题。固件采用"先启动 WiFi STA 模式，在回调中初始化摄像头 + 重试 3 次"的策略来解决此问题。启动时请注意等待 1-2 秒的摄像头初始化延迟。
#JJ|
#VP|**⚠️ GPIO14 共享问题**：MiBee Cam 上，GPIO14 引脚被摄像头（XCLK）和 SD 卡（CLK）共享使用。固件采用 SD 卡初始化 → 摄像头初始化 → SD 卡重新初始化的顺序来避免冲突。如果遇到 SD 卡或摄像头功能异常，请首先检查此引脚连接。