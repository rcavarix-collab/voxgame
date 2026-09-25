// world.cpp
//
// Implementations for world.h: the Chunk destructor (needs the real
// <d3d11.h> for ->Release(), which world.h itself deliberately avoids
// including), gravity, terrain generation/column loading, player
// physics, and the DDA raycast.

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX // see render.cpp for why this precedes windows.h (pulled in transitively via d3d11.h here)
#endif
#include "world.h"
#include "pulse.h"
#include "shapes.h"
#include <d3d11.h>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cstring>
#include <queue>
#include <windows.h> // QueryPerformanceCounter for new-world seeds

Chunk::~Chunk() {
    if (vb) vb->Release();
    if (ib) ib->Release();
}

int g_loadRadius = 3; // chunks, horizontal only (Section 2.4); a Graphics Settings slider now [1,8]

World g_world;
Player g_player;
float g_dayTimeSeconds = 0.0f;

// =======================================================================
// Part V - Scheduled block updates
// =======================================================================

uint32_t g_worldTick = 0;

namespace {
struct LaterFirst {
    bool operator()(const ScheduledUpdate& a, const ScheduledUpdate& b) const {
        if (a.due != b.due) return (int32_t)(a.due - b.due) > 0; // wrap-safe
        return (int32_t)(a.seq - b.seq) > 0;
    }
};
std::priority_queue<ScheduledUpdate, std::vector<ScheduledUpdate>, LaterFirst> g_updates;
uint32_t g_updateSeq = 0;
// Pending updates per column, kept in step with the queue so eviction can
// ask "is something still changing here?" without scanning it.
std::unordered_map<long long, int> g_updatesPerColumn;

void ForgetColumnUpdate(int x, int z) {
    auto it = g_updatesPerColumn.find(ColumnKey(FloorDiv16(x), FloorDiv16(z)));
    if (it != g_updatesPerColumn.end() && --it->second <= 0) g_updatesPerColumn.erase(it);
}

// ---- Handlers, one per UpdateKind ----

// Gravity: fall one cell if still unsupported, then re-check what was
// resting on top and whether this block keeps falling -- both a tick
// later, so a column of blocks comes down one cell per tick, visibly.
void UpdateGravity(World& w, int x, int y, int z) {
    BlockID id = w.Get(x, y, z);
    if (id == BLOCK_AIR || g_blocks[id].foundational) return; // stale entry
    if (y - 1 < Y_MIN) return;
    if (w.Solid(x, y - 1, z)) return; // became supported since queued

    // The state byte travels with the block. (A per-block data record
    // wouldn't -- every block that has one is foundational today, so none
    // can fall; a falling data block would need its record moved.)
    w.SetRaw(x, y - 1, z, id, w.GetState(x, y, z));
    w.SetRaw(x, y, z, BLOCK_AIR);

    MaybeQueueFall(w, x, y + 1, z, 1); // whatever was resting on top
    MaybeQueueFall(w, x, y - 1, z, 1); // keep falling if still unsupported
}

// Grass cover: grass that's been cut off from the sky since its check was
// queued turns to dirt. Uncovered in the meantime, it lives.
void UpdateGrassCover(World& w, int x, int y, int z) {
    if (w.Get(x, y, z) != BLOCK_MEADOW_GRASS) return; // stale entry
    if (OpenToSky(w, x, y, z)) return;
    w.SetRaw(x, y, z, BLOCK_DIRT);
}

using UpdateHandler = void (*)(World&, int, int, int);
const UpdateHandler kHandlers[UPD_KIND_COUNT] = {
    UpdateGravity,
    UpdateGrassCover,
};
} // namespace

void ScheduleUpdate(int x, int y, int z, UpdateKind kind, uint32_t delayTicks) {
    g_updates.push({ x, y, z, g_worldTick + delayTicks, g_updateSeq++, kind });
    g_updatesPerColumn[ColumnKey(FloorDiv16(x), FloorDiv16(z))]++;
}

