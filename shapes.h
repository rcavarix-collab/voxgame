// shapes.h
//
// Non-cube block shapes (DESIGN.md 4.4) -- the primitives from the
// original Prismative.cpp prototype (slab, ramp, tube, pyramid, half
// pyramid, funnel, half funnel), baked into the chunk mesh like cubes
// rather than drawn per instance. Each shape is authored once in a
// canonical orientation, in 1/8-block units, and rotated by the block's
// state byte. Also supplies collision boxes, so a slab or ramp can be
// walked on. Pure C++, tested natively.

#pragma once

#include "blocks.h"
#include <cstdint>

// Everything here is in 1/8-block units: 0..8 across a cell.
static const int SHAPE_UNITS = 8;

struct ShapeVertex { uint8_t x, y, z, u, v; };

// Shade classes: the six axis faces (BlockFace), plus faces that slope
// up (ramp, pyramid sides) or down (funnel sides).
enum : uint8_t { SHADE_SLOPE_UP = 6, SHADE_SLOPE_DOWN = 7 };

struct ShapePoly {
    uint8_t count;        // 3 or 4
    ShapeVertex v[4];
    uint8_t texFace;      // BlockFace whose texture this polygon uses
    uint8_t shade;        // BlockFace or SHADE_SLOPE_*
    int8_t boundary;      // BlockFace this polygon lies flat on (hidden by a full neighbour there), or -1
};

struct ShapeBox { uint8_t x0, y0, z0, x1, y1, z1; };

static const int MAX_SHAPE_POLYS = 48;
static const int MAX_SHAPE_BOXES = 4;

// The shape's polygons / collision boxes for a block with this state
// (facing, and STATE_UPPER for slabs), already oriented. Return count.
// `variant` (0-3, from the cell's position: ShapeVariant) turns the
// faceted props (4.15) a quarter turn at a time about their anchor, so a
// scatter of the same prop never lines up.
int ShapePolys(BlockShape shape, uint8_t state, ShapePoly* out, int variant = 0);
int ShapeBoxes(BlockShape shape, uint8_t state, ShapeBox* out);
// A pulse pipe (Part VI) with `joined` = the faces (bit f = BlockFace f)
// whose neighbour it joins: a straight bar for a run, a node with arms
// for a bend or junction. A pipe joined on one side (or none: then along
// its placed axis) is an open end, and its mouth gets a collar. A
// twisted pipe (`twist` +1 clockwise, -1 anticlockwise: PipeTwist) wears
// a raised thread winding round its straight runs, that way.
int PipePolys(uint8_t joined, uint8_t state, ShapePoly* out, int twist = 0);
// Which of a pipe's six faces open onto something it joins.
static inline uint8_t PipeJoinMask(const BlockID neighbour[FACE_COUNT]) {
    uint8_t m = 0;
    for (int f = 0; f < FACE_COUNT; f++) if (BlockJoinsPipe(neighbour[f])) m |= (uint8_t)(1u << f);
    return m;
}
// The open ends of a pipe joined on `joined` (see PipePolys), as a face mask.
static inline uint8_t PipeMouths(uint8_t joined, uint8_t state) {
    int n = 0, only = 0;
    for (int f = 0; f < FACE_COUNT; f++) if (joined & (1u << f)) { n++; only = f; }
    if (n == 1) return (uint8_t)(1u << (only ^ 1)); // faces pair up: f ^ 1 is the opposite side
    if (n == 0) { int f = StateFacing(state); return (uint8_t)((1u << f) | (1u << (f ^ 1))); }
    return 0;
}
static inline int ShapeVariant(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (int)(h & 3u);
}
