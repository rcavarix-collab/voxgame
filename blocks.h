// blocks.h
//
// The block registry (DESIGN.md Part III): every block type is defined
// by exactly one row in g_blocks below -- its save identity, physical
// flags, and which texture goes on which face. Adding a block means one
// enum entry plus one row; meshing, gravity, picking, the hotbar, the
// save format and the texture loader all read this table rather than
// keeping their own lists.

#pragma once

#include <cstdint>

enum BlockID : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_FOUNDATION,
    BLOCK_STONE,
    BLOCK_DIRT,
    BLOCK_WOOD,
    BLOCK_CHEST,
    BLOCK_MACHINE,
    BLOCK_STONE_SLAB,
    BLOCK_WOOD_RAMP,
    BLOCK_TUBE,
    BLOCK_STONE_PYRAMID,
    BLOCK_STONE_PYRAMID_HALF,
    BLOCK_STONE_FUNNEL,
    BLOCK_STONE_FUNNEL_HALF,
    BLOCK_MUSIC,
    BLOCK_TIMESTREAM,
    BLOCK_ATTRACTOR,
    BLOCK_GLASS,
    BLOCK_CRYSTAL,
    BLOCK_SNOW,
    BLOCK_SAND,
    BLOCK_SANDSTONE,
    BLOCK_CRACKED_EARTH,
    BLOCK_CLAY,
    BLOCK_BASALT,
    BLOCK_MAGMA_ROCK,
    BLOCK_LOG,
    BLOCK_MOSS,
    BLOCK_MOSS_STONE,
    BLOCK_MEADOW_GRASS,
    BLOCK_SHALLOW_WATER,
    BLOCK_GLACIER_ICE,
    BLOCK_VOLCANIC_ASH,
    BLOCK_CORAL_REEF,
    BLOCK_JUNGLE_CANOPY,
    BLOCK_AUTUMN_LEAF_LITTER,
    BLOCK_PEAT_BOG,
    BLOCK_SALT_FLAT,
    BLOCK_RIVER_PEBBLE,
    BLOCK_COASTAL_SAND,
    BLOCK_VEINED_FLESH,
    BLOCK_FLESH_WOUND,
    BLOCK_PULSING_MEMBRANE,
    BLOCK_WEEPING_SORE,
    BLOCK_CORRUPTED_FLESH,
    BLOCK_GENESIS_SOIL,
    BLOCK_SEEDLING_SPROUT,
    BLOCK_DAWN_LIGHT,
    BLOCK_STAR_FORGE,
    BLOCK_NEW_LOG,
    BLOCK_COUNT
};

// Geometry (shapes.h). Everything but CUBE is baked into the chunk mesh
// from a canonical definition rotated by the block's state.
enum BlockShape : uint8_t {
    SHAPE_CUBE = 0,
    SHAPE_SLAB,          // half height; STATE_UPPER puts it in the top half
    SHAPE_RAMP,          // rises toward its facing
    SHAPE_TUBE,          // quarter-block bar along its facing's axis
    SHAPE_PYRAMID,
    SHAPE_PYRAMID_HALF,  // half-height pyramid
    SHAPE_FUNNEL,        // upside-down pyramid
    SHAPE_FUNNEL_HALF,   // upside-down half pyramid, in the top half
};

// Blocks that light up on their own (world shader, Section 4.2): the
// kind rides in spare vertex bits, the driving value is one per-frame
// constant, so a glowing block costs nothing on the CPU.
enum BlockGlow : uint8_t {
    GLOW_NONE = 0,
    GLOW_MUSIC,       // pulses with the music actually playing
    GLOW_TIMESTREAM,  // lights while The Line passes through it (Part XVIII)
    GLOW_EMBER,       // a steady warm light source (magma); what glows on it is the texture's glow map (4.13)
    GLOW_PULSE,       // its texture's glow map breathes slowly (well under 1 Hz: flash-safe); casts no light
};
// Whether a glow kind lights the world around it (glowlight.h).
static inline bool GlowCastsLight(BlockGlow g) { return g == GLOW_MUSIC || g == GLOW_TIMESTREAM || g == GLOW_EMBER; }

// How placement sets the state byte.
enum PlaceRule : uint8_t {
    PLACE_PLAIN = 0,     // state 0
    PLACE_FACE_PLAYER,   // front turns toward the player (chest, machine)
    PLACE_AWAY,          // facing = the way the player looks (a ramp rises away from you)
    PLACE_CLICKED_AXIS,  // facing = normal of the face clicked (tubes run out from what you click)
    PLACE_SLAB_HALF,     // upper or lower half, by where on the face you click
};

// Face order is shared by the mesher, the per-face shading table and a
// block's stored facing: +X, -X, +Y (top), -Y (bottom), +Z, -Z.
enum BlockFace : uint8_t {
    FACE_POS_X = 0, FACE_NEG_X, FACE_POS_Y, FACE_NEG_Y, FACE_POS_Z, FACE_NEG_Z, FACE_COUNT
};

