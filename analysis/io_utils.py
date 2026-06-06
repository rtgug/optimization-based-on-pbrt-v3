"""
Image I/O utilities.
Reads EXR (via imageio) and PFM (pure Python).
Also handles PNM/PPM which pbrt can output.
"""
import struct
import numpy as np
from pathlib import Path


# ═══════════════════════════════════════════════════════════════
# PFM – pure Python, zero dependencies
# ═══════════════════════════════════════════════════════════════

def read_pfm(path: str | Path) -> np.ndarray:
    """Read a PFM file, return float32 RGB array (H, W, 3)."""
    with open(path, "rb") as f:
        # header line
        header = f.readline().decode("ascii").strip()
        color = header == "PF"       # True → RGB, False → grayscale
        nchan = 3 if color else 1

        # dimensions
        dims = f.readline().decode("ascii").strip()
        try:
            w, h = map(int, dims.split())
        except ValueError:
            w, h = map(int, dims.split(b" "))

        # scale (endianness indicator)
        scale = float(f.readline().decode("ascii").strip())
        big_endian = scale > 0
        endian = ">" if big_endian else "<"
        fmt = f"{endian}{nchan * w * h}f"

        # raw pixel data
        raw = f.read()
        pixels = struct.unpack(fmt, raw)

    data = np.array(pixels, dtype=np.float32).reshape((h, w, nchan))
    if not color:
        data = data[..., np.newaxis]
    # PFM stores bottom-to-top; flip vertically
    return np.flipud(data).copy()


def write_pfm(path: str | Path, data: np.ndarray):
    """Write a float32 RGB array (H, W, 3) as a PFM file."""
    h, w = data.shape[:2]
    nchan = data.shape[2] if data.ndim == 3 else 1
    header = "PF\n" if nchan == 3 else "Pf\n"
    dims = f"{w} {h}\n"
    scale = "-1.0\n"   # negative → little-endian

    # PFM stores bottom-to-top; flip
    flipped = np.flipud(data.astype(np.float32))
    raw = struct.pack(f"<{nchan * w * h}f", *flipped.ravel())

    with open(path, "wb") as f:
        f.write(header.encode("ascii"))
        f.write(dims.encode("ascii"))
        f.write(scale.encode("ascii"))
        f.write(raw)


# ═══════════════════════════════════════════════════════════════
# EXR – via imageio (FreeImage backend)
# ═══════════════════════════════════════════════════════════════

def read_exr(path: str | Path) -> np.ndarray:
    """Read an EXR file, return float32 RGB array (H, W, 3)."""
    import imageio.v3 as iio
    img = iio.imread(str(path))
    # imageio returns (H, W, C) or (H, W) – normalise
    if img.ndim == 2:
        img = img[..., np.newaxis]
    return img.astype(np.float32)


def write_exr(path: str | Path, data: np.ndarray):
    """Write a float32 RGB array (H, W, 3) as an EXR file."""
    import imageio.v3 as iio
    iio.imwrite(str(path), data.astype(np.float32))


# ═══════════════════════════════════════════════════════════════
# Generic reader
# ═══════════════════════════════════════════════════════════════

def read_image(path: str | Path) -> np.ndarray:
    """Auto-detect format and read image, returning float32 RGB (H,W,3)."""
    path = Path(path)
    ext = path.suffix.lower()
    if ext == ".pfm":
        return read_pfm(path)
    elif ext == ".exr":
        return read_exr(path)
    elif ext in (".png", ".jpg", ".jpeg", ".tga", ".bmp"):
        import imageio.v3 as iio
        img = iio.imread(str(path))
        if img.ndim == 2:
            img = img[..., np.newaxis]
        # Normalise 8-bit to [0, 1]
        if img.dtype == np.uint8:
            img = img.astype(np.float32) / 255.0
        return img.astype(np.float32)
    else:
        raise ValueError(f"Unsupported image format: {ext}")


def read_luminance(path: str | Path) -> np.ndarray:
    """Read image and return luminance (Y channel), shape (H, W)."""
    rgb = read_image(path)
    if rgb.shape[2] == 1:
        return rgb[:, :, 0]
    # ITU-R BT.709 luminance weights
    return 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]
