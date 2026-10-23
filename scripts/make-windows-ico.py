#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
"""Draws icons/dofscroll-app.svg (rounded square, ring, two arrows, cap) into packaging/windows/dofscroll.ico.

The icon is embedded in dofscroll.exe and dofscroll-ui.exe (taskbar, Start menu, Explorer). Only needs
Pillow (pip install pillow); rerun after the SVG changes. Usage: python scripts/make-windows-ico.py
"""
import os
from PIL import Image, ImageChops, ImageDraw

S = 1024  # render size; the SVG viewBox is 128 -> factor 8
K = S / 128


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(size, c0, c1, diagonal):
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1)) if diagonal else y / (size - 1)
            px[x, y] = lerp(c0, c1, t)
    return img


def circle(d, cx, cy, r, **kw):
    d.ellipse([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], **kw)


def render():
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # background: rounded square with the diagonal blue gradient
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([4 * K, 4 * K, 124 * K, 124 * K], radius=28 * K, fill=255)
    bg = gradient(S, (0x4C, 0x78, 0xA8), (0x2B, 0x4A, 0x6B), True).convert("RGBA")
    out.paste(bg, (0, 0), mask)
    # dark disc (opacity 0.55)
    disc = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    circle(ImageDraw.Draw(disc), 64, 64, 46, fill=(0x1F, 0x35, 0x50, round(255 * 0.55)))
    out = Image.alpha_composite(out, disc)
    d = ImageDraw.Draw(out)
    # white ring (stroke 5, opacity 0.85)
    ring = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    circle(rd, 64, 64, 43 + 2.5, fill=(255, 255, 255, round(255 * 0.85)))
    circle(rd, 64, 64, 43 - 2.5, fill=(0, 0, 0, 0))
    out = Image.alpha_composite(out, ring)
    d = ImageDraw.Draw(out)
    d.polygon([(64 * K, 17 * K), (82 * K, 38 * K), (46 * K, 38 * K)], fill=(255, 255, 255, 255))
    d.polygon([(64 * K, 111 * K), (82 * K, 90 * K), (46 * K, 90 * K)], fill=(255, 255, 255, 255))
    # cap: vertical gradient disc with a dark outline (stroke 3)
    circle(d, 64, 64, 22 + 1.5, fill=(0x1F, 0x35, 0x50, 255))
    capmask = Image.new("L", (S, S), 0)
    circle(ImageDraw.Draw(capmask), 64, 64, 22 - 1.5, fill=255)
    cap = gradient(S, (0xF4, 0xF7, 0xFB), (0xCF, 0xD8, 0xE6), False).convert("RGBA")
    out.paste(cap, (0, 0), capmask)
    return out


if __name__ == "__main__":
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    dst = os.path.join(root, "packaging", "windows", "dofscroll.ico")
    img = render()
    img.save(dst, format="ICO", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)])
    preview = os.environ.get("ICO_PREVIEW")  # optional: also write a PNG to look at
    if preview:
        img.resize((256, 256), Image.LANCZOS).save(preview, "PNG")
    print("wrote", os.path.normpath(dst))
