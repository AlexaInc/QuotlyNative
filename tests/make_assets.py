#!/usr/bin/env python3
"""Generate offline test assets: a pink 'premium star' emoji PNG and a green
gradient photo PNG (both with correct PNG encoding, no external deps)."""
import math, os, struct, zlib, sys

def write_png(path, w, h, get_pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter none
        for x in range(w):
            r, g, b, a = get_pixel(x, y)
            raw += bytes((r, g, b, a))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

def star_pixel(x, y, w=64, h=64):
    cx, cy = w / 2, h / 2 + 2
    ro, ri = 30.0, 13.5
    dx, dy = x + 0.5 - cx, y + 0.5 - cy
    ang = math.atan2(dy, dx) + math.pi / 2
    seg = math.pi / 5
    k = ang % (2 * seg)
    t = abs(k - seg)
    r_edge = ri + (ro - ri) * (math.cos(t) * seg / math.cos(0) ) / ( math.cos(t - 0) * 0 + 1)  # placeholder
    # proper star radius at angle: use polygon test instead
    pts = []
    for i in range(10):
        rr = ro if i % 2 == 0 else ri
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    inside = False
    j = 9
    for i in range(10):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y + 0.5) != (yj > y + 0.5):
            xin = xi + (y + 0.5 - yi) / (yj - yi) * (xj - xi)
            if x + 0.5 < xin:
                inside = not inside
        j = i
    if not inside:
        return (0, 0, 0, 0)
    # pink gradient fill
    f = max(0.0, min(1.0, 1.0 - (math.hypot(dx, dy) / ro) * 0.55))
    r = int(244 * f + 60 * (1 - f))
    g = int(145 * f + 40 * (1 - f))
    b = int(212 * f + 90 * (1 - f))
    return (r, g, b, 255)

def green_pixel(x, y, w=240, h=320):
    cx, cy = w / 2, h / 2
    d = math.hypot(x - cx, (y - cy) * 0.8) / math.hypot(cx, cy * 0.8)
    v = max(0.0, 1.0 - d)
    r = int(10 + 30 * v)
    g = int(60 + 120 * v)
    b = int(20 + 40 * v)
    return (r, g, b, 255)

def bow_pixel(x, y, w=64, h=64):
    cx, cy = w / 2, h / 2
    # two triangle lobes + centre knot = simple pink bow
    in_left  = abs(x - (cx - 14)) + abs(y - cy) * 1.4 <= 16
    in_right = abs(x - (cx + 14)) + abs(y - cy) * 1.4 <= 16
    in_knot  = (x - cx) ** 2 + (y - cy) ** 2 <= 36
    if not (in_left or in_right or in_knot):
        return (0, 0, 0, 0)
    if in_knot:
        return (236, 64, 122, 255)
    return (244, 143, 177, 255)

out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out_dir, exist_ok=True)
os.makedirs(os.path.join(out_dir, "emoji_cache"), exist_ok=True)
write_png(os.path.join(out_dir, "emoji_cache", "emoji_5210956306952758910.png"), 64, 64, star_pixel)
write_png(os.path.join(out_dir, "emoji_cache", "emoji_5233605022419270727.png"), 64, 64, bow_pixel)
write_png(os.path.join(out_dir, "green_photo.png"), 240, 320, green_pixel)
print("assets written to", out_dir)
