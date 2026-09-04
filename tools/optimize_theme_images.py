#!/usr/bin/env python3
"""Losslessly reduce true-color theme backgrounds to indexed PNG files."""

import argparse
from pathlib import Path

from PIL import Image


def optimize(path: Path) -> None:
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        colors = rgba.getcolors(maxcolors=256)
        if colors is None:
            raise ValueError(f"{path}: more than 256 colors")

        # Backgrounds are opaque. Dropping an unused alpha channel allows an
        # exact adaptive palette instead of retaining a larger RGBA PNG.
        if all(alpha == 255 for _, (*_, alpha) in colors):
            image = rgba.convert("RGB")
        else:
            image = rgba

        palette = image.quantize(colors=len(colors), method=Image.MEDIANCUT)
        if palette.convert(image.mode).tobytes() != image.tobytes():
            raise ValueError(f"{path}: palette conversion changed pixels")
        palette.save(path, format="PNG", optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("theme_dir", type=Path)
    args = parser.parse_args()

    paths = sorted(args.theme_dir.glob("background_*.png"))
    for path in paths:
        optimize(path)
    print(f"Optimized {len(paths)} backgrounds in {args.theme_dir}")


if __name__ == "__main__":
    main()
