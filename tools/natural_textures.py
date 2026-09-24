#!/usr/bin/env python3
"""Generates assets/textures/natural.vtex: the natural-material set.

Every material is a set of continuous fields over one tile -- colour,
height, shine, glow -- defined in *tile units* (0..16 across), so the same
material renders at any resolution: 16, 32 or 64 pixels per block (64 is
what the game uses; DESIGN.md 4.3 / 4.13). Height becomes the normal map,
shine a sun glint, glow what lights up by itself.

Palettes and material ideas come from the art batches (DESIGN.md 4.3);
the drawing follows the texture brief's rules:
  1. No regular dither: surfaces are value noise and clumps, never a
     repeating cycle.
  2. No details in fixed spots: pebbles, flowers, specks and leaves are
     scattered by a seeded hash.
  3. Lines wrap: every field is computed on a 16x16 torus, so cracks,
     strata, grain and ripples continue across block edges.
Colours are quantised to short ramps built from each palette, so the
result stays painterly rather than photographic.

  python3 tools/natural_textures.py                 # the game's set, 64 px
  python3 tools/natural_textures.py --size 16 --out /tmp/n16.vtex
  python3 tools/natural_textures.py --flat          # no height/shine/glow maps

Deterministic (seeded): rerunning reproduces the file exactly.
"""

import argparse
import math
import os
import random

T = 16.0   # tile units per block


# ---- tileable fields ------------------------------------------------------------

def lattice_noise(seed, cells, cells_y=None):
    """Smooth value noise repeating every tile: `cells` lattice cells across
    and `cells_y` down (default the same). Stretch a pattern by giving the
    two different counts -- never by scaling u or v, which breaks the wrap."""
    cx, cy = cells, cells_y or cells
    rng = random.Random(seed)
    lat = [[rng.random() for _ in range(cx)] for _ in range(cy)]
    sx, sy = T / cx, T / cy

    def f(u, v):
        gx, gy = u / sx, v / sy
        x0, y0 = math.floor(gx), math.floor(gy)
        tx, ty = gx - x0, gy - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a0 = lat[y0 % cy][x0 % cx]; a1 = lat[y0 % cy][(x0 + 1) % cx]
        b0 = lat[(y0 + 1) % cy][x0 % cx]; b1 = lat[(y0 + 1) % cy][(x0 + 1) % cx]
        a = a0 + (a1 - a0) * tx
        b = b0 + (b1 - b0) * tx
        return a + (b - a) * ty
    return f


def fbm(seed, cells=(4, 8, 16, 32), weights=None, aspect=1.0):
    """Octaves summed; the finest only show at higher resolutions. `aspect`
    multiplies the lattice count down the tile (4 = streaks four times
    finer across than along u)."""
    weights = weights or [0.5 ** i for i in range(len(cells))]
    fs = [lattice_noise(seed + 97 * i, max(1, int(c)), max(1, int(round(c * aspect)))) for i, c in enumerate(cells)]
    total = sum(weights)
    return lambda u, v: sum(f(u, v) * w for f, w in zip(fs, weights)) / total


def wrapd(a, b):
    d = abs(a - b) % T
    return min(d, T - d)


def warp(seed, amount, cells=4):
    """Wiggles coordinates by up to `amount` tile units (tileable), so
    straight Voronoi borders become wandering cracks and veins."""
    wx, wy = lattice_noise(seed, cells), lattice_noise(seed + 1, cells)
    return lambda u, v: ((u + (wx(u, v) - 0.5) * 2 * amount) % T, (v + (wy(u, v) - 0.5) * 2 * amount) % T)


def voronoi(seed, count, min_sep=2.0):
    """f(u, v) -> (d1, d2, index): distance to the nearest and second-nearest
    site on the torus; (d2 - d1) / 2 is the distance to the cell border."""
    rng = random.Random(seed)
    pts, tries = [], 0
    while len(pts) < count and tries < 5000:
        tries += 1
        p = (rng.random() * T, rng.random() * T)
        if all(math.hypot(wrapd(p[0], q[0]), wrapd(p[1], q[1])) >= min_sep for q in pts):
            pts.append(p)

    def f(u, v):
        b0, b1, bi = 1e9, 1e9, -1
        for i, (px, py) in enumerate(pts):
            d = math.hypot(wrapd(u, px), wrapd(v, py))
            if d < b0: b0, b1, bi = d, b0, i
            elif d < b1: b1 = d
        return b0, b1, bi
    return f


def scatter(seed, per_cell=1.0, radius=0.5, cell=2.0, squash=(1.0, 1.0)):
    """Scattered round features: f(u, v) -> (strength 0..1 inside the
    nearest one, its random id). A jittered grid of `cell`-unit cells, each
    holding a feature with probability `per_cell`; wraps."""
    n = max(1, int(round(T / cell)))
    cell = T / n                     # snapped so a whole number of cells spans the tile: it wraps
    cache = {}

    def feat(ci, cj):
        key = (ci % n, cj % n)
        if key not in cache:
            r = random.Random(seed * 7919 + key[0] * 131 + key[1])
            cache[key] = None if r.random() >= per_cell else (
                r.uniform(0.15, 0.85), r.uniform(0.15, 0.85), radius * r.uniform(0.7, 1.3), r.random())
        f = cache[key]
        return None if f is None else ((ci + f[0]) * cell, (cj + f[1]) * cell, f[2], f[3])

    def f(u, v):
        ci, cj = int(math.floor(u / cell)), int(math.floor(v / cell))
        best, bid = 0.0, -1.0
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                ft = feat(ci + di, cj + dj)
                if not ft: continue
                fx, fy, rr, rid = ft
                d = math.hypot(wrapd(u, fx) / squash[0], wrapd(v, fy) / squash[1])
                s = 1.0 - d / rr
                if s > best: best, bid = s, rid
        return best, bid
    return f


