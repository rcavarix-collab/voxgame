// main.cpp
//
// Voxel Logistics Game - Milestone 1 prototype.
// World storage, chunked meshing with per-block atlas texturing, gravity/
// falling blocks, exact-DDA block picking, place/break, crash-safe
// versioned save/load, basic FPS movement and collision.
//
// Two-source-file project (main.cpp + supplement.cpp), built and linked in
// a single compiler invocation. supplement.cpp owns procedural texture
// generation (GDI+, used only at load time) and exposes it to this file
// through plain extern declarations below -- there is no shared header.

// MSVC's windows.h defines min/max function-like macros unless this is
// set first -- without it, any bare std::min/std::max call in this file
// (not just the one used today) would silently break at the token that
// happens to be followed by '('.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

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
// cbuffer below which is declared row_major so no transpose is needed
// between CPU and GPU layouts.
struct Mat4 { float m[4][4]; };

static Mat4 MatIdentity() {
    Mat4 r = {};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}
static Mat4 MatMul(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}
static Mat4 MatTranslation(float x, float y, float z) {
    Mat4 r = MatIdentity();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}
static Mat4 MatLookToLH(Vec3 eye, Vec3 dir, Vec3 up) {
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
static Mat4 MatPerspectiveFovLH(float fovY, float aspect, float zn, float zf) {
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

// ---------------------------------------------------------------------
// supplement.cpp entry points (procedural texture generation via GDI+).
// Kept as plain extern declarations since the project has no shared
// header; the two files agree on this contract by convention only.
// ---------------------------------------------------------------------
extern "C" bool GenerateGameTextures(
    int tileSize, int atlasCols, int atlasRows,
    uint8_t** outAtlasPixelsBGRA, int* outAtlasW, int* outAtlasH,
    uint8_t** outPipePixelsBGRA, int* outPipeSize);
extern "C" void FreeGeneratedPixels(uint8_t* p);
// A small monospace font-glyph grid (ASCII 32..126) plus one reserved
// solid-white cell, generated once at load time the same way as the
// block atlas -- used to draw every UI panel/border/crosshair/label in
// the dedicated UI pass (Section 4.6) via a single bound texture and a
// per-vertex color tint.
extern "C" bool GenerateUIAtlas(
    int cellW, int cellH, int cols, int rows,
    uint8_t** outPixelsBGRA, int* outW, int* outH);

// =======================================================================
// Part II/III - World representation and block model
// =======================================================================

static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;

static const int CHUNK_SIZE = 16;
static const int CHUNK_CELLS = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
static const int Y_MIN = 0;
static const int Y_MAX = 255;
static const int LOAD_RADIUS = 3; // chunks, horizontal only (Section 2.4)
static const int MAX_FALLS = 64;  // capped per-tick gravity work (Section 5.1)

enum BlockID : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_FOUNDATION,
    BLOCK_STONE,
    BLOCK_DIRT,
    BLOCK_WOOD,
    BLOCK_CHEST,
    BLOCK_MACHINE,
    BLOCK_PIPE_STRAIGHT,
    BLOCK_PIPE_CORNER,
    BLOCK_PIPE_JUNCTION,
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
    "pipe_straight",
    "pipe_corner",
    "pipe_junction",
};

struct BlockInfo {
    bool foundational; // never falls, always supports (Part V)
    bool solid;         // collision / raycast / face-culling participant
    int shape;           // 0 cube, 1 straight pipe, 2 corner, 3 junction
    int tex;              // atlas slot for cube blocks, -1 otherwise
};

// Single source of truth per block (Section 3.2) -- no virtual dispatch
// in the hot paths (meshing, gravity, picking) reads this table instead.
static const BlockInfo g_info[BLOCK_COUNT] = {
    /* air            */ { false, false, 0, -1 },
    /* foundation     */ { true,  true,  0,  0 },
    /* stone          */ { false, true,  0,  1 },
    /* dirt           */ { false, true,  0,  2 },
    /* wood           */ { false, true,  0,  3 },
    /* chest          */ { true,  true,  0,  4 },
    /* machine        */ { true,  true,  0,  5 },
    /* pipe_straight  */ { true,  false, 1, -1 },
    /* pipe_corner    */ { true,  false, 2, -1 },
    /* pipe_junction  */ { true,  false, 3, -1 },
};

// Atlas layout. NOTE: this order (foundation, stone, dirt, wood, chest,
// machine) must match the tile draw order in supplement.cpp's
// GenerateGameTextures -- there is no shared header enforcing this, so
// changing the order here means changing it there too.
static const int ATLAS_COLS = 3;
static const int ATLAS_ROWS = 2;
static const int TILE_SIZE = 64;

static void AtlasRect(int slot, float& u0, float& v0, float& u1, float& v1) {
    int col = slot % ATLAS_COLS;
    int row = slot / ATLAS_COLS;
    float texW = (float)(ATLAS_COLS * TILE_SIZE);
    float texH = (float)(ATLAS_ROWS * TILE_SIZE);
    // Half-texel inset so point-filtered sampling never bleeds into the
    // neighboring tile at the shared edge.
    float insetU = 0.5f / texW;
    float insetV = 0.5f / texH;
    u0 = (float)(col * TILE_SIZE) / texW + insetU;
    u1 = (float)((col + 1) * TILE_SIZE) / texW - insetU;
    v0 = (float)(row * TILE_SIZE) / texH + insetV;
    v1 = (float)((row + 1) * TILE_SIZE) / texH - insetV;
}

// =======================================================================
// Part 4.6 - UI pass support: font-glyph atlas layout and quad builders.
// Same generate-once-at-load-time approach as the block atlas (Section
// 4.3), just with GDI+ drawing glyphs instead of block patterns. Layout
// is 16 cols x 6 rows = 96 cells: ASCII 32..126 (95 printable chars) at
// cell index (code-32), plus one reserved solid-white cell at the last
// index for drawing untextured tinted rectangles through the same
// texture/shader/draw-call path as text.
// =======================================================================
static const int UI_CELL_W = 20;
static const int UI_CELL_H = 28;
static const int UI_ATLAS_COLS = 16;
static const int UI_ATLAS_ROWS = 6;
static const int UI_WHITE_CELL = UI_ATLAS_COLS * UI_ATLAS_ROWS - 1;

struct UIVertex { float x, y, u, v, r, g, b, a; };

static void UIAtlasRect(int cell, float& u0, float& v0, float& u1, float& v1) {
    int col = cell % UI_ATLAS_COLS;
    int row = cell / UI_ATLAS_COLS;
    float texW = (float)(UI_ATLAS_COLS * UI_CELL_W);
    float texH = (float)(UI_ATLAS_ROWS * UI_CELL_H);
    float insetU = 0.5f / texW, insetV = 0.5f / texH;
    u0 = (float)(col * UI_CELL_W) / texW + insetU;
    u1 = (float)((col + 1) * UI_CELL_W) / texW - insetU;
    v0 = (float)(row * UI_CELL_H) / texH + insetV;
    v1 = (float)((row + 1) * UI_CELL_H) / texH - insetV;
}

static int UICharCell(char c) {
    if (c < 32 || c > 126) return -1;
    return (int)c - 32;
}

static void UIAddQuad(std::vector<UIVertex>& v, float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a) {
    v.push_back({ x0, y0, u0, v0, r, g, b, a });
    v.push_back({ x1, y0, u1, v0, r, g, b, a });
    v.push_back({ x1, y1, u1, v1, r, g, b, a });
    v.push_back({ x0, y0, u0, v0, r, g, b, a });
    v.push_back({ x1, y1, u1, v1, r, g, b, a });
    v.push_back({ x0, y1, u0, v1, r, g, b, a });
}

// Untextured tinted rectangle -- samples the reserved white cell.
static void UIDrawRect(std::vector<UIVertex>& v, float x0, float y0, float x1, float y1,
                        float r, float g, float b, float a) {
    float u0, v0, u1, v1;
    UIAtlasRect(UI_WHITE_CELL, u0, v0, u1, v1);
    UIAddQuad(v, x0, y0, x1, y1, u0, v0, u1, v1, r, g, b, a);
}

static float UITextWidth(const std::string& text, float scale) {
    return (float)text.size() * UI_CELL_W * scale;
}

// Fixed-advance (monospace-grid) text -- each glyph cell is centered on
// its own character during atlas generation, so a constant per-char
// advance is enough for a "rudimentary" HUD/menu without a real text
// shaping pass.
static void UIDrawText(std::vector<UIVertex>& v, const std::string& text, float x, float y,
                        float scale, float r, float g, float b, float a) {
    float w = UI_CELL_W * scale, h = UI_CELL_H * scale;
    float curX = x;
    for (char c : text) {
        int cell = UICharCell(c);
        if (cell >= 0) {
            float u0, v0, u1, v1;
            UIAtlasRect(cell, u0, v0, u1, v1);
            UIAddQuad(v, curX, y, curX + w, y + h, u0, v0, u1, v1, r, g, b, a);
        }
        curX += w;
    }
}

// Floor division exactly as specified in Section 2.2 -- a naive `/`
// truncates toward zero and misassigns blocks near the origin on the
// negative side.
static inline int FloorDiv16(int v) {
    return v >= 0 ? v / 16 : (v - 15) / 16;
}
static inline int LocalOf(int v, int fd) {
    return v - fd * 16;
}

struct ChunkCoord {
    int x, y, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = (size_t)(uint32_t)c.x * 73856093u;
        h ^= (size_t)(uint32_t)c.y * 19349663u;
        h ^= (size_t)(uint32_t)c.z * 83492791u;
        return h;
    }
};

