/*
 * motion_detect.c — motion-triggered photography: trigger source + locked-
 * exposure darkness probe + auto-flash photo chain.
 *
 * Two trigger modes (compile-time, CONFIG_MIBEE_CSI_MOTION):
 *
 *  - CSI mode (2026-09-09, replaces the pixel pipeline as this board's
 *    production trigger): motion verdicts come from the ESPectre WiFi CSI
 *    snapshot (RF domain — immune to darkness, AEC transients and the
 *    flash LED itself, which the old pixel pipeline had to guard against).
 *    This task only polls the snapshot (250ms), keeps the darkness cache
 *    warm and runs the photo chain. The per-frame JPEG decode + 7-stage
 *    ΣΔ analysis and its ~60KB INTERNAL working buffer are retired.
 *
 *  - ΣΔ mode (gate closed, family default): decodes each analysis frame to
 *    a 1/8 grayscale grid (TJpgDec DC path, lm_jpeg.c) and runs the 7-stage
 *    ΣΔ pipeline in lm_motion.c (validated on the host sim: 0 false
 *    triggers across the σ×contrast matrix, detection boundary ≈ 4.5σ̂).
 *
 * Darkness (both modes): lm_dark_probe.c locks AEC/AGC and measures true
 * luma — the mean of frames under auto exposure is only a fallback
 * indicator. CSI mode adds an on-demand probe at event time (see
 * scene_dark_decision) so a 24/7 NVR viewer can no longer starve the
 * darkness cache and silently disable night flash photos.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motion_detect.h"
#include "camera_driver.h"
#include "frame_broadcaster.h"
#include "config_manager.h"
#include "storage_manager.h"
#include "time_sync.h"
#include "health_monitor.h"
#include "flash_led.h"
#include "mjpeg_streamer.h"
#include "video_recorder.h"
#include "lm_motion.h"
#include "lm_jpeg.h"
#include "lm_dark_probe.h"
#if CONFIG_MIBEE_CSI_MOTION
#include "csi_motion.h"
#include <string.h>
#endif

static const char *TAG = "motion_detect";

#define MOTION_TASK_PRIORITY   5
#define MOTION_TASK_STACK_SIZE 8192

#define ANALYSIS_INTERVAL_MS   250    /* ~4 fps analysis cadence */
#define PROBE_PERIOD_MS        LM_PROBE_PERIOD_MS
#define STOP_WAIT_MS           5000
/* CSI 模式：事件时刻探针缓存的信任窗（超过即按需即时探测） */
#define CSI_DARK_CACHE_MS      120000
/* 暗场闪光拍照：点亮后丢弃的帧数（AEC/AWB 再收敛），抓其后的第一帧 */
#define MOTION_FLASH_WARMUP_FRAMES  3

/* ---- Static state ---- */
static TaskHandle_t s_motion_task_handle = NULL;
/* 任务静态分配（PIT-039）：本板内部 RAM 碎片化下 8KB 运行期栈分配启动即败
 * （连续多日日志 E (4064) Failed to create motion detection task）。
 * .bss 栈/TCB 无碎片风险；IDLE 清理跳过静态内存，vTaskDelete 安全。 */
static StaticTask_t s_motion_task_tcb;
static StackType_t  s_motion_task_stack[MOTION_TASK_STACK_SIZE / sizeof(StackType_t)];
static volatile bool s_running = false;
static bool s_in_cooldown = false;
static int64_t s_cooldown_start_us = 0;
static volatile bool s_motion_recent = false;  /* 最近分析周期持续检出（契约 §3.2 active 语义） */
#if !CONFIG_MIBEE_CSI_MOTION
static volatile bool s_cfg_dirty = false;      /* 灵敏度等参数变更 → 重建管线 */
#endif
static uint8_t s_brightness_pct = 50;
static uint8_t s_bright_method = 0;   /* 0=init 1=luma fallback 2=locked probe */
static bool s_scene_dark = false;
static uint32_t s_unknown_seq = 0;

/* lm_motion working state — grid is a fixed max-size scratch (largest
 * supported frame UXGA 1600×1200 → 200×150); the pipeline re-inits when
 * the decoded geometry changes. CSI 模式仍用它做探针/兜底的解码画布。 */
#define LM_GRID_MAX_W 200
#define LM_GRID_MAX_H 150
/* PSRAM 运行期分配（PIT-039）：原 30KB .bss 把 DRAM 最大连续区吃到 httpd
 * 任务栈（6KB）都分配不出来；网格是 ~4fps 冷访问、非 DMA，PSRAM 零损。 */
