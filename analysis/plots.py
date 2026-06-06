"""
Plotting functions for the optimization project.
All plots save to PLOT_DIR with consistent styling.
"""
import json
import numpy as np
from pathlib import Path

import matplotlib
matplotlib.use("Agg")                                 # non-interactive backend
import matplotlib.pyplot as plt

from .config import PLOT_DIR, COLORS, FIGURE_DPI, FONT_FAMILY

plt.rcParams.update({
    "font.family": FONT_FAMILY,
    "font.size": 11,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "figure.dpi": FIGURE_DPI,
    "savefig.dpi": FIGURE_DPI,
    "savefig.bbox": "tight",
    "savefig.pad_inches": 0.1,
})


def _save(name: str):
    path = PLOT_DIR / name
    plt.savefig(str(path))
    print(f"  [PLOT] Saved {path}")
    plt.close()


# ═══════════════════════════════════════════════════════════════
# 1. Convergence curve (MSE / RMSE vs SPP)
# ═══════════════════════════════════════════════════════════════

def plot_convergence(experiments_data: dict, metric: str = "rmse",
                     title: str = "Convergence Curve",
                     filename: str = "convergence.png"):
    """Plot convergence curve for one or more experiments.

    Parameters
    ----------
    experiments_data : dict
        {"baseline": {"spp": [...], "rmse": [...]}, "optimized": {...}}
    metric : str
        One of "mse", "rmse", "rel_mse", "psnr"
    """
    fig, ax = plt.subplots(figsize=(8, 5))

    for idx, (name, data) in enumerate(experiments_data.items()):
        color = COLORS[idx % len(COLORS)]
        ax.plot(data["spp"], data[metric], "o-",
                color=color, linewidth=1.5, markersize=5, label=name)

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("Samples Per Pixel (SPP)")
    ax.set_ylabel(metric.upper())
    ax.set_title(title)
    ax.legend(framealpha=0.8)
    ax.grid(True, alpha=0.3, which="both")
    _save(filename)


# ═══════════════════════════════════════════════════════════════
# 2. Error heatmap (per-pixel squared error)
# ═══════════════════════════════════════════════════════════════

def plot_error_heatmap(test_image: Path, reference_image: Path,
                       title: str = "Per-Pixel Error Heatmap",
                       filename: str = "error_heatmap.png"):
    """Generate a heatmap showing per-pixel squared error."""
    from .io_utils import read_luminance
    test_lum  = read_luminance(test_image)
    ref_lum   = read_luminance(reference_image)
    error_map = (test_lum - ref_lum) ** 2

    fig, ax = plt.subplots(figsize=(8, 6))
    im = ax.imshow(error_map, cmap="inferno", origin="upper",
                   aspect="equal", interpolation="nearest")
    cbar = plt.colorbar(im, ax=ax, shrink=0.82)
    cbar.set_label("Squared Luminance Error")
    ax.set_title(title)
    ax.axis("off")
    _save(filename)


def plot_side_by_side_heatmaps(baseline_img: Path, optimized_img: Path,
                                reference_img: Path,
                                filename: str = "error_comparison.png"):
    """Baseline error vs optimized error heatmaps, side-by-side."""
    from .io_utils import read_luminance
    ref_lum = read_luminance(reference_img)
    base_err = (read_luminance(baseline_img) - ref_lum) ** 2
    opt_err  = (read_luminance(optimized_img) - ref_lum) ** 2
    vmax = max(np.percentile(base_err, 99), np.percentile(opt_err, 99))

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    for ax, data, label in zip(axes, [base_err, opt_err],
                                ["Baseline Error", "Optimized Error"]):
        im = ax.imshow(data, cmap="inferno", origin="upper", vmin=0, vmax=vmax,
                       aspect="equal", interpolation="nearest")
        ax.set_title(label)
        ax.axis("off")

    cbar = fig.colorbar(im, ax=axes, shrink=0.7, pad=0.02)
    cbar.set_label("Squared Luminance Error")
    fig.suptitle("Error Heatmap Comparison")
    _save(filename)


# ═══════════════════════════════════════════════════════════════
# 3. 2D sampling pattern
# ═══════════════════════════════════════════════════════════════

def plot_sampling_pattern(samples: np.ndarray,
                          title: str = "2D Sampling Pattern",
                          filename: str = "sampling_pattern.png"):
    """Scatter-plot of 2D sample points (N, 2)."""
    fig, ax = plt.subplots(figsize=(6, 6))
    ax.scatter(samples[:, 0], samples[:, 1], s=18, c=COLORS[0],
               edgecolors="white", linewidth=0.3, alpha=0.85)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.set_xlabel("u")
    ax.set_ylabel("v")
    ax.set_title(title)
    ax.set_aspect("equal")
    ax.grid(True, alpha=0.2)
    _save(filename)


def plot_sampling_comparison(patterns: dict,
                             title: str = "Sampling Pattern Comparison",
                             filename: str = "sampling_comparison.png"):
    """Compare multiple 2D sampling patterns side-by-side.

    patterns : dict of {label: ndarray (N,2)}
    """
    n = len(patterns)
    fig, axes = plt.subplots(1, n, figsize=(5 * n, 5))
    if n == 1:
        axes = [axes]

    for ax, (label, samples) in zip(axes, patterns.items()):
        ax.scatter(samples[:, 0], samples[:, 1], s=12, c=COLORS[0],
                   edgecolors="white", linewidth=0.2, alpha=0.8)
        ax.set_xlim(0, 1); ax.set_ylim(0, 1)
        ax.set_xlabel("u"); ax.set_ylabel("v")
        ax.set_title(label)
        ax.set_aspect("equal")
        ax.grid(True, alpha=0.15)

    fig.suptitle(title)
    _save(filename)


# ═══════════════════════════════════════════════════════════════
# 4. Stacked bar chart – timing breakdown
# ═══════════════════════════════════════════════════════════════

def plot_timing_stack(baseline: dict, optimized: dict,
                      title: str = "Timing Breakdown Comparison",
                      filename: str = "timing_stack.png"):
    """Stacked bar chart for timing comparison.

    baseline / optimized are dicts like:
        {"Build": 0.5, "Render": 5.2, "Other": 0.1}
    """
    categories = list(baseline.keys())
    base_vals = [baseline.get(c, 0) for c in categories]
    opt_vals  = [optimized.get(c, 0) for c in categories]

    fig, ax = plt.subplots(figsize=(7, 5))
    x = np.arange(len(categories))
    width = 0.35

    bars1 = ax.bar(x - width/2, base_vals, width, label="Baseline",
                   color=COLORS[0], alpha=0.85)
    bars2 = ax.bar(x + width/2, opt_vals,  width, label="Optimized",
                   color=COLORS[1], alpha=0.85)

    # Annotate total time on top
    for bars in [bars1, bars2]:
        total = sum(b.get_height() for b in bars)
        ax.text(bars[0].get_x() + bars[0].get_width() / 2, total + max(base_vals + opt_vals) * 0.02,
                f"{total:.1f}s", ha="center", fontsize=10, fontweight="bold")

    ax.set_xticks(x)
    ax.set_xticklabels(categories)
    ax.set_ylabel("Time (seconds)")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")
    _save(filename)


# ═══════════════════════════════════════════════════════════════
# 5. Ablation study – grouped bar chart
# ═══════════════════════════════════════════════════════════════

def plot_ablation_bars(results: dict, metric: str = "rmse",
                       title: str = "Ablation Study",
                       filename: str = "ablation.png"):
    """Grouped bar chart for ablation experiments.

    results : dict of {variant_label: float}
    """
    labels = list(results.keys())
    values = list(results.values())

    fig, ax = plt.subplots(figsize=(max(5, len(labels) * 1.2), 5))
    bars = ax.bar(labels, values, color=COLORS[:len(labels)], alpha=0.85,
                  edgecolor="black", linewidth=0.5)

    for bar, val in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + max(values) * 0.02,
                f"{val:.4f}", ha="center", fontsize=9)

    ax.set_ylabel(metric.upper())
    ax.set_title(title)
    ax.grid(True, alpha=0.3, axis="y")
    fig.autofmt_xdate()
    _save(filename)


# ═══════════════════════════════════════════════════════════════
# 6. Complexity scaling plot (for BVH)
# ═══════════════════════════════════════════════════════════════

def plot_scaling(n_values: list, baseline_times: list, optimized_times: list,
                 title: str = "Complexity Scaling",
                 filename: str = "scaling.png"):
    """Log-log scaling plot: O(N) vs O(log N)."""
    fig, ax = plt.subplots(figsize=(8, 5))

    ax.plot(n_values, baseline_times, "o-", color=COLORS[0],
            label="Baseline O(N)", linewidth=1.5, markersize=6)
    ax.plot(n_values, optimized_times, "s--", color=COLORS[1],
            label="Optimized O(log N)", linewidth=1.5, markersize=6)

    ax.set_xscale("log", base=10)
    ax.set_yscale("log", base=10)
    ax.set_xlabel("Number of primitives (N)")
    ax.set_ylabel("Render time (seconds)")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, alpha=0.3, which="both")
    _save(filename)


# ═══════════════════════════════════════════════════════════════
# 7. Multi-panel summary figure
# ═══════════════════════════════════════════════════════════════

def plot_summary(experiments_data: dict, image_error_data: dict,
                 title: str = "Optimization Summary",
                 filename: str = "summary.png"):
    """Combined 2×2 summary: convergence + heatmap + ablation + timing."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Top-left: convergence
    ax = axes[0, 0]
    for idx, (name, data) in enumerate(experiments_data.items()):
        ax.plot(data["spp"], data["rmse"], "o-", color=COLORS[idx],
                linewidth=1.5, markersize=4, label=name)
    ax.set_xscale("log", base=2); ax.set_yscale("log")
    ax.set_xlabel("SPP"); ax.set_ylabel("RMSE")
    ax.set_title("Convergence"); ax.legend(fontsize=8); ax.grid(True, alpha=0.3)

    # Top-right: heatmap placeholder
    ax = axes[0, 1]
    ax.text(0.5, 0.5, "Error Heatmap\n(run full pipeline)", ha="center",
            va="center", transform=ax.transAxes, fontsize=12, color="gray")
    ax.set_title("Error Heatmap"); ax.axis("off")

    # Bottom-left: ablation
    ax = axes[1, 0]
    if image_error_data:
        labels = list(image_error_data.keys())
        vals = list(image_error_data.values())
        ax.bar(labels, vals, color=COLORS[:len(labels)], alpha=0.85)
        ax.set_ylabel("RMSE"); ax.set_title("Ablation Study")
        ax.grid(True, alpha=0.3, axis="y")
    else:
        ax.text(0.5, 0.5, "Ablation data\n(run experiments)", ha="center",
                va="center", transform=ax.transAxes, fontsize=12, color="gray")
        ax.axis("off")

    # Bottom-right: timing stack placeholder
    ax = axes[1, 1]
    ax.text(0.5, 0.5, "Timing Breakdown\n(run with timers)", ha="center",
            va="center", transform=ax.transAxes, fontsize=12, color="gray")
    ax.set_title("Timing Stack"); ax.axis("off")

    fig.suptitle(title, fontsize=15, fontweight="bold")
    plt.tight_layout()
    _save(filename)


if __name__ == "__main__":
    # Quick demo: generate a dummy convergence plot
    rng = np.random.default_rng(42)
    spp = [4, 8, 16, 32, 64, 128, 256, 512]
    demo = {
        "Baseline": {
            "spp": spp,
            "rmse": [0.1 / np.sqrt(s) + rng.normal(0, 0.002) for s in spp],
        },
        "Optimized": {
            "spp": spp,
            "rmse": [0.04 / np.sqrt(s) + rng.normal(0, 0.001) for s in spp],
        },
    }
    plot_convergence(demo, title="Demo Convergence Curve")
    print("Demo plot saved – check results/plots/")

    # Demo sampling pattern
    samples = rng.random((128, 2))
    plot_sampling_pattern(samples, title="Demo: Random 2D Samples")
    print("Done.")
