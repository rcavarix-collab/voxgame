// world.cpp
//
// Implementations for world.h: the Chunk destructor (needs the real
// <d3d11.h> for ->Release(), which world.h itself deliberately avoids
// including), gravity, terrain generation/column loading, player
// physics, and the DDA raycast.

#define NOMINMAX // see render.cpp for why this precedes windows.h (pulled in transitively via d3d11.h here)
#include "world.h"
#include <d3d11.h>
#include <cmath>
#include <cfloat>
#include <algorithm>

Chunk::~Chunk() {
    if (vb) vb->Release();
    if (ib) ib->Release();
}

int g_loadRadius = 3; // chunks, horizontal only (Section 2.4); a Graphics Settings slider now [1,8]

World g_world;
Player g_player;
float g_dayTimeSeconds = 0.0f;

// =======================================================================
// Part V - Falling-block gravity system
// =======================================================================

std::deque<FallEntry> g_fallQueue;
// Pending falls per column, kept in step with g_fallQueue so eviction can
// ask "is a cascade still running here?" without scanning the queue.
static std::unordered_map<long long, int> g_fallsPerColumn;

void ClearFallQueue() {
    g_fallQueue.clear();
    g_fallsPerColumn.clear();
}

void MaybeQueueFall(World& w, int x, int y, int z) {
    if (y < Y_MIN || y > Y_MAX) return;
    BlockID id = w.Get(x, y, z);
    if (id == BLOCK_AIR) return;
    if (g_info[id].foundational) return;
    if (!g_info[id].solid) return;
    if (y - 1 < Y_MIN) return;         // resting on the world floor
    if (w.Solid(x, y - 1, z)) return;  // supported
    g_fallQueue.push_back({ x, y, z });
    g_fallsPerColumn[ColumnKey(FloorDiv16(x), FloorDiv16(z))]++;
}

