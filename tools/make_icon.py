#!/usr/bin/env python3
"""Draws the app icon (a wheel with a lightning bolt) at the four sizes Harbour allows."""
import math
from PIL import Image, ImageDraw

SCALE = 8


def draw(size):
    s = size * SCALE
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # background: a rounded square in a dark teal, lighter towards the top
    bg = Image.new("RGBA", (s, s))
    bd = ImageDraw.Draw(bg)
    for y in range(s):
        t = y / s
        bd.line([(0, y), (s, y)], fill=(int(24 + 20 * (1 - t)), int(96 + 50 * (1 - t)), int(118 + 40 * (1 - t)), 255))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.22), fill=255)
    img.paste(bg, (0, 0), mask)

    cx, cy, r = s / 2, s / 2 + s * 0.02, s * 0.33
    white = (255, 255, 255, 255)
    # wheel: rim and spokes
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=white, width=int(s * 0.045))
    for k in range(8):
        a = k * math.pi / 4
        d.line([(cx, cy), (cx + r * 0.92 * math.cos(a), cy + r * 0.92 * math.sin(a))],
               fill=(255, 255, 255, 150), width=max(1, int(s * 0.012)))
    # hub
    hub = s * 0.035
    d.ellipse([cx - hub, cy - hub, cx + hub, cy + hub], fill=white)
    # the bolt, in the Mahle-ish yellow, over the wheel
    bolt = [(0.56, 0.20), (0.38, 0.55), (0.49, 0.55), (0.43, 0.82), (0.66, 0.44), (0.54, 0.44), (0.62, 0.20)]
    d.polygon([(x * s, y * s) for x, y in bolt], fill=(255, 205, 40, 255), outline=(60, 40, 0, 255))
    return img.resize((size, size), Image.LANCZOS)


for n in (86, 108, 128, 172):
    draw(n).save(f"icons/{n}x{n}/harbour-mahlebike.png")
print("icons written")
