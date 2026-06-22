"""
Main analysis pipeline — orchestrates batch rendering + metrics + plotting.
Run from project root:
    python -m analysis.pipeline
    python -m analysis.pipeline --step reference
    python -m analysis.pipeline --step metrics
    python -m analysis.pipeline --step plots
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from analysis.config import (
    PROJECT_ROOT, SCENE_NAME, SPP_SWEEP, EXPERIMENTS,
    IMG_DIR, DATA_DIR, PLOT_DIR, REFERENCE_FILE, REFERENCE_SPP,
)
from analysis.io_utils import read_image
from analysis.metrics import compute_convergence_data, compute_metrics_for_file
from analysis.plots import (
    plot_convergence, plot_error_heatmap, plot_side_by_side_heatmaps,
)


def step_reference():
    """Render the ground-truth reference image at very high SPP."""
    from analysis.batch_render import modify_scene_spp, render_one
    from analysis.config import SCENE_FILE, OVERRIDE_RESOLUTION
    print(f"\n> Step 1: Rendering reference (SPP={REFERENCE_SPP})")

    if REFERENCE_FILE.exists():
        print(f"  Reference already exists -> {REFERENCE_FILE}")
        return

    scene_dir = SCENE_FILE.parent
    ref_stem = "_reference"
    tmp_scene = scene_dir / f"{ref_stem}.pbrt"
    tmp_exr   = scene_dir / f"{ref_stem}.pfm"

    modify_scene_spp(SCENE_FILE, REFERENCE_SPP, tmp_scene,
                     OVERRIDE_RESOLUTION, output_name=ref_stem)
    render_one(tmp_scene, REFERENCE_FILE, REFERENCE_SPP, cwd=scene_dir)

    if tmp_exr.exists() and not REFERENCE_FILE.exists():
        REFERENCE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp_exr.rename(REFERENCE_FILE)
    if tmp_scene.exists():
        tmp_scene.unlink()

    if REFERENCE_FILE.exists():
        print(f"  [OK] Reference saved -> {REFERENCE_FILE}")
    else:
        print(f"  [ERR] WARNING: Reference not found. Check pbrt output.")


def step_batch():
    """Run all experiment sweeps."""
    import subprocess
    print("\n> Step 2: Batch rendering experiments")
    subprocess.run([
        sys.executable, str(Path(__file__).parent / "batch_render.py"),
        "--experiment", "all",
    ])


def step_metrics():
    """Compute metrics for all rendered images against reference."""
    print("\n> Step 3: Computing metrics")

    if not REFERENCE_FILE.exists():
        print(f"  [ERR] Reference not found: {REFERENCE_FILE}")
        print("    Run: python -m analysis.pipeline --step reference")
        return

    all_convergence = {}

    for exp_name, exp_info in EXPERIMENTS.items():
        print(f"\n  Experiment: {exp_info['label']} ({exp_name})")

        # Collect images for this experiment
        image_paths = []
        valid_spp = []
        for spp in SPP_SWEEP:
            img = IMG_DIR / f"{SCENE_NAME}_{exp_name}_spp{spp:04d}.pfm"
            if img.exists():
                image_paths.append(img)
                valid_spp.append(spp)
            else:
                print(f"    [MISS] spp={spp}")

        if not image_paths:
            print(f"    No images found, skipping.")
            continue

        data = compute_convergence_data(image_paths, REFERENCE_FILE, valid_spp)
        all_convergence[exp_info["label"]] = data

        # Save per-experiment metrics JSON
        metrics_path = DATA_DIR / f"{SCENE_NAME}_{exp_name}_convergence.json"
        metrics_path.parent.mkdir(parents=True, exist_ok=True)
        with open(metrics_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print(f"    [OK] Saved -> {metrics_path}")
        print(f"    RMSE @ max SPP: {data['rmse'][-1]:.6f}" if data["rmse"] else "    No data")

    # Save combined data
    combined_path = DATA_DIR / f"{SCENE_NAME}_all_convergence.json"
    with open(combined_path, "w", encoding="utf-8") as f:
        json.dump(all_convergence, f, indent=2, ensure_ascii=False)
    print(f"\n  Combined data -> {combined_path}")


def step_plots():
    """Generate all plots from computed metrics."""
    print("\n> Step 4: Generating plots")

    combined_path = DATA_DIR / f"{SCENE_NAME}_all_convergence.json"
    if not combined_path.exists():
        print(f"  [ERR] Metrics data not found: {combined_path}")
        print("    Run: python -m analysis.pipeline --step metrics")
        return

    with open(combined_path, "r", encoding="utf-8") as f:
        all_data = json.load(f)

    # ── Convergence curve ──
    print("\n  Plot 1: Convergence curve")
    plot_convergence(all_data, metric="rmse",
                     title="Convergence Curve — Optimized BVH vs Stock BVH (Smoke Scene)",
                     filename="smoke_bvh_convergence.png")

    # ── Error heatmap (best-SPP baseline) ──
    print("  Plot 2: Error heatmap")
    max_spp = SPP_SWEEP[-1]
    baseline_img = IMG_DIR / f"{SCENE_NAME}_baseline_bvh_spp{max_spp:04d}.pfm"
    if baseline_img.exists() and REFERENCE_FILE.exists():
        plot_error_heatmap(
            baseline_img, REFERENCE_FILE,
            title=f"Per-Pixel Error — Stock BVH Baseline (Smoke Scene, SPP={max_spp})",
            filename="smoke_bvh_error_heatmap.png",
        )

    # ── If multiple experiments: side-by-side error comparison ──
    exp_keys = list(EXPERIMENTS.keys())
    if len(exp_keys) >= 2:
        print("  Plot 3: Side-by-side error comparison")
        img_a = IMG_DIR / f"{SCENE_NAME}_{exp_keys[0]}_spp{max_spp:04d}.pfm"
        img_b = IMG_DIR / f"{SCENE_NAME}_{exp_keys[1]}_spp{max_spp:04d}.pfm"
        if img_a.exists() and img_b.exists():
            plot_side_by_side_heatmaps(
                img_a, img_b, REFERENCE_FILE,
                filename="smoke_bvh_error_comparison.png",
            )

    print("\n  [OK] All plots saved ->", PLOT_DIR)


def main():
    parser = argparse.ArgumentParser(description="Analysis pipeline for pbrt-v3 optimization")
    parser.add_argument("--step", type=str, default="all",
                        choices=["reference", "batch", "metrics", "plots", "all"],
                        help="Which step to run (default: all)")
    parser.add_argument("--spp", type=str, default=None,
                        help="Comma-separated SPP list (for batch step)")
    args = parser.parse_args()

    IMG_DIR.mkdir(parents=True, exist_ok=True)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    PLOT_DIR.mkdir(parents=True, exist_ok=True)

    steps = {
        "reference": step_reference,
        "batch": step_batch,
        "metrics": step_metrics,
        "plots": step_plots,
        "all": lambda: (step_reference(), step_batch(), step_metrics(), step_plots()),
    }

    fn = steps.get(args.step)
    if fn:
        fn()
    else:
        print(f"Unknown step: {args.step}")
        sys.exit(1)

    print(f"\n{'='*60}")
    print("PIPELINE COMPLETE")
    print(f"  Images:  {IMG_DIR}")
    print(f"  Data:    {DATA_DIR}")
    print(f"  Plots:   {PLOT_DIR}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
