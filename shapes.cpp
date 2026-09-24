// shapes.cpp -- see shapes.h.

#include "shapes.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numeric>
#include <vector>

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


// ---------------------------------------------------------------------
// Faceted props (DESIGN.md 4.15). Each is authored once, anchored on its
// bottom face and growing up (+Y), leaning toward +Z, as a few convex
// parts given by their corner points; a small exact integer convex hull
// turns each part into outward-wound faces. Placing the block orients the
// whole thing: the clicked face becomes the anchor (a mound on a floor, a
// drape from a ceiling, a snag or ledge drooping off a wall).
// ---------------------------------------------------------------------

struct P3 { int x, y, z; };
inline P3 Sub(P3 a, P3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline P3 Cross(P3 a, P3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline int Dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline bool Eq(P3 a, P3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

struct CFace { std::vector<P3> v; P3 n; }; // outward-wound, outward normal

// Convex hull of a few integer points: every plane through three points
// with all the others on one side is a face; the points on it, ordered
// around it, are that face's polygon. O(n^4), exact, run once per shape.
void Hull(const std::vector<P3>& pts, std::vector<CFace>& out) {
    std::vector<P3> u;
    u.reserve(pts.size());
    for (P3 p : pts) { bool dup = false; for (P3 q : u) dup = dup || Eq(p, q); if (!dup) u.push_back(p); }
    int n = (int)u.size();
    std::vector<P3> seen;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            for (int k = j + 1; k < n; k++) {
                P3 nn = Cross(Sub(u[j], u[i]), Sub(u[k], u[i]));
                if (nn.x == 0 && nn.y == 0 && nn.z == 0) continue;
                int d = Dot(nn, u[i]), pos = 0, neg = 0;
                for (int m = 0; m < n; m++) { int sd = Dot(nn, u[m]) - d; pos += sd > 0; neg += sd < 0; }
                if (pos && neg) continue;
                if (pos) nn = { -nn.x, -nn.y, -nn.z };           // outward
                int g = std::gcd(std::gcd(std::abs(nn.x), std::abs(nn.y)), std::abs(nn.z));
                P3 key = { nn.x / g, nn.y / g, nn.z / g };
                bool dup = false; for (P3 q : seen) dup = dup || Eq(q, key);
                if (dup) continue;
                seen.push_back(key);
                CFace f; f.n = key;
                int dd = Dot(key, u[i]);
                for (int m = 0; m < n; m++) if (Dot(key, u[m]) == dd) f.v.push_back(u[m]);
                // Order around the centroid, counterclockwise seen from outside.
                double cx = 0, cy = 0, cz = 0;
                for (P3 p : f.v) { cx += p.x; cy += p.y; cz += p.z; }
                cx /= f.v.size(); cy /= f.v.size(); cz /= f.v.size();
                // A basis in the plane: e1 = (first point - centroid), e2 = n x e1.
                double e1x = f.v[0].x - cx, e1y = f.v[0].y - cy, e1z = f.v[0].z - cz;
                double e2x = key.y * e1z - key.z * e1y, e2y = key.z * e1x - key.x * e1z, e2z = key.x * e1y - key.y * e1x;
                std::vector<std::pair<double, P3>> ang;
                for (P3 p : f.v) {
                    double px = p.x - cx, py = p.y - cy, pz = p.z - cz;
                    ang.push_back({ std::atan2(px * e2x + py * e2y + pz * e2z, px * e1x + py * e1y + pz * e1z), p });
                }
                std::sort(ang.begin(), ang.end(), [](const std::pair<double, P3>& a, const std::pair<double, P3>& b) { return a.first < b.first; });
                f.v.clear();
                for (auto& a : ang) f.v.push_back(a.second);
                // Winding: (v1 - v0) x (v2 - v0) must point outward.
                if (f.v.size() >= 3 && Dot(Cross(Sub(f.v[1], f.v[0]), Sub(f.v[2], f.v[0])), key) < 0) std::reverse(f.v.begin(), f.v.end());
                out.push_back(f);
            }
}

// An octagonal ring: half-width w, corners cut by c (0 < c < w), centred on (cx, cz).
void Ring(std::vector<P3>& pts, int y, int cx, int cz, int w, int c) {
    const int a = w - c;
    const P3 r[8] = { { cx - a, y, cz - w }, { cx + a, y, cz - w }, { cx + w, y, cz - a }, { cx + w, y, cz + a },
                      { cx + a, y, cz + w }, { cx - a, y, cz + w }, { cx - w, y, cz + a }, { cx - w, y, cz - a } };
    for (const P3& p : r) pts.push_back(p);
}
void Box(std::vector<P3>& pts, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int i = 0; i < 8; i++) pts.push_back({ i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0 });
}

// Appends points (assigning a braced list trips a GCC 13 -Wnonnull false positive).
void Add(std::vector<P3>& p, std::initializer_list<P3> l) { for (P3 q : l) p.push_back(q); }

enum Anchor : uint8_t { ANCHOR_CLICKED, ANCHOR_YAW, ANCHOR_UPRIGHT };

struct Canon { std::vector<CFace> faces; Anchor anchor; bool turns; };

void AddHull(Canon& c, const std::vector<P3>& pts) { Hull(pts, c.faces); }

// An explicit polygon of a non-convex part, wound to face `out`.
void AddFace(Canon& c, std::vector<P3> v, P3 out) {
    CFace f; f.v = std::move(v);
    P3 n = Cross(Sub(f.v[1], f.v[0]), Sub(f.v[2], f.v[0]));
    if (Dot(n, out) < 0) { std::reverse(f.v.begin(), f.v.end()); n = { -n.x, -n.y, -n.z }; }
    f.n = n;
    c.faces.push_back(f);
}

Canon BuildCanon(BlockShape shape) {
    Canon c; c.anchor = ANCHOR_CLICKED; c.turns = true;
    std::vector<P3> p;
    switch (shape) {
    case SHAPE_SWELL_MOUND:   // a low clump: flat-topped, a touch of lean
        Ring(p, 0, 4, 4, 3, 1); Ring(p, 2, 4, 4, 3, 1); Ring(p, 4, 4, 5, 2, 1);
        AddHull(c, p); break;
    case SHAPE_SWELL_BULB:    // pinched base, swelling and leaning well off-centre
        Ring(p, 0, 4, 4, 2, 1); Ring(p, 2, 4, 5, 3, 1); Ring(p, 4, 4, 6, 2, 1); p.push_back({ 4, 5, 6 });
        AddHull(c, p); break;
    case SHAPE_SWELL_KNOB:    // tall and pinched: a bud, a root knuckle
        Ring(p, 0, 4, 4, 2, 1); Ring(p, 3, 4, 4, 3, 1); Ring(p, 6, 4, 5, 2, 1); p.push_back({ 4, 7, 5 });
        AddHull(c, p); break;
    case SHAPE_SWELL_BOULDER: // wide and low, worn round
        Ring(p, 0, 4, 4, 4, 2); Ring(p, 2, 4, 4, 4, 2); Ring(p, 3, 4, 4, 3, 2);
        AddHull(c, p); break;
    case SHAPE_SWELL_BREAKER: // barely breaking the surface
        Ring(p, 0, 4, 4, 4, 2); Ring(p, 1, 4, 4, 3, 2);
        AddHull(c, p); break;
    case SHAPE_SHARD:         // one broken chunk: a skewed base, a tilted jagged top
        Add(p, { { 1, 0, 1 }, { 6, 0, 0 }, { 8, 0, 5 }, { 4, 0, 8 }, { 0, 0, 5 },
              { 2, 5, 3 }, { 5, 3, 1 }, { 6, 2, 6 }, { 3, 6, 5 } });
        AddHull(c, p); break;
    case SHAPE_RIPPLE_LIP:    // on a water surface: a lapping ridge along the far (+Z) edge
        c.anchor = ANCHOR_YAW; c.turns = false;
        Add(p, { { 0, 0, 4 }, { 8, 0, 4 }, { 0, 0, 8 }, { 8, 0, 8 }, { 0, 1, 6 }, { 8, 1, 6 }, { 0, 1, 8 }, { 8, 1, 8 } });
        AddHull(c, p); break;
    case SHAPE_BEAM:          // a rafter rising toward +Z; cells chain corner to corner
        c.anchor = ANCHOR_YAW; c.turns = false;
        for (int x : { 3, 5 }) { p.push_back({ x, 0, 0 }); p.push_back({ x, 0, 1 }); p.push_back({ x, 7, 8 }); p.push_back({ x, 8, 8 }); p.push_back({ x, 8, 7 }); p.push_back({ x, 1, 0 }); }
        AddHull(c, p); break;
    case SHAPE_CORBEL:        // anchored on a wall (y = 0 is the wall; world-down is +Z)
        c.turns = false;
        Box(p, 2, 0, 0, 6, 6, 2); AddHull(c, p); p.clear();
        Box(p, 2, 0, 2, 6, 4, 4); AddHull(c, p); p.clear();
        Box(p, 2, 0, 4, 6, 2, 6); AddHull(c, p); break;
    case SHAPE_SHUTTER:       // a thin panel with three louvers
        c.turns = false;
        Box(p, 0, 0, 0, 8, 1, 8); AddHull(c, p);
        for (int z : { 1, 4, 7 }) { p.clear(); Box(p, 1, 1, z - 1, 7, 2, z); AddHull(c, p); }
        break;
    case SHAPE_CHIMNEY_CAP:   // stepped: neck, overhanging lip, crown, finial
        c.anchor = ANCHOR_UPRIGHT; c.turns = false;
        Box(p, 1, 0, 1, 7, 3, 7); AddHull(c, p); p.clear();
        Box(p, 0, 3, 0, 8, 5, 8); AddHull(c, p); p.clear();
        Box(p, 2, 5, 2, 6, 7, 6); AddHull(c, p); p.clear();
        Box(p, 3, 7, 3, 5, 8, 5); AddHull(c, p); break;
    case SHAPE_AWNING:        // off a wall: a plate sloping down (+Z) as it reaches out (+Y)
        c.turns = false;
        Add(p, { { 0, 0, 0 }, { 8, 0, 0 }, { 0, 0, 1 }, { 8, 0, 1 }, { 0, 6, 3 }, { 8, 6, 3 }, { 0, 6, 4 }, { 8, 6, 4 } });
        AddHull(c, p); break;
    case SHAPE_PIPE:          // octagonal, end to end along the axis
        c.turns = false;
        Ring(p, 0, 4, 4, 3, 1); Ring(p, 8, 4, 4, 3, 1); AddHull(c, p); break;
    case SHAPE_GEAR:          // a notched disc in relief, with a hub
        c.turns = false;
        Ring(p, 0, 4, 4, 3, 1); Ring(p, 1, 4, 4, 3, 1); AddHull(c, p); p.clear();
        Box(p, 3, 0, 0, 5, 1, 1); AddHull(c, p); p.clear();
        Box(p, 3, 0, 7, 5, 1, 8); AddHull(c, p); p.clear();
        Box(p, 0, 0, 3, 1, 1, 5); AddHull(c, p); p.clear();
        Box(p, 7, 0, 3, 8, 1, 5); AddHull(c, p); p.clear();
        Box(p, 3, 1, 3, 5, 2, 5); AddHull(c, p); break;
    case SHAPE_VENT:          // a narrow stack flaring at the top
        c.anchor = ANCHOR_UPRIGHT; c.turns = false;
        Ring(p, 0, 4, 4, 2, 1); Ring(p, 5, 4, 4, 2, 1); AddHull(c, p); p.clear();
        Ring(p, 5, 4, 4, 2, 1); Ring(p, 8, 4, 4, 4, 2); AddHull(c, p); break;
    case SHAPE_HOPPER: {      // open top: sloped outer walls, a rim, sloped inner walls to a floor
        c.anchor = ANCHOR_UPRIGHT; c.turns = false;
        const P3 up = { 0, 1, 0 };
        auto sq = [](int y, int a, int b) { return std::vector<P3>{ { a, y, a }, { b, y, a }, { b, y, b }, { a, y, b } }; };
        std::vector<P3> bot = sq(0, 2, 6), top = sq(8, 0, 8), rimIn = sq(8, 1, 7), floor = sq(4, 3, 5);
        AddFace(c, bot, { 0, -1, 0 });
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            P3 mid = { (top[i].x + top[j].x) - 8, 0, (top[i].z + top[j].z) - 8 }; // outward from the centre
            AddFace(c, { bot[i], bot[j], top[j], top[i] }, mid);
            AddFace(c, { top[i], top[j], rimIn[j], rimIn[i] }, up);
            P3 in = { -mid.x, 2, -mid.z };                                         // facing inward and up
            AddFace(c, { rimIn[i], rimIn[j], floor[j], floor[i] }, in);
        }
        AddFace(c, floor, up);
        break;
    }
    case SHAPE_STRUT:         // an X-brace in the x-y plane, turned to face the player
        c.anchor = ANCHOR_YAW; c.turns = false;
        for (int z : { 3, 5 }) { p.push_back({ 0, 0, z }); p.push_back({ 1, 0, z }); p.push_back({ 8, 7, z }); p.push_back({ 8, 8, z }); p.push_back({ 7, 8, z }); p.push_back({ 0, 1, z }); }
        AddHull(c, p); p.clear();
        for (int z : { 3, 5 }) { p.push_back({ 8, 0, z }); p.push_back({ 7, 0, z }); p.push_back({ 0, 7, z }); p.push_back({ 0, 8, z }); p.push_back({ 1, 8, z }); p.push_back({ 8, 1, z }); }
        AddHull(c, p); break;
    case SHAPE_CANOPY_CAP:    // a stalk, and a broad cap overhanging it (its underside shows)
        Ring(p, 0, 4, 4, 2, 1); Ring(p, 5, 4, 4, 2, 1); AddHull(c, p); p.clear();
        Ring(p, 4, 4, 4, 4, 2); Ring(p, 6, 4, 4, 4, 2); Ring(p, 8, 4, 4, 2, 1); AddHull(c, p); break;
    case SHAPE_COIL_STALK:    // a square stem rising, then curling over toward +Z and back in
        Box(p, 3, 0, 3, 5, 6, 5); AddHull(c, p); p.clear();
        Box(p, 3, 6, 3, 5, 8, 6); AddHull(c, p); p.clear();
        Box(p, 3, 5, 6, 5, 8, 8); AddHull(c, p); p.clear();
        Box(p, 3, 3, 5, 5, 5, 7); AddHull(c, p); break;
    default: break;
    }
    return c;
}

const Canon& GetCanon(BlockShape shape) {
    static const std::vector<Canon> all = [] {
        std::vector<Canon> v(SHAPE_COUNT);
        for (int s = SHAPE_SWELL_MOUND; s < SHAPE_COUNT; s++) if (ShapeIsProp(s)) v[s] = BuildCanon((BlockShape)s);
        return v;
    }();
    return all[shape];
}

// 3x3 integer rotation (entries -1/0/1), applied about the cell's centre.
struct M3 { int m[3][3]; };
P3 Mul(const M3& r, P3 v) {
    return { r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z,
             r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z,
             r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z };
}
M3 MulM(const M3& a, const M3& b) {
    M3 c = {};
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) c.m[i][j] += a.m[i][k] * b.m[k][j];
    return c;
}
int Det(const M3& a) {
    return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) - a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0])
         + a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
}
const M3 kIdentity = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
// Quarter turns about Y taking +Z toward +X.
M3 TurnY(int k) {
    M3 r = kIdentity;
    const M3 q = { { { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 } } };
    for (int i = 0; i < (k & 3); i++) r = MulM(q, r);
    return r;
}
P3 FaceDir(int f) {
    static const P3 d[FACE_COUNT] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    return d[f];
}
// The rotation taking canonical +Y (growth) to `face`; on a wall, the
// canonical lean (+Z) turns to point down, so props droop.
M3 AnchorTo(int face) {
    if (face == FACE_POS_Y) return kIdentity;
    if (face == FACE_NEG_Y) return { { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } } }; // a mirror: windings flip
    P3 ey = FaceDir(face), ez = { 0, -1, 0 }, ex = Cross(ey, ez);
    return { { { ex.x, ey.x, ez.x }, { ex.y, ey.y, ez.y }, { ex.z, ey.z, ez.z } } };
}
// The rotation about Y taking canonical +Z to a horizontal facing.
M3 YawTo(int face) {
    switch (face) {
    case FACE_POS_X: return TurnY(1);
    case FACE_NEG_Z: return TurnY(2);
    case FACE_NEG_X: return TurnY(3);
    default: return kIdentity;
    }
}