struct PipeInstance {
    int worldX, worldY, worldZ;
    int shape;
};

struct Vertex {
    float px, py, pz;
    float u, v;
};

struct Chunk {
    uint8_t blocks[CHUNK_CELLS] = {};
    bool dirty = true;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    UINT indexCount = 0;
    std::vector<PipeInstance> pipes;

    ~Chunk() {
        if (vb) vb->Release();
        if (ib) ib->Release();
    }

    static int LocalIndex(int lx, int ly, int lz) {
        return (ly * CHUNK_SIZE + lz) * CHUNK_SIZE + lx;
    }
};

class World {
public:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks;

    static ChunkCoord ToChunk(int x, int y, int z) {
        return { FloorDiv16(x), FloorDiv16(y), FloorDiv16(z) };
    }

    Chunk* FindChunk(const ChunkCoord& cc) {
        auto it = chunks.find(cc);
        return it == chunks.end() ? nullptr : it->second.get();
    }

    Chunk* GetOrCreateChunk(const ChunkCoord& cc) {
        auto it = chunks.find(cc);
        if (it != chunks.end()) return it->second.get();
        auto chunk = std::make_unique<Chunk>();
        Chunk* ptr = chunk.get();
        chunks.emplace(cc, std::move(chunk));
        return ptr;
    }

    BlockID Get(int x, int y, int z) {
        if (y < Y_MIN || y > Y_MAX) return BLOCK_AIR;
        ChunkCoord cc = ToChunk(x, y, z);
        Chunk* c = FindChunk(cc);
        if (!c) return BLOCK_AIR;
        int lx = LocalOf(x, cc.x), ly = LocalOf(y, cc.y), lz = LocalOf(z, cc.z);
        return (BlockID)c->blocks[Chunk::LocalIndex(lx, ly, lz)];
    }

    bool Solid(int x, int y, int z) {
        BlockID id = Get(x, y, z);
        return id != BLOCK_AIR && g_info[id].solid;
    }

    // Marks the owning chunk dirty, plus any neighbor whose visible
    // boundary faces could be affected by this edit (Section 4.2).
    void MarkDirtyForEdit(const ChunkCoord& cc, int lx, int ly, int lz) {
        MarkChunkDirty(cc);
        if (lx == 0) MarkChunkDirty({ cc.x - 1, cc.y, cc.z });
        if (lx == CHUNK_SIZE - 1) MarkChunkDirty({ cc.x + 1, cc.y, cc.z });
        if (ly == 0) MarkChunkDirty({ cc.x, cc.y - 1, cc.z });
        if (ly == CHUNK_SIZE - 1) MarkChunkDirty({ cc.x, cc.y + 1, cc.z });
        if (lz == 0) MarkChunkDirty({ cc.x, cc.y, cc.z - 1 });
        if (lz == CHUNK_SIZE - 1) MarkChunkDirty({ cc.x, cc.y, cc.z + 1 });
    }

    void MarkChunkDirty(const ChunkCoord& cc) {
        Chunk* c = FindChunk(cc);
        if (c) c->dirty = true;
    }

    // Bulk-load / worldgen path: no gravity trigger, no live-support
    // check. See Section 5.2 -- running gravity checks against a
    // partially-reconstructed world corrupts structures because
    // unordered_map iteration order is not spatial.
    void SetRaw(int x, int y, int z, BlockID id) {
        if (y < Y_MIN || y > Y_MAX) return;
        ChunkCoord cc = ToChunk(x, y, z);
        Chunk* c = GetOrCreateChunk(cc);
        int lx = LocalOf(x, cc.x), ly = LocalOf(y, cc.y), lz = LocalOf(z, cc.z);
        c->blocks[Chunk::LocalIndex(lx, ly, lz)] = (uint8_t)id;
        MarkDirtyForEdit(cc, lx, ly, lz);
    }

    // Live edit path used during play; gravity re-evaluation happens
    // via the caller invoking MaybeQueueFall after this (kept separate
    // so World has no dependency on the fall-queue globals).
    void Set(int x, int y, int z, BlockID id) {
        SetRaw(x, y, z, id);
    }
};

// =======================================================================
// Part V - Falling-block gravity system
// =======================================================================

struct FallEntry { int x, y, z; };
static std::deque<FallEntry> g_fallQueue;

static void MaybeQueueFall(World& w, int x, int y, int z) {
    if (y < Y_MIN || y > Y_MAX) return;
    BlockID id = w.Get(x, y, z);
    if (id == BLOCK_AIR) return;
    if (g_info[id].foundational) return;
    if (!g_info[id].solid) return;
    if (y - 1 < Y_MIN) return;         // resting on the world floor
    if (w.Solid(x, y - 1, z)) return;  // supported
    g_fallQueue.push_back({ x, y, z });
}

// Drains at most MAX_FALLS entries per call regardless of queue length,
// so a single catastrophic edit (removing a foundation under a huge
// structure) cannot spike frame time -- the cascade is smoothed across
// many ticks instead (Section 5.1).
static void ProcessFalls(World& w) {
    int n = (int)std::min<size_t>(MAX_FALLS, g_fallQueue.size());
    for (int i = 0; i < n; i++) {
        FallEntry e = g_fallQueue.front();
        g_fallQueue.pop_front();

        BlockID id = w.Get(e.x, e.y, e.z);
        if (id == BLOCK_AIR || g_info[id].foundational) continue; // stale entry
        if (e.y - 1 < Y_MIN) continue;
        if (w.Solid(e.x, e.y - 1, e.z)) continue; // became supported since queued

        w.SetRaw(e.x, e.y - 1, e.z, id);
        w.SetRaw(e.x, e.y, e.z, BLOCK_AIR);

        MaybeQueueFall(w, e.x, e.y + 1, e.z); // whatever was resting on top
        MaybeQueueFall(w, e.x, e.y - 1, e.z); // keep falling if still unsupported
    }
}

// A live world edit that could have removed support underneath a block.
static void LiveEdit(World& w, int x, int y, int z, BlockID id) {
    w.Set(x, y, z, id);
    MaybeQueueFall(w, x, y + 1, z);
}

// =======================================================================
// Part VII - World generation (deterministic, bypasses live gravity)
// =======================================================================

static int TerrainHeight(int wx, int wz) {
    double h = 40.0 + 6.0 * sin(wx * 0.15) + 4.0 * cos(wz * 0.13);
    int ih = (int)h;
    if (ih < 20) ih = 20;
    if (ih > 60) ih = 60;
    return ih;
}

