/*
 * lm_motion.c — Sigma-Delta dark-scene motion pipeline (7 stages)
 *
 * See lm_motion.h for the stage map and provenance (external team report
 * 2026-09-03; Manzanera & Richefeu PRL 2007; Lacassagne/Manzanera ICIP 2009
 * Alg. 2 conditional update + forced reconvergence). Pure integer math.
 */
#include <string.h>
#include "lm_motion.h"

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

static lm_config_t s_cfg;
static int s_w = 0, s_h = 0, s_n = 0;
static bool s_inited = false;

/* Layout inside the caller buffer (byte addressed, 4-byte slack at head
 * to guarantee uint32 alignment of everything after it). */
static uint8_t  *s_m;        /* ΣΔ mean model            n   */
static uint8_t  *s_v;        /* ΣΔ deviation model (thr) n   */
static uint8_t  *s_fg;       /* raw fg mask              n   */
static uint8_t  *s_filt;     /* majority-filtered mask   n   */
static uint8_t  *s_visit;    /* CCL visited              n   */
static int32_t  *s_stack;    /* CCL flood stack          n   */

static uint16_t s_hist_d[256];    /* histogram of signed D (+128)  */
static uint16_t s_hist_abs[256];  /* histogram of |D − med|        */

static lm_result_t s_res;

static int s_warmup_left;
static int s_reconv_left;
static int s_blind_left;
static int s_gchg_run;
static bool s_seeded;

static int s_energy_smooth;
static uint8_t s_marks;          /* bit ring, hist_len ≤ 8 */
static int s_mark_pos;

static int count_marks(const lm_config_t *c)
{
    int mk = 0;
    for (int b = 0; b < c->hist_len; b++) mk += (s_marks >> b) & 1;
    return mk;
}

#define LM_MODE_NORMAL 0
#define LM_MODE_WARMUP 1
#define LM_MODE_RECONV 2
#define LM_MODE_BLIND  3

#define LM_WARMUP_FRAMES 10

/* Detection quiet-gate (report §2 doctrine): above σ̂≈13 the analysis grid is
 * in the "no usable signal" regime (pitch black / AGC maxed) — clustered
 * noise starts passing the shape gate (sim: blobs of E>100 at σ=12). Favor
 * missed detections over false triggers there; hardware PIR is the answer
 * in that regime, per the report's own hardware section. */
#define LM_QUIET_SIGMA_X100 1300

/* ------------------------------------------------------------------ */
/*  Init / teardown                                                    */
/* ------------------------------------------------------------------ */

size_t lm_motion_buf_size(int w, int h)
{
    size_t n = (size_t)w * (size_t)h;
    /* 4 model/work masks + CCL stack (int32) + 4B alignment slack */
    return n * 8 + 4;
}

void lm_motion_defaults(lm_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->logn = 2;              /* N = 4 → threshold ≈ 2.7σ */
    /* 4 absorbs pure JPEG-DC quantization flicker (±2DN patchy drift seen
     * on-device in the near-zero-noise DC domain); sim-validated rows are
     * unchanged since V* ≈ 2.7σ dominates whenever σ ≥ 1. */
    c->vmin = 4;
    c->vmax = 80;
    c->gchg_fg_pct = 25;
    c->gchg_frames = 3;
    c->med_off_num = 2;       /* |median(D)| > σ̂/2 */
    c->reconverge_frames = 20;
    c->reconverge_step = 8;
    c->min_area = 15;
    c->min_side = 2;
    c->max_side = 70;
    c->max_aspect_x10 = 40;
    c->min_fill_pct = 20;
    c->e_lo = 10;          /* tuned: 5×7 target → E≈22-31, A→25; noise E≈0 */
    c->e_hi = 18;
    c->hist_len = 8;
    c->m_of_n = 3;
    c->blind_frames = 15;
}