// Per-block state byte, stored alongside the block ID in every chunk and
// in saves. Low 3 bits: facing (a BlockFace, horizontal only in
// practice) for orientable blocks. The other 5 bits are reserved -- e.g.
// a machine's on/off, a slab's half -- so they can be claimed without a
// storage or save-format change.
static const uint8_t STATE_FACING_MASK = 0x07;
static const uint8_t STATE_UPPER = 0x08; // slab in the top half
static inline BlockFace StateFacing(uint8_t state) {
    uint8_t f = state & STATE_FACING_MASK;
    return f < FACE_COUNT ? (BlockFace)f : FACE_POS_Z;
}

struct BlockDef {
    const char* name;     // identity on disk (Section 3.1) -- never renumbered, only renamed with a migration
    bool solid;           // collision, raycast hits, hides neighbouring faces
    bool foundational;    // never falls, always supports (Part V)
    bool placeable;       // appears on the hotbar
    bool orientable;      // stores a facing; its `front` texture goes on that side
    bool hasData;         // may carry a per-block data record (contents, machine state)
    BlockShape shape;
    PlaceRule place;
    BlockGlow glow;
    bool translucent;     // see-through (glass): drawn after the opaque world, blended; hides only its own kind
    // Texture names (assets/textures/TEXTURE_BRIEF.md). The most specific
    // one set wins: front > side > all for the four sides, top/bottom >
    // all for those faces. nullptr = not set. A texture with no authored
    // .vtex art falls back to the procedural texture of the same name,
    // then to the block's own name.
    const char* texAll;
    const char* texTop;
    const char* texBottom;
    const char* texSide;
    const char* texFront;
};

