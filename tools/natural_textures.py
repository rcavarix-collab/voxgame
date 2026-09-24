#!/usr/bin/env python3
"""Generates assets/textures/natural.vtex: the natural-material starter set.

Each texture is 16x16 pixel art built from palettes taken from the art
batch (snow, sandstone, cracked earth, magma rock, bark, the olive "flesh"
colours shifted into moss), following the three rules in the texture brief:

  1. No regular dither: backgrounds are irregular clumps of 2-4 pixels,
     from smooth value noise, never a repeating cycle of keys.
  2. No details in fixed spots: sparkles, specks and pebbles are scattered
     by a seeded hash, so a wall of them reads as one surface, not a grid.
  3. Lines wrap: every noise field and crack network is computed on a
     16x16 torus, so anything that leaves one edge re-enters the opposite
     one and tiles meet with no seam.

Deterministic (seeded), so rerunning it reproduces the file exactly.
Tweak a material here and rerun:  python3 tools/natural_textures.py
(or hand-edit the .vtex and stop regenerating -- it's plain text).
"""

import math
import os
import random

N = 16


# ---- tileable building blocks ------------------------------------------------

def value_noise(seed, cells):
    """Smooth noise on the 16x16 torus with a `cells` x `cells` lattice."""
    rng = random.Random(seed)
    lat = [[rng.random() for _ in range(cells)] for _ in range(cells)]
    step = N / cells

    def f(x, y):
        gx, gy = x / step, y / step
        x0, y0 = int(math.floor(gx)), int(math.floor(gy))
        tx, ty = gx - x0, gy - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        v = lambda i, j: lat[j % cells][i % cells]
        a = v(x0, y0) + (v(x0 + 1, y0) - v(x0, y0)) * tx
        b = v(x0, y0 + 1) + (v(x0 + 1, y0 + 1) - v(x0, y0 + 1)) * tx
        return a + (b - a) * ty
    return f


def fbm(seed, octaves=((4, 0.6), (8, 0.4))):
    parts = [(value_noise(seed + i * 101, c), w) for i, (c, w) in enumerate(octaves)]
    total = sum(w for _, w in parts)
    return lambda x, y: sum(f(x + 0.5, y + 0.5) * w for f, w in parts) / total


def torus_d2(ax, ay, bx, by):
    dx = abs(ax - bx); dx = min(dx, N - dx)
    dy = abs(ay - by); dy = min(dy, N - dy)
    return dx * dx + dy * dy


def voronoi_edges(seed, count, jitter_min=2.5):
    """Distance (in pixels) from each pixel centre to the nearest cell
    border of a Voronoi pattern on the torus: small = on a crack."""
    rng = random.Random(seed)
    pts = []
    while len(pts) < count:
        p = (rng.random() * N, rng.random() * N)
        if all(torus_d2(p[0], p[1], q[0], q[1]) > jitter_min ** 2 for q in pts):
            pts.append(p)

    def edge(x, y):
        cx, cy = x + 0.5, y + 0.5
        d = sorted(math.sqrt(torus_d2(cx, cy, px, py)) for px, py in pts)
        return (d[1] - d[0]) * 0.5
    return edge


def scatter(seed, x, y):
    """A per-pixel random number, independent of its neighbours."""
    return random.Random(seed * 1000003 + y * 16 + x).random()


def pick(bands, v):
    """bands: [(threshold, key), ...] ascending; the first threshold v is under."""
    for t, k in bands:
        if v < t:
            return k
    return bands[-1][1]


# ---- the materials ------------------------------------------------------------

def snow():
    n = fbm(11, ((4, 0.7), (8, 0.3)))
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.34, "d"), (1.1, "a")], n(x, y))   # soft drifts of shade
            r = scatter(12, x, y)
            if r < 0.035: k = "c"                          # sparkle
            elif r > 0.975: k = "b"                        # a crystal's shadow
            row += k
        rows.append(row)
    return "snow", "Snow: near-flat, soft drifts, a few sparkles", \
        [("a", "dfe6ea"), ("b", "c3ced6"), ("c", "f6f8f8"), ("d", "d3dce2")], rows


def sand():
    n = fbm(21)
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.36, "b"), (0.66, "a"), (1.1, "e")], n(x, y))
            r = scatter(22, x, y)
            if r < 0.06: k = "c"
            elif r > 0.96: k = "e" if k != "e" else "a"
            row += k
        rows.append(row)
    return "sand", "Sand: warm grain in soft ripples of tone", \
        [("a", "e0b077"), ("b", "d0a068"), ("c", "b98850"), ("e", "ecc891")], rows


