// soundscape.cpp -- see soundscape.h and docs/SOUND_PALETTE.md 3.1.

#include "soundscape.h"
#include "world.h"
#include <cmath>
#include <cstring>

SoundMaterial BlockSoundMaterial(BlockID id) {
    switch (id) {
    case BLOCK_DIRT: case BLOCK_CLAY: case BLOCK_CRACKED_EARTH: case BLOCK_PEAT_BOG: case BLOCK_SAND:
    case BLOCK_COASTAL_SAND: case BLOCK_SALT_FLAT: case BLOCK_SNOW: case BLOCK_VOLCANIC_ASH: case BLOCK_MEADOW_GRASS:
    case BLOCK_MOSS: case BLOCK_AUTUMN_LEAF_LITTER: case BLOCK_SHALLOW_WATER:
        return MAT_EARTH;
    case BLOCK_STONE: case BLOCK_STONE_SLAB: case BLOCK_STONE_PYRAMID: case BLOCK_STONE_PYRAMID_HALF:
    case BLOCK_STONE_FUNNEL: case BLOCK_STONE_FUNNEL_HALF: case BLOCK_SANDSTONE: case BLOCK_BASALT:
    case BLOCK_MAGMA_ROCK: case BLOCK_MOSS_STONE: case BLOCK_RIVER_PEBBLE: case BLOCK_FOUNDATION:
    case BLOCK_VOID_STATIC_GROUND: case BLOCK_ARCHIVIST_WALL: case BLOCK_CORAL_REEF:
        return MAT_STONE;
    case BLOCK_WOOD: case BLOCK_WOOD_RAMP: case BLOCK_LOG: case BLOCK_CHEST:
        return MAT_WOOD;
    case BLOCK_JUNGLE_CANOPY: case BLOCK_WILDFLOWER_YELLOW: case BLOCK_WILDFLOWER_BLUE: case BLOCK_GLOW_MUSHROOM_CLUSTER:
    case BLOCK_FERN_FROND: case BLOCK_THORN_BRAMBLE: case BLOCK_REED_GRASS:
        return MAT_PLANT;
    case BLOCK_GLASS: case BLOCK_CRYSTAL: case BLOCK_RAW_FRAGMENT_ORE: case BLOCK_GLACIER_ICE:
        return MAT_GLASS;
    case BLOCK_MACHINE: case BLOCK_TUBE: case BLOCK_CUSTODIAN_LATTICE: case BLOCK_MUSIC: case BLOCK_TIMESTREAM:
    case BLOCK_ATTRACTOR: case BLOCK_STAR_FORGE:
        return MAT_METAL;
    case BLOCK_VEINED_FLESH: case BLOCK_FLESH_WOUND: case BLOCK_PULSING_MEMBRANE: case BLOCK_WEEPING_SORE: case BLOCK_CORRUPTED_FLESH:
        return MAT_FLESH;
    case BLOCK_GENESIS_SOIL: case BLOCK_SEEDLING_SPROUT: case BLOCK_DAWN_LIGHT: case BLOCK_NEW_LOG:
        return MAT_GENESIS;
    // Props and pieces (4.15) sound like what they're made of.
    case BLOCK_MOSS_CLUMP: case BLOCK_MOSS_TUFT: case BLOCK_MEADOW_TUSSOCK: case BLOCK_EARTH_CLOD: case BLOCK_PEAT_CLOD:
    case BLOCK_SALT_BLISTER: case BLOCK_SNOW_DRIFT: case BLOCK_COASTAL_BOULDER: case BLOCK_WATER_RIPPLE:
        return MAT_EARTH;
    case BLOCK_STONE_SHARD: case BLOCK_BASALT_SHARD: case BLOCK_MAGMA_SHARD: case BLOCK_SANDSTONE_SHARD: case BLOCK_MOSS_STONE_SHARD:
    case BLOCK_PEBBLE_BOULDER: case BLOCK_PEBBLE_BREAKER: case BLOCK_CORAL_NODE: case BLOCK_STONE_CORBEL:
    case BLOCK_STONE_CHIMNEY_CAP: case BLOCK_CLAY_CHIMNEY_CAP:
        return MAT_STONE;
    case BLOCK_LOG_BEAM: case BLOCK_WOOD_BEAM: case BLOCK_WOOD_CORBEL: case BLOCK_WOOD_SHUTTER: case BLOCK_WOOD_AWNING: case BLOCK_WOOD_STRUT:
        return MAT_WOOD;
    case BLOCK_LEAF_PAD:
        return MAT_PLANT;
    case BLOCK_ORE_SHARD: case BLOCK_GLACIER_SHARD:
        return MAT_GLASS;
    case BLOCK_CONDUIT_PIPE: case BLOCK_LATTICE_PIPE: case BLOCK_MACHINE_GEAR: case BLOCK_FOUNDATION_VENT: case BLOCK_ORE_HOPPER: case BLOCK_LATTICE_STRUT:
        return MAT_METAL;
    case BLOCK_SORE_BULB: case BLOCK_CORRUPTED_BULB: case BLOCK_MEMBRANE_SAC:
        return MAT_FLESH;
    case BLOCK_GENESIS_CLOD: case BLOCK_GENESIS_ROOT_KNUCKLE:
        return MAT_GENESIS;
    default:
        return MAT_NONE;
    }
}

