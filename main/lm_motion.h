/*
 * lm_motion.h — Sigma-Delta dark-scene motion pipeline (7 stages)
 *
 * Design per external team report (2026-09-03) implementing
 * Manzanera & Richefeu 2007 ΣΔ background estimation with the
 * ICIP09 conditional-update variant plus mandatory global-change
 * forced reconvergence:
 *
 *   ① ΣΔ background model  M±A random walk, V tracks median(N·|I−M|)
 *   ② Robust noise est.    σ̂ = 1.4826×MAD(signed D) + median offset
 *   ③ Global-change detect + 20-frame forced reconvergence (A=8)
 *   ④ 3×3 majority filter (≥5/9)
 *   ⑤ Connected components + shape gate (area/bbox/aspect/fill)
 *   ⑥ Energy hysteresis A += (E−A)/4 + N-of-M temporal confirmation
 *   ⑦ Flash blind window (no model update, no detection)
 *
 * Pure integer math, no libc heap use: caller supplies the working
 * buffer (internal DRAM on target) via lm_motion_init_buf().
 */
#ifndef LM_MOTION_H
#define LM_MOTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int w, h;
    /* --- stage ① ΣΔ --- */
    int logn;              /* N = 1<<logn (multiplier for V target); 2 → N=4, thr ≈ 2.7σ */
    int vmin, vmax;        /* V clamp; V* ≈ 0.6745·N·σ at steady state */
    /* --- stage ③ global change --- */
    int gchg_fg_pct;       /* fg occupancy pct that counts toward "global change" (25) */
    int gchg_frames;       /* consecutive frames above gchg_fg_pct to declare (3) */
    int med_off_num;       /* |median(D)| > σ̂/med_off_num declares global shift (2) */
    int reconverge_frames; /* forced reconvergence length (20) */
    int reconverge_step;   /* ΣΔ step A during reconvergence (8) */
    /* --- stage ⑤ blob gate --- */
    int min_area;          /* ≥15 px */
    int min_side, max_side;/* blob bbox side limits (2..70) */
    int max_aspect_x10;    /* long/short ≤ 4.0 (stored ×10) */
    int min_fill_pct;      /* area/(bw·bh) ≥ 20% */
    /* --- stage ⑥ temporal --- */
    int e_lo;              /* energy mark threshold (12) */
    int e_hi;              /* trigger threshold (35) */
    int hist_len;          /* M in N-of-M (8) */
    int m_of_n;            /* N in N-of-M (3) */
    /* --- stage ⑦ flash --- */
    int blind_frames;      /* frames to blank after a trigger/flash (15) */
} lm_config_t;

typedef struct {
    int frame_no;
    int mode;              /* 0 normal, 1 warmup, 2 reconverge, 3 blind */
    int sigma_x100;        /* σ̂ estimate ×100 */
    int median_off;        /* signed median(D) — nonzero ⇒ global luma shift */
    int fg_raw_pct;        /* fg pixels before filtering, % of grid */
    int fg_filt_pct;       /* fg pixels after majority filter, % */
    int energy;            /* E: px in shape-valid blobs this frame */
    int energy_smooth;     /* hysteresis accumulator A */
    int marks;             /* marks in history window */
    int blobs;             /* shape-valid blob count */
    int luma_mean;         /* mean of input grid (0..255) */
    bool converged;        /* model past warmup and not reconverging */
} lm_result_t;

/* Working-buffer size in bytes for a w×h grid (all stages, single block). */
size_t lm_motion_buf_size(int w, int h);

/* Fill a config with report-tuned defaults. */
void lm_motion_defaults(lm_config_t *c);

/*
 * Initialize on a caller-provided buffer (must be ≥ lm_motion_buf_size()).
 * mem need not be aligned (internally handled). Returns 0 on success.
 */
int lm_motion_init_buf(int w, int h, const lm_config_t *c, void *mem, size_t mem_size);

/* Release (buffer stays owned by caller). */
void lm_motion_deinit(void);

/* Feed one grayscale frame (w*h bytes). Returns true when a motion event
 * is confirmed (stage ⑥). Result is also available via lm_motion_result(). */
bool lm_motion_process(const uint8_t *img, lm_result_t *r_out);

/* Blank detection+model-update for `frames` frames (flash/trigger guard). */
void lm_motion_flash_guard(int frames);

/* Re-seed model from the next frame (e.g. after resolution change). */
void lm_motion_reset(void);

const lm_result_t *lm_motion_result(void);
const lm_config_t *lm_motion_config(void);

#ifdef __cplusplus
}
#endif
#endif /* LM_MOTION_H */
