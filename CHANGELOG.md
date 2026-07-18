# Changelog

## v0.2.0 (2026-07-18)

### New Features

**File Manager Page**
- New "Files" page showing all SD card files (photos + recordings)
- Type filter tabs: All / Photos / Recordings
- File type labels (green for photos, red for recordings)
- Download and delete files by type

**Web UI Upgrade (SPIFFS OTA)**
- New `POST /api/ota/spiffs` endpoint
- Upload SPIFFS image via web to update HTML/CSS/JS
- No serial cable needed for UI updates
- Auto erase, write, and reboot after upload

**Firmware OTA Upgrade**
- New `POST /api/ota/upload` endpoint
- Upload firmware binary via web browser
- Progress bar display
- Auto partition switch and reboot after upload
- A/B partition scheme with rollback protection

**Auto Firmware Versioning**
- Version derived from git tags (`git describe --tags`)
- Format: `v1.2.3-4-gabcdef` (tag-commits-hash)
- Clean version number when proper tag is created

**Smart WiFi Roaming**
- Backup WiFi RSSI threshold configuration
- Auto-scan backup networks when signal drops below threshold
- Configurable signal gap to prevent frequent switching
- Roaming config API and Web UI controls

**Configurable XCLK Frequency**
- Camera clock frequency dropdown (10/16/20 MHz)
- 10 MHz mode for unstable OV2640 clone modules
- Resolves `NO-SOI` (invalid JPEG) errors

**Snapshot Fallback Mode**
- Auto-switch to snapshot mode when MJPEG stream fails
- Timed captures for approximate real-time view
- Periodic attempts to recover MJPEG stream

### Improvements

**Navigation Restructured**
- New order: Dashboard → Live Preview → Files → Settings
- "Photos" page renamed to "Files"
- "Config" page renamed to "Settings"

**Settings Page Collapsible Sections**
- All settings sections collapsed by default
- Click title to expand/collapse
- Firmware upgrade embedded in settings (no separate page)

**Enhanced File List API**
- `GET /api/files` supports `type` parameter (all/photos/recordings)
- Boot-time file list cache for reliable access after camera init
- Resolves `opendir()` hang caused by GPIO14 SPI conflict

**Enhanced Download API**
- `GET /api/download` supports `type` parameter (photo/recording)
- Download recording files (AVI format)

**Network Optimizations**
- TCP buffer tuning for better stream stability on weak links
- Fixed stream starvation by reverting core pinning strategy
- WiFi TX power and power save config applied after boot
- Periodic reconnect to prevent network degradation

**Recording Improvements**
- Config FPS passed to frame broker
- Idle at 2fps when no stream clients to save resources
- Fixed `save_to_sd` checkbox state issue

### Bug Fixes

- Fixed file list cache corruption after first query (tab characters not restored)
- Removed erroneous `xSemaphoreGive` in `storage_save_photo`
- Fixed `f_getfree()` in storage cleanup causing GPIO14 SPI hang
- Fixed stale HTML after firmware update (added Cache-Control header)
- Fixed config page save button silently failing due to missing comma
- Fixed serial config printf format error
- Fixed preview page reconnection logic

### Build Improvements

- Optimized firmware size and IRAM usage to resolve linker overflow
- A/B OTA partition layout with rollback Kconfig
- Config version upgraded to V13 (XCLK) and V14 (roaming)

---

### 新功能

**文件管理页**
- 全新"文件"页面，显示 SD 卡所有文件（照片 + 录像）
- 类型筛选标签：全部 / 照片 / 录像
- 文件类型标签显示（绿色照片、红色录像）
- 支持按类型下载和删除文件

**Web UI 升级（SPIFFS OTA）**
- 新增 `POST /api/ota/spiffs` 端点
- 通过网页上传 SPIFFS 镜像文件更新 Web 界面
- 无需串口即可更新 HTML/CSS/JS
- 上传后自动擦除、写入、重启

**固件 OTA 升级**
- 新增 `POST /api/ota/upload` 端点
- 网页上传固件二进制文件
- 支持进度条显示
- 上传完成后自动切换分区并重启
- A/B 分区方案，支持回滚保护

**固件版本自动管理**
- 版本号从 git 标签自动生成（`git describe --tags`）
- 格式：`v1.2.3-4-gabcdef`（标签-提交数-哈希）
- 创建正式标签后显示干净版本号

**智能 WiFi 漫游**
- 新增备用 WiFi RSSI 阈值配置
- 信号低于阈值时自动扫描备用网络
- 可配置切换信号差值，避免频繁跳转
- 智能漫游配置 API 和 Web UI 控制

**XCLK 频率可配置**
- 新增摄像头时钟频率下拉选择（10/16/20 MHz）
- 10 MHz 模式兼容不稳定的 OV2640 克隆模块
- 解决 `NO-SOI`（无效 JPEG）错误

**快照回退模式**
- MJPEG 流连接失败时自动切换到截图模式
- 定时拍照显示近似实时画面
- 定期尝试恢复 MJPEG 流

### 改进

**导航重构**
- 新顺序：仪表盘 → 实时预览 → 文件 → 设置
- "照片"页更名为"文件"
- "配置"页更名为"设置"

**设置页折叠面板**
- 所有设置区域默认折叠，减少滚动
- 点击标题展开/收起
- 固件升级嵌入设置页（不再单独页面）

**文件列表 API 增强**
- `GET /api/files` 支持 `type` 参数（all/photos/recordings）
- 开机时缓存文件列表，摄像头初始化后仍可查询
- 解决 GPIO14 SPI 冲突导致的 `opendir()` 挂死问题

**下载 API 增强**
- `GET /api/download` 支持 `type` 参数（photo/recording）
- 支持下载录像文件（AVI 格式）

**网络优化**
- TCP 缓冲区调优，改善弱网环境下的流稳定性
- 修复流饥饿问题，回退核心绑定策略
- WiFi TX 功率和省电模式配置在启动后应用
- 定期重连防止长时间运行后网络变慢

**录像改进**
- 配置的 FPS 参数传递到帧代理
- 无流客户端时降至 2fps 节省资源
- 修复 `save_to_sd` 复选框状态问题

### Bug 修复

- 修复文件列表缓存在首次查询后损坏的问题（tab 字符未恢复）
- 修复 `storage_save_photo` 中多余的 `xSemaphoreGive`
- 修复存储清理中 `f_getfree()` 导致 GPIO14 SPI 挂死
- 修复固件更新后浏览器缓存旧 HTML 的问题（添加 Cache-Control 头）
- 修复配置页面保存按钮因缺少逗号而静默失败
- 修复串口配置 printf 格式错误
- 修复流预览页重连逻辑

### 构建改进

- 优化固件体积和 IRAM 使用，解决链接器溢出
- A/B OTA 分区布局 + 回滚 Kconfig
- 配置版本升级到 V13（XCLK）和 V14（漫游）

---

## v0.1.0

### New Features

- Initial release with MJPEG streaming, motion detection, SD card storage
- Timelapse and burst capture modes
- Dual WiFi failover configuration
- REST API for complete control
- Prometheus metrics endpoint

---

### 新功能

- 初始版本，支持 MJPEG 流、运动检测、SD 卡存储
- 延时摄影和连拍模式
- 双 WiFi 故障转移配置
- REST API 完整控制
- Prometheus 指标端点