SoundClass BlockSoundClass(BlockID id) {
    switch (id) {
    case BLOCK_DIRT: case BLOCK_MEADOW_GRASS: case BLOCK_MOSS: case BLOCK_MOSS_STONE: case BLOCK_LOG:
    case BLOCK_JUNGLE_CANOPY: case BLOCK_AUTUMN_LEAF_LITTER: case BLOCK_PEAT_BOG: case BLOCK_SHALLOW_WATER:
    case BLOCK_CORAL_REEF: case BLOCK_RIVER_PEBBLE: case BLOCK_COASTAL_SAND: case BLOCK_SAND: case BLOCK_SNOW:
    case BLOCK_CLAY: case BLOCK_WILDFLOWER_YELLOW: case BLOCK_WILDFLOWER_BLUE: case BLOCK_GLOW_MUSHROOM_CLUSTER:
    case BLOCK_FERN_FROND: case BLOCK_THORN_BRAMBLE: case BLOCK_REED_GRASS: case BLOCK_GLACIER_ICE:
    case BLOCK_MOSS_CLUMP: case BLOCK_MOSS_TUFT: case BLOCK_MEADOW_TUSSOCK: case BLOCK_EARTH_CLOD: case BLOCK_PEAT_CLOD:
    case BLOCK_SALT_BLISTER: case BLOCK_SNOW_DRIFT: case BLOCK_COASTAL_BOULDER: case BLOCK_PEBBLE_BOULDER: case BLOCK_CORAL_NODE:
    case BLOCK_MOSS_STONE_SHARD: case BLOCK_WATER_RIPPLE: case BLOCK_PEBBLE_BREAKER: case BLOCK_LEAF_PAD:
        return SC_NATURAL;
    case BLOCK_CONDUIT_PIPE: case BLOCK_LATTICE_PIPE: case BLOCK_MACHINE_GEAR: case BLOCK_FOUNDATION_VENT: case BLOCK_ORE_HOPPER:
    case BLOCK_LATTICE_STRUT:
        return SC_MECHANICAL;
    case BLOCK_SORE_BULB: case BLOCK_CORRUPTED_BULB: case BLOCK_MEMBRANE_SAC:
        return SC_DARK;
    case BLOCK_GENESIS_CLOD: case BLOCK_GENESIS_ROOT_KNUCKLE:
        return SC_GENESIS;
    case BLOCK_MACHINE: case BLOCK_TUBE: case BLOCK_CHEST: case BLOCK_FOUNDATION: case BLOCK_CUSTODIAN_LATTICE:
    case BLOCK_ARCHIVIST_WALL: case BLOCK_ATTRACTOR:
        return SC_MECHANICAL;
    case BLOCK_VEINED_FLESH: case BLOCK_FLESH_WOUND: case BLOCK_PULSING_MEMBRANE: case BLOCK_WEEPING_SORE:
    case BLOCK_CORRUPTED_FLESH: case BLOCK_VOID_STATIC_GROUND:
        return SC_DARK;
    case BLOCK_GENESIS_SOIL: case BLOCK_SEEDLING_SPROUT: case BLOCK_DAWN_LIGHT: case BLOCK_STAR_FORGE: case BLOCK_NEW_LOG:
        return SC_GENESIS;
    default:
        return SC_NEUTRAL;
    }
}