static uint8_t *s_grid = NULL;
#if !CONFIG_MIBEE_CSI_MOTION
static int s_grid_w = 0, s_grid_h = 0;
static uint8_t *s_lm_buf = NULL;      /* pipeline working buffer */
static size_t s_lm_buf_size = 0;
static bool s_lm_inited = false;
#endif

/* diagnostics snapshot for /api/status */
static lm_result_t s_diag;
static int32_t s_decode_us = -1;
static volatile uint32_t s_frames_analyzed = 0;

/* probe scheduling */
static int64_t s_last_probe_us = -(int64_t)PROBE_PERIOD_MS * 1000;  /* probe soon after boot */

/* ---- Internal helpers ---- */

static bool is_in_cooldown(void)
{
    if (!s_in_cooldown) {
        return false;
    }

    const cam_config_t *cfg = config_get();
    int64_t elapsed_us = esp_timer_get_time() - s_cooldown_start_us;
    /* 家族超集模型：持续活动期以 active_interval_s 为再触发间隔；
     * 活动停止（s_motion_recent=false）后回退 cooldown_s。 */
    int64_t limit_us = (int64_t)(s_motion_recent ? cfg->motion_active_interval_s
                                                 : cfg->motion_cooldown_s) * 1000000LL;

    if (elapsed_us >= limit_us) {
        s_in_cooldown = false;
        return false;
    }

    return true;
}

#if !CONFIG_MIBEE_CSI_MOTION
/* (Re)configure the pipeline for the decoded grid geometry. Returns false on OOM. */
static bool analysis_alloc(int gw, int gh)
{
    size_t buf_sz = lm_motion_buf_size(gw, gh);
    if (s_lm_buf && !s_cfg_dirty && gw == s_grid_w && gh == s_grid_h && buf_sz <= s_lm_buf_size) {
        return true;   /* same geometry & config, keep pipeline state */
    }

    uint8_t *lmbuf = heap_caps_malloc(buf_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lmbuf) lmbuf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lmbuf) {
        ESP_LOGE(TAG, "pipeline buffer OOM (%ux%u)", (unsigned)gw, (unsigned)gh);
        return false;
    }
    free(s_lm_buf);
    s_lm_buf = lmbuf; s_lm_buf_size = buf_sz;

    lm_config_t lmcfg;
    lm_motion_defaults(&lmcfg);
    /* 家族超集模型（契约 §3.2）：motion_sensitivity 0-100（越大越灵敏）映射到
     * 管线能量触发阈值：sensitivity 70（默认）→ 内部 threshold 30 → e_hi 18
     * （sim 验证点），与旧版完全等价：internal_thr = 100 - sensitivity。 */
    lmcfg.e_hi = 12 + (100 - config_get()->motion_sensitivity) / 5;
    lmcfg.e_lo = lmcfg.e_hi / 2 + 1;
    /* Blob bbox gate must scale with the grid: a close subject fills the
     * frame (bbox ≈ grid size) and a fixed max_side=70 rejected it —
     * measured live: fg=10% blobs=0 E=0 with a person 1m from the lens.
     * Anti-noise is stage ③ + the σ̂ quiet gate, not the side limit. */
    lmcfg.max_side = (gw > gh) ? gw : gh;
    if (lm_motion_init_buf(gw, gh, &lmcfg, s_lm_buf, buf_sz) != 0) {
        ESP_LOGE(TAG, "lm_motion_init_buf failed");
        s_lm_inited = false;
        return false;
    }
    s_lm_inited = true;
    s_grid_w = gw; s_grid_h = gh;
    s_cfg_dirty = false;
    ESP_LOGI(TAG, "analysis grid %dx%d (e_hi=%d e_lo=%d)",
             gw, gh, lmcfg.e_hi, lmcfg.e_lo);
    return true;
}
#endif /* !CONFIG_MIBEE_CSI_MOTION */

/* Probe callback: pull one broker frame, decode, return its luma mean. */
static int probe_sample_luma(void)
{
    camera_fb_t *fb = NULL;
    if (frame_broker_get_copy(&fb, 1500) != ESP_OK || fb == NULL) {
        return -1;
    }
    int gw = 0, gh = 0, luma = -1;
    uint32_t acc = 0;
    if (lm_jpeg_decode_gray(fb, s_grid, &gw, &gh) > 0) {
        for (int i = 0; i < gw * gh; i++) acc += s_grid[i];
        luma = (int)(acc / (uint32_t)(gw * gh));
    }
    frame_broker_free(fb);
    return luma;
}