void ProcessFalls(World& w) {
    int n = (int)std::min<size_t>(MAX_FALLS, g_fallQueue.size());
    for (int i = 0; i < n; i++) {
        FallEntry e = g_fallQueue.front();
        g_fallQueue.pop_front();
        auto fc = g_fallsPerColumn.find(ColumnKey(FloorDiv16(e.x), FloorDiv16(e.z)));
        if (fc != g_fallsPerColumn.end() && --fc->second <= 0) g_fallsPerColumn.erase(fc);

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

void LiveEdit(World& w, int x, int y, int z, BlockID id) {
    w.Set(x, y, z, id);
    MaybeQueueFall(w, x, y + 1, z);
}

// =======================================================================
// World generation and chunk loading
// =======================================================================

// TEMPORARY, for testing: a dead-flat world (surface at y = 12, so every
// column is a single chunk tall). Set false for the rolling hills below.
// Existing saves keep whatever terrain they already stored; only newly
// generated columns follow this switch, so an old hilly save will show
// cliffs where it meets new flat ground.
static const bool FLAT_TEST_WORLD = true;
static const int FLAT_TEST_HEIGHT = 12;

int TerrainHeight(int wx, int wz) {
    if (FLAT_TEST_WORLD) return FLAT_TEST_HEIGHT;
    double h = 40.0 + 6.0 * sin(wx * 0.15) + 4.0 * cos(wz * 0.13);
    int ih = (int)h;
    if (ih < 20) ih = 20;
    if (ih > 60) ih = 60;
    return ih;
}

std::unordered_set<long long> g_generatedColumns;
std::unordered_set<long long> g_residentColumns;
std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> g_evictedChunks;

long long ColumnKey(int cx, int cz) {
    return ((long long)(uint32_t)cx << 32) | (uint32_t)cz;
}
static void DecodeColumnKey(long long key, int& cx, int& cz) {
    cx = (int)(uint32_t)((uint64_t)key >> 32);
    cz = (int)(uint32_t)((uint64_t)key & 0xFFFFFFFFu);
}

int ColumnDistance(int cx, int cz, int playerChunkX, int playerChunkZ) {
    long long adx = (long long)cx - playerChunkX; if (adx < 0) adx = -adx;
    long long adz = (long long)cz - playerChunkZ; if (adz < 0) adz = -adz;
    long long d = adx > adz ? adx : adz;
    return d > INT32_MAX ? INT32_MAX : (int)d;
}

// Faces on a chunk's side toward a neighbor column are culled while that
// neighbor is resident and solid there (and emitted while it's absent),
// so any column appearing or disappearing changes the correct mesh of
// the resident chunks beside it.
static void MarkHorizontalNeighborsDirty(World& w, const ChunkCoord& cc) {
    w.MarkChunkDirty({ cc.x - 1, cc.y, cc.z });
    w.MarkChunkDirty({ cc.x + 1, cc.y, cc.z });
    w.MarkChunkDirty({ cc.x, cc.y, cc.z - 1 });
    w.MarkChunkDirty({ cc.x, cc.y, cc.z + 1 });
}

// Moves each resident chunk of this column into g_evictedChunks as-is
// (no copy of its block data), releasing only its GPU buffers. The
// inverse of RestoreColumnToWorld below.
static void EvictColumnFromWorld(World& w, int cx, int cz) {
    for (int cy = 0; cy <= FloorDiv16(Y_MAX); cy++) {
        ChunkCoord cc{ cx, cy, cz };
        std::unique_ptr<Chunk> c = w.TakeChunk(cc);
        if (!c) continue;
        if (c->vb) { c->vb->Release(); c->vb = nullptr; }
        if (c->ib) { c->ib->Release(); c->ib = nullptr; }
        c->indexCount = 0;
        c->dirty = true; // its mesh is gone; rebuild whenever it comes back
        g_evictedChunks.emplace(cc, std::move(c));
        MarkHorizontalNeighborsDirty(w, cc);
    }
}

static void RestoreColumnToWorld(World& w, int cx, int cz) {
    for (int cy = 0; cy <= FloorDiv16(Y_MAX); cy++) {
        ChunkCoord cc{ cx, cy, cz };
        auto it = g_evictedChunks.find(cc);
        if (it == g_evictedChunks.end()) continue;
        w.AdoptChunk(cc, std::move(it->second)); // still flagged dirty from eviction
        g_evictedChunks.erase(it);
        MarkHorizontalNeighborsDirty(w, cc);
    }
}

// Gravity only ever moves a block straight down, so a pending fall only
// ever touches its own column. Evicting that column mid-cascade would
// make every remaining entry read air and get discarded as stale,
// leaving the rest of the structure floating once the column returns.
static bool ColumnHasPendingFalls(int cx, int cz) {
    return g_fallsPerColumn.count(ColumnKey(cx, cz)) != 0;
}

void GenerateColumn(World& w, int cx, int cz) {
    long long key = ColumnKey(cx, cz);
    if (g_residentColumns.count(key)) return; // already resident -- nothing to do
    if (g_generatedColumns.count(key)) {
        // Real data exists for this column, just not resident right now
        // (it was evicted after the player left, Section 2.4-perf) --
        // restore it rather than re-running terrain generation, which
        // would silently overwrite any edits with fresh terrain.
        RestoreColumnToWorld(w, cx, cz);
        g_residentColumns.insert(key);
        return;
    }
    g_generatedColumns.insert(key);
    g_residentColumns.insert(key);

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

int g_lastPlayerChunkX = INT32_MIN, g_lastPlayerChunkZ = INT32_MIN;

// Columns queued for generation but not yet generated. Entering view
// range only enqueues a column; ProcessColumnGeneration below drains a
// capped number per tick, following the exact pattern the falling-block
// queue already established (Section 5.1): a hard per-tick work cap
// instead of an unbounded burst, so crossing into a large unexplored
// area -- or the initial spawn, which needs the whole load radius at
// once -- can't spike a single frame.
std::deque<std::pair<int, int>> g_pendingColumns;
std::unordered_set<long long> g_pendingColumnSet;
std::deque<std::pair<int, int>> g_pendingEvictions;
std::unordered_set<long long> g_pendingEvictionSet;

void EnsureChunksLoaded(int playerChunkX, int playerChunkZ) {
    // Recomputed only when the player's chunk coordinate actually
    // changes (Section 2.4) -- not every frame.
    if (playerChunkX == g_lastPlayerChunkX && playerChunkZ == g_lastPlayerChunkZ) return;
    g_lastPlayerChunkX = playerChunkX;
    g_lastPlayerChunkZ = playerChunkZ;

    // Queued ring by ring outward from the player's own column, so the
    // ground under and around them always generates first. (This used
    // to be a corner-to-corner raster order, which put the player's own
    // column halfway down the queue -- dozens of ticks at spawn, long
    // enough to fall into where the ground was about to appear.)
    for (int ring = 0; ring <= g_loadRadius; ring++) {
        for (int dx = -ring; dx <= ring; dx++) {
            for (int dz = -ring; dz <= ring; dz++) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring) continue; // ring edge only
                int cx = playerChunkX + dx, cz = playerChunkZ + dz;
                long long key = ColumnKey(cx, cz);
                if (g_residentColumns.count(key) || g_pendingColumnSet.count(key)) continue;
                g_pendingColumnSet.insert(key);
                g_pendingColumns.push_back({ cx, cz });
            }
        }
    }

    // Resident columns that have drifted past a margin beyond the load
    // radius (not right at its edge, so a player oscillating near the
    // boundary doesn't thrash evict/restore every other step) are
    // queued for eviction, drained through the same bounded-per-tick
    // pattern column generation already uses (Section 5.1's philosophy
    // applied here too). g_residentColumns only ever holds roughly the
    // loaded area's worth of keys, so this scan stays cheap regardless
    // of how much total ground the player has covered this session.
    for (long long key : g_residentColumns) {
        int cx, cz; DecodeColumnKey(key, cx, cz);
        int dist = ColumnDistance(cx, cz, playerChunkX, playerChunkZ);
        if (dist > g_loadRadius + CHUNK_EVICT_MARGIN && !g_pendingEvictionSet.count(key)) {
            g_pendingEvictionSet.insert(key);
            g_pendingEvictions.push_back({ cx, cz });
        }
    }
}

void ProcessColumnGeneration(World& w) {
    int n = (int)std::min<size_t>(MAX_COLUMN_GENS_PER_TICK, g_pendingColumns.size());
    for (int i = 0; i < n; i++) {
        auto col = g_pendingColumns.front();
        g_pendingColumns.pop_front();
        g_pendingColumnSet.erase(ColumnKey(col.first, col.second));
        GenerateColumn(w, col.first, col.second);
    }
}

void ProcessColumnEviction(World& w) {
    int n = (int)std::min<size_t>(MAX_COLUMN_EVICTIONS_PER_TICK, g_pendingEvictions.size());
    for (int i = 0; i < n; i++) {
        auto col = g_pendingEvictions.front();
        g_pendingEvictions.pop_front();
        long long key = ColumnKey(col.first, col.second);
        g_pendingEvictionSet.erase(key);
        if (!g_residentColumns.count(key)) continue;
        // The player can walk back into range between enqueue and now --
        // evicting then would punch a hole inside the load radius that
        // nothing refills until the next chunk crossing.
        if (ColumnDistance(col.first, col.second, g_lastPlayerChunkX, g_lastPlayerChunkZ)
                <= g_loadRadius + CHUNK_EVICT_MARGIN) continue;
        if (ColumnHasPendingFalls(col.first, col.second)) {
            g_pendingEvictionSet.insert(key);
            g_pendingEvictions.push_back(col); // retry once the cascade drains
            continue;
        }
        EvictColumnFromWorld(w, col.first, col.second);
        g_residentColumns.erase(key);
    }
}

// =======================================================================
// Section 4.7 - Player physics
// =======================================================================

// BoxIntersectsSolid tests every voxel cell the box's full vertical
// extent overlaps (not just a few discrete height samples) -- a
// complete AABB-vs-voxel-grid overlap rather than sampled points.
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

static bool ColumnResidentAt(float x, float z) {
    return g_residentColumns.count(ColumnKey(FloorDiv16((int)floor(x)), FloorDiv16((int)floor(z)))) != 0;
}

void UpdatePlayerPhysics(World& w, Player& p, float dt, bool fwd, bool back, bool left, bool right, bool jump) {
    // Ground that doesn't exist yet reads as air. Until the player's own
    // column is generated (spawn, a load, a teleport-sized jump) hold
    // them exactly where they are instead of letting them fall into the
    // space the terrain is about to fill.
    if (!ColumnResidentAt(p.x, p.z)) { p.velY = 0.0f; return; }
    // Never entombed: if the box overlaps solid blocks anyway (terrain
    // that appeared around an edge, a block that fell onto the player),
    // lift them a block per tick until they're standing free.
    if (BoxIntersectsSolid(w, p.x, p.y, p.z)) {
        p.y = floorf(p.y) + 1.0f;
        p.velY = 0.0f;
        p.onGround = false;
        return;
    }

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

    // Don't walk off the edge of generated ground either.
    if (ColumnResidentAt(p.x + mx, p.z) && !BoxIntersectsSolid(w, p.x + mx, p.y, p.z)) p.x += mx;
    if (ColumnResidentAt(p.x, p.z + mz) && !BoxIntersectsSolid(w, p.x, p.y, p.z + mz)) p.z += mz;

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

bool Raycast(World& w, float ox, float oy, float oz, float dx, float dy, float dz, float maxDist,
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
