/*
 * MiBee Cam — ESPectre WiFi CSI motion sensing (optional pilot module)
 *
 * Copyright (C) 2026 MiBee Cam Authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ESPectre SDK part is GPL-3.0-only (components/espectre/LICENSE), so the
 * combined firmware is distributed under GPLv3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "csi_motion.h"

#if CONFIG_MIBEE_CSI_MOTION

#include <cstdarg>
#include <cstdio>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "espectre_sdk.h"

static const char *TAG = "csi_motion";

/* Route SDK-internal ESPECTRE_LOGx into the firmware log. W/E only — the
 * runtime's own INFO heartbeat would duplicate the listener heartbeat. */
static bool espectre_log_enabled(void *ctx, espectre::LogLevel level, const char *tag)
{
    (void)ctx; (void)tag;
    return level <= espectre::LogLevel::WARNING;
}

static void espectre_log_write(void *ctx, espectre::LogLevel level, const char *tag,
                               int line, const char *format, va_list args)
{
    (void)ctx;
    char buf[192];
    vsnprintf(buf, sizeof(buf), format, args);
    ESP_LOGW(TAG, "[espectre %s:%d] %s", tag ? tag : "?", line, buf);
}

namespace {

espectre::RuntimeFrontendController s_controller;

/* 契约 v1.6：最新快照。写者 = ESPectre pump 任务（on_motion_state_changed
 * 与 on_periodic_update 同线程，单写者成立）；读者 = httpd 任务
 * （/api/status 的 csi 字段）与 motion 任务（CSI 触发模式 250ms 轮询），
 * portMUX 拷贝，临界区仅 3 字段。状态转移回调里也落快照：只靠 ~1Hz 的
 * 周期更新会让 <1s 的 MOTION 片段漏采（MOTION_ON_HITS=4 @250ms 判定的
 * 最短驻留恰与其同量级）。 */
static portMUX_TYPE s_snap_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_snap_valid = false;
static csi_motion_status_t s_snap;

static void snapshot_update(const espectre::RuntimeSnapshot &s)
{
    const char *st = s.ready_to_publish
                         ? (s.motion_state == espectre::MotionState::MOTION ? "MOTION" : "IDLE")
                         : "warming";
    portENTER_CRITICAL(&s_snap_mux);
    s_snap_valid = true;
    strlcpy(s_snap.state, st, sizeof(s_snap.state));
    s_snap.score = s.movement_metric;
    s_snap.thr = s.threshold;
    portEXIT_CRITICAL(&s_snap_mux);
}

/* Listener: keep callbacks bounded and non-blocking (SDK threading
 * contract) — the photo chain lives in motion_detect's own task and picks
 * the verdict up via the snapshot poll. */
class CamCsiListener : public espectre::IRuntimeListener {
public:
    void on_motion_state_changed(const espectre::RuntimeSnapshot &s) override {
        if (!s.ready_to_publish) return;
        ESP_LOGI(TAG, "motion=%s score=%.2f thr=%.2f rssi=%d ch=%u",
                 s.motion_state == espectre::MotionState::MOTION ? "MOTION" : "IDLE",
                 s.movement_metric, s.threshold,
                 (int)s.link_rssi_dbm, (unsigned)s.link_channel);
        snapshot_update(s);
    }

    void on_calibration_started(const espectre::RuntimeSnapshot &s) override {
        ESP_LOGI(TAG, "calibration started (target=%u pkts)",
                 (unsigned)s.calibration_target_packets);
    }

    void on_calibration_finished(const espectre::RuntimeSnapshot &s, bool success) override {
        ESP_LOGI(TAG, "calibration %s (thr=%.2f)",
                 success ? "OK" : "FAILED", s.threshold);
    }

    void on_periodic_update(const espectre::RuntimeSnapshot &s,
                            uint32_t packets_received) override {
        snapshot_update(s);
        const espectre::RuntimeDiagnosticsSample *d = s_controller.diagnostics_sample();
        if (d != nullptr) {
            ESP_LOGI(TAG,
                     "status: state=%s score=%.2f pkts=%u cal=%u/%u prof=%d | "
                     "diag tx=%.1f cb=%.1f cls=%.1f rej=%.1f acc=%.1f adm=%.1f filt=%.1f",
                     s.ready_to_publish
                         ? (s.motion_state == espectre::MotionState::MOTION ? "MOTION" : "IDLE")
                         : "warming",
                     s.movement_metric, (unsigned)packets_received,
                     (unsigned)s.calibration_packets,
                     (unsigned)s.calibration_target_packets,
                     (int)s.csi_capture_profile,
                     d->traffic_tx_pps, d->csi_callback_pps, d->csi_classified_pps,
                     d->csi_provenance_rejected_pps, d->csi_accepted_pps,
                     d->csi_admitted_pps, d->csi_filtered_pps);
        } else {
            ESP_LOGI(TAG, "status: state=%s pkts=%u (no diag)",
                     s.ready_to_publish
                         ? (s.motion_state == espectre::MotionState::MOTION ? "MOTION" : "IDLE")
                         : "warming",
                     (unsigned)packets_received);
        }
    }

    void on_runtime_fault(const char *message) override {
        ESP_LOGW(TAG, "runtime fault: %s", message);
    }
};

CamCsiListener s_listener;

/* Single-owner pump task per SDK threading contract. Core 1 prio 1:
 * lowest user task there, below the streamers (prio 2) — sensing is
 * debounced over seconds, never latency-critical. */
void csi_motion_task(void *unused)
{
    (void)unused;
    espectre::LogSink sink;
    sink.enabled = espectre_log_enabled;
    sink.write = espectre_log_write;
    espectre::set_log_sink(sink);
    espectre::RuntimeConfig config = espectre::make_runtime_sensing_config_from_kconfig();
    config.device_id = espectre::derive_runtime_device_id();
    s_controller.set_config(config);
    if (!s_controller.setup(&s_listener)) {
        ESP_LOGE(TAG, "ESPectre setup failed — sensing disabled");
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "ESPectre sensing started (pps=%u mode=%d)",
             (unsigned)config.csi_target_pps, (int)config.csi_traffic_mode);
    while (true) {
        s_controller.loop();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

} /* namespace */

esp_err_t csi_motion_init(void)
{
    if (xTaskCreatePinnedToCore(csi_motion_task, "csi_motion", 6144,
                                nullptr, 1, nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "failed to create csi_motion task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* 契约 v1.6：/api/status "csi" 字段 + motion 任务 CSI 触发轮询的读侧 */
bool csi_motion_get_status(csi_motion_status_t *out)
{
    if (out == nullptr) return false;
    bool valid;
    portENTER_CRITICAL(&s_snap_mux);
    valid = s_snap_valid;
    if (valid) {
        *out = s_snap;
    }
    portEXIT_CRITICAL(&s_snap_mux);
    return valid;
}

#else /* !CONFIG_MIBEE_CSI_MOTION */

esp_err_t csi_motion_init(void)
{
    return ESP_OK;
}

/* CSI-off 生产形态 stub（PIT-038 补遗二）：web_server.c 无条件调用此函数
 * 取实时快照，stub 恒 false → /api/status 的 csi 字段缺省。此前 stub 漏写
 * 此函数，门关时链接必炸（PIT-039 记录）。 */
bool csi_motion_get_status(csi_motion_status_t *out)
{
    (void)out;
    return false;
}

#endif /* CONFIG_MIBEE_CSI_MOTION */
