"""
Batch rendering script — runs pbrt across SPP sweeps and experiments.
Call from project root:
    python analysis/batch_render.py [--spp 64,256,1024] [--experiment baseline]
"""
import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

# Append project root so we can import analysis modules
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from analysis.config import (
    PROJECT_ROOT, BUILD_DIR, SCENE_FILE, SCENE_NAME, PBRT_EXE,
    SPP_SWEEP, EXPERIMENTS, IMG_DIR, DATA_DIR, OVERRIDE_RESOLUTION,
)


def modify_integrator_params(text: str, maxdepth: int = None,
                            rrthreshold: float = None) -> str:
    """Modify (or add) integrator parameters in scene text.

    Finds the ``Integrator "path"`` line, strips any existing
    *maxdepth* / *rrthreshold* parameters, then appends the
    requested values.  Parameters left as ``None`` are left
    unchanged (any existing value is preserved only if both
    are None, otherwise stripped).
    """
    if maxdepth is None and rrthreshold is None:
        return text

    import re
    lines = text.split('\n')
    for i, line in enumerate(lines):
        if re.match(r'\s*Integrator\s+"path"', line):
            # Remove existing maxdepth / rrthreshold if present
            line = re.sub(r'"integer maxdepth"\s+\[\d+\]', '', line)
            line = re.sub(r'"float rrthreshold"\s+\[[\d.]+(?:e[+-]?\d+)?\]',
                          '', line)
            # Collapse whitespace
            line = re.sub(r'\s{2,}', ' ', line).strip()
            # Append requested parameters
            if maxdepth is not None:
                line += f' "integer maxdepth" [{maxdepth}]'
            if rrthreshold is not None:
                line += f' "float rrthreshold" [{rrthreshold}]'
            lines[i] = line
            break
    return '\n'.join(lines)


def modify_scene_spp(scene_path: Path, spp: int, out_path: Path,
                     override_resolution: tuple | None = None,
                     output_name: str = "temp_render",
                     maxdepth: int = None,
                     rrthreshold: float = None) -> Path:
    """Create a temporary scene with modified SPP, resolution, output name,
    and integrator parameters.

    The temp file is placed next to the original scene so relative
    ``Include`` paths (e.g. ``geometry/killeroo.pbrt``) still resolve.

    Returns path to the temporary .pbrt file.
    """
    text = scene_path.read_text(encoding="utf-8")

    import re

    # --- SPP ---
    text = re.sub(
        r'(Sampler\s+"\w+"\s+.*?"integer pixelsamples"\s+\[)\d+(\])',
        rf'\g<1>{spp}\g<2>',
        text
    )

    # --- Resolution ---
    if override_resolution:
        w, h = override_resolution
        text = re.sub(
            r'("integer xresolution"\s+\[)\d+(\])',
            rf'\g<1>{w}\g<2>',
            text
        )
        text = re.sub(
            r'("integer yresolution"\s+\[)\d+(\])',
            rf'\g<1>{h}\g<2>',
            text
        )

    # --- Output filename (free-standing quoted string, not inside []) ---
    text = re.sub(
        r'("string filename"\s+)"[^"]+\.(exr|pfm|png|tga)"',
        rf'\g<1>"{output_name}.pfm"',
        text
    )

    # --- Integrator parameters (maxdepth, rrthreshold) ---
    text = modify_integrator_params(text, maxdepth=maxdepth,
                                    rrthreshold=rrthreshold)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text, encoding="utf-8")
    return out_path


def render_one(scene_path: Path, output_exr: Path, spp: int,
               nthreads: int = 0, cwd: Path | None = None) -> dict:
    """Run pbrt once; return timing dict.

    pbrt writes its output to the filename declared inside the scene
    file, in whatever directory it was launched from.  Set *cwd* to
    the scene's directory so relative ``Include`` paths work.
    """
    start = time.perf_counter()

    cmd = [str(PBRT_EXE)]
    if nthreads > 0:
        cmd += ["--nthreads", str(nthreads)]
    cmd.append(str(scene_path))

    work_dir = str(cwd) if cwd else str(PROJECT_ROOT)

    print(f"  -> pbrt --spp {spp}  ", end="", flush=True)
    try:
        result = subprocess.run(
            cmd,
            cwd=work_dir,
            capture_output=True,
            text=True,
            timeout=3600,
        )
        elapsed = time.perf_counter() - start
        ok = result.returncode == 0
        print(f"[{'OK' if ok else 'FAIL'}]  {elapsed:.1f}s")
        if not ok:
            print(f"    stderr: {result.stderr[:300]}")
    except subprocess.TimeoutExpired:
        elapsed = 3600
        ok = False
        print(f"[TIMEOUT]")

    return {
        "spp": spp,
        "success": ok,
        "elapsed_sec": round(elapsed, 2),
        "timestamp": datetime.now().isoformat(),
    }


def run_sweep(scene: Path, experiment_name: str, spp_list: list,
              nthreads: int = 0, maxdepth: int = None,
              rrthreshold: float = None) -> list:
    """Run one experiment across all SPP values.

    Temp scenes are written into the scene's directory so relative
    ``Include`` statements stay valid.  Output images are moved from
    the scene directory to ``results/images/`` after each render.

    *maxdepth* and *rrthreshold*, when provided, override the
    corresponding integrator parameters in the scene file.
    """
    results = []
    scene_dir = scene.parent

    for i, spp in enumerate(spp_list):
        out_exr = IMG_DIR / f"{SCENE_NAME}_{experiment_name}_spp{spp:04d}.pfm"
        tmp_stem = f"_tmp_{experiment_name}_spp{spp:04d}"
        tmp_scene = scene_dir / f"{tmp_stem}.pbrt"
        tmp_exr   = scene_dir / f"{tmp_stem}.pfm"   # where pbrt writes

        # Skip if already rendered
        if out_exr.exists():
            print(f"  [SKIP] spp={spp} already exists -> {out_exr.name}")
            results.append({
                "spp": spp, "success": True, "elapsed_sec": 0,
                "cached": True, "image": str(out_exr),
            })
            continue

        modify_scene_spp(scene, spp, tmp_scene, OVERRIDE_RESOLUTION,
                         output_name=tmp_stem,
                         maxdepth=maxdepth, rrthreshold=rrthreshold)
        r = render_one(tmp_scene, out_exr, spp, nthreads, cwd=scene_dir)
        r["image"] = str(out_exr)

        # Move pbrt output from scene_dir to results/images/
        if tmp_exr.exists():
            out_exr.parent.mkdir(parents=True, exist_ok=True)
            tmp_exr.rename(out_exr)
            r["success"] = True
        else:
            # Fallback: check common locations
            for candidate in [
                scene_dir / f"{tmp_stem}.pfm",
                PROJECT_ROOT / f"{tmp_stem}.pfm",
                BUILD_DIR / f"{tmp_stem}.pfm",
            ]:
                if candidate.exists():
                    candidate.rename(out_exr)
                    r["success"] = True
                    break
            else:
                r["success"] = False
                r["error"] = "Output image not found"

        results.append(r)

        # Cleanup temp scene
        if tmp_scene.exists():
            tmp_scene.unlink()

    return results


def main():
    parser = argparse.ArgumentParser(description="Batch render pbrt experiments")
    parser.add_argument("--spp", type=str, default=None,
                        help="Comma-separated SPP list, e.g. 16,64,256")
    parser.add_argument("--experiment", type=str, default=None,
                        help="Experiment key from config, or 'all'")
    parser.add_argument("--reference", action="store_true",
                        help="Render reference (very high SPP)")
    parser.add_argument("--nthreads", type=int, default=0,
                        help="Number of render threads (0 = auto)")
    args = parser.parse_args()

    spp_list = SPP_SWEEP
    if args.spp:
        spp_list = [int(s.strip()) for s in args.spp.split(",")]

    experiments_to_run = EXPERIMENTS
    if args.experiment and args.experiment != "all":
        key = args.experiment
        if key not in EXPERIMENTS:
            print(f"Unknown experiment: {key}. Available: {list(EXPERIMENTS)}")
            sys.exit(1)
        experiments_to_run = {key: EXPERIMENTS[key]}

    # ── Reference render ──
    if args.reference:
        from analysis.config import REFERENCE_SPP, REFERENCE_FILE
        print(f"\n{'='*60}")
        print(f"RENDERING REFERENCE  (SPP={REFERENCE_SPP})")
        print(f"{'='*60}")
        scene_dir = SCENE_FILE.parent
        ref_stem = "_reference"
        tmp_scene = scene_dir / f"{ref_stem}.pbrt"
        tmp_exr   = scene_dir / f"{ref_stem}.pfm"
        modify_scene_spp(SCENE_FILE, REFERENCE_SPP, tmp_scene,
                         OVERRIDE_RESOLUTION, output_name=ref_stem,
                         maxdepth=50, rrthreshold=0.0)  # unbiased reference
        r = render_one(tmp_scene, REFERENCE_FILE, REFERENCE_SPP,
                       args.nthreads, cwd=scene_dir)
        if tmp_exr.exists() and not REFERENCE_FILE.exists():
            REFERENCE_FILE.parent.mkdir(parents=True, exist_ok=True)
            tmp_exr.rename(REFERENCE_FILE)
        if tmp_scene.exists():
            tmp_scene.unlink()
        print(f"Reference: {r}")

    # ── Experiment sweeps ──
    for exp_name, exp_info in experiments_to_run.items():
        print(f"\n{'='*60}")
        print(f"EXPERIMENT: {exp_info['label']}  ({exp_name})")
        print(f"  SPP sweep: {spp_list}")
        print(f"{'='*60}")
        results = run_sweep(
            SCENE_FILE, exp_name, spp_list, args.nthreads,
            maxdepth=exp_info.get("maxdepth"),
            rrthreshold=exp_info.get("rrthreshold"),
        )

        # Save results JSON
        data_path = DATA_DIR / f"{SCENE_NAME}_{exp_name}_results.json"
        data_path.parent.mkdir(parents=True, exist_ok=True)
        with open(data_path, "w", encoding="utf-8") as f:
            json.dump(results, f, indent=2, ensure_ascii=False)
        print(f"  Results saved -> {data_path}")

    print(f"\n{'='*60}")
    print("BATCH RENDERING COMPLETE")
    print(f"  Images: {IMG_DIR}")
    print(f"  Data:   {DATA_DIR}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
