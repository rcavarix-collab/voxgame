// main.cpp
//
// Voxistics - Milestone 1 prototype.
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
// would silently break at the token that happens to be followed by '('.
#define NOMINMAX
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath, for locating the save directory (Section 7)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <xaudio2.h> // procedural music playback (Section 10)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar> // swprintf, for building slotN.sav filenames (Section 7.2.4)
#include <cmath>
#include <cfloat>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib") // provides the FOLDERID_* GUID data (declared, not defined, in knownfolders.h)
#pragma comment(lib, "xaudio2.lib")

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
// One deterministic, seamlessly-looping ambient track, synthesized
// entirely in code the same way the textures above are (Section 10) --
// no external audio asset, nothing to license.
extern "C" bool GenerateAmbientTrack(int16_t** outPCM, uint32_t* outSampleCount, uint32_t* outSampleRate);
extern "C" void FreeGeneratedAudio(int16_t* p);

// =======================================================================
// Part II/III - World representation and block model
// =======================================================================

static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;

static const int CHUNK_SIZE = 16;
static const int CHUNK_CELLS = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
static const int Y_MIN = 0;
static const int Y_MAX = 255;
static int g_loadRadius = 3; // chunks, horizontal only (Section 2.4); a Graphics Settings slider now [1,8]
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
// Section 4.6 - UI pass support: font-glyph atlas layout and quad builders.
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
// World generation and chunk loading -- deterministic terrain, bypassing
// live gravity (Section 5.2), and column loading kept sparse per Section
// 2.1/2.4. Not a numbered Part of its own in the design doc.
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

    // Cached per column and reused below for every vertical chunk level
    // (both the anyContent check and the fill pass) instead of calling
    // TerrainHeight -- and re-running its trig -- once per level.
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    int maxHeightInColumn = 0;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int h = TerrainHeight(baseX + lx, baseZ + lz);
            heights[lx][lz] = h;
            if (h > maxHeightInColumn) maxHeightInColumn = h;
        }
    }

    int maxCy = FloorDiv16(maxHeightInColumn);
    for (int cy = 0; cy <= maxCy; cy++) {
        int chunkYLow = cy * CHUNK_SIZE;
        // Skip chunks that would contain nothing but air anywhere in this
        // column -- chunks only exist when they hold real content
        // (Section 2.1).
        bool anyContent = false;
        for (int lx = 0; lx < CHUNK_SIZE && !anyContent; lx++)
            for (int lz = 0; lz < CHUNK_SIZE && !anyContent; lz++)
                if (chunkYLow <= heights[lx][lz]) anyContent = true;
        if (!anyContent) continue;

        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int wx = baseX + lx, wz = baseZ + lz;
                int h = heights[lx][lz];
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

// Columns queued for generation but not yet generated. Entering view
// range only enqueues a column; ProcessColumnGeneration below drains a
// capped number per tick, following the exact pattern the falling-block
// queue already established (Section 5.1): a hard per-tick work cap
// instead of an unbounded burst, so crossing into a large unexplored
// area -- or the initial spawn, which needs the whole load radius at
// once -- can't spike a single frame.
static std::deque<std::pair<int, int>> g_pendingColumns;
static std::unordered_set<long long> g_pendingColumnSet;
static const int MAX_COLUMN_GENS_PER_TICK = 4;

// Only enqueues columns now (Section 5.1's queue pattern applied to
// generation, below) -- it never touches World directly, unlike its
// gravity/mesh-rebuild counterparts elsewhere in this file.
static void EnsureChunksLoaded(int playerChunkX, int playerChunkZ) {
    // Recomputed only when the player's chunk coordinate actually
    // changes (Section 2.4) -- not every frame.
    if (playerChunkX == g_lastPlayerChunkX && playerChunkZ == g_lastPlayerChunkZ) return;
    g_lastPlayerChunkX = playerChunkX;
    g_lastPlayerChunkZ = playerChunkZ;

    for (int dx = -g_loadRadius; dx <= g_loadRadius; dx++) {
        for (int dz = -g_loadRadius; dz <= g_loadRadius; dz++) {
            int cx = playerChunkX + dx, cz = playerChunkZ + dz;
            long long key = ColumnKey(cx, cz);
            if (g_generatedColumns.count(key) || g_pendingColumnSet.count(key)) continue;
            g_pendingColumnSet.insert(key);
            g_pendingColumns.push_back({ cx, cz });
        }
    }
}

static void ProcessColumnGeneration(World& w) {
    int n = (int)std::min<size_t>(MAX_COLUMN_GENS_PER_TICK, g_pendingColumns.size());
    for (int i = 0; i < n; i++) {
        auto col = g_pendingColumns.front();
        g_pendingColumns.pop_front();
        g_pendingColumnSet.erase(ColumnKey(col.first, col.second));
        GenerateColumn(w, col.first, col.second);
    }
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

// One mesh per pipe shape (index by BlockInfo.shape: 1 straight, 2
// corner, 3 junction; [0] unused). Distinct silhouettes per shape, but
// still a fixed canonical orientation -- real per-instance orientation
// and connection-aware geometry is still Milestone 2 work alongside
// network connectivity (Section 4.4).
struct PipeMesh { ID3D11Buffer* vb = nullptr; ID3D11Buffer* ib = nullptr; UINT indexCount = 0; };
static PipeMesh g_pipeMeshes[4];

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

// Third pass state: a basic skybox, drawn before the world so normal
// depth-tested opaque geometry always overdraws it. Untextured -- a
// gradient computed per-pixel from the interpolated (camera-relative)
// position reused as a view direction -- so its own tiny pipeline
// (position only, no sampler/texture) rather than reusing either the
// world or UI shader. Per-pixel rather than per-vertex specifically to
// avoid the box-corner artifact a coarse per-vertex gradient on a
// 6-quad box shows: near a geometric box corner (shared by 3 faces,
// all zenith-colored there), vertex interpolation alone pulls in extra
// zenith color even at screen positions whose actual view direction is
// nowhere near straight up. Computing the gradient from the true
// (renormalized) direction instead makes it smooth in every direction,
// independent of the mesh's face boundaries.
struct SkyVertex { float x, y, z; };
static ID3D11VertexShader* g_skyVS = nullptr;
static ID3D11PixelShader* g_skyPS = nullptr;
static ID3D11InputLayout* g_skyLayout = nullptr;
static ID3D11Buffer* g_skyCBuffer = nullptr;
static ID3D11Buffer* g_skyVB = nullptr;
static ID3D11Buffer* g_skyIB = nullptr;
static UINT g_skyIndexCount = 0;

static const char* g_skyShaderSrc =
    "cbuffer SkyCB : register(b0) { row_major matrix viewProj; };\n"
    "struct VSIn { float3 pos:POSITION; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 dir:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn input) { PSIn o; o.pos = mul(float4(input.pos,1.0f), viewProj); o.dir = input.pos; return o; }\n"
    "static const float3 ZENITH = float3(0.25f, 0.45f, 0.85f);\n"
    "static const float3 HORIZON = float3(0.65f, 0.75f, 0.95f);\n"
    "float4 PSMain(PSIn input) : SV_TARGET {\n"
    "    float3 d = normalize(input.dir);\n"
    "    float t = saturate(d.y);\n"
    "    return float4(lerp(HORIZON, ZENITH, t), 1.0f);\n"
    "}\n";

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

// Emits a full 6-faced box between two corners, in the same local
// unit-cube space EmitFace's callers already use (the box origin passed
// to EmitFace is always 0,0,0 and the corners carry the real offsets).
static void AddBox(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                    float x0, float y0, float z0, float x1, float y1, float z1,
                    float u0, float v0, float u1, float v1) {
    EmitFace(verts, indices, 0,0,0, x1,y0,z0, x1,y1,z0, x1,y1,z1, x1,y0,z1, u0,v0,u1,v1); // +X
    EmitFace(verts, indices, 0,0,0, x0,y0,z1, x0,y1,z1, x0,y1,z0, x0,y0,z0, u0,v0,u1,v1); // -X
    EmitFace(verts, indices, 0,0,0, x0,y1,z0, x0,y1,z1, x1,y1,z1, x1,y1,z0, u0,v0,u1,v1); // +Y
    EmitFace(verts, indices, 0,0,0, x0,y0,z1, x0,y0,z0, x1,y0,z0, x1,y0,z1, u0,v0,u1,v1); // -Y
    EmitFace(verts, indices, 0,0,0, x1,y0,z1, x1,y1,z1, x0,y1,z1, x0,y0,z1, u0,v0,u1,v1); // +Z
    EmitFace(verts, indices, 0,0,0, x0,y0,z0, x0,y1,z0, x1,y1,z0, x1,y0,z0, u0,v0,u1,v1); // -Z
}

static PipeMesh UploadPipeMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    PipeMesh mesh;
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(verts.size() * sizeof(Vertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = {}; vinit.pSysMem = verts.data();
    g_device->CreateBuffer(&vbd, &vinit, &mesh.vb);

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = {}; iinit.pSysMem = indices.data();
    g_device->CreateBuffer(&ibd, &iinit, &mesh.ib);

    mesh.indexCount = (UINT)indices.size();
    return mesh;
}

static void BuildPipeMeshes() {
    const float u0 = 0.05f, v0 = 0.05f, u1 = 0.95f, v1 = 0.95f;
    const float lo = 0.35f, hi = 0.65f; // pipe cross-section: 0.3 thick, centered

    // Straight: a through-pipe spanning the full cell vertically.
    {
        std::vector<Vertex> verts; std::vector<uint32_t> indices;
        AddBox(verts, indices, lo, 0.0f, lo, hi, 1.0f, hi, u0, v0, u1, v1);
        g_pipeMeshes[1] = UploadPipeMesh(verts, indices);
    }
    // Corner: a vertical stub from the floor up to mid-height, elbowing
    // into a horizontal stub out to the +X face at that height.
    {
        std::vector<Vertex> verts; std::vector<uint32_t> indices;
        AddBox(verts, indices, lo, 0.0f, lo, hi, 0.5f, hi, u0, v0, u1, v1);
        AddBox(verts, indices, lo, lo, lo, 1.0f, hi, hi, u0, v0, u1, v1);
        g_pipeMeshes[2] = UploadPipeMesh(verts, indices);
    }
    // Junction: a full vertical through-pipe crossed by a full
    // horizontal through-pipe at mid-height, suggesting multiple
    // connections branching through this cell.
    {
        std::vector<Vertex> verts; std::vector<uint32_t> indices;
        AddBox(verts, indices, lo, 0.0f, lo, hi, 1.0f, hi, u0, v0, u1, v1);
        AddBox(verts, indices, 0.0f, lo, lo, 1.0f, hi, hi, u0, v0, u1, v1);
        g_pipeMeshes[3] = UploadPipeMesh(verts, indices);
    }
}

static void AddSkyQuad(std::vector<SkyVertex>& v, std::vector<uint32_t>& idx,
                        float x0, float y0, float z0, float x1, float y1, float z1,
                        float x2, float y2, float z2, float x3, float y3, float z3) {
    uint32_t base = (uint32_t)v.size();
    v.push_back({ x0, y0, z0 });
    v.push_back({ x1, y1, z1 });
    v.push_back({ x2, y2, z2 });
    v.push_back({ x3, y3, z3 });
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}

// A large inverted box centered on the camera each frame (Section 4.6-
// adjacent: a third pass, drawn with depth off before the opaque world
// pass so normal depth-tested geometry always overdraws it, and with
// its view matrix's translation stripped so it never appears to move
// as the player walks -- only as they look around, exactly like a
// conventional skybox). No per-vertex color needed -- the pixel shader
// computes the zenith/horizon gradient itself from the interpolated
// position, reused as a view direction.
static void BuildSkyMesh() {
    std::vector<SkyVertex> verts;
    std::vector<uint32_t> indices;
    const float E = 50.0f; // arbitrary -- depth test is off, so size only has to clear the near plane

    AddSkyQuad(verts, indices, -E, E, -E, -E, E, E, E, E, E, E, E, -E); // top
    AddSkyQuad(verts, indices, -E, -E, E, -E, -E, -E, E, -E, -E, E, -E, E); // bottom
    AddSkyQuad(verts, indices, E, E, -E, E, E, E, E, -E, E, E, -E, -E); // +X
    AddSkyQuad(verts, indices, -E, E, E, -E, E, -E, -E, -E, -E, -E, -E, E); // -X
    AddSkyQuad(verts, indices, E, E, E, -E, E, E, -E, -E, E, E, -E, E); // +Z
    AddSkyQuad(verts, indices, -E, E, -E, E, E, -E, E, -E, -E, -E, -E, -E); // -Z

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(verts.size() * sizeof(SkyVertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = {}; vinit.pSysMem = verts.data();
    g_device->CreateBuffer(&vbd, &vinit, &g_skyVB);

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = {}; iinit.pSysMem = indices.data();
    g_device->CreateBuffer(&ibd, &iinit, &g_skyIB);

    g_skyIndexCount = (UINT)indices.size();
}

static void UpdateCBuffer(const Mat4& mvp) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    CBData* data = (CBData*)mapped.pData;
    data->mvp = mvp;
    g_context->Unmap(g_cbuffer, 0);
}

// =======================================================================
// Section 4.7 - Camera / player
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

// Player half-width / height used for AABB collision. BoxIntersectsSolid
// below tests every voxel cell the box's full vertical extent overlaps
// (not just a few discrete height samples), which is the concept
// Prismative used player collision for -- just done here as a complete
// AABB-vs-voxel-grid overlap rather than sampled points.
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
// Section 4.5 - Amanatides-Woo exact voxel DDA raycast for block picking
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

// Settings data persisted below in SaveGame/LoadGame. Declared here
// (ahead of Part VII) purely because these need to exist before that
// code does; the menu UI that actually edits them lives much further
// down in the WinMain/message-loop section, since it needs D3D/window
// state that doesn't exist this early in the file.
enum GameAction {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_JUMP,
    ACT_BREAK, ACT_PLACE, ACT_MENU, ACT_SAVE, ACT_LOAD,
    ACT_COUNT
};
static const char* g_actionNames[ACT_COUNT] = { // stable identity for the save file, same idea as g_blockNames
    "forward", "back", "left", "right", "jump", "break", "place", "menu", "save", "load"
};
static const int MOUSE_LEFT = -1, MOUSE_RIGHT = -2, MOUSE_MIDDLE = -3; // share the bound-input-code space with VK_* (all positive)
static int g_keyBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_F5, VK_F9
};
static float g_sensitivityMultX = 1.0f, g_sensitivityMultY = 1.0f;
static bool g_invertX = false, g_invertY = false;
static bool g_showFPS = false;
static float g_masterVolume = 1.0f;
static float g_musicVolume = 1.0f;
static void ApplyAudioVolumes(); // defined in Section 10 (Audio); LoadGame's legacy-settings path needs it before that section exists
static float g_fov = 45.0f; // degrees, vertical -- matches the fixed value this replaces, unchanged until a player moves the slider
static bool g_toggleMovement = false; // Accessibility (Section 11): press-to-toggle instead of hold-to-move for WASD
static bool g_highContrastUI = false; // Accessibility: higher-luminance-contrast menu palette
static bool g_moveToggleLatch[ACT_COUNT] = {}; // only ACT_FORWARD/BACK/LEFT/RIGHT indices are ever used

// Section 13 - Day clock. World state (not a global preference), since
// different saves can legitimately be at different points in their
// day -- persisted in the save payload alongside player position, not
// settings.cfg. Advances only while gameplay is actually ticking (the
// same gate that already freezes physics/chunk-gen while any menu is
// open), wraps at DAY_LENGTH_SECONDS. A fresh New Game starts at 0
// (dawn) -- the character's first light in a land they've never seen.
static const float DAY_LENGTH_SECONDS = 3600.0f; // one in-game day = one real hour, locked in
static float g_dayTimeSeconds = 0.0f;
// Music Intensity (Accessibility, Section 11): 0 = ambient bed only, no
// arp/pulse layer at all; 1 = the full designed arc. This is a ceiling,
// not a ceiling-breaker -- 1.0 is already the maximum energy/brightness
// the track ever reaches by design (Section 10.3's hard accessibility
// rule -- gradual transitions, capped filter resonance, no sudden
// onsets -- applies identically at every intensity, never relaxed for
// "less sensitive" players). What actually moves with this setting is
// how present the arp layer is even during its quietest hours and how
// often it takes its scheduled breathing gaps, not whether the safety
// constraints apply.
static float g_musicIntensity = 1.0f;

// =======================================================================
// Part VII - Save / load (crash-safe, versioned, name-indexed)
// =======================================================================

static const uint32_t SAVE_VERSION = 4; // v3 dropped the embedded settings block (Section 7.2.3); v4 adds the day-clock field (Section 13). Both v2 and v3 files remain loadable -- see LoadGame's version handling.

// Resolves (creating if needed) Documents\My Games\Voxistics -- the
// conventional PC-game save location: visible and easy for players to
// find, back up, or copy between machines, unlike a hidden AppData
// folder. Falls back to the current working directory (this prototype's
// original behavior) if the known-folder lookup fails for any reason,
// or if something unexpected already occupies part of the intended
// path -- e.g. a plain file sitting where a folder needs to be. A save
// attempt should always have somewhere safe to go rather than failing
// forever because the "nice" location didn't pan out.
// Shared by GetSaveDirectory and GetSavesDirectory below: checks
// exists()&&!is_directory() before create_directories() specifically to
// catch a plain file already occupying part of the intended path,
// rather than letting a failed directory creation surface as a
// mysterious save failure. Empty return means "use the fallback"
// (the current working directory) rather than this path.
static std::filesystem::path EnsureDirectoryBulletproof(std::filesystem::path dir, const char* what) {
    namespace fs = std::filesystem;
    if (dir.empty()) return fs::path();
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_directory(dir, ec)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: a file already occupies the intended directory path, falling back\n", what);
        OutputDebugStringA(msg);
        return fs::path();
    }
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: could not create the directory, falling back\n", what);
        OutputDebugStringA(msg);
        return fs::path();
    }
    return dir;
}

// Resolves (creating if needed) Documents\My Games\Voxistics -- the
// conventional PC-game save location: visible and easy for players to
// find, back up, or copy between machines, unlike a hidden AppData
// folder. Falls back to the current working directory (this prototype's
// original behavior) if the known-folder lookup fails for any reason,
// or if something unexpected already occupies part of the intended
// path -- e.g. a plain file sitting where a folder needs to be. A save
// attempt should always have somewhere safe to go rather than failing
// forever because the "nice" location didn't pan out.
static std::filesystem::path GetSaveDirectory() {
    namespace fs = std::filesystem;
    PWSTR docsPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docsPath);
    fs::path dir;
    if (SUCCEEDED(hr) && docsPath) {
        dir = fs::path(docsPath) / L"My Games" / L"Voxistics";
    }
    if (docsPath) CoTaskMemFree(docsPath);

    if (dir.empty()) {
        OutputDebugStringA("GetSaveDirectory: could not resolve Documents, falling back to working directory\n");
        return fs::path();
    }
    return EnsureDirectoryBulletproof(dir, "GetSaveDirectory");
}

