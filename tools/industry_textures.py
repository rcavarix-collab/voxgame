#!/usr/bin/env python3
"""Generates assets/textures/industry.vtex: the pulse-logistics set (DESIGN.md Part VI).

One material family, so the harvester, pipes and stores read as belonging
together: sage-grey enamel, pewter trim, brushed-zinc pipes, and the pulse's own warm
amber (clockwise pulse's blue, anticlockwise pulse's red) as the only
accent -- the colours the beads in the pipes and the store's heaps use.
Drawn with the natural set's field helpers (tools/natural_textures.py) and
written by its renderer: 32 px, with height, shine and glow maps.

Pipes are textured along their length: u runs along the pipe (0..16 is one
block), v across it, and a pipe's faces show the middle quarter (v 6..10),
so the sheen sits there; a ring marks each block's joint.

  python3 tools/industry_textures.py          # writes assets/textures/industry.vtex

Deterministic: rerunning reproduces the file exactly.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from natural_textures import T, fbm, lattice_noise, ramp, sstep, wrapd, render  # noqa: E402

# Not black and gold (owner: handsome but cliche): sage-grey enamel housings,
# pewter trim, brushed-zinc pipes -- quiet, so the pulse's three colours are
# the only strong ones.
ENAMEL = ["1e2622", "28332d", "334039", "3f4e46", "4c5d54", "5b6d63", "6c7f74", "7f9387"]
PEWTER = ["4a4f54", "626870", "7c838a", "969da4", "b2b8be", "d0d5d9"]
ZINC = ["2e3338", "3a4046", "474e55", "555d65", "646c75", "747d86", "878f98", "9ca4ac"]
ACCENT = {0: ["a8742a", "d8a040", "ffcf70", "ffe6a8"],     # plain pulse: warm yellow-amber
          1: ["0e2a86", "1840c8", "2f64ff", "8aa8ff"],     # clockwise: a strong blue
          -1: ["7a0e10", "c01a1e", "f0302c", "ff9a8e"]}    # anticlockwise: a strong red


def brushed(seed):
    """Fine grain running along u (long along, fine across)."""
    return fbm(seed, cells=(2, 4, 8), aspect=4.0)  # four times finer across than along


def m_pipe(spin):
    grain = brushed(900 + spin)
    accent = ACCENT[spin]
    def f(u, v):
        t = grain(u, v)
        # A round-ish sheen across the face (its middle is v 8).
        across = (v - 8.0) / 2.0
        sheen = max(0.0, 1.0 - across * across)
        seam = wrapd(u, 0.0)
        if seam < 0.9:  # the ring at each block's joint: enamel, or a twisted pipe's accent (its thread wears this too)
            edge = seam > 0.55
            c = ramp(ENAMEL[3:] if spin == 0 else accent, (0.35 if edge else 0.6) + 0.35 * sheen)
            return c, 1, 0.55 if edge else 0.85, 0.8, 0.1 if spin else 0
        if seam < 1.3 and spin == 0:  # a thin accent line beside the ring
            return ramp(accent[:3], 0.4 + 0.5 * sheen), 1, 0.6, 0.6, 0.15
        if spin != 0:
            # Rifling that winds with the thread: the pipe's v runs the same
            # way round on every face, so lines of u - spin*v slant one hand
            # all the way round -- one turn a block, like the thread (a face
            # is 4 units round and a quarter block long). Period 4 across the
            # 4-unit faces, so the lines meet at each edge.
            s = (u - spin * v) % 4.0
            if s < 0.8:
                return ramp(accent, 0.25 + 0.6 * sheen), 1, 0.6, 0.6, 0.12
        c = ramp(ZINC, 0.2 + 0.3 * t + 0.45 * sheen)
        return c, 1, 0.45 + 0.1 * t, 0.35 + 0.35 * sheen, 0
    return f


def frame(u, v, width=1.0):
    return min(u, T - u, v, T - v) < width


def rivet(u, v, points, r=0.55):
    return any(math.hypot(u - x, v - y) < r for x, y in points)


CORNERS = [(1.6, 1.6), (14.4, 1.6), (1.6, 14.4), (14.4, 14.4)]


def plate_base(seed):
    grain = brushed(seed)
    blotch = fbm(seed + 1, cells=(2, 4))
    def base(u, v):
        t = 0.6 * grain(u, v) + 0.4 * blotch(u, v)
        return ramp(ENAMEL, 0.3 + 0.4 * t), 1, 0.45 + 0.1 * t, 0.2, 0
    return base


def m_harvester_side():
    base = plate_base(910)
    def f(u, v):
        if frame(u, v):
            edge = min(u, T - u, v, T - v)
            return ramp(PEWTER, 0.3 + 0.4 * sstep(0.0, 1.0, edge)), 1, 0.7, 0.75, 0
        if rivet(u, v, CORNERS):
            return ramp(PEWTER, 0.8), 1, 0.9, 0.9, 0
        # The intake grille: horizontal slots, warm light inside.
        if 3.0 <= u <= 13.0 and 4.5 <= v <= 11.5:
            slot = (v - 4.5) % 1.75
            if slot < 0.85:
                glow = 0.35 + 0.25 * lattice_noise(911, 4)(u, v)
                return ramp(ACCENT[0][:3], 0.2 + 0.5 * glow), 1, 0.1, 0.1, glow
            return ramp(ENAMEL, 0.55), 1, 0.7, 0.55, 0
        if 2.2 <= u <= 13.8 and 3.7 <= v <= 12.3:  # a recessed lip round the grille
            return ramp(ENAMEL, 0.12), 1, 0.25, 0.2, 0
        return base(u, v)
    return f


def m_harvester_top():
    base = plate_base(920)
    def f(u, v):
        if frame(u, v):
            return ramp(PEWTER, 0.5), 1, 0.7, 0.75, 0
        if rivet(u, v, CORNERS):
            return ramp(PEWTER, 0.8), 1, 0.9, 0.9, 0
        r = math.hypot(u - 8.0, v - 8.0)
        a = math.atan2(v - 8.0, u - 8.0)
        if r < 2.6:  # the core it gathers into
            return ramp(ACCENT[0], 1.0 - r / 2.6 * 0.6), 1, 0.35, 0.3, 0.9 - 0.25 * r / 2.6
        if r < 5.2:  # vanes between core and ring, light between them
            vane = abs(((a / (2 * math.pi)) * 8.0) % 1.0 - 0.5) < 0.14
            if vane:
                return ramp(ENAMEL, 0.6 + 0.1 * (r - 2.6)), 1, 0.75, 0.6, 0
            return ramp(ACCENT[0][:3], 0.55 - 0.08 * (r - 2.6)), 1, 0.15, 0.1, 0.45 - 0.12 * (r - 2.6)
        if r < 6.2:  # the pewter ring
            return ramp(PEWTER, 0.5 + 0.4 * (1 - abs(r - 5.7) / 0.5)), 1, 0.85, 0.85, 0
        return base(u, v)
    return f


def m_store_side():
    base = plate_base(930)
    bolts = [(x + 0.5, y) for x in (2.0, 6.0, 10.0, 14.0) for y in (1.2, 14.8)]
    def f(u, v):
        if v < 2.2 or v > T - 2.2:  # riveted pewter bands, top and bottom
            if rivet(u - 0.5, v, bolts, 0.5):
                return ramp(PEWTER, 0.9), 1, 0.95, 0.9, 0
            edge = min(v, T - v)
            return ramp(PEWTER, 0.35 + 0.3 * sstep(0.0, 2.2, edge)), 1, 0.75, 0.8, 0
        # The sight glass: a tall window onto what it holds.
        if 6.3 <= u <= 9.7 and 3.4 <= v <= 12.6:
            if u < 6.8 or u > 9.2 or v < 3.9 or v > 12.1:
                return ramp(PEWTER, 0.55), 1, 0.8, 0.8, 0
            depth = (v - 3.9) / 8.2
            return ramp(ACCENT[0], 0.25 + 0.45 * depth), 1, 0.2, 0.9, 0.25 + 0.3 * depth
        return base(u, v)
    return f


def m_store_top():
    base = plate_base(940)
    def f(u, v):
        if frame(u, v, 0.8):
            return ramp(PEWTER, 0.45), 1, 0.7, 0.75, 0
        r = math.hypot(u - 8.0, v - 8.0)
        a = math.atan2(v - 8.0, u - 8.0)
        if r < 1.4:  # the hatch's handle
            return ramp(PEWTER, 0.9 - 0.3 * r / 1.4), 1, 0.95, 0.9, 0
        if r < 6.0:  # the lid
            if abs(r - 5.0) < 0.45 and abs(((a / (2 * math.pi)) * 8.0) % 1.0 - 0.5) < 0.18:  # eight bolts
                return ramp(PEWTER, 0.85), 1, 0.9, 0.9, 0
            return ramp(ENAMEL, 0.45 + 0.15 * (1 - r / 6.0)), 1, 0.6, 0.55, 0
        if r < 6.6:
            return ramp(ENAMEL, 0.05), 1, 0.2, 0.1, 0
        return base(u, v)
    return f


def m_plate():
    base = plate_base(950)
    def f(u, v):
        if rivet(u, v, CORNERS):
            return ramp(PEWTER, 0.7), 1, 0.85, 0.8, 0
        return base(u, v)
    return f


TEXTURES = [
    ("pulse_pipe", "Pulse pipe: brushed zinc along its length, an enamel ring at each joint", m_pipe(0)),
    ("pulse_pipe_cw", "Clockwise pipe: the same metal, a blue helix slanting with its twist", m_pipe(1)),
    ("pulse_pipe_ccw", "Anticlockwise pipe: the same metal, a red helix slanting the other way", m_pipe(-1)),
    ("pulse_harvester_side", "Harvester, sides: a pewter-framed enamel housing with a warm-lit intake grille", m_harvester_side()),
    ("pulse_harvester_top", "Harvester, top: a vaned intake round a glowing core", m_harvester_top()),
    ("pulse_store_side", "Pulse store, sides: a banded vessel with a sight glass", m_store_side()),
    ("pulse_store_top", "Pulse store, top: a bolted hatch", m_store_top()),
    ("pulse_plate", "Pulse machinery, underside: a plain riveted plate", m_plate()),
]


def main():
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "textures", "industry.vtex")
    out = ["# industry.vtex -- the pulse-logistics set, 32 px per block, with height/shine/glow maps.",
           "# Generated by tools/industry_textures.py; rerun it after tweaking a material.", ""]
    for name, note, mat in TEXTURES:
        out += render(name, note, mat, 32, False)
    with open(out_path, "w") as f:
        f.write("\n".join(out))
    print("wrote", os.path.normpath(out_path))


if __name__ == "__main__":
    main()
