// essence.h
//
// The essence network (DESIGN.md Part XIX): sources and sinks of essence
// and the routes between them, as the data the network map draws. Two
// kinds of node exist so far -- natural convergence zones, placed
// deterministically from the world seed, and player-built attractors --
// and routes between them are derived from positions and magnitudes.
//
// PROVISIONAL: the real essence simulation doesn't exist yet. Everything
// the map needs goes through EssenceNetwork, so when the simulation
// arrives it replaces BuildRoutes / the zone generator and fills the same
// structure; the map itself doesn't change.
//
// Pure C++, deterministic, tested natively.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---- The shared intensity scale -----------------------------------------
// Essence speaks in orders of magnitude everywhere (The Line's falloff,
// node sizes, route tiers): one band per decade. Band 0 is below 10.
static inline int EssenceBand(double magnitude) {
    if (!(magnitude >= 10.0)) return 0;
    int b = 0;
    while (magnitude >= 10.0 && b < 12) { magnitude /= 10.0; b++; }
    return b;
}
// The words the map uses for bands -- qualitative, never numbers.
const char* EssenceBandWord(int band);

enum class NodeKind : uint8_t { Convergence, Attractor };

struct EssenceNode {
    uint64_t id;          // stable: zones from (region, index), attractors from block position
    NodeKind kind;
    float x, z;           // real world position (top-down)
    int y;                // block height (attractors); zones sit at the surface
    double magnitude;     // essence strength (arbitrary units; read through bands)
    int64_t parent = -1;  // index of the node this rolls up into for grouping/labels (-1 = top level)
};

enum class RouteStyle : uint8_t {
    Active,        // solid: currently flowing, high throughput
    Intermittent,  // dashed: flowing, low or intermittent
    Planned,       // dotted: possible but not built
};

struct EssenceRoute {
    int a, b;             // node indices
    RouteStyle style;
    int band;             // flow tier (EssenceBand of the flow)
    bool bound;           // entangled: no physical path, drawn curved
};

struct EssenceTuning {
    float regionSize = 128.0f;        // zone placement grid, blocks
    int maxZonesPerRegion = 3;
    float discoverRadius = 40.0f;     // blocks: a node is discovered once the player comes this close
    float attractorReach = 64.0f;     // blocks: an attractor draws from zones this close (active route)
    float plannedReach = 160.0f;      // ... and could, if extended, from zones this close (planned route)
    float naturalRouteReach = 160.0f; // major zones this close exchange essence
    int minorBand = 1;                // nodes at or below this band aggregate into belts on the map
    uint32_t boundOneIn = 20;         // 1 in N pairs of distant major zones are entangled...
    float boundMaxDist = 600.0f;      // ...if no further apart than this
};

class EssenceNetwork {
public:
    EssenceTuning tuning;

    void Reset(uint64_t worldSeed);
    // Generates zones for regions near (x, z) as needed and discovers any
    // node within the discovery radius. Cheap; call once per tick.
    void Update(float x, float z);

    void AddAttractor(int x, int y, int z);
    void RemoveAttractor(int x, int y, int z);

    // Rebuilds routes and hierarchy among discovered nodes if anything
    // changed since the last call (only the map needs them).
    void RefreshRoutes();

    // Discovered nodes only -- the map never sees the rest.
    const std::vector<EssenceNode>& Nodes() const { return m_nodes; }
    const std::vector<EssenceRoute>& Routes() const { return m_routes; }
    uint64_t Version() const { return m_version; }

    // Save support.
    struct SaveData {
        std::vector<uint64_t> discoveredZones;
        std::vector<int32_t> attractors; // x, y, z triples
    };
    SaveData Snapshot() const;
    void Restore(uint64_t worldSeed, const SaveData& d);

    // All zones of one region, discovered or not (tests, and generation).
    std::vector<EssenceNode> ZonesOfRegion(int rx, int rz) const;

private:
    uint64_t m_seed = 0;
    std::unordered_set<long long> m_generatedRegions;
    std::vector<EssenceNode> m_undiscovered;   // generated near the player, not yet seen
    std::vector<EssenceNode> m_nodes;          // discovered
    std::unordered_set<uint64_t> m_discoveredIds;
    std::vector<EssenceRoute> m_routes;
    uint64_t m_version = 1, m_routesVersion = 0;

    void GenerateRegion(int rx, int rz);
    void Discover(const EssenceNode& n);
};

extern EssenceNetwork g_essence;
