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

struct Chunk {
    uint8_t blocks[CHUNK_CELLS] = {};
    // Per-block state byte (blocks.h: facing in the low bits), parallel
    // to blocks[]. Always 0 for blocks that don't use it.
    uint8_t state[CHUNK_CELLS] = {};
    // Per-block data records (a chest's contents, a machine's buffers),
    // keyed by LocalIndex. Sparse and usually absent entirely -- the
    // pointer stays null for the vast majority of chunks, which hold no
    // such block. A record is dropped whenever its cell's block changes.
    std::unique_ptr<std::unordered_map<uint16_t, std::vector<uint8_t>>> data;
    // True once this chunk differs from what the world's generator
    // produces for it (any edit, fall, or load). Unmodified chunks are
    // never saved and are simply dropped on eviction: the generator
    // rebuilds them bit-for-bit (Section 7.5), so only real changes cost
    // memory or disk.
    bool modified = false;
    bool dirty = true;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    unsigned int indexCount = 0; // UINT, spelled out so this header doesn't need <windows.h>
    unsigned int opaqueIndexCount = 0; // the first indices; the rest are see-through (translucent pass)

    ~Chunk(); // defined in world.cpp, where <d3d11.h> (for ->Release()) is actually included

    static int LocalIndex(int lx, int ly, int lz) {
        return (ly * CHUNK_SIZE + lz) * CHUNK_SIZE + lx;
    }

    // Writes one cell; drops any data record there if the block changed.
    void SetCell(int idx, BlockID id, uint8_t st) {
        if (blocks[idx] != id && data) {
            data->erase((uint16_t)idx);
            if (data->empty()) data.reset();
        }
        blocks[idx] = (uint8_t)id;
        state[idx] = st;
    }
};

class World {
public:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks;
    // Resident chunks waiting for a mesh rebuild, so the rebuilder only
    // ever looks at chunks that need work (none at all while nothing is
    // changing) instead of scanning every resident chunk each frame.
    // Every member has its `dirty` flag set; a flagged chunk may sit
    // outside the set while it waits for neighbouring columns. Chunks
    // enter and leave `chunks` only through GetOrCreateChunk /
    // AdoptChunk / TakeChunk / ClearChunks, which keep the two in step.
    std::unordered_set<ChunkCoord, ChunkCoordHash> dirtyChunks;

    // Replaces any chunk already at cc.
    void AdoptChunk(const ChunkCoord& cc, std::unique_ptr<Chunk> c) {
        c->dirty = true; // new to the world: needs a mesh
        chunks[cc] = std::move(c);
        dirtyChunks.insert(cc);
    }
    std::unique_ptr<Chunk> TakeChunk(const ChunkCoord& cc) {
        auto it = chunks.find(cc);
        if (it == chunks.end()) return nullptr;
        std::unique_ptr<Chunk> c = std::move(it->second);
        chunks.erase(it);
        dirtyChunks.erase(cc);
        return c;
    }
    void ClearChunks() { chunks.clear(); dirtyChunks.clear(); }

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
        dirtyChunks.insert(cc); // born dirty: no mesh yet
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
    uint8_t GetState(int x, int y, int z) {
        if (y < Y_MIN || y > Y_MAX) return 0;
        ChunkCoord cc = ToChunk(x, y, z);
        Chunk* c = FindChunk(cc);
        if (!c) return 0;
        int lx = LocalOf(x, cc.x), ly = LocalOf(y, cc.y), lz = LocalOf(z, cc.z);
        return c->state[Chunk::LocalIndex(lx, ly, lz)];
    }

    bool Solid(int x, int y, int z) {
        return BlockSolid(Get(x, y, z));
    }

    // Marks the owning chunk dirty, plus every neighbour -- face, edge
    // or corner -- whose mesh this cell can affect: face culling reaches
    // across a shared face, and ambient occlusion (Section 4.2) reaches
    // across edges and corners too.
    void MarkDirtyForEdit(const ChunkCoord& cc, int lx, int ly, int lz) {
        int x0 = lx == 0 ? -1 : 0, x1 = lx == CHUNK_SIZE - 1 ? 1 : 0;
        int y0 = ly == 0 ? -1 : 0, y1 = ly == CHUNK_SIZE - 1 ? 1 : 0;
        int z0 = lz == 0 ? -1 : 0, z1 = lz == CHUNK_SIZE - 1 ? 1 : 0;
        for (int dy = y0; dy <= y1; dy++)
            for (int dz = z0; dz <= z1; dz++)
                for (int dx = x0; dx <= x1; dx++)
                    MarkChunkDirty({ cc.x + dx, cc.y + dy, cc.z + dz });
    }

    // Always (re)inserts: a chunk can be flagged dirty but parked outside
    // the set while it waits for neighbouring columns (RebuildDirtyChunks),
    // and this is how it gets back in.
    void MarkChunkDirty(const ChunkCoord& cc) {
        Chunk* c = FindChunk(cc);
        if (c) { c->dirty = true; dirtyChunks.insert(cc); }
    }