static std::unordered_set<long long> g_generatedColumns;

static long long ColumnKey(int cx, int cz) {
    return ((long long)(uint32_t)cx << 32) | (uint32_t)cz;
}

static void GenerateColumn(World& w, int cx, int cz) {
    long long key = ColumnKey(cx, cz);
    if (g_generatedColumns.count(key)) return;
    g_generatedColumns.insert(key);

    int baseX = cx * CHUNK_SIZE, baseZ = cz * CHUNK_SIZE;
    int maxCy = FloorDiv16(60) + 1;
    for (int cy = 0; cy <= maxCy; cy++) {
        int chunkYLow = cy * CHUNK_SIZE;
        // Skip chunks that would contain nothing but air anywhere in this
        // column -- chunks only exist when they hold real content
        // (Section 2.1).
        bool anyContent = false;
        for (int lx = 0; lx < CHUNK_SIZE && !anyContent; lx++)
            for (int lz = 0; lz < CHUNK_SIZE && !anyContent; lz++)
                if (chunkYLow <= TerrainHeight(baseX + lx, baseZ + lz)) anyContent = true;
        if (!anyContent) continue;

        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int wx = baseX + lx, wz = baseZ + lz;
                int h = TerrainHeight(wx, wz);
                for (int ly = 0; ly < CHUNK_SIZE; ly++) {
                    int wy = chunkYLow + ly;
                    if (wy > h) continue;
                    BlockID id;
                    if (wy == 0) id = BLOCK_FOUNDATION;
                    else if (wy >= h - 2) id = BLOCK_DIRT;
                    else id = BLOCK_STONE;
                    w.SetRaw(wx, wy, wz, id);
                }
            }
        }
    }
}

static int g_lastPlayerChunkX = INT32_MIN, g_lastPlayerChunkZ = INT32_MIN;

static void EnsureChunksLoaded(World& w, int playerChunkX, int playerChunkZ) {
    // Recomputed only when the player's chunk coordinate actually
    // changes (Section 2.4) -- not every frame.
    if (playerChunkX == g_lastPlayerChunkX && playerChunkZ == g_lastPlayerChunkZ) return;
    g_lastPlayerChunkX = playerChunkX;
    g_lastPlayerChunkZ = playerChunkZ;

    for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; dx++)
        for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; dz++)
            GenerateColumn(w, playerChunkX + dx, playerChunkZ + dz);
}

// =======================================================================
// Part IV - Rendering: D3D11 globals and chunk meshing
// =======================================================================

static HWND g_hwnd = nullptr;
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static ID3D11DepthStencilView* g_dsv = nullptr;
static ID3D11VertexShader* g_vs = nullptr;
static ID3D11PixelShader* g_ps = nullptr;
static ID3D11InputLayout* g_layout = nullptr;
static ID3D11Buffer* g_cbuffer = nullptr;
static ID3D11SamplerState* g_sampler = nullptr;
static ID3D11RasterizerState* g_rasterState = nullptr;
static ID3D11DepthStencilState* g_depthState = nullptr;
static ID3D11ShaderResourceView* g_atlasSRV = nullptr;
static ID3D11ShaderResourceView* g_pipeSRV = nullptr;

static ID3D11Buffer* g_pipeMeshVB = nullptr;
static ID3D11Buffer* g_pipeMeshIB = nullptr;
static UINT g_pipeMeshIndexCount = 0;

// Second pass state (Section 4.6): its own shaders, input layout,
// constant buffer, sampler, blend state and depth-stencil state, kept
// fully separate from the world pass's pipeline objects rather than
// overloading them.
static ID3D11VertexShader* g_uiVS = nullptr;
static ID3D11PixelShader* g_uiPS = nullptr;
static ID3D11InputLayout* g_uiLayout = nullptr;
static ID3D11Buffer* g_uiCBuffer = nullptr;
static ID3D11SamplerState* g_uiSampler = nullptr;
static ID3D11BlendState* g_uiBlendState = nullptr;
static ID3D11DepthStencilState* g_uiDepthState = nullptr;
static ID3D11ShaderResourceView* g_uiSRV = nullptr; // font-glyph + white-cell atlas
static ID3D11Buffer* g_uiVB = nullptr;              // dynamic, re-mapped per UI draw batch
static const UINT UI_VB_CAPACITY = 4096;             // vertices

struct CBData { Mat4 mvp; };

static const char* g_shaderSrc =
    "cbuffer CB : register(b0) { row_major matrix mvp; };\n"
    "struct VSIn { float3 pos:POSITION; float2 uv:TEXCOORD0; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn input) { PSIn o; o.pos = mul(float4(input.pos,1.0f), mvp); o.uv = input.uv; return o; }\n"
    "Texture2D tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "float4 PSMain(PSIn input) : SV_TARGET { return tex0.Sample(samp0, input.uv); }\n";

// UI pass shader: takes vertex positions already in pixel space and
// maps them to NDC directly (an orthographic projection in all but
// name -- Section 4.6), plus a per-vertex color tint so the same
// textured quad can draw plain glyphs, tinted panels/borders, and
// full-color icons through one pipeline.
static const char* g_uiShaderSrc =
    "cbuffer UICB : register(b0) { float4 screenSize; };\n"
    "struct VSIn { float2 pos:POSITION; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
    "PSIn VSMain(VSIn input) {\n"
    "    PSIn o;\n"
    "    float2 ndc = float2(input.pos.x / screenSize.x * 2.0f - 1.0f, 1.0f - input.pos.y / screenSize.y * 2.0f);\n"
    "    o.pos = float4(ndc, 0.0f, 1.0f);\n"
    "    o.uv = input.uv;\n"
    "    o.col = input.col;\n"
    "    return o;\n"
    "}\n"
    "Texture2D tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "float4 PSMain(PSIn input) : SV_TARGET { return tex0.Sample(samp0, input.uv) * input.col; }\n";

// Emits one cube face (4 verts + 6 indices) for the given corners.
static void EmitFace(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                      float x, float y, float z,
                      float c0x, float c0y, float c0z,
                      float c1x, float c1y, float c1z,
                      float c2x, float c2y, float c2z,
                      float c3x, float c3y, float c3z,
                      float u0, float v0, float u1, float v1) {
    uint32_t base = (uint32_t)verts.size();
    verts.push_back({ x + c0x, y + c0y, z + c0z, u0, v1 });
    verts.push_back({ x + c1x, y + c1y, z + c1z, u0, v0 });
    verts.push_back({ x + c2x, y + c2y, z + c2z, u1, v0 });
    verts.push_back({ x + c3x, y + c3y, z + c3z, u1, v1 });
    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
}

