"""
Experiment configuration for the optimization project.
Edit this file to change scenes, SPP values, and branch names.
"""
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────
PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR   = PROJECT_ROOT / "build"
SCENES_DIR  = PROJECT_ROOT / "scenes"
RESULTS_DIR = PROJECT_ROOT / "results"
IMG_DIR     = RESULTS_DIR / "images"
DATA_DIR    = RESULTS_DIR / "data"
PLOT_DIR    = RESULTS_DIR / "plots"

PBRT_EXE    = BUILD_DIR / "pbrt.exe"
PBRT_EXE_BASELINE = BUILD_DIR / "pbrt_baseline.exe"  # v1's unoptimized BVH pbrt for comparison

# ── Scene ───────────────────────────────────────────────────
SCENE_FILE  = SCENES_DIR / "cloud" / "smoke.pbrt"
SCENE_NAME  = "smoke"

# Resolution overrides (optional, set None to use scene defaults)
OVERRIDE_RESOLUTION = (100, 100)   # Reduced for CPU rendering feasibility

# ── Reference image ─────────────────────────────────────────
REFERENCE_SPP  = 512
REFERENCE_FILE = IMG_DIR / f"{SCENE_NAME}_reference_spp{REFERENCE_SPP}.pfm"

# ── Sampling sweep ──────────────────────────────────────────
SPP_SWEEP = [4, 16, 64, 256]

# ── Branches / Experiments ──────────────────────────────────
EXPERIMENTS = {
    "baseline_bvh": {
        "label": "Baseline (Stock BVH)",
        "branch": "master",
        "pbrt_exe": PBRT_EXE_BASELINE,  # v1's pbrt with unoptimized BVH
        "description": "Original pbrt-v3 BVH acceleration structure (no optimizations). "
                       "Rendered with baseline pbrt for comparison.",
    },
    "optimized_bvh": {
        "label": "Optimized (SAH BVH 6-Phase)",
        "branch": "feat/optimization",
        "pbrt_exe": PBRT_EXE,  # v3's pbrt with all BVH optimizations
        "description": "Six-phase BVH optimization: prefix/suffix scan SAH, "
                       "parameterized costs, adaptive buckets, adaptive leaf "
                       "threshold, parallel build, SSE ray-box intersection.",
    },
}

# ── Adaptive sampling (Top 3) ───────────────────────────────
ADAPTIVE_VARIANCE_THRESHOLDS = [0.001, 0.005, 0.01, 0.05, 0.1]

# ── MIS ablation (Top 1) ────────────────────────────────────
MIS_BETA_VALUES = [1, 2, 4, 8, 16]

# ── Plot settings ───────────────────────────────────────────
FIGURE_DPI    = 150
FONT_FAMILY   = "serif"
COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b"]

# ── Derived convenience ─────────────────────────────────────
def img_path(scene, spp, experiment="baseline"):
    return IMG_DIR / f"{scene}_{experiment}_spp{spp:04d}.pfm"

def data_path(scene, experiment="baseline"):
    return DATA_DIR / f"{scene}_{experiment}_results.json"