int lm_motion_init_buf(int w, int h, const lm_config_t *c, void *mem, size_t mem_size)
{
    if (!c || !mem || w <= 0 || h <= 0) return -1;
    if (mem_size < lm_motion_buf_size(w, h)) return -2;
    if (c->hist_len < 1 || c->hist_len > 8) return -3;

    s_cfg = *c;
    s_w = w; s_h = h;
    s_n = w * h;

    uintptr_t base = ((uintptr_t)mem + 3u) & ~(uintptr_t)3u;
    uint8_t *p = (uint8_t *)base;
    s_m     = p;          p += s_n;
    s_v     = p;          p += s_n;
    s_fg    = p;          p += s_n;
    s_filt  = p;          p += s_n;
    s_visit = p;          p += s_n;
    s_stack = (int32_t *)p;

    s_inited = true;
    lm_motion_reset();
    return 0;
}

void lm_motion_reset(void)
{
    if (!s_inited) return;
    memset(s_m, 0, (size_t)(unsigned)s_n * 5);          /* m,v,fg,filt,visit */
    memset(&s_res, 0, sizeof(s_res));
    s_warmup_left = LM_WARMUP_FRAMES;
    s_reconv_left = 0;
    s_blind_left = 0;
    s_gchg_run = 0;
    s_seeded = false;
    s_energy_smooth = 0;
    s_marks = 0;
    s_mark_pos = 0;
    s_res.mode = LM_MODE_WARMUP;
}

void lm_motion_deinit(void)
{
    s_inited = false;
    s_w = s_h = s_n = 0;
}

void lm_motion_flash_guard(int frames)
{
    if (frames > s_blind_left) s_blind_left = frames;
}

const lm_result_t *lm_motion_result(void) { return &s_res; }
const lm_config_t *lm_motion_config(void) { return &s_cfg; }

/* ------------------------------------------------------------------ */
/*  Stage ② helpers — median / MAD over histograms                     */
/* ------------------------------------------------------------------ */

/* Median of a 256-bin histogram of `total` samples. */
static int hist_median(const uint16_t *hist, int total)
{
    int half = total >> 1, acc = 0;
    for (int i = 0; i < 256; i++) {
        acc += hist[i];
        if (acc > half) return i;
    }
    return 255;
}

/* ------------------------------------------------------------------ */
/*  Stage ⑤ — connected components with shape gate                     */
/* ------------------------------------------------------------------ */

/* Flood fill from `start` over s_filt (fg==1). Returns blob pixel count,
 * fills bbox. Uses s_stack/s_visit. */