void ProcessScheduledUpdates(World& w) {
    int n = 0;
    while (!g_updates.empty() && n < MAX_UPDATES_PER_TICK) {
        ScheduledUpdate u = g_updates.top();
        if ((int32_t)(u.due - g_worldTick) > 0) break; // nothing else is due yet
        g_updates.pop();
        ForgetColumnUpdate(u.x, u.z);
        if (u.kind < UPD_KIND_COUNT) kHandlers[u.kind](w, u.x, u.y, u.z);
        n++;
    }
    g_worldTick++;
}

void ClearScheduledUpdates() {
    g_updates = decltype(g_updates)();
    g_updatesPerColumn.clear();
    g_worldTick = 0;
    g_updateSeq = 0;
}

size_t ScheduledUpdateCount() { return g_updates.size(); }

std::vector<PendingUpdate> SnapshotScheduledUpdates() {
    std::vector<PendingUpdate> out;
    auto copy = g_updates; // small in practice: only what's changing right now
    while (!copy.empty()) {
        const ScheduledUpdate& u = copy.top();
        int32_t d = (int32_t)(u.due - g_worldTick);
        out.push_back({ u.x, u.y, u.z, u.kind, (uint32_t)(d > 0 ? d : 0) });
        copy.pop();
    }
    return out;
}

void RestoreScheduledUpdates(const std::vector<PendingUpdate>& updates) {
    for (const PendingUpdate& u : updates)
        if (u.kind < UPD_KIND_COUNT) ScheduleUpdate(u.x, u.y, u.z, u.kind, u.delay);
}

void MaybeQueueFall(World& w, int x, int y, int z, uint32_t delayTicks) {
    if (y < Y_MIN || y > Y_MAX) return;
    BlockID id = w.Get(x, y, z);
    if (id == BLOCK_AIR) return;
    if (g_blocks[id].foundational) return;
    if (!g_blocks[id].solid) return;
    if (y - 1 < Y_MIN) return;         // resting on the world floor
    if (w.Solid(x, y - 1, z)) return;  // supported
    ScheduleUpdate(x, y, z, UPD_GRAVITY, delayTicks);
}

bool OpenToSky(World& w, int x, int y, int z) {
    for (int yy = y + 1; yy <= Y_MAX && yy <= y + 64; yy++)
        if (BlockShadesGrass(w.Get(x, yy, z))) return false;
    return true;
}

void LiveEdit(World& w, int x, int y, int z, BlockID id, uint8_t state) {
    w.Set(x, y, z, id, state);
    MaybeQueueFall(w, x, y + 1, z);
    // Placing something that keeps the sky off: the first grass below it
    // (anything solid in between means it was shaded already) starts to
    // die back -- checked minutes from now, each cell a little differently.
    if (BlockShadesGrass(id)) {
        for (int yy = y - 1; yy >= Y_MIN && yy >= y - 64; yy--) {
            BlockID below = w.Get(x, yy, z);
            if (below == BLOCK_AIR || !g_blocks[below].solid) continue; // air, plants
            if (below == BLOCK_MEADOW_GRASS) {
                uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)yy * 19349663u ^ (uint32_t)z * 83492791u;
                ScheduleUpdate(x, yy, z, UPD_GRASS_COVER, GRASS_COVER_TICKS + (h % (GRASS_COVER_TICKS / 2)));
            }
            break;
        }
    }
}

// =======================================================================
// World generation and chunk loading
// =======================================================================

// Generators (Section 2.5). Each (type, version) must keep producing
// exactly the same terrain forever, because saves store only what the
// player changed and regenerate the rest -- so an output-changing edit
// to a generator means a new version alongside the old one, not an
// in-place change.
WorldGenParams g_worldGen;

const char* WorldGenName(WorldGenType t) {
    switch (t) {
    case GEN_HILLS: return "hills";
    case GEN_FLAT: return "flat";
    default: return "?";
    }
}
bool WorldGenFromName(const char* name, WorldGenType& out) {
    for (int t = 0; t < GEN_TYPE_COUNT; t++)
        if (strcmp(name, WorldGenName((WorldGenType)t)) == 0) { out = (WorldGenType)t; return true; }
    return false;
}
uint32_t WorldGenLatestVersion(WorldGenType t) {
    switch (t) {
    case GEN_HILLS: return 1;
    case GEN_FLAT: return 2; // v2: the surface is a patchwork of three grounds (SurfaceBlockAt)
    default: return 0;
    }
}

