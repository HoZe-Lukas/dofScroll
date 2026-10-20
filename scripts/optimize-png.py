#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
"""Palette-quantizes PNG screenshots in place (pure Python, standard library only).

User interface screenshots are flat colours plus antialiased text, so 256 colours (median cut, no
dithering) look the same and the file shrinks to a fraction. Input: non-interlaced 8-bit RGB or RGBA
PNG (what Qt writes); other files are left alone. Used by scripts/make-site-screenshots.sh.

Usage: scripts/optimize-png.py file.png [file.png ...]
"""
import struct
import sys
import zlib


def read_chunks(data):
    pos = 8
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        yield typ, data[pos + 8:pos + 8 + n]
        pos += 12 + n


def chunk(typ, body):
    return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF)


def decode(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    idat = b""
    hdr = None
    for typ, body in read_chunks(data):
        if typ == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
    if not hdr:
        return None
    w, h, depth, ctype, _comp, _filt, interlace = hdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        return None
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    rows = []
    pos = 0
    for _ in range(h):
        f = raw[pos]
        cur = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 255
        rows.append(cur)
        prev = cur
    return w, h, bpp, rows


def median_cut(colors, n):
    """colors: {(r, g, b): count}. Returns a list of at most n representative colours."""
    boxes = [list(colors.items())]
    while len(boxes) < n:
        best, bi = -1, -1
        for i, b in enumerate(boxes):
            if len(b) < 2:
                continue
            span = max(max(c[0][k] for c in b) - min(c[0][k] for c in b) for k in range(3))
            score = span * sum(c[1] for c in b) ** 0.5
            if score > best:
                best, bi = score, i
        if bi < 0:
            break
        b = boxes.pop(bi)
        ch = max(range(3), key=lambda k: max(c[0][k] for c in b) - min(c[0][k] for c in b))
        b.sort(key=lambda c: c[0][ch])
        half, acc = sum(c[1] for c in b) / 2, 0
        cut = 1
        for i, c in enumerate(b):
            acc += c[1]
            if acc >= half:
                cut = min(max(i + 1, 1), len(b) - 1)
                break
        boxes += [b[:cut], b[cut:]]
    pal = []
    for b in boxes:
        tot = sum(c[1] for c in b)
        pal.append(tuple(int(round(sum(c[0][k] * c[1] for c in b) / tot)) for k in range(3)))
    return pal


def optimize(path):
    data = open(path, "rb").read()
    img = decode(data)
    if img is None:
        print("optimize-png: skipped %s (not an 8-bit RGB/RGBA PNG)" % path)
        return
    w, h, bpp, rows = img
    counts = {}
    for r in rows:
        for i in range(0, w * bpp, bpp):
            k = (r[i], r[i + 1], r[i + 2])
            counts[k] = counts.get(k, 0) + 1
    pal = list(counts) if len(counts) <= 256 else median_cut(counts, 256)
    cache = {}
    if len(counts) <= 256:
        cache = {c: i for i, c in enumerate(pal)}

    def nearest(c):
        i = cache.get(c)
        if i is None:
            i = min(range(len(pal)), key=lambda j: (pal[j][0] - c[0]) ** 2 + (pal[j][1] - c[1]) ** 2 + (pal[j][2] - c[2]) ** 2)
            cache[c] = i
        return i

    out = bytearray()
    prev = bytearray(w)
    for r in rows:
        cur = bytearray(nearest((r[i], r[i + 1], r[i + 2])) for i in range(0, w * bpp, bpp))
        cands = [(0, cur),
                 (1, bytearray([cur[0]] + [(cur[i] - cur[i - 1]) & 255 for i in range(1, w)])),
                 (2, bytearray((cur[i] - prev[i]) & 255 for i in range(w)))]
        f, body = min(cands, key=lambda c: sum(v if v < 128 else 256 - v for v in c[1]))
        out.append(f)
        out += body
        prev = cur
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", b"".join(bytes(c) for c in pal)) + chunk(b"IDAT", zlib.compress(bytes(out), 9)) + chunk(b"IEND", b"")
    if len(png) < len(data):
        open(path, "wb").write(png)
    print("optimize-png: %s %d -> %d bytes (%d colours)" % (path, len(data), min(len(png), len(data)), len(pal)))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        optimize(p)