static void RebuildChunkMesh(World& w, const ChunkCoord& cc, Chunk& c) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    c.pipes.clear();

    int baseX = cc.x * CHUNK_SIZE, baseY = cc.y * CHUNK_SIZE, baseZ = cc.z * CHUNK_SIZE;

    for (int ly = 0; ly < CHUNK_SIZE; ly++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                BlockID id = (BlockID)c.blocks[Chunk::LocalIndex(lx, ly, lz)];
                if (id == BLOCK_AIR) continue;
                int wx = baseX + lx, wy = baseY + ly, wz = baseZ + lz;

                if (g_info[id].shape != 0) {
                    c.pipes.push_back({ wx, wy, wz, g_info[id].shape });
                    continue;
                }

                float u0, v0, u1, v1;
                AtlasRect(g_info[id].tex, u0, v0, u1, v1);
                float x = (float)wx, y = (float)wy, z = (float)wz;

                if (!w.Solid(wx + 1, wy, wz))
                    EmitFace(verts, indices, x, y, z, 1,0,0, 1,1,0, 1,1,1, 1,0,1, u0,v0,u1,v1);
                if (!w.Solid(wx - 1, wy, wz))
                    EmitFace(verts, indices, x, y, z, 0,0,1, 0,1,1, 0,1,0, 0,0,0, u0,v0,u1,v1);
                if (!w.Solid(wx, wy + 1, wz))
                    EmitFace(verts, indices, x, y, z, 0,1,0, 0,1,1, 1,1,1, 1,1,0, u0,v0,u1,v1);
                if (!w.Solid(wx, wy - 1, wz))
                    EmitFace(verts, indices, x, y, z, 0,0,1, 0,0,0, 1,0,0, 1,0,1, u0,v0,u1,v1);
                if (!w.Solid(wx, wy, wz + 1))
                    EmitFace(verts, indices, x, y, z, 1,0,1, 1,1,1, 0,1,1, 0,0,1, u0,v0,u1,v1);
                if (!w.Solid(wx, wy, wz - 1))
                    EmitFace(verts, indices, x, y, z, 0,0,0, 0,1,0, 1,1,0, 1,0,0, u0,v0,u1,v1);
            }
        }
    }

    if (c.vb) { c.vb->Release(); c.vb = nullptr; }
    if (c.ib) { c.ib->Release(); c.ib = nullptr; }
    c.indexCount = 0;

    if (!verts.empty()) {
        D3D11_BUFFER_DESC vbd = {};
        vbd.Usage = D3D11_USAGE_DEFAULT;
        vbd.ByteWidth = (UINT)(verts.size() * sizeof(Vertex));
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vinit = {};
        vinit.pSysMem = verts.data();
        g_device->CreateBuffer(&vbd, &vinit, &c.vb);

        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA iinit = {};
        iinit.pSysMem = indices.data();
        g_device->CreateBuffer(&ibd, &iinit, &c.ib);

        c.indexCount = (UINT)indices.size();
    }

    c.dirty = false;
}

static void RebuildDirtyChunks(World& w) {
    for (auto& kv : w.chunks) {
        if (kv.second->dirty) RebuildChunkMesh(w, kv.first, *kv.second);
    }
}

// Small placeholder mesh shared by every pipe instance regardless of
// shape -- distinct L/plus geometry per shape is left for Milestone 2
// alongside real network connectivity (Section 4.4 flags per-instance
// draw calls as a known scaling limit already).
static void BuildPipeMesh() {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    float s = 0.3f, lo = 0.5f - s, hi = 0.5f + s;
    float u0 = 0.05f, v0 = 0.05f, u1 = 0.95f, v1 = 0.95f;
    EmitFace(verts, indices, 0,0,0, hi,lo,lo, hi,hi,lo, hi,hi,hi, hi,lo,hi, u0,v0,u1,v1);
    EmitFace(verts, indices, 0,0,0, lo,lo,hi, lo,hi,hi, lo,hi,lo, lo,lo,lo, u0,v0,u1,v1);
    EmitFace(verts, indices, 0,0,0, lo,hi,lo, lo,hi,hi, hi,hi,hi, hi,hi,lo, u0,v0,u1,v1);
    EmitFace(verts, indices, 0,0,0, lo,lo,hi, lo,lo,lo, hi,lo,lo, hi,lo,hi, u0,v0,u1,v1);
    EmitFace(verts, indices, 0,0,0, hi,lo,hi, hi,hi,hi, lo,hi,hi, lo,lo,hi, u0,v0,u1,v1);
    EmitFace(verts, indices, 0,0,0, lo,lo,lo, lo,hi,lo, hi,hi,lo, hi,lo,lo, u0,v0,u1,v1);

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(verts.size() * sizeof(Vertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = {}; vinit.pSysMem = verts.data();
    g_device->CreateBuffer(&vbd, &vinit, &g_pipeMeshVB);

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = {}; iinit.pSysMem = indices.data();
    g_device->CreateBuffer(&ibd, &iinit, &g_pipeMeshIB);

    g_pipeMeshIndexCount = (UINT)indices.size();
}

static void UpdateCBuffer(const Mat4& mvp) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    CBData* data = (CBData*)mapped.pData;
    data->mvp = mvp;
    g_context->Unmap(g_cbuffer, 0);
}

// =======================================================================
// Part IV.7 - Camera / player
// =======================================================================

struct Player {
    float x = 8.0f, y = 50.0f, z = 8.0f;
    float yaw = 0.0f, pitch = 0.0f;
    float velY = 0.0f;
    bool onGround = false;
    int hotbarIndex = 0;
};

static const BlockID g_placeable[] = {
    BLOCK_FOUNDATION, BLOCK_STONE, BLOCK_DIRT, BLOCK_WOOD,
    BLOCK_CHEST, BLOCK_MACHINE, BLOCK_PIPE_STRAIGHT, BLOCK_PIPE_CORNER, BLOCK_PIPE_JUNCTION
};
static const int g_placeableCount = sizeof(g_placeable) / sizeof(g_placeable[0]);

static void GetCameraVectors(const Player& p, Vec3& forward, Vec3& right, Vec3& up) {
    float cp = cosf(p.pitch), sp = sinf(p.pitch);
    float cy = cosf(p.yaw), sy = sinf(p.yaw);
    forward = { sy * cp, sp, cy * cp };
    up = { 0, 1, 0 };
    right = Normalize(Cross(up, forward));
}

// Player half-width / height used for AABB collision (Section 4.7 /
// Prismative concept: sample multiple heights against solid voxels).
static const float PLAYER_HALFW = 0.3f;
static const float PLAYER_HEIGHT = 1.8f;
static const float PLAYER_EYE = 1.6f;

static bool BoxIntersectsSolid(World& w, float cx, float cy, float cz) {
    int minX = (int)floor(cx - PLAYER_HALFW), maxX = (int)floor(cx + PLAYER_HALFW);
    int minY = (int)floor(cy),                  maxY = (int)floor(cy + PLAYER_HEIGHT);
    int minZ = (int)floor(cz - PLAYER_HALFW), maxZ = (int)floor(cz + PLAYER_HALFW);
    for (int x = minX; x <= maxX; x++)
        for (int y = minY; y <= maxY; y++)
            for (int z = minZ; z <= maxZ; z++)
                if (w.Solid(x, y, z)) return true;
    return false;
}

static void UpdatePlayerPhysics(World& w, Player& p, float dt, bool fwd, bool back, bool left, bool right, bool jump) {
    Vec3 f, r, u;
    GetCameraVectors(p, f, r, u);
    float fx = f.x, fz = f.z;
    float rx = r.x, rz = r.z;
    float len = sqrtf(fx * fx + fz * fz);
    if (len > 0.0001f) { fx /= len; fz /= len; }

    const float SPEED = 4.5f;
    float mx = 0, mz = 0;
    if (fwd)  { mx += fx; mz += fz; }
    if (back) { mx -= fx; mz -= fz; }
    if (right){ mx += rx; mz += rz; }
    if (left) { mx -= rx; mz -= rz; }
    float mlen = sqrtf(mx * mx + mz * mz);
    if (mlen > 0.0001f) { mx = mx / mlen * SPEED * dt; mz = mz / mlen * SPEED * dt; }

    if (!BoxIntersectsSolid(w, p.x + mx, p.y, p.z)) p.x += mx;
    if (!BoxIntersectsSolid(w, p.x, p.y, p.z + mz)) p.z += mz;

    const float GRAVITY = 20.0f;
    const float JUMP_SPEED = 7.0f;
    if (p.onGround && jump) { p.velY = JUMP_SPEED; p.onGround = false; }
    p.velY -= GRAVITY * dt;
    if (p.velY < -50.0f) p.velY = -50.0f;

    float dy = p.velY * dt;
    if (!BoxIntersectsSolid(w, p.x, p.y + dy, p.z)) {
        p.y += dy;
        p.onGround = false;
    } else {
        if (p.velY < 0) p.onGround = true;
        p.velY = 0;
    }
}

// =======================================================================
// Part 4.5 - Amanatides-Woo exact voxel DDA raycast for block picking
// =======================================================================

static bool Raycast(World& w, float ox, float oy, float oz, float dx, float dy, float dz, float maxDist,
                     int& hitX, int& hitY, int& hitZ, int& placeX, int& placeY, int& placeZ) {
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6f) return false;
    dx /= len; dy /= len; dz /= len;

    int voxX = (int)floor(ox), voxY = (int)floor(oy), voxZ = (int)floor(oz);
    int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    int stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    int stepZ = dz > 0 ? 1 : (dz < 0 ? -1 : 0);

    auto tDeltaOf = [](float d) { return d == 0.0f ? FLT_MAX : fabsf(1.0f / d); };
    float tDeltaX = tDeltaOf(dx), tDeltaY = tDeltaOf(dy), tDeltaZ = tDeltaOf(dz);

    auto tMaxOf = [](float origin, int vox, int step, float d) {
        if (step == 0) return FLT_MAX;
        float boundary = step > 0 ? (float)(vox + 1) : (float)vox;
        return (boundary - origin) / d;
    };
    float tMaxX = tMaxOf(ox, voxX, stepX, dx);
    float tMaxY = tMaxOf(oy, voxY, stepY, dy);
    float tMaxZ = tMaxOf(oz, voxZ, stepZ, dz);

    int prevX = voxX, prevY = voxY, prevZ = voxZ;
    float traveled = 0.0f;

    if (w.Solid(voxX, voxY, voxZ)) {
        hitX = voxX; hitY = voxY; hitZ = voxZ;
        placeX = voxX; placeY = voxY; placeZ = voxZ;
        return true;
    }

    while (traveled <= maxDist) {
        prevX = voxX; prevY = voxY; prevZ = voxZ;
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            voxX += stepX; traveled = tMaxX; tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            voxY += stepY; traveled = tMaxY; tMaxY += tDeltaY;
        } else {
            voxZ += stepZ; traveled = tMaxZ; tMaxZ += tDeltaZ;
        }

        if (w.Solid(voxX, voxY, voxZ)) {
            hitX = voxX; hitY = voxY; hitZ = voxZ;
            placeX = prevX; placeY = prevY; placeZ = prevZ;
            return true;
        }
    }
    return false;
}