static inline float Presence(float count, float scale) { return 1.0f - expf(-count / scale); }
static inline float Clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
// Eases toward a target with time constant tau (seconds).
static inline float Ease(float v, float target, float tau, float dt) { return v + (target - v) * (1.0f - expf(-dt / tau)); }

void Soundscape::Reset() {
    *this = Soundscape();
}

void Soundscape::NoteInteraction() { interactions += 1.0f; }

void Soundscape::CensusStep(World& w, int px, int py, int pz) {
    if (slab == 0) {
        ox = px - BOX_XZ / 2; oy = py - BOX_Y / 2; oz = pz - BOX_XZ / 2;
        cur = {};
        musicN = 0;
        memset(seenThisSweep, 0, sizeof(seenThisSweep));
        newThisSweep = 0;
    }
    // Top-down, so "exposed" (open to the air above) is one read per cell.
    int y = oy + BOX_Y - 1 - slab;
    if (y > Y_MIN) {
        for (int x = 0; x < BOX_XZ; x++)
            for (int z = 0; z < BOX_XZ; z++) {
                int wx = ox + x, wz = oz + z;
                BlockID id = w.Get(wx, y, wz);
                if (id == BLOCK_AIR) continue;
                if (!seenThisSweep[id]) {
                    seenThisSweep[id] = true;
                    if (!seenBlock[id]) { seenBlock[id] = true; newThisSweep++; }
                }
                SoundClass sc = BlockSoundClass(id);
                bool exposed = !g_blocks[w.Get(wx, y + 1, wz)].solid;
                switch (sc) {
                case SC_NATURAL: if (exposed) cur.natural++; break;
                case SC_MECHANICAL: cur.mechanical++; break;
                case SC_DARK: cur.dark++; break;
                case SC_GENESIS: cur.genesis++; break;
                default: break;
                }
                if (BlockSoundMaterial(id) == MAT_PLANT) cur.plants++;
                if (id == BLOCK_SHALLOW_WATER || id == BLOCK_GLACIER_ICE || id == BLOCK_PEAT_BOG || id == BLOCK_MOSS) cur.water++;
                if (id == BLOCK_MAGMA_ROCK || id == BLOCK_STAR_FORGE) cur.ember++;
                if (id == BLOCK_GLOW_MUSHROOM_CLUSTER || id == BLOCK_PULSING_MEMBRANE) cur.glow++;
                if (id == BLOCK_MACHINE || id == BLOCK_TUBE) cur.machines++;
                if (id == BLOCK_RAW_FRAGMENT_ORE) cur.ore++;
                if (g_blocks[id].glow == GLOW_EMBER || g_blocks[id].glow == GLOW_PULSE || id == BLOCK_GLOW_MUSHROOM_CLUSTER || id == BLOCK_DAWN_LIGHT)
                    cur.emissive++;
                if (id == BLOCK_MUSIC) {
                    int dx = wx - px, dy = y - py, dz = wz - pz, d2 = dx * dx + dy * dy + dz * dz;
                    uint32_t key = (uint32_t)wx * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)wz * 83492791u;
                    // Keep the nearest three.
                    int slot = musicN < 3 ? musicN++ : -1;
                    if (slot < 0) { int far = 0; for (int k = 1; k < 3; k++) if (musicD2[k] > musicD2[far]) far = k; if (musicD2[far] > d2) slot = far; }
                    if (slot >= 0) { musicKey[slot] = key; musicY[slot] = y; musicD2[slot] = d2; musicX[slot] = wx + 0.5f; musicZ[slot] = wz + 0.5f; }
                }
            }
    }
    if (++slab >= BOX_Y) { slab = 0; EndSweep(); }
}