// The multi-slot saves subfolder (Section 7.2.4), inside the same
// bulletproofed base directory as settings.cfg.
static std::filesystem::path GetSavesDirectory() {
    std::filesystem::path base = GetSaveDirectory();
    if (base.empty()) return base;
    return EnsureDirectoryBulletproof(base / L"Saves", "GetSavesDirectory");
}

static const int MAX_SAVE_SLOTS = 5;

// Recomputed on every save/load rather than cached once -- cheap, and
// means a save directory that only becomes available partway through a
// run (e.g. a transient permissions/antivirus hiccup clears up) is
// retried instead of being stuck with whatever the very first attempt
// happened to find.
static std::filesystem::path GetSaveFilePath(int slot) {
    std::filesystem::path dir = GetSavesDirectory();
    wchar_t name[32];
    swprintf(name, 32, L"slot%d.sav", slot + 1);
    return dir.empty() ? std::filesystem::path(name) : dir / name;
}

static bool SlotExists(int slot) {
    std::error_code ec;
    return std::filesystem::exists(GetSaveFilePath(slot), ec);
}

// One-time migration (same philosophy as the v2-settings migration
// above): a save from before multi-slot support existed lived directly
// at Documents\My Games\Voxistics\voxelproto.sav. If that file exists
// and slot 1 doesn't yet, move it into the new Saves\slot1.sav location
// rather than leaving it invisible to the new slot picker forever.
static void MigrateLegacySingleSaveIfPresent() {
    namespace fs = std::filesystem;
    std::filesystem::path base = GetSaveDirectory();
    if (base.empty()) return;
    fs::path legacyPath = base / L"voxelproto.sav";
    std::error_code ec;
    if (!fs::exists(legacyPath, ec)) return;
    if (SlotExists(0)) return; // slot 1 already has its own save; never overwrite it
    fs::path slot1Path = GetSaveFilePath(0);
    if (slot1Path.empty()) return;
    fs::rename(legacyPath, slot1Path, ec); // same volume (same parent tree) -- a plain rename is sufficient
}

// =======================================================================
// Section 7.2.3 - Global settings file
// =======================================================================
//
// Gameplay/UI preferences (sensitivity, inversion, render distance, the
// FPS toggle, volumes, keybindings) live in their own small text file,
// separate from any world save, so they're available before any save is
// loaded (e.g. a title screen's Options) and carry over between saves
// rather than being tied to one. Plain "key=value" lines rather than the
// versioned binary format saves use: it's a handful of scalars a player
// might reasonably want to hand-edit or inspect, and forward/backward
// compatibility just falls out of "unknown keys are ignored, missing
// keys keep their compiled-in default" with no version field needed.
static std::filesystem::path GetSettingsFilePath() {
    std::filesystem::path dir = GetSaveDirectory(); // same bulletproofed directory as the save file
    std::filesystem::path filename = L"settings.cfg";
    return dir.empty() ? filename : dir / filename;
}

