#!/usr/bin/env python3
"""Generate the Hands on Lab SNES artwork on the display's native pixel grid."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1] / "themes" / "hands-on-lab"

NAVY = (16, 43, 78)
CREAM = (251, 247, 239)
AMBER = (240, 167, 43)
PALE_BLUE = (210, 221, 231)
PALE_GRID = (236, 233, 224)
MAGENTA = (255, 0, 255)

PIXEL_FONT = {
    "S": (0b11111, 0b10000, 0b10000, 0b11111, 0b00001, 0b00001, 0b11111),
    "N": (0b10001, 0b11001, 0b11001, 0b10101, 0b10011, 0b10011, 0b10001),
    "E": (0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111),
}


def pixel_text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, scale: int = 2) -> None:
    x, y = xy
    for char in text:
        glyph = PIXEL_FONT[char]
        for row, bits in enumerate(glyph):
            for col in range(5):
                if bits & (1 << (4 - col)):
                    draw.rectangle(
                        (x + col * scale, y + row * scale,
                         x + (col + 1) * scale - 1, y + (row + 1) * scale - 1),
                        fill=NAVY,
                    )
        x += 6 * scale


def controller(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], stroke: int, color: tuple[int, int, int]) -> None:
    x0, y0, x1, y1 = box
    radius = max(6, (y1 - y0) // 3)
    for inset in range(stroke):
        left, top, right, bottom = x0 + inset, y0 + inset, x1 - inset, y1 - inset
        r = max(1, radius - inset)
        draw.line((left + r, top, right - r, top), fill=color)
        draw.line((left + r, bottom, right - r, bottom), fill=color)
        draw.line((left, top + r, left, bottom - r), fill=color)
        draw.line((right, top + r, right, bottom - r), fill=color)
        draw.arc((left, top, left + r * 2, top + r * 2), 180, 270, fill=color)
        draw.arc((right - r * 2, top, right, top + r * 2), 270, 360, fill=color)
        draw.arc((left, bottom - r * 2, left + r * 2, bottom), 90, 180, fill=color)
        draw.arc((right - r * 2, bottom - r * 2, right, bottom), 0, 90, fill=color)

    center_y = (y0 + y1) // 2
    dpad_x = x0 + (x1 - x0) * 28 // 100
    arm = max(3, (y1 - y0) // 7)
    length = max(9, (y1 - y0) // 3)
    draw.rectangle((dpad_x - arm, center_y - length, dpad_x + arm, center_y + length), fill=color)
    draw.rectangle((dpad_x - length, center_y - arm, dpad_x + length, center_y + arm), fill=color)

    button_x = x0 + (x1 - x0) * 76 // 100
    button_r = max(2, (y1 - y0) // 11)
    gap = button_r * 3
    for dx, dy in ((0, -gap), (gap, 0), (0, gap), (-gap, 0)):
        draw.ellipse((button_x + dx - button_r, center_y + dy - button_r,
                      button_x + dx + button_r, center_y + dy + button_r), outline=color, width=stroke)

    dash_y = center_y + max(4, (y1 - y0) // 8)
    dash_x = x0 + (x1 - x0) * 48 // 100
    draw.line((dash_x - 7, dash_y, dash_x - 2, dash_y), fill=color, width=stroke)
    draw.line((dash_x + 3, dash_y, dash_x + 8, dash_y), fill=color, width=stroke)


def save_indexed(image: Image.Image, name: str) -> None:
    colors = image.getcolors(maxcolors=256)
    indexed = image.quantize(colors=len(colors), method=Image.NONE, dither=Image.NONE)
    indexed.save(ROOT / name, format="PNG", optimize=True)


def make_background() -> None:
    image = Image.new("RGB", (320, 240), CREAM)
    draw = ImageDraw.Draw(image)

    for y in range(72, 232, 16):
        for x in range(8, 320, 16):
            draw.point((x, y), fill=PALE_GRID)

    controller(draw, (150, 104, 306, 198), 5, PALE_BLUE)
    draw.rectangle((284, 181, 291, 188), fill=AMBER)
    draw.line((16, 220, 104, 220), fill=PALE_BLUE, width=3)
    draw.rectangle((16, 216, 21, 224), fill=AMBER)
    save_indexed(image, "background_snes.png")


def make_banner() -> None:
    image = Image.new("RGB", (272, 24), MAGENTA)
    draw = ImageDraw.Draw(image)
    pixel_text(draw, (37, 2), "SNES", scale=2)
    draw.line((36, 20, 238, 20), fill=NAVY, width=2)
    draw.line((238, 20, 250, 12), fill=NAVY, width=2)
    draw.line((250, 12, 263, 12), fill=NAVY, width=2)
    draw.rectangle((264, 9, 270, 15), fill=AMBER)
    draw.rectangle((266, 11, 268, 13), fill=CREAM)
    save_indexed(image, "banner_snes.png")


def make_logo() -> None:
    image = Image.new("RGB", (46, 50), MAGENTA)
    draw = ImageDraw.Draw(image)
    controller(draw, (2, 10, 43, 39), 3, NAVY)
    draw.rectangle((35, 27, 38, 30), fill=AMBER)
    save_indexed(image, "logo_snes.png")


if __name__ == "__main__":
    make_background()
    make_banner()
    make_logo()
    print("Generated SNES background, banner, and logo")