#if CONFIG_MIBEE_CSI_MOTION
static void probe_tick(void)
{
    /* Skip while anyone is watching or recording — the exposure lock makes
     * a couple of frames look wrong. (No ΣΔ model left to protect, so the
     * old preheat gate + flash-guard blanking are gone.) */
    if (mjpeg_streamer_get_client_count() > 0) return;
    if (recorder_get_state() == RECORDER_RECORDING ||
        recorder_get_state() == RECORDER_PAUSED) return;

    /* Settle 窗口内有帧可采（idle 2fps 下 550ms 只有 ~1 帧，提速更稳） */
    frame_broker_boost(8000);
    int pct = lm_dark_probe_run(probe_sample_luma);
    if (pct >= 0) {
        s_brightness_pct = (uint8_t)pct;
        s_bright_method = 2;
        s_scene_dark = lm_dark_probe_is_dark(pct);
        ESP_LOGI(TAG, "probe result: %u%% dark=%s", s_brightness_pct,
                 s_scene_dark ? "YES" : "no");
    }
    s_last_probe_us = esp_timer_get_time();
}

/* 事件时刻的暗场判定（三级，CSI 模式）：
 * 1) 探针缓存新鲜（<CSI_DARK_CACHE_MS）→ 直接用，常规路径零延迟；
 * 2) 录制中 → 不能上 AEC 锁（过渡帧会写进录制流），退回单帧自动曝光
 *    亮度（AGC 拉向中灰，偏亮侧的弱指示，仅作兜底）；
 * 3) 其余（典型：NVR 常驻观看令周期探针长期跳过）→ 即时锁定曝光探针，
 *    对观看流的代价约 2 帧短暂异常，换来正确的闪光判决。 */
static bool scene_dark_decision(void)
{
    int64_t age = lm_dark_probe_age_ms();
    if (age >= 0 && age < CSI_DARK_CACHE_MS) {
        return s_scene_dark;
    }

    if (recorder_get_state() != RECORDER_IDLE) {
        camera_fb_t *fb = NULL;
        if (frame_broker_get_copy(&fb, 1500) == ESP_OK && fb != NULL) {
            int gw = 0, gh = 0, luma = -1;
            uint32_t acc = 0;
            if (lm_jpeg_decode_gray(fb, s_grid, &gw, &gh) > 0) {
                for (int i = 0; i < gw * gh; i++) acc += s_grid[i];
                luma = (int)(acc / (uint32_t)(gw * gh));
            }
            frame_broker_free(fb);
            if (luma >= 0) {
                s_brightness_pct = (uint8_t)(luma * 100 / 255);
                s_bright_method = 1;
                s_scene_dark = lm_dark_probe_is_dark(s_brightness_pct);
                ESP_LOGI(TAG, "probe skipped (recording) — luma fallback: %u%% dark=%s",
                         s_brightness_pct, s_scene_dark ? "YES" : "no");
            }
            return s_scene_dark;
        }
        ESP_LOGW(TAG, "darkness unknown (recording, no frame) — flash off");
        return false;
    }

    frame_broker_boost(8000);
    int pct = lm_dark_probe_run(probe_sample_luma);
    s_last_probe_us = esp_timer_get_time();
    if (pct >= 0) {
        s_brightness_pct = (uint8_t)pct;
        s_bright_method = 2;
        s_scene_dark = lm_dark_probe_is_dark(pct);
        ESP_LOGI(TAG, "probe result (on-demand): %u%% dark=%s",
                 s_brightness_pct, s_scene_dark ? "YES" : "no");
    } else {
        ESP_LOGW(TAG, "on-demand probe failed — using last known darkness");
    }
    return s_scene_dark;
}
#else /* ΣΔ mode */
static void probe_tick(void)
{
    /* Pipeline must be live and settled first: the boot-time probe used to
     * fire before the first decode, its flash-guard was then wiped by
     * lm_motion_init_buf()->reset(), and the AEC restore transients right
     * after warmup triggered as E=61 blobs (2026-09-04 boot trace). */
    if (!s_lm_inited || s_diag.frame_no < 30) return;

    /* Skip while anyone is watching or recording — the exposure lock makes
     * a couple of frames look wrong. */
    if (mjpeg_streamer_get_client_count() > 0) return;
    if (recorder_get_state() == RECORDER_RECORDING ||
        recorder_get_state() == RECORDER_PAUSED) return;

    /* The probe's exposure lock/unlock produces transition frames with
     * patchy ±2DN DC quantization drift — measured live as E≈60-120 blobs
     * that triggered the pipeline every probe cycle. Blank the model for
     * the whole window plus settle time (2026-09-04 live trace). */
    lm_motion_flash_guard(30);
    int pct = lm_dark_probe_run(probe_sample_luma);
    lm_motion_flash_guard(30);
    if (pct >= 0) {
        s_brightness_pct = (uint8_t)pct;
        s_bright_method = 2;
        s_scene_dark = lm_dark_probe_is_dark(pct);
        ESP_LOGI(TAG, "probe result: %u%% dark=%s", s_brightness_pct,
                 s_scene_dark ? "YES" : "no");
    }
    s_last_probe_us = esp_timer_get_time();
}
#endif /* CONFIG_MIBEE_CSI_MOTION */

