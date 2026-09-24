// common.h
//
// Shared foundation for every other file in the project: minimal linear
// algebra and the block model (identity, metadata table, atlas layout).
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

enum BlockID : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_FOUNDATION,
    BLOCK_STONE,
    BLOCK_DIRT,
    BLOCK_WOOD,
    BLOCK_CHEST,
    BLOCK_MACHINE,
    BLOCK_COUNT
};

// Identity written to disk is the name below, never the enum value
// (Section 3.1) -- this is what lets the roster grow without corrupting
// old saves.
static const char* g_blockNames[BLOCK_COUNT] = {
    "air",
    "foundation",
    "stone",
    "dirt",
    "wood",
    "chest",
    "machine",
};

struct BlockInfo {
    bool foundational; // never falls, always supports (Part V)
    bool solid;         // collision / raycast / face-culling participant
    int tex;              // atlas slot
};

// Single source of truth per block (Section 3.2) -- no virtual dispatch
// in the hot paths (meshing, gravity, picking) reads this table instead.
static const BlockInfo g_info[BLOCK_COUNT] = {
    /* air            */ { false, false, -1 },
    /* foundation     */ { true,  true,   0 },
    /* stone          */ { false, true,   1 },
    /* dirt           */ { false, true,   2 },
    /* wood           */ { false, true,   3 },
    /* chest          */ { true,  true,   4 },
    /* machine        */ { true,  true,   5 },
};

// Atlas layout. NOTE: this order (foundation, stone, dirt, wood, chest,
// machine) must match the tile draw order in textures.cpp's
// GenerateGameTextures -- there is no shared header enforcing this, so
// changing the order here means changing it there too.
static const int ATLAS_COLS = 3;
static const int ATLAS_ROWS = 2;
static const int TILE_SIZE = 64;

static inline void AtlasRect(int slot, float& u0, float& v0, float& u1, float& v1) {
    int col = slot % ATLAS_COLS;
    int row = slot / ATLAS_COLS;
    float texW = (float)(ATLAS_COLS * TILE_SIZE);
    float texH = (float)(ATLAS_ROWS * TILE_SIZE);
    // A 1/64-texel inset: just enough that float error at a face's very
    // edge can never floor into the neighbouring tile, far too small to
    // shift any texel. (This used to be a half-texel inset, which under
    // point sampling maps each face onto texel centres 0.5..63.5 -- so
    // the first and last texel column of every tile drew at half width,
    // a visible 1px seam on every block. No MSAA, so pixel centres never
    // extrapolate past the face and a tiny margin is all that's needed.)
    float insetU = (1.0f / 64.0f) / texW;
    float insetV = (1.0f / 64.0f) / texH;
    u0 = (float)(col * TILE_SIZE) / texW + insetU;
    u1 = (float)((col + 1) * TILE_SIZE) / texW - insetU;
    v0 = (float)(row * TILE_SIZE) / texH + insetV;
    v1 = (float)((row + 1) * TILE_SIZE) / texH - insetV;
}
