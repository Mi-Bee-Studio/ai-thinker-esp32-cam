# PROJECT KNOWLEDGE BASE

ESP32-CAM firmware (mibee-cam) — OV2640 camera with MJPEG streaming, motion detection, video recording, SD storage, and serial AT config. Built with ESP-IDF v6.0.1 for MiBee Cam board (ESP32-D0WD-V3, 4MB flash, 8MB PSRAM).

**Sister project:** `Mi-Bee-Studio/seeed-esp32s3-cam` — ESP32-S3 variant, same team, same WiFi config patterns but better RF. Reference it when comparing WiFi behavior.

## AT command interface (family contract v1.0, 2026-09-04)

统一契约：`docs/at-command.md`（四仓 md5 一致，地位同 api-contract）。核心集：
`AT / AT+HELP / AT+GMR / AT+STATUS / AT+WIFI?|= / AT+IP? / AT+CAMRES?|= / AT+CAMQUAL?|= /
AT+REBOOT / AT+RESTORE`（+能力裁剪项）。红线：**任何读指令不回显密码**；CAMQUAL 边界
10-63（PIT-021）。本板串口 /dev/ttyUSB0（CH340，开串口即复位，PIT-003）。CAMRES/CAMQUAL 热重配；实现于家族共享核心 main/at_command.c（四仓 md5 一致）+ 板级 main/at_port.c（2026-09-05 取代 serial_config.c；AT+RESET 已删除，恢复出厂走 AT+RESTORE）。**台架板注意**：本机 ttyUSB0 这块板 App 级串口 RX 疑似硬件损坏（AT 无响应、写指令无任何效果；ROM 级 esptool 正常、TX 正常、luatos 同模式正常——2026-09-06 实测，PIT-030）。

## STRUCTURE

15 firmware modules (1:1 `.c`/`.h`) + vendored cJSON in `main/`. Web UI in `main/web_ui/` (SPIFFS). Docs in `docs/{en,zh}/`.

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Boot sequence / entry | `main/main.c` | 19-step boot; WiFi callback → deferred STA services task (camera, MJPEG, web, motion, recorder) |
| Pin mappings, config struct, enums | `main/common.h` | `CAM_PIN_*`, `SD_PIN_*`, `cam_config_t` (version 9, magic `0xA5B6C7D8`), `wifi_state_t` |
| NVS config persistence + SD override | `main/config_manager.c/h` | Version 9, migration support; `config_load_from_sd()` reads `/sdcard/config.txt` |
| WiFi AP/STA + callbacks | `main/wifi_manager.c/h` | Max 4 callbacks, state machine, reason-code logging, dual-network failover |
| OV2640 camera driver | `main/camera_driver.c/h` | PSRAM, GPIO14 sharing, DMA freeze workaround |
| Video recording (AVI segmented) | `main/video_recorder.c/h` | Init in deferred STA services task |
| REST API + SPIFFS static | `main/web_server.c/h` | 10+ endpoints, WebSocket support |
| MJPEG streaming | `main/mjpeg_streamer.c/h` | 2-client limit, async mode |
| Motion detection | `main/motion_detect.c/h` | Frame-diff, brightness flash trigger |
| SD card storage | `main/storage_manager.c/h` | FAT, hot-plug monitor, photo cache warming |
| Serial AT config | `main/at_command.c` + `main/at_port.c` | 家族 AT 核心（四仓 md5 一致）+ 板级 port，契约 v1.1（2026-09-05 取代 serial_config.c） |
| Prometheus metrics | `main/health_monitor.c/h` | `/metrics` endpoint, 10s interval |
| Timelapse | `main/timelapse.c/h` | Burst capture, configurable interval |
| Partition layout | `partitions.csv` | nvs 24KB + phy_init 4KB + factory 3.5MB + otadata + spiffs 432KB |
| SDK defaults | `sdkconfig.defaults` | PSRAM, legacy I2C, size opt, WiFi PHY full cal, stack tuning |
| Build dependencies | `main/idf_component.yml` | `espressif/esp32-camera ^2.1.6` |
| CI pipeline | `.github/workflows/release.yml` | `espressif/idf:v6.0` container, merged binary |

## REST API Endpoints

