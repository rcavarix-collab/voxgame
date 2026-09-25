// fire.h
//
// Weak fire (DESIGN.md §3). Rocket blasts and explosions start it; it
// creeps across flammable ground in 2 m cells -- dry grass readily, meadow
// less, moss and clover reluctantly, soil and rock never -- sets trees and
// bushes in its path burning, and leaves the ground scorched. A burnt cell
// never burns again.
//
// LG2.cpp's fire, re-implemented safely (docs/SEED_REVIEW.md §2): burning
// cells live in a flat vector plus a hash set of burnt cells, nothing holds
// pointers into a container that changes, spread attempts are budgeted per
// tick, and the number of burning cells is capped. Cost scales with the
// fire, never with the world. Pure C++, tested natively.

#pragma once

#include "common.h"
#include "events.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Terrain;
class Props;

struct FireTuning {
    float cell = 2.0f;           // metres
    float spreadPerSecond = 0.06f; // chance per second per neighbour, times flammability: weak, a creep not a sweep
    float burnMin = 6.0f, burnMax = 10.0f; // seconds a cell burns
    int maxBurning = 600;        // governed: the most cells alight at once
    int attemptsPerTick = 80;    // governed: spread checks per tick
};

struct FireCell { int x, z; float age, life; };

class Fire {
public:
    void Reset(uint32_t seed);
    // Light the ground in a disc (a blast). Returns cells lit.
    int IgniteArea(const Terrain& t, Vec3 centre, float radius, EventList& ev);
    void Tick(Terrain& t, Props& props, const FireTuning& tune, float dt, EventList& ev);
    // Forget cached fuel inside a disc (the ground changed there).
    void GroundChanged(Vec3 centre, float radius);
    bool BurningAt(Vec3 p) const;
    const std::vector<FireCell>& Cells() const { return m_cells; }
    int Burnt() const { return (int)m_burnt.size(); }

    static float Flammability(uint8_t ground);

private:
    float FuelAt(const Terrain& t, int cx, int cz);
    bool Light(const Terrain& t, int cx, int cz, const FireTuning& tune);
    float Rand();

    std::vector<FireCell> m_cells;
    std::unordered_set<int64_t> m_lit, m_burnt;   // cells alight, cells used up
    std::unordered_map<int64_t, float> m_fuel;    // flammability, looked up once per cell
    uint64_t m_rng = 1;
    size_t m_cursor = 0;                          // spread attempts go round the burning cells in turn
    FireTuning m_tune;
};
