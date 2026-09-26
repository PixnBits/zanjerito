#!/usr/bin/env python3
"""Rasterize Zanjerito PWA icons with Pillow (no rsvg/ImageMagick).

Draws the same geometry as internal/api/ui/icons/icon.svg (and the
maskable variant) at high resolution, then downsamples with LANCZOS.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "internal" / "api" / "ui" / "icons"

SAND = (0xE8, 0xDC, 0xC8, 255)
CREAM = (0xF7, 0xF1, 0xE6, 255)
TEAL = (0x2A, 0x7A, 0x74, 255)
TERRACOTTA = (0xC4, 0x5C, 0x3E, 255)
# Cream at 40% over terracotta, pre-blended (ImageDraw replaces pixels, it does not alpha-blend).
CREAM_GLINT = (0xD8, 0x98, 0x81, 255)

# Design space matches the SVGs (viewBox 0 0 512 512).
DESIGN = 512.0
SUPER = 2048  # 4× the 512 design, then LANCZOS down


def cubic(p0, p1, p2, p3, n=48):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1.0 - t
        x = u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0]
        y = u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    return pts


def xf(x, y, size, mark_scale=1.0, pivot=(256.0, 270.0)):
    """Map design coords → pixel coords, optionally scaling the mark around pivot."""
    s = size / DESIGN
    px, py = x * s, y * s
    cx, cy = pivot[0] * s, pivot[1] * s
    return (cx + (px - cx) * mark_scale, cy + (py - cy) * mark_scale)


def clockwise_arc(cx, cy, r, a0, a1, n=64):
    """Clockwise arc from a0 to a1 (radians, y-down)."""
    while a1 > a0:
        a1 -= 2 * math.pi
    pts = []
    for i in range(n + 1):
        a = a0 + (a1 - a0) * (i / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def drop_polygon(size, mark_scale=1.0, pivot=(256.0, 270.0)):
    """Same path as icon.svg: cubic sides + circular bulb (r=72 at 256,300)."""
    t = lambda x, y: xf(x, y, size, mark_scale, pivot)
    left = cubic(t(256, 140), t(256, 198), t(184, 248), t(184, 300))
    right = cubic(t(328, 300), t(328, 248), t(256, 198), t(256, 140))
    cx, cy = t(256, 300)
    r = 72.0 * (size / DESIGN) * mark_scale
    a_left = math.atan2(t(184, 300)[1] - cy, t(184, 300)[0] - cx)
    a_right = math.atan2(t(328, 300)[1] - cy, t(328, 300)[0] - cx)
    bulb = clockwise_arc(cx, cy, r, a_left, a_right)
    return left[:-1] + bulb + right[1:]


def draw_mark(draw: ImageDraw.ImageDraw, size: int, mark_scale: float = 1.0, pivot=(256.0, 270.0)):
    drop = drop_polygon(size, mark_scale, pivot)
    draw.polygon(drop, fill=TEAL)
    # Channel + cream glint (same rects as the SVG).
    x0, y0 = xf(158, 368, size, mark_scale, pivot)
    x1, y1 = xf(158 + 196, 368 + 32, size, mark_scale, pivot)
    rad = 16.0 * (size / DESIGN) * mark_scale
    draw.rounded_rectangle([x0, y0, x1, y1], radius=rad, fill=TERRACOTTA)
    gx0, gy0 = xf(178, 378, size, mark_scale, pivot)
    gx1, gy1 = xf(178 + 156, 378 + 8, size, mark_scale, pivot)
    draw.rounded_rectangle([gx0, gy0, gx1, gy1], radius=4.0 * (size / DESIGN) * mark_scale, fill=CREAM_GLINT)


def render_regular(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size / DESIGN
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=108 * s, fill=SAND)
    d.rounded_rectangle([36 * s, 36 * s, (36 + 440) * s - 1, (36 + 440) * s - 1], radius=88 * s, fill=CREAM)
    draw_mark(d, size, mark_scale=1.0)
    return img


def render_maskable(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), SAND)
    d = ImageDraw.Draw(img)
    draw_mark(d, size, mark_scale=1.05, pivot=(256.0, 270.0))
    return img


def render_apple(size: int) -> Image.Image:
    """Full-bleed opaque sand (no alpha); iOS applies its own mask."""
    img = Image.new("RGB", (size, size), SAND[:3])
    rgba = Image.new("RGBA", (size, size), SAND)
    d = ImageDraw.Draw(rgba)
    s = size / DESIGN
    d.rounded_rectangle([36 * s, 36 * s, (36 + 440) * s - 1, (36 + 440) * s - 1], radius=88 * s, fill=CREAM)
    draw_mark(d, size, mark_scale=1.0)
    return Image.alpha_composite(Image.new("RGBA", (size, size), SAND), rgba).convert("RGB")


def down(img: Image.Image, size: int) -> Image.Image:
    if img.size == (size, size):
        return img
    return img.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    regular = render_regular(SUPER)
    maskable = render_maskable(SUPER)
    apple = render_apple(SUPER)
    down(regular, 192).save(OUT / "icon-192.png", "PNG", optimize=True)
    down(regular, 512).save(OUT / "icon-512.png", "PNG", optimize=True)
    down(maskable, 512).save(OUT / "icon-512-maskable.png", "PNG", optimize=True)
    down(apple, 180).save(OUT / "apple-touch-icon.png", "PNG", optimize=True)
    print("wrote", OUT / "icon-192.png")
    print("wrote", OUT / "icon-512.png")
    print("wrote", OUT / "icon-512-maskable.png")
    print("wrote", OUT / "apple-touch-icon.png")


if __name__ == "__main__":
    main()
