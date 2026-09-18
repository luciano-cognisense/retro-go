#!/usr/bin/env python3
"""Validate the approved Hands on Lab SNES launcher artwork.

The final artwork is manually authored at the display's native resolution. This
script validates it instead of recreating the former placeholder design and
accidentally overwriting the approved files.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1] / "themes" / "hands-on-lab"
MAGENTA = (255, 0, 255)

ASSETS = {
    "background_snes.png": (320, 240),
    "banner_snes.png": (272, 24),
    "logo_snes.png": (46, 50),
    "button_a.png": (18, 18),
    "button_b.png": (18, 18),
}


def validate_asset(name: str, expected_size: tuple[int, int]) -> None:
    path = ROOT / name
    if not path.is_file():
        raise FileNotFoundError(f"Missing launcher asset: {path}")

    with Image.open(path) as image:
        if image.size != expected_size:
            raise ValueError(
                f"{name}: expected {expected_size[0]}x{expected_size[1]}, "
                f"got {image.width}x{image.height}"
            )

        if name in {"banner_snes.png", "logo_snes.png", "button_a.png", "button_b.png"}:
            colors = image.convert("RGB").getcolors(maxcolors=image.width * image.height)
            if not colors or MAGENTA not in {color for _, color in colors}:
                raise ValueError(f"{name}: pure #FF00FF transparency color not found")


if __name__ == "__main__":
    for asset_name, asset_size in ASSETS.items():
        validate_asset(asset_name, asset_size)
    print("Hands on Lab SNES launcher artwork is valid")
