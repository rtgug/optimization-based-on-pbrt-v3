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

# ── Scene ───────────────────────────────────────────────────
SCENE_FILE  = SCENES_DIR / "cornell-box.pbrt"
SCENE_NAME  = "cornell-box"

# Resolution overrides (optional, set None to use scene defaults)
OVERRIDE_RESOLUTION = None   # use scene's native resolution (64x64)

# ── Reference image ─────────────────────────────────────────
REFERENCE_SPP  = 1024
REFERENCE_FILE = IMG_DIR / f"{SCENE_NAME}_reference_spp{REFERENCE_SPP}.pfm"

# ── Sampling sweep ──────────────────────────────────────────
SPP_SWEEP = [4, 16, 64, 256]

# ── Branches / Experiments ──────────────────────────────────
# Each experiment specifies:
#   label       – display name for plots
#   branch      – git branch to checkout (for multi-branch comparisons)
#   maxdepth    – override Integrator "maxdepth" in the scene (None = keep default)
#   rrthreshold – override Integrator "rrthreshold" in the scene (None = keep default)
#   description – human-readable summary
#
# Key: when rrthreshold=0, Russian roulette is DISABLED → fixed-depth
# truncation (biased). When rrthreshold≈0.25, RR is ACTIVE and paths
# terminate probabilistically (unbiased).
EXPERIMENTS = {
    "baseline": {
        "label": "Baseline (fixed-depth, biased)",
        "branch": "master",
        "maxdepth": 5,
        "rrthreshold": 0.0,   # RR disabled → pure fixed-depth truncation
        "description": "Standard path integrator with fixed bounce limit. "
                       "Paths are truncated at maxdepth=5, discarding "
                       "contributions from deeper bounces → biased.",
    },
    "russian_roulette": {
        "label": "Russian Roulette (unbiased)",
        "branch": "feat/optimization",
        "maxdepth": 50,
        "rrthreshold": 0.25,
        "description": "Path integrator with Russian roulette termination. "
                       "After 3 bounces, paths with contribution below "
                       "rrThreshold are probabilistically terminated with "
                       "survival weight compensation → unbiased.",
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
