// soundscape.h
//
// Where the world sound palette's three axes come from
// (docs/SOUND_PALETTE.md 3.1): a fixed-cost census of the blocks around
// the player -- one horizontal slab of a 32 x 24 x 32 box per frame, so the
// whole box refreshes every ~24 frames whatever the world's size -- plus
// the player's movement and recent interactions, eased with slow time
// constants so every change is gradual. It also notices discoveries
// (first glowing block, first dark ground, a region of new materials)
// and fills the ambient scheduler's scene. Pure C++ over world.h; tested
// natively.

#pragma once

#include "blocks.h"
#include "sfx_synth.h"
#include <cstdint>

class World;

// What a block sounds like, and which side of the axes it counts toward.
SoundMaterial BlockSoundMaterial(BlockID id);
enum SoundClass : uint8_t { SC_NEUTRAL, SC_NATURAL, SC_MECHANICAL, SC_DARK, SC_GENESIS };
SoundClass BlockSoundClass(BlockID id);

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
    void CensusStep(World& w, int px, int py, int pz);
    // Enclosure: is there a roof over the player, and how much rock above?
    void ProbeSky(World& w, int px, int py, int pz);
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
    int musicN = 0; uint32_t musicKey[3] = {}; int musicY[3] = {}; int musicD2[3] = {};
    bool seenBlock[BLOCK_COUNT] = {};     // this session
    bool seenThisSweep[BLOCK_COUNT] = {};
    bool glinted[BLOCK_COUNT] = {};
    int newThisSweep = 0;
    float sessionSeconds = 0;
    float interactions = 0;               // decaying count (~8 s)
    float lookUpSeconds = 0;
    float sinceDark = 1e9f, sinceOre = 1e9f;
    bool roof = false; int rockAbove = 0;
    SoundId pending[8]; int pendingN = 0;
    void Discover(SoundId id) { if (pendingN < 8) pending[pendingN++] = id; }
    void EndSweep();
};
