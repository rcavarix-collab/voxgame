// shapes.cpp -- see shapes.h.

#include "shapes.h"
#include <initializer_list>

namespace {

const int S = SHAPE_UNITS;

// Texture projection for an axis-aligned face, matching the cube
// mesher's orientation (side textures upright, row 0 at the top).
void ProjectUV(int face, ShapeVertex& p) {
    switch (face) {
    case FACE_POS_X: p.u = p.z;           p.v = (uint8_t)(S - p.y); break;
    case FACE_NEG_X: p.u = (uint8_t)(S - p.z); p.v = (uint8_t)(S - p.y); break;
    case FACE_POS_Y: p.u = p.x;           p.v = (uint8_t)(S - p.z); break;
    case FACE_NEG_Y: p.u = p.x;           p.v = p.z; break;
    case FACE_POS_Z: p.u = (uint8_t)(S - p.x); p.v = (uint8_t)(S - p.y); break;
    default:         p.u = p.x;           p.v = (uint8_t)(S - p.y); break;
    }
}

// The cube mesher's corner order per face (unit corners 0/1).
const int kCorners[FACE_COUNT][4][3] = {
    { {1,0,0}, {1,1,0}, {1,1,1}, {1,0,1} },
    { {0,0,1}, {0,1,1}, {0,1,0}, {0,0,0} },
    { {0,1,0}, {0,1,1}, {1,1,1}, {1,1,0} },
    { {0,0,1}, {0,0,0}, {1,0,0}, {1,0,1} },
    { {1,0,1}, {1,1,1}, {0,1,1}, {0,0,1} },
    { {0,0,0}, {0,1,0}, {1,1,0}, {1,0,0} },
};

// Six faces of a box, each marked as lying on the cell boundary or not.
int BoxFaces(const ShapeBox& b, ShapePoly* out) {
    int n = 0;
    for (int f = 0; f < FACE_COUNT; f++) {
        ShapePoly& p = out[n++];
        p.count = 4;
        p.texFace = (uint8_t)f;
        p.shade = (uint8_t)f;
        for (int k = 0; k < 4; k++) {
            const int* c = kCorners[f][k];
            ShapeVertex& v = p.v[k];
            v.x = c[0] ? b.x1 : b.x0; v.y = c[1] ? b.y1 : b.y0; v.z = c[2] ? b.z1 : b.z0;
            ProjectUV(f, v);
        }
        bool onEdge = (f == FACE_POS_X && b.x1 == S) || (f == FACE_NEG_X && b.x0 == 0) ||
                      (f == FACE_POS_Y && b.y1 == S) || (f == FACE_NEG_Y && b.y0 == 0) ||
                      (f == FACE_POS_Z && b.z1 == S) || (f == FACE_NEG_Z && b.z0 == 0);
        p.boundary = onEdge ? (int8_t)f : (int8_t)-1;
    }
    return n;
}

ShapeVertex V(int x, int y, int z) { return { (uint8_t)x, (uint8_t)y, (uint8_t)z, 0, 0 }; }

ShapePoly Poly(int face, int shade, int boundary, std::initializer_list<ShapeVertex> vs) {
    ShapePoly p = {};
    p.count = (uint8_t)vs.size();
    int i = 0;
    for (const ShapeVertex& v : vs) { p.v[i] = v; ProjectUV(face, p.v[i]); i++; }
    p.texFace = (uint8_t)face; p.shade = (uint8_t)shade; p.boundary = (int8_t)boundary;
    return p;
}

// Canonical ramp: rises toward +Z (full height at z = 8).
int CanonicalRamp(ShapePoly* out) {
    int n = 0;
    out[n++] = Poly(FACE_NEG_Y, FACE_NEG_Y, FACE_NEG_Y, { V(0,0,8), V(0,0,0), V(8,0,0), V(8,0,8) });
    out[n++] = Poly(FACE_POS_Z, FACE_POS_Z, FACE_POS_Z, { V(8,0,8), V(8,8,8), V(0,8,8), V(0,0,8) });
    out[n++] = Poly(FACE_NEG_X, FACE_NEG_X, FACE_NEG_X, { V(0,0,8), V(0,8,8), V(0,0,0) });
    out[n++] = Poly(FACE_POS_X, FACE_POS_X, FACE_POS_X, { V(8,0,0), V(8,8,8), V(8,0,8) });
    // The slope takes the top texture, laid along the incline.
    ShapePoly slope = {};
    slope.count = 4; slope.texFace = FACE_POS_Y; slope.shade = SHADE_SLOPE_UP; slope.boundary = -1;
    slope.v[0] = { 0, 0, 0, 0, 8 }; slope.v[1] = { 0, 8, 8, 0, 0 };
    slope.v[2] = { 8, 8, 8, 8, 0 }; slope.v[3] = { 8, 0, 0, 8, 8 };
    out[n++] = slope;
    return n;
}

// Pyramid (apexY > baseY) or funnel (apexY < baseY): a full square at
// baseY and four triangles meeting at the centre.
int Pointed(int baseY, int apexY, ShapePoly* out) {
    int n = 0;
    bool up = apexY > baseY;
    int baseFace = up ? FACE_NEG_Y : FACE_POS_Y;
    int baseBoundary = (baseY == 0 || baseY == S) ? baseFace : -1;
    if (up) out[n++] = Poly(baseFace, baseFace, baseBoundary, { V(0,baseY,8), V(0,baseY,0), V(8,baseY,0), V(8,baseY,8) });
    else    out[n++] = Poly(baseFace, baseFace, baseBoundary, { V(0,baseY,0), V(0,baseY,8), V(8,baseY,8), V(8,baseY,0) });
    int shade = up ? SHADE_SLOPE_UP : SHADE_SLOPE_DOWN;
    ShapeVertex a = V(4, apexY, 4);
    out[n++] = Poly(FACE_NEG_Z, shade, -1, { V(0,baseY,0), a, V(8,baseY,0) });
    out[n++] = Poly(FACE_POS_Z, shade, -1, { V(8,baseY,8), a, V(0,baseY,8) });
    out[n++] = Poly(FACE_NEG_X, shade, -1, { V(0,baseY,8), a, V(0,baseY,0) });
    out[n++] = Poly(FACE_POS_X, shade, -1, { V(8,baseY,0), a, V(8,baseY,8) });
    return n;
}

// Rotation about the vertical axis taking +Z to `facing` (horizontal).
void RotateY(BlockFace facing, uint8_t& x, uint8_t& z) {
    int ox = x, oz = z;
    switch (facing) {
    case FACE_POS_X: x = (uint8_t)oz;       z = (uint8_t)(S - ox); break;
    case FACE_NEG_Z: x = (uint8_t)(S - ox); z = (uint8_t)(S - oz); break;
    case FACE_NEG_X: x = (uint8_t)(S - oz); z = (uint8_t)ox; break;
    default: break;
    }
}
int RotateFaceY(BlockFace facing, int f) {
    if (f == FACE_POS_Y || f == FACE_NEG_Y) return f;
    static const int toX[FACE_COUNT]  = { FACE_NEG_Z, FACE_POS_Z, FACE_POS_Y, FACE_NEG_Y, FACE_POS_X, FACE_NEG_X };
    static const int toNZ[FACE_COUNT] = { FACE_NEG_X, FACE_POS_X, FACE_POS_Y, FACE_NEG_Y, FACE_NEG_Z, FACE_POS_Z };
    static const int toNX[FACE_COUNT] = { FACE_POS_Z, FACE_NEG_Z, FACE_POS_Y, FACE_NEG_Y, FACE_NEG_X, FACE_POS_X };
    switch (facing) {
    case FACE_POS_X: return toX[f];
    case FACE_NEG_Z: return toNZ[f];
    case FACE_NEG_X: return toNX[f];
    default: return f;
    }
}

BlockFace HorizontalFacing(uint8_t state) {
    BlockFace f = StateFacing(state);
    return (f == FACE_POS_Y || f == FACE_NEG_Y) ? FACE_POS_Z : f;
}

} // namespace

