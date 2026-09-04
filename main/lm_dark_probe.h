/*
 * lm_dark_probe.h — locked-exposure darkness probe
 *
 * Report §4.2, with one integration upgrade: instead of switching the
 * sensor to GRAYSCALE+QQVGA (which needs esp_camera_deinit/init — 100-200ms
 * of dead air, stream break, esp32-camera issue #514), we lock AEC/AGC in
 * the normal JPEG path and decode the luma mean from the frame we already
 * decode for motion analysis. Same physics — fixed exposure+gain ⇒ mean
 * luma ∝ illuminance — none of the switching cost.
 *
 * Cadence: every 30s, only when nobody is watching (no MJPEG clients) and
 * the recorder is idle: the lock produces a couple of oddly-exposed frames
 * which would be visible to viewers / recorded.
 */
#ifndef LM_DARK_PROBE_H
#define LM_DARK_PROBE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed exposure used while locked (report default: aec_value 300). */
#define LM_PROBE_AEC_VALUE   300
/* Period between probes, ms. */
#define LM_PROBE_PERIOD_MS   30000

/*
 * Run one probe cycle (lock → settle → sample → restore). Call from the
 * motion task. `sample_luma_cb` receives broker frames and returns the
 * decoded grid luma mean (0..255), or -1 on failure — the callback is
 * motion_detect's decode path.
 * Returns brightness percent 0..100, or -1 if the probe could not run
 * this cycle (busy / no frames).
 */
int lm_dark_probe_run(int (*sample_luma_cb)(void));

/* Whether the external scene should be considered dark at `pct`. */
bool lm_dark_probe_is_dark(int pct);

/* Last successful probe result (0..100) and its age in ms; -1 if never. */
int  lm_dark_probe_last_pct(void);
int64_t lm_dark_probe_age_ms(void);

#ifdef __cplusplus
}
#endif
#endif /* LM_DARK_PROBE_H */