// =======================================================================
// Part VII - Save / load (crash-safe, versioned, name-indexed)
// =======================================================================

static const uint32_t SAVE_VERSION = 1;
static const char* SAVE_PATH = "voxelproto.sav";

static uint32_t Fnv1a(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

static void AppendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
static void AppendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v >> 8) & 0xFF));
}
static void AppendU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}
static void AppendI32(std::vector<uint8_t>& b, int32_t v) { AppendU32(b, (uint32_t)v); }
static void AppendF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits; memcpy(&bits, &v, 4); AppendU32(b, bits);
}
static void AppendStr(std::vector<uint8_t>& b, const char* s) {
    uint16_t len = (uint16_t)strlen(s);
    AppendU16(b, len);
    for (uint16_t i = 0; i < len; i++) b.push_back((uint8_t)s[i]);
}

struct Reader {
    const uint8_t* data; size_t size; size_t pos = 0;
    bool ok = true;
    bool need(size_t n) { if (pos + n > size) { ok = false; return false; } return true; }
    uint8_t ReadU8() { if (!need(1)) return 0; return data[pos++]; }
    uint16_t ReadU16() { if (!need(2)) return 0; uint16_t v = data[pos] | (data[pos+1] << 8); pos += 2; return v; }
    uint32_t ReadU32() { if (!need(4)) return 0; uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)data[pos+i] << (8*i); pos += 4; return v; }
    int32_t ReadI32() { return (int32_t)ReadU32(); }
    float ReadF32() { uint32_t bits = ReadU32(); float f; memcpy(&f, &bits, 4); return f; }
    std::string ReadStr() {
        uint16_t len = ReadU16();
        if (!need(len)) return "";
        std::string s((const char*)&data[pos], len);
        pos += len;
        return s;
    }
};

static void SaveGame(World& w, Player& p) {
    std::vector<uint8_t> buf;
    AppendU32(buf, ('G' << 24) | ('L' << 16) | ('X' << 8) | 'V'); // magic "VXLG" (little-endian on disk)
    AppendU32(buf, SAVE_VERSION);

    AppendF32(buf, p.x); AppendF32(buf, p.y); AppendF32(buf, p.z);
    AppendF32(buf, p.yaw); AppendF32(buf, p.pitch);
    AppendI32(buf, p.hotbarIndex);

    AppendU32(buf, BLOCK_COUNT);
    for (int i = 0; i < BLOCK_COUNT; i++) AppendStr(buf, g_blockNames[i]);

    // Count non-air blocks first.
    uint32_t blockCount = 0;
    for (auto& kv : w.chunks) {
        Chunk& c = *kv.second;
        for (int i = 0; i < CHUNK_CELLS; i++) if (c.blocks[i] != BLOCK_AIR) blockCount++;
    }
    AppendU32(buf, blockCount);
    for (auto& kv : w.chunks) {
        const ChunkCoord& cc = kv.first;
        Chunk& c = *kv.second;
        int baseX = cc.x * CHUNK_SIZE, baseY = cc.y * CHUNK_SIZE, baseZ = cc.z * CHUNK_SIZE;
        for (int ly = 0; ly < CHUNK_SIZE; ly++)
            for (int lz = 0; lz < CHUNK_SIZE; lz++)
                for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                    uint8_t id = c.blocks[Chunk::LocalIndex(lx, ly, lz)];
                    if (id == BLOCK_AIR) continue;
                    AppendI32(buf, baseX + lx);
                    AppendI32(buf, baseY + ly);
                    AppendI32(buf, baseZ + lz);
                    AppendU8(buf, id);
                }
    }

    uint32_t checksum = Fnv1a(buf.data(), buf.size());
    AppendU32(buf, checksum);

    // Crash-safe write sequence (Section 7.3): write to .tmp, only then
    // rotate the previous save to .bak and rename .tmp into place.
    namespace fs = std::filesystem;
    std::string tmpPath = std::string(SAVE_PATH) + ".tmp";
    std::string bakPath = std::string(SAVE_PATH) + ".bak";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write((const char*)buf.data(), (std::streamsize)buf.size());
        if (!out) return;
    }
    std::error_code ec;
    if (fs::exists(SAVE_PATH, ec)) {
        fs::remove(bakPath, ec);
        fs::rename(SAVE_PATH, bakPath, ec);
    }
    fs::rename(tmpPath, SAVE_PATH, ec);
}

