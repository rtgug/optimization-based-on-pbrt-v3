"""
Error metrics for comparing rendered images against a reference.
"""
import numpy as np
from pathlib import Path
from .io_utils import read_image, read_luminance


def mse(img: np.ndarray, ref: np.ndarray) -> float:
    """Mean Squared Error between two images (H,W,C) float32."""
    assert img.shape == ref.shape, f"Shape mismatch: {img.shape} vs {ref.shape}"
    return float(np.nanmean((img - ref) ** 2))


def rmse(img: np.ndarray, ref: np.ndarray) -> float:
    """Root Mean Squared Error."""
    return float(np.sqrt(mse(img, ref)))


def psnr(img: np.ndarray, ref: np.ndarray, max_val: float = 1.0) -> float:
    """Peak Signal-to-Noise Ratio in dB."""
    e = mse(img, ref)
    if e == 0:
        return float("inf")
    return float(20 * np.log10(max_val) - 10 * np.log10(e))


def rel_mse(img: np.ndarray, ref: np.ndarray, eps: float = 1e-6) -> float:
    """Relative MSE: MSE / (mean(ref²) + eps). Robust for dark images."""
    m = mse(img, ref)
    ref_power = float(np.nanmean(ref ** 2))
    return m / (ref_power + eps)


def per_pixel_error(img: np.ndarray, ref: np.ndarray) -> np.ndarray:
    """Return per-pixel squared error, shape (H, W)."""
    if img.ndim == 3 and img.shape[2] >= 1:
        # Use luminance for visual error maps
        def lum(x):
            if x.shape[2] == 1:
                return x[:, :, 0]
            return 0.2126 * x[:, :, 0] + 0.7152 * x[:, :, 1] + 0.0722 * x[:, :, 2]
        return (lum(img) - lum(ref)) ** 2
    return (img - ref) ** 2


def compute_metrics_for_file(image_path: str | Path,
                              reference_path: str | Path) -> dict:
    """Compute all metrics for a single image file against a reference."""
    img = read_image(image_path)
    ref = read_image(reference_path)
    return {
        "image": str(image_path),
        "reference": str(reference_path),
        "mse": mse(img, ref),
        "rmse": rmse(img, ref),
        "psnr": psnr(img, ref),
        "rel_mse": rel_mse(img, ref),
    }


def compute_convergence_data(image_paths: list,
                              reference_path: str | Path,
                              spp_values: list[int]) -> dict:
    """Compute MSE for a sweep of SPP values against a reference.

    Returns dict with keys 'spp', 'mse', 'rmse', 'psnr', 'rel_mse' —
    each a list of floats (one per SPP step).
    """
    ref = read_image(reference_path)
    result = {"spp": [], "mse": [], "rmse": [], "psnr": [], "rel_mse": []}
    for spp, img_path in zip(spp_values, image_paths):
        if not Path(img_path).exists():
            continue
        try:
            img = read_image(img_path)
            result["spp"].append(spp)
            result["mse"].append(mse(img, ref))
            result["rmse"].append(rmse(img, ref))
            result["psnr"].append(psnr(img, ref))
            result["rel_mse"].append(rel_mse(img, ref))
        except Exception as exc:
            print(f"  [WARN] Could not read {img_path}: {exc}")
    return result
