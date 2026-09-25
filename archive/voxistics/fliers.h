// fliers.h
//
// Fliers (DESIGN.md Part XXI): a few small two-winged creatures -- part
// butterfly, part dragonfly -- that wander low over the surface near the
// player. Each lives one game day, but ages by the local time rate, so
// The Line (where time runs up to 30x) cuts a flier's life short within a
// couple of minutes; they fly low enough to be caught by it. They drop
// nothing; their death feeds the ground: on grass, the patch glows in vivid
// colour for a few minutes (the grass taking in the fallen nutrients, drawn
// as a handful of per-frame shader constants); on bare dirt or wood, mold
// grows there.
//
// Transient (not saved): a load or new world starts a fresh population,
// staggered in age. A handful of fliers and spots: cost is negligible and
// independent of the world. Pure C++, tested natively.

#pragma once

#include "world.h"
#include <cstdint>
#include <vector>

struct FlierTuning {
    int population = 5;             // kept around the player
    float lifeSeconds = 3600.0f;    // one game day, at normal time
    float speed = 2.4f;             // blocks per second
    float minHeight = 0.8f;         // above the ground under them...
    float maxHeight = 2.6f;         // ...low enough for The Line to catch them
    float spawnNear = 18.0f, spawnFar = 36.0f; // blocks from the player they appear at
    float leaveDistance = 64.0f;    // wander further than this and they're gone (a new one appears)
    float spotRadius = 2.5f;        // the glowing patch a death leaves on grass
    float spotSeconds = 240.0f;     // how long it glows (fading out over the last part)
};

struct Flier {
    float x = 0, y = 0, z = 0;
    float heading = 0;       // radians, from +X toward +Z
    float turn = 0;          // current turning rate (wanders smoothly)
    float age = 0;           // life seconds used (at the local time rate)
    float hue = 0;           // 0..1: each flier's own wing colour
    float flapPhase = 0;
    float bobPhase = 0;
    float cruise = 1.6f;     // the height above ground it likes
};

// Where a flier fell on grass: a glowing patch that fades.
struct NutrientSpot {
    float x = 0, y = 0, z = 0; // on top of the ground block
    float hue = 0;
    float age = 0;
};

class FlierSystem {
public:
    void Reset(uint64_t seed);
    // `timeRate(x, y, z)`: how fast time runs there (The Line); null = 1.
    typedef float (*TimeRateFn)(float x, float y, float z);
    void Tick(World& w, const FlierTuning& t, float px, float py, float pz, float dt, TimeRateFn timeRate = nullptr);

    const std::vector<Flier>& Fliers() const { return m_fliers; }
    const std::vector<NutrientSpot>& Spots() const { return m_spots; }
    // How strongly a spot glows now, 0..1 (fades in quickly, out slowly).
    static float SpotStrength(const NutrientSpot& s, const FlierTuning& t);
    // Totals since the last reset, for the debug readout and tests.
    int deaths = 0, deathsByLine = 0, molds = 0;

private:
    float Rand();                          // 0..1
    bool Ground(World& w, float x, float y, float z, int& gy, BlockID& top) const;
    void Spawn(World& w, const FlierTuning& t, float px, float py, float pz, bool anyAge);
    void Die(World& w, const FlierTuning& t, const Flier& f, bool byLine);

    std::vector<Flier> m_fliers;
    std::vector<NutrientSpot> m_spots;
    uint64_t m_rng = 1;
};

extern FlierSystem g_fliers;
extern FlierTuning g_flierTuning;
