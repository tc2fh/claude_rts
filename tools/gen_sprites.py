#!/usr/bin/env python3
"""Generate greybox placeholder sprites (RGBA8 PNG) for claude_rts.

Pure stdlib (zlib + struct); the PNG encoder is written by hand (signature,
IHDR, one IDAT with filter-0 scanlines, IEND). Run:

    python3 tools/gen_sprites.py

Writes 7 PNGs into game/assets/sprites/. These are programmer-art placeholders
to give the prototype readable silhouettes; swap for real art later (T's lane).
The view preloads them by exact filename and size, so those are load-bearing:
hq_* 48x48, worker_*/soldier_*/node 32x32. Light comes from the top-left
(light facets up/left, dark facets down/right) with a 1px near-black outline,
tuned to read against the dark blue-gray ground (0.10, 0.12, 0.16).
"""
import os
import struct
import zlib

OUTDIR = os.path.join(os.path.dirname(__file__), "..", "game", "assets", "sprites")


def shades(r, g, b):
    """(light, mid, dark, outline) RGBA shades from one base colour in [0, 1]."""
    def px8(v):
        return max(0, min(255, int(round(v * 255))))
    light = tuple(px8(v + (1.0 - v) * 0.45) for v in (r, g, b)) + (255,)
    mid = tuple(px8(v) for v in (r, g, b)) + (255,)
    dark = tuple(px8(v * 0.55) for v in (r, g, b)) + (255,)
    line = tuple(px8(v * 0.16) for v in (r, g, b)) + (255,)
    return light, mid, dark, line


BLUE = shades(0.30, 0.70, 1.00)
RED = shades(1.00, 0.40, 0.30)
GOLD = shades(0.95, 0.82, 0.25)


def blank(size):
    return [[(0, 0, 0, 0)] * size for _ in range(size)]


def rect(px, x, y, w, h, col):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[yy][xx] = col


def disc(px, cx, cy, r, col):
    for yy in range(cy - r, cy + r + 1):
        for xx in range(cx - r, cx + r + 1):
            if (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) <= r * r:
                px[yy][xx] = col


def tri(px, a, b, c, col):
    def edge(p, q, x, y):
        return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0])
    area = edge(a, b, c[0], c[1])
    if area == 0:
        return
    for yy in range(min(a[1], b[1], c[1]), max(a[1], b[1], c[1]) + 1):
        for xx in range(min(a[0], b[0], c[0]), max(a[0], b[0], c[0]) + 1):
            w0 = edge(a, b, xx, yy)
            w1 = edge(b, c, xx, yy)
            w2 = edge(c, a, xx, yy)
            if area < 0:
                w0, w1, w2 = -w0, -w1, -w2
            if w0 >= 0 and w1 >= 0 and w2 >= 0:
                px[yy][xx] = col


