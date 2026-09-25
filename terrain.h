// terrain.h
//
// The destructible ground (DESIGN.md §2): a density field sampled every
// CELL metres, drawn as flat-shaded facets that follow its true shape
// (surface nets -- one vertex per cell the surface passes through, each
// quad split on its shorter diagonal, vertices slid along the ground by a
// fixed hash for a hand-cut look).
//
// Storage scales with change, not with world size: a chunk nobody has
// touched is "pristine" and stores nothing -- its samples come straight
// from the generator. The first blast that reaches a chunk gives it its
// own samples. Only the surface is ever meshed; what's underneath is known
// (it's the generator, or the stored samples) but not built until a blast
// exposes it, and then only the chunks the blast touched are rebuilt.
//
// Each sample carries a ground type (several grasses, dirts, rock) laid out
// in clumps by the generator; the renderer picks a facet's look from its
// ground type, which way it faces and how deep it lies (grass on top,
// its soil on the sides of a fresh crater), and footsteps pick their sound
// from it. Pure C++, tested natively (tests/tests.cpp).

#pragma once

#include "common.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

static const float CELL = 2.0f;         // metres between samples (owner's pick: the best performer)
static const int CHUNK = 16;            // cells per chunk side: 32 m
static const int CHUNK_Y_MIN = -2;      // chunk rows: -64 m ...
static const int CHUNK_Y_MAX = 1;       // ... to +64 m
static const float DENSITY_SCALE = 32.0f; // stored density = metres * 32, clamped to +-127 (+-4 m)

// Ground types. The low 7 bits of Sample::mat; bit 7 marks scorched ground.
enum Ground : uint8_t {
    GROUND_MEADOW,     // grasses...
    GROUND_DRYGRASS,
    GROUND_MOSS,
    GROUND_CLOVER,
    GROUND_LOAM,       // ...soils (also bare patches on the surface)...
    GROUND_CLAY,
    GROUND_GRAVEL,
    GROUND_ROCK,       // ...and the rock under everything
    GROUND_COUNT
};
static const uint8_t GROUND_SCORCHED = 0x80;
static inline bool GroundIsGrass(uint8_t g) { return (g & 0x7F) <= GROUND_CLOVER; }
// What a grass grows in: the soil a crater's walls show beneath it.
static inline uint8_t GroundSoil(uint8_t g) {
    static const uint8_t soil[GROUND_COUNT] = { GROUND_LOAM, GROUND_CLAY, GROUND_LOAM, GROUND_LOAM, GROUND_LOAM, GROUND_CLAY, GROUND_GRAVEL, GROUND_ROCK };
    return soil[g & 0x7F];
}
const char* GroundName(uint8_t g);

struct Sample { int8_t d; uint8_t mat; }; // d > 0: solid

// 16 bytes. `mat` is the ground type of the facet (taken from the cell's
// solid corner nearest the surface); `ao` is how open the cell is (0 =
// buried in a hole, 255 = open sky), for cheap ambient occlusion.
struct TerrainVertex { float x, y, z; uint8_t mat, ao, pad0, pad1; };

struct ChunkKey {
    int x, y, z;
    bool operator==(const ChunkKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const { return (size_t)Hash3(k.x, k.y, k.z, 77); }
};

struct TerrainChunk {
    std::vector<Sample> samples;   // CHUNK^3 when modified; empty = pristine (the generator's)
    std::vector<TerrainVertex> verts;
    std::vector<uint16_t> indices;
    bool meshed = false;           // verts/indices are current
    bool queued = false;
    uint32_t version = 0;          // bumps on every rebuild (the renderer re-uploads)
    bool Modified() const { return !samples.empty(); }
};

struct TerrainStats { int resident = 0, modified = 0, meshedThisFrame = 0, waiting = 0; long long triangles = 0; };

class Terrain {
public:
    void Reset(uint32_t seed);

    // ---- queries (metres) ----
    Sample SampleAt(int gx, int gy, int gz) const;          // grid point (gx*CELL, gy*CELL, gz*CELL)
    float Density(Vec3 p) const;                            // metres, trilinear; > 0 solid
    bool Solid(Vec3 p) const { return Density(p) > 0.0f; }
    Vec3 Normal(Vec3 p) const;                              // outward (toward air)
    // The ground's top under (x, z), searching down from `fromY` (so a
    // mech in a tunnel finds the tunnel floor, not the hill above).
    // Returns false if there's none within `maxDrop`.
    bool GroundBelow(float x, float z, float fromY, float maxDrop, float& groundY) const;
    bool Raycast(Vec3 origin, Vec3 dir, float maxDist, Vec3& hit) const;
    // Ground type at the surface under a point (footsteps).
    uint8_t GroundAt(Vec3 p) const;
    // Height of the untouched ground: the generator's surface.
    float OriginalHeight(float x, float z) const;

    // ---- change ----
    // Removes a sphere of ground and scorches what's left around it.
    // Returns how much solid volume went (m^3, roughly), for effects and debris.
    float Blast(Vec3 centre, float radius);

    // ---- per frame ----
    // Keeps chunks around `focus` resident (nearest first) and drops
    // pristine ones beyond `radius` + a margin. Modified chunks are kept.
    void Update(Vec3 focus, float radius);
    // Rebuilds at most `budget` queued meshes, nearest first.
    int BuildMeshes(int budget);

    const std::unordered_map<ChunkKey, TerrainChunk, ChunkKeyHash>& Chunks() const { return m_chunks; }
    TerrainStats Stats() const;
    static Vec3 ChunkMin(const ChunkKey& k) { return { k.x * CHUNK * CELL, k.y * CHUNK * CELL, k.z * CHUNK * CELL }; }

    // The mesher, exposed for tests: builds `c`'s surface from samples.
    void Mesh(const ChunkKey& k, TerrainChunk& c) const;

private:
    Sample Generate(int gx, int gy, int gz) const;
    bool ChunkHasSurface(const ChunkKey& k) const;
    void Queue(const ChunkKey& k);

    uint32_t m_seed = 1;
    float m_groundY = 1.0f;
    std::unordered_map<ChunkKey, TerrainChunk, ChunkKeyHash> m_chunks;
    std::vector<ChunkKey> m_queue;
    Vec3 m_focus = { 0, 0, 0 };
    mutable int m_meshedThisFrame = 0;
};
