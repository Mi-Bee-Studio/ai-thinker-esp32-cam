# lm_sim — host-side regression harness for the ΣΔ motion pipeline

Validates `main/lm_motion.c` against the acceptance matrix of the 2026-09-04
external algorithm report (dark-scene motion + flash): 0 false triggers
across the full σ×contrast sweep, detection boundary ≈ 4.5σ̂.

    gcc -O2 -Wall -Wextra -I../../main -o sim sim.c ../../main/lm_motion.c -lm
    ./sim          # default params (as shipped)
    ./sim 1        # override ΣΔ logn (N=2) — reference experiment

Scenario per the report §6.1: 80×60 grid, textured background, per-pixel
Gaussian noise σ, 0.3% hot pixels, AGC pump (+22 DN × 3 frames / 45),
5×7 elliptical target frames 60–110 @ 1.5 px/frame, 200 frames @ 5 fps,
8 positive + 8 negative trials per cell.

Shipped-parameter results (2026-09-04, this repo):
- FAR = 0.0/h in ALL 42 cells (incl. σ=12 via the σ̂≥13 quiet gate)
- Detection boundary: σ=1→C10, σ=2/3→C20, σ=4/5→C30, σ=8→C40 (=4.5σ̂),
  σ=12→quiet (per report §2 doctrine: 宁可漏报不误报; PIR territory).

Run after ANY change to lm_motion.c.