// TEMPORARY, for testing: new worlds are dead flat (surface at y = 12,
// every column a single chunk tall). Switch back to GEN_HILLS when
// testing is done -- existing worlds keep the generator they were made
// with either way, since it's stored in each save.
WorldGenParams DefaultNewWorldGen() {
    WorldGenParams p;
    p.type = GEN_FLAT;
    p.version = WorldGenLatestVersion(p.type);
    // Nothing reads the seed yet (both generators are seedless), but
    // every world gets one now so a seeded generator needs no format
    // change. Mixed from the clock so worlds differ.
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    uint64_t x = (uint64_t)t.QuadPart * 0x9E3779B97F4A7C15ull;
    x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29;
    p.seed = x;
    return p;
}

static const int FLAT_V1_HEIGHT = 12;

int TerrainHeight(int wx, int wz) {
    if (g_worldGen.type == GEN_FLAT) return FLAT_V1_HEIGHT;
    // hills v1 -- the original terrain (every pre-v5 save was made with it).
    double h = 40.0 + 6.0 * sin(wx * 0.15) + 4.0 * cos(wz * 0.13);
    int ih = (int)h;
    if (ih < 20) ih = 20;
    if (ih > 60) ih = 60;
    return ih;
}

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

static const int COLUMN_CHUNKS = Y_MAX / CHUNK_SIZE + 1;

bool ColumnNeighborhoodResident(int cx, int cz) {
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
            if (!g_residentColumns.count(ColumnKey(cx + dx, cz + dz))) return false;
    return true;
}

// A column arriving changes what the meshes of every chunk in the 3x3
// columns around it should be (face culling across shared faces,
// ambient occlusion across edges and corners) -- and may be the last
// neighbour a waiting chunk needed before it can be meshed at all.
static void MarkColumnNeighborhoodDirty(World& w, int cx, int cz) {
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
            for (int cy = 0; cy < COLUMN_CHUNKS; cy++)
                w.MarkChunkDirty({ cx + dx, cy, cz + dz });
}

// Evicting frees unmodified chunks outright (the generator rebuilds them
// bit-for-bit on return) and keeps only modified ones, minus their GPU
// buffers, in g_evictedChunks -- so memory held for places the player
// has left scales with what they changed there, not with distance
// walked.
static void EvictColumnFromWorld(World& w, int cx, int cz) {
    for (int cy = 0; cy < COLUMN_CHUNKS; cy++) {
        ChunkCoord cc{ cx, cy, cz };
        std::unique_ptr<Chunk> c = w.TakeChunk(cc);
        if (!c || !c->modified) continue; // unmodified: destroyed here
        if (c->vb) { c->vb->Release(); c->vb = nullptr; }
        if (c->ib) { c->ib->Release(); c->ib = nullptr; }
        c->indexCount = 0; c->opaqueIndexCount = 0;
        c->dirty = true;
        g_evictedChunks[cc] = std::move(c);
    }
}

// A column with updates still pending isn't evicted: gravity only ever
// moves a block straight down within its own column, and evicting it
// mid-cascade would make every remaining entry read air and be dropped
// as stale, leaving the rest of the structure floating on return.
// (Long-delay updates, e.g. machine timers, will want to travel with
// their chunk instead -- see DESIGN.md 5.4.)
static bool ColumnHasPendingUpdates(int cx, int cz) {
    return g_updatesPerColumn.count(ColumnKey(cx, cz)) != 0;
}

static inline BlockID TerrainBlockAt(int wy, int surface) {
    if (wy == 0) return BLOCK_FOUNDATION;
    if (wy >= surface - 2) return BLOCK_DIRT;
    return BLOCK_STONE;
}

// Flat v2's ground (DESIGN.md 2.5): the plain's top layer is a patchwork
// of three materials -- soft grass, crunchy sand, hard pebbles -- in
// fractal blobs: three octaves of seeded value noise (48, 20 and 8 blocks
// across), summed and thresholded, so sand and pebble patches sit apart in
// a sea of grass, with ragged, blobby edges. A few hashes per column, once,
// when the column generates.
static const BlockID kPatchSoft = BLOCK_MEADOW_GRASS, kPatchCrunch = BLOCK_COASTAL_SAND, kPatchHard = BLOCK_RIVER_PEBBLE;
static inline double LatticeValue(int64_t x, int64_t z, uint64_t seed) {
    uint64_t h = seed ^ ((uint64_t)x * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)z * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}