    // Bulk path (falls, and anything else that must not trigger live
    // gravity checks -- Section 5.2): writes the cell, marks the chunk
    // modified (it no longer matches the generator) and dirty.
    void SetRaw(int x, int y, int z, BlockID id, uint8_t st = 0) {
        if (y < Y_MIN || y > Y_MAX) return;
        ChunkCoord cc = ToChunk(x, y, z);
        Chunk* c = GetOrCreateChunk(cc);
        int lx = LocalOf(x, cc.x), ly = LocalOf(y, cc.y), lz = LocalOf(z, cc.z);
        c->SetCell(Chunk::LocalIndex(lx, ly, lz), id, st);
        c->modified = true;
        MarkDirtyForEdit(cc, lx, ly, lz);
    }

    // Live edit path used during play; gravity re-evaluation happens
    // via the caller invoking MaybeQueueFall after this (kept separate
    // so World has no dependency on the fall-queue globals).
    void Set(int x, int y, int z, BlockID id, uint8_t st = 0) {
        SetRaw(x, y, z, id, st);
    }
};

// Chunk render-distance, horizontal only (Section 2.4); a Graphics
// Settings slider now [1,8]. Defined in world.cpp; read by
// EnsureChunksLoaded here and written by the settings UI (game.cpp).
extern int g_loadRadius;

// =======================================================================
// Part V - Scheduled block updates (gravity is the first kind)
// =======================================================================
//
// "Update the block at (x, y, z) in N ticks": one bounded queue for every
// system whose blocks change over time. Only blocks that are actually
// changing cost anything -- an idle world has an empty queue -- which is
// Part 1.3's rule applied to simulation. At most MAX_UPDATES_PER_TICK run
// per tick however many are due, so one catastrophic edit (removing a
// foundation under a huge structure) is smoothed over many ticks instead
// of spiking a frame (Section 5.1). Due updates run oldest-first.

enum UpdateKind : uint8_t {
    UPD_GRAVITY = 0,  // fall one cell if unsupported
    UPD_KIND_COUNT
};

struct ScheduledUpdate {
    int x, y, z;
    uint32_t due;     // g_worldTick at which it runs
    uint32_t seq;     // FIFO order among updates due the same tick
    UpdateKind kind;
};

static const int MAX_UPDATES_PER_TICK = 64;
// Simulation ticks since the world was loaded or created (relative only;
// saves store updates' remaining delays, not absolute ticks).
extern uint32_t g_worldTick;

void ScheduleUpdate(int x, int y, int z, UpdateKind kind, uint32_t delayTicks);
// Runs due updates (capped), then advances g_worldTick. Once per sim tick.
void ProcessScheduledUpdates(World& w);
// Empties the queue and its per-column bookkeeping together (New Game,
// Load -- entries from the old world must not replay in the new one).
void ClearScheduledUpdates();
size_t ScheduledUpdateCount();
// A snapshot for saving, and the matching restore (delays relative to now).
struct PendingUpdate { int x, y, z; UpdateKind kind; uint32_t delay; };
std::vector<PendingUpdate> SnapshotScheduledUpdates();
void RestoreScheduledUpdates(const std::vector<PendingUpdate>& updates);

// Schedules a gravity check for the block at (x, y, z) if it's a block
// that can fall and currently has nothing under it.
void MaybeQueueFall(World& w, int x, int y, int z, uint32_t delayTicks = 0);
// A live world edit that could have removed support underneath a block.
void LiveEdit(World& w, int x, int y, int z, BlockID id, uint8_t state = 0);

// =======================================================================
// World generation and chunk loading -- deterministic terrain, bypassing
// live gravity (Section 5.2), and column loading kept sparse per Section
// 2.1/2.4. Not a numbered Part of its own in the design doc.
// =======================================================================

// Every world records which generator made it (Section 2.5), because
// unmodified terrain is never saved -- it's regenerated on demand, so a
// save is only readable with the exact generator (type + version) that
// produced it. A generator's output must therefore be a pure function of
// (params, coordinates), and a change to it that alters output needs a
// new version number, with the old version kept for existing worlds.
enum WorldGenType : uint8_t { GEN_HILLS = 0, GEN_FLAT = 1, GEN_TYPE_COUNT };
struct WorldGenParams {
    WorldGenType type = GEN_FLAT;
    uint32_t version = 1;
    uint64_t seed = 0;
};
extern WorldGenParams g_worldGen;
const char* WorldGenName(WorldGenType t);
bool WorldGenFromName(const char* name, WorldGenType& out);
// Highest generator version this build can reproduce, per type.
uint32_t WorldGenLatestVersion(WorldGenType t);
// New worlds use this. TEMPORARY: flat while testing (see world.cpp).
WorldGenParams DefaultNewWorldGen();

