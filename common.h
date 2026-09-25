// common.h
//
// Shared foundation for every other file in the project: minimal linear
// algebra, world-size constants, and (via blocks.h) the block registry.
// No Windows/D3D/XAudio2 dependency here on purpose -- this is the one
// header every other module includes, so it stays free of anything that
// would force an unrelated module to pull in a graphics or audio API it
// doesn't use.
//
// Everything below was originally declared `static` inside a single
// main.cpp (the project's two-source-file era) -- moved here verbatim
// as part of splitting into multiple files, kept `static`/`inline` so
// each including translation unit still gets its own internal-linkage
// copy of these small constants/functions, exactly as before, with the
// same lack of a shared header between them replaced by an *actual*
// shared header instead of "the same file."

#pragma once

#include <cstdint>
#include <cmath>

// ---------------------------------------------------------------------
// Minimal linear algebra. The mingw-w64 port of DirectXMath only carries
// the plain storage structs (XMFLOAT4X4 and friends) -- the actual
// vector/matrix math API (XMVECTOR, XMMATRIX, XMMatrixLookToLH, etc.)
// isn't present in that header, so the handful of operations this
// prototype needs are implemented directly here from the standard
// row-vector / left-handed formulas.
// ---------------------------------------------------------------------
struct Vec3 { float x, y, z; };
static inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static inline Vec3 Normalize(Vec3 a) {
    float len = sqrtf(Dot(a, a));
    if (len < 1e-6f) return { 0, 0, 0 };
    return { a.x / len, a.y / len, a.z / len };
}

// ---------------------------------------------------------------------
// The compass (DESIGN.md 2.2). The world's cardinal directions, which
// everything that has a direction is defined against: the sun rises in the
// east and sets in the west, passing overhead (sky.h -- the world sits on
// its equator); the celestial pole lies on the northern horizon; the
// essence map shows north up and east to the right. A player's yaw of 0
// faces north, and yaw grows turning toward the east.
// ---------------------------------------------------------------------
static const Vec3 kEast  = {  1.0f, 0.0f,  0.0f };
static const Vec3 kWest  = { -1.0f, 0.0f,  0.0f };
static const Vec3 kNorth = {  0.0f, 0.0f,  1.0f };
static const Vec3 kSouth = {  0.0f, 0.0f, -1.0f };
static const Vec3 kUp    = {  0.0f, 1.0f,  0.0f };
// The nearest of the eight compass points to a view yaw (debug readouts).
static inline const char* CompassPoint(float yaw) {
    static const char* names[8] = { "NORTH", "NORTH-EAST", "EAST", "SOUTH-EAST", "SOUTH", "SOUTH-WEST", "WEST", "NORTH-WEST" };
    float turns = yaw / 6.2831853f;
    turns -= floorf(turns);
    return names[(int)(turns * 8.0f + 0.5f) & 7];
}

// Row-major 4x4, row-vector convention (v' = v * M), matching the HLSL
// cbuffers (declared row_major) so no transpose is needed between CPU
// and GPU layouts.
struct Mat4 { float m[4][4]; };

static inline Mat4 MatIdentity() {
    Mat4 r = {};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}
static inline Mat4 MatMul(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}
static inline Mat4 MatTranslation(float x, float y, float z) {
    Mat4 r = MatIdentity();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}
static inline Mat4 MatLookToLH(Vec3 eye, Vec3 dir, Vec3 up) {
    Vec3 zaxis = Normalize(dir);
    Vec3 xaxis = Normalize(Cross(up, zaxis));
    Vec3 yaxis = Cross(zaxis, xaxis);
    Mat4 r = {};
    r.m[0][0] = xaxis.x; r.m[0][1] = yaxis.x; r.m[0][2] = zaxis.x; r.m[0][3] = 0;
    r.m[1][0] = xaxis.y; r.m[1][1] = yaxis.y; r.m[1][2] = zaxis.y; r.m[1][3] = 0;
    r.m[2][0] = xaxis.z; r.m[2][1] = yaxis.z; r.m[2][2] = zaxis.z; r.m[2][3] = 0;
    r.m[3][0] = -Dot(xaxis, eye); r.m[3][1] = -Dot(yaxis, eye); r.m[3][2] = -Dot(zaxis, eye); r.m[3][3] = 1;
    return r;
}
static inline Mat4 MatPerspectiveFovLH(float fovY, float aspect, float zn, float zf) {
    float yScale = 1.0f / tanf(fovY * 0.5f);
    float xScale = yScale / aspect;
    Mat4 r = {};
    r.m[0][0] = xScale;
    r.m[1][1] = yScale;
    r.m[2][2] = zf / (zf - zn);
    r.m[2][3] = 1.0f;
    r.m[3][2] = -zn * zf / (zf - zn);
    return r;
}
// Orthographic projection (row-vector, left-handed, D3D [0,1] depth),
// centred on the view axis.
static inline Mat4 MatOrthoLH(float w, float h, float zn, float zf) {
    Mat4 r = {};
    r.m[0][0] = 2.0f / w;
    r.m[1][1] = 2.0f / h;
    r.m[2][2] = 1.0f / (zf - zn);
    r.m[3][2] = -zn / (zf - zn);
    r.m[3][3] = 1.0f;
    return r;
}

// Initial window client size. The live backbuffer size is g_screenW /
// g_screenH (render.cpp), which follows window resizes and fullscreen.
static const int DEFAULT_WINDOW_W = 1280;
static const int DEFAULT_WINDOW_H = 720;
// Smallest client area the window can be resized to (menus and hotbar fit).
static const int MIN_CLIENT_W = 960;
static const int MIN_CLIENT_H = 680;
extern int g_screenW, g_screenH;

// ---------------------------------------------------------------------
// Cacophony additions: helpers its game systems (terrain, mech, props,
// weapons) use on top of Voxistics' set. The world's axes are the same
// (+Y up, the compass above); one unit is one metre.
// ---------------------------------------------------------------------
static inline Vec3 operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
static inline float Length(Vec3 a) { return sqrtf(Dot(a, a)); }
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static const float kPi = 3.14159265359f;

// A small, fast, well-mixed hash (for procedural placement, per-facet
// tints and jitter): the same input always gives the same output, so
// anything built from it is stable across rebuilds and sessions.
static inline uint32_t Hash3(int32_t x, int32_t y, int32_t z, uint32_t salt = 0) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^ salt * 0x9e3779b9u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}
static inline float Hash01(int32_t x, int32_t y, int32_t z, uint32_t salt = 0) { return (Hash3(x, y, z, salt) >> 8) * (1.0f / 16777216.0f); }