int PropPolys(BlockShape shape, uint8_t state, ShapePoly* out, int variant) {
    const Canon& c = GetCanon(shape);
    int facing = StateFacing(state);
    M3 r = kIdentity;
    if (c.anchor == ANCHOR_CLICKED) {
        r = AnchorTo(facing);
        if (c.turns && (facing == FACE_POS_Y || facing == FACE_NEG_Y)) r = MulM(r, TurnY(variant));
    } else if (c.anchor == ANCHOR_YAW) {
        r = YawTo(HorizontalFacing(state));
    }
    bool mirror = Det(r) < 0;
    int n = 0;
    for (const CFace& f : c.faces) {
        // Texture by the prop's own sense of up: its cap wears the top.
        int ax = std::abs(f.n.x), ay = std::abs(f.n.y), az = std::abs(f.n.z);
        int texFace = ay > ax + az ? (f.n.y > 0 ? FACE_POS_Y : FACE_NEG_Y) : FACE_POS_Z; // steeper than 45 degrees: a cap
        P3 wn = Mul(r, f.n);
        int wx = std::abs(wn.x), wy = std::abs(wn.y), wz = std::abs(wn.z);
        int axisFace = -1;
        if (wy == 0 && wz == 0) axisFace = wn.x > 0 ? FACE_POS_X : FACE_NEG_X;
        else if (wx == 0 && wz == 0) axisFace = wn.y > 0 ? FACE_POS_Y : FACE_NEG_Y;
        else if (wx == 0 && wy == 0) axisFace = wn.z > 0 ? FACE_POS_Z : FACE_NEG_Z;
        // Slanted facets get a slope class; the shader lights them by their true normal.
        uint8_t shade = axisFace >= 0 ? (uint8_t)axisFace : (uint8_t)(wn.y >= 0 ? SHADE_SLOPE_UP : SHADE_SLOPE_DOWN);
        int uvFace = axisFace >= 0 ? axisFace
                   : (wy >= wx && wy >= wz) ? (wn.y > 0 ? FACE_POS_Y : FACE_NEG_Y)
                   : (wx >= wz) ? (wn.x > 0 ? FACE_POS_X : FACE_NEG_X) : (wn.z > 0 ? FACE_POS_Z : FACE_NEG_Z);
        std::vector<P3> vs;
        for (P3 v : f.v) { P3 q = Mul(r, { v.x - 4, v.y - 4, v.z - 4 }); vs.push_back({ q.x + 4, q.y + 4, q.z + 4 }); }
        if (mirror) std::reverse(vs.begin(), vs.end());
        // Lying flat on the cell boundary: hidden by a full neighbour there.
        int boundary = -1;
        if (axisFace >= 0) {
            bool all = true;
            for (P3 v : vs) {
                int coord = axisFace <= FACE_NEG_X ? v.x : axisFace <= FACE_NEG_Y ? v.y : v.z;
                int edge = (axisFace & 1) ? 0 : S;
                all = all && coord == edge;
            }
            if (all) boundary = axisFace;
        }
        // Fan into polygons of at most four corners.
        int m = (int)vs.size();
        for (int k = 1; k + 1 < m && n < MAX_SHAPE_POLYS; k += 2) {
            ShapePoly& p = out[n++];
            p.count = (uint8_t)(k + 2 < m ? 4 : 3);
            int idx[4] = { 0, k, k + 1, k + 2 };
            for (int q = 0; q < p.count; q++) {
                p.v[q] = V(vs[idx[q]].x, vs[idx[q]].y, vs[idx[q]].z);
                ProjectUV(uvFace, p.v[q]);
            }
            p.texFace = (uint8_t)texFace; p.shade = shade; p.boundary = (int8_t)boundary;
        }
    }
    return n;
}