/**
 * @brief Handle motion detected event — capture photo with flash if dark.
 *
 * Flash warm-up is frame-count driven (report §7.4): after flash on we
 * discard frames until the sensor's AEC/AWB re-converges, rather than a
 * fixed 200ms.
 */
static void handle_motion_event(bool dark_scene)
{
    ESP_LOGI(TAG, "Motion detected!%s (scene %s)", dark_scene ? " (auto-flash)" : "",
             dark_scene ? "DARK" : "bright");
    health_monitor_incr_motion_events();

    if (!s_running) return;

    if (!storage_is_available()) {
        ESP_LOGW(TAG, "SD card not available, skipping photo save");
        return;
    }

    /* 拍照链全程提速（idle 2→5fps）：3 帧预热 + 抓拍在 1s 内完成，
     * 闪光灯点亮时长同步缩短。 */
    frame_broker_boost(10000);

    camera_fb_t *fb = NULL;
    if (dark_scene) {
        ESP_LOGI(TAG, "Dark scene — flash photo with frame-driven warm-up");
        /* 2026-09-09 修复（PIT-040）：get_copy 立即返回"当前帧"，旧写法丢弃
         * 3 次拿到的是同一帧的引用、抓到的也是闪光前的旧帧（实测 08:12 事
         * 件 468ms 内完成、成片 13.5KB 纯暗帧）。改为记录闪光点亮前的发布
         * 序号，等序号推进 3 帧（AEC/AWB 再收敛）后抓第 4 帧——帧驱动语义
         * 为真。boost 5fps 下闪光点亮 ~0.8s，2fps 兜底 ~2s，超时 3s。 */
        uint32_t gen0 = frame_broker_current_gen();
        flash_led_on();
        if (frame_broker_get_copy_after(gen0 + MOTION_FLASH_WARMUP_FRAMES, &fb, 3000)
                != ESP_OK || fb == NULL) {
            ESP_LOGW(TAG, "Failed to capture with flash (no fresh frame after warm-up)");
        }
        flash_led_off();
#if !CONFIG_MIBEE_CSI_MOTION
        /* Flash polluted the ΣΔ model's view: blank it (report stage ⑦). */
        lm_motion_flash_guard(15);
#endif
    }

    if (fb == NULL) {
        if (frame_broker_get_copy(&fb, 2000) != ESP_OK || fb == NULL) {
            ESP_LOGW(TAG, "Failed to capture frame for photo save");
            return;
        }
    }

    char filename[64];
    if (time_sync_is_synced()) {
        const char *timestamp = time_sync_get_str();
        snprintf(filename, sizeof(filename), "motion_%s.jpg", timestamp);
        for (char *p = filename; *p; p++) {
            if (*p == ' ' || *p == ':') *p = '_';
        }
    } else {
        snprintf(filename, sizeof(filename), "motion_unknown_%03u.jpg", (unsigned)s_unknown_seq++);
    }

    if (!s_running) { frame_broker_free(fb); return; }

    esp_err_t err = storage_save_photo(fb, filename);
    frame_broker_free(fb);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Motion photo saved: %s", filename);
    } else {
        ESP_LOGE(TAG, "Failed to save motion photo: %s", esp_err_to_name(err));
    }

    s_in_cooldown = true;
    s_cooldown_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Entering rate-limit (cooldown=%us, active=%us)",
             config_get()->motion_cooldown_s, config_get()->motion_active_interval_s);
}