def sandstone_side():
    # One set of strata per tile, uneven thicknesses, each boundary
    # wobbling by up to a pixel along x (a periodic wobble, so it wraps).
    layers = [("a", 4), ("d", 1), ("b", 3), ("c", 1), ("a", 3), ("e", 1), ("b", 3)]   # sums to 16
    wob = [value_noise(31 + i, 2) for i in range(len(layers))]   # long, gentle waves
    n = fbm(32)
    rows = [[" "] * N for _ in range(N)]
    for x in range(N):
        tops, y = [], 0
        for i, (_, h) in enumerate(layers):
            tops.append(y + (1 if wob[i](x + 0.5, 0.5) > 0.62 else 0) - (1 if wob[i](x + 0.5, 0.5) < 0.30 else 0))
            y += h
        for y in range(N):
            # This pixel's layer: the nearest boundary at or above it (wrapping).
            best, bestd = 0, N + 1
            for i, t in enumerate(tops):
                d = (y - t) % N
                if d < bestd:
                    best, bestd = i, d
            k = layers[best][0]
            if k in "ab" and n(x, y) > 0.70: k = "e" if k == "a" else "f"
            if scatter(33, x, y) < 0.04 and k in "ab": k = "f"
            rows[y][x] = k
    return "sandstone_layered", "Sandstone, side: one set of uneven strata per tile, boundaries wobbling a pixel", \
        [("a", "e0b077"), ("b", "c89860"), ("c", "a67c4a"), ("d", "8f6a3e"), ("e", "ecc891"), ("f", "b98850")], \
        ["".join(r) for r in rows]


def sandstone_top():
    n = fbm(41, ((4, 0.8), (8, 0.2)))
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.40, "b"), (1.1, "a")], n(x, y))
            if scatter(42, x, y) < 0.03: k = "f"
            row += k
        rows.append(row)
    return "sandstone_top", "Sandstone, top and bottom: smooth, faintly mottled", \
        [("a", "dcae78"), ("b", "d0a26c"), ("f", "b98850")], rows


def cracked_earth():
    n = fbm(51)
    edge = voronoi_edges(52, 6, 4.5)
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.40, "c"), (0.68, "a"), (1.1, "b")], n(x, y))
            e = edge(x, y)
            if e < 0.45: k = "e"            # the crack itself
            elif e < 0.95: k = "d" if scatter(53, x, y) < 0.55 else k   # its broken rim
            elif scatter(54, x, y) < 0.03: k = "f"
            row += k
        rows.append(row)
    return "cracked_earth", "Cracked earth: one connected crack network that wraps every edge", \
        [("a", "b0673a"), ("b", "c47a48"), ("c", "96552f"), ("d", "6e4428"), ("e", "3a2418"), ("f", "d9a568")], rows


def clay():
    n = fbm(61, ((4, 0.7), (8, 0.3)))
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.38, "c"), (0.70, "a"), (1.1, "b")], n(x, y))
            if scatter(62, x, y) < 0.04: k = "d"
            row += k
        rows.append(row)
    return "clay", "Clay: the cracked-earth colours, smooth and unbroken", \
        [("a", "b0673a"), ("b", "bf7446"), ("c", "9e5a32"), ("d", "8a4e2c")], rows


def basalt():
    n = fbm(71)
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.34, "c"), (0.64, "a"), (1.1, "b")], n(x, y))
            r = scatter(72, x, y)
            if r < 0.04: k = "g"
            elif r > 0.97: k = "c"
            row += k
        rows.append(row)
    return "basalt", "Basalt: the magma rock's stone, without the veins", \
        [("a", "2c2729"), ("b", "37302f"), ("c", "211d1e"), ("g", "4a4244")], rows


def magma_rock():
    n = fbm(81)
    edge = voronoi_edges(82, 4, 5.5)
    live = fbm(83, ((4, 1.0),))       # which stretches of the network run hot
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.34, "c"), (0.64, "a"), (1.1, "b")], n(x, y))
            e, hot = edge(x, y), live(x, y) > 0.38
            if hot and e < 0.30: k = "e"          # molten core
            elif hot and e < 0.70: k = "d"        # glowing rim
            elif hot and e < 1.10 and scatter(84, x, y) < 0.4: k = "f"   # cooling crust
            row += k
        rows.append(row)
    return "magma_rock", "Magma rock: dark stone split by lava veins that wrap the edges", \
        [("a", "2a2527"), ("b", "342d2f"), ("c", "1e1b1c"), ("d", "c8541e"), ("e", "f08c34"), ("f", "7a2a10")], rows