int TerrainHeight(int wx, int wz); // for g_worldGen
long long ColumnKey(int cx, int cz);
// Makes a column resident: generates its terrain from g_worldGen, then
// overlays any modified chunks held in g_evictedChunks for it.
void GenerateColumn(World& w, int cx, int cz);

extern std::deque<std::pair<int, int>> g_pendingColumns;
extern std::unordered_set<long long> g_pendingColumnSet;
// Reset directly by LoadGame (persist.cpp) after swapping in a loaded
// world, so the next EnsureChunksLoaded call re-scans from the
// player's actual position instead of trusting stale pre-load state.
extern int g_lastPlayerChunkX, g_lastPlayerChunkZ;
static const int MAX_COLUMN_GENS_PER_TICK = 4;

// Columns currently backing real Chunk objects in World::chunks. Bounded
// by roughly the loaded area rather than lifetime-explored area -- this
// is what keeps the world draw loop bounded by "near the player" instead
// of "everywhere ever visited" (DESIGN.md Part 1.3). A column that isn't
// resident is simply (re)generated when it's needed again.
extern std::unordered_set<long long> g_residentColumns;
// Modified chunks of non-resident columns: everything the generator
// can't reproduce, and nothing it can. Evicting a column moves its
// modified chunks here and simply frees the rest (Section 2.4); loading
// a save puts every saved chunk here; GenerateColumn overlays them back
// onto fresh terrain when the column becomes resident again. A
// ChunkCoord is in World::chunks XOR here, never both. SaveGame
// (persist.cpp) walks both to capture every modified chunk.
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
// True when the column and all 8 around it are resident -- the condition
// for meshing its chunks (face culling and ambient occlusion both read
// across neighbouring columns).
bool ColumnNeighborhoodResident(int cx, int cz);

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

// Player half-width / heights used for AABB collision (Section 4.7).
// Crouched (and sliding) the player is under a block tall, so they fit a
// 1x1 gap.
static const float PLAYER_HALFW = 0.3f;
static const float PLAYER_HEIGHT = 1.8f;
static const float PLAYER_EYE = 1.6f;
static const float PLAYER_CROUCH_HEIGHT = 0.9f;
static const float PLAYER_CROUCH_EYE = 0.75f;
static const float PLAYER_SLIDE_EYE = 0.55f;

struct Player {
    float x = 8.0f, y = 50.0f, z = 8.0f;
    float yaw = 0.0f, pitch = 0.0f;
    float velY = 0.0f;
    bool onGround = false;
    int hotbarIndex = 0;
    // Movement state (not saved: a load into a tight spot just crouches).
    bool sprinting = false;
    bool crouching = false;       // crouch-height box (also while sliding)
    bool crouchHeld = false;      // the crouch input last tick (a fresh press starts a slide)
    float slideTime = 0.0f;       // > 0 while power-sliding: seconds so far
    float slideVX = 0.0f, slideVZ = 0.0f; // the slide's momentum, blocks/s
    // Camera only, eased toward their targets each tick.
    float eyeHeight = PLAYER_EYE; // above the feet
    float roll = 0.0f;            // lean, radians (+ = toward the right)
    float leanPitch = 0.0f;       // added to pitch while leaning into a slide
};

static inline float PlayerHeight(const Player& p) { return p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT; }
static inline bool PlayerSliding(const Player& p) { return p.slideTime > 0.0f; }

// View vectors, including a slide's lean (roll about the view direction,
// a small pitch). `right` stays level, so movement never feels the lean.
static inline void GetCameraVectors(const Player& p, Vec3& forward, Vec3& right, Vec3& up) {
    float pitch = p.pitch + p.leanPitch;
    float cp = cosf(pitch), sp = sinf(pitch);
    float cy = cosf(p.yaw), sy = sinf(p.yaw);
    forward = { sy * cp, sp, cy * cp };
    Vec3 worldUp = { 0, 1, 0 };
    right = Normalize(Cross(worldUp, forward));
    Vec3 levelUp = Cross(forward, right); // perpendicular to forward, in the vertical plane
    float cr = cosf(p.roll), sr = sinf(p.roll);
    up = { levelUp.x * cr + right.x * sr, levelUp.y * cr + right.y * sr, levelUp.z * cr + right.z * sr };
}

// One tick's movement input.
struct MoveInput {
    bool fwd = false, back = false, left = false, right = false, jump = false;
    bool sprint = false, crouch = false;
};

void UpdatePlayerPhysics(World& w, Player& p, float dt, const MoveInput& in);
static inline void UpdatePlayerPhysics(World& w, Player& p, float dt, bool fwd, bool back, bool left, bool right, bool jump) {
    MoveInput in; in.fwd = fwd; in.back = back; in.left = left; in.right = right; in.jump = jump;
    UpdatePlayerPhysics(w, p, dt, in);
}

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
constexpr float DAY_LENGTH_SECONDS = 3600.0f; // one in-game day = one real hour, locked in
extern float g_dayTimeSeconds;

// The live world/player -- defined in world.cpp, used everywhere.
extern World g_world;
extern Player g_player;
