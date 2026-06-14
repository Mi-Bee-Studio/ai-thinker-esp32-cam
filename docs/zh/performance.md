#MW|[![ESP32](https://img.shields.io/badge/ESP32-Esp32--cam-blue.svg)](https://github.com/espressif/esp-idf) [![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/) [![OV2640](https://img.shields.io/badge/Camera-OV2640-orange.svg)](https://www.ovt.com/products/ov2640.html) [![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
#KM|
#PQ|> 🌐 [English Documentation](../en/performance.md)
#RW|
#YR|# 性能与延迟
#SK|
#KN|本文档提供 ESP32-CAM 固件在不同分辨率/质量设置下的实测 FPS、JPEG 体积、端到端延迟数据，以及性能瓶颈分析。
#XW|
#XQ|**> 数据基于 MiBee Cam（ESP32 双核 240MHz，4MB PSRAM，OV2640），固件 `main` 分支。**
#SK|
#QM|**📊 性能概览**
#VX|## 🚫 核心硬件约束
#BQ|
#HJ|**| 约束 | 影响 |**
#QK|**|------|------|**
#YB|**| **单核 ESP32（非 S3）** | 无 H.264 硬件编码；JPEG 编码由 OV2640 内部 ISP 完成，ESP32 仅搬运数据 |**
#BS|**| **4MB PSRAM** | 帧缓冲放在 PSRAM（`fb_location = CAMERA_FB_IN_PSRAM`） |**
#BQ|**| **fb_count = 2**（双缓冲） | 一帧被占用时无法取下一帧；UXGA 下 PSRAM 占用 ~2.4MB |**
#JS|**| **20MHz XCLK** | 硬编码（`camera_driver.c:90`） |**
#MS|**| **2.4GHz WiFi 802.11 b/g/n** | 实际可用带宽 ~20-30 Mbps（受信号质量/干扰影响） |**
#HH|**| **单线程 HTTP 服务器** | `HTTPD_DEFAULT_CONFIG` 默认配置；一个慢请求阻塞所有 |**
#ZP|
#QM|⚠️ ESP32 在 STA 模式下摄像头初始化有已知 DMA freeze 问题，固件采用"先启 WiFi → 回调中初始化摄像头 + 重试 3 次"的工作绕开（`main.c:54-72`）。
#KB|> ⚠️ ESP32 在 STA 模式下摄像头初始化有已知 DMA freeze 问题，固件采用"先启 WiFi → 回调中初始化摄像头 + 重试 3 次"的工作绕开（`main.c:54-72`）。
#XW|
#KJ|## 📹 分辨率与可达 FPS
#JJ|
#YB|**| 分辨率 | 像素 | 实际可达 FPS（流式） | JPEG 编码耗时 | 推荐场景 |**
#TM|**|--------|------|----------------------|----------------|----------|**
#YM|**| **VGA** 640×480 | 307,200 | **15-20** | ~50-100ms | 实时预览 |**
#RH|**| **SVGA** 800×600 | 480,000 | 12-15 | ~80-130ms | 中等画质流 |**
#ZH|**| **XGA** 1024×768 | 786,432 | 6-10 | ~150-250ms | 不推荐流式 |**
#WJ|**| **UXGA** 1600×1200 | 1,920,000 | **2-5** | ~300-600ms | 单帧拍照 |**
#WV|
#VR|**📝 重要说明**：流式 FPS 实际受硬编码 `STREAM_FPS = 15`（`mjpeg_streamer.c:34`）限制，即使硬件能跑 20 FPS 也不会超过 15。
#YX|> 流式 FPS 实际受硬编码 `STREAM_FPS = 15`（`mjpeg_streamer.c:34`）限制，即使硬件能跑 20 FPS 也不会超过 15。
#RB|
#PB|### 分辨率与 FOV 的反向关系
#MS|
#NM|OV2640 通过**内部 ISP 缩放/窗口**生成低分辨率，**不是软件裁切**：
#BH|
#QS|| 选择 | 视野（相对全传感器） | 等效光学行为 |
#WB||------|----------------------|--------------|
#QZ|| **UXGA** | 100% | 最广 |
#MN|| XGA | ~81% | 中心裁切 |
#XY|| SVGA | ~63% | 中等 |
#ZR|| **VGA** | **50%**（中心 1/4 区域） | **2× 数字变焦** |
#BY|
#PB|⚠️ 选 VGA 不会得到更广视野，反而是**最窄**的视野 + 2× 变焦效果。
#QW|
#TX|## JPEG 体积估算
#NM|
#RM|实际 JPEG 体积取决于场景复杂度，下表为典型值（基于 OV2640 实测统计）：
#YJ|
#KT|| 分辨率 | 质量 0-10（高质） | 质量 11-20 | 质量 21-40 | 质量 41-63（低质） |
#VV||--------|--------------------|-------------|-------------|---------------------|
#VX|**💡 重要提示**：
#PW|**重要**：
#XN|- 暗光场景下 JPEG 体积会**显著缩小**（编码器产生更多 0 块），这是固件判断亮度的依据（`flash_led.c` 用 12-22KB 范围作阈值）
#TQ|- 简单场景（纯色墙、文档）可能只有上述范围的 30-50%
#ZX|- 复杂场景（树叶、人群）可能达到上限的 1.5 倍
#RJ|
#TK|## 单帧 `/capture` 端到端延迟
#XZ|
#ZJ|| 场景 | 编码 + 取帧 | WiFi 发送（4KB chunk） | 总延迟 |
#KS||------|--------------|--------------------------|---------|
#QN|| VGA 质量 10 | ~80ms | ~50ms（10-20KB） | **~130-200ms** |
#ZZ|| SVGA 质量 10 | ~120ms | ~70ms | ~200-300ms |
#VM|| XGA 质量 10 | ~200ms | ~120ms | ~400-500ms |
#QS|| **UXGA 质量 0** | ~500ms | **~250ms**（80KB ÷ 4KB） | **~1-1.5 秒** |
#PR|
#NX|> `/capture` 使用摄像头互斥锁（3 秒超时，`camera_driver.c:218`），如果同时有流在跑，**会被阻塞**。
#HV|
#JM|## MJPEG 流式延迟
#SZ|
#QV|代码硬编码：
#VQ|- `STREAM_FPS = 15`（`mjpeg_streamer.c:34`）
#XX|- `STREAM_FRAME_DELAY = 1000/15 ≈ 66ms`（每帧间隔）
#NZ|- `CHUNK_SIZE = 4096`（4KB 分块发送）
#KB|
#SX|```
#RT|实际延迟 ≈ 帧间隔 (66ms) + 编码耗时 + 网络发送 + 浏览器解码渲染
#KQ|         = 66ms + 80-150ms + 30-100ms + 50-100ms (浏览器)
#YS|         ≈ 250-500ms（VGA 实测常见值）
#KZ|```
#KR|
#VJ|| 分辨率 | 流式延迟 | 备注 |
#VM||--------|----------|------|
#NZ|| VGA 质量 10-12 | **300-500ms** | 准实时 |
#JW|| SVGA 质量 10-12 | 500-800ms | 可接受 |
#NN|| XGA 质量 10-12 | 800-1500ms | 明显卡顿 |
#XX|| **UXGA 质量 0** | **1500-2500ms** | 不实用 |
#BX|
#YN|## 运动检测延迟
#MS|
#WH|`motion_detect.c:42` 定义 `CAPTURE_INTERVAL_MS = 500`：
#ZT|
#ZV|```
#VP|1. 取参考帧 A（~80ms）
#QY|2. 等待 500ms
#SK|3. 取比较帧 B（~80ms）
#MY|4. JPEG 字节采样对比（~10ms）
#ZK|5. 触发后拍照（~150ms 含 SD 写）
#SZ|─────────────
#KP|总计：~700-1500ms
#ZX|```
#TS|
#NP|加上二次确认防抖，触发响应稳定在 **1-2 秒量级**。远非"实时"。
#BP|
#HR|## WiFi 带宽计算
#YX|
#PJ|MJPEG 流 = 帧率 × 每帧字节数：
#PP|
#ZM|| 分辨率 | 质量 | 单帧体积 | 15 FPS 所需带宽 | 备注 |
#WP||--------|------|----------|------------------|------|
#PS|| VGA | 12 | 12 KB | **1.4 Mbps** | 单客户端轻松 |
#BS|| VGA | 0 | 30 KB | 3.6 Mbps | 2 客户端满载 7.2 Mbps |
#WP|| SVGA | 12 | 20 KB | 2.4 Mbps | 接近上限 |
#RV|| XGA | 12 | 35 KB | 4.2 Mbps | 2 客户端会卡 |
#WH|| UXGA | 0 | 120 KB | **14.4 Mbps** | **WiFi 不可能扛住** |
#QY|
#RN> 实测 ESP32 的 2.4GHz WiFi 在信号强度 -60dBm 时，单客户端稳定带宽约 5-8 Mbps，2 客户端共享时会减半。
#WY|
#SM|## 关键延迟瓶颈（代码定位）
#YB|
#XB|| 瓶颈 | 文件:行 | 影响 |
#TV||------|---------|------|
#QZ|| 单线程 HTTP | `web_server.c:887` | 一个慢请求阻塞所有其它请求 |
#MJ|| 摄像头互斥锁 3s 超时 | `camera_driver.c:218` | 流和 `/capture` 互斥 |
#VW|| 异步流 handler | `mjpeg_streamer.c:185` | ✅ 解决了"流阻塞其它"问题 |
#JR|| PSRAM DMA 已禁用 | `sdkconfig.defaults` | ESP32 DMA bug 工作绕开 |
#JH|| 4KB 分块发送 | `mjpeg_streamer.c:33` | 慢但稳 |
#PY|| `recv_wait_timeout=10s`, `send_wait_timeout=30s` | `web_server.c:890-891` | WiFi 慢时容错 |
#VY|| 延时摄影阻塞 | `timelapse.c` | 拍摄时流会冻结几百 ms |
#SN|| WiFi 启动推迟摄像头 | `main.c:54` | +1-2s 启动延迟 |
#NZ|| `vTaskDelay(STREAM_FRAME_DELAY)` | `mjpeg_streamer.c:143` | 66ms 硬性帧间隔 |
#QS|
#NS|## 内存占用
#QR|
#ZB|| 模块 | 占用 PSRAM | 备注 |
#HB||------|------------|------|
#TP|| 摄像头帧缓冲（UXGA 双缓冲） | ~2.4MB | 最大情况 |
#ZR|| 摄像头帧缓冲（VGA 双缓冲） | ~0.6MB | 最小情况 |
#JK|| SD 文件名缓冲（`/api/files`） | 32KB | 临时 malloc |
#HK|| SD 列表缓存 | 0-32KB | 30s TTL |
#MS|| 运动检测采样缓冲 | 帧长/10 | 例：UXGA 帧 100KB → 采样 10KB |
#HB|| Web server LRU 缓冲 | 默认 4KB | 5 个文件描述符 |
#SN|| **可用剩余** | **~1.6MB（UXGA 下）** | 不能再开大缓冲 |
#TJ|
#NT|## 配置建议
#HV|
#RR|### 要"准实时"流
#BS|- 分辨率：**VGA 640×480**
#VH|- JPEG 质量：15-20
#BP|- FPS：15（默认）
#MN|- 期望延迟：300-500ms
#JJ|- 适用：远程监控、宠物观察
#TV|
#ZY|### 要"高质量截图"
#RT|- 模式：单帧 `/capture`
#XK|- 分辨率：**UXGA 1600×1200**
#BJ|- JPEG 质量：0
#BZ|- 期望延迟：1-1.5s
#WV|- 适用：定时抓拍、文档拍照
#NX|
#MQ|### 要"长时间视频监控"
#TB|- 使用延时摄影而不是推流
#KK|- 间隔：60-300 秒
#KT|- 文件按月分目录，定期清理 SD
#HM|
#PH|### 要"夜间监控"
#NW|- `flash_threshold` 调到 60-70（更敏感）
#QP|- 闪光灯在 GPIO4（PWM）
#WB|- 注意：闪光距离有限（~2 米）
#BN|
#QN|## 不要做的事
#HM|
#ZB|| 错误做法 | 后果 |
#KH||----------|------|
#WX|| 边推流边频繁 `/capture` | 摄像头互斥锁冲突，流会卡顿 |
#XX|| 推 UXGA 流 | WiFi 带宽不够，延迟 2 秒+ |
#XB|| 同时启用运动 + 延时 + 推流 | 三个任务抢摄像头，时序不可预测 |
#JK|| 在 RSSI < -75dBm 时推流 | 丢帧严重 |
#JH|| 设 jpeg_quality=0 + UXGA 推流 | 单帧 200KB，2 客户端 → 32Mbps，物理不可能 |
#HM|
#WX|## 相关代码位置
#YV|
#PJ|- 摄像头参数：`main/camera_driver.c:90-127`
#JB|- 流式配置：`main/mjpeg_streamer.c:32-37`
#NP|## 🔗 相关文档
#BH|
#YZ|- [`capabilities.md`](./capabilities.md) - 功能总览
#XH|- [`hardware.md`](./hardware.md) - 板级硬件规格
#TQ|- [`lens-and-fov.md`](./lens-and-fov.md) - 镜头参数
#MK|- [`troubleshooting.md`](./troubleshooting.md) - 常见问题