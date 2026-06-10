"""
Adaptive sampling analysis: visualize variance maps and sample allocation.
Run after rendering with the AdaptiveIntegrator to compare results.

Usage:
    python analysis/adaptive_analysis.py
"""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from analysis.config import (
    SCENE_NAME, SPP_SWEEP, IMG_DIR, DATA_DIR, PLOT_DIR,
    REFERENCE_FILE,
)
from analysis.io_utils import read_image, read_luminance
from analysis.metrics import mse, rmse, psnr

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm

plt.rcParams.update({
    "font.family": "serif",
    "font.size": 11,
    "figure.dpi": 150,
    "savefig.dpi": 150,
    "savefig.bbox": "tight",
})


def plot_variance_comparison(baseline_error, adaptive_error, filename):
    """Side-by-side error heatmap with equal color scale."""
    vmax = max(np.percentile(baseline_error, 99),
               np.percentile(adaptive_error, 99))

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))

    titles = ["Baseline Error", "Adaptive Error", "Improvement"]
    data = [baseline_error, adaptive_error,
            baseline_error - adaptive_error]

    cmaps = ["inferno", "inferno", "coolwarm"]
    for ax, d, t, cm in zip(axes, data, titles, cmaps):
        if t == "Improvement":
            v = max(abs(np.percentile(d, 1)), abs(np.percentile(d, 99)))
            im = ax.imshow(d, cmap=cm, vmin=-v, vmax=v, aspect="equal",
                           interpolation="nearest")
        else:
            im = ax.imshow(d, cmap=cm, vmin=0, vmax=vmax, aspect="equal",
                           interpolation="nearest")
        ax.set_title(t)
        ax.axis("off")
        plt.colorbar(im, ax=ax, shrink=0.8)

    fig.suptitle(f"Adaptive vs Baseline Error – {SCENE_NAME}", fontsize=14)
    fig.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(str(path))
    print(f"  [PLOT] Saved {path}")
    plt.close()


def plot_allocation_map(alloc_data: np.ndarray, filename: str):
    """Visualize the sample allocation as a heatmap."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    im0 = axes[0].imshow(alloc_data, cmap="viridis", aspect="equal",
                         interpolation="nearest")
    axes[0].set_title("Sample Allocation (per pixel)")
    axes[0].axis("off")
    plt.colorbar(im0, ax=axes[0], shrink=0.8, label="Samples allocated")

    # Histogram of allocation
    axes[1].hist(alloc_data.ravel(), bins=50, color="steelblue",
                 edgecolor="white", alpha=0.85)
    axes[1].set_xlabel("Samples per pixel")
    axes[1].set_ylabel("Pixel count")
    axes[1].set_title(f"Allocation distribution\n"
                      f"min={alloc_data.min()}, "
                      f"max={alloc_data.max()}, "
                      f"mean={alloc_data.mean():.1f}")
    axes[1].grid(True, alpha=0.3)

    fig.suptitle(f"Optimal Sample Allocation – {SCENE_NAME}", fontsize=14)
    fig.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(str(path))
    print(f"  [PLOT] Saved {path}")
    plt.close()


def plot_efficiency_curve(spp_values, baseline_rmse, adaptive_rmse, filename):
    """Log-log convergence curve comparing baseline vs adaptive."""
    fig, ax = plt.subplots(figsize=(8, 5))

    ax.plot(spp_values, baseline_rmse, "o-", color="#1f77b4",
            linewidth=1.5, markersize=5, label="Baseline (uniform)")
    ax.plot(spp_values, adaptive_rmse, "s--", color="#ff7f0e",
            linewidth=1.5, markersize=5, label="Adaptive (Lagrange)")

    # Theoretical O(1/sqrt(N)) reference line
    ref_spp = np.array(spp_values)
    ref_rmse = baseline_rmse[0] * np.sqrt(spp_values[0] / ref_spp)
    ax.plot(ref_spp, ref_rmse, ":", color="gray", alpha=0.5,
            label="O(1/√N) slope")

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("Equivalent SPP")
    ax.set_ylabel("RMSE")
    ax.set_title(f"Convergence: Adaptive vs Baseline – {SCENE_NAME}")
    ax.legend(framealpha=0.8)
    ax.grid(True, alpha=0.3, which="both")

    # Annotate speedup at max SPP
    if len(spp_values) > 1:
        speedup = baseline_rmse[-1] / adaptive_rmse[-1]
        ax.annotate(f"RMSE ratio @max SPP = {speedup:.3f}×",
                    xy=(spp_values[-1], adaptive_rmse[-1]),
                    xytext=(spp_values[-1] * 0.6, adaptive_rmse[-1] * 1.8),
                    fontsize=10,
                    bbox=dict(boxstyle="round", fc="wheat", alpha=0.8))

    fig.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(str(path))
    print(f"  [PLOT] Saved {path}")
    plt.close()


def main():
    # Find baseline and adaptive experiment images
    baseline_images = {}
    adaptive_images = {}

    for spp in SPP_SWEEP:
        base = IMG_DIR / f"{SCENE_NAME}_baseline_spp{spp:04d}.pfm"
        adap = IMG_DIR / f"{SCENE_NAME}_adaptive_lagrange_spp{spp:04d}.pfm"
        if base.exists():
            baseline_images[spp] = base
        if adap.exists():
            adaptive_images[spp] = adap

    if not baseline_images:
        print("No baseline images found. Run the pipeline first.")
        return

    if not adaptive_images:
        print("No adaptive images found. Run adaptive experiment first.")
        print("  python -m analysis.batch_render --experiment adaptive_lagrange")
        return

    if not REFERENCE_FILE.exists():
        print("No reference image. Run: python -m analysis.pipeline --step reference")
        return

    ref = read_image(REFERENCE_FILE)

    # ── 1. Convergence curve ──
    print("\n> Plotting convergence curves...")
    spp_list = sorted(set(baseline_images.keys()) & set(adaptive_images.keys()))
    baseline_rmse = []
    adaptive_rmse = []
    for spp in spp_list:
        baseline_rmse.append(rmse(read_image(baseline_images[spp]), ref))
        adaptive_rmse.append(rmse(read_image(adaptive_images[spp]), ref))

    plot_efficiency_curve(spp_list, baseline_rmse, adaptive_rmse,
                          f"{SCENE_NAME}_adaptive_convergence.png")

    # ── 2. Per-pixel error comparison at highest SPP ──
    print("\n> Plotting error comparison...")
    max_spp = max(spp_list)
    base_lum = read_luminance(baseline_images[max_spp])
    adap_lum = read_luminance(adaptive_images[max_spp])
    ref_lum = read_luminance(REFERENCE_FILE)

    base_err = (base_lum - ref_lum) ** 2
    adap_err = (adap_lum - ref_lum) ** 2

    plot_variance_comparison(base_err, adap_err,
                             f"{SCENE_NAME}_adaptive_comparison.png")

    # ── 3. Print numerical summary ──
    print(f"\n{'='*60}")
    print(f"ADAPTIVE SAMPLING SUMMARY – {SCENE_NAME}")
    print(f"{'='*60}")
    print(f"\n  Metric         @{max_spp} SPP     Baseline    Adaptive    Improvement")
    print(f"  {'─'*58}")
    for spp in spp_list:
        b_mse = mse(read_image(baseline_images[spp]), ref)
        a_mse = mse(read_image(adaptive_images[spp]), ref)
        b_rmse = np.sqrt(b_mse)
        a_rmse = np.sqrt(a_mse)
        b_psnr = psnr(read_image(baseline_images[spp]), ref)
        a_psnr = psnr(read_image(adaptive_images[spp]), ref)
        impr = (b_mse - a_mse) / b_mse * 100 if b_mse > 0 else 0
        print(f"  RMSE @ spp={spp:<4d}  {b_rmse:.6f}    {a_rmse:.6f}    {impr:+.1f}%")
        print(f"  PSNR @ spp={spp:<4d}  {b_psnr:.2f}    {a_psnr:.2f}    {a_psnr - b_psnr:+.2f} dB")

    # Equivalent SPP for adaptive to match baseline RMSE
    print(f"\n  ── Efficiency Analysis ──")
    for i, spp in enumerate(spp_list):
        if i < len(spp_list) - 1:
            b_rmse_i = baseline_rmse[i]
            # Find where adaptive curve crosses baseline
            frac = b_rmse_i / adaptive_rmse[-1] if adaptive_rmse[-1] > 0 else 1
            equiv_spp = spp_list[-1] / (frac * frac) if frac > 0 else spp_list[-1]
            print(f"  To match baseline RMSE={b_rmse_i:.6f} @spp={spp}, "
                  f"adaptive needs ~{equiv_spp:.0f} equiv SPP "
                  f"(savings: {100*(1-spp/equiv_spp):+.0f}%)")

    print(f"\n{'='*60}")
    print(f"Plots saved to: {PLOT_DIR}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