static bool SaveSettings() {
    std::ostringstream ss;
    ss << "sensitivityX=" << g_sensitivityMultX << "\n";
    ss << "sensitivityY=" << g_sensitivityMultY << "\n";
    ss << "invertX=" << (g_invertX ? 1 : 0) << "\n";
    ss << "invertY=" << (g_invertY ? 1 : 0) << "\n";
    ss << "renderDistance=" << g_loadRadius << "\n";
    ss << "showFPS=" << (g_showFPS ? 1 : 0) << "\n";
    ss << "masterVolume=" << g_masterVolume << "\n";
    ss << "musicVolume=" << g_musicVolume << "\n";
    ss << "fov=" << g_fov << "\n";
    ss << "toggleMovement=" << (g_toggleMovement ? 1 : 0) << "\n";
    ss << "highContrastUI=" << (g_highContrastUI ? 1 : 0) << "\n";
    ss << "musicIntensity=" << g_musicIntensity << "\n";
    for (int i = 0; i < ACT_COUNT; i++) {
        ss << "keybind." << g_actionNames[i] << "=" << g_keyBindings[i] << "\n"; // name-indexed, same reasoning as g_blockNames
    }

    namespace fs = std::filesystem;
    fs::path path = GetSettingsFilePath();
    fs::path tmpPath = path; tmpPath += L".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::string data = ss.str();
        out.write(data.data(), (std::streamsize)data.size());
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    return !ec;
}

// Missing file (first run) or missing/unrecognized individual keys
// (an older settings.cfg from before some setting existed) both just
// keep whatever the caller's compiled-in default already was -- loading
// settings can only ever refine current state, never fail outright.
static void LoadSettings() {
    std::ifstream in(GetSettingsFilePath());
    if (!in) return;

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }

    auto getF = [&](const char* k, float def) { auto it = kv.find(k); return it == kv.end() ? def : (float)atof(it->second.c_str()); };
    auto getI = [&](const char* k, int def) { auto it = kv.find(k); return it == kv.end() ? def : atoi(it->second.c_str()); };
    auto getB = [&](const char* k, bool def) { auto it = kv.find(k); return it == kv.end() ? def : (atoi(it->second.c_str()) != 0); };

    g_sensitivityMultX = getF("sensitivityX", g_sensitivityMultX);
    g_sensitivityMultY = getF("sensitivityY", g_sensitivityMultY);
    g_invertX = getB("invertX", g_invertX);
    g_invertY = getB("invertY", g_invertY);
    g_loadRadius = getI("renderDistance", g_loadRadius);
    g_showFPS = getB("showFPS", g_showFPS);
    g_masterVolume = getF("masterVolume", g_masterVolume);
    g_musicVolume = getF("musicVolume", g_musicVolume);
    g_fov = getF("fov", g_fov);
    g_toggleMovement = getB("toggleMovement", g_toggleMovement);
    g_highContrastUI = getB("highContrastUI", g_highContrastUI);
    g_musicIntensity = getF("musicIntensity", g_musicIntensity);
    for (int i = 0; i < ACT_COUNT; i++) {
        std::string key = std::string("keybind.") + g_actionNames[i];
        g_keyBindings[i] = getI(key.c_str(), g_keyBindings[i]);
    }
}

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

static bool SaveGame(World& w, Player& p, int slot) {
    std::vector<uint8_t> buf;
    AppendU32(buf, ('G' << 24) | ('L' << 16) | ('X' << 8) | 'V'); // magic "VXLG" (little-endian on disk)
    AppendU32(buf, SAVE_VERSION);

    AppendF32(buf, p.x); AppendF32(buf, p.y); AppendF32(buf, p.z);
    AppendF32(buf, p.yaw); AppendF32(buf, p.pitch);
    AppendI32(buf, p.hotbarIndex);
    AppendF32(buf, g_dayTimeSeconds); // Section 13 -- world state, not a settings.cfg preference

    // No settings block as of v3 -- gameplay/UI preferences live in the
    // separate global settings.cfg (Section 7.2.3) now, not here.

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
    fs::path savePath = GetSaveFilePath(slot);
    fs::path tmpPath = savePath; tmpPath += L".tmp";
    fs::path bakPath = savePath; bakPath += L".bak";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write((const char*)buf.data(), (std::streamsize)buf.size());
        if (!out) return false;
    }
    std::error_code ec;
    if (fs::exists(savePath, ec)) {
        fs::remove(bakPath, ec);
        fs::rename(savePath, bakPath, ec);
    }
    fs::rename(tmpPath, savePath, ec);
    if (ec) return false;
    return true;
}

static bool LoadGame(World& w, Player& p, int slot) {
    std::ifstream in(GetSaveFilePath(slot), std::ios::binary | std::ios::ate);
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
    // v2 (settings embedded in the save) and v3 (settings moved out, no
    // day clock yet) are both still loadable, not just the current v4
    // (Section 7.2.3/13) -- each older version's world/player data is a
    // strict prefix of the newer format, just missing fields added
    // since. Loading an older save fills those in with sensible
    // defaults (below) rather than refusing an otherwise-fine world.
    if (version != 2 && version != 3 && version != SAVE_VERSION) {
        OutputDebugStringA("LoadGame: unsupported version, aborting load\n");
        return false;
    }
    bool hasLegacySettings = (version == 2);
    bool hasDayTime = (version >= 4);

    Player loaded;
    loaded.x = r.ReadF32(); loaded.y = r.ReadF32(); loaded.z = r.ReadF32();
    loaded.yaw = r.ReadF32(); loaded.pitch = r.ReadF32();
    loaded.hotbarIndex = r.ReadI32();

    // Day clock (Section 13): absent on v2/v3 saves made before it
    // existed -- those resume at dawn (0.0) rather than needing a
    // meaningless stored value.
    float loadedDayTime = 0.0f;
    if (hasDayTime) loadedDayTime = r.ReadF32();

    // Legacy (v2-only) settings block: read into locals first, same as
    // the rest of this function -- nothing gets applied to live state
    // until the whole load is known to be valid. Absent entirely on v3.
    float loadedSensX = 0, loadedSensY = 0;
    bool loadedInvertX = false, loadedInvertY = false;
    int32_t loadedRenderDist = 0;
    bool loadedShowFPS = false;
    float loadedVolume = 0;
    std::vector<std::pair<std::string, int32_t>> loadedBindings;
    if (hasLegacySettings) {
        loadedSensX = r.ReadF32(); loadedSensY = r.ReadF32();
        loadedInvertX = r.ReadU8() != 0; loadedInvertY = r.ReadU8() != 0;
        loadedRenderDist = r.ReadI32();
        loadedShowFPS = r.ReadU8() != 0;
        loadedVolume = r.ReadF32();
        uint32_t bindCount = r.ReadU32();
        loadedBindings.resize(bindCount);
        for (uint32_t i = 0; i < bindCount; i++) {
            loadedBindings[i].first = r.ReadStr();
            loadedBindings[i].second = r.ReadI32();
        }
        if (!r.ok) return false;
    }

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
    g_dayTimeSeconds = loadedDayTime;

    if (hasLegacySettings) {
        g_sensitivityMultX = loadedSensX; g_sensitivityMultY = loadedSensY;
        g_invertX = loadedInvertX; g_invertY = loadedInvertY;
        g_loadRadius = loadedRenderDist;
        g_showFPS = loadedShowFPS;
        g_masterVolume = loadedVolume;
        // Remap saved keybinding action names -> current GameAction indices,
        // the same name-indexed pattern as the block remap above (Section
        // 3.1): an unrecognized action name is skipped with a warning
        // instead of corrupting some other action's binding, and any action
        // absent from the save simply keeps its pre-load value.
        for (auto& kv : loadedBindings) {
            bool found = false;
            for (int a = 0; a < ACT_COUNT; a++) {
                if (kv.first == g_actionNames[a]) { g_keyBindings[a] = kv.second; found = true; break; }
            }
            if (!found) {
                char msg[256];
                snprintf(msg, sizeof(msg), "LoadGame: unknown action name '%s', ignoring binding\n", kv.first.c_str());
                OutputDebugStringA(msg);
            }
        }
        ApplyAudioVolumes();

        // One-time migration (Section 7.2.3): seed the new global config
        // from this legacy save's settings, but only if nothing has
        // created settings.cfg yet -- once it exists, it's the source of
        // truth and this block never overwrites it again.
        if (!std::filesystem::exists(GetSettingsFilePath())) {
            SaveSettings();
        }
    }

    // Mark every column present in the loaded world as already
    // generated, so the next chunk-load pass never re-runs procedural
    // generation over it and stomps loaded/edited blocks with fresh
    // terrain -- only genuinely new columns around the player (beyond
    // what this save covered) will generate normally from here.
    g_generatedColumns.clear();
    for (auto& kv : w.chunks) g_generatedColumns.insert(ColumnKey(kv.first.x, kv.first.z));
    g_pendingColumns.clear();
    g_pendingColumnSet.clear();
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

    // --- Sky pass pipeline objects: depth off (g_uiDepthState is reused
    // here -- it's the same DepthEnable=FALSE state the UI pass already
    // needed, no reason to create a second identical one) ---
    ID3DBlob* skyVsBlob = nullptr, * skyPsBlob = nullptr;
    hr = D3DCompile(g_skyShaderSrc, strlen(g_skyShaderSrc), nullptr, nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &skyVsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(g_skyShaderSrc, strlen(g_skyShaderSrc), nullptr, nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &skyPsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    g_device->CreateVertexShader(skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), nullptr, &g_skyVS);
    g_device->CreatePixelShader(skyPsBlob->GetBufferPointer(), skyPsBlob->GetBufferSize(), nullptr, &g_skyPS);

    D3D11_INPUT_ELEMENT_DESC skyLayoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(skyLayoutDesc, 1, skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), &g_skyLayout);
    skyVsBlob->Release();
    skyPsBlob->Release();

    D3D11_BUFFER_DESC skyCbd = {};
    skyCbd.Usage = D3D11_USAGE_DYNAMIC;
    skyCbd.ByteWidth = sizeof(Mat4);
    skyCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    skyCbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&skyCbd, nullptr, &g_skyCBuffer);

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
// Section 10 - Audio (XAudio2 playback of the procedural ambient track)
// =======================================================================
//
// One persistent source voice loops the single ambient track generated
// once at startup (supplement.cpp) for as long as the process runs.
// There is only a "Music" channel so far -- no sound effects -- but
// Master and Music are already separate settings/sliders so adding SFX
// voices later is just more source voices under the same mastering
// voice, not a change to the mixing model.

static IXAudio2* g_xaudio2 = nullptr;
static IXAudio2MasteringVoice* g_masteringVoice = nullptr;
static IXAudio2SourceVoice* g_musicVoice = nullptr;
static int16_t* g_musicPCM = nullptr;

