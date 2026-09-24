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

static const int MAX_SHAPE_POLYS = 16;
static const int MAX_SHAPE_BOXES = 4;

// The shape's polygons / collision boxes for a block with this state
// (facing, and STATE_UPPER for slabs), already oriented. Return count.
int ShapePolys(BlockShape shape, uint8_t state, ShapePoly* out);
int ShapeBoxes(BlockShape shape, uint8_t state, ShapeBox* out);