static bool LoadGame(World& w, Player& p) {
    std::ifstream in(SAVE_PATH, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize size = in.tellg();
    if (size < 12) return false;
    in.seekg(0);
    std::vector<uint8_t> buf((size_t)size);
    in.read((char*)buf.data(), size);
    if (!in) return false;

    if (buf.size() < 4) return false;
    uint32_t storedChecksum;
    memcpy(&storedChecksum, buf.data() + buf.size() - 4, 4);
    uint32_t computed = Fnv1a(buf.data(), buf.size() - 4);
    if (storedChecksum != computed) {
        OutputDebugStringA("LoadGame: checksum mismatch, aborting load\n");
        return false;
    }

    Reader r{ buf.data(), buf.size() - 4 };
    uint32_t magic = r.ReadU32();
    uint32_t expectedMagic = ('G' << 24) | ('L' << 16) | ('X' << 8) | 'V';
    if (magic != expectedMagic) {
        OutputDebugStringA("LoadGame: bad magic, aborting load\n");
        return false;
    }
    uint32_t version = r.ReadU32();
    if (version != SAVE_VERSION) {
        OutputDebugStringA("LoadGame: unsupported version, aborting load\n");
        return false;
    }

    Player loaded;
    loaded.x = r.ReadF32(); loaded.y = r.ReadF32(); loaded.z = r.ReadF32();
    loaded.yaw = r.ReadF32(); loaded.pitch = r.ReadF32();
    loaded.hotbarIndex = r.ReadI32();

    uint32_t nameCount = r.ReadU32();
    std::vector<std::string> savedNames(nameCount);
    for (uint32_t i = 0; i < nameCount; i++) savedNames[i] = r.ReadStr();
    if (!r.ok) return false;

    // Remap saved name index -> current BlockID. Anything no longer
    // present maps to AIR with a logged warning (Section 7.4) rather
    // than silently reinterpreting whatever ID occupies that slot today.
    std::vector<BlockID> remap(nameCount, BLOCK_AIR);
    for (uint32_t i = 0; i < nameCount; i++) {
        bool found = false;
        for (int b = 0; b < BLOCK_COUNT; b++) {
            if (savedNames[i] == g_blockNames[b]) { remap[i] = (BlockID)b; found = true; break; }
        }
        if (!found) {
            char msg[256];
            snprintf(msg, sizeof(msg), "LoadGame: unknown block name '%s', mapping to air\n", savedNames[i].c_str());
            OutputDebugStringA(msg);
        }
    }

    uint32_t blockCount = r.ReadU32();
    if (!r.ok) return false;

    World fresh; // build into a scratch world; only swap in if fully valid
    for (uint32_t i = 0; i < blockCount; i++) {
        int32_t x = r.ReadI32(), y = r.ReadI32(), z = r.ReadI32();
        uint8_t nameIdx = r.ReadU8();
        if (!r.ok) return false;
        BlockID id = (nameIdx < remap.size()) ? remap[nameIdx] : BLOCK_AIR;
        fresh.SetRaw(x, y, z, id); // bulk load path, no gravity (Section 5.2)
    }

    w.chunks = std::move(fresh.chunks);
    p = loaded;
    g_generatedColumns.clear();
    g_lastPlayerChunkX = INT32_MIN;
    g_lastPlayerChunkZ = INT32_MIN;
    return true;
}

// =======================================================================
// D3D11 initialization
// =======================================================================

static bool InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = SCREEN_W;
    scd.BufferDesc.Height = SCREEN_H;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL chosen;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &scd, &g_swapChain, &g_device, &chosen, &g_context);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = SCREEN_W; depthDesc.Height = SCREEN_H;
    depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depthTex = nullptr;
    g_device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    g_device->CreateDepthStencilView(depthTex, nullptr, &g_dsv);
    depthTex->Release();

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)SCREEN_W; vp.Height = (float)SCREEN_H;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);

    ID3DBlob* vsBlob = nullptr, * psBlob = nullptr, * errBlob = nullptr;
    hr = D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_layout);
    vsBlob->Release();
    psBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(CBData);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&cbd, nullptr, &g_cbuffer);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_device->CreateSamplerState(&sampDesc, &g_sampler);

    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = TRUE;
    g_device->CreateRasterizerState(&rastDesc, &g_rasterState);

    D3D11_DEPTH_STENCIL_DESC depthStateDesc = {};
    depthStateDesc.DepthEnable = TRUE;
    depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    g_device->CreateDepthStencilState(&depthStateDesc, &g_depthState);

    // --- UI pass pipeline objects (Section 4.6: a second pass, its own
    // shaders, orthographic-in-pixel-space, depth off, alpha blend on) ---
    ID3DBlob* uiVsBlob = nullptr, * uiPsBlob = nullptr;
    hr = D3DCompile(g_uiShaderSrc, strlen(g_uiShaderSrc), nullptr, nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &uiVsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(g_uiShaderSrc, strlen(g_uiShaderSrc), nullptr, nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &uiPsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    g_device->CreateVertexShader(uiVsBlob->GetBufferPointer(), uiVsBlob->GetBufferSize(), nullptr, &g_uiVS);
    g_device->CreatePixelShader(uiPsBlob->GetBufferPointer(), uiPsBlob->GetBufferSize(), nullptr, &g_uiPS);

    D3D11_INPUT_ELEMENT_DESC uiLayoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(uiLayoutDesc, 3, uiVsBlob->GetBufferPointer(), uiVsBlob->GetBufferSize(), &g_uiLayout);
    uiVsBlob->Release();
    uiPsBlob->Release();

    D3D11_BUFFER_DESC uiCbd = {};
    uiCbd.Usage = D3D11_USAGE_DYNAMIC;
    uiCbd.ByteWidth = sizeof(float) * 4; // screenSize.xy, padding
    uiCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    uiCbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&uiCbd, nullptr, &g_uiCBuffer);

    D3D11_BUFFER_DESC uiVbd = {};
    uiVbd.Usage = D3D11_USAGE_DYNAMIC;
    uiVbd.ByteWidth = UI_VB_CAPACITY * sizeof(UIVertex);
    uiVbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    uiVbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&uiVbd, nullptr, &g_uiVB);

    D3D11_SAMPLER_DESC uiSampDesc = {};
    uiSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    uiSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    uiSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    uiSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    uiSampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_device->CreateSamplerState(&uiSampDesc, &g_uiSampler);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&blendDesc, &g_uiBlendState);

    D3D11_DEPTH_STENCIL_DESC uiDepthDesc = {};
    uiDepthDesc.DepthEnable = FALSE;
    uiDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    g_device->CreateDepthStencilState(&uiDepthDesc, &g_uiDepthState);

    return true;
}

static bool InitTextures() {
    uint8_t* atlasPixels = nullptr; int atlasW = 0, atlasH = 0;
    uint8_t* pipePixels = nullptr; int pipeSize = 0;
    if (!GenerateGameTextures(TILE_SIZE, ATLAS_COLS, ATLAS_ROWS, &atlasPixels, &atlasW, &atlasH, &pipePixels, &pipeSize))
        return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = atlasW; td.Height = atlasH;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = atlasPixels;
    sd.SysMemPitch = atlasW * 4;
    ID3D11Texture2D* atlasTex = nullptr;
    g_device->CreateTexture2D(&td, &sd, &atlasTex);
    g_device->CreateShaderResourceView(atlasTex, nullptr, &g_atlasSRV);
    atlasTex->Release();

    D3D11_TEXTURE2D_DESC td2 = {};
    td2.Width = pipeSize; td2.Height = pipeSize;
    td2.MipLevels = 1; td2.ArraySize = 1;
    td2.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td2.SampleDesc.Count = 1;
    td2.Usage = D3D11_USAGE_IMMUTABLE;
    td2.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd2 = {};
    sd2.pSysMem = pipePixels;
    sd2.SysMemPitch = pipeSize * 4;
    ID3D11Texture2D* pipeTex = nullptr;
    g_device->CreateTexture2D(&td2, &sd2, &pipeTex);
    g_device->CreateShaderResourceView(pipeTex, nullptr, &g_pipeSRV);
    pipeTex->Release();

    FreeGeneratedPixels(atlasPixels);
    FreeGeneratedPixels(pipePixels);

    uint8_t* uiPixels = nullptr; int uiW = 0, uiH = 0;
    if (!GenerateUIAtlas(UI_CELL_W, UI_CELL_H, UI_ATLAS_COLS, UI_ATLAS_ROWS, &uiPixels, &uiW, &uiH))
        return false;

    D3D11_TEXTURE2D_DESC td3 = {};
    td3.Width = uiW; td3.Height = uiH;
    td3.MipLevels = 1; td3.ArraySize = 1;
    td3.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td3.SampleDesc.Count = 1;
    td3.Usage = D3D11_USAGE_IMMUTABLE;
    td3.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd3 = {};
    sd3.pSysMem = uiPixels;
    sd3.SysMemPitch = uiW * 4;
    ID3D11Texture2D* uiTex = nullptr;
    g_device->CreateTexture2D(&td3, &sd3, &uiTex);
    g_device->CreateShaderResourceView(uiTex, nullptr, &g_uiSRV);
    uiTex->Release();

    FreeGeneratedPixels(uiPixels);
    return true;
}

// =======================================================================
// WinMain / message loop
// =======================================================================

static World g_world;
static Player g_player;
static bool g_mouseCaptured = false;
static bool g_keyDown[256] = {};
static bool g_menuOpen = false;
static int g_mouseX = 0, g_mouseY = 0;

