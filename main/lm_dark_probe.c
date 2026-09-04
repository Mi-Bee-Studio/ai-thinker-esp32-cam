/*
 * lm_dark_probe.c — locked-exposure darkness probe (see lm_dark_probe.h)
 */
#include "lm_dark_probe.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "config_manager.h"

static const char *TAG = "lm_dark_probe";

static int s_last_pct = -1;
static int64_t s_last_time_us = 0;

int lm_dark_probe_run(int (*sample_luma_cb)(void))
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s || !sample_luma_cb) return -1;

    /* Snapshot current AE state so restore is exact */
    const int aec_was = s->status.aec;
    const int agc_was = s->status.agc;
    const int aec_value_was = s->status.aec_value;
    const int agc_gain_was = s->status.agc_gain;

    /* Lock: auto-exposure off + fixed exposure; auto-gain off + gain 1x */
    s->set_exposure_ctrl(s, 0);
    s->set_gain_ctrl(s, 0);
    s->set_aec_value(s, LM_PROBE_AEC_VALUE);
    s->set_agc_gain(s, 0);

    /* Let a few frames elapse at the locked exposure (broker cadence is
     * ~2fps idle), then sample twice and keep the second — the first may
     * still be mid-transition. */
    vTaskDelay(pdMS_TO_TICKS(550));
    (void)sample_luma_cb();
    vTaskDelay(pdMS_TO_TICKS(120));
    int luma = sample_luma_cb();

    /* Restore exactly what was there before */
    s->set_aec_value(s, aec_value_was);
    s->set_agc_gain(s, agc_gain_was);
    s->set_gain_ctrl(s, agc_was);
    s->set_exposure_ctrl(s, aec_was);

    if (luma < 0) {
        ESP_LOGW(TAG, "probe: no frame sampled (luma=%d)", luma);
        return -1;
    }

    s_last_pct = luma * 100 / 255;
    s_last_time_us = esp_timer_get_time();
    ESP_LOGI(TAG, "probe: locked-exposure luma=%d → %d%% dark",
             luma, s_last_pct);
    return s_last_pct;
}

bool lm_dark_probe_is_dark(int pct)
{
    return pct >= 0 && pct < config_get()->flash_threshold;
}

int lm_dark_probe_last_pct(void) { return s_last_pct; }

int64_t lm_dark_probe_age_ms(void)
{
    if (s_last_time_us == 0) return -1;
    return (esp_timer_get_time() - s_last_time_us) / 1000;
}