static void ApplyAudioVolumes() {
    if (g_musicVoice) g_musicVoice->SetVolume(g_masterVolume * g_musicVolume);
}

// Failure anywhere here (no audio device, driver issue, etc.) leaves
// every g_* pointer null and every subsequent audio call a silent no-op
// via the null checks in ApplyAudioVolumes/ShutdownAudio -- a machine
// with no usable audio device still gets a fully playable game, just a
// silent one, rather than a startup failure.
static bool InitAudio() {
    if (FAILED(XAudio2Create(&g_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
    if (FAILED(g_xaudio2->CreateMasteringVoice(&g_masteringVoice))) return false;

    uint32_t sampleCount = 0, sampleRate = 0;
    if (!GenerateAmbientTrack(&g_musicPCM, &sampleCount, &sampleRate)) return false;

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)((wfx.nChannels * wfx.wBitsPerSample) / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    if (FAILED(g_xaudio2->CreateSourceVoice(&g_musicVoice, &wfx))) return false;

    // LoopCount=INFINITE plays this same buffer forever without
    // re-submitting -- XAudio2 reads pAudioData directly rather than
    // copying it, so g_musicPCM has to stay alive as long as the voice
    // does (freed only in ShutdownAudio).
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = sampleCount * sizeof(int16_t);
    buf.pAudioData = (const BYTE*)g_musicPCM;
    buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    buf.Flags = XAUDIO2_END_OF_STREAM;

    ApplyAudioVolumes();
    if (FAILED(g_musicVoice->SubmitSourceBuffer(&buf))) return false;
    g_musicVoice->Start();
    return true;
}

static void ShutdownAudio() {
    if (g_musicVoice) { g_musicVoice->Stop(); g_musicVoice->DestroyVoice(); g_musicVoice = nullptr; }
    if (g_masteringVoice) { g_masteringVoice->DestroyVoice(); g_masteringVoice = nullptr; }
    if (g_xaudio2) { g_xaudio2->Release(); g_xaudio2 = nullptr; }
    if (g_musicPCM) { FreeGeneratedAudio(g_musicPCM); g_musicPCM = nullptr; }
}

// =======================================================================
// wWinMain / message loop
// =======================================================================

static World g_world;
static Player g_player;
static int g_currentSlot = 0; // which of the MAX_SAVE_SLOTS files Save/Load/QuickSave/QuickLoad act on this session
static bool g_mouseCaptured = false;
static bool g_keyDown[256] = {};
enum class MenuScreen { None, Pause, LookSettings, Graphics, Display, Audio, Keybindings, Accessibility, TitleMain, SlotPicker, OptionsHub };
// Starts at the title screen (Section 12) rather than dropping straight
// into gameplay -- the game begins with no world loaded until New Game
// or Load Game picks a slot.
static MenuScreen g_menuScreen = MenuScreen::TitleMain;
enum class GameState { Title, InGame };
static GameState g_gameState = GameState::Title;
// Where OptionsHub's BACK row returns to -- Pause if Options was opened
// mid-game, TitleMain if opened from the title screen, since the same
// hub and the same six settings submenus serve both contexts.
static MenuScreen g_optionsReturnScreen = MenuScreen::TitleMain;
enum class SlotPickerMode { New, Load };
static SlotPickerMode g_slotPickerMode = SlotPickerMode::New;
// New Game on an already-occupied slot needs a confirmation rather than
// silently overwriting -- a second click within a few seconds confirms;
// otherwise the arm times out and a third click starts over.
static int g_confirmOverwriteSlot = -1;
static float g_confirmOverwriteTimer = 0.0f;
static int g_mouseX = 0, g_mouseY = 0;
static std::string g_toastMessage;
static float g_toastTimer = 0.0f; // seconds remaining; drawn by RenderUIPass
static float g_fpsTimer = 0.0f;
static int g_fpsFrameCount = 0, g_fpsDisplay = 0; // updated once/sec, shown when Display Settings' FPS counter is on

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

// Generic submenu layout: every menu screen (Pause and each settings
// submenu) is a titled panel of stacked full-width rows plus, for a few
// rows, an inline slider. One shared layout system parameterized by row
// count/height means Keybindings' 12 compact rows and Look Settings'
// taller slider rows don't need duplicated panel/row-rect math.
struct UIRect { float x0, y0, x1, y1; };
struct SubmenuLayout { float panelW, rowH, rowGap, topMargin, bottomMargin; int rowCount; };

static UIRect SubmenuPanelRect(const SubmenuLayout& L) {
    float h = L.topMargin + L.rowCount * (L.rowH + L.rowGap) - L.rowGap + L.bottomMargin;
    float px = (SCREEN_W - L.panelW) / 2.0f, py = (SCREEN_H - h) / 2.0f;
    return { px, py, px + L.panelW, py + h };
}
static UIRect SubmenuRowRect(const SubmenuLayout& L, int rowIndex) {
    UIRect panel = SubmenuPanelRect(L);
    float bw = L.panelW - 60.0f;
    float bx = panel.x0 + 30.0f;
    float by = panel.y0 + L.topMargin + rowIndex * (L.rowH + L.rowGap);
    return { bx, by, bx + bw, by + L.rowH };
}
static bool PointInRect(int px, int py, const UIRect& r) {
    return px >= r.x0 && px <= r.x1 && py >= r.y0 && py <= r.y1;
}

// Pause itself only handles resuming, save/load, and quitting -- every
// settings category now lives one level down in OptionsHub (Section
// 13), shared with the title screen's own Options button, rather than
// listing all six categories directly in both places.
static const SubmenuLayout PAUSE_LAYOUT    = { 320.0f, 40.0f, 12.0f, 70.0f, 20.0f, 6 };
enum PauseRow { PROW_RESUME = 0, PROW_OPTIONS = 1, PROW_SAVE = 2, PROW_LOAD = 3, PROW_QUIT_TO_TITLE = 4, PROW_QUIT = 5 };

// The options hub: one settings-category picker shared by Pause (mid-
// game) and the title screen (pre-game) alike, since every submenu
// underneath it is pure global-preference state with no dependency on
// a loaded world.
static const SubmenuLayout OPTIONS_HUB_LAYOUT = { 340.0f, 40.0f, 12.0f, 70.0f, 20.0f, 7 };
enum OptionsHubRow { OHROW_LOOK = 0, OHROW_GRAPHICS = 1, OHROW_DISPLAY = 2, OHROW_AUDIO = 3, OHROW_ACCESSIBILITY = 4, OHROW_KEYBINDS = 5, OHROW_BACK = 6 };

// The title screen (Section 12): shown at startup instead of dropping
// straight into gameplay, and again after "Quit to Title" from Pause.
static const SubmenuLayout TITLE_LAYOUT = { 320.0f, 44.0f, 14.0f, 90.0f, 20.0f, 4 };
enum TitleRow { TROW_NEW_GAME = 0, TROW_LOAD_GAME = 1, TROW_OPTIONS = 2, TROW_QUIT = 3 };

// One row per save slot plus BACK. Rows beyond MAX_SAVE_SLOTS-1 are
// BACK; see SLOTROW_BACK below rather than a fixed enum, since the slot
// count is a constant, not a fixed small set of named rows.
static const SubmenuLayout SLOT_PICKER_LAYOUT = { 420.0f, 48.0f, 10.0f, 90.0f, 20.0f, MAX_SAVE_SLOTS + 1 };
static const int SLOTROW_BACK = MAX_SAVE_SLOTS;

// Look Settings: separate X/Y sensitivity sliders and separate X/Y
// inversion, per the request -- a single combined sensitivity value
// didn't let the two axes be tuned independently.
static const SubmenuLayout LOOK_LAYOUT     = { 400.0f, 56.0f, 12.0f, 70.0f, 20.0f, 6 };
enum LookRow { LROW_INVERT_X = 0, LROW_SENS_X = 1, LROW_INVERT_Y = 2, LROW_SENS_Y = 3, LROW_RESET = 4, LROW_BACK = 5 };

// Graphics: one real setting -- render distance -- rather than stubbing
// out controls (fog distance, shadow quality, etc.) this prototype has
// no rendering path for yet.
static const SubmenuLayout GRAPHICS_LAYOUT = { 380.0f, 56.0f, 12.0f, 70.0f, 20.0f, 3 };
enum GraphicsRow { GROW_RENDER_DIST = 0, GROW_RESET = 1, GROW_BACK = 2 };

// Display: one real setting -- an FPS counter toggle. Resolution/
// fullscreen switching would need swap-chain resize and WM_SIZE
// handling this prototype doesn't have yet, so it isn't faked here.
static const SubmenuLayout DISPLAY_LAYOUT  = { 340.0f, 40.0f, 12.0f, 70.0f, 20.0f, 3 };
enum DisplayRow { DROW_SHOW_FPS = 0, DROW_RESET = 1, DROW_BACK = 2 };

// Audio: Master and Music sliders, backed by a real XAudio2 voice
// (Section 10) playing the procedural ambient track. Separate channels
// now even though Music is the only one with anything to play yet, so a
// future SFX channel is one more slider, not a remix of this one.
static const SubmenuLayout AUDIO_LAYOUT    = { 380.0f, 56.0f, 12.0f, 70.0f, 20.0f, 4 };
enum AudioRow { AROW_MASTER_VOLUME = 0, AROW_MUSIC_VOLUME = 1, AROW_RESET = 2, AROW_BACK = 3 };

// Accessibility: a real, working slice rather than every idea discussed
// -- a field-of-view slider (motion/vestibular comfort: neither wider
// nor narrower is universally more comfortable, so this is a slider a
// player tunes either direction, not a binary toggle), a toggle-to-move
// mode for WASD (motor accessibility: movement no longer requires
// holding a key down for the whole duration), and a high-contrast UI
// palette (low-vision legibility). Deliberately NOT here yet: a "reduce
// flashing" toggle, since nothing in this prototype flashes or strobes
// today -- the actual commitment (Section 11) is that no future effect
// introduces uncontrolled flashing/strobing at all, which a toggle
// controlling zero real effects wouldn't strengthen; a colorblind-safe
// palette, since nothing in the current UI conveys meaning through hue
// alone yet (nothing to remap); and a UI scale slider, which (unlike
// the above) is real future work, just architecturally bigger -- every
// hit-rect, not only the visuals, would need to move in lockstep.
static const SubmenuLayout ACCESSIBILITY_LAYOUT = { 400.0f, 56.0f, 12.0f, 70.0f, 20.0f, 6 };
enum AccessibilityRow { ARROW_FOV = 0, ARROW_TOGGLE_MOVE = 1, ARROW_HIGH_CONTRAST = 2, ARROW_MUSIC_INTENSITY = 3, ARROW_RESET = 4, ARROW_BACK = 5 };

// Keybindings: every action bindable to any keyboard key or the left/
// right/middle mouse button (GameAction/g_actionNames/g_keyBindings/
// MOUSE_LEFT etc. are declared earlier, ahead of Part VII's save/load
// code, since that needs them too). Scope decisions worth being
// explicit about: no gamepad support exists in this prototype to bind
// to; mouse wheel and side (X1/X2) buttons aren't bindable inputs yet;
// the 9 hotbar-select keys stay fixed rather than adding 9 more rows;
// and rebinding does not warn about or prevent two actions sharing the
// same input.
static const char* g_actionLabels[ACT_COUNT] = { // on-screen text
    "MOVE FORWARD", "MOVE BACK", "MOVE LEFT", "MOVE RIGHT", "JUMP",
    "BREAK BLOCK", "PLACE BLOCK", "PAUSE MENU", "QUICK SAVE", "QUICK LOAD"
};
static const SubmenuLayout KEYBIND_LAYOUT = { 480.0f, 32.0f, 8.0f, 92.0f, 20.0f, ACT_COUNT + 2 }; // +reset +back

static const int g_defaultBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_F5, VK_F9
};
static int g_rebindingAction = -1; // -1 = not capturing; else a GameAction index

static bool g_mouseButtonDown[3] = {}; // 0=left,1=right,2=middle
static int MouseButtonIndex(int code) {
    if (code == MOUSE_LEFT) return 0;
    if (code == MOUSE_RIGHT) return 1;
    if (code == MOUSE_MIDDLE) return 2;
    return -1;
}
static bool IsInputDown(int code) {
    int mi = MouseButtonIndex(code);
    if (mi >= 0) return g_mouseButtonDown[mi];
    if (code >= 0 && code < 256) return g_keyDown[code];
    return false;
}
// In toggle-move mode (Accessibility, Section 11) the four movement
// actions report a latched state that a key PRESS flips, rather than
// whether the key is currently physically held -- the whole point being
// that a player no longer needs to hold it down for the entire duration
// of movement. Every other action (jump, break, place, menu, ...) is
// unaffected and keeps the ordinary held-state behavior.
static bool IsMovementAction(GameAction a) {
    return a == ACT_FORWARD || a == ACT_BACK || a == ACT_LEFT || a == ACT_RIGHT;
}
static bool IsActionDown(GameAction a) {
    if (g_toggleMovement && IsMovementAction(a)) return g_moveToggleLatch[a];
    return IsInputDown(g_keyBindings[a]);
}

// Human-readable name for a bound input code, for the Keybindings rows.
static std::string GetInputDisplayName(int code) {
    if (code == MOUSE_LEFT) return "MOUSE LEFT";
    if (code == MOUSE_RIGHT) return "MOUSE RIGHT";
    if (code == MOUSE_MIDDLE) return "MOUSE MIDDLE";
    UINT scan = MapVirtualKeyW((UINT)code, MAPVK_VK_TO_VSC);
    LONG fakeLParam = (LONG)(scan << 16);
    wchar_t buf[64] = {};
    int len = GetKeyNameTextW(fakeLParam, buf, 64);
    if (len <= 0) return "?";
    std::string s;
    for (int i = 0; i < len; i++) s.push_back((char)buf[i]); // default bindings are all plain-ASCII key names
    return s;
}
static void ResetKeybindingsToDefault() {
    for (int i = 0; i < ACT_COUNT; i++) g_keyBindings[i] = g_defaultBindings[i];
}

// Look/Graphics/Display/Audio preference VALUES (g_sensitivityMultX/Y,
// g_invertX/Y, g_showFPS, g_masterVolume) are declared earlier for the
// same reason as the keybinding data above -- Section VII needs them.
static const float BASE_MOUSE_SENS = 0.0025f;
static const float SENS_MIN = 0.25f, SENS_MAX = 3.0f;

static void ResetLookSettings() { g_sensitivityMultX = 1.0f; g_sensitivityMultY = 1.0f; g_invertX = false; g_invertY = false; }
static void ResetGraphicsSettings() {
    g_loadRadius = 3;
    g_lastPlayerChunkX = INT32_MIN; g_lastPlayerChunkZ = INT32_MIN; // force a rescan at the new radius
}
static void ResetDisplaySettings() { g_showFPS = false; }
static void ResetAudioSettings() { g_masterVolume = 1.0f; g_musicVolume = 1.0f; ApplyAudioVolumes(); }
static void ResetAccessibilitySettings() {
    g_fov = 45.0f;
    g_toggleMovement = false;
    g_highContrastUI = false;
    g_musicIntensity = 1.0f;
    memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch));
}