static void PickAndAct(bool breakBlock) {
    Vec3 f, r, u;
    GetCameraVectors(g_player, f, r, u);
    float dx = f.x, dy = f.y, dz = f.z;
    float ex = g_player.x, ey = g_player.y + PLAYER_EYE, ez = g_player.z;

    int hx, hy, hz, px, py, pz;
    if (!Raycast(g_world, ex, ey, ez, dx, dy, dz, 6.0f, hx, hy, hz, px, py, pz)) return;

    if (breakBlock) {
        LiveEdit(g_world, hx, hy, hz, BLOCK_AIR);
    } else {
        BlockID toPlace = g_placeable[g_player.hotbarIndex];
        LiveEdit(g_world, px, py, pz, toPlace);
    }
}

// Captures and hides the cursor, re-centering it, to enter FPS look mode.
static void CaptureMouseForPlay() {
    g_mouseCaptured = true;
    ShowCursor(FALSE);
    SetCapture(g_hwnd);
    RECT rc; GetClientRect(g_hwnd, &rc);
    POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
    ClientToScreen(g_hwnd, &center);
    SetCursorPos(center.x, center.y);
}

// Releases the cursor so it can move freely over menu buttons.
static void ReleaseMouseForMenu() {
    g_mouseCaptured = false;
    ShowCursor(TRUE);
    ReleaseCapture();
}

// Pause-menu layout: shared between rendering (RenderUIPass) and click
// hit-testing (HandleMenuClick) so the two can never drift apart.
struct UIRect { float x0, y0, x1, y1; };
static const int MENU_BUTTON_COUNT = 4;
static const char* g_menuLabels[MENU_BUTTON_COUNT] = { "RESUME", "SAVE GAME", "LOAD GAME", "QUIT" };
static const float MENU_PANEL_W = 320.0f, MENU_PANEL_H = 280.0f;

static UIRect GetMenuPanelRect() {
    float px = (SCREEN_W - MENU_PANEL_W) / 2.0f;
    float py = (SCREEN_H - MENU_PANEL_H) / 2.0f;
    return { px, py, px + MENU_PANEL_W, py + MENU_PANEL_H };
}
static UIRect GetMenuButtonRect(int index) {
    UIRect panel = GetMenuPanelRect();
    float bw = 260.0f, bh = 40.0f, gap = 12.0f;
    float bx = panel.x0 + 30.0f;
    float by = panel.y0 + 70.0f + index * (bh + gap);
    return { bx, by, bx + bw, by + bh };
}
static bool PointInRect(int px, int py, const UIRect& r) {
    return px >= r.x0 && px <= r.x1 && py >= r.y0 && py <= r.y1;
}

