// soundscape.h
//
// Where the world sound palette's three axes come from
// (docs/SOUND_PALETTE.md 3.1): a fixed-cost census of the blocks around
// the player -- one horizontal slab of a 32 x 24 x 32 box per frame, so the
// whole box refreshes every ~24 frames whatever the world's size -- plus
// the player's movement and recent interactions, eased with slow time
// constants so every change is gradual. It also notices discoveries
// (first glowing block, first dark ground, a region of new materials)
// and fills the ambient scheduler's scene. Pure C++; tested natively.
//
// Cacophony seam: the census asks a CensusSource what each cell sounds
// like (its class, material and presences) instead of reading Voxistics'
// block world and block table; Cacophony answers from the terrain (a
// cell's ground type), the fire (burning cells are embers) and the props,
// with a cell of CELL metres, so the 32 x 24 x 32 box spans the land
// around the mech.

#pragma once

#include "sfx_synth.h"
#include <cstdint>


// Which side of the axes a cell counts toward.
enum SoundClass : uint8_t { SC_NEUTRAL, SC_NATURAL, SC_MECHANICAL, SC_DARK, SC_GENESIS };

// What one cell of the census holds (Cacophony seam: in place of a BlockID).
struct CensusCell {
    bool filled = false;       // anything there at all (false = air)
    bool solid = false;        // blocks the sky (for "exposed" and the roof probe)
    SoundClass cls = SC_NEUTRAL;
    uint8_t kind = 0;          // a small id per kind of thing (< CENSUS_KINDS), for noticing what's new
    bool plant = false, water = false, ember = false, glow = false, machine = false, ore = false, emissive = false;
};
static const int CENSUS_KINDS = 64;
typedef CensusCell (*CensusSource)(int x, int y, int z);

struct SoundscapeInput {
    float dt = 0;              // seconds
    float speed = 0;           // horizontal, blocks/s
    bool sprinting = false, sliding = false;
    float pitch = 0;           // view pitch, radians (+ = up)
    int musicSection = 0;      // MusicSection now playing
};

class Soundscape {
public:
    static const int BOX_XZ = 32, BOX_Y = 24;

    void Reset();
    // One slab of the census, around (px, py, pz). Call once per frame.
    void CensusStep(CensusSource get, int px, int py, int pz);
    // Enclosure: is there a roof over the player, and how much rock above?
    void ProbeSky(CensusSource get, int px, int py, int pz);
    void NoteInteraction();    // a place, break or other deliberate act
    void Update(const SoundscapeInput& in);

    const SoundAxes& Axes() const { return axes; }
    const AmbientScene& Scene() const { return scene; }
    // Discoveries noticed since the last call (Glint, Omen, Vein, Horizon).
    int TakeDiscoveries(SoundId* out, int max);

    // The last complete census (for tests and F3).
    struct Counts { int natural, mechanical, dark, genesis, plants, water, ember, glow, machines, ore, emissive; };
    const Counts& LastCounts() const { return last; }
    bool HaveCensus() const { return sweeps > 0; }

private:
    SoundAxes axes;
    AmbientScene scene;
    Counts cur = {}, last = {};
    int slab = 0, sweeps = 0;
    int ox = 0, oy = 0, oz = 0;           // this sweep's box origin (min corner)
    int musicN = 0; uint32_t musicKey[3] = {}; int musicY[3] = {}; int musicD2[3] = {}; float musicX[3] = {}, musicZ[3] = {};
    bool seenBlock[CENSUS_KINDS] = {};    // this session (Cacophony seam: kinds, not block IDs)
    bool seenThisSweep[CENSUS_KINDS] = {};
    bool glinted[CENSUS_KINDS] = {};
    int newThisSweep = 0;
    bool glintPending = false;
    float sessionSeconds = 0;
    float interactions = 0;               // decaying count (~8 s)
    float lookUpSeconds = 0;
    float sinceDark = 1e9f, sinceOre = 1e9f;
    bool roof = false; int rockAbove = 0;
    SoundId pending[8]; int pendingN = 0;
    void Discover(SoundId id) { if (pendingN < 8) pending[pendingN++] = id; }
    void EndSweep();
};