static double ValueNoise(double x, double z, uint64_t seed) {
    double fx = floor(x), fz = floor(z);
    int64_t ix = (int64_t)fx, iz = (int64_t)fz;
    double u = x - fx, v = z - fz;
    u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
    double a = LatticeValue(ix, iz, seed), b = LatticeValue(ix + 1, iz, seed);
    double c = LatticeValue(ix, iz + 1, seed), d = LatticeValue(ix + 1, iz + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
BlockID SurfaceBlockAt(int wx, int wz) {
    const uint64_t s = g_worldGen.seed;
    double n = 0.55 * ValueNoise(wx / 48.0, wz / 48.0, s)
             + 0.30 * ValueNoise(wx / 20.0, wz / 20.0, s ^ 0x5bd1e995ull)
             + 0.15 * ValueNoise(wx / 8.0, wz / 8.0, s ^ 0x27d4eb2full);
    if (n < 0.34) return kPatchCrunch;
    if (n > 0.64) return kPatchHard;
    return kPatchSoft;
}

// Fills a column's terrain straight into fresh chunk arrays -- no
// per-block World::Set (and its per-block hash lookups and dirty
// marking), since nothing else can be in these chunks yet.
static void GenerateColumnTerrain(World& w, int cx, int cz) {
    int baseX = cx * CHUNK_SIZE, baseZ = cz * CHUNK_SIZE;

    // Cached per column and reused for every vertical chunk level
    // instead of re-running TerrainHeight once per level.
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    BlockID surface[CHUNK_SIZE][CHUNK_SIZE];
    const bool patchwork = g_worldGen.type == GEN_FLAT && g_worldGen.version >= 2;
    int maxHeightInColumn = 0;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int h = TerrainHeight(baseX + lx, baseZ + lz);
            heights[lx][lz] = h;
            surface[lx][lz] = patchwork ? SurfaceBlockAt(baseX + lx, baseZ + lz) : BLOCK_DIRT;
            if (h > maxHeightInColumn) maxHeightInColumn = h;
        }
    }

    int maxCy = FloorDiv16(std::min(maxHeightInColumn, Y_MAX));
    for (int cy = 0; cy <= maxCy; cy++) {
        int chunkYLow = cy * CHUNK_SIZE;
        // Chunks only exist when they hold real content (Section 2.1).
        bool anyContent = false;
        for (int lx = 0; lx < CHUNK_SIZE && !anyContent; lx++)
            for (int lz = 0; lz < CHUNK_SIZE && !anyContent; lz++)
                if (chunkYLow <= heights[lx][lz]) anyContent = true;
        if (!anyContent) continue;

        Chunk* c = w.GetOrCreateChunk({ cx, cy, cz });
        for (int ly = 0; ly < CHUNK_SIZE; ly++) {
            int wy = chunkYLow + ly;
            for (int lz = 0; lz < CHUNK_SIZE; lz++)
                for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                    int h = heights[lx][lz];
                    if (wy > h) continue;
                    BlockID b = TerrainBlockAt(wy, h);
                    if (wy == h && patchwork) b = surface[lx][lz]; // the top layer: the patchwork
                    c->blocks[Chunk::LocalIndex(lx, ly, lz)] = (uint8_t)b;
                }
        }
    }
}

