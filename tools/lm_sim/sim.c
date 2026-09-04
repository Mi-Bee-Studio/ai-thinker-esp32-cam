/*
 * sim.c — host-side validation harness for lm_motion (desktop gcc).
 *
 * Reproduces the external report's scenario (§6.1):
 *   - 80×60 grid, low-frequency textured background + gradient
 *   - per-pixel Gaussian noise σ, 0.3% hot pixels ±70DN
 *   - AGC pump: +22DN global step for 3 frames every 45 frames
 *   - target: 5×7 ellipse, contrast C, frames 60..110, 1.5 px/frame diagonal
 *   - 5 fps, 200 frames per trial
 * Metrics: TPR over positive trials / false triggers per hour on negatives.
 *
 * Build:  gcc -O2 -Wall -Wextra -I<repo>/main -o sim sim.c <repo>/main/lm_motion.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lm_motion.h"

#define W 80
#define H 60
#define N (W * H)
#define FRAMES 200
#define TRIALS_POS 8
#define TRIALS_NEG 8
#define FPS 5

static unsigned long rng_state = 12345;
static unsigned rnd(void) { rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rng_state >> 33); }

static double gauss(void)
{
    /* Box-Muller */
    double u1 = (rnd() + 1.0) / 4294967297.0;
    double u2 = (rnd() + 1.0) / 4294967297.0;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static uint8_t bg[W][H];
static uint8_t frame[N];

static void bg_init(void)
{
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            bg[x][y] = (uint8_t)(50 + 25 * sin(x / 13.0) + 15 * sin(y / 17.0) + (x * 30) / W);
}

static void frame_build(int f, double sigma, int contrast, bool with_target,
                        int agc_step, uint8_t *hotmap)
{
    double tx = -1, ty = -1;
    if (with_target && f >= 60 && f <= 110) {
        tx = 10 + (f - 60) * 1.5;
        ty = 8 + (f - 60) * 1.2;
    }
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            double v = bg[x][y] + gauss() * sigma + agc_step;
            if (hotmap[y * W + x]) v += (hotmap[y * W + x] == 1) ? 70 : -70;
            if (tx >= 0) {
                double dx = (x - tx) / 2.5, dy = (y - ty) / 3.5;
                if (dx * dx + dy * dy <= 1.0) v += contrast;
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            frame[y * W + x] = (uint8_t)v;
        }
    }
}

typedef struct { int tpr; double far_per_hour; } outcome_t;

static outcome_t run_series(double sigma, int contrast, bool with_target, int trials, int logn_override)
{
    outcome_t o = { 0, 0.0 };
    int detections = 0, false_triggers = 0;

    for (int t = 0; t < trials; t++) {
        rng_state = 99991 + 7919 * t + (with_target ? 13 : 29) * (int)(sigma * 100) + contrast;
        uint8_t hotmap[N];
        memset(hotmap, 0, sizeof(hotmap));
        for (int i = 0; i < N * 3 / 1000; i++) {
            int idx = rnd() % N;
            hotmap[idx] = rnd() & 1 ? 1 : 2;
        }

        static uint8_t mem[N * 8 + 8];   /* ≥ lm_motion_buf_size(W,H) */
        lm_config_t cfg;
        lm_motion_defaults(&cfg);
        cfg.max_side = W > H ? W : H;   /* firmware scales blob gate with grid */
        if (logn_override >= 0) cfg.logn = logn_override;
        if (lm_motion_init_buf(W, H, &cfg, mem, sizeof(mem)) != 0) {
            fprintf(stderr, "init failed\n"); exit(1);
        }

        bool detected_in_window = false;
        int triggers_total = 0;
        for (int f = 0; f < FRAMES; f++) {
            int agc = ((f % 45) < 3) ? 22 : 0;
            frame_build(f, sigma, contrast, with_target, agc, hotmap);
            lm_result_t r;
            bool trig = lm_motion_process(frame, &r);
            if (trig) {
                triggers_total++;
                bool in_window = (f >= 55 && f <= 120);
                if (with_target && in_window) detected_in_window = true;
                if (!with_target || !in_window) {
                    if (!with_target) false_triggers++;          /* any trigger on a clean trial is false */
                }
            }
        }
        lm_motion_deinit();
        if (with_target && detected_in_window) detections++;
        (void)triggers_total;
    }

    o.tpr = detections * 100 / trials;
    /* FRAMES frames @FPS = FRAMES/FPS seconds of exposure per trial */
    o.far_per_hour = (double)false_triggers / trials * (3600.0 * FPS / FRAMES);
    return o;
}

int main(int argc, char **argv)
{
    int logn_override = argc > 1 ? atoi(argv[1]) : -1;
    bg_init();
    printf("%-6s %-9s | %-16s | %-16s\n", "sigma", "contrast", "TPR %", "FAR /h");
    printf("---------------------------------------------\n");
    double sigmas[] = { 1, 2, 3, 4, 5, 8, 12 };
    int contrasts[] = { 10, 20, 30, 40, 60, 80 };
    for (size_t si = 0; si < sizeof(sigmas) / sizeof(sigmas[0]); si++) {
        for (size_t ci = 0; ci < sizeof(contrasts) / sizeof(contrasts[0]); ci++) {
            outcome_t p = run_series(sigmas[si], contrasts[ci], true, TRIALS_POS, logn_override);
            outcome_t g = run_series(sigmas[si], contrasts[ci], false, TRIALS_NEG, logn_override);
            printf("%-6.0f %-9d | %3d%%             | %8.1f\n",
                   sigmas[si], contrasts[ci], p.tpr, g.far_per_hour);
        }
    }
    return 0;
}