def outline(px, col):
    """1px outline: paint transparent pixels that touch the silhouette."""
    size = len(px)
    solid = [[p[3] > 0 for p in row] for row in px]
    for y in range(size):
        for x in range(size):
            if solid[y][x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < size and 0 <= ny < size and solid[ny][nx]:
                    px[y][x] = col
                    break


def chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png(name, px):
    os.makedirs(OUTDIR, exist_ok=True)
    path = os.path.join(OUTDIR, name)
    h = len(px)
    w = len(px[0])
    raw = b"".join(
        b"\x00" + b"".join(struct.pack("BBBB", *p) for p in row) for row in px
    )
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # RGBA8, non-interlaced
    blob = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(blob)
    print("wrote {} ({}x{}, {} bytes)".format(path, w, h, len(blob)))


def draw_hq(pal):
    """Command center: main block + dome core, side annex, landing pad."""
    light, mid, dark, line = pal
    px = blank(48)
    rect(px, 28, 33, 16, 11, dark)      # landing pad slab
    rect(px, 30, 35, 12, 7, mid)        # pad deck
    rect(px, 34, 37, 4, 3, light)       # pad marking
    rect(px, 31, 18, 9, 9, mid)         # right annex linking pad
    rect(px, 31, 18, 9, 3, light)
    rect(px, 38, 21, 2, 6, dark)
    rect(px, 2, 24, 5, 12, mid)         # left side block
    rect(px, 2, 24, 5, 3, light)
    rect(px, 2, 33, 5, 3, dark)
    rect(px, 5, 14, 26, 26, mid)        # main body
    rect(px, 5, 14, 26, 6, light)       # roof facet (lit)
    rect(px, 27, 20, 4, 20, dark)       # right facet (shaded)
    rect(px, 5, 36, 22, 4, dark)        # base facet (shaded)
    disc(px, 17, 26, 7, dark)           # dome rim
    disc(px, 16, 25, 5, mid)            # dome core
    disc(px, 14, 23, 2, light)          # dome highlight
    rect(px, 14, 35, 6, 5, dark)        # door
    outline(px, line)
    return px


def draw_worker(pal):
    """Harvester: round body with backpack (back) and tool nub (front, +x)."""
    light, mid, dark, line = pal
    px = blank(32)
    rect(px, 4, 13, 4, 7, dark)         # backpack nub
    disc(px, 17, 17, 9, dark)           # body shadow rim
    disc(px, 16, 16, 9, mid)            # round body
    disc(px, 13, 13, 4, light)          # top-left highlight
    rect(px, 24, 15, 4, 3, dark)        # tool arm
    rect(px, 27, 13, 3, 6, light)       # tool head
    outline(px, line)
    return px


def draw_soldier(pal):
    """Combat unit: angular shouldered body, barrel points right (0 rad = +x)."""
    light, mid, dark, line = pal
    px = blank(32)
    rect(px, 10, 24, 4, 4, dark)        # legs
    rect(px, 16, 24, 4, 4, dark)
    rect(px, 6, 10, 3, 8, mid)          # left shoulder pad
    rect(px, 6, 10, 3, 2, light)
    rect(px, 21, 10, 3, 8, dark)        # right shoulder pad (shaded)
    rect(px, 9, 11, 12, 13, mid)        # torso
    rect(px, 9, 11, 12, 4, light)       # torso top facet
    rect(px, 18, 15, 3, 9, dark)        # torso right facet
    rect(px, 9, 21, 12, 3, dark)        # torso base facet
    rect(px, 12, 6, 6, 5, mid)          # head
    rect(px, 12, 6, 6, 2, light)
    rect(px, 24, 14, 6, 3, dark)        # weapon barrel (+x)
    rect(px, 24, 14, 6, 1, mid)         # barrel top edge (lit)
    outline(px, line)
    return px


def draw_node(pal):
    """Mineral node: faceted crystal cluster, 4 shards on a rock strip."""
    light, mid, dark, line = pal
    px = blank(32)
    tri(px, (9, 9), (14, 26), (4, 26), dark)      # back-left shard
    tri(px, (9, 9), (9, 26), (5, 26), mid)
    tri(px, (25, 11), (29, 26), (20, 26), dark)   # back-right shard
    tri(px, (16, 3), (22, 27), (10, 27), mid)     # central shard
    tri(px, (16, 3), (16, 27), (11, 27), light)   # lit left facet
    tri(px, (22, 16), (26, 27), (18, 27), mid)    # front-right shard
    tri(px, (22, 16), (22, 27), (19, 27), light)
    rect(px, 5, 26, 23, 3, dark)                  # rock strip
    outline(px, line)
    return px


def main():
    # Faction buildings/units (blue vs red must read apart at a glance).
    write_png("hq_blue.png", draw_hq(BLUE))
    write_png("hq_red.png", draw_hq(RED))
    write_png("worker_blue.png", draw_worker(BLUE))
    write_png("worker_red.png", draw_worker(RED))
    write_png("soldier_blue.png", draw_soldier(BLUE))
    write_png("soldier_red.png", draw_soldier(RED))
    # Neutral resource.
    write_png("node.png", draw_node(GOLD))


if __name__ == "__main__":
    main()