int ShapeBoxes(BlockShape shape, uint8_t state, ShapeBox* out) {
    switch (shape) {
    case SHAPE_SLAB:
        out[0] = (state & STATE_UPPER) ? ShapeBox{ 0, 4, 0, 8, 8, 8 } : ShapeBox{ 0, 0, 0, 8, 4, 8 };
        return 1;
    case SHAPE_TUBE: {
        BlockFace f = StateFacing(state);
        if (f == FACE_POS_X || f == FACE_NEG_X) out[0] = { 0, 3, 3, 8, 5, 5 };
        else if (f == FACE_POS_Y || f == FACE_NEG_Y) out[0] = { 3, 0, 3, 5, 8, 5 };
        else out[0] = { 3, 3, 0, 5, 5, 8 };
        return 1;
    }
    case SHAPE_RAMP: {
        // Two steps, so the player's half-block step-up walks it.
        ShapeBox steps[2] = { { 0, 0, 0, 8, 4, 8 }, { 0, 4, 4, 8, 8, 8 } };
        BlockFace f = HorizontalFacing(state);
        for (int i = 0; i < 2; i++) {
            uint8_t ax = steps[i].x0, az = steps[i].z0, bx = steps[i].x1, bz = steps[i].z1;
            RotateY(f, ax, az); RotateY(f, bx, bz);
            out[i] = { (uint8_t)(ax < bx ? ax : bx), steps[i].y0, (uint8_t)(az < bz ? az : bz),
                       (uint8_t)(ax < bx ? bx : ax), steps[i].y1, (uint8_t)(az < bz ? bz : az) };
        }
        return 2;
    }
    case SHAPE_PYRAMID:      out[0] = { 0, 0, 0, 8, 4, 8 }; out[1] = { 2, 4, 2, 6, 8, 6 }; return 2;
    case SHAPE_PYRAMID_HALF: out[0] = { 0, 0, 0, 8, 2, 8 }; out[1] = { 2, 2, 2, 6, 4, 6 }; return 2;
    case SHAPE_FUNNEL:       out[0] = { 0, 4, 0, 8, 8, 8 }; out[1] = { 2, 0, 2, 6, 4, 6 }; return 2;
    case SHAPE_FUNNEL_HALF:  out[0] = { 0, 6, 0, 8, 8, 8 }; out[1] = { 2, 4, 2, 6, 6, 6 }; return 2;
    default:                 out[0] = { 0, 0, 0, 8, 8, 8 }; return 1;
    }
}