static void HandleMenuClick(int mx, int my) {
    for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
        if (!PointInRect(mx, my, GetMenuButtonRect(i))) continue;
        switch (i) {
        case 0: // RESUME
            g_menuOpen = false;
            CaptureMouseForPlay();
            break;
        case 1: // SAVE GAME
            SaveGame(g_world, g_player);
            break;
        case 2: // LOAD GAME
            LoadGame(g_world, g_player);
            g_menuOpen = false;
            CaptureMouseForPlay();
            break;
        case 3: // QUIT
            PostQuitMessage(0);
            break;
        }
        return;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEMOVE:
        g_mouseX = (int)(short)LOWORD(lParam);
        g_mouseY = (int)(short)HIWORD(lParam);
        return 0;
    case WM_LBUTTONDOWN: {
        int mx = (int)(short)LOWORD(lParam), my = (int)(short)HIWORD(lParam);
        if (g_menuOpen) {
            HandleMenuClick(mx, my);
        } else if (!g_mouseCaptured) {
            CaptureMouseForPlay();
        } else {
            PickAndAct(true);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
        if (g_mouseCaptured && !g_menuOpen) PickAndAct(false);
        return 0;
    case WM_KEYDOWN:
        if (wParam < 256) g_keyDown[wParam] = true;
        if (wParam == VK_ESCAPE) {
            if (g_menuOpen) {
                g_menuOpen = false;
                CaptureMouseForPlay();
            } else {
                g_menuOpen = true;
                ReleaseMouseForMenu();
            }
        } else if (wParam >= '1' && wParam <= '9' && !g_menuOpen) {
            int idx = (int)(wParam - '1');
            if (idx < g_placeableCount) g_player.hotbarIndex = idx;
        } else if (wParam == VK_F5) {
            SaveGame(g_world, g_player);
        } else if (wParam == VK_F9) {
            LoadGame(g_world, g_player);
        }
        return 0;
    case WM_KEYUP:
        if (wParam < 256) g_keyDown[wParam] = false;
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// Draws one small dynamic-VB batch through the UI pipeline. Called
// several times per frame (once per bound texture) since the UI pass
// mixes the font/white atlas with the block atlases for hotbar icons.
static void UIDrawBatch(const std::vector<UIVertex>& verts, ID3D11ShaderResourceView* srv) {
    if (verts.empty()) return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_uiVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    size_t count = std::min<size_t>(verts.size(), UI_VB_CAPACITY);
    memcpy(mapped.pData, verts.data(), count * sizeof(UIVertex));
    g_context->Unmap(g_uiVB, 0);

    UINT stride = sizeof(UIVertex), offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_uiVB, &stride, &offset);
    g_context->PSSetShaderResources(0, 1, &srv);
    g_context->Draw((UINT)count, 0);
}

// Second pass: orthographic-in-pixel-space, depth off, alpha blend on
// (Section 4.6). Builds the crosshair, hotbar, "click to play" hint and
// pause menu as CPU-side quad lists, then draws them through the UI
// pipeline set up in InitD3D. Assumes the world pass already ran this
// frame (so g_context's shader/IA state gets fully re-set here rather
// than assumed).
static void RenderUIPass() {
    std::vector<UIVertex> glyphVerts; // font atlas + white cell (panels, borders, text, crosshair)

    if (g_mouseCaptured && !g_menuOpen) {
        float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
        UIDrawRect(glyphVerts, cx - 8, cy - 1, cx + 8, cy + 1, 1, 1, 1, 0.85f);
        UIDrawRect(glyphVerts, cx - 1, cy - 8, cx + 1, cy + 8, 1, 1, 1, 0.85f);
    }

    struct IconQuad { float x0, y0, x1, y1, u0, v0, u1, v1; bool pipe; };
    std::vector<IconQuad> icons;

    const int SLOT = 48, GAP = 4;
    int hotbarN = g_placeableCount;
    int totalW = hotbarN * SLOT + (hotbarN - 1) * GAP;
    float hbStartX = (SCREEN_W - totalW) / 2.0f;
    float hbY0 = SCREEN_H - SLOT - 16.0f;
    for (int i = 0; i < hotbarN; i++) {
        float x0 = hbStartX + i * (SLOT + GAP), x1 = x0 + SLOT;
        float y0 = hbY0, y1 = y0 + SLOT;
        bool selected = (i == g_player.hotbarIndex);
        if (selected) UIDrawRect(glyphVerts, x0 - 4, y0 - 4, x1 + 4, y1 + 4, 1.0f, 0.9f, 0.2f, 0.9f);
        UIDrawRect(glyphVerts, x0, y0, x1, y1, 0.12f, 0.12f, 0.12f, 0.75f);

        BlockID b = g_placeable[i];
        float iu0, iv0, iu1, iv1;
        bool pipe = g_info[b].shape != 0;
        if (pipe) { iu0 = 0.05f; iv0 = 0.05f; iu1 = 0.95f; iv1 = 0.95f; }
        else AtlasRect(g_info[b].tex, iu0, iv0, iu1, iv1);
        icons.push_back({ x0 + 6, y0 + 6, x1 - 6, y1 - 6, iu0, iv0, iu1, iv1, pipe });
    }

    if (!g_menuOpen) {
        std::string name = g_blockNames[g_placeable[g_player.hotbarIndex]];
        float scale = 0.8f;
        float tw = UITextWidth(name, scale);
        UIDrawText(glyphVerts, name, (SCREEN_W - tw) / 2.0f, hbY0 - 26.0f, scale, 1, 1, 1, 0.9f);
    }

    if (!g_mouseCaptured && !g_menuOpen) {
        std::string hint = "CLICK TO PLAY";
        float scale = 1.3f;
        float tw = UITextWidth(hint, scale);
        UIDrawText(glyphVerts, hint, (SCREEN_W - tw) / 2.0f, SCREEN_H * 0.42f, scale, 1, 1, 1, 0.9f);
    }

    if (g_menuOpen) {
        UIDrawRect(glyphVerts, 0, 0, (float)SCREEN_W, (float)SCREEN_H, 0, 0, 0, 0.55f);
        UIRect panel = GetMenuPanelRect();
        UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.10f, 0.10f, 0.13f, 0.95f);

        std::string title = "PAUSED";
        float titleScale = 1.3f;
        float titleW = UITextWidth(title, titleScale);
        UIDrawText(glyphVerts, title, panel.x0 + (MENU_PANEL_W - titleW) / 2.0f, panel.y0 + 16.0f, titleScale, 1, 1, 1, 1);

        for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
            UIRect r = GetMenuButtonRect(i);
            bool hover = PointInRect(g_mouseX, g_mouseY, r);
            float shade = hover ? 0.32f : 0.22f;
            UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, shade, shade, shade + 0.06f, 1);
            std::string label = g_menuLabels[i];
            float lw = UITextWidth(label, 1.0f);
            UIDrawText(glyphVerts, label, r.x0 + ((r.x1 - r.x0) - lw) / 2.0f, r.y0 + 10.0f, 1.0f, 1, 1, 1, 1);
        }
    }

    g_context->OMSetDepthStencilState(g_uiDepthState, 0);
    float blendFactor[4] = { 0, 0, 0, 0 };
    g_context->OMSetBlendState(g_uiBlendState, blendFactor, 0xFFFFFFFF);
    g_context->VSSetShader(g_uiVS, nullptr, 0);
    g_context->PSSetShader(g_uiPS, nullptr, 0);
    g_context->IASetInputLayout(g_uiLayout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetConstantBuffers(0, 1, &g_uiCBuffer);
    g_context->PSSetSamplers(0, 1, &g_uiSampler);

    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_uiCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        float* f = (float*)mapped.pData;
        f[0] = (float)SCREEN_W; f[1] = (float)SCREEN_H; f[2] = 0; f[3] = 0;
        g_context->Unmap(g_uiCBuffer, 0);
    }

    UIDrawBatch(glyphVerts, g_uiSRV);

    for (auto& ic : icons) {
        std::vector<UIVertex> iconVerts;
        UIAddQuad(iconVerts, ic.x0, ic.y0, ic.x1, ic.y1, ic.u0, ic.v0, ic.u1, ic.v1, 1, 1, 1, 1);
        UIDrawBatch(iconVerts, ic.pipe ? g_pipeSRV : g_atlasSRV);
    }

    // Restore world-pass defaults so next frame's world draws don't
    // inherit UI blend/depth state.
    g_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depthState, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "VoxelLogisticsWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    RECT wr = { 0, 0, SCREEN_W, SCREEN_H };
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    AdjustWindowRect(&wr, style, FALSE);
    g_hwnd = CreateWindowA("VoxelLogisticsWindowClass", "Voxel Logistics",
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            wr.right - wr.left, wr.bottom - wr.top,
                            nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return -1;
    ShowWindow(g_hwnd, nCmdShow);

    if (!InitD3D(g_hwnd)) return -1;
    if (!InitTextures()) return -1;
    BuildPipeMesh();

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);
    const float FIXED_DT = 1.0f / 60.0f;
    float accumulator = 0.0f;

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - lastTime.QuadPart) / (float)freq.QuadPart;
        lastTime = now;
        if (dt > 0.25f) dt = 0.25f; // clamp huge stalls (e.g. window drag)
        accumulator += dt;

        if (g_mouseCaptured) {
            POINT cursor; GetCursorPos(&cursor);
            RECT rc; GetClientRect(g_hwnd, &rc);
            POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
            ClientToScreen(g_hwnd, &center);
            int dx = cursor.x - center.x, dy = cursor.y - center.y;
            const float SENS = 0.0025f;
            g_player.yaw += dx * SENS;
            g_player.pitch -= dy * SENS;
            if (g_player.pitch > 1.55f) g_player.pitch = 1.55f;
            if (g_player.pitch < -1.55f) g_player.pitch = -1.55f;
            SetCursorPos(center.x, center.y);
        }

        // Fixed-timestep simulation, decoupled from render/present rate
        // (Section 5.3's recommended accumulator approach). While the
        // pause menu is open the world is frozen and the accumulator is
        // dropped rather than left to build up, so resuming doesn't
        // trigger a burst of catch-up ticks for however long it was paused.
        if (g_menuOpen) {
            accumulator = 0.0f;
        } else {
            while (accumulator >= FIXED_DT) {
                int pcx = FloorDiv16((int)floor(g_player.x));
                int pcz = FloorDiv16((int)floor(g_player.z));
                EnsureChunksLoaded(g_world, pcx, pcz);

                bool fwd = g_keyDown['W'], back = g_keyDown['S'];
                bool left = g_keyDown['A'], right = g_keyDown['D'];
                bool jump = g_keyDown[VK_SPACE];
                UpdatePlayerPhysics(g_world, g_player, FIXED_DT, fwd, back, left, right, jump);
                ProcessFalls(g_world);

                accumulator -= FIXED_DT;
            }
        }

        RebuildDirtyChunks(g_world);

        float clearColor[4] = { 0.4f, 0.6f, 0.9f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, g_dsv);
        g_context->ClearRenderTargetView(g_rtv, clearColor);
        g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        g_context->RSSetState(g_rasterState);
        g_context->OMSetDepthStencilState(g_depthState, 0);

        Vec3 f, r, u;
        GetCameraVectors(g_player, f, r, u);
        Vec3 eye = { g_player.x, g_player.y + PLAYER_EYE, g_player.z };
        Mat4 view = MatLookToLH(eye, f, u);
        const float PI_OVER_4 = 0.78539816339f;
        Mat4 proj = MatPerspectiveFovLH(PI_OVER_4, (float)SCREEN_W / SCREEN_H, 0.1f, 500.0f);
        Mat4 viewProj = MatMul(view, proj);

        g_context->VSSetShader(g_vs, nullptr, 0);
        g_context->PSSetShader(g_ps, nullptr, 0);
        g_context->IASetInputLayout(g_layout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetConstantBuffers(0, 1, &g_cbuffer);
        g_context->PSSetSamplers(0, 1, &g_sampler);

        UpdateCBuffer(viewProj);
        g_context->PSSetShaderResources(0, 1, &g_atlasSRV);
        UINT stride = sizeof(Vertex), offset = 0;
        for (auto& kv : g_world.chunks) {
            Chunk& c = *kv.second;
            if (c.indexCount == 0) continue;
            g_context->IASetVertexBuffers(0, 1, &c.vb, &stride, &offset);
            g_context->IASetIndexBuffer(c.ib, DXGI_FORMAT_R32_UINT, 0);
            g_context->DrawIndexed(c.indexCount, 0, 0);
        }

        // Pipe instances: one draw call per instance (documented scaling
        // limit, Section 4.4) -- fine at prototype density.
        g_context->PSSetShaderResources(0, 1, &g_pipeSRV);
        g_context->IASetVertexBuffers(0, 1, &g_pipeMeshVB, &stride, &offset);
        g_context->IASetIndexBuffer(g_pipeMeshIB, DXGI_FORMAT_R32_UINT, 0);
        for (auto& kv : g_world.chunks) {
            for (auto& pipe : kv.second->pipes) {
                Mat4 world = MatTranslation((float)pipe.worldX, (float)pipe.worldY, (float)pipe.worldZ);
                UpdateCBuffer(MatMul(world, viewProj));
                g_context->DrawIndexed(g_pipeMeshIndexCount, 0, 0);
            }
        }

        RenderUIPass();

        g_swapChain->Present(1, 0);
    }

    return 0;
}
