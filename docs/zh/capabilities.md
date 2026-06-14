#SX|[![GitHub release](https://img.shields.io/github/v/release/Mi-Bee-Studio/mibee-cam?include_prereleases&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/releases)
#RZ|[![GitHub stars](httpshttps://img.shields.io/github/stars/Mi-Bee-Studio/mibee-cam?style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam)
#ZV|[![Build Status](https://img.shields.io/github/actions/workflow/status/Mi-Bee-Studio/mibee-cam/release.yml?branch=main&style=flat-square)](https://github.com/Mi-Bee-Studio/mibee-cam/actions)
#WP|[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue?style=flat-square)](https://docs.espressif.com/projects/esp-idf/en/latest/)
#WR|[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)
#SY|
#XV|> 🌐 [English Documentation](../en/capabilities.md)
#XW|
#NK|# 项目功能与能力
#SK|
#WK|本文档汇总 MiBee Cam 固件当前支持的全部功能、运行模式以及已移除的特性。
#TX|
#PB|**数据基于当前 main 分支（`CONFIG_VERSION = 9`，魔数 `0xA5B6C7D8`）。**
#BY|
#NW|## 概览
#VP|
#SV|固件是一个**纯 JPEG 拍照 + MJPEG 推流**的 ESP32-CAM 固件，目标硬件：
#KS|
#NN|- **主控**：ESP32（双核 Xtensa LX6 @ 240MHz，**非 ESP32-S3**）
#NP|- **存储**：4MB Flash + 4MB PSRAM
#XS|- **摄像头**：OV2640（2MP，定焦定光圈镜头）
#MV|- **网络**：2.4GHz 802.11 b/g/n WiFi
#NV|
#PP|## 支持的功能
#XW|
#XV|### 📷 视频与拍照
#JJ|
#RN|| 功能 | 接口 | 限制 |
#XK||------|------|------|
#TT|| MJPEG 实时流 | `GET /stream` | 最多 2 并发客户端，15 FPS 硬编码 |
#MQ|| 单帧抓拍 | `GET /capture` | 返回单张 JPEG，分辨率/质量按当前配置 |
#HB|| 多分辨率输出 | 配置文件 | VGA / SVGA / XGA / UXGA |
#MB|| JPEG 质量可调 | 配置文件 | 0-63（数字越小质量越高） |
#KS|| 垂直翻转 | 配置项 `vflip` | 调用 OV2640 `set_vflip` 寄存器 |
#TX|
#PY|### 🎬 自动化拍摄
#RB|
#KT|| 功能 | 默认行为 | 配置项 |
#KR||------|----------|--------|
#VT|| 运动检测 | 500ms 双帧差分（基于 JPEG 字节采样） | `motion_threshold`, `motion_cooldown` |
#JS|| 运动触发拍照 | 检测到变化后保存到 SD | 自动 |
#JP|| 自动闪光（暗光场景） | 基于 JPEG 体积判断亮度，触发时开 LED | `flash_threshold` |
#VS|| 延时摄影 | 按设定间隔拍一张到 SD | `timelapse_enabled`, `timelapse_interval_s` |
#TP|| 延时 + 突发连拍 | 运动触发时连拍 N 张 | `timelapse_burst_count` |
#BB|| SD 空间自动清理 | 剩余 < 20% 时删最旧文件，删到 30% 停 | 自动 |
#TJ|
#QH|### 🌐 网络与 Web
#BY|
#PM|| 功能 | 说明 |
#ZV||------|------|
#MR|| **WiFi STA 模式** | 配置 SSID/密码后自动连接，支持断线重连 |
#SY|**WiFi AP 模式** | 首次未配网时进入，SSID `MiBeeCam`，密码 `12345678` |
#MN|| **WiFi STA 模式备选** | 配置第二个 WiFi 网络（仅 STA 连接失败时回退） | `wifi_ssid_2`, `wifi_pass_2`, `allow_ap_fallback` |
#NY|| **TX 功率可调** | 0-80（0.25dBm 单位，80=20dBm） |
#NJ|| **省电模式** | `WIFI_PS_MIN_MODEM` |
#ZY|| **Web UI 仪表盘** | `/`（自动刷新 10s） |
#QN|| **Web UI 实时预览** | `/preview.html`（带分辨率/质量控件） |
#XH|| **Web UI 配置** | `/config.html`（WiFi / 摄像头 / 运动 / 延时） |
#KB|| **Web UI 文件浏览** | `/files.html`（分页 + 下载/删除） |
#JH|| **NTP 时间同步** | 可配时区，默认 `CST-8` |
#VW|| **Prometheus 指标** | `GET /api/metrics`（堆/RSSI/SD/温度/事件计数） |
#NT|
#MR|### 💾 存储
#JN|
#PM|| 功能 | 说明 |
#KN||------|------|
#ZZ|| **SD 卡 SPI 模式** | CLK=GPIO14, MOSI=GPIO15, MISO=GPIO2, CS=GPIO13 |
#KZ|| **FAT32 文件系统** | 5 次重试挂载 |
#ZZ|| **按月分目录** | `/sdcard/photos/YYYY-MM/` |
#VX|| **文件名带时间戳** | `motion_2026-06-02_14-30-25.jpg` |
#NP|| **照片列表缓存** | 30s TTL，避免每次 HTTP 请求都遍历 SD |
#WK|| **热插拔检测** | 10s 轮询一次，自动重挂载 |
#QM|| **NVS 配置** | 命名空间 `camcfg`，V1→V9 自动迁移 |
#HV|
#MR|### 🔌 REST API
#SZ|
#VV|完整端点列表（详见 [`api.md`](./api.md)）：
#VB|
#ZH|| Method | Path | 说明 |
#TJ||--------|------|------|
#QK|| GET    | `/api/status` | 设备状态 JSON |
#RM|| GET    | `/api/config` | 当前配置（密码字段不返回） |
#RT|| POST   | `/api/config` | 更新配置（需 `X-Password` 头） |
#NM|| POST   | `/api/reset` | 出厂重置（需 `X-Password` 头） |
#QX|| POST   | `/api/reboot` | 重启设备（需 `X-Password` 头） |
#TT|| GET    | `/capture` | 单帧 JPEG |
#XJ|| GET    | `/stream` | MJPEG 实时流 |
#YR|| GET    | `/api/metrics` | Prometheus 指标 |
#QQ|| GET    | `/api/files` | 列出 SD 照片（分页） |
#QY|| DELETE | `/api/files?name=xxx` | 删除照片 |
#QT|| GET    | `/api/download?name=xxx` | 下载照片 |
#ST|| GET    | `/api/auth` | 验证 web 密码 |
#XY|| POST   | `/api/timelapse/start` | 启动延时 |
#MM|| POST   | `/api/timelapse/stop` | 停止延时 |
#PS|| GET    | `/api/timelapse/status` | 延时状态 |
#VN|| POST   | `/api/record?action=start|stop` | 录像控制（开始/停止） |
#VX|| GET    | `/api/record` | 录像状态信息 |
#WR|| GET    | `/api/storage` | 存储使用与清理状态 |
#KJ|| POST   | `/api/flash` | 控制 LED 闪光灯 |
#QT|## 不支持的功能（明确列出）
#ZS|
#VV|- ✅ **连续视频录制**：AVI 格式，支持 3 种模式（连续、延时、动态）
#QN|- ❌ **音视频录制**：无麦克风输入，无音频编码
#XK|- ❌ **自动对焦 / 光学变焦**：OV2640 是定焦镜头
#NV|- ❌ **水平翻转 / 180° 旋转**：固件仅暴露 `vflip`（垂直翻转）
#RM|- ❌ **ROI 数字缩放**：固件没有兴趣区域裁切接口
#PB|- ❌ **OTA 升级**：分区表只有 `otadata`，没有 `ota_0/ota_1` 槽位
#NK|- ❌ **HTTP 认证以外的鉴权**：只有 `X-Password` 头
#XP|- ❌ **HTTPS**：HTTP 服务器仅支持明文
#WY|
#MB|## 已移除的功能
#QJ|
#TN|| 功能 | 移除时间 | 原因 |
#HV||------|----------|------|
#QX|| **FTP 客户端** | v2 早期 | 太重，资源紧张 |
#QX|| **TF 卡 WiFi 配置（config.txt）** | 保留 | SD `/config.txt` 仍可启动时导入 WiFi |
#HN|| **BOOT 按钮出厂重置** | 始终禁用 | GPIO0 = 摄像头 XCLK，按下检测不可靠，改用 `POST /api/reset` |
#HN|| **PSRAM DMA 模式** | 一直禁用 | ESP32 原版 DMA bug，固件 `sdkconfig.defaults` 强制 `CAMERA_PSRAM_DMA=n` |
#KY|| **新 SCCB/I2C 驱动** | 一直禁用 | `CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY=y` 兼容 OV2640 |
#KY|| **NAS 上传 / WebDAV** | 从未实现 | 代码中不存在 |
#KY|| **ONVIF 发现 / 服务** | 从未实现 | 代码中不存在 |
#KY|| **WebSocket 推送** | 从未实现 | 代码中不存在 |
#KY|| **Webhook 通知** | 从未实现 | 代码中不存在 |
#KY|| **帧广播器** | 从未实现 | 代码中不存在 |
#KY|| **SHA256** | 从未实现 | 代码中不存在 |
#XM|## 启动序列
#QY|
#NY|`app_main()` 中的 **16 步启动序列**：
#WY|
#YB|1. **NVS 闪存初始化** — 配置存储准备
#BK|2. **配置加载** — 从 NVS 加载设置（版本 9）
#QB|3. **状态 LED 初始化** — GPIO33 系统状态指示
#SN|4. **SPIFFS 挂载** — Web 界面文件系统准备
#YS|5. **WiFi 子系统初始化** — 网络接口和事件循环
#WK|6. **WiFi 状态回调注册** — 网络状态变化监听
#ZP|7. **健康监控初始化** — 系统指标收集
#NP|8. **WiFi 模式选择** — STA 已配置则连接，否则启动 AP
#BT|9. **MJPEG 流传输器初始化** — 实时视频流准备
#NQ|10. **Web 服务器启动** — HTTP 服务和 API 端点
#TW|11. **时间同步初始化** — NTP 客户端（仅 STA 模式）
#WS|12. **运动检测启动** — 帧监控服务（仅 STA 模式）
#XR|13. **视频录制器初始化** — AVI 录制系统启动
#TT|14. **延时摄影初始化** — 延时拍摄功能启动
#ZY|15. **延时摄影启动** — 延时拍摄服务启动
#VM|16. **系统监控和服务** — 后台任务和服务启动
#MV|
#JS|### 启动序列详情
#WB|
#SM|**步骤 1-4：基础系统初始化**
#NQ|- NVS 初始化用于配置存储
#SX|- LED 设置用于视觉状态指示
#HM|- SPIFFS 挂载用于 Web 界面文件系统
#JZ|
#HP|**步骤 5-8：网络和系统准备**
#VR|- WiFi 子系统初始化和网络接口准备
#SV|- 健康监控启动用于系统状态跟踪
#JX|- WiFi 模式选择（配置的 STA 模式优先，失败则启动 AP）
#MK|
#VX|**步骤 9-12：网络服务启动**
#WQ|- MJPEG 流传输服务准备
#HR|- Web 服务器启动提供 HTTP API
#PJ|- 时间同步（仅在 STA 模式下）
#WW|- 运动检测服务（仅在 STA 模式下）
#TZ|
#NK|**步骤 13-16：应用服务启动**
#ZT|- 视频录制器初始化
#JH|- 延时摄影初始化
#TW|- 延时摄影启动服务
#NM|- 系统监控和后台任务启动
#RS|
#XX|## 配置结构
#VM|
#WZ|`cam_config_t`（NVS blob，大小约 200 字节）：
#PT|
#SN|| 字段 | 类型 | 默认值 | 用途 |
#JJ||------|------|--------|------|
#JB|| `wifi_ssid` | char[33] | "" | STA 模式 SSID |
#NK|| `wifi_pass` | char[65] | "" | STA 模式密码 |
#JN|| `device_name` | char[33] | "MiBeeCam" | 主机名 |
#SN|| `resolution` | uint8 | 0 (VGA) | 0=VGA, 1=SVGA, 2=XGA, 3=UXGA |
#QM|| `fps` | uint8 | 15 | 帧率目标 |
#NT|| `jpeg_quality` | uint8 | 12 | 0-63（低=高质） |
#JT|| `web_password` | char[33] | "" | 可选 Web 访问密码 |
#PR|| `timezone` | char[33] | "CST-8" | 时区字符串 |
#YK|| `motion_threshold` | uint8 | 30 | 差分百分比阈值（1-100） |
#ZY|| `motion_cooldown` | uint8 | 5 | 触发冷却（秒） |
#BB|| `vflip` | uint8 | 0 | 垂直翻转开关 |
#JZ|| `wifi_tx_power` | uint8 | 80 | 0.25dBm 单位（80=20dBm） |
#VV|| `flash_threshold` | uint8 | 40 | 暗光判定阈值（0-100） |
#TW|| `record_mode` | uint8 | 0 | 0=关闭, 1=连续, 2=延时, 3=动态 |
#MP|| `allow_ap_fallback` | bool | false | 是否允许 AP 模式回退 |
#XK|| `wifi_ssid_2` | char[33] | "" | 第二个 WiFi SSID |
#YN|| `wifi_pass_2` | char[65] | "" | 第二个 WiFi 密码 |
#QT|| `timelapse_enabled` | bool | false | 是否启用延时摄影 |
#HN|| `timelapse_interval_s` | uint16 | 60 | 延时间隔（秒） |
#WH|| `timelapse_burst_count` | uint8 | 3 | 延时连拍数量 |
#KY|| `cleanup_photo_pct` | uint8 | 20 | 照片低于此百分比时自动清理 |
#BQ|| `cleanup_video_pct` | uint8 | 10 | 视频低于此百分比时自动清理 |
#SB|| `magic` + `version` | uint32 | `0xA5B6C7D8` / 9 | 魔数+版本号（用于迁移） |
#TT|
#KX|## 故障转移行为
#BN|
#YY|- **STA 模式连接失败**：尝试第二个 WiFi 网络，失败后回退到 AP 模式
#ZK|- **SD 卡挂载失败**：继续运行，只是不能保存照片（web 仍可用）
#KY|- **摄像头初始化失败（STA 模式下）**：重试 3 次，每次间隔 500ms（ESP32 DMA 冻结问题解决方法，固件在 WiFi STA 模式下延迟摄像头初始化）
#RS|- **WiFi 中途断开**：触发 `WIFI_STATE_STA_DISCONNECTED`，健康监控记录，LED 闪烁
#SW|- **TF 卡热拔**：10s 轮询检测到，自动卸载并通知
#BV|- **TF 卡容量 < 20%**：自动从最旧文件开始删
#ZP|- **配置版本不匹配**：blob 大小比较 → 用旧字段填充 + 新字段默认值
#VB|
#TW|## 相关文档
#HM|
#KW|- [`getting-started.md`](./getting-started.md) - 编译、烧录、首次配置
#XH|- [`hardware.md`](./hardware.md) - 板级硬件规格
#KB|- [`performance.md`](./performance.md) - 各分辨率的 FPS / 延迟 / JPEG 体积
#RM|- [`lens-and-fov.md`](./lens-and-fov.md) - 镜头参数、视场角、广角与微距选项
#SQ|- [`architecture.md`](./architecture.md) - 系统架构
#VK|- [`api.md`](./api.md) - REST API 完整参考
#MK|- [`troubleshooting.md`](./troubleshooting.md) - 常见问题