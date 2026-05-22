#!/usr/bin/env python3
"""Convert one or more SVG icons to single-channel SDF-like PNGs that the
cockpit / overlay SDF shader (SetSDFMode(1)) accepts.

Output naming follows the repo's resource convention:
    resource/binary/<author>/<category>/<name>-<version>.aatex.png

Pipeline (no scipy/numpy dependency):
  1. Inkscape rasterises the SVG to a 1024×1024 RGBA mask.
  2. alpha channel → hard 0/255 binary.
  3. PIL Gaussian blur (radius 24 on hi-res) → soft edge band that
     approximates a signed distance field across the band's ±radius range.
     This is the standard "blur-SDF" trick — sufficient for ~128 px icons.
  4. Lanczos downsample to 128×128 → save as 8-bit grayscale PNG.

True EDT (Felzenszwalb-Huttenlocher) would be more accurate but needs
numpy/scipy, which aren't always installed.

Usage:
    svg_to_sdf_icon.py SRC.svg DEST.png [--size N] [--blur R] [--hi N]

Used by hand right now; ideally invoked from the build system once mobile
asset trees are consolidated. See /tmp/svg2sdf.py for the original batch
runner that generated the six overlay icons (overlay_menu_sdf …
overlay_camera_sdf).
"""

import argparse
import os
import subprocess
import sys
from PIL import Image, ImageFilter

# Snap-installed Inkscape can't write under /tmp; we use the source SVG's
# directory or fall back to the user's home.
def _staging_path(svg_path: str) -> str:
    base = os.path.dirname(os.path.abspath(svg_path))
    if not base:
        base = os.path.expanduser("~")
    return os.path.join(base, "._svg2sdf_hi.png")


def rasterise(svg_path: str, hi: int, staging: str) -> Image.Image:
    if os.path.exists(staging):
        os.remove(staging)
    subprocess.run(
        ["inkscape", svg_path,
         "--export-type=png",
         f"--export-filename={staging}",
         f"--export-width={hi}", f"--export-height={hi}",
         "--export-area-page"],
        check=True, capture_output=True,
    )
    return Image.open(staging).convert("RGBA")


def svg_to_sdf(svg: str, out: str, *, size: int, blur: int, hi: int) -> None:
    staging = _staging_path(svg)
    img = rasterise(svg, hi, staging)
    alpha = img.split()[-1]
    mask = alpha.point(lambda v: 255 if v > 64 else 0)
    blurred = mask.filter(ImageFilter.GaussianBlur(blur))
    field = blurred.resize((size, size), Image.LANCZOS).convert("L")

    # Wrap the SDF in RGBA, putting the field in the ALPHA channel and
    # leaving RGB solid white. The cockpit / overlay SDF shader path
    # (rRenderStateKey::SDFTextured → uber.frag's SDF-outline branch)
    # reads `texSample.a` for the distance field — see the cockpit's
    # btn_*_sdf-0.1.aatex.png, which use the same layout. Saving as
    # single-channel L would set alpha = 255 everywhere and the shader
    # would render an opaque rectangle.
    from PIL import Image as _Im
    white = _Im.new("L", field.size, 255)
    rgba  = _Im.merge("RGBA", (white, white, white, field))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    rgba.save(out, "PNG")
    try:
        os.remove(staging)
    except OSError:
        pass


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("src", help="path to SVG")
    p.add_argument("dst", help="path to output PNG")
    p.add_argument("--size", type=int, default=128, help="final SDF resolution (default 128)")
    p.add_argument("--blur", type=int, default=24,  help="blur radius on hi-res mask (default 24)")
    p.add_argument("--hi",   type=int, default=1024, help="rasterise resolution (default 1024)")
    args = p.parse_args()
    svg_to_sdf(args.src, args.dst, size=args.size, blur=args.blur, hi=args.hi)
    print(f"  {args.src} → {args.dst} ({args.size}×{args.size})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