> **2026-09-02 契约 v1.0 统一化**（权威规范：`docs/api-contract.md`，下表已部分过时）：
> 新增 `GET /api/scan`（setup.html 的扫描按钮从"优雅降级"变为真实可用）；
> capabilities 增加 `api_version`/`wifi_scan`；status 字段对齐契约
> （`sensor`→`camera`、`wifi_mode`→`wifi_state` 小写枚举（原中文值废除，中文展示移至前端）、
> 新增 `stream_clients_max`=1）。MPA(index.html) 已同步改读新字段。
> 注：本仓实际分区为双 OTA 1.5MB×2 + SPIFFS 956KB（本文旧描述"单 factory 3.5MB/无OTA"已过时，OTA 端点存在且可用）。
>
> **契约 v1.1（2026-09-02）**：公开默认密码统一为 `mibeecam2026`（Kconfig 默认值，可入文档；本地部署可在 gitignored sdkconfig 用 `CONFIG_MIBEE_CAM_DEFAULT_WEB_PASSWORD` 覆盖；空密码加载自动迁移）、拒绝 <6 位密码；
> `/api/timelapse/*` 三端点已移除（启停走 POST /api/config 的 `timelapse_enabled`，
> 运行态在 GET /api/status 的 `timelapse_running`/`timelapse_photo_count`）；
> `/api/led` 新增 JSON body 主语义（`{"brightness":0-100}`），`?action=` 保留兼容；api_version=1.1。