// Columns: name, solid, foundational, placeable, orientable, hasData,
// shape, place rule, glow, translucent, then textures all / top / bottom
// / side / front.
#define TEX(all, top, bottom, side, front) all, top, bottom, side, front
inline const BlockDef g_blocks[BLOCK_COUNT] = {
    { "air",                false, false, false, false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX(nullptr, nullptr, nullptr, nullptr, nullptr) },
    { "foundation",         true,  true,  true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("foundation", nullptr, nullptr, nullptr, nullptr) },
    { "stone",              true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "dirt",               true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("dirt", nullptr, nullptr, nullptr, nullptr) },
    { "wood",               true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    { "chest",              true,  true,  true,  true,  true,  SHAPE_CUBE,         PLACE_FACE_PLAYER, GLOW_NONE, false,  TEX("chest", nullptr, nullptr, nullptr, "chest_front") },
    { "machine",            true,  true,  true,  true,  true,  SHAPE_CUBE,         PLACE_FACE_PLAYER, GLOW_NONE, false,  TEX("machine", nullptr, nullptr, nullptr, "machine_front") },
    // Shape test blocks (the Prismative.cpp primitives), foundational for
    // now so a test build doesn't collapse while it's being looked at.
    { "stone_slab",         true,  true,  true,  false, false, SHAPE_SLAB,         PLACE_SLAB_HALF, GLOW_NONE, false,    TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "wood_ramp",          true,  true,  true,  false, false, SHAPE_RAMP,         PLACE_AWAY, GLOW_NONE, false,         TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    { "tube",               true,  true,  true,  false, false, SHAPE_TUBE,         PLACE_CLICKED_AXIS, GLOW_NONE, false, TEX("tube", nullptr, nullptr, nullptr, nullptr) },
    { "stone_pyramid",      true,  true,  true,  false, false, SHAPE_PYRAMID,      PLACE_PLAIN, GLOW_NONE, false,        TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "stone_pyramid_half", true,  true,  true,  false, false, SHAPE_PYRAMID_HALF, PLACE_PLAIN, GLOW_NONE, false,        TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "stone_funnel",       true,  true,  true,  false, false, SHAPE_FUNNEL,       PLACE_PLAIN, GLOW_NONE, false,        TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "stone_funnel_half",  true,  true,  true,  false, false, SHAPE_FUNNEL_HALF,  PLACE_PLAIN, GLOW_NONE, false,        TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    // Reactive blocks: plain cubes that light up on their own.
    { "music_block",        true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_MUSIC, false,       TEX("music_block", nullptr, nullptr, nullptr, nullptr) },
    { "timestream_block",   true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_TIMESTREAM, false,  TEX("timestream_block", nullptr, nullptr, nullptr, nullptr) },
    // PROVISIONAL placeholder for player-built essence attractors (Part XIX):
    // a node on the essence map that draws from convergence zones in reach.
    { "essence_attractor",  true,  true,  true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("essence_attractor", nullptr, nullptr, nullptr, nullptr) },
    // See-through blocks (DESIGN.md 4.11): clear glass and a tinted crystal.
    { "glass",              true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, true,         TEX("glass", nullptr, nullptr, nullptr, nullptr) },
    { "crystal",            true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, true,         TEX("crystal", nullptr, nullptr, nullptr, nullptr) },
    // Natural materials (art: assets/textures/natural.vtex, made by
    // tools/natural_textures.py from the art batch's palettes).
    { "snow",               true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("snow", nullptr, nullptr, nullptr, nullptr) },
    { "sand",               true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("sand", nullptr, nullptr, nullptr, nullptr) },
    { "sandstone",          true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX(nullptr, "sandstone_top", "sandstone_top", "sandstone_layered", nullptr) },
    { "cracked_earth",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("cracked_earth", nullptr, nullptr, nullptr, nullptr) },
    { "clay",               true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("clay", nullptr, nullptr, nullptr, nullptr) },
    { "basalt",             true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("basalt", nullptr, nullptr, nullptr, nullptr) },
    { "magma_rock",         true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_EMBER, false,       TEX("magma_rock", nullptr, nullptr, nullptr, nullptr) },
    { "log",                true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX(nullptr, "log_top", "log_top", "log_bark", nullptr) },
    { "moss",               true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE, false,        TEX("moss", nullptr, nullptr, nullptr, nullptr) },
    // More natural materials (the second art batch), then the dark set and
    // its light counterpart, the genesis set. Water and ice are provisional
    // solid see-through blocks until fluids exist.
    { "moss_stone",        true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("moss_stone", nullptr, nullptr, nullptr, nullptr) },
    { "meadow_grass",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("meadow_grass", nullptr, nullptr, nullptr, nullptr) },
    { "shallow_water",     true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      true ,       TEX("shallow_water", nullptr, nullptr, nullptr, nullptr) },
    { "glacier_ice",       true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      true ,       TEX("glacier_ice", nullptr, nullptr, nullptr, nullptr) },
    { "volcanic_ash",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("volcanic_ash", nullptr, nullptr, nullptr, nullptr) },
    { "coral_reef",        true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("coral_reef", nullptr, nullptr, nullptr, nullptr) },
    { "jungle_canopy",     true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("jungle_canopy", nullptr, nullptr, nullptr, nullptr) },
    { "autumn_leaf_litter", true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("autumn_leaf_litter", nullptr, nullptr, nullptr, nullptr) },
    { "peat_bog",          true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("peat_bog", nullptr, nullptr, nullptr, nullptr) },
    { "salt_flat",         true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("salt_flat", nullptr, nullptr, nullptr, nullptr) },
    { "river_pebble",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("river_pebble", nullptr, nullptr, nullptr, nullptr) },
    { "coastal_sand",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("coastal_sand", nullptr, nullptr, nullptr, nullptr) },
    { "veined_flesh",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("veined_flesh", nullptr, nullptr, nullptr, nullptr) },
    { "flesh_wound",       true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("flesh_wound", nullptr, nullptr, nullptr, nullptr) },
    { "pulsing_membrane",  true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_PULSE,     false,       TEX("pulsing_membrane", nullptr, nullptr, nullptr, nullptr) },
    { "weeping_sore",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("weeping_sore", nullptr, nullptr, nullptr, nullptr) },
    { "corrupted_flesh",   true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("corrupted_flesh", nullptr, nullptr, nullptr, nullptr) },
    { "genesis_soil",      true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("genesis_soil", nullptr, nullptr, nullptr, nullptr) },
    { "seedling_sprout",   true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("seedling_sprout", nullptr, nullptr, nullptr, nullptr) },
    { "dawn_light",        true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("dawn_light", nullptr, nullptr, nullptr, nullptr) },
    { "star_forge",        true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_EMBER,     false,       TEX("star_forge", nullptr, nullptr, nullptr, nullptr) },
    { "new_log",           true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX(nullptr, "log_top", "log_top", "new_bark", nullptr) },
};
#undef TEX

static inline bool BlockSolid(BlockID id) { return g_blocks[id].solid; }
// Solid and a full cube (collision, geometry).
static inline bool BlockFullCube(BlockID id) { return g_blocks[id].solid && g_blocks[id].shape == SHAPE_CUBE; }
// A full cube you can't see through: hides the faces beside it and
// darkens AO. Glass is a full cube but not opaque.
static inline bool BlockOpaqueCube(BlockID id) { return BlockFullCube(id) && !g_blocks[id].translucent; }

// The placeable blocks in registry order: the hotbar's contents.
struct PlaceableList {
    BlockID ids[BLOCK_COUNT];
    int count;
};
inline PlaceableList BuildPlaceableList() { // inline, not static: the inline variable below must see one function in every file
    PlaceableList l = {};
    for (int i = 0; i < BLOCK_COUNT; i++)
        if (g_blocks[i].placeable) l.ids[l.count++] = (BlockID)i;
    return l;
}
inline const PlaceableList g_placeableList = BuildPlaceableList();

// Texture name for one face of a block with the given facing -- resolved
// once per (block, facing, face) into a texture-array layer at load
// time (textures), never per vertex.
static inline const char* BlockFaceTextureName(BlockID id, BlockFace facing, BlockFace face) {
    const BlockDef& d = g_blocks[id];
    const char* name = nullptr;
    if (face == FACE_POS_Y) name = d.texTop;
    else if (face == FACE_NEG_Y) name = d.texBottom;
    else {
        if (d.orientable && face == facing) name = d.texFront;
        if (!name) name = d.texSide;
    }
    if (!name) name = d.texAll;
    if (!name) name = d.name;
    return name;
}
