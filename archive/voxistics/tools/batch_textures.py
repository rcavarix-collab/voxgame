#!/usr/bin/env python3
"""Generates assets/textures/batch_sept.vtex: a big trial batch of block
materials for the owner to sort through (DESIGN.md 4.13; the texture
direction in docs/REVIEW_2026-09.md).

Made family by family against the review's briefs:
  land      -- muted, earthy, low saturation; relief through clumps,
               strata and stones, never tiny repeated specks
  building  -- mid-tone stone, timber, earth and fired clay
  industry  -- darker, quiet metals and mineral panels
  strange   -- light rather than pigment: glow maps, kept off the three
               pulse hues at full strength

A handful of parametric recipes (soil, stones, strata, speckle, bricks,
planks, turf, plate, ribs, grate, straw, plaster, tile, lattice); each
material is one line of parameters, so a tweak is a number, not a redraw.
Each texture is named after its block, so the block needs no texture line.

  python3 tools/batch_textures.py     # writes assets/textures/batch_sept.vtex

Deterministic (seeded). The same field helpers and writer as the natural set.
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from natural_textures import T, fbm, lattice_noise, voronoi, scatter, ramp, sstep, wrapd, render  # noqa: E402


# ---- recipes: each returns f(u, v) -> (rgb, alpha, height, shine, glow) ----

def soil(pal, seed, pebbles=0.35, pebble_pal=("6e665c", "8a8276"), wet=0.0):
    n, big = fbm(seed), fbm(seed + 1, (2, 4))
    peb = scatter(seed + 2, pebbles, 0.55, 2.3)
    def f(u, v):
        t = 0.6 * n(u, v) + 0.4 * big(u, v)
        p, _ = peb(u, v)
        if p > 0.15:
            return ramp(list(pebble_pal), 0.3 + 0.6 * p), 1, 0.55 + 0.35 * p, 0.05 + wet * 0.3, 0
        return ramp(pal, t), 1, 0.25 + 0.4 * t, 0.02 + wet * (0.25 + 0.3 * big(u, v)), 0
    return f


def stones(pal, seed, count=9, sep=3.4, gap_pal=("1e1a16", "2a2520"), gap=0.35, dome=1.0, gap_fill=None):
    """Rounded stones (Voronoi cells), each its own tone, with dark or filled gaps."""
    cells = voronoi(seed, count, sep)
    n = fbm(seed + 1, (4, 8, 16))
    tone = [random.Random(seed * 31 + i).uniform(-0.18, 0.18) for i in range(64)]
    def f(u, v):
        d1, d2, i = cells(u, v)
        border = (d2 - d1) * 0.5
        t = n(u, v)
        if border < gap:
            if gap_fill:
                return gap_fill(u, v)
            return ramp(list(gap_pal), t), 1, 0.08, 0.0, 0
        round_ = sstep(gap, gap + 1.4 * dome, border)
        c = ramp(pal, 0.45 + tone[i % 64] + 0.25 * (t - 0.5) + 0.2 * round_)
        return c, 1, 0.3 + 0.6 * round_ + 0.1 * t, 0.06, 0
    return f


def strata(pal, seed, bands=5, wobble=0.9, ledge=True):
    n = fbm(seed, (2, 4, 8))
    w = lattice_noise(seed + 1, 4)
    edges = sorted(random.Random(seed).uniform(0, T) for _ in range(bands))
    tone = [random.Random(seed + 7 + i).uniform(0.15, 0.85) for i in range(bands)]
    def f(u, v):
        vv = (v + (w(u, v) - 0.5) * 2 * wobble) % T
        k = 0
        for j, e in enumerate(edges):
            if vv >= e: k = j
        e0 = edges[k]; e1 = edges[(k + 1) % bands] + (T if k == bands - 1 else 0)
        into = ((vv - e0) % T) / max(0.5, (e1 - e0))
        t = tone[k] + 0.25 * (n(u, v) - 0.5)
        lip = sstep(0.12, 0.0, into) if ledge else 0
        return ramp(pal, t - 0.25 * lip), 1, 0.35 + 0.4 * (1 - into) * 0.6 + 0.2 * n(u, v) - 0.3 * lip, 0.04, 0
    return f


def speckle(pal, speck_pal, seed, density=0.9, size=0.35, cell=1.4):
    n = fbm(seed, (4, 8, 16))
    sp = scatter(seed + 1, density, size, cell)
    def f(u, v):
        t = n(u, v)
        s, sid = sp(u, v)
        if s > 0.2:
            return ramp(list(speck_pal), sid), 1, 0.55, 0.12, 0
        return ramp(pal, t), 1, 0.45 + 0.2 * t, 0.06, 0
    return f


def bricks(pal, mortar_pal, seed, cols=2, rows=4, mortar=0.4, rough=0.5):
    bw, bh = T / cols, T / rows
    n = fbm(seed, (4, 8, 16))
    tone = [random.Random(seed * 13 + i).uniform(-0.15, 0.15) for i in range(64)]
    def f(u, v):
        r = int(v // bh)
        uu = (u + (bw / 2 if r % 2 else 0)) % T
        c = int(uu // bw)
        x, y = uu - c * bw, v - r * bh
        edge = min(x, bw - x, y, bh - y)
        t = n(u, v)
        if edge < mortar:
            return ramp(list(mortar_pal), t), 1, 0.12, 0.0, 0
        bevel = sstep(mortar, mortar + 0.6, edge)
        return ramp(pal, 0.45 + tone[(r * cols + c) % 64] + rough * 0.3 * (t - 0.5) + 0.12 * bevel), 1, 0.4 + 0.45 * bevel, 0.05, 0
    return f


def planks(pal, seed, count=4, weather=0.0, gap_pal=("24180e", "301f12")):
    ph = T / count
    tone = [random.Random(seed + i).uniform(-0.12, 0.12) for i in range(count)]
    joint = [(i * 7 + 3) % 16 + 0.5 for i in range(count)]
    grain = fbm(seed + 20, (1, 2, 4, 8), [1, 0.6, 0.3, 0.15], aspect=8)
    worn = fbm(seed + 21, (2, 4))
    def f(u, v):
        p = int(v // ph) % count
        y = v - p * ph
        gap = sstep(0.35, 0.0, min(y, ph - y))
        end = sstep(0.35, 0.0, wrapd(u, joint[p]))
        g = grain(u, (v + p * 3.7) % T)
        t = 0.5 + tone[p] + (g - 0.5) * 0.7 - weather * 0.3 * worn(u, v)
        if max(gap, end) > 0.6:
            return ramp(list(gap_pal), t), 1, 0.1, 0.0, 0
        return ramp(pal, t), 1, 0.7 + 0.15 * g - 0.5 * max(gap, end), 0.06, 0
    return f


def turf(tuft_pal, gap_pal, seed, tufts=0.9):
    # Smaller, closer tufts than the meadow's, so a turf reads as a sward,
    # not as blotches.
    blades = fbm(seed, (16, 32, 64), [1, 0.7, 0.5], aspect=0.375)
    n = fbm(seed + 1, (2, 4))
    tf = scatter(seed + 2, tufts, 0.8, 1.6, squash=(1.0, 0.8))
    def f(u, v):
        b = blades(u, v)
        s, sid = tf(u, v)
        w = n(u, v)
        if s > 0:
            return ramp(tuft_pal, 0.35 + 0.3 * s + 0.25 * b + 0.05 * (sid - 0.5) + 0.15 * (w - 0.5)), 1, 0.45 + 0.5 * s, 0.04, 0
        return ramp(gap_pal, 0.3 + 0.4 * b + 0.3 * w), 1, 0.2 + 0.3 * b, 0.02, 0
    return f


def plate(pal, seed, seams=2, rivets=True, rivet_pal=("8a9096", "b0b6bc")):
    n = fbm(seed, (2, 4, 8), aspect=2.0)
    step = T / seams
    def f(u, v):
        x, y = u % step, v % step
        edge = min(x, step - x, y, step - y)
        t = n(u, v)
        if edge < 0.3:
            return ramp(pal[:2], t), 1, 0.15, 0.3, 0
        if rivets and edge < 1.2:
            cx = min(x, step - x); cy = min(y, step - y)
            for rx, ry in ((0.9, 0.9),):
                if math.hypot(cx - rx, cy - ry) < 0.4:
                    return ramp(list(rivet_pal), 0.7), 1, 0.9, 0.8, 0
        return ramp(pal, 0.3 + 0.4 * t), 1, 0.5 + 0.1 * t, 0.45, 0
    return f


def ribs(pal, seed, period=2.0):
    n = fbm(seed, (2, 4), aspect=4.0)
    def f(u, v):
        ph = (u % period) / period
        prof = 0.5 + 0.5 * math.cos(ph * 2 * math.pi)
        t = n(u, v)
        return ramp(pal, 0.2 + 0.55 * prof + 0.2 * (t - 0.5)), 1, 0.2 + 0.7 * prof, 0.5, 0
    return f


def grate(bar_pal, hole_pal, seed, period=2.0, bar=0.6):
    n = fbm(seed, (4, 8))
    def f(u, v):
        x, y = u % period, v % period
        t = n(u, v)
        if x < bar or y < bar:
            onx = x < bar
            return ramp(bar_pal, 0.4 + 0.3 * t + (0.15 if onx else 0)), 1, 0.8 if onx else 0.7, 0.5, 0
        return ramp(hole_pal, t), 1, 0.0, 0.0, 0
    return f


def straw(pal, seed):
    s1 = fbm(seed, (2, 4, 8, 16), aspect=6.0)   # strands along u
    n = fbm(seed + 1, (2, 4))
    def f(u, v):
        vv = (v + u) % T                        # laid on a slant, as thatch is (45 degrees: the only slant that wraps)
        t = s1(u, vv)
        layer = (v % 4.0) / 4.0
        return ramp(pal, 0.2 + 0.6 * t + 0.2 * (n(u, v) - 0.5) - 0.25 * sstep(0.85, 1.0, layer)), 1, 0.3 + 0.5 * t - 0.3 * sstep(0.85, 1.0, layer), 0.03, 0
    return f


def plaster(pal, stain_pal, seed, cracks=True):
    n, big = fbm(seed, (4, 8, 16)), fbm(seed + 1, (2, 4))
    cr = voronoi(seed + 2, 4, 6.0)
    def f(u, v):
        t = n(u, v)
        d1, d2, _ = cr(u, v)
        crack = cracks and (d2 - d1) * 0.5 < 0.12 and big(u, v) > 0.55
        stain = sstep(0.62, 0.8, big(u + 5, v + 3))
        if crack:
            return ramp(list(stain_pal), 0.2), 1, 0.2, 0.0, 0
        c = ramp(pal, 0.5 + 0.25 * (t - 0.5) - 0.35 * stain)
        return c, 1, 0.55 + 0.1 * t, 0.03, 0
    return f


def tiles(pal, grout_pal, seed, count=4, grout=0.35):
    step = T / count
    tone = [random.Random(seed + i).uniform(-0.12, 0.12) for i in range(count * count)]
    n = fbm(seed + 3, (4, 8))
    def f(u, v):
        i, j = int(u // step), int(v // step)
        x, y = u - i * step, v - j * step
        edge = min(x, step - x, y, step - y)
        t = n(u, v)
        if edge < grout:
            return ramp(list(grout_pal), t), 1, 0.1, 0.0, 0
        bev = sstep(grout, grout + 0.5, edge)
        return ramp(pal, 0.5 + tone[(j * count + i) % len(tone)] + 0.15 * (t - 0.5)), 1, 0.4 + 0.4 * bev, 0.25, 0
    return f


def lattice(pal, glow_pal, seed, count=8, sep=3.0, glow=0.6):
    """Crystal cells lit from within: a glow map does the work (the strange layer)."""
    cells = voronoi(seed, count, sep)
    tone = [random.Random(seed + i).uniform(0.0, 1.0) for i in range(64)]
    def f(u, v):
        d1, d2, i = cells(u, v)
        b = (d2 - d1) * 0.5
        if b < 0.25:
            return ramp(pal[:2], 0.3), 1, 0.2, 0.3, 0
        core = 1.0 - min(1.0, d1 / 3.0)
        g = glow * (0.35 + 0.65 * core) * (0.6 + 0.4 * tone[i % 64])
        return ramp(glow_pal, 0.2 + 0.7 * core), 1, 0.4 + 0.5 * sstep(0.25, 1.2, b), 0.6, g
    return f


def streaked(base, streak_pal, seed, glow=0.25):
    """A base material crossed by faint pale bands that glow a little -- time-worn."""
    w = lattice_noise(seed, 4)
    def f(u, v):
        rgb, a, h, sh, gl = base(u, v)
        band = abs(((v + (w(u, v) - 0.5) * 3.0) % 5.3) - 2.65)
        if band < 0.35:
            k = 1 - band / 0.35
            return ramp(list(streak_pal), k), a, h, sh, glow * k
        return rgb, a, h, sh, gl
    return f


def spores(base, spore_pal, seed, glow=0.5):
    sp = scatter(seed, 0.6, 0.3, 1.6)
    def f(u, v):
        s, _ = sp(u, v)
        if s > 0.3:
            return ramp(list(spore_pal), s), 1, 0.6, 0.1, glow * s
        return base(u, v)
    return f


# ---- the batch -------------------------------------------------------------

LAND = [
    ("loam", "Loam: soft crumbly brown earth, a few small stones", soil(["3e2e20", "4c3826", "5a432e", "684e36"], 1001)),
    ("dark_humus", "Dark humus: near-black rich soil, fibrous", soil(["1c1812", "241f17", "2e271d", "383024"], 1011, 0.15, ("4a4236", "5c5244"))),
    ("gravel", "Gravel: small grey-brown stones packed tight", stones(["5a554e", "6e6860", "827c72", "968f84"], 1021, 22, 1.9, gap=0.2, dome=0.6)),
    ("coarse_sand", "Coarse sand: pale grains, loosely clumped", speckle(["a89a7c", "b8aa8a", "c6b898", "d2c6a6"], ["8a7e66", "e0d6be"], 1031, 1.0, 0.3, 1.0)),
    ("silt", "Silt: fine smooth pale mud, faintly layered", strata(["6e6458", "807668", "928878", "a49a8a", "b2a898"], 1041, 7, 0.6, True)),
    ("wet_mud", "Wet mud: dark, glistening, a few pebbles", soil(["2a2218", "342a1e", "3e3224", "483a2a"], 1051, 0.2, wet=1.0)),
    ("clay_bank", "Clay bank: layered ochre and rust clays", strata(["6a3e22", "8a5430", "a86c3e", "c08a52", "b87848"], 1061, 5, 0.8)),
    ("river_cobbles", "River cobbles: rounded stones in sand", stones(["6a6862", "7e7c74", "949188", "a8a49a"], 1071, 8, 3.6, gap=0.45, dome=1.2,
                                                                     gap_fill=lambda u, v: (ramp(["9a8e72", "aa9e82"], 0.5), 1, 0.1, 0.02, 0))),
    ("slate", "Slate: dark blue-grey, thin flat layers", strata(["22262c", "30353c", "3e434c", "4e545c", "5c626a"], 1081, 9, 0.25)),
    ("granite", "Granite: pale grey, speckled black and pink", speckle(["8a8886", "9a9896", "aaa8a4", "b8b6b2"], ["2a2826", "a07a70", "3a3634"], 1091, 1.0, 0.3, 1.1)),
    ("limestone", "Limestone: soft cream stone, faint shell flecks", speckle(["b0a890", "bcb49c", "c8c0a8", "d2cab2"], ["9a9280", "d8d0bc"], 1101, 0.5, 0.25, 1.8)),
    ("chalk", "Chalk: white, chalky, faintly pitted", soil(["c8c4b8", "d4d0c4", "dedad0", "e8e4da"], 1111, 0.25, ("b0aca0", "bcb8ac"))),
    ("mossy_gravel", "Mossy gravel: stones with moss creeping between", stones(["5e5a52", "726e64", "868178", "9a948a"], 1121, 18, 2.1, gap=0.3, dome=0.7,
                                                                             gap_fill=lambda u, v: (ramp(["2e4a22", "3a5a2a", "466a32"], lattice_noise(1122, 8)(u, v)), 1, 0.2, 0.02, 0))),
    ("heather_turf", "Heather turf: low dusky mauve-green tufts", turf(["4e4448", "5a4e54", "665a5e", "6e6656"], ["3a3634", "423e3a", "4a4640"], 1131)),
    ("dry_turf", "Dry turf: straw-coloured tufts over dry ground", turf(["80703e", "8e7c48", "9c8850", "aa965a"], ["625436", "6c5c3c", "766444"], 1143)),
    ("frost_turf", "Frost turf: pale rimed tufts", turf(["7a8a82", "8c9a94", "9eaca6", "b0bcb8"], ["58625e", "626c68", "6c7672"], 1151)),
]

BUILDING = [
    ("fieldstone_wall", "Fieldstone wall: irregular stones in pale mortar", stones(["5a5650", "6e6a62", "847e74", "9a9488"], 2001, 7, 4.0, ("8a8478", "9a9488"), 0.4, 0.9)),
    ("ashlar", "Ashlar: cut stone blocks, fine joints", bricks(["7e7a72", "8c8880", "9a968e", "a8a49c"], ["4a4640", "56524c"], 2011, 2, 2, 0.25, 0.4)),
    ("plank_floor", "Plank floor: light boards, tight joints", planks(["8a6a44", "a07e52", "b49262", "c6a472"], 2021, 4)),
    ("weathered_boards", "Weathered boards: silvered grey timber", planks(["5a5650", "6e6a62", "847e74", "9a948a"], 2031, 4, weather=1.0)),
    ("thatch", "Thatch: straw laid in slanting courses", straw(["6a5428", "84683a", "9e7e48", "b8965a"], 2041)),
    ("adobe_brick", "Adobe brick: sun-baked earth blocks", bricks(["8a6446", "9a7252", "aa805e", "b88e6a"], ["6a4c36", "7a583e"], 2051, 2, 4, 0.5, 0.8)),
    ("terracotta_tile", "Terracotta tile: fired clay squares", tiles(["8a4228", "a0502e", "b45e36", "c46e42"], ["4a2a1c", "5a3422"], 2061)),
    ("whitewash", "Whitewash: lime-washed wall, a stain or two", plaster(["b8b4a8", "c8c4b8", "d6d2c6", "e2dfd4"], ["8a8478", "9a9488"], 2071)),
    ("fired_brick", "Fired brick: deep red bricks, grey mortar", bricks(["6a2a1e", "7e3424", "92402c", "a44c34"], ["6a6660", "7a766e"], 2081, 2, 4, 0.4, 0.6)),
    ("cobble_path", "Cobble path: worn flat cobbles", stones(["5a5852", "6a6860", "7a776e", "8a867c"], 2091, 10, 3.0, ("2e2a24", "3a352e"), 0.3, 0.5)),
]

INDUSTRY = [
    ("steel_plate", "Steel plate: dark riveted panels", plate(["2a2e32", "34393e", "3e444a", "4a5056", "565c62"], 3001, 2)),
    ("floor_grate", "Floor grate: a dark lattice of bars", grate(["3a3e42", "4a4f54", "5c6268"], ["0a0b0c", "121416"], 3011)),
    ("enamel_panel", "Enamel panel: sage-grey enamel, fine seams", plate(["3f4e46", "4c5d54", "5b6d63", "6c7f74"], 3021, 1, False)),
    ("enamel_panel_dark", "Dark enamel panel: deep green-grey", plate(["1e2622", "28332d", "334039", "3f4e46"], 3031, 1, False)),
    ("concrete", "Concrete: grey, faintly pitted, form lines", soil(["5e5e5a", "6a6a66", "767672", "82827e"], 3041, 0.3, ("4a4a46", "8e8e8a"))),
    ("corrugated_sheet", "Corrugated sheet: ribbed zinc", ribs(["3a4046", "474e55", "555d65", "646c75", "747d86"], 3051)),
]

STRANGE = [
    ("pulse_crystal", "Pulse crystal: amber cells lit from within", lattice(["2a1e10", "3a2a16"], ["6a4a1e", "a8742a", "d8a040", "ffd890"], 4001)),
    ("timeworn_stone", "Timeworn stone: grey stone crossed by faint pale glowing bands",
     streaked(speckle(["4a4c50", "585a5e", "66686c", "74767a"], ["3a3c40", "7a7c80"], 4011, 0.5, 0.25, 1.8), ["9aa4b0", "c8d2dc", "e8eef4"], 4012)),
    ("void_slate", "Void slate: near-black stone with faint pinpricks of light",
     spores(strata(["0c0c10", "121218", "18181e", "1e1e26"], 4021, 6, 0.3), ["8a8aa0", "c8c8e0"], 4022, 0.6)),
    ("essence_moss", "Essence moss: soft moss with glowing spores",
     spores(soil(["22301a", "2a3a20", "324426", "3a4e2c"], 4031, 0.0), ["9ab870", "c8e0a0", "e8f4d0"], 4032, 0.55)),
]

FAMILIES = [("land", LAND), ("building", BUILDING), ("industry", INDUSTRY), ("strange", STRANGE)]


def main():
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "textures", "batch_sept.vtex")
    out = ["# batch_sept.vtex -- a trial batch of materials to sort through, 32 px, with height/shine/glow maps.",
           "# Generated by tools/batch_textures.py (families: land, building, industry, strange).", ""]
    for fam, mats in FAMILIES:
        out.append("# ---- %s ----" % fam)
        for name, note, mat in mats:
            out += render(name, note, mat, 32, False)
    with open(out_path, "w") as f:
        f.write("\n".join(out))
    print("wrote", os.path.normpath(out_path), "-", sum(len(m) for _, m in FAMILIES), "textures")


if __name__ == "__main__":
    main()