def log_bark():
    # Long vertical furrows: depth varies across x (periodic) and meanders
    # slowly down the tile, so ridges run the full height and wrap.
    across = value_noise(91, 8)
    meander = value_noise(92, 2)
    fine = fbm(93, ((8, 1.0),))
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            u = x + (meander(0.5, y + 0.5) - 0.5) * 3.0
            v = across(u % N, 0.5) * 0.75 + fine(x, y) * 0.25
            k = pick([(0.30, "e"), (0.42, "f"), (0.62, "b"), (0.80, "a"), (1.1, "c")], v)
            if k in "ab" and scatter(94, x, y) < 0.03: k = "g"
            row += k
        rows.append(row)
    return "log_bark", "Log, sides: long meandering vertical furrows, no bands", \
        [("a", "6b5240"), ("b", "5a4433"), ("c", "7d6248"), ("e", "33261c"), ("f", "4a3628"), ("g", "2c2018")], rows


def log_top():
    # A cut end: growth rings round a slightly off-centre pith, bark rim.
    # Not meant to tile (each log end is its own block face).
    wob = value_noise(101, 4)
    rows = []
    cx, cy = 7.6, 8.3
    for y in range(N):
        row = ""
        for x in range(N):
            r = math.hypot(x + 0.5 - cx, y + 0.5 - cy) + (wob(x + 0.5, y + 0.5) - 0.5) * 0.9
            if x in (0, N - 1) or y in (0, N - 1): k = "g" if scatter(102, x, y) < 0.4 else "e"   # bark rim
            elif r < 1.2: k = "c"                                                                # pith
            else: k = "a" if int(r * 0.9) % 2 == 0 else "b"                                      # rings
            row += k
        rows.append(row)
    return "log_top", "Log, ends: growth rings round the pith, a bark rim", \
        [("a", "c29664"), ("b", "a87c4e"), ("c", "8a6440"), ("e", "4a3628"), ("g", "33261c")], rows


def moss():
    n = fbm(111)
    rows = []
    for y in range(N):
        row = ""
        for x in range(N):
            k = pick([(0.30, "b"), (0.58, "a"), (0.82, "c"), (1.1, "e")], n(x, y))
            r = scatter(112, x, y)
            if r < 0.05: k = "d"                  # a gap to the soil
            elif r > 0.965: k = "e"               # a lit tuft
            row += k
        rows.append(row)
    return "moss", "Moss: the olive of the corrupted-flesh study, shifted greener into soft cushions", \
        [("a", "4e5a2e"), ("b", "3f4a26"), ("c", "5f6a34"), ("d", "2a2a18"), ("e", "74803e")], rows


MATERIALS = [snow, sand, sandstone_side, sandstone_top, cracked_earth, clay, basalt, magma_rock, log_bark, log_top, moss]
BLOCKS = [  # block name, faces
    ("snow", [("all", "snow")]),
    ("sand", [("all", "sand")]),
    ("sandstone", [("side", "sandstone_layered"), ("top", "sandstone_top"), ("bottom", "sandstone_top")]),
    ("cracked_earth", [("all", "cracked_earth")]),
    ("clay", [("all", "clay")]),
    ("basalt", [("all", "basalt")]),
    ("magma_rock", [("all", "magma_rock")]),
    ("log", [("side", "log_bark"), ("top", "log_top"), ("bottom", "log_top")]),
    ("moss", [("all", "moss")]),
]


def main():
    out = ["# natural.vtex -- the natural-material starter set.",
           "# Generated by tools/natural_textures.py from the art batch's palettes;",
           "# rerun it after tweaking a material, or hand-edit here and stop regenerating.",
           ""]
    for make in MATERIALS:
        name, note, palette, rows = make()
        assert len(rows) == N and all(len(r) == N for r in rows), name
        keys = {k for k, _ in palette}
        assert all(c in keys for r in rows for c in r), name
        out += ["# " + note, "texture " + name, "size 16", "palette"]
        out += ["  %s %s" % (k, v) for k, v in palette]
        out += ["pixels"] + ["  " + r for r in rows] + ["end", ""]
    for block, faces in BLOCKS:
        out += ["block " + block] + ["  %s %s" % f for f in faces] + ["end", ""]
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "textures", "natural.vtex")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