// A prop's collision: one box, the bounds of every way it can be turned.
ShapeBox PropBox(BlockShape shape, uint8_t state) {
    ShapePoly polys[MAX_SHAPE_POLYS];
    ShapeBox b = { S, S, S, 0, 0, 0 };
    for (int variant = 0; variant < 4; variant++) {
        int n = PropPolys(shape, state, polys, variant);
        for (int i = 0; i < n; i++)
            for (int k = 0; k < polys[i].count; k++) {
                const ShapeVertex& v = polys[i].v[k];
                b.x0 = std::min(b.x0, v.x); b.y0 = std::min(b.y0, v.y); b.z0 = std::min(b.z0, v.z);
                b.x1 = std::max(b.x1, v.x); b.y1 = std::max(b.y1, v.y); b.z1 = std::max(b.z1, v.z);
            }
    }
    return b;
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
    default:
        if (ShapeIsProp(shape)) {
            // Cached: collision asks every tick.
            static ShapeBox cache[SHAPE_COUNT][8];
            static bool have[SHAPE_COUNT][8];
            int f = state & STATE_FACING_MASK;
            if (!have[shape][f]) { cache[shape][f] = PropBox(shape, (uint8_t)f); have[shape][f] = true; }
            out[0] = cache[shape][f];
            return 1;
        }
        out[0] = { 0, 0, 0, 8, 8, 8 }; return 1;
    }
}

