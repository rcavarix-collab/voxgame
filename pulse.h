// pulse.h
//
// Pulse logistics (DESIGN.md Part VI). A harvester gathers pulse evenly
// with the music -- one on every beat -- and pushes it into the pipes
// beside it. Pipes join whatever is next to them, from any face, and a
// run of them is one network. A pulse goes to one of the network's
// outlets in turn: a store (or a chest or machine) with room, or an open
// end. From an open end it flies out straight -- pulse isn't wholly
// bound by the world's physics, which is why it obeys machines -- until
// it meets a surface (and is gone) or the mouth of another pipe facing
// it (which catches it). Pulses aren't really there: they're data, drawn
// passing through the gap, so they cross each other freely.
//
// Counts live in the blocks' own data records, so they're saved with the
// world; what's in transit is not (a load starts with empty pipes).
// Networks are found by flood fill and kept until the world changes.
// Cost scales with pulses in flight and networks in use, never with the
// size of the world. Pure C++, tested natively.

#pragma once

#include "world.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct PulseTuning {
    float pipeSpeed = 6.0f;      // blocks per second along a pipe
    float flySpeed = 12.0f;      // blocks per second out of an open end
    float flyRange = 64.0f;      // blocks an uncaught pulse flies before it fades away
    int maxInFlight = 2048;      // everything in pipes and in the air, all told
    int maxNetworkCells = 4096;  // the most pipe one network may span
    int maxPerBeat = 4;          // a clock jump (debug time, a load) doesn't unleash a flood
};

// How much pulse a block holds (0 = it holds none: not a place for pulse).
int PulseCapacity(BlockID id);
// Stored in the block's data record (created on first use).
int PulseStored(World& w, int x, int y, int z);
void SetPulseStored(World& w, int x, int y, int z, int count);

struct PulseCell {
    int x = 0, y = 0, z = 0;
    bool operator==(const PulseCell& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct PulseCellHash {
    size_t operator()(const PulseCell& c) const {
        return (size_t)(uint32_t)c.x * 73856093u ^ (size_t)(uint32_t)c.y * 19349663u ^ (size_t)(uint32_t)c.z * 83492791u;
    }
};

// One pulse on screen: where, and how visible (fading in and out).
struct PulseView { float x, y, z, alpha; };

class PulseSystem {
public:
    void Reset();
    // A block was placed or broken (only harvesters need telling: the
    // networks notice any change on their own).
    void OnPlaced(int x, int y, int z, BlockID id);
    // A chunk came back into the world (from a save or the modified-chunk
    // store): any harvesters in it start gathering again.
    void OnChunkArrived(const ChunkCoord& cc, const Chunk& c);
    // `beats`: the music's beat count (continuous); a harvester gathers
    // one pulse per whole beat.
    void Tick(World& w, const PulseTuning& t, double beats, float dt);

    void Views(std::vector<PulseView>& out) const;
    int InFlight() const { return (int)m_moving.size(); }
    int Harvesters() const { return (int)m_harvesters.size(); }
    // Totals since the last reset, for the debug readout and tests.
    long long gathered = 0, delivered = 0, lost = 0, caught = 0;

private:
    struct Outlet {
        PulseCell pipe;   // the pipe it leaves from
        PulseCell target; // the store beside it, or the air beyond an open end
        int face = 0;     // from pipe to target
        bool mouth = false;
    };
    struct Network {
        std::vector<PulseCell> pipes;
        std::vector<Outlet> outlets;
        size_t next = 0;  // outlets take turns
    };
    struct Moving {
        std::vector<PulseCell> path; // cell centres to pass through
        float along = 0;             // cells travelled along the path
        int pipesFrom = 0, pipesTo = 0; // path[pipesFrom..pipesTo] must stay pipe
        bool toMouth = false;        // on arriving: fly out of path.back() through `face`
        int face = 0;
        PulseCell target;            // a store's cell, when not to a mouth
        bool reserved = false;       // counted in m_reserved against `target`
        bool flying = false;
        float px = 0, py = 0, pz = 0, flown = 0;
        PulseCell cell;              // flying: the cell it's in
        float age = 0;
    };

    int NetworkAt(World& w, const PulseTuning& t, const PulseCell& pipe);
    bool Route(World& w, const PulseTuning& t, const PulseCell& from, const PulseCell* source, int skipMouthFace, Moving& out);
    void Fly(Moving& m, const PulseCell& from, int face);
    void Unreserve(Moving& m);
    bool StepFlying(World& w, const PulseTuning& t, Moving& m, float dt);
    bool StepPiped(World& w, const PulseTuning& t, Moving& m, float dt);

    std::unordered_set<PulseCell, PulseCellHash> m_harvesters;
    std::unordered_map<PulseCell, int, PulseCellHash> m_pipeNet;
    std::vector<Network> m_nets;
    std::unordered_map<PulseCell, int, PulseCellHash> m_reserved; // pulses on their way to each store
    std::vector<Moving> m_moving;
    uint64_t m_seenEdits = ~0ull;
    unsigned m_turn = 0; // which face of a harvester tries first (so every pipe off it gets used)
    double m_lastBeat = -1;
    bool m_haveBeat = false;
};

extern PulseSystem g_pulse;
extern PulseTuning g_pulseTuning;