def hexrgb(h):
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def ramp(colors, t, steps=None):
    """Colour along hex anchors at t in 0..1, quantised to `steps` tones
    (default: 3 between each pair of anchors) -- a painterly palette."""
    t = min(max(t, 0.0), 1.0)
    if len(colors) == 1: return hexrgb(colors[0])
    steps = steps or (len(colors) - 1) * 3 + 1
    t = round(t * (steps - 1)) / (steps - 1)
    x = t * (len(colors) - 1)
    i = min(int(x), len(colors) - 2)
    f = x - i
    a, b = hexrgb(colors[i]), hexrgb(colors[i + 1])
    return tuple(int(round(a[k] + (b[k] - a[k]) * f)) for k in range(3))


def sstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3 - 2 * t)


# ---- the materials ----------------------------------------------------------------
# Each returns f(u, v) -> (rgb, alpha 0..1, height 0..1, shine 0..1, glow 0..1).

def m_stone():
    n, big = fbm(101), fbm(102, (2, 4))
    cr = voronoi(103, 5, 5.0)
    speck = scatter(104, 0.6, 0.35, 2.0)
    def f(u, v):
        t = 0.55 * n(u, v) + 0.45 * big(u, v)
        d1, d2, _ = cr(u, v)
        crack = sstep(0.35, 0.0, (d2 - d1) * 0.5) * sstep(0.3, 0.6, big(u + 3, v + 5))
        s, _ = speck(u, v)
        c = ramp(["5e5f64", "7a7b80", "8f9095", "a4a5aa"], t - 0.35 * crack - (0.15 if s > 0.3 else 0))
        return c, 1, 0.55 + 0.35 * t - 0.5 * crack, 0.05, 0
    return f


def m_dirt():
    n = fbm(111)
    peb = scatter(112, 0.45, 0.55, 2.3)
    grit = scatter(113, 0.35, 0.25, 1.4)
    def f(u, v):
        t = n(u, v)
        c = ramp(["5a3e28", "76523a", "8e6448", "a47a58"], t)
        h = 0.35 + 0.35 * t
        p, pid = peb(u, v)
        if p > 0:
            c = ramp(["6e6258", "8a7e70", "a09482"], pid * 0.6 + p * 0.4)
            h = 0.6 + 0.35 * math.sqrt(p)
        g, _ = grit(u, v)
        if g > 0.2 and p <= 0: c = ramp(["a88a68", "c0a080"], g)
        return c, 1, h, 0, 0
    return f