All business endpoints use the `/api/` prefix. Returns JSON envelope `{"ok":true,"data":...}` on success, `{"ok":false,"error":"..."}` on failure.

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/api/status` | open | Device status (WiFi, camera, system, storage) |
| GET | `/api/config` | open | Current configuration (passwords masked) |
| POST | `/api/config` | write | Partial config update; first-time password setup when `web_password` is empty |
| GET | `/api/capabilities` | open | Board capability flags (12 booleans) |
| GET | `/api/capture` | open | Single JPEG snapshot (`image/jpeg`, not JSON) |
| GET | `/api/scan` | open | WiFi AP scan |
| POST | `/api/reset` | write | Factory reset config to defaults |
| POST | `/api/reboot` | write | Reboot device |
| GET | `/api/record` | open | Recording status |
| POST | `/api/record` | write | Start/stop recording (`?action=start|stop`) |
| GET | `/api/files` | open | List SD files (`?type=all\|photos\|recordings&offset=&limit=` ≤200，含 `total`；2026-09-03 修复 type=all 翻页错位) |
| DELETE | `/api/files` | write | Delete a file (`?name=...&type=photo\|recording`，缺省 photo；此前删录像必失败已修) |
| POST | `/api/files/batch` | write | 批量删除 `{names:[...]}` 或 `{scope:"all\|photos\|recordings"}` → `{deleted,failed}`（契约 v1.2，跳过正在写的录像段） |
| GET | `/api/download` | open | Download file (`?name=...&type=photo|recording`) |
| POST | `/api/format` | write | **申请-重启-开机格式化**：置 NVS 标志→应答 2s 后重启→main.c Step 5.5 相机初始化前执行格式化并清除标志。运行时格式化必挂死（GPIO14 相机/SD 共享 SPI），2026-09-03 前该端点因此 503 且未鉴权，均已修 |
| GET | `/api/ota/info` | open | OTA status/info |
| POST | `/api/ota/upload` | write | Upload firmware binary |
| POST | `/api/ota/spiffs` | write | Upload SPIFFS image |
| POST | `/api/led` | write | Flash LED control (`?action=on|off|toggle`) |
| GET | `/api/led` | open | Flash LED state |
| POST | `/api/timelapse/start` | write | Start timelapse |
| POST | `/api/timelapse/stop` | write | Stop timelapse |
| GET | `/api/timelapse/status` | open | Timelapse status |
| OPTIONS | `/*` | — | CORS preflight (204 No Content) |

**Auth:** `X-Password` header for write operations. When `web_password` is empty (first boot), all writes return 401 `SET_PASSWORD_FIRST` except `POST /api/config` with a `web_password` field (first-time setup).

**MJPEG stream:** Separate TCP server on port `:81` (independent of main web server on port 80).

**Exempt paths:** `/metrics` (Prometheus), `/onvif/*` (SOAP) are not under `/api/`.

### 2026-09-03 晚 WiFi 三连修（"一直连不上"事故，全部已烧录验证）

现场：主 AP（真实 SSID 脱敏，见本机根工作区文件）在板位 RSSI **-82dBm**（低于 DHCP
可用阈值），备用 AP（SSID 脱敏）-63~-66dBm。三固件缺陷叠加把弱信号放大成"完全失联"：

1. **DHCP 挂死盲区**：关联秒成但 DHCP 广播全丢 → 每 60s 被 AP 踢掉才计 1 次断开，
   熬满 10 次 ≈10 分钟才切备用。修复：关联后 12s 无 IP 主动断开计 DHCP 超时，
   连续 2 次立即切换（wifi_manager.c，`DHCP_TIMEOUT_MS/DHCP_FAILOVER_AFTER`）。
2. **每次启动先扎主网黑洞**。修复：NVS(`wifi_pref/last_net`) 记住上次拿到 IP 的
   网络，`wifi_start_sta_preferred()` 直接从好网启动——实测二次上线 40s → **3.3s**。
3. **httpd 自愈误杀（重启放大器，最恶性）**：health_monitor 6×10s 探测失败 =
   esp_restart()，而 WiFi 掉线时探测必失败（EHOSTUNREACH）→ **掉线 60s 被翻译成
   重启** → 更多掉线 → 循环。日志特征：`recv 113`×N → wifi txq stop →
   `rst:0xc` 无 panic 文字。修复：WiFi 未连接时不计数。
   （与 seeed 2026-09-02 自愈误触发同族——**任何"探测失败→重启"逻辑都必须排除
   网络不可达**，家族其余仓排查时注意。）

工具链：`tools/overnight_log.py /dev/ttyUSB0` 常驻采集器已部署。CH340 特有：
pyserial open 默认断言 RTS 会把 EN **持续按在复位态**（采集器 0 字节输入）——
open 后必须立即 `ser.rts=False; ser.dtr=False`（三仓采集器已同步此修复）。

物理层事实：这块 PCB 天线在当前位置主网不可用，固件只能保证"快速落在最好的
网上"；要根本解决需挪位/外接天线。

### 2026-09-03 晚二连修（"wifi 一直不稳定"= EMFILE 重启循环）

用户报"139 WiFi 不稳定"。串口日志实证：**不是射频问题，是 socket 表满重启循环**
（`E httpd: httpd_accept_conn: error in accept (23)` → health 探测 6×10s 失败 →
`rst:0xc` → 重启后 ~12s 又满 → 循环，一段日志内 21 次重启）。

4. **EMFILE 病根（同 luatos/seeed）**：SPA 流看门狗每 ~7s 重连 :81，被踢连接在
   设备侧留 TIME_WAIT（默认 2×MSL=120s），默认 `LWIP_MAX_SOCKETS=10` 必爆。
   `sdkconfig.defaults`：`CONFIG_LWIP_MAX_SOCKETS=16` + `CONFIG_LWIP_TCP_MSL=15000`
   （与 luatos 仓已验证配置一致）。**改后已删 sdkconfig 重配烧录，EMFILE 消失**。
5. **status 新字段**：`current_ssid`（当前实连 SSID，区别于 config 配置值）、
   `wifi_net`（`primary|secondary` 槽位，`wifi_using_secondary()`）、`wifi_channel`。
   seeed 同名字段先例，契约文档已补。启动后前 ~30s 探测可能 3 次超时（无 EMFILE、
   40s 自愈、从未到 6/6）——观察项，与 SD 预热/camera warm 期 httpd 忙有关。
6. **SPA 双 WiFi 配置回归**：统一切 SPA 时丢了旧 config.html 的备用 WiFi 表单。
   WiFi 页新增：当前连接行（SSID+槽位+信道+RSSI）、备用 SSID/密码（仅当
   `GET /api/config` 返回 `wifi_ssid_2` 才显示——n16r8 POST 是白名单校验，绝不
   给它发该键）；保存 WiFi 凭据后 1.5s 自动 `/api/reboot` 生效。头行 `hd-sub`
   也显示当前 SSID。四仓 SPA md5 已重新拉齐。

### 2026-09-03 深夜 契约 v1.2（SD 管理三件套 + cleanup 语义统一，已烧录验证）

7. **status 补齐家族字段**：`sd_present`/`sd_total_bytes`/`sd_free_bytes`/
   `sd_free_percent`/`recording`（seeed 先例）。此前统一 SPA 的存储卡片要求
   `sd_present===true` 才显示容量，本板缺该字段 → 永远显示"未检测到 SD 卡"
   而文件列表正常（用户报障"存储显示未检测sd卡却列出一大堆文件"的根因）。
   SPA 同时加了 `sd_total_mb` 回退，双保险。
8. **批量删除 `POST /api/files/batch`**（names/scope 二选一，见 API 表），
   列表分页修复（type=all 时 offset 现按合并后列表统一切片）。
9. **格式化走"申请→重启→开机格式化"**（API 表格式化行有细节）。实测全链路 OK：
   请求→2s→重启→Step 5.5 格式化→清标志→正常启动，之后重启不再触发。
   ⚠️ 格式化会真删数据——**2026-09-03 当晚新 UI 上线 2 分钟后按钮即被点击执行，
   卡上 729 个文件被清空**（按钮有双重确认，属正常使用）。
10. **cleanup_low_pct/high_pct 语义统一为"空闲百分比"**（V15→V16 迁移重置 20/30）。
    旧语义是"已用百分比"（线上 80/30 曾意味着"删到只剩 30% 已用"）；
    config POST 校验同步反转（现要求 low < high）。
11. **诊断教训**：本板弱射频下 httpd 会间歇无响应（RTT 飙到 100-200ms、
    curl 000）但 ping 通、无 EMFILE、无重启——先怀疑链路再怀疑固件；
    判据：连续探测中间夹着 200 = 链路抖动，不是挂死。

   **诊断流程（下次"WiFi 不稳定"照此走，别先怪天线）**：
   ① `/api/status` 看 `uptime`——几十秒说明在重启循环；② 采集器
   `overnight_reboots.log` 里找签名 `httpd_accept_conn: error in accept (23)` +
   `probe failed (n/6)` + `rst:0xc`，三者连读即 EMFILE 自愈误杀；③ 修复顺序：
   先 `LWIP_MAX_SOCKETS=16`+`TCP_MSL=15000`（改 defaults 后删 sdkconfig 重配），
   再考虑射频/省电（本板 `wifi_power_save` 出厂即 0=PS_NONE，无需动）。
   本地 ping 抖动大（20~100ms）在重启循环期间测无意义，修复后再测基线。

### 2026-09-04 摄像头上限实测 + 配置校验收紧（已烧录验证）

**实测**（90s MJPEG 探针 + `/api/capture` 计时，SPA 看门狗互踢噪音下限）：
| 档位 | 推流 fps | 采集侧 fps | p95 帧大小 | 稳定性 |
|---|---|---|---|---|
| SVGA q12 | 4.4 | ~5+ | 26KB | 稳 |
| XGA q12 | 2.7 | ~4.5 | 46KB | 稳 |
| UXGA q12 | 0.62 | ~1.7 | 184KB | 稳（无崩溃/重启，链路 ~130KB/s 是瓶颈）|
| UXGA q10 | 0.56 | — | 224KB | 稳，224KB < fb 上限 384KB（w*h/5）|

UXGA 保留在选项里（拍照/延时有价值、设备零故障），但流是幻灯片——用户须知。
**限制落地**：`camera_driver.h` `CAMERA_QUALITY_MIN/MAX = 10/63`（esp32-camera JPEG fb
按 w*h/5 分配，q<10 场景复杂时超预算截帧）；`GET /api/camera` 新增
`quality_min/quality_max`（SPA 滑杆据此钳制）；POST /api/camera 与 /api/config 的
framesize 校验 **0-24→0-3**（原先可传越界枚举值！）、quality **0-63→10-63**；
`config_set_jpeg_quality` 与 NVS 加载钳制旧值。分辨率变更仍走互斥锁热重配
（OV2640 运行时 set_framesize 有效——与 seeed 的 OV5640 不同，PIT-019）。

### 2026-09-04 分辨率三层上限（家族统一）

`camera_get_effective_max_res() = min(sensor, board, memory)`（camera_driver.c，
细节见 PITFALLS PIT-021 附录）：sensor 层查 esp32-camera 组件能力表
（`esp_camera_sensor_get_info().max_size`，勿手抄 PID 表），换传感器候选表自适应；
board 层 `CAMERA_RES_BOARD_MAX=UXGA`（本板上限=传感器上限，实测矩阵见上节）；
memory 层 PSRAM fb 预算（256K floor，只能收紧）。`GET /api/camera` 下发
`res_cap_source`（sensor/board/memory）；列表/POST/AT+CAMRES 全部改走 effective。
本板满配不变（0-3 四档、source=sensor）。

### 2026-09-04 晚 API parity：free_psram 补齐（契约 §4 违约修复，已烧录验证）

本板有 4MB PSRAM 但 /api/status 一直没发 `free_psram`（SPA 的 PSRAM 芯片/
系统面板行因此缺失）。同轮 n16r8 补齐 wifi_rssi/wifi_channel/chip_temp
（用户报障"119 无信号显示"的根因）。
**USB 部署验证（2026-09-04 晚，ttyUSB0）**：弱射频 Web OTA 连续 6 次断流后
改串口烧写（两块 CH340 无序列号致 by-id 名字冲突，认口靠 ttyUSB0/ttyUSB1 区分）。
上板全过：`res_cap_source=sensor`（OV2640 传感器上限即板上限）、四档列表、
POST 越界 400 带来源、`free_psram` 3.8MB、capture 0.17s、ΣΔ 流水线与暗度探针
正常（暗景 2-3% luma）、单次干净启动无 rst。注意本仓 CH340 烧后仍需
`esptool --chip esp32 --no-stub run` 释放复位。

## Web UI

> **2026-09-03 起统一为家族 SPA（用户拍板）**：`/` 现服务与三 S3 仓 md5 一致的统一 SPA
> 四文件（index.html/app.js/i18n.js/style.css，能力驱动：本板自动显示 存储/录像/延时/闪光
> 标签，隐藏 WS/音频）。实测前提全部满足：:81 流已带 ACAO 头（crossOrigin 看门狗可用）、
> 静态服务有 no-cache、SPIFFS ~950KB 富余。原 MPA 页面**保留在原路径作兜底**
> （preview/config/files/setup.html 直接 URL 可达）。CMakeLists 已加 spiffs `DEPENDS`
> 显式文件依赖（同三仓陷阱：目录级依赖不随内容编辑触发）。
> 弱射频下首载可能撞掉线窗口——SPA 首载失败会停在骨架屏，**刷新即恢复**（boot 链无重试是
> 已知限制）。`/api/camera` 热重配走互斥锁+drain_outstanding_fbs 安全路径，无需重启应用
> （与 luatos 的重启方案不同，本板 fb 归还计数做对了）。

多页面应用（MPA），中文优先，每页独立内联 JS + 共享 `style.css` 设计系统。针对 OV2640/无 AI 精简适配，不是从 S3 照抄。

| 文件 | 作用 |
|------|------|
| `style.css` | 共享设计系统（teal 主色/卡片/状态点/徽章/进度条），~6KB，浏览器缓存后二刷每页只传 HTML |
| `index.html` | 仪表盘：设备/摄像头/WiFi/存储/移动侦测/内存/录像状态，10s 轮询 `/api/status`+`/api/record`，无推流（省带宽） |
| `preview.html` | 实时预览：MJPEG `:81/stream` + 弱 WiFi 截图兜底（`/api/capture` ~1fps）；快捷改分辨率/质量/录像/闪光/拍照 |
| `config.html` | 全配置（折叠分组）+ 固件/WebUI OTA 上传。摄像头仅 4 档（VGA/SVGA/XGA/UXGA），无 RTSP 假控件 |
| `files.html` | SD 文件管理：照片/录像分页列表 + 下载/删除 |
| `setup.html` | 首次配网向导（AP 模式深色主题）：WiFi+设备名+管理密码+时区。必须含 `web_password`（SET_PASSWORD_FIRST 状态机） |

**关键 API 对齐（曾因照抄 S3 出错）：** `/api/status` 返回 `resolution`/`stream_clients`（非 camera_resolution/mjpeg_clients）；摄像头字段是 `cam_framesize`/`cam_quality`/`cam_vflip`；闪光是 `POST /api/led?action=toggle`（非 JSON body）；推流在独立 `:81` 端口；拍照是 `/api/capture`。

**性能：** 首屏 ~6KB（index+style.css），原 S3 SPA 首屏 ~68KB。无 i18n 模块（中文优先，省 11KB）。

## Capabilities

This board returns the following from `GET /api/capabilities`:

| Capability | Supported |
|------------|-----------|
| ai | ❌ |
| sd | ✅ |
| audio | ❌ |
| ota | ✅ |
| mic | ❌ |
| flash_led | ✅ |
| recording | ✅ |
| timelapse | ✅ |
| onvif | ✅ |
| rtsp | ❌ |
| websocket | ❌ |
| mdns | ❌ |

## ESP-IDF BUILD

### Environment

ESP-IDF v6.0.1 at `~/.espressif/v6.0.1/esp-idf/`. Other versions: v5.4.4, v5.5.4.

```bash
export IDF_PATH=~/.espressif/v6.0.1/esp-idf
. $IDF_PATH/export.sh
idf.py --version   # → ESP-IDF v6.0.1
```

### Commands

```bash
idf.py set-target esp32          # ESP32 only (NOT S2/S3)
idf.py build                      # Incremental build
idf.py -p /dev/ttyUSB0 flash      # USB fallback ONLY — see flashing policy below
idf.py -p /dev/ttyUSB0 monitor    # Serial monitor
idf.py fullclean                  # Removes build/ + managed_components/

# Regenerate sdkconfig from defaults (REQUIRED after editing sdkconfig.defaults):
rm sdkconfig && idf.py set-target esp32 && idf.py build
```

**Flashing policy (root AGENTS.md, 2026-09-04): Web OTA is the default delivery path
for this repo** (dual OTA slots 1.5MB×2, endpoints live — see API table above):

```bash
curl -X POST http://<ip>/api/ota/upload -H 'X-Password: <pwd>' \
     -H 'Content-Type: application/octet-stream' \
     --max-time 300 --data-binary @build/mibee_cam.bin      # firmware → next slot → reboot
curl -X POST http://<ip>/api/ota/spiffs -H 'X-Password: <pwd>' \
     --data-binary @build/spiffs.bin                        # UI (erases SPIFFS; risky on weak WiFi)
```

Verify: `/api/ota/info` `running_partition` flipped; after a UI upload, device
`/app.js` md5 == repo file (PIT-017). USB only for blank chips, rescue, or
network-unreachable devices.

**`sdkconfig` is generated and gitignored.** A stale `sdkconfig` silently overrides `sdkconfig.defaults` — always delete it after changing defaults.

### Flash output

- App: `build/mibee_cam.bin` (~1.06 MB, ~30% of 3.5MB factory partition)
- Also: `bootloader.bin`, `partition-table.bin`, `spiffs.bin`, `ota_data_initial.bin`
- `build/flash_args` has pre-computed esptool offsets

### Booting after flash (RTS trap)

`idf.py flash` resets the chip via RTS after writing, but on this board the post-flash reset does **not** reliably boot the new firmware — the chip can sit with the RTS line held in a state where nothing runs. Symptom: flash reports success, but the device is silent on serial and the web UI never comes up.

Explicitly boot the freshly-flashed image with:

```bash
esptool --chip esp32 -p /dev/ttyUSB0 --no-stub run
```

`--no-stub` is required: the default stub soft-reset leaves the chip in the same stuck state. `run` releases the reset lines so the bootloader actually executes.

This only matters after `flash`. `idf.py monitor` alone also won't recover it — you need the `esptool ... run` invocation.

### SPIFFS

`spiffs_create_partition_image(spiffs main/web_ui FLASH_IN_PROJECT)` in root `CMakeLists.txt` auto-packages `main/web_ui/` into `build/spiffs.bin` at build time.

## WiFi (CRITICAL GOTCHAS)

This board has **marginal WiFi RF** (AI-Thinker-class PCB antenna + distance to router). Several non-obvious config choices compensate:

### PHY calibration — full every boot

`sdkconfig.defaults` forces `CONFIG_ESP_PHY_RF_CAL_FULL=y` + `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n`. The default (partial cal + stored NVS data) produces unreliable results on this board — symptoms: `AUTH_EXPIRE` (reason=2), `NO_AP_FOUND` (reason=201), intermittent connect/fail across reboots.

If WiFi won't connect after a change, **erase the phy_init partition** to force fresh calibration:
```bash
python3 -m esptool --port /dev/ttyUSB0 erase_region 0xf000 0x1000
```

### STA config (`wifi_manager.c: wifi_start_sta`)

- `threshold.authmode = WIFI_AUTH_OPEN` — auto-negotiate WPA2/WPA3/mixed/WPA/OPEN
- `pmf_cfg.capable = true, .required = false` — PMF capable for WPA3 transition APs
- `sae_pwe_h2e = WPA3_SAE_PWE_BOTH` — WPA3 SAE compatibility
- `WIFI_BW20` (HT20) — ~3dB better RX sensitivity than HT40 for weak signals
- TX power: 20 dBm (80 quarter-dBm) — do NOT lower; marginal antenna needs full power

### Reason code diagnostics

`wifi_disconnect_reason_str()` in `wifi_manager.c` maps reason codes to readable strings. Key codes:
- `reason=2 AUTH_EXPIRE` → AP can't hear ESP32 TX (signal/calibration, NOT password)
- `reason=15/204` → 4-way handshake timeout (likely wrong password)
- `reason=201 NO_AP_FOUND` → wrong SSID or out of range
- `reason=211` → authmode threshold mismatch

### Stack overflow risks

- `esp_timer` task (6144 bytes) — overflows under WiFi reconnect stress. If adding WiFi-related timers, may need 8192+.
- `warm_cache` task (8192 bytes) — overflows with 700+ SD photos at 4096. Any task doing fatfs directory traversal needs ≥8192.

## SERIAL DEBUGGING

Serial port: `/dev/ttyUSB0` (CH340 USB-serial). User must be in `dialout` group.

To capture boot log with hardware reset (ESP32 EN on RTS, GPIO0 on DTR):
```python
import serial, time, sys
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.5)
ser.reset_input_buffer()
ser.setDTR(False); time.sleep(0.05)   # GPIO0 high = normal boot
ser.setRTS(True);  time.sleep(0.1)    # EN low = reset
ser.setRTS(False)                      # release → firmware boots
# then read for N seconds
```

For panic/backtrace analysis: the ELF at `build/mibee_cam.elf` + `xtensa-esp32-elf-addr2line` resolves addresses. Note: corrupted backtraces (`|<-CORRUPTED`) usually mean stack overflow, not the displayed call chain.

## CONVENTIONS

- **Naming**: `snake_case` functions/vars, `snake_case_t` typedefs, `UPPER_CASE` pin/constant macros
- **Module pattern**: 1:1 `.c`/`.h`, `module_init()` / `module_start()` / `module_get_*()` / `module_set_*()`
- **Static prefix**: `s_` for module-level static vars
- **Logging**: `ESP_LOGx(TAG, ...)` exclusively, errors include `esp_err_to_name(ret)`
- **Error handling**: `esp_err_t` returns, check `!= ESP_OK`, log + return
- **Comments**: Doxygen on public functions, `/* ── Section ── */` dividers, `/* */` for workaround rationale
- **Config struct**: `cam_config_t` in `common.h`, persisted in NVS, magic+version validated on load

## GOTCHAS

- **GPIO14 shared**: SD card CLK and camera XCLK share GPIO14. Init order: release SD bus → camera init → SD init. After camera init, some fatfs operations (`f_getfree`, `stat`, `opendir`) may hang — only `mkdir`/`fopen`/`fwrite`/`fclose` are reliable.
- **Camera init deferred**: Must happen AFTER WiFi STA connection (ESP32 DMA freeze workaround, esp32-camera#620).
- **BOOT button unusable**: GPIO0 is camera XCLK — cannot use for factory reset. Use `POST /api/reset`.
- **No OTA**: Single factory partition. Firmware updates via serial flash only.
- **cJSON vendored in `main/`**: 3rd-party (known issues: O(n²) comparison, int overflow). Not a managed dependency.
- **No test infrastructure**: All validation is runtime error-checking on hardware.
- **`nas_uploader.c/h` referenced in old docs but does not exist** in this project.

## NOTES

- **Config version 9** — if adding fields to `cam_config_t`, bump `CONFIG_VERSION` in `common.h` and add migration logic in `config_manager.c` (`config_load()` handles version-skipped migration).
- **PSRAM required** for camera frame buffers. Board has 8MB PSRAM, 4MB usable.
- **Hardware-specific**: MiBee Cam only. Pins differ on ESP32-S3 (see sister project `seeed-esp32s3-cam`).
- **WiFi antenna**: Board has PCB antenna + optional IPEX external antenna (requires soldering 0Ω select resistor). PCB antenna has marginal range.

## 2026-09-04 上午家族修复同步
- **MJPEG accept 路径补 `SO_SNDTIMEO=10s`**（PIT-024 家族同步；本仓死客户端探测已有）。
- **统一 logo**：`main/web_ui/favicon.svg`（蜂窝六边形+双蜜黄条纹，四仓同 md5），
  `index.html` 页头 `<img class="brand-logo">` + `<link rel="icon">`；`style.css` 加 `.brand-logo`。
  四仓 web_ui 五文件 md5 必须一致（共享 SPA 纪律）。
- **删除遗留静态页** `config/files/preview/setup.html`（pre-SPA 产物，SPA 已完全覆盖其功能，
  且 seeed/n16r8 的 SPIFFS 容量本就装不下；git 历史可找回）。
- **web_ui 新增文件必须 `idf.py reconfigure`**（file(GLOB) 陷阱，PIT-026）；烧录前停该口采集器。

## 2026-09-04 运动检测算法重做（ΣΔ 像素域流水线，外部报告评估落地）

外部团队报告（暗光运动检测算法调研）评估结论 + 落地实现。**评估三诊断全部属实**
（JPEG 字节比对无物理意义 / JPEG 体积当亮度暗光方向反转 / 无时空验证），
报告声称"已交付"的代码实物并不存在——全部由本仓自行实现。

- **核心文件**：`main/lm_motion.c/h`（七级 ΣΔ 流水线，纯 C 整数运算，调用方供内存）
  `main/lm_jpeg.c/h`（TJpgDec 1/8 DC-only 解码，vendor 在 `components/tjpgd/`）
  `main/lm_dark_probe.c/h`（锁定曝光暗度探针）+ `motion_detect.c` 重写。
- **桌面回归**：`tools/lm_sim/`（gcc 主机仿真，复现报告 σ×对比度矩阵）——
  **42 组全部 0 误报**；边界 σ=8→C40（=4.5σ̂ 精确复现）、σ=4/5→C30（较报告保守一档，
  换全矩阵零误报）、σ≥12 静默门（报告 §2 自己的"宁可漏报"教义）。改 lm_motion 必跑。
- **与报告的三处偏差（均有实测依据）**：
  1. 条件式 ΣΔ（ICIP09 Alg.2）在重噪声下逐像素冻结振荡、σ̂ 塌缩——改回无条件更新
     （Alg.1），全局鲁棒性由③级重收敛承担；
  2. 暗度探针不切 GRAYSCALE 格式（报告自己的最大告警项：deinit/init 100-200ms 断流），
     改为**锁 AEC/AGC + 复用运动解码器的亮度均值**——同物理、零切换成本；
  3. blob 外接框上限随网格缩放（固定 70 在真机上把 1 米内的人整框拒掉：fg=10% blobs=0）。
- **上板实测**（.139）：SVGA→100×75 网格 79ms/帧、VGA→80×60 44ms/帧，3.4fps 分析；
  探针锁定曝光 luma=9→3% 判暗（旧 JPEG 体积法同场景报 100%——方向反转实锤）；
  闪光灯 3 次开灭零误触（③级真机验证）；静态 6min+ 零事件；两次触发均带
  `TRIGGER ctx` 能量上下文（短暂运动的尾帧触发，E 先冲高后归零属 N-of-M 设计代价）。
- **诊断**：`/api/status` 新增 `motion_diag`{sigma_x100, energy, energy_smooth, fg_pct,
  blobs, mode, luma_mean, decode_us, frames}；`brightness_method` 语义修正：
  1=自动曝光亮度回退（原标签"register"是历史错标），2=锁定曝光探针。
- **遗留观察**：本板弱网漫游（primary -81dBm）令推流/状态访问忽好忽坏——
  流媒体 fps 波动是链路问题非流水线（A/B：motion ON 时反而最高）；SD readdir
  在相机运行后不可靠是老毛病（按名下载正常，列表缓存空）。
- **参数映射**：`motion_threshold`（0-100，默认 30）映射 e_hi=12+thr/5、e_lo=e_hi/2+1；
  `flash_threshold`（默认 40）为暗度判定线；探针周期 30s 仅在无观众/非录制时运行。

### 探针诱发误触的三连修复（2026-09-04 上板迭代，验收浸泡通过后定稿）
1. **探针过渡帧误触**：锁/恢复曝光的过渡帧在 DC 域（σ̂≈0、V 落 vmin）产生补丁式
   ±2DN 量化漂移 → 成形 blob E=60-120 → 每个探针周期必触发。修复：探针全程
   `lm_motion_flash_guard(30)` + `vmin 2→4`（吸收纯量化闪烁，仿真复验 42 组仍全零误报）。
2. **引导顺序缺陷**：首次探针跑在流水线首次解码之前 → guard 被 `init_buf→reset()`
   清零 → AEC 恢复漂移在 warmup 结束后以 E=61 blob 触发。修复：探针门控
   `s_lm_inited && frame_no≥30`。
3. 验收浸泡（终版固件）：10min 仅 1 次触发（用户真实运动），探针周期零诱发，
   LED 照度阶跃×3 零误触，VGA/SVGA 双几何冷启动干净。