// A handful of settings are sliders rather than toggles/buttons. One
// small generic slider system (value/range/row-rect all looked up by
// ID) instead of one-off X-sensitivity-shaped code repeated per slider.
enum SliderId { SLIDER_NONE = -1, SLIDER_SENS_X = 0, SLIDER_SENS_Y = 1, SLIDER_RENDER_DIST = 2, SLIDER_MASTER_VOLUME = 3, SLIDER_MUSIC_VOLUME = 4, SLIDER_FOV = 5, SLIDER_MUSIC_INTENSITY = 6 };
static int g_draggingSlider = SLIDER_NONE;

struct SliderRange { float minV, maxV; };
static SliderRange GetSliderRange(int id) {
    switch (id) {
    case SLIDER_SENS_X: case SLIDER_SENS_Y: return { SENS_MIN, SENS_MAX };
    case SLIDER_RENDER_DIST: return { 1.0f, 8.0f };
    case SLIDER_MASTER_VOLUME: case SLIDER_MUSIC_VOLUME: return { 0.0f, 1.0f };
    case SLIDER_FOV: return { 45.0f, 100.0f };
    case SLIDER_MUSIC_INTENSITY: return { 0.0f, 1.0f };
    default: return { 0.0f, 1.0f };
    }
}
// Only meaningful while the slider's own submenu is the active screen
// -- the only time it can be dragged or needs drawing.
static UIRect GetSliderRowRect(int id) {
    switch (id) {
    case SLIDER_SENS_X: return SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_X);
    case SLIDER_SENS_Y: return SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_Y);
    case SLIDER_RENDER_DIST: return SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RENDER_DIST);
    case SLIDER_MASTER_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME);
    case SLIDER_MUSIC_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME);
    case SLIDER_FOV: return SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_FOV);
    case SLIDER_MUSIC_INTENSITY: return SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MUSIC_INTENSITY);
    default: return { 0, 0, 0, 0 };
    }
}
static float GetSliderValue(int id) {
    switch (id) {
    case SLIDER_SENS_X: return g_sensitivityMultX;
    case SLIDER_SENS_Y: return g_sensitivityMultY;
    case SLIDER_RENDER_DIST: return (float)g_loadRadius;
    case SLIDER_MASTER_VOLUME: return g_masterVolume;
    case SLIDER_MUSIC_VOLUME: return g_musicVolume;
    case SLIDER_FOV: return g_fov;
    case SLIDER_MUSIC_INTENSITY: return g_musicIntensity;
    default: return 0.0f;
    }
}
static void SetSliderValue(int id, float v) {
    switch (id) {
    case SLIDER_SENS_X: g_sensitivityMultX = v; break;
    case SLIDER_SENS_Y: g_sensitivityMultY = v; break;
    case SLIDER_RENDER_DIST: {
        int newRadius = (int)(v + 0.5f);
        if (newRadius != g_loadRadius) {
            g_loadRadius = newRadius;
            g_lastPlayerChunkX = INT32_MIN; g_lastPlayerChunkZ = INT32_MIN;
        }
        break;
    }
    case SLIDER_MASTER_VOLUME: g_masterVolume = v; ApplyAudioVolumes(); break;
    case SLIDER_MUSIC_VOLUME: g_musicVolume = v; ApplyAudioVolumes(); break;
    case SLIDER_FOV: g_fov = v; break;
    case SLIDER_MUSIC_INTENSITY: g_musicIntensity = v; break;
    }
}
static std::string GetSliderLabel(int id) {
    char buf[64];
    switch (id) {
    case SLIDER_SENS_X: snprintf(buf, sizeof(buf), "X SENSITIVITY: %.2fx", g_sensitivityMultX); break;
    case SLIDER_SENS_Y: snprintf(buf, sizeof(buf), "Y SENSITIVITY: %.2fx", g_sensitivityMultY); break;
    case SLIDER_RENDER_DIST: snprintf(buf, sizeof(buf), "RENDER DISTANCE: %d CHUNKS", g_loadRadius); break;
    case SLIDER_MASTER_VOLUME: snprintf(buf, sizeof(buf), "MASTER VOLUME: %d%%", (int)(g_masterVolume * 100.0f + 0.5f)); break;
    case SLIDER_MUSIC_VOLUME: snprintf(buf, sizeof(buf), "MUSIC VOLUME: %d%%", (int)(g_musicVolume * 100.0f + 0.5f)); break;
    case SLIDER_FOV: snprintf(buf, sizeof(buf), "FIELD OF VIEW: %d DEG", (int)(g_fov + 0.5f)); break;
    case SLIDER_MUSIC_INTENSITY: snprintf(buf, sizeof(buf), "MUSIC INTENSITY: %d%%", (int)(g_musicIntensity * 100.0f + 0.5f)); break;
    default: buf[0] = 0;
    }
    return buf;
}
// The slider track sits in the lower half of its row, with the label
// above it. The hit rect is a bit taller than the visible track so it's
// not fiddly to grab.
static UIRect GetSliderTrackRect(UIRect r) { return { r.x0 + 8, r.y0 + 34, r.x1 - 8, r.y0 + 42 }; }
static UIRect GetSliderHitRect(UIRect r) { return { r.x0 + 8, r.y0 + 24, r.x1 - 8, r.y0 + 50 }; }

// Sets a slider's value directly from a mouse x position along its
// track -- shared by the initial click and every subsequent drag
// update while the button stays held.
static void ApplySliderDrag(int mx) {
    if (g_draggingSlider == SLIDER_NONE) return;
    UIRect track = GetSliderTrackRect(GetSliderRowRect(g_draggingSlider));
    float t = (mx - track.x0) / (track.x1 - track.x0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SliderRange rng = GetSliderRange(g_draggingSlider);
    SetSliderValue(g_draggingSlider, rng.minV + t * (rng.maxV - rng.minV));
}
static void BeginSliderDrag(int id, int mx) {
    g_draggingSlider = id;
    SetCapture(g_hwnd); // keep receiving WM_MOUSEMOVE if the drag leaves the client area
    ApplySliderDrag(mx);
}

// Wrap Save/Load so every call site (F5/F9-equivalent bound inputs and
// the pause-menu buttons) gets the same on-screen confirmation instead
// of failing or succeeding silently.
static void DoSave() {
    bool ok = SaveGame(g_world, g_player, g_currentSlot);
    g_toastMessage = ok ? "GAME SAVED" : "SAVE FAILED";
    g_toastTimer = 2.0f;
}
static void DoLoad() {
    bool ok = LoadGame(g_world, g_player, g_currentSlot);
    g_toastMessage = ok ? "GAME LOADED" : "LOAD FAILED (no save?)";
    g_toastTimer = 2.0f;
}

static void HandleMenuClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_RESUME))) { g_menuScreen = MenuScreen::None; CaptureMouseForPlay(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_OPTIONS))) { g_optionsReturnScreen = MenuScreen::Pause; g_menuScreen = MenuScreen::OptionsHub; return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_SAVE))) { DoSave(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_LOAD))) { DoLoad(); g_menuScreen = MenuScreen::None; CaptureMouseForPlay(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT_TO_TITLE))) {
        g_gameState = GameState::Title;
        g_menuScreen = MenuScreen::TitleMain;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT))) { PostQuitMessage(0); return; }
}