void Soundscape::EndSweep() {
    last = cur;
    sweeps++;
    scene.musicBlockCount = musicN;
    for (int k = 0; k < 3; k++) { scene.musicBlockKey[k] = musicKey[k]; scene.musicBlockY[k] = musicY[k]; scene.musicBlockX[k] = musicX[k]; scene.musicBlockZ[k] = musicZ[k]; }
    // Discoveries, one per sweep at most, most important first.
    bool grace = sessionSeconds < 20.0f; // the first look around is not "new"
    if (last.dark > 0) { if (sinceDark > 600.0f) Discover(SND_OMEN); sinceDark = 0; }
    else if (last.ore > 0 && sinceOre > 120.0f && !grace) { Discover(SND_VEIN); sinceOre = 0; }
    else if (!grace && newThisSweep >= 3) Discover(SND_HORIZON);
    if (last.ore > 0) sinceOre = 0;
    // Glint: the first emissive block of each kind this session.
    static const BlockID emissive[] = { BLOCK_MAGMA_ROCK, BLOCK_STAR_FORGE, BLOCK_GLOW_MUSHROOM_CLUSTER, BLOCK_DAWN_LIGHT, BLOCK_PULSING_MEMBRANE };
    for (BlockID e : emissive)
        if (seenThisSweep[e] && !glinted[e]) { glinted[e] = true; if (!grace) Discover(SND_GLINT); }
}

void Soundscape::ProbeSky(World& w, int px, int py, int pz) {
    roof = false; rockAbove = 0;
    for (int dy = 2; dy <= 40; dy++) {
        BlockID id = w.Get(px, py + dy, pz);
        if (g_blocks[id].solid) { rockAbove++; if (dy <= 12) roof = true; }
    }
}

void Soundscape::Update(const SoundscapeInput& in) {
    float dt = in.dt;
    sessionSeconds += dt;
    sinceDark += dt; sinceOre += dt;
    interactions *= expf(-dt / 8.0f);
    const Counts& c = last;
    float mechP = Presence((float)c.mechanical, 20.0f);
    float natP = Presence((float)c.natural, 150.0f);
    float plantP = Presence((float)c.plants, 20.0f);
    float genP = Presence((float)c.genesis, 15.0f);
    float darkP = Presence((float)c.dark, 15.0f);
    // docs/SOUND_PALETTE.md 3.1.
    float mTarget = Clampf(0.35f + 0.6f * mechP - 0.3f * natP * (1.0f - mechP), 0.0f, 1.0f);
    float pTarget = Clampf(0.1f + 0.2f * natP + 0.2f * plantP + 0.5f * genP - 1.3f * darkP, -1.0f, 1.0f);
    float move = in.sliding ? 1.0f : in.sprinting && in.speed > 1.0f ? 0.8f : in.speed > 0.5f ? 0.4f : 0.0f;
    static const float sectionEnergy[6] = { 0.25f, 0.6f, 1.0f, 0.6f, 0.25f, 0.0f };
    float energy = sectionEnergy[in.musicSection < 0 || in.musicSection > 5 ? 0 : in.musicSection];
    float aTarget = Clampf(0.12f + 0.35f * move + 0.3f * fminf(1.0f, interactions / 12.0f) + 0.12f * energy + 0.15f * mechP, 0.0f, 1.0f);
    if (!HaveCensus()) { mTarget = axes.mechanical; pTarget = axes.positive; }
    axes.mechanical = Ease(axes.mechanical, mTarget, 5.0f, dt);
    axes.positive = Ease(axes.positive, pTarget, 10.0f, dt);
    axes.activity = Ease(axes.activity, aTarget, aTarget > axes.activity ? 1.5f : 6.0f, dt);

    scene.plants = plantP;
    scene.water = Presence((float)c.water, 10.0f);
    scene.ember = Presence((float)c.ember, 4.0f);
    scene.glow = Presence((float)c.glow, 4.0f);
    scene.machines = Presence((float)c.machines, 12.0f);
    scene.dark = darkP;
    scene.enclosed = roof;
    scene.deep = rockAbove >= 12;
    lookUpSeconds = in.pitch > 0.6f && !roof ? lookUpSeconds + dt : 0.0f;
    scene.lookingUp = lookUpSeconds >= 4.0f;
    scene.stillSeconds = in.speed < 0.3f ? scene.stillSeconds + dt : 0.0f;
    scene.negativeSeconds = axes.positive < -0.3f ? scene.negativeSeconds + dt : 0.0f;
}

int Soundscape::TakeDiscoveries(SoundId* out, int max) {
    int n = pendingN < max ? pendingN : max;
    for (int i = 0; i < n; i++) out[i] = pending[i];
    pendingN = 0;
    return n;
}