int ShapePolys(BlockShape shape, uint8_t state, ShapePoly* out, int variant) {
    if (ShapeIsProp(shape)) return PropPolys(shape, state, out, variant);
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
    default:                 return 0;
    }
}

int PipePolys(uint8_t joined, uint8_t state, ShapePoly* out) {
    // Built from boxes, but never lazily intersected: every face that would
    // lie against another part (an arm's end on the node, a bar's end in its
    // collar) or against what the pipe joins (the next pipe carries the
    // tube on; a block covers it) is left out, so there are no doubled,
    // flickering faces and a run is one seamless tube.
    uint8_t mouths = PipeMouths(joined, state);
    uint8_t ends = (uint8_t)(joined | mouths);
    int n = 0;
    auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1, uint8_t skip) {
        ShapePoly six[FACE_COUNT];
        int m = BoxFaces(ShapeBox{ (uint8_t)x0, (uint8_t)y0, (uint8_t)z0, (uint8_t)x1, (uint8_t)y1, (uint8_t)z1 }, six);
        for (int k = 0; k < m; k++) if (!(skip & (1u << six[k].texFace))) out[n++] = six[k];
    };
    auto bit = [](int f) { return (uint8_t)(1u << f); };
    // A straight run: one bar, end to end, open only where it's a mouth
    // (and there the collar is the end).
    bool straight = false;
    for (int axis = 0; axis < 3 && !straight; axis++) {
        uint8_t pair = (uint8_t)(3u << (axis * 2));
        if (ends != pair) continue;
        straight = true;
        uint8_t skip = pair; // both ends: joined onward, or inside a collar
        if (axis == 0) box(0, 3, 3, 8, 5, 5, skip);
        else if (axis == 1) box(3, 0, 3, 5, 8, 5, skip);
        else box(3, 3, 0, 5, 5, 8, skip);
    }
    if (!straight) {
        box(2, 2, 2, 6, 6, 6, 0); // the node a bend or junction turns in
        // Arms: no face on the node, none at the far end (joined, or in a collar).
        if (ends & bit(FACE_POS_X)) box(6, 3, 3, 8, 5, 5, bit(FACE_POS_X) | bit(FACE_NEG_X));
        if (ends & bit(FACE_NEG_X)) box(0, 3, 3, 2, 5, 5, bit(FACE_POS_X) | bit(FACE_NEG_X));
        if (ends & bit(FACE_POS_Y)) box(3, 6, 3, 5, 8, 5, bit(FACE_POS_Y) | bit(FACE_NEG_Y));
        if (ends & bit(FACE_NEG_Y)) box(3, 0, 3, 5, 2, 5, bit(FACE_POS_Y) | bit(FACE_NEG_Y));
        if (ends & bit(FACE_POS_Z)) box(3, 3, 6, 5, 5, 8, bit(FACE_POS_Z) | bit(FACE_NEG_Z));
        if (ends & bit(FACE_NEG_Z)) box(3, 3, 0, 5, 5, 2, bit(FACE_POS_Z) | bit(FACE_NEG_Z));
    }
    // A collar round each open mouth, so an open end reads as one.
    if (mouths & bit(FACE_POS_X)) box(7, 2, 2, 8, 6, 6, 0);
    if (mouths & bit(FACE_NEG_X)) box(0, 2, 2, 1, 6, 6, 0);
    if (mouths & bit(FACE_POS_Y)) box(2, 7, 2, 6, 8, 6, 0);
    if (mouths & bit(FACE_NEG_Y)) box(2, 0, 2, 6, 1, 6, 0);
    if (mouths & bit(FACE_POS_Z)) box(2, 2, 7, 6, 6, 8, 0);
    if (mouths & bit(FACE_NEG_Z)) box(2, 2, 0, 6, 6, 1, 0);
    return n;
}