static void HandleOptionsHubClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_LOOK))) { g_menuScreen = MenuScreen::LookSettings; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_GRAPHICS))) { g_menuScreen = MenuScreen::Graphics; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_DISPLAY))) { g_menuScreen = MenuScreen::Display; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_AUDIO))) { g_menuScreen = MenuScreen::Audio; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_ACCESSIBILITY))) { g_menuScreen = MenuScreen::Accessibility; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_KEYBINDS))) { g_menuScreen = MenuScreen::Keybindings; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_BACK))) { g_menuScreen = g_optionsReturnScreen; return; }
}

static void HandleLookSettingsClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_X))) { g_invertX = !g_invertX; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_Y))) { g_invertY = !g_invertY; SaveSettings(); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_X)))) { BeginSliderDrag(SLIDER_SENS_X, mx); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_Y)))) { BeginSliderDrag(SLIDER_SENS_Y, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_RESET))) { ResetLookSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleGraphicsClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RENDER_DIST)))) { BeginSliderDrag(SLIDER_RENDER_DIST, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RESET))) { ResetGraphicsSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleDisplayClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_FPS))) { g_showFPS = !g_showFPS; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_RESET))) { ResetDisplaySettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleAudioClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME)))) { BeginSliderDrag(SLIDER_MASTER_VOLUME, mx); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME)))) { BeginSliderDrag(SLIDER_MUSIC_VOLUME, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(AUDIO_LAYOUT, AROW_RESET))) { ResetAudioSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(AUDIO_LAYOUT, AROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleAccessibilityClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_FOV)))) { BeginSliderDrag(SLIDER_FOV, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_TOGGLE_MOVE))) {
        g_toggleMovement = !g_toggleMovement;
        memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch)); // switching modes shouldn't leave a stale latch active
        SaveSettings();
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_HIGH_CONTRAST))) { g_highContrastUI = !g_highContrastUI; SaveSettings(); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MUSIC_INTENSITY)))) { BeginSliderDrag(SLIDER_MUSIC_INTENSITY, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_RESET))) { ResetAccessibilitySettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleKeybindingsClick(int mx, int my) {
    for (int i = 0; i < ACT_COUNT; i++) {
        if (PointInRect(mx, my, SubmenuRowRect(KEYBIND_LAYOUT, i))) { g_rebindingAction = i; return; }
    }
    if (PointInRect(mx, my, SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT))) { ResetKeybindingsToDefault(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT + 1))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}

// Resets world/player/chunk-generation bookkeeping to a brand-new game
// -- the same bookkeeping reset LoadGame performs after a load (Section
// 7.4), just starting from nothing instead of loaded data. The normal
// per-tick EnsureChunksLoaded/ProcessColumnGeneration path (Section 2.4)
// then lazily generates terrain around the spawn point exactly as it
// always has, once ticking resumes.
static void ResetWorldForNewGame() {
    g_world = World();
    g_player = Player();
    g_dayTimeSeconds = 0.0f; // dawn -- first light in a land they've never seen (Section 13)
    g_generatedColumns.clear();
    g_pendingColumns.clear();
    g_pendingColumnSet.clear();
    g_lastPlayerChunkX = INT32_MIN;
    g_lastPlayerChunkZ = INT32_MIN;
}

// Shared tail end of both New Game and Load Game: leave the slot
// picker, mark a real game as running, and hand control to the player.
static void EnterGameplay() {
    g_gameState = GameState::InGame;
    g_menuScreen = MenuScreen::None;
    CaptureMouseForPlay();
}

static void HandleTitleClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_NEW_GAME))) {
        g_slotPickerMode = SlotPickerMode::New;
        g_confirmOverwriteSlot = -1;
        g_menuScreen = MenuScreen::SlotPicker;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_LOAD_GAME))) {
        g_slotPickerMode = SlotPickerMode::Load;
        g_confirmOverwriteSlot = -1;
        g_menuScreen = MenuScreen::SlotPicker;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_OPTIONS))) { g_optionsReturnScreen = MenuScreen::TitleMain; g_menuScreen = MenuScreen::OptionsHub; return; }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_QUIT))) { PostQuitMessage(0); return; }
}

static void HandleSlotPickerClick(int mx, int my) {
    for (int slot = 0; slot < MAX_SAVE_SLOTS; slot++) {
        if (!PointInRect(mx, my, SubmenuRowRect(SLOT_PICKER_LAYOUT, slot))) continue;

        if (g_slotPickerMode == SlotPickerMode::Load) {
            if (!SlotExists(slot)) { g_toastMessage = "EMPTY SLOT"; g_toastTimer = 1.5f; return; }
            g_currentSlot = slot;
            if (!LoadGame(g_world, g_player, slot)) {
                g_toastMessage = "LOAD FAILED (corrupt save?)";
                g_toastTimer = 2.0f;
                return;
            }
            EnterGameplay();
            return;
        }

        // New Game: an empty slot starts immediately; an occupied one
        // needs a second click within a few seconds to confirm the
        // overwrite, rather than silently destroying an existing world.
        if (SlotExists(slot) && g_confirmOverwriteSlot != slot) {
            g_confirmOverwriteSlot = slot;
            g_confirmOverwriteTimer = 4.0f;
            return;
        }
        g_currentSlot = slot;
        ResetWorldForNewGame();
        SaveGame(g_world, g_player, slot); // write immediately so the slot is no longer "empty" from this point on
        EnterGameplay();
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(SLOT_PICKER_LAYOUT, SLOTROW_BACK))) { g_menuScreen = MenuScreen::TitleMain; return; }
}

// Centralizes what a just-pressed input does, whatever its source (a
// VK_* from WM_KEYDOWN, or a MOUSE_* sentinel from a mouse-button-down
// message) -- the single place Menu/Save/Load/Break/Place dispatch is
// gated, so every input source is guaranteed to agree on the rules
// instead of each caller re-deriving them.
static bool IsSettingsSubmenu(MenuScreen s) {
    return s == MenuScreen::LookSettings || s == MenuScreen::Graphics || s == MenuScreen::Display
        || s == MenuScreen::Audio || s == MenuScreen::Accessibility || s == MenuScreen::Keybindings;
}
static void FireBoundAction(int code) {
    if (code == g_keyBindings[ACT_MENU]) {
        if (g_gameState == GameState::Title) {
            // ESC only ever backs out one level while at the title
            // screen -- there's no "resume gameplay" state to return to,
            // and TitleMain itself is the top of this tree.
            if (g_menuScreen == MenuScreen::SlotPicker) g_menuScreen = MenuScreen::TitleMain;
            else if (g_menuScreen == MenuScreen::OptionsHub) g_menuScreen = g_optionsReturnScreen;
            else if (IsSettingsSubmenu(g_menuScreen)) g_menuScreen = MenuScreen::OptionsHub;
            return;
        }
        if (g_menuScreen == MenuScreen::None) {
            g_menuScreen = MenuScreen::Pause;
            ReleaseMouseForMenu();
        } else if (g_menuScreen == MenuScreen::Pause) {
            g_menuScreen = MenuScreen::None;
            CaptureMouseForPlay();
        } else if (g_menuScreen == MenuScreen::OptionsHub) {
            g_menuScreen = g_optionsReturnScreen;
        } else if (IsSettingsSubmenu(g_menuScreen)) {
            g_menuScreen = MenuScreen::OptionsHub;
        } else {
            g_menuScreen = MenuScreen::Pause;
        }
        return;
    }
    if (g_gameState == GameState::Title) return; // Save/Load/Break/Place all require an actual game running
    if (code == g_keyBindings[ACT_SAVE]) { DoSave(); return; }
    if (code == g_keyBindings[ACT_LOAD]) { DoLoad(); return; }
    if (g_menuScreen != MenuScreen::None) return; // Break/Place only fire during actual play
    if (!g_mouseCaptured) return;
    if (code == g_keyBindings[ACT_BREAK]) { PickAndAct(true); return; }
    if (code == g_keyBindings[ACT_PLACE]) { PickAndAct(false); return; }
}