static int flood_fill(int start, int *minx, int *maxx, int *miny, int *maxy)
{
    int count = 0;
    int sp = 0;
    s_stack[sp++] = start;
    s_visit[start] = 1;
    *minx = s_w; *maxx = 0; *miny = s_h; *maxy = 0;
    while (sp > 0) {
        int idx = s_stack[--sp];
        int x = idx % s_w, y = idx / s_w;
        count++;
        if (x < *minx) *minx = x;
        if (x > *maxx) *maxx = x;
        if (y < *miny) *miny = y;
        if (y > *maxy) *maxy = y;
        if (x > 0        && s_filt[idx - 1]  && !s_visit[idx - 1])  { s_visit[idx - 1] = 1;  s_stack[sp++] = idx - 1; }
        if (x < s_w - 1  && s_filt[idx + 1]  && !s_visit[idx + 1])  { s_visit[idx + 1] = 1;  s_stack[sp++] = idx + 1; }
        if (y > 0        && s_filt[idx - s_w] && !s_visit[idx - s_w]) { s_visit[idx - s_w] = 1; s_stack[sp++] = idx - s_w; }
        if (y < s_h - 1  && s_filt[idx + s_w] && !s_visit[idx + s_w]) { s_visit[idx + s_w] = 1; s_stack[sp++] = idx + s_w; }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/*  Main pipeline                                                      */
/* ------------------------------------------------------------------ */

bool lm_motion_process(const uint8_t *img, lm_result_t *r_out)
{
    if (!s_inited || !img) return false;

    const lm_config_t *c = &s_cfg;
    const int n = s_n;
    s_res.frame_no++;
    s_res.blobs = 0;
    s_res.energy = 0;

    /* Luma mean — free diagnostics + darkness fallback */
    {
        uint32_t acc = 0;
        for (int i = 0; i < n; i++) acc += img[i];
        s_res.luma_mean = (int)(acc / (uint32_t)n);
    }

    /* ---- seed / blind bookkeeping ---- */
    const size_t nz = (size_t)(unsigned)s_n;
    if (!s_seeded) {
        memcpy(s_m, img, nz);
        memset(s_v, c->vmin, nz);
        s_seeded = true;
    }

    if (s_blind_left > 0) {
        s_blind_left--;
        s_res.mode = LM_MODE_BLIND;
        /* Flash pollutes every statistic: no model update, no detection.
         * Keep energy decaying so post-flash return is quick. */
        s_energy_smooth -= s_energy_smooth / 4;
        s_res.energy_smooth = s_energy_smooth;
        s_res.marks = count_marks(c);
        if (r_out) *r_out = s_res;
        return false;
    }

    /* ---- stage ① ΣΔ update + raw fg ----
     * Unconditional M/V updates (PRL07 Alg.1). The ICIP09 conditional
     * variant (freeze M/V on fg pixels) was traced on the host sim and
     * locks into per-pixel oscillation on heavy noise: fg pixels freeze
     * far from I, V climbs, they drop out, M re-chases — σ̂ collapses to
     * ~0 and thresholds stop tracking noise. Moving objects are instead
     * handled by their transit time (blob moves ≫1px/frame at analysis
     * fps; M re-absorbs at ±1/frame only after they stop) and global
     * shifts by stage ③ forced reconvergence. */
    const bool reconv = (s_reconv_left > 0);
    const int step = reconv ? c->reconverge_step : 1;
    int fg_count = 0;

    for (int i = 0; i < n; i++) {
        const int I = img[i];
        int M = s_m[i];
        if (I > M)      { M += step; if (M > 255) M = 255; }
        else if (I < M) { M -= step; if (M < 0)   M = 0;   }
        s_m[i] = (uint8_t)M;
        int d = I - M;                       /* signed residual */
        if (d < 0) d = -d;
        int target = d << c->logn;           /* N·O, N = 2^logn */
        int V = s_v[i];
        if (V < target)      { V++; if (V > c->vmax) V = c->vmax; }
        else if (V > target) { V--; if (V < c->vmin) V = c->vmin; }
        s_v[i] = (uint8_t)V;
        if (d >= V) { s_fg[i] = 1; fg_count++; }
        else          s_fg[i] = 0;
    }

    s_res.fg_raw_pct = fg_count * 100 / n;
    s_res.energy_smooth = s_energy_smooth;
    s_res.marks = count_marks(c);

    /* ---- stage ② robust σ̂ + median offset (signed-D histogram) ---- */
    memset(s_hist_d, 0, sizeof(s_hist_d));
    for (int i = 0; i < n; i++) {
        int d = img[i] - s_m[i];             /* signed */
        if (d < -128) d = -128;
        if (d > 127)  d = 127;
        s_hist_d[d + 128]++;
    }
    int med = hist_median(s_hist_d, n) - 128;   /* signed median */
    s_res.median_off = med;

    memset(s_hist_abs, 0, sizeof(s_hist_abs));
    for (int i = 0; i < n; i++) {
        int a = (int)img[i] - (int)s_m[i] - med;
        if (a < 0) a = -a;
        if (a > 255) a = 255;
        s_hist_abs[a]++;
    }
    int mad = hist_median(s_hist_abs, n);
    s_res.sigma_x100 = mad * 14826 / 100;   /* σ̂×100 = 1.4826×MAD×100 */

    /* ---- stage ③ global-change detection ---- */
    if (reconv) {
        s_reconv_left--;
        s_gchg_run = 0;
        s_res.mode = LM_MODE_RECONV;
        /* Detection blank while reconverging; history cleared so stale
         * marks cannot fire immediately after a global shift. */
        s_marks = 0;
        s_energy_smooth = 0;
        s_res.energy_smooth = 0;
        s_res.marks = 0;
        s_res.converged = false;
        if (r_out) *r_out = s_res;
        return false;
    }
    if (s_warmup_left > 0) {
        s_warmup_left--;
        s_res.mode = LM_MODE_WARMUP;
        if (r_out) *r_out = s_res;
        return false;
    }

    {
        bool gchg = false;
        if (s_res.fg_raw_pct > c->gchg_fg_pct) {
            if (++s_gchg_run >= c->gchg_frames) gchg = true;
        } else {
            s_gchg_run = 0;
        }
        /* |median(D)| > σ̂/med_off_num ⇒ whole-frame luma shift (AEC/AGC step).
         * σ̂ is floored at 3.0 for this decision: the reconvergence step (8)
         * pins M onto I, collapsing σ̂ toward 0 — an unfloored comparison
         * would then fire on ±1 median noise and lock the pipeline into
         * permanent reconvergence (death spiral, traced in the host sim). */
        int amed = med < 0 ? -med : med;
        int sig_floor = s_res.sigma_x100 < 300 ? 300 : s_res.sigma_x100;
        if (amed >= 2 && amed * c->med_off_num * 100 > sig_floor) {
            gchg = true;
        }
        if (gchg) {
            s_reconv_left = c->reconverge_frames;
            s_gchg_run = 0;
            s_res.mode = LM_MODE_RECONV;
            s_marks = 0;
            s_energy_smooth = 0;
            s_res.energy_smooth = 0;
            s_res.marks = 0;
            if (r_out) *r_out = s_res;
            return false;
        }
    }

    /* ---- stage ④ 3×3 majority filter (≥5/9) ---- */
    memset(s_filt, 0, nz);
    {
        uint8_t *fg = s_fg, *f = s_filt;
        for (int y = 0; y < s_h; y++) {
            const int y0 = y > 0 ? y - 1 : 0, y1 = y < s_h - 1 ? y + 1 : s_h - 1;
            for (int x = 0; x < s_w; x++) {
                const int x0 = x > 0 ? x - 1 : 0, x1 = x < s_w - 1 ? x + 1 : s_w - 1;
                int cnt = 0;
                for (int yy = y0; yy <= y1; yy++)
                    for (int xx = x0; xx <= x1; xx++)
                        cnt += fg[yy * s_w + xx];
                if (cnt >= 5) f[y * s_w + x] = 1;
            }
        }
        int filt_count = 0;
        for (int i = 0; i < n; i++) filt_count += f[i];
        s_res.fg_filt_pct = filt_count * 100 / n;
    }

    /* ---- stage ⑤ CCL + shape gate ---- */
    memset(s_visit, 0, nz);
    for (int i = 0; i < n; i++) {
        if (!s_filt[i] || s_visit[i]) continue;
        int minx, maxx, miny, maxy;
        int area = flood_fill(i, &minx, &maxx, &miny, &maxy);
        int bw = maxx - minx + 1, bh = maxy - miny + 1;
        if (area < c->min_area) continue;
        if (bw < c->min_side || bh < c->min_side) continue;
        if (bw > c->max_side || bh > c->max_side) continue;
        int short_side = bw < bh ? bw : bh;
        int long_side  = bw < bh ? bh : bw;
        if (long_side * 10 > short_side * c->max_aspect_x10) continue;
        if (area * 100 < bw * bh * c->min_fill_pct) continue;
        s_res.blobs++;
        s_res.energy += area;
    }

    /* ---- stage ⑥ energy hysteresis + N-of-M ---- */
    s_energy_smooth += (s_res.energy - s_energy_smooth) / 4;
    s_res.energy_smooth = s_energy_smooth;

    s_mark_pos = (s_mark_pos + 1) % c->hist_len;
    s_marks &= (uint8_t)~(1u << s_mark_pos);
    if (s_energy_smooth > c->e_lo) s_marks |= (uint8_t)(1u << s_mark_pos);
    s_res.marks = count_marks(c);
    s_res.mode = LM_MODE_NORMAL;
    s_res.converged = true;

    bool trigger = (s_energy_smooth > c->e_hi) && (s_res.marks >= c->m_of_n)
                && (s_res.sigma_x100 <= LM_QUIET_SIGMA_X100);
    if (trigger) {
        /* One shot per event: blank history so sustained motion re-triggers
         * only via a fresh N-of-M run. External blind window guards flash. */
        s_marks = 0;
        lm_motion_flash_guard(c->blind_frames);
        if (r_out) *r_out = s_res;
        return true;
    }

    if (r_out) *r_out = s_res;
    return false;
}
