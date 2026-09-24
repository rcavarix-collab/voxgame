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

// =======================================================================
// Part II/III - World representation and block model
// =======================================================================

static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;

static const int CHUNK_SIZE = 16;
static const int CHUNK_CELLS = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
static const int Y_MIN = 0;
static const int Y_MAX = 255;
static const int MAX_FALLS = 64;  // capped per-tick gravity work (Section 5.1)

// The block registry (IDs, names, flags, per-face textures) lives in
// blocks.h -- one row per block type (Part III).
#include "blocks.h"