def m_wood():
    # Four planks per tile, each its own tone; grain runs along them and
    # wraps; plank ends staggered; the gaps between planks are deep.
    tone = [random.Random(120 + i).uniform(-0.12, 0.12) for i in range(4)]
    joint = [(i * 7 + 3) % 16 + 0.5 for i in range(4)]
    grain = fbm(121, (1, 2, 4, 8), [1, 0.6, 0.3, 0.15], aspect=8)   # long along the plank, fine across
    knots = scatter(122, 0.25, 0.7, 4.0, (1.6, 0.8))
    def f(u, v):
        p = int(v // 4) % 4
        y = v - p * 4
        gap = sstep(0.35, 0.0, min(y, 4 - y))
        end = sstep(0.35, 0.0, wrapd(u, joint[p]))
        g = grain(u, (v + p * 3.7) % T)
        k, _ = knots(u, v)
        t = 0.5 + tone[p] + (g - 0.5) * 0.7 - 0.35 * k
        c = ramp(["6a4424", "8c6034", "a87a48", "c09460"], t - 0.5 * max(gap, end))
        return c, 1, 0.75 + 0.15 * g - 0.7 * max(gap, end) - 0.2 * k, 0.08, 0
    return f


def m_snow():
    n = fbm(131, (2, 4, 8))
    sp = scatter(132, 0.35, 0.25, 1.5)
    def f(u, v):
        t = n(u, v)
        s, _ = sp(u, v)
        c = ramp(["c3ced6", "d3dce2", "dfe6ea", "eef2f4"], 0.35 + 0.65 * t + 0.4 * s)
        return c, 1, 0.4 + 0.5 * t, 0.15 + 0.85 * (s > 0.3), 0
    return f


def m_sand():
    n = fbm(141)
    rip = lattice_noise(142, 4)
    sp = scatter(143, 0.5, 0.3, 1.6)
    def f(u, v):
        t = n(u, v)
        w = math.sin((v + 1.6 * rip(u, v)) * 2 * math.pi / 4.0) * 0.5 + 0.5    # soft ripples, 4 per tile
        s, _ = sp(u, v)
        c = ramp(["b98850", "d0a068", "e0b077", "ecc891"], 0.25 + 0.55 * t + 0.2 * w - 0.4 * (s > 0.3))
        return c, 1, 0.35 + 0.25 * w + 0.15 * t, 0.03, 0
    return f


def m_sandstone_side():
    layers = [("e0b077", 4, 0.8), ("8f6a3e", 1, 0.35), ("c89860", 3, 0.65), ("a67c4a", 1, 0.45),
              ("e0b077", 3, 0.75), ("ecc891", 1, 0.9), ("c89860", 3, 0.6)]
    wave = lattice_noise(150, 2)                                   # all strata share one gentle wave...
    wob = [lattice_noise(151 + i, 4) for i in range(len(layers))]  # ...each varying a little on its own
    n = fbm(158)
    tops, y = [], 0
    for _, h, _ in layers:
        tops.append(y); y += h
    def f(u, v):
        best, bd = 0, 99
        for i, t0 in enumerate(tops):
            d = (v - (t0 + (wave(u, 0.5) - 0.5) * 1.6 + (wob[i](u, 0.5) - 0.5) * 0.5)) % T
            if d < bd: best, bd = i, d
        col, _, hh = layers[best]
        t = n(u, v)
        c = tuple(int(min(255, max(0, ch * (0.9 + 0.2 * t)))) for ch in hexrgb(col))
        c = tuple(int(round(ch / 6.0) * 6) for ch in c)   # a few tones per layer
        return c, 1, hh * (0.85 + 0.15 * t) - 0.2 * sstep(0.4, 0.0, bd), 0, 0
    return f


def m_sandstone_top():
    n = fbm(161, (2, 4, 8, 16))
    def f(u, v):
        t = n(u, v)
        return ramp(["c89860", "d0a26c", "dcae78", "e6bc88"], t), 1, 0.4 + 0.3 * t, 0, 0
    return f


def m_cracked_earth(pal=("7a3f22", "96552f", "b0673a", "c47a48"), crackc=("3a2418", "5e3a22"),
                    seed=171, count=7, sep=4.2, raised=False, shine=0.0):
    n = fbm(seed)
    vo = voronoi(seed + 1, count, sep)
    wp = warp(seed + 2, 0.45)
    def f(u, v):
        d1, d2, idx = vo(*wp(u, v))
        e = (d2 - d1) * 0.5
        t = n(u, v) * 0.7 + (idx % 3) * 0.1
        if raised:   # salt: the crack edges are pushed-up ridges
            ridge = sstep(0.55, 0.0, e)
            return ramp(list(pal), 0.3 + 0.6 * t + 0.3 * ridge), 1, 0.4 + 0.2 * t + 0.4 * ridge, shine, 0
        plate = sstep(0.0, 1.8, e)
        crack = sstep(0.28, 0.0, e)
        c = ramp(list(pal), 0.2 + 0.7 * t) if crack < 0.5 else ramp(list(crackc), crack)
        return c, 1, 0.2 + 0.6 * plate * (0.8 + 0.2 * t) - 0.3 * crack, shine, 0
    return f


def m_clay():
    n = fbm(181, (2, 4, 8, 16))
    def f(u, v):
        t = n(u, v)
        return ramp(["8a4e2c", "9e5a32", "b0673a", "bf7446"], t), 1, 0.45 + 0.2 * t, 0.12, 0
    return f


def m_basalt():
    n = fbm(191)
    vo = voronoi(192, 6, 4.0)
    def f(u, v):
        d1, d2, idx = vo(u, v)
        joint = sstep(0.25, 0.0, (d2 - d1) * 0.5)
        t = n(u, v)
        c = ramp(["211d1e", "2c2729", "37302f", "4a4244"], 0.2 + 0.6 * t + 0.08 * (idx % 3) - 0.3 * joint)
        return c, 1, 0.6 + 0.3 * t - 0.5 * joint, 0.1, 0
    return f


def m_magma():
    n = fbm(201)
    vo = voronoi(202, 4, 5.5)
    live = lattice_noise(203, 4)
    wp = warp(204, 0.9)
    def f(u, v):
        d1, d2, _ = vo(*wp(u, v))
        hot = sstep(0.3, 0.45, live(u, v))
        vein = sstep(0.55, 0.0, (d2 - d1) * 0.5) * hot
        if vein > 0.15:
            return ramp(["7a2a10", "c8541e", "f08c34", "ffc060"], vein), 1, 0.15, 0, min(1.0, vein * 1.3)
        t = n(u, v)
        return ramp(["1e1b1c", "2a2527", "342d2f", "40383a"], t), 1, 0.55 + 0.4 * t, 0.08, 0
    return f


def m_bark():
    across = lattice_noise(211, 8)
    mean = lattice_noise(212, 2)
    fine = fbm(213, (8, 16, 32), aspect=0.25)   # fibres running down the trunk
    def f(u, v):
        uu = (u + (mean(0.5, v) - 0.5) * 3.0) % T
        r = across(uu, 0.5) * 0.75 + fine(u, v) * 0.25
        return ramp(["2c2018", "4a3628", "5a4433", "6b5240", "7d6248"], r), 1, r, 0, 0
    return f


def m_log_top():
    wob = lattice_noise(221, 4)
    def f(u, v):
        if min(u, v, T - u, T - v) < 1.0:
            return ramp(["2c2018", "4a3628"], wob((u * 3) % T, (v * 3) % T)), 1, 0.9, 0, 0
        r = math.hypot(u - 7.6, v - 8.3) + (wob(u, v) - 0.5) * 0.9
        ring = 0.5 + 0.5 * math.cos(r * 2 * math.pi / 1.3)
        c = ramp(["8a6440", "a87c4e", "c29664"], 0.25 + 0.75 * ring if r > 1.1 else 0.0)
        return c, 1, 0.6 + 0.1 * ring, 0.05, 0
    return f


def m_moss(pal=("2a2a18", "3f4a26", "4e5a2e", "5f6a34", "74803e"), seed=231):
    n = fbm(seed)
    cush = scatter(seed + 1, 0.9, 1.3, 2.4)
    def f(u, v):
        t = n(u, v)
        s, _ = cush(u, v)
        s = math.sqrt(max(s, 0))
        return ramp(list(pal), 0.15 + 0.5 * t + 0.35 * s), 1, 0.3 + 0.3 * t + 0.4 * s, 0, 0
    return f


def m_moss_stone():
    vo = voronoi(241, 7, 3.6)
    n = fbm(242)
    moss = fbm(243, (4, 8))
    def f(u, v):
        d1, d2, idx = vo(u, v)
        e = (d2 - d1) * 0.5
        t = n(u, v)
        gap = e < 0.45
        mossy = moss(u, v) > 0.55 and e < 1.1
        if gap or mossy:
            return ramp(["3a5a2e", "4a6a3a", "5e8248", "72965a"], 0.3 + 0.6 * t), 1, 0.35 + 0.25 * t + (0.2 if mossy and not gap else 0), 0, 0
        c = ramp(["4e4e4c", "5a5a58", "6a6a68", "7a7a76", "8a8a86"], 0.25 + 0.5 * t + 0.08 * (idx % 3))
        return c, 1, 0.4 + 0.55 * sstep(0.0, 1.6, e), 0.06, 0
    return f


def m_meadow():
    blades = fbm(251, (16, 32, 64), [1, 0.7, 0.5], aspect=0.375)   # thin upright blades
    n = fbm(252, (2, 4))
    flowers = scatter(253, 0.22, 0.45, 3.2)
    def f(u, v):
        b = blades(u, v)
        t = 0.5 * n(u, v) + 0.5 * b
        fl, fid = flowers(u, v)
        if fl > 0:
            pal = ["c8a830", "e0c840", "f0e070"] if fid < 0.6 else ["4a6fb0", "6a8fd0", "90b0e8"]
            return ramp(pal, 0.4 + 0.6 * fl), 1, 0.8 + 0.2 * fl, 0.05, 0
        return ramp(["2e5a22", "3e6b2e", "4a7a38", "588a42", "6a9a50"], t), 1, 0.3 + 0.6 * b, 0.04, 0
    return f


def m_water():
    warp = lattice_noise(261, 4)
    fine = fbm(262, (4, 8, 16))
    def f(u, v):
        w1 = math.sin((v + 2.0 * warp(u, v)) * 2 * math.pi / (T / 3)) * 0.5 + 0.5
        w2 = math.sin((u + v * 0.5 + 1.5 * warp(v, u)) * 2 * math.pi / (T / 2)) * 0.5 + 0.5
        t = 0.6 * w1 + 0.25 * w2 + 0.15 * fine(u, v)
        return ramp(["246a6e", "2a7a7e", "3f9498", "5cb8ba"], t), 0.62 + 0.2 * t, 0.3 + 0.35 * t, 0.85, 0
    return f


def m_ice():
    n = fbm(271, (2, 4, 8))
    vo = voronoi(272, 4, 5.5)
    bub = scatter(273, 0.3, 0.25, 2.0)
    wp = warp(274, 0.5)
    def f(u, v):
        d1, d2, _ = vo(*wp(u, v))
        crack = sstep(0.2, 0.0, (d2 - d1) * 0.5)
        t = n(u, v)
        b, _ = bub(u, v)
        c = ramp(["a8c8dc", "c8e0ec", "e0eef0", "f4fafa"], 0.3 + 0.6 * t + 0.4 * (b > 0.3)) if crack < 0.5 else ramp(["6a90a8", "8aaec2"], crack)
        return c, 0.82 + 0.15 * crack, 0.6 + 0.2 * t - 0.4 * crack, 0.9, 0
    return f


def m_ash():
    n = fbm(281, (4, 8, 16, 32))
    embers = scatter(282, 0.3, 0.45, 3.2)
    def f(u, v):
        e, _ = embers(u, v)
        if e > 0.25:
            return ramp(["6a3a20", "8a5a3a", "c07a3a"], e), 1, 0.45, 0, min(1.0, e * 0.6)
        t = n(u, v)
        return ramp(["1e1c1a", "302e2c", "3a3836", "46443f", "52504a"], t), 1, 0.35 + 0.3 * t, 0, 0
    return f


def m_coral():
    polyp = scatter(291, 0.95, 0.85, 1.6)
    n = fbm(292, (2, 4, 8))
    def f(u, v):
        p, pid = polyp(u, v)
        t = n(u, v)
        if p > 0.8:   # the dark mouth at each polyp's centre
            return ramp(["4a1a24", "6a2a3a"], t), 1, 0.55, 0.3, 0
        if p > 0:
            c = ramp(["6a2a3a", "8a3a4a", "a85a6a", "c47888", "e0a0b0"], 0.3 + 0.6 * p + 0.1 * pid)
            return c, 1, 0.3 + 0.6 * math.sqrt(p), 0.1, 0
        return ramp(["4a1a24", "6a2a3a"], t), 1, 0.15, 0, 0
    return f


def m_canopy():
    leaves = scatter(301, 1.0, 1.6, 2.0, (1.0, 0.55))
    under = fbm(302, (2, 4, 8))
    def f(u, v):
        l, lid = leaves(u, v)
        if l > 0:
            vein = 1 if abs(((v * 3.0 + lid * 7) % 1.0) - 0.5) < 0.06 and l > 0.35 else 0
            c = ramp(["1e5a2e", "2a6e38", "3a8248", "4a9a58", "5aae66"], 0.2 + 0.55 * l + 0.25 * lid - 0.25 * vein)
            return c, 1, 0.4 + 0.5 * math.sqrt(l) - 0.1 * vein, 0.2, 0
        return ramp(["0c2a14", "123e1e"], under(u, v)), 1, 0.1, 0, 0
    return f


def m_litter():
    layers = [scatter(311 + k, 0.8, 1.3, 2.4, (1.0, 0.6)) for k in range(3)]
    pal = [["8a4a24", "a85e2e", "c8862e"], ["6e3818", "8a4a24", "a85e2e"], ["a85e2e", "c8862e", "e0a848"]]
    base = fbm(314)
    def f(u, v):
        for k in (2, 1, 0):   # topmost layer first
            l, lid = layers[k]((u + k * 1.7) % T, (v + k * 2.3) % T)
            if l > 0:
                return ramp(pal[k], 0.25 + 0.6 * l + 0.15 * lid), 1, 0.35 + 0.2 * k + 0.2 * l, 0.05, 0
        return ramp(["2e1a0a", "4a2810"], base(u, v)), 1, 0.1, 0, 0
    return f


def m_peat():
    fib = fbm(321, (2, 4, 8, 16), [1, 0.8, 0.6, 0.4], aspect=4)   # flattened fibres
    n = fbm(322, (2, 4))
    def f(u, v):
        t = 0.55 * fib(u, v) + 0.45 * n(u, v)
        wet = sstep(0.42, 0.3, n(u, v))
        return ramp(["1e1a14", "2e2a20", "3a3428", "463f30", "5a5240"], t - 0.2 * wet), 1, 0.3 + 0.5 * t - 0.2 * wet, 0.1 + 0.6 * wet, 0
    return f


def m_pebbles():
    vo = voronoi(331, 9, 3.2)
    n = fbm(332, (4, 8, 16, 32))
    pal = [["6a6a62", "8a8a82", "9a9a90"], ["5a6a72", "72828a", "8a9aa2"], ["8a8468", "b0a888", "c4bc9c"], ["6a6258", "7a7a70", "90887a"]]
    def f(u, v):
        d1, d2, idx = vo(u, v)
        e = (d2 - d1) * 0.5
        if e < 0.35:
            return ramp(["6a5e48", "8a7a60"], n(u, v)), 1, 0.1, 0, 0
        dome = sstep(0.35, 1.8, e)
        return ramp(pal[idx % 4], 0.3 + 0.5 * dome + 0.2 * n(u, v)), 1, 0.25 + 0.7 * math.sqrt(dome), 0.25 * dome, 0
    return f


def m_coastal_sand():
    warp = lattice_noise(341, 4)
    n = fbm(342)
    shell = scatter(343, 0.15, 0.35, 3.2)
    def f(u, v):
        w = math.sin((v + 1.2 * warp(u, v) + 0.4 * math.sin(u * 2 * math.pi / 8)) * 2 * math.pi / 3.2)
        crest = 0.5 + 0.5 * w
        s, _ = shell(u, v)
        if s > 0.3:
            return ramp(["e8e0d0", "f4f0e8"], s), 1, 0.7, 0.3, 0
        t = n(u, v)
        return ramp(["b89050", "c8a86a", "ddc190", "e8cfa0", "f0dcb0"], 0.2 + 0.5 * crest + 0.3 * t), 1, 0.3 + 0.35 * crest + 0.1 * t, 0.03, 0
    return f


# -- the dark set --

def m_veined_flesh():
    n = fbm(401, (2, 4, 8, 16))
    veins = voronoi(402, 8, 3.0)
    nod = scatter(403, 0.35, 0.6, 2.8)
    wp = warp(404, 0.7)
    def f(u, v):
        t = n(u, v)
        d1, d2, _ = veins(*wp(u, v))
        vein = sstep(0.3, 0.0, (d2 - d1) * 0.5)
        k, _ = nod(u, v)
        if k > 0.15:
            return ramp(["a04860", "d47a90", "e8a0b4"], k), 1, 0.7 + 0.25 * k, 0.6, 0
        c = ramp(["32141c", "4a1420", "5a1a28", "6e2432"], 0.2 + 0.7 * t) if vein < 0.5 else ramp(["6a0c24", "8a1030"], vein)
        return c, 1, 0.4 + 0.3 * t + 0.15 * vein, 0.35, 0
    return f


def m_flesh_wound():
    # A framed ornament (like the rune shrine): an even dark frame on all
    # four sides and a diamond gash in the middle, wet and faintly aglow.
    n = fbm(411, (2, 4, 8))
    def f(u, v):
        t = n(u, v)
        edge = min(u, v, T - u, T - v)
        if edge < 1.5:
            return ramp(["1e060c", "2a0810"], t), 1, 0.85, 0.2, 0
        dm = abs(u - 8) + abs(v - 8)
        if dm < 1.6:                     # the deep centre
            return ramp(["301420", "5a1426"], t), 1, 0.05, 0.9, 0.25
        if abs(dm - 4.5) < 0.6:          # the torn rim
            return ramp(["8a1030", "c85070"], 1 - abs(dm - 4.5) / 0.6), 1, 0.6, 0.8, 0
        if dm < 4.5:
            return ramp(["5a1426", "8a1030"], 0.3 + 0.5 * t), 1, 0.25 + 0.2 * (dm / 4.5), 0.7, 0
        return ramp(["2e0a12", "3a0e18", "4a1420"], t), 1, 0.45 + 0.15 * t, 0.3, 0
    return f


def m_membrane():
    n = fbm(421, (2, 4, 8))
    veins = voronoi(422, 6, 3.5)
    bulge = scatter(423, 0.6, 1.2, 3.2)
    wp = warp(424, 0.7)
    def f(u, v):
        t = n(u, v)
        d1, d2, _ = veins(*wp(u, v))
        vein = sstep(0.28, 0.0, (d2 - d1) * 0.5)
        b, _ = bulge(u, v)
        if vein > 0.5:
            return ramp(["40141c", "5a1a28"], vein), 1, 0.55, 0.4, 0.5 * vein
        c = ramp(["5e2030", "6e2838", "7e3242", "9a4a5a", "b06070"], 0.2 + 0.5 * t + 0.35 * b)
        return c, 1, 0.3 + 0.2 * t + 0.4 * math.sqrt(max(b, 0)), 0.5, 0.15 * b
    return f


def m_weeping_sore():
    n = fbm(431, (2, 4, 8, 16))
    sores = scatter(432, 0.4, 1.1, 3.2)
    def f(u, v):
        t = n(u, v)
        s, _ = sores(u, v)
        if s > 0.55:                     # the weeping centre
            return ramp(["c85a70", "e08898"], (s - 0.55) / 0.45), 1, 0.25, 1.0, 0
        if s > 0.25:                     # the raised, darker ring
            return ramp(["3a1420", "5a1a28"], t), 1, 0.75, 0.4, 0
        return ramp(["4a1420", "5a1a28", "6e2432"], t), 1, 0.4 + 0.2 * t + 0.2 * max(s, 0), 0.3, 0
    return f


def m_corrupted_flesh():
    fib = fbm(441, (8, 16, 32, 64), [1, 0.8, 0.6, 0.4], aspect=0.3)   # matted, hanging fibres
    n = fbm(442, (2, 4))
    holes = scatter(443, 0.25, 0.6, 3.2)
    def f(u, v):
        t = 0.6 * fib(u, v) + 0.4 * n(u, v)
        h, _ = holes(u, v)
        if h > 0.2:
            return ramp(["1e1c14", "2a2418"], h), 1, 0.05, 0.5, 0
        return ramp(["3e3e26", "4a4a2e", "5a5a34", "6a6a3a"], t), 1, 0.35 + 0.45 * t, 0.15, 0
    return f


# -- the genesis set: the light counterparts --

def m_genesis_soil():
    n = fbm(501)
    motes = scatter(502, 0.35, 0.3, 2.0)
    def f(u, v):
        t = n(u, v)
        m, _ = motes(u, v)
        if m > 0.2:
            return ramp(["c89840", "e0b050", "f0d080"], m), 1, 0.6, 0.7, 0.2 + 0.4 * m
        return ramp(["241808", "2e2010", "3a2818", "46301e", "523a26"], t), 1, 0.35 + 0.4 * t, 0.05, 0
    return f


def m_seedling_sprout():
    # The shrine's geometry, genesis version: an even gold frame on all four
    # sides, a gold diamond ring, and a sprout with a softly glowing bud.
    n = fbm(511, (2, 4, 8))
    def f(u, v):
        t = n(u, v)
        if min(u, v, T - u, T - v) < 1.5:
            return ramp(["a88420", "c9a227", "e0bc40"], 0.3 + 0.6 * t), 1, 0.85, 0.7, 0
        dm = abs(u - 8) + abs(v - 8)
        if abs(dm - 5.2) < 0.45:
            return ramp(["c9a227", "f0d060"], 1 - abs(dm - 5.2) / 0.45), 1, 0.75, 0.8, 0
        bud = math.hypot(u - 8, v - 6.6)
        if bud < 1.0:
            return ramp(["e0c040", "f0d060", "fff0a0"], 1 - bud), 1, 0.8, 0.4, 0.7 * (1 - bud)
        if abs(u - 8) < 0.35 and 6.6 < v < 10.8:                       # stem
            return ramp(["3a6a2c", "4a7a38"], t), 1, 0.6, 0.1, 0
        leaf_l = math.hypot((u - 6.6) / 1.4, (v - 9.0) / 0.6) < 1.0
        leaf_r = math.hypot((u - 9.4) / 1.4, (v - 8.4) / 0.6) < 1.0
        if leaf_l or leaf_r:
            return ramp(["4a7a38", "5e9048", "72a858"], 0.4 + 0.5 * t), 1, 0.65, 0.2, 0
        return ramp(["d8ccb0", "e8dcc0", "f2e8d0"], t), 1, 0.4 + 0.1 * t, 0.15, 0
    return f


def m_dawn_light():
    # A smooth symmetric band (it tiles as a repeating band of dawn); the
    # bright middle glows.
    n = fbm(521, (2, 4))
    def f(u, v):
        d = abs(v - 8.0) / 8.0 + (n(u, v) - 0.5) * 0.06
        c = ramp(["f0c868", "d4a050", "9a6a5a", "5a4a6a", "2e3258", "1a1e3a"], d, steps=16)
        return c, 1, 0.5, 0.1, max(0.0, 1.0 - d * 2.2) * 0.8
    return f


def m_star_forge():
    # Light gathering inward: sparks streaming along rays toward a bright
    # core, brighter the nearer they get.
    n = fbm(531)
    rng = random.Random(532)
    rays = [(rng.uniform(0, 2 * math.pi), rng.uniform(2.6, 6.0), rng.uniform(1.8, 3.6)) for _ in range(18)]
    def f(u, v):
        t = n(u, v)
        du, dv = u - 8.0, v - 8.0
        r = math.hypot(du, dv)
        if r < 1.4:
            return ramp(["f0e8c8", "fff8e8"], 1 - r / 1.4), 1, 0.7, 0.3, 1.0
        a = math.atan2(dv, du)
        for ang, start, length in rays:
            da = abs((a - ang + math.pi) % (2 * math.pi) - math.pi) * r   # distance across the ray
            along = r - 1.6
            if da < 0.28 and start - length < along < start and (along * 1.7) % 1.0 < 0.65:
                k = 1.0 - along / 7.0
                return ramp(["c89840", "f0c860", "f0e8c8"], k), 1, 0.5, 0.2, 0.4 + 0.5 * k
        halo = max(0.0, 1.0 - r / 4.0)
        return ramp(["1c1826", "24202e", "2e2a3a", "3a3448"], 0.2 + 0.5 * t + 0.3 * halo), 1, 0.45 + 0.3 * t, 0.1, 0.15 * halo
    return f


def m_new_bark():
    # Young bark: smooth, lighter, with short horizontal pores (lenticels)
    # -- the "before" of the ancient log.
    n = fbm(541, (2, 4, 8), aspect=0.5)
    fib = fbm(542, (8, 16, 32), aspect=0.25)
    pores = scatter(543, 0.55, 0.9, 2.0, (1.0, 0.18))
    def f(u, v):
        t = 0.6 * n(u, v) + 0.4 * fib(u, v)
        p, _ = pores(u, v)
        if p > 0.15:
            return ramp(["b89060", "d4a860"], p), 1, 0.3, 0.1, 0
        return ramp(["6e5234", "7a5a3a", "8a6a48", "9a7a54", "a8885e"], t), 1, 0.5 + 0.3 * t, 0.12, 0
    return f


TEXTURES = [
    ("stone", "Stone: cool grey, mottled, a few hairline cracks", m_stone()),
    ("dirt", "Dirt: warm clumps, scattered pebbles and grit", m_dirt()),
    ("wood", "Wood: staggered planks, grain along them, deep joints", m_wood()),
    ("snow", "Snow: soft drifts, sparkling crystals", m_snow()),
    ("sand", "Sand: soft ripples, grains", m_sand()),
    ("sandstone_layered", "Sandstone, side: uneven strata that stand out as ledges", m_sandstone_side()),
    ("sandstone_top", "Sandstone, top and bottom: smooth and faintly mottled", m_sandstone_top()),
    ("cracked_earth", "Cracked earth: raised plates between deep wrapping cracks", m_cracked_earth()),
    ("clay", "Clay: smooth, faintly glossy", m_clay()),
    ("basalt", "Basalt: dark stone with jointing", m_basalt()),
    ("magma_rock", "Magma rock: dark stone split by glowing lava veins that wrap", m_magma()),
    ("log_bark", "Log, sides: long meandering furrows", m_bark()),
    ("log_top", "Log, ends: growth rings, a bark rim", m_log_top()),
    ("moss", "Moss: soft cushions", m_moss()),
    ("moss_stone", "Mossy cobble: domed stones, moss in the gaps and over some", m_moss_stone()),
    ("meadow_grass", "Meadow grass: blades, the odd yellow or blue flower", m_meadow()),
    ("shallow_water", "Shallow water: see-through, rippling, glossy", m_water()),
    ("glacier_ice", "Glacier ice: see-through, glossy, a few deep cracks and bubbles", m_ice()),
    ("volcanic_ash", "Volcanic ash: fine and dark, rare smouldering embers", m_ash()),
    ("coral_reef", "Coral: clustered polyps", m_coral()),
    ("jungle_canopy", "Jungle leaves: overlapping leaves with veins", m_canopy()),
    ("autumn_leaf_litter", "Leaf litter: overlapping fallen leaves in autumn colours", m_litter()),
    ("peat_bog", "Peat: dark fibres, wet glossy hollows", m_peat()),
    ("salt_flat", "Salt flat: white crust with raised polygon ridges",
     m_cracked_earth(("c8c0ae", "dcd6c4", "e8e4d8", "f4f0e6"), ("b8b0a0", "d8d2c2"), 351, 7, 4.0, True, 0.2)),
    ("river_pebble", "River pebbles: rounded stones in sand", m_pebbles()),
    ("coastal_sand", "Coastal sand: ripple marks, the odd shell", m_coastal_sand()),
    ("veined_flesh", "Veined flesh: wet dark tissue, a web of veins, pale nodules", m_veined_flesh()),
    ("flesh_wound", "Flesh wound: an even dark frame round a wet diamond gash, faintly aglow", m_flesh_wound()),
    ("pulsing_membrane", "Pulsing membrane: taut, veined, its veins glow (the block pulses them slowly)", m_membrane()),
    ("weeping_sore", "Weeping sore: dark flesh with scattered wet sores", m_weeping_sore()),
    ("corrupted_flesh", "Corrupted flesh: rotting olive fibres, dark pits", m_corrupted_flesh()),
    ("genesis_soil", "Genesis soil: rich dark earth with glinting golden motes", m_genesis_soil()),
    ("seedling_sprout", "Seedling shrine: an even gold frame and ring round a sprout with a glowing bud", m_seedling_sprout()),
    ("dawn_light", "Dawn light: a smooth band of dawn colours, its bright middle aglow", m_dawn_light()),
    ("star_forge", "Star forge: sparks streaming inward to a bright core", m_star_forge()),
    ("new_bark", "New bark: smooth young bark with horizontal pores", m_new_bark()),
]
SINGLE = ("stone", "dirt", "wood", "snow", "sand", "cracked_earth", "clay", "basalt", "magma_rock", "moss",
          "moss_stone", "meadow_grass", "shallow_water", "glacier_ice", "volcanic_ash", "coral_reef", "jungle_canopy",
          "autumn_leaf_litter", "peat_bog", "salt_flat", "river_pebble", "coastal_sand",
          "veined_flesh", "flesh_wound", "pulsing_membrane", "weeping_sore", "corrupted_flesh",
          "genesis_soil", "seedling_sprout", "dawn_light", "star_forge")
BLOCKS = [(n, [("all", n)]) for n in SINGLE] + [
    ("sandstone", [("side", "sandstone_layered"), ("top", "sandstone_top"), ("bottom", "sandstone_top")]),
    ("log", [("side", "log_bark"), ("top", "log_top"), ("bottom", "log_top")]),
    ("new_log", [("side", "new_bark"), ("top", "log_top"), ("bottom", "log_top")]),
]

KEYS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!$%&*+-./:;<=>?@^_~()[]{}|,'`\"\\"
HDIG = "0123456789abcdefghijklmnopqrstuvwxyz"


def render(name, note, mat, size, flat):
    k = size / T
    px, hs, ss, gs = [], [], [], []
    for y in range(size):
        prow, hrow, srow, grow = [], [], [], []
        for x in range(size):
            rgb, a, h, sh, gl = mat((x + 0.5) / k, (y + 0.5) / k)
            prow.append((tuple(rgb), int(round(min(max(a, 0), 1) * 255))))
            hrow.append(h); srow.append(sh); grow.append(gl)
        px.append(prow); hs.append(hrow); ss.append(srow); gs.append(grow)
    colours = sorted({c for row in px for c in row})
    if len(colours) > len(KEYS):
        raise SystemExit("%s: %d colours, more than %d keys" % (name, len(colours), len(KEYS)))
    key = {c: KEYS[i] for i, c in enumerate(colours)}
    out = ["# " + note, "texture " + name, "size %d" % size, "palette"]
    for c in colours:
        (r, g, b), a = c
        out.append("  %s %02x%02x%02x%s" % (key[c], r, g, b, "" if a == 255 else "%02x" % a))
    out.append("pixels")
    out += ["  " + "".join(key[c] for c in row) for row in px]
    if not flat:
        q = lambda f, n: min(n, max(0, int(round(f * n))))
        out.append("height")
        out += ["  " + "".join(HDIG[q(h, 35)] for h in row) for row in hs]
        if any(s > 0.06 for row in ss for s in row):
            out.append("shine")
            out += ["  " + "".join(str(q(s, 9)) for s in row) for row in ss]
        if any(g > 0.06 for row in gs for g in row):
            out.append("glow")
            out += ["  " + "".join(str(q(g, 9)) for g in row) for row in gs]
    out += ["end", ""]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=64, choices=(8, 16, 32, 64))
    ap.add_argument("--flat", action="store_true", help="colours only, no height/shine/glow maps")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "textures", "natural.vtex"))
    args = ap.parse_args()
    out = ["# natural.vtex -- the natural-material set, %d px per block%s." % (args.size, ", colours only" if args.flat else ", with height/shine/glow maps"),
           "# Generated by tools/natural_textures.py from the art batches' palettes;",
           "# rerun it after tweaking a material, or hand-edit here and stop regenerating.", ""]
    for name, note, mat in TEXTURES:
        out += render(name, note, mat, args.size, args.flat)
    for block, faces in BLOCKS:
        out += ["block " + block] + ["  %s %s" % f for f in faces] + ["end", ""]
    with open(args.out, "w") as f:
        f.write("\n".join(out))
    print("wrote", os.path.normpath(args.out))


if __name__ == "__main__":
    main()