void GenerateColumn(World& w, int cx, int cz) {
    long long key = ColumnKey(cx, cz);
    if (g_residentColumns.count(key)) return; // already resident -- nothing to do
    g_residentColumns.insert(key);

    GenerateColumnTerrain(w, cx, cz);
    // Whatever the player changed here (kept from an eviction, or read
    // from the save) replaces the generated chunk wholesale.
    for (int cy = 0; cy < COLUMN_CHUNKS; cy++) {
        auto it = g_evictedChunks.find({ cx, cy, cz });
        if (it == g_evictedChunks.end()) continue;
        g_pulse.OnChunkArrived(it->first, *it->second); // its harvesters gather again (Part VI)
        w.AdoptChunk(it->first, std::move(it->second));
        g_evictedChunks.erase(it);
    }
    MarkColumnNeighborhoodDirty(w, cx, cz);
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
    // Generation runs one ring past the view radius: a chunk is only
    // meshed once all 8 neighbouring columns exist (face culling and AO
    // read across them -- see RebuildDirtyChunks), so this extra ring is
    // what lets the outermost visible ring be meshed.
    int genRadius = g_loadRadius + 1;
    for (int ring = 0; ring <= genRadius; ring++) {
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
        if (dist > genRadius + CHUNK_EVICT_MARGIN && !g_pendingEvictionSet.count(key)) {
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
        // Queued back when the player was elsewhere and they've since
        // moved on: don't generate a column only to evict it again.
        if (ColumnDistance(col.first, col.second, g_lastPlayerChunkX, g_lastPlayerChunkZ) > g_loadRadius + 1) continue;
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
                <= g_loadRadius + 1 + CHUNK_EVICT_MARGIN) continue;
        if (ColumnHasPendingUpdates(col.first, col.second)) {
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
static bool BoxIntersectsSolid(World& w, float cx, float cy, float cz, float height = PLAYER_HEIGHT) {
    float ax0 = cx - PLAYER_HALFW, ax1 = cx + PLAYER_HALFW;
    float ay0 = cy,                ay1 = cy + height;
    float az0 = cz - PLAYER_HALFW, az1 = cz + PLAYER_HALFW;
    int minX = (int)floor(ax0), maxX = (int)floor(ax1);
    int minY = (int)floor(ay0), maxY = (int)floor(ay1);
    int minZ = (int)floor(az0), maxZ = (int)floor(az1);
    for (int x = minX; x <= maxX; x++)
        for (int y = minY; y <= maxY; y++)
            for (int z = minZ; z <= maxZ; z++) {
                BlockID id = w.Get(x, y, z);
                if (!BlockSolid(id)) continue;
                if (g_blocks[id].shape == SHAPE_CUBE) return true;
                // Shaped block: only its own collision boxes count, so a
                // slab is half height and a tube is only as thick as it
                // looks. Touching (sharing a surface) isn't overlapping,
                // which is what lets the player stand on top of one.
                ShapeBox boxes[MAX_SHAPE_BOXES];
                int n = ShapeBoxes(g_blocks[id].shape, w.GetState(x, y, z), boxes);
                const float k = 1.0f / SHAPE_UNITS;
                for (int i = 0; i < n; i++) {
                    const ShapeBox& b = boxes[i];
                    if (ax0 < x + b.x1 * k && ax1 > x + b.x0 * k &&
                        ay0 < y + b.y1 * k && ay1 > y + b.y0 * k &&
                        az0 < z + b.z1 * k && az1 > z + b.z0 * k) return true;
                }
            }
    return false;
}

static bool ColumnResidentAt(float x, float z) {
    return g_residentColumns.count(ColumnKey(FloorDiv16((int)floor(x)), FloorDiv16((int)floor(z)))) != 0;
}

// Movement speeds and the power slide, blocks per second (Section 4.7).
static const float WALK_SPEED = 4.5f;
static const float SPRINT_SPEED = 7.5f;
static const float CROUCH_SPEED = 1.8f;
static const float SLIDE_START_SPEED = 10.0f; // the burst when a sprint drops into a slide
static const float SLIDE_FRICTION = 1.5f;     // per second: speed falls as e^(-friction * t) on the ground
static const float SLIDE_MAX_SECONDS = 1.4f;
// The slide forgives timing: crouch and sprint may come in either order,
// up to this far apart (a sprint just let go of still counts, and a crouch
// pressed a moment early -- or just before landing -- waits for it).
static const float SLIDE_SPRINT_GRACE = 0.4f; // seconds since sprinting
static const float SLIDE_PRESS_GRACE = 0.3f;  // seconds a crouch press stays fresh
static const float SLIDE_LEAN_ROLL = 0.21f;   // radians (~12 degrees) of lean at full sideways slide
static const float SLIDE_LEAN_PITCH = 0.08f;  // radians of dip at full forward slide

void UpdatePlayerPhysics(World& w, Player& p, float dt, const MoveInput& in) {
    // Ground that doesn't exist yet reads as air. Until the player's own
    // column is generated (spawn, a load, a teleport-sized jump) hold
    // them exactly where they are instead of letting them fall into the
    // space the terrain is about to fill.
    if (!ColumnResidentAt(p.x, p.z)) { p.velY = 0.0f; return; }
    // Safety net: however the player got below the world (it shouldn't be
    // possible -- the floor can't be broken), put them back on the highest
    // solid block of their column rather than falling forever.
    if (p.y < Y_MIN - 32.0f) {
        int bx = (int)floor(p.x), bz = (int)floor(p.z), top = Y_MIN;
        for (int y = Y_MAX; y >= Y_MIN; y--) if (w.Solid(bx, y, bz)) { top = y + 1; break; }
        p.y = (float)top;
        p.velY = 0.0f;
        return;
    }
    // No room to stand (a load or a block placed in a crawlspace) but room
    // to crouch: crouch, rather than being pushed up out of it.
    if (!p.crouching && BoxIntersectsSolid(w, p.x, p.y, p.z, PLAYER_HEIGHT) && !BoxIntersectsSolid(w, p.x, p.y, p.z, PLAYER_CROUCH_HEIGHT))
        p.crouching = true;
    // Never entombed: if the box overlaps solid blocks anyway (terrain
    // that appeared around an edge, a block that fell onto the player),
    // lift them a block per tick until they're standing free.
    if (BoxIntersectsSolid(w, p.x, p.y, p.z, PlayerHeight(p))) {
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

    float mx = 0, mz = 0;
    if (in.fwd)  { mx += fx; mz += fz; }
    if (in.back) { mx -= fx; mz -= fz; }
    if (in.right){ mx += rx; mz += rz; }
    if (in.left) { mx -= rx; mz -= rz; }
    float mlen = sqrtf(mx * mx + mz * mz);
    if (mlen > 0.0001f) { mx /= mlen; mz /= mlen; } // unit direction (or zero)

    // Crouch, sprint and the power slide. A fresh crouch press while
    // sprinting on the ground drops into a slide: a burst of speed along
    // the way the player was running that bleeds off over about a second,
    // at crouch height (so a slide goes under a 1-block gap). Otherwise
    // holding crouch crouches; letting go stands up only where there's
    // room to.
    bool crouchPressed = in.crouch && !p.crouchHeld;
    p.crouchHeld = in.crouch;
    p.crouchBuffer = crouchPressed ? SLIDE_PRESS_GRACE : std::max(0.0f, p.crouchBuffer - dt);
    // Sprinting, or trying to (sprint + forward with crouch already held).
    bool sprintIntent = in.sprint && in.fwd && !in.back;
    p.sinceSprint = (sprintIntent || p.sprinting) ? 0.0f : p.sinceSprint + dt;
    if (p.crouchBuffer > 0.0f && in.crouch && p.onGround && p.sinceSprint <= SLIDE_SPRINT_GRACE &&
        !PlayerSliding(p) && mlen > 0.0001f) {
        p.slideTime = 1e-4f;
        p.slideVX = mx * SLIDE_START_SPEED; p.slideVZ = mz * SLIDE_START_SPEED;
        p.crouching = true;
        p.crouchBuffer = 0.0f; // one press, one slide
    }
    if (PlayerSliding(p)) {
        float speed = sqrtf(p.slideVX * p.slideVX + p.slideVZ * p.slideVZ);
        if (in.jump || speed < CROUCH_SPEED || p.slideTime > SLIDE_MAX_SECONDS) p.slideTime = 0.0f; // over (a jump ends it)
    }
    bool sliding = PlayerSliding(p);
    if (in.crouch || sliding) p.crouching = true;
    else if (p.crouching && !BoxIntersectsSolid(w, p.x, p.y, p.z, PLAYER_HEIGHT)) p.crouching = false;
    p.sprinting = in.sprint && in.fwd && !in.back && !p.crouching;

    // Horizontal moves, one axis at a time. Don't walk off the edge of
    // generated ground; and when blocked while standing, step up onto
    // anything up to half a block high (slabs, ramps, pyramid bases) --
    // but never a full block. Returns whether the move happened.
    const float STEP = 0.5f;
    const float h = PlayerHeight(p);
    auto tryMove = [&](float dx, float dz) {
        if (dx == 0.0f && dz == 0.0f) return true;
        if (!ColumnResidentAt(p.x + dx, p.z + dz)) return false;
        if (!BoxIntersectsSolid(w, p.x + dx, p.y, p.z + dz, h)) { p.x += dx; p.z += dz; return true; }
        if (p.onGround && !BoxIntersectsSolid(w, p.x, p.y + STEP, p.z, h) &&
            !BoxIntersectsSolid(w, p.x + dx, p.y + STEP, p.z + dz, h)) {
            p.x += dx; p.z += dz; p.y += STEP;
            p.eyeHeight -= STEP; // the view doesn't jump: it glides up with the eye's easing (~0.25 s)
            return true;
        }
        return false;
    };
    if (sliding) {
        // Momentum, not input: friction on the ground, none in the air;
        // a wall stops that axis.
        if (p.onGround) {
            float k = expf(-SLIDE_FRICTION * dt);
            p.slideVX *= k; p.slideVZ *= k;
        }
        if (!tryMove(p.slideVX * dt, 0.0f)) p.slideVX = 0.0f;
        if (!tryMove(0.0f, p.slideVZ * dt)) p.slideVZ = 0.0f;
        p.slideTime += dt;
    } else {
        float speed = p.crouching ? CROUCH_SPEED : (p.sprinting ? SPRINT_SPEED : WALK_SPEED);
        tryMove(mx * speed * dt, 0.0f);
        tryMove(0.0f, mz * speed * dt);
    }

    const float GRAVITY = 20.0f;
    const float JUMP_SPEED = 7.0f;
    if (p.onGround && in.jump) { p.velY = JUMP_SPEED; p.onGround = false; }
    p.velY -= GRAVITY * dt;
    if (p.velY < -50.0f) p.velY = -50.0f;

    float dy = p.velY * dt;
    if (!BoxIntersectsSolid(w, p.x, p.y + dy, p.z, h)) {
        p.y += dy;
        p.onGround = false;
    } else {
        if (p.velY < 0) p.onGround = true;
        p.velY = 0;
    }

    // Camera: the eye drops when crouching and further in a slide, and a
    // slide leans the view into the way it's carrying the player relative
    // to where they look -- sideways rolls toward that side, straight
    // ahead dips forward, backwards tips back -- scaled by its speed.
    float eyeTarget = sliding ? PLAYER_SLIDE_EYE : (p.crouching ? PLAYER_CROUCH_EYE : PLAYER_EYE);
    float rollTarget = 0.0f, pitchTarget = 0.0f;
    if (sliding) {
        float speed = sqrtf(p.slideVX * p.slideVX + p.slideVZ * p.slideVZ);
        if (speed > 1e-3f) {
            float amount = speed / SLIDE_START_SPEED; amount = amount > 1.0f ? 1.0f : amount;
            float sx = p.slideVX / speed, sz = p.slideVZ / speed;
            rollTarget = (sx * rx + sz * rz) * SLIDE_LEAN_ROLL * amount;
            pitchTarget = -(sx * fx + sz * fz) * SLIDE_LEAN_PITCH * amount;
        }
    }
    float ease = 1.0f - expf(-dt / 0.08f);
    p.eyeHeight += (eyeTarget - p.eyeHeight) * ease;
    p.roll += (rollTarget - p.roll) * ease;
    p.leanPitch += (pitchTarget - p.leanPitch) * ease;
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

        // Past the starting cell, walk-through blocks (plants) are hits
        // too; the cell the eye is in only counts if it's solid, so
        // standing in grass doesn't make it the target of every click.
        if (w.Pickable(voxX, voxY, voxZ)) {
            hitX = voxX; hitY = voxY; hitZ = voxZ;
            placeX = prevX; placeY = prevY; placeZ = prevZ;
            return true;
        }
    }
    return false;
}