#if CONFIG_MIBEE_CSI_MOTION
/**
 * @brief FreeRTOS task — CSI 触发模式：快照轮询 + 照片链
 *
 * 运动判定来自 csi_motion 的 ESPectre 快照（RF 域）。250ms 轮询一次
 * portMUX 拷贝，成本可忽略；MOTION 期间按家族超集模型再触发
 * （active_interval_s），活动停止后回退 cooldown_s。
 */
static void motion_detection_task(void *arg)
{
    ESP_LOGI(TAG, "Motion task started (CSI trigger, poll=250ms)");

    while (s_running) {
        if (!camera_is_initialized()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        flash_led_init();

        /* 周期暗探针（无人观看且未录制时；事件路径另有按需探针兜底） */
        if ((esp_timer_get_time() - s_last_probe_us) / 1000 >= PROBE_PERIOD_MS) {
            probe_tick();
        }

        csi_motion_status_t st;
        bool motion = csi_motion_get_status(&st) && strcmp(st.state, "MOTION") == 0;
        /* 活动状态跟踪（契约 §3.2 active 语义）：快照即活动 */
        s_motion_recent = motion;
        /* motion_enabled=0 → 感知照跑（诊断/状态上报），只是不落盘不联动 */
        if (motion && config_get()->motion_enabled && !is_in_cooldown()) {
            handle_motion_event(scene_dark_decision());
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGI(TAG, "Motion detection task exiting");
    s_motion_task_handle = NULL;
    vTaskDelete(NULL);
}
#else
/**
 * @brief FreeRTOS task — decode + ΣΔ pipeline analysis loop
 */
static void motion_detection_task(void *arg)
{
    ESP_LOGI(TAG, "Motion task started (ΣΔ pixel-domain pipeline)");

    int64_t last_analysis_us = 0;

    while (s_running) {
        if (!camera_is_initialized()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        flash_led_init();

        /* Probe cadence (independent of analysis) */
        if ((esp_timer_get_time() - s_last_probe_us) / 1000 >= PROBE_PERIOD_MS) {
            probe_tick();
        }

        /* Analysis cadence gate */
        int64_t now = esp_timer_get_time();
        if (now - last_analysis_us < (int64_t)ANALYSIS_INTERVAL_MS * 1000) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        last_analysis_us = now;

        camera_fb_t *fb = NULL;
        if (frame_broker_get_copy(&fb, 2000) != ESP_OK || fb == NULL) {
            ESP_LOGW(TAG, "No frame for analysis");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        int gw = 0, gh = 0;
        int64_t t0 = esp_timer_get_time();
        size_t fb_len = fb->len;
        int gw_ret = lm_jpeg_decode_gray(fb, s_grid, &gw, &gh);
        frame_broker_free(fb);

        if (gw_ret <= 0 || gw > LM_GRID_MAX_W || gh > LM_GRID_MAX_H) {
            ESP_LOGW(TAG, "JPEG decode failed or grid too big (len=%u, %dx%d)",
                     (unsigned)fb_len, gw, gh);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        s_decode_us = (int32_t)(esp_timer_get_time() - t0);

        if (!analysis_alloc(gw, gh)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        s_frames_analyzed++;
        lm_result_t r;
        bool trig = s_lm_inited ? lm_motion_process(s_grid, &r) : false;
        s_diag = r;

        /* Fallback darkness between probes: auto-exposure luma mean is a
         * weak indicator (AGC biases it toward mid-grey) but better than
         * the old JPEG-size heuristic. Probe results overwrite it. */
        if (lm_dark_probe_age_ms() < 0) {
            s_brightness_pct = (uint8_t)(r.luma_mean * 100 / 255);
            s_bright_method = 1;
            s_scene_dark = s_brightness_pct < config_get()->flash_threshold;
        }

        ESP_LOGD(TAG, "σ=%d.%02d E=%d A=%d fg=%d%% blobs=%d mode=%d decode=%dms",
                 r.sigma_x100 / 100, r.sigma_x100 % 100, r.energy, r.energy_smooth,
                 r.fg_filt_pct, r.blobs, r.mode, s_decode_us / 1000);

        if (trig) {
            ESP_LOGI(TAG, "TRIGGER ctx: E=%d A=%d blobs=%d fg=%d%% σ=%d.%02d marks=%d dec=%dms",
                     r.energy, r.energy_smooth, r.blobs, r.fg_filt_pct,
                     r.sigma_x100 / 100, r.sigma_x100 % 100, r.marks, s_decode_us / 1000);
        }
        /* 活动状态跟踪（契约 §3.2 active 语义）：本周期有触发即视为持续活动 */
        s_motion_recent = trig;
        /* motion_enabled=0 → 检测照跑（诊断/状态上报），只是不落盘不联动 */
        if (trig && config_get()->motion_enabled && !is_in_cooldown()) {
            handle_motion_event(s_scene_dark);
        }
    }

    ESP_LOGI(TAG, "Motion detection task exiting");
    s_motion_task_handle = NULL;
    vTaskDelete(NULL);
}
#endif /* CONFIG_MIBEE_CSI_MOTION */

/* ---- Public API ---- */

esp_err_t motion_detect_init(void)
{
    s_running = false;
    s_in_cooldown = false;
    s_cooldown_start_us = 0;
    s_motion_task_handle = NULL;
    memset(&s_diag, 0, sizeof(s_diag));
    return ESP_OK;
}

esp_err_t motion_detect_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Motion detection already running");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_grid == NULL) {
        s_grid = heap_caps_malloc(LM_GRID_MAX_W * LM_GRID_MAX_H,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_grid == NULL) {
            s_grid = heap_caps_malloc(LM_GRID_MAX_W * LM_GRID_MAX_H,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (s_grid == NULL) {
            ESP_LOGE(TAG, "Failed to allocate analysis grid (30KB)");
            s_running = false;
            return ESP_FAIL;
        }
    }
    s_running = true;
    s_in_cooldown = false;

    s_motion_task_handle = xTaskCreateStaticPinnedToCore(
        motion_detection_task,
        "motion_detect",
        MOTION_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL,
        MOTION_TASK_PRIORITY,
        s_motion_task_stack,
        &s_motion_task_tcb,
        tskNO_AFFINITY
    );

    if (s_motion_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create motion detection task");
        s_running = false;
        return ESP_FAIL;
    }

#if CONFIG_MIBEE_CSI_MOTION
    ESP_LOGI(TAG, "Motion detection started (CSI trigger, cooldown=%us, active=%us; "
             "sensitivity 参数不适用于 CSI 判决)",
             config_get()->motion_cooldown_s, config_get()->motion_active_interval_s);
#else
    ESP_LOGI(TAG, "Motion detection started (ΣΔ pipeline, sensitivity=%u, cooldown=%us, active=%us)",
             config_get()->motion_sensitivity, config_get()->motion_cooldown_s,
             config_get()->motion_active_interval_s);
#endif
    return ESP_OK;
}

void motion_detect_apply_config(void)
{
#if CONFIG_MIBEE_CSI_MOTION
    /* CSI 判决阈值在 ESPectre Kconfig，不在运行时配置——无管线可重建。
     * cooldown/active_interval 每次判定即时读取，无需脏标记。 */
#else
    /* 灵敏度/阈值参数变更 → 下一分析周期重建管线（几何不变也重建） */
    s_cfg_dirty = true;
#endif
}

esp_err_t motion_detect_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping motion detection...");
    s_running = false;

    TickType_t timeout = pdMS_TO_TICKS(STOP_WAIT_MS);
    while (s_motion_task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout -= pdMS_TO_TICKS(100);
    }

    if (s_motion_task_handle != NULL) {
        ESP_LOGW(TAG, "Motion task did not exit in time, force deleting");
        vTaskDelete(s_motion_task_handle);
        s_motion_task_handle = NULL;
    }

    /* 网格留给下次 start 复用（PSRAM 充裕，避免反复分配） */
    s_in_cooldown = false;
    return ESP_OK;
}

bool motion_detect_is_running(void)
{
    return s_running;
}

/* ---- Brightness public API ---- */

uint8_t motion_detect_get_brightness_pct(void)
{
    return s_brightness_pct;
}

uint8_t motion_detect_get_brightness_method(void)
{
    /* 2 = locked-exposure grayscale probe; 1 = auto-exposure luma fallback */
    return s_bright_method;
}

bool motion_detect_is_scene_dark(void)
{
    return s_scene_dark;
}

/* ---- Diagnostics for /api/status ---- */

const lm_result_t *motion_detect_get_diag(void)
{
    return &s_diag;
}

uint32_t motion_detect_get_frames_analyzed(void)
{
    return s_frames_analyzed;
}

int32_t motion_detect_get_decode_us(void)
{
    return s_decode_us;
}
