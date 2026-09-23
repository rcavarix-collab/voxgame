// world.h
//
// The world simulation model: chunk storage, the player's physical
// presence in it, gravity, terrain generation/column loading, and
// block-picking. No rendering and no D3D calls here -- Chunk owns a
// vertex/index buffer pair (that's an existing, deliberate design: a
// chunk's GPU mesh is part of its own state, not tracked separately),
// but only as opaque pointers (ID3D11Buffer forward-declared below) so
// this header itself never needs <d3d11.h>; the buffers are actually
// created/destroyed in render.cpp/world.cpp, not here.

#pragma once

#include "common.h"
#include <cstdint>
#include <cfloat>
#include <vector>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// Forward-declared rather than #include <d3d11.h> -- Chunk only ever
// stores pointers to these, never calls a method on them directly in
// this header (the destructor that does is defined in world.cpp, where
// the real header is included).
struct ID3D11Buffer;

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

struct Vertex {
    float px, py, pz;
    float u, v;
};

struct Chunk {
    uint8_t blocks[CHUNK_CELLS] = {};
    bool dirty = true;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    unsigned int indexCount = 0; // UINT, spelled out so this header doesn't need <windows.h>

    ~Chunk(); // defined in world.cpp, where <d3d11.h> (for ->Release()) is actually included

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

// Chunk render-distance, horizontal only (Section 2.4); a Graphics
// Settings slider now [1,8]. Defined in world.cpp; read by
// EnsureChunksLoaded here and written by the settings UI (game.cpp).
extern int g_loadRadius;

// =======================================================================
// Part V - Falling-block gravity system
// =======================================================================

struct FallEntry { int x, y, z; };
extern std::deque<FallEntry> g_fallQueue;

void MaybeQueueFall(World& w, int x, int y, int z);
// Drains at most MAX_FALLS entries per call regardless of queue length,
// so a single catastrophic edit (removing a foundation under a huge
// structure) cannot spike frame time -- the cascade is smoothed across
// many ticks instead (Section 5.1).
void ProcessFalls(World& w);
// A live world edit that could have removed support underneath a block.
void LiveEdit(World& w, int x, int y, int z, BlockID id);

// =======================================================================
// World generation and chunk loading -- deterministic terrain, bypassing
// live gravity (Section 5.2), and column loading kept sparse per Section
// 2.1/2.4. Not a numbered Part of its own in the design doc.
// =======================================================================

int TerrainHeight(int wx, int wz);
long long ColumnKey(int cx, int cz);
void GenerateColumn(World& w, int cx, int cz);

// Once true, always true: "real data exists for this column somewhere"
// (either resident in World::chunks or evicted below), so terrain-gen
// never re-runs over it and stomps player edits. Never shrinks, and
// deliberately not iterated per-frame anywhere -- only ever a set of
// int64 keys, checked by O(1) lookup.
extern std::unordered_set<long long> g_generatedColumns;
extern std::deque<std::pair<int, int>> g_pendingColumns;
extern std::unordered_set<long long> g_pendingColumnSet;
// Reset directly by LoadGame (persist.cpp) after swapping in a loaded
// world, so the next EnsureChunksLoaded call re-scans from the
// player's actual position instead of trusting stale pre-load state.
extern int g_lastPlayerChunkX, g_lastPlayerChunkZ;
static const int MAX_COLUMN_GENS_PER_TICK = 4;

// Columns currently backing real Chunk objects in World::chunks --
// unlike g_generatedColumns, this one DOES shrink (a column leaving the
// load radius is evicted below) and stays bounded by roughly the loaded
// area rather than growing with lifetime-explored area. This is what
// keeps RebuildDirtyChunks's per-frame scan and the world draw loop
// bounded by "near the player" instead of "everywhere ever visited"
// (DESIGN.md Part 1.3) -- frustum culling alone only skipped the draw
// call, not the growth of what there was to scan.
extern std::unordered_set<long long> g_residentColumns;
// Evicted (out-of-radius) chunks, moved here whole with their GPU
// buffers released. A ChunkCoord is in World::chunks XOR here, never
// both -- GenerateColumn and the evict/restore helpers in world.cpp
// maintain that invariant. SaveGame (persist.cpp) must walk both maps
// to capture the complete world, or anything currently evicted would
// silently vanish from the save.
extern std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> g_evictedChunks;
extern std::deque<std::pair<int, int>> g_pendingEvictions;
extern std::unordered_set<long long> g_pendingEvictionSet;
static const int MAX_COLUMN_EVICTIONS_PER_TICK = 4;
// Columns are evicted only once this many chunks beyond g_loadRadius, so
// a player oscillating at the boundary doesn't thrash evict/restore.
static const int CHUNK_EVICT_MARGIN = 2;
// Chebyshev distance in columns; overflow-safe for the INT32_MIN
// "unknown position" sentinel g_lastPlayerChunkX/Z start at.
int ColumnDistance(int cx, int cz, int playerChunkX, int playerChunkZ);

// Only enqueues columns now (Section 5.1's queue pattern applied to
// generation) -- it never touches World directly, unlike its
// gravity/mesh-rebuild counterparts. Also queues eviction for resident
// columns that have drifted well outside the load radius.
void EnsureChunksLoaded(int playerChunkX, int playerChunkZ);
void ProcessColumnGeneration(World& w);
void ProcessColumnEviction(World& w);

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
    BLOCK_CHEST, BLOCK_MACHINE
};
static const int g_placeableCount = sizeof(g_placeable) / sizeof(g_placeable[0]);

static inline void GetCameraVectors(const Player& p, Vec3& forward, Vec3& right, Vec3& up) {
    float cp = cosf(p.pitch), sp = sinf(p.pitch);
    float cy = cosf(p.yaw), sy = sinf(p.yaw);
    forward = { sy * cp, sp, cy * cp };
    up = { 0, 1, 0 };
    right = Normalize(Cross(up, forward));
}

// Player half-width / height used for AABB collision (Section 4.7).
static const float PLAYER_HALFW = 0.3f;
static const float PLAYER_HEIGHT = 1.8f;
static const float PLAYER_EYE = 1.6f;

void UpdatePlayerPhysics(World& w, Player& p, float dt, bool fwd, bool back, bool left, bool right, bool jump);

// =======================================================================
// Section 4.5 - Amanatides-Woo exact voxel DDA raycast for block picking
// =======================================================================

bool Raycast(World& w, float ox, float oy, float oz, float dx, float dy, float dz, float maxDist,
             int& hitX, int& hitY, int& hitZ, int& placeX, int& placeY, int& placeZ);

// =======================================================================
// Section 13 - Day clock
// =======================================================================
//
// World state (not a global preference), since different saves can
// legitimately be at different points in their day -- persisted in the
// save payload alongside player position (persist.cpp), not
// settings.cfg. Advances only while gameplay is actually ticking (the
// same gate that already freezes physics/chunk-gen while any menu is
// open), wraps at DAY_LENGTH_SECONDS. A fresh New Game starts at 0
// (dawn) -- the character's first light in a land they've never seen.
static const float DAY_LENGTH_SECONDS = 3600.0f; // one in-game day = one real hour, locked in
extern float g_dayTimeSeconds;

// The live world/player -- defined in world.cpp, used everywhere.
extern World g_world;
extern Player g_player;