int ShapePolys(BlockShape shape, uint8_t state, ShapePoly* out) {
    switch (shape) {
    case SHAPE_CUBE: case SHAPE_SLAB: case SHAPE_TUBE: {
        ShapeBox b; ShapeBoxes(shape, state, &b);
        return BoxFaces(b, out);
    }
    case SHAPE_RAMP: {
        int n = CanonicalRamp(out);
        BlockFace f = HorizontalFacing(state);
        for (int i = 0; i < n; i++) {
            ShapePoly& p = out[i];
            for (int k = 0; k < p.count; k++) RotateY(f, p.v[k].x, p.v[k].z);
            bool slope = p.shade == SHADE_SLOPE_UP;
            p.texFace = (uint8_t)RotateFaceY(f, p.texFace);
            if (!slope) {
                p.shade = p.texFace;
                for (int k = 0; k < p.count; k++) ProjectUV(p.texFace, p.v[k]); // keep sides upright
            }
            if (p.boundary >= 0) p.boundary = (int8_t)RotateFaceY(f, p.boundary);
        }
        return n;
    }
    case SHAPE_PYRAMID:      return Pointed(0, 8, out);
    case SHAPE_PYRAMID_HALF: return Pointed(0, 4, out);
    case SHAPE_FUNNEL:       return Pointed(8, 0, out);
    case SHAPE_FUNNEL_HALF:  return Pointed(8, 4, out);
    case SHAPE_CARD:         return 0; // a plant card is built by the mesher itself (4.14)
    }
    return 0;
}
