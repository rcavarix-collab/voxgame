// common.h
//
// Shared foundation: minimal linear algebra and window-size constants. No
// Windows/D3D dependency here on purpose -- every module includes it, and
// the pure-C++ systems (terrain, mech) are tested natively without Windows.
// Math follows the row-vector, left-handed convention (v' = v * M) so the
// HLSL cbuffers (row_major) take matrices as they are. The world's axes:
// +Y is up; one unit is one metre.

#pragma once

#include <cmath>
#include <cstdint>

struct Vec3 { float x, y, z; };
static inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static inline Vec3 operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
static inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline float Length(Vec3 a) { return sqrtf(Dot(a, a)); }
static inline Vec3 Normalize(Vec3 a) {
    float len = Length(a);
    if (len < 1e-6f) return { 0, 0, 0 };
    return a * (1.0f / len);
}
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static const float kPi = 3.14159265359f;
static const Vec3 kUp = { 0.0f, 1.0f, 0.0f };

// Row-major 4x4, row-vector convention.
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
static inline Mat4 MatLookToLH(Vec3 eye, Vec3 dir, Vec3 up) {
    Vec3 z = Normalize(dir), x = Normalize(Cross(up, z)), y = Cross(z, x);
    Mat4 r = {};
    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z;
    r.m[3][0] = -Dot(x, eye); r.m[3][1] = -Dot(y, eye); r.m[3][2] = -Dot(z, eye); r.m[3][3] = 1;
    return r;
}
static inline Mat4 MatPerspectiveFovLH(float fovY, float aspect, float zn, float zf) {
    float ys = 1.0f / tanf(fovY * 0.5f), xs = ys / aspect;
    Mat4 r = {};
    r.m[0][0] = xs; r.m[1][1] = ys;
    r.m[2][2] = zf / (zf - zn); r.m[2][3] = 1.0f;
    r.m[3][2] = -zn * zf / (zf - zn);
    return r;
}
// Orthographic, centred on the view axis, D3D [0,1] depth.
static inline Mat4 MatOrthoLH(float w, float h, float zn, float zf) {
    Mat4 r = {};
    r.m[0][0] = 2.0f / w; r.m[1][1] = 2.0f / h;
    r.m[2][2] = 1.0f / (zf - zn); r.m[3][2] = -zn / (zf - zn); r.m[3][3] = 1.0f;
    return r;
}

// A small, fast, well-mixed hash (for procedural placement, per-facet
// tints and jitter): the same input always gives the same output, so
// anything built from it is stable across rebuilds and sessions.
static inline uint32_t Hash3(int32_t x, int32_t y, int32_t z, uint32_t salt = 0) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^ salt * 0x9e3779b9u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}
static inline float Hash01(int32_t x, int32_t y, int32_t z, uint32_t salt = 0) { return (Hash3(x, y, z, salt) >> 8) * (1.0f / 16777216.0f); }

// Initial window client size; the live size is g_screenW / g_screenH.
static const int DEFAULT_WINDOW_W = 1280;
static const int DEFAULT_WINDOW_H = 720;
extern int g_screenW, g_screenH;