// Dispatches a click to whichever submenu is currently open. Only
// called for the left button -- menus never respond to right/middle
// click, matching ordinary UI convention.
static void DispatchMenuClick(int mx, int my) {
    switch (g_menuScreen) {
    case MenuScreen::Pause: HandleMenuClick(mx, my); break;
    case MenuScreen::OptionsHub: HandleOptionsHubClick(mx, my); break;
    case MenuScreen::LookSettings: HandleLookSettingsClick(mx, my); break;
    case MenuScreen::Graphics: HandleGraphicsClick(mx, my); break;
    case MenuScreen::Display: HandleDisplayClick(mx, my); break;
    case MenuScreen::Audio: HandleAudioClick(mx, my); break;
    case MenuScreen::Accessibility: HandleAccessibilityClick(mx, my); break;
    case MenuScreen::Keybindings: HandleKeybindingsClick(mx, my); break;
    case MenuScreen::TitleMain: HandleTitleClick(mx, my); break;
    case MenuScreen::SlotPicker: HandleSlotPickerClick(mx, my); break;
    default: break;
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
        ApplySliderDrag(g_mouseX); // no-op unless a slider is actively held
        return 0;
    case WM_LBUTTONDOWN: {
        g_mouseButtonDown[0] = true;
        int mx = (int)(short)LOWORD(lParam), my = (int)(short)HIWORD(lParam);
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_LEFT; g_rebindingAction = -1; SaveSettings(); return 0; }
        if (g_menuScreen != MenuScreen::None) { DispatchMenuClick(mx, my); return 0; }
        if (!g_mouseCaptured) { CaptureMouseForPlay(); return 0; }
        FireBoundAction(MOUSE_LEFT);
        return 0;
    }
    case WM_LBUTTONUP:
        g_mouseButtonDown[0] = false;
        // Only release capture if a slider drag actually set it --
        // unconditionally releasing here would also kick the player out
        // of FPS mouse-look capture (CaptureMouseForPlay's SetCapture)
        // on every ordinary left-click-to-break-block during gameplay.
        if (g_draggingSlider != SLIDER_NONE) {
            g_draggingSlider = SLIDER_NONE;
            ReleaseCapture();
            SaveSettings(); // once per completed drag, not per pixel of motion
        }
        return 0;
    case WM_RBUTTONDOWN:
        g_mouseButtonDown[1] = true;
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_RIGHT; g_rebindingAction = -1; SaveSettings(); return 0; }
        FireBoundAction(MOUSE_RIGHT);
        return 0;
    case WM_RBUTTONUP:
        g_mouseButtonDown[1] = false;
        return 0;
    case WM_MBUTTONDOWN:
        g_mouseButtonDown[2] = true;
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_MIDDLE; g_rebindingAction = -1; SaveSettings(); return 0; }
        FireBoundAction(MOUSE_MIDDLE);
        return 0;
    case WM_MBUTTONUP:
        g_mouseButtonDown[2] = false;
        return 0;
    case WM_KEYDOWN:
        if (wParam < 256) g_keyDown[wParam] = true;
        if (g_rebindingAction != -1) {
            // Escape is reserved as the universal "cancel this rebind"
            // input rather than something bindable mid-capture -- every
            // other key or mouse button commits as the new binding,
            // wherever it's pressed (including, e.g., on the Back row).
            if (wParam != VK_ESCAPE) { g_keyBindings[g_rebindingAction] = (int)wParam; SaveSettings(); }
            g_rebindingAction = -1;
            return 0;
        }
        if (wParam >= '1' && wParam <= '9' && g_menuScreen == MenuScreen::None) {
            int idx = (int)(wParam - '1');
            if (idx < g_placeableCount) g_player.hotbarIndex = idx;
            return 0;
        }
        // Toggle-to-move (Accessibility, Section 11): flip the latch on a
        // genuine press only -- bit 30 of lParam is set when this
        // WM_KEYDOWN is Windows' own key-repeat rather than a fresh
        // press, and without excluding it, holding the key would rapidly
        // flip the latch back and forth instead of toggling once.
        if (g_toggleMovement && g_menuScreen == MenuScreen::None && !(lParam & (1 << 30))) {
            for (GameAction a : { ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT }) {
                if ((int)wParam == g_keyBindings[a]) g_moveToggleLatch[a] = !g_moveToggleLatch[a];
            }
        }
        FireBoundAction((int)wParam);
        return 0;
    case WM_KEYUP:
        if (wParam < 256) g_keyDown[wParam] = false;
        return 0;
    case WM_KILLFOCUS:
        // Losing focus while a key is held would otherwise leave it
        // stuck "down" forever -- this window won't get the matching
        // WM_KEYUP if focus moved elsewhere. And losing focus while the
        // mouse is captured would otherwise keep yanking the real
        // cursor back to center every frame even while alt-tabbed away,
        // since the look-code's recenter loop only checked
        // g_mouseCaptured, not whether this window was still focused.
        // Auto-pausing (like most FPS games do on focus loss) fixes
        // both at once: it releases the cursor immediately, and the
        // key-state reset below prevents any stuck movement.
        memset(g_keyDown, 0, sizeof(g_keyDown));
        memset(g_mouseButtonDown, 0, sizeof(g_mouseButtonDown));
        memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch)); // don't resume walking on refocus from a stale toggle
        g_rebindingAction = -1;
        // Mouse capture isn't guaranteed to be released automatically
        // just because keyboard focus was -- release whatever a slider
        // drag or FPS-look capture left behind explicitly (harmless
        // no-op if nothing was actually captured).
        g_draggingSlider = SLIDER_NONE;
        ReleaseCapture();
        if (g_mouseCaptured) {
            g_menuScreen = MenuScreen::Pause;
            ReleaseMouseForMenu();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
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

    bool menuIsOpen = g_menuScreen != MenuScreen::None;

    if (g_mouseCaptured && !menuIsOpen) {
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

    if (!menuIsOpen) {
        std::string name = g_blockNames[g_placeable[g_player.hotbarIndex]];
        float scale = 0.8f;
        float tw = UITextWidth(name, scale);
        UIDrawText(glyphVerts, name, (SCREEN_W - tw) / 2.0f, hbY0 - 26.0f, scale, 1, 1, 1, 0.9f);
    }

    if (!g_mouseCaptured && !menuIsOpen) {
        std::string hint = "CLICK TO PLAY";
        float scale = 1.3f;
        float tw = UITextWidth(hint, scale);
        UIDrawText(glyphVerts, hint, (SCREEN_W - tw) / 2.0f, SCREEN_H * 0.42f, scale, 1, 1, 1, 0.9f);
    }

    // High-contrast mode (Accessibility, Section 11) pushes every panel/
    // button/track toward the luminance extremes -- near-black
    // backgrounds, a strongly saturated hover/handle color -- rather
    // than the subtle gray-shade steps used otherwise. Text is already
    // white-on-dark in both modes, at effectively maximum contrast, so
    // only the fill colors below need to branch.
    auto drawRowButton = [&](const UIRect& r, const std::string& label, float scale = 1.0f) {
        bool hover = PointInRect(g_mouseX, g_mouseY, r);
        if (g_highContrastUI) {
            float shade = hover ? 0.9f : 0.04f;
            UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, shade, shade, hover ? 0.1f : shade, 1);
        } else {
            float shade = hover ? 0.32f : 0.22f;
            UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, shade, shade, shade + 0.06f, 1);
        }
        float lw = UITextWidth(label, scale);
        UIDrawText(glyphVerts, label, r.x0 + ((r.x1 - r.x0) - lw) / 2.0f, r.y0 + (r.y1 - r.y0 - UI_CELL_H * scale) / 2.0f, scale, 1, 1, 1, 1);
    };
    // A slider row: label above, track+handle below. Value/range/label
    // text all come from the generic slider-by-ID lookups, so adding a
    // slider anywhere else only means adding cases there, not another
    // copy of this drawing code.
    auto drawSliderRow = [&](UIRect r, int sliderId) {
        UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, g_highContrastUI ? 0.03f : 0.16f, g_highContrastUI ? 0.03f : 0.16f, g_highContrastUI ? 0.03f : 0.19f, 1);
        UIDrawText(glyphVerts, GetSliderLabel(sliderId), r.x0 + 8, r.y0 + 2.0f, 0.8f, 1, 1, 1, 1);

        UIRect track = GetSliderTrackRect(r);
        UIDrawRect(glyphVerts, track.x0, track.y0, track.x1, track.y1, 0, 0, 0, 1);
        SliderRange rng = GetSliderRange(sliderId);
        float t = (GetSliderValue(sliderId) - rng.minV) / (rng.maxV - rng.minV);
        float handleCx = track.x0 + t * (track.x1 - track.x0);
        bool hover = PointInRect(g_mouseX, g_mouseY, GetSliderHitRect(r));
        float hc = hover ? 1.0f : (g_highContrastUI ? 0.95f : 0.85f);
        UIDrawRect(glyphVerts, handleCx - 6, track.y0 - 6, handleCx + 6, track.y1 + 6, hc, hc, g_highContrastUI ? 0.0f : 0.2f, 1);
    };
    auto drawPanelTitle = [&](const UIRect& panel, float panelW, const char* title, float scale) {
        float tw = UITextWidth(title, scale);
        UIDrawText(glyphVerts, title, panel.x0 + (panelW - tw) / 2.0f, panel.y0 + 16.0f, scale, 1, 1, 1, 1);
    };
    auto drawPanelBg = [&](const UIRect& panel) {
        if (g_highContrastUI) UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.0f, 0.0f, 0.0f, 0.98f);
        else UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.10f, 0.10f, 0.13f, 0.95f);
    };

    if (g_menuScreen != MenuScreen::None) {
        UIDrawRect(glyphVerts, 0, 0, (float)SCREEN_W, (float)SCREEN_H, 0, 0, 0, 0.55f);
    }

    if (g_menuScreen == MenuScreen::Pause) {
        UIRect panel = SubmenuPanelRect(PAUSE_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, PAUSE_LAYOUT.panelW, "PAUSED", 1.3f);

        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_RESUME), "RESUME");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_OPTIONS), "OPTIONS");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_SAVE), "SAVE GAME");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_LOAD), "LOAD GAME");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT_TO_TITLE), "QUIT TO TITLE");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT), "QUIT");
    } else if (g_menuScreen == MenuScreen::OptionsHub) {
        UIRect panel = SubmenuPanelRect(OPTIONS_HUB_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, OPTIONS_HUB_LAYOUT.panelW, "OPTIONS", 1.2f);

        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_LOOK), "LOOK SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_GRAPHICS), "GRAPHICS SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_DISPLAY), "DISPLAY SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_AUDIO), "AUDIO SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_ACCESSIBILITY), "ACCESSIBILITY");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_KEYBINDS), "KEYBINDINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::TitleMain) {
        UIRect panel = SubmenuPanelRect(TITLE_LAYOUT);
        drawPanelBg(panel);
        std::string gameTitle = "VOXISTICS";
        float titleScale = 1.6f;
        UIDrawText(glyphVerts, gameTitle, panel.x0 + (TITLE_LAYOUT.panelW - UITextWidth(gameTitle, titleScale)) / 2.0f, panel.y0 - 60.0f, titleScale, 1, 1, 1, 1);

        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_NEW_GAME), "NEW GAME");
        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_LOAD_GAME), "LOAD GAME");
        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_OPTIONS), "OPTIONS");
        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_QUIT), "QUIT");
    } else if (g_menuScreen == MenuScreen::SlotPicker) {
        UIRect panel = SubmenuPanelRect(SLOT_PICKER_LAYOUT);
        drawPanelBg(panel);
        const char* title = g_slotPickerMode == SlotPickerMode::New ? "NEW GAME - CHOOSE A SLOT" : "LOAD GAME - CHOOSE A SLOT";
        drawPanelTitle(panel, SLOT_PICKER_LAYOUT.panelW, title, 0.9f);

        for (int slot = 0; slot < MAX_SAVE_SLOTS; slot++) {
            bool occupied = SlotExists(slot);
            std::string label;
            char slotNum[16];
            snprintf(slotNum, sizeof(slotNum), "WORLD %d", slot + 1);
            if (g_slotPickerMode == SlotPickerMode::New && g_confirmOverwriteSlot == slot) {
                label = std::string(slotNum) + " - CLICK AGAIN TO OVERWRITE";
            } else {
                label = std::string(slotNum) + (occupied ? " - SAVED" : " - EMPTY");
            }
            drawRowButton(SubmenuRowRect(SLOT_PICKER_LAYOUT, slot), label, 0.85f);
        }
        drawRowButton(SubmenuRowRect(SLOT_PICKER_LAYOUT, SLOTROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::LookSettings) {
        UIRect panel = SubmenuPanelRect(LOOK_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, LOOK_LAYOUT.panelW, "LOOK SETTINGS", 1.1f);

        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_X), g_invertX ? "INVERT X LOOK: ON" : "INVERT X LOOK: OFF");
        drawSliderRow(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_X), SLIDER_SENS_X);
        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_Y), g_invertY ? "INVERT Y LOOK: ON" : "INVERT Y LOOK: OFF");
        drawSliderRow(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_Y), SLIDER_SENS_Y);
        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Graphics) {
        UIRect panel = SubmenuPanelRect(GRAPHICS_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, GRAPHICS_LAYOUT.panelW, "GRAPHICS SETTINGS", 1.0f);

        drawSliderRow(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RENDER_DIST), SLIDER_RENDER_DIST);
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Display) {
        UIRect panel = SubmenuPanelRect(DISPLAY_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, DISPLAY_LAYOUT.panelW, "DISPLAY SETTINGS", 1.0f);

        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_FPS), g_showFPS ? "SHOW FPS COUNTER: ON" : "SHOW FPS COUNTER: OFF");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Audio) {
        UIRect panel = SubmenuPanelRect(AUDIO_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, AUDIO_LAYOUT.panelW, "AUDIO SETTINGS", 1.0f);

        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME), SLIDER_MASTER_VOLUME);
        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME), SLIDER_MUSIC_VOLUME);
        drawRowButton(SubmenuRowRect(AUDIO_LAYOUT, AROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(AUDIO_LAYOUT, AROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Accessibility) {
        UIRect panel = SubmenuPanelRect(ACCESSIBILITY_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, ACCESSIBILITY_LAYOUT.panelW, "ACCESSIBILITY", 1.0f);

        drawSliderRow(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_FOV), SLIDER_FOV);
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_TOGGLE_MOVE), g_toggleMovement ? "TOGGLE-TO-MOVE: ON" : "TOGGLE-TO-MOVE: OFF");
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_HIGH_CONTRAST), g_highContrastUI ? "HIGH-CONTRAST UI: ON" : "HIGH-CONTRAST UI: OFF");
        drawSliderRow(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MUSIC_INTENSITY), SLIDER_MUSIC_INTENSITY);
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Keybindings) {
        UIRect panel = SubmenuPanelRect(KEYBIND_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, KEYBIND_LAYOUT.panelW, "KEYBINDINGS", 1.0f);
        std::string hint = "CLICK A ROW, THEN PRESS THE NEW INPUT";
        UIDrawText(glyphVerts, hint, panel.x0 + (KEYBIND_LAYOUT.panelW - UITextWidth(hint, 0.55f)) / 2.0f, panel.y0 + 44.0f, 0.55f, 0.8f, 0.8f, 0.8f, 0.8f);

        for (int i = 0; i < ACT_COUNT; i++) {
            std::string label;
            if (g_rebindingAction == i) label = std::string(g_actionLabels[i]) + ": PRESS INPUT (ESC CANCELS)";
            else label = std::string(g_actionLabels[i]) + ": [" + GetInputDisplayName(g_keyBindings[i]) + "]";
            drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, i), label, 0.7f);
        }
        drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT + 1), "BACK");
    }

    if (g_showFPS) {
        char buf[32];
        snprintf(buf, sizeof(buf), "FPS: %d", g_fpsDisplay);
        UIDrawText(glyphVerts, buf, 12.0f, 12.0f, 0.9f, 1, 1, 0.6f, 0.9f);
    }

    // Transient save/load confirmation -- fades over its last half
    // second so it doesn't just vanish abruptly.
    if (g_toastTimer > 0.0f) {
        float alpha = g_toastTimer < 0.5f ? g_toastTimer / 0.5f : 1.0f;
        float scale = 1.1f;
        float tw = UITextWidth(g_toastMessage, scale);
        UIDrawText(glyphVerts, g_toastMessage, (SCREEN_W - tw) / 2.0f, 40.0f, scale, 1.0f, 0.95f, 0.55f, alpha);
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Wide (W-suffixed) throughout, deliberately -- mixing an ANSI-
    // registered window (RegisterClassA/CreateWindowA) with the wide
    // DefWindowProcW that the project's Unicode character-set setting
    // makes the unsuffixed DefWindowProc macro expand to is a known
    // Win32 mismatch that corrupts non-client text (the title bar):
    // it's what produced the garbled CJK-looking title before this.
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VoxisticsWindowClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT wr = { 0, 0, SCREEN_W, SCREEN_H };
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    AdjustWindowRect(&wr, style, FALSE);
    g_hwnd = CreateWindowW(L"VoxisticsWindowClass", L"Voxistics",
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            wr.right - wr.left, wr.bottom - wr.top,
                            nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return -1;
    ShowWindow(g_hwnd, nCmdShow);

    LoadSettings(); // before anything reads g_sensitivityMultX/g_loadRadius/g_masterVolume/etc.
    MigrateLegacySingleSaveIfPresent(); // before the title screen's slot picker can show slot 1

    // XAudio2Create requires COM initialized on the calling thread.
    // Nothing else in this file has needed that so far (SHGetKnownFolderPath
    // manages its own COM state internally), so this is the first call
    // that actually needs it.
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(comHr);

    if (!InitD3D(g_hwnd)) return -1;
    if (!InitTextures()) return -1;
    InitAudio(); // a machine with no usable audio device still gets a silent but playable game (Section 10)
    BuildPipeMeshes();
    BuildSkyMesh();

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

        if (g_toastTimer > 0.0f) {
            g_toastTimer -= dt;
            if (g_toastTimer < 0.0f) g_toastTimer = 0.0f;
        }
        if (g_confirmOverwriteSlot != -1) {
            g_confirmOverwriteTimer -= dt;
            if (g_confirmOverwriteTimer <= 0.0f) g_confirmOverwriteSlot = -1; // armed confirm expired; next click re-arms instead of overwriting
        }

        g_fpsFrameCount++;
        g_fpsTimer += dt;
        if (g_fpsTimer >= 1.0f) {
            g_fpsDisplay = g_fpsFrameCount;
            g_fpsFrameCount = 0;
            g_fpsTimer -= 1.0f;
        }

        // The foreground check is defense-in-depth alongside the
        // WM_KILLFOCUS handler above: without it, a focus change this
        // same frame that WM_KILLFOCUS hasn't been dispatched for yet
        // would still let this recenter the real cursor into the
        // window while some other application is what's actually
        // focused.
        if (g_mouseCaptured && GetForegroundWindow() == g_hwnd) {
            POINT cursor; GetCursorPos(&cursor);
            RECT rc; GetClientRect(g_hwnd, &rc);
            POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
            ClientToScreen(g_hwnd, &center);
            int dx = cursor.x - center.x, dy = cursor.y - center.y;
            float sensX = BASE_MOUSE_SENS * g_sensitivityMultX;
            float sensY = BASE_MOUSE_SENS * g_sensitivityMultY;
            g_player.yaw += (g_invertX ? -dx : dx) * sensX;
            g_player.pitch += (g_invertY ? dy : -dy) * sensY;
            if (g_player.pitch > 1.55f) g_player.pitch = 1.55f;
            if (g_player.pitch < -1.55f) g_player.pitch = -1.55f;
            SetCursorPos(center.x, center.y);
        }

        // Fixed-timestep simulation, decoupled from render/present rate
        // (Section 5.3's recommended accumulator approach). While the
        // pause menu is open the world is frozen and the accumulator is
        // dropped rather than left to build up, so resuming doesn't
        // trigger a burst of catch-up ticks for however long it was paused.
        if (g_menuScreen != MenuScreen::None) {
            accumulator = 0.0f;
        } else {
            while (accumulator >= FIXED_DT) {
                // Day clock (Section 13): advances only here, gated
                // identically to every other simulation system -- the
                // single authoritative source of "what time is it,"
                // which the music (Part XIV) reads directly rather than
                // tracking its own independent notion of time.
                g_dayTimeSeconds = fmodf(g_dayTimeSeconds + FIXED_DT, DAY_LENGTH_SECONDS);

                int pcx = FloorDiv16((int)floor(g_player.x));
                int pcz = FloorDiv16((int)floor(g_player.z));
                EnsureChunksLoaded(pcx, pcz);
                ProcessColumnGeneration(g_world);

                bool fwd = IsActionDown(ACT_FORWARD), back = IsActionDown(ACT_BACK);
                bool left = IsActionDown(ACT_LEFT), right = IsActionDown(ACT_RIGHT);
                bool jump = IsActionDown(ACT_JUMP);
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
        // g_fov (Accessibility, Section 11) is stored in degrees since
        // that's the meaningful unit for a player-facing slider; 45 deg
        // is this constant's old fixed value, unchanged until the
        // slider is touched.
        float fovRadians = g_fov * (3.14159265359f / 180.0f);
        Mat4 proj = MatPerspectiveFovLH(fovRadians, (float)SCREEN_W / SCREEN_H, 0.1f, 500.0f);
        Mat4 viewProj = MatMul(view, proj);

        // Sky pass: depth off (reusing the UI pass's depth-disabled
        // state), drawn before the opaque world pass so normal depth-
        // tested geometry always overdraws it regardless of the sky
        // box's actual size. Its view matrix drops the eye position
        // (rotation only) so the sky rotates with the camera but never
        // translates with it, same as any conventional skybox.
        {
            Mat4 skyView = MatLookToLH({ 0, 0, 0 }, f, u);
            Mat4 skyViewProj = MatMul(skyView, proj);
            g_context->OMSetDepthStencilState(g_uiDepthState, 0);
            g_context->VSSetShader(g_skyVS, nullptr, 0);
            g_context->PSSetShader(g_skyPS, nullptr, 0);
            g_context->IASetInputLayout(g_skyLayout);
            g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_context->VSSetConstantBuffers(0, 1, &g_skyCBuffer);
            D3D11_MAPPED_SUBRESOURCE mapped;
            g_context->Map(g_skyCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            *(Mat4*)mapped.pData = skyViewProj;
            g_context->Unmap(g_skyCBuffer, 0);
            UINT skyStride = sizeof(SkyVertex), skyOffset = 0;
            g_context->IASetVertexBuffers(0, 1, &g_skyVB, &skyStride, &skyOffset);
            g_context->IASetIndexBuffer(g_skyIB, DXGI_FORMAT_R32_UINT, 0);
            g_context->DrawIndexed(g_skyIndexCount, 0, 0);
            g_context->OMSetDepthStencilState(g_depthState, 0);
        }

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
        // limit, Section 4.4) -- fine at prototype density. Which mesh
        // is bound switches per instance based on shape, still with no
        // extra draw calls added.
        g_context->PSSetShaderResources(0, 1, &g_pipeSRV);
        for (auto& kv : g_world.chunks) {
            for (auto& pipe : kv.second->pipes) {
                PipeMesh& mesh = g_pipeMeshes[pipe.shape];
                if (mesh.indexCount == 0) continue;
                g_context->IASetVertexBuffers(0, 1, &mesh.vb, &stride, &offset);
                g_context->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R32_UINT, 0);
                Mat4 world = MatTranslation((float)pipe.worldX, (float)pipe.worldY, (float)pipe.worldZ);
                UpdateCBuffer(MatMul(world, viewProj));
                g_context->DrawIndexed(mesh.indexCount, 0, 0);
            }
        }

        RenderUIPass();

        g_swapChain->Present(1, 0);
    }

    ShutdownAudio();
    if (comInitialized) CoUninitialize();
    return 0;
}
