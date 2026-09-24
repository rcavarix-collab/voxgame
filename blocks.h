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
    BLOCK_VOID_STATIC_GROUND,
    BLOCK_CUSTODIAN_LATTICE,
    BLOCK_RAW_FRAGMENT_ORE,
    BLOCK_ARCHIVIST_WALL,
    BLOCK_WILDFLOWER_YELLOW,
    BLOCK_WILDFLOWER_BLUE,
    BLOCK_GLOW_MUSHROOM_CLUSTER,
    BLOCK_FERN_FROND,
    BLOCK_THORN_BRAMBLE,
    BLOCK_REED_GRASS,
    BLOCK_MOSS_CLUMP,
    BLOCK_MOSS_TUFT,
    BLOCK_MEADOW_TUSSOCK,
    BLOCK_EARTH_CLOD,
    BLOCK_PEAT_CLOD,
    BLOCK_GENESIS_CLOD,
    BLOCK_GENESIS_ROOT_KNUCKLE,
    BLOCK_SALT_BLISTER,
    BLOCK_CORAL_NODE,
    BLOCK_SORE_BULB,
    BLOCK_CORRUPTED_BULB,
    BLOCK_MEMBRANE_SAC,
    BLOCK_SNOW_DRIFT,
    BLOCK_STONE_SHARD,
    BLOCK_BASALT_SHARD,
    BLOCK_MAGMA_SHARD,
    BLOCK_SANDSTONE_SHARD,
    BLOCK_MOSS_STONE_SHARD,
    BLOCK_ORE_SHARD,
    BLOCK_GLACIER_SHARD,
    BLOCK_WATER_RIPPLE,
    BLOCK_LEAF_PAD,
    BLOCK_LOG_BEAM,
    BLOCK_WOOD_BEAM,
    BLOCK_WOOD_CORBEL,
    BLOCK_STONE_CORBEL,
    BLOCK_WOOD_SHUTTER,
    BLOCK_WOOD_AWNING,
    BLOCK_CONDUIT_PIPE,
    BLOCK_LATTICE_PIPE,
    BLOCK_MACHINE_GEAR,
    BLOCK_FOUNDATION_VENT,
    BLOCK_ORE_HOPPER,
    BLOCK_WOOD_STRUT,
    BLOCK_LATTICE_STRUT,
    // Pulse logistics (DESIGN.md Part VI)
    BLOCK_PULSE_HARVESTER,
    BLOCK_PULSE_PIPE,
    BLOCK_PULSE_STORE,
    BLOCK_PULSE_PIPE_CW,   // twisted pipes: they give pulse a clockwise or anticlockwise spin
    BLOCK_PULSE_PIPE_CCW,
    BLOCK_PULSE_DIFFUSER,  // takes pulse of any spin and spends it widening The Line's band
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
    SHAPE_CARD,          // a plant: one upright card that turns to face the viewer (4.14); not solid
    // Faceted props (DESIGN.md 4.15), anchored flush on the face they were
    // placed against -- the same mesh reads as a mound on a floor, a drape
    // from a ceiling, a snag or ledge from a wall:
    SHAPE_SWELL_MOUND,   // low octagonal mound (clump, tussock, clod)
    SHAPE_SWELL_BULB,    // lopsided bulb, pinched at the base
    SHAPE_SWELL_KNOB,    // tall pinched knob / bud (root knuckle)
    SHAPE_SWELL_BOULDER, // wide, flat, worn smooth (weathered rock, drift)
    SHAPE_SWELL_BREAKER, // very flat and wide: a rock tip or pad at a water surface
    SHAPE_SHARD,         // an angular broken chunk (scree; a ledge on a wall)
    SHAPE_RIPPLE_LIP,    // a thin lapping ridge along one edge, on a water surface
    // Dwelling
    SHAPE_BEAM,          // a diagonal rafter, rising away; chains corner to corner
    SHAPE_CORBEL,        // a stepped right-angle bracket off a wall
    SHAPE_SHUTTER,       // a thin louvered panel flush on a face
    SHAPE_AWNING,        // a thin sloped overhang off a wall
    // Industry
    SHAPE_PIPE,          // an octagonal conduit along the clicked axis
    SHAPE_GEAR,          // a notched disc in relief on a face
    SHAPE_VENT,          // a column flaring at the top
    SHAPE_HOPPER,        // an open inverted frustum that catches what falls in
    SHAPE_STRUT,         // an X-brace in the plane facing the player
    // Landmarks: distinctive, meant to be placed deliberately and sparingly
    // Logistics
    SHAPE_PULSE_PIPE,    // a pipe that joins whatever is beside it: straight runs, bends, junctions (mesher)
    SHAPE_BEVEL_CUBE,    // a full block with its edges chamfered: machines, softened (26 polygons)
    SHAPE_COUNT
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
    { "machine",            true,  true,  true,  true,  true,  SHAPE_BEVEL_CUBE,       PLACE_FACE_PLAYER, GLOW_NONE, false,  TEX("machine", nullptr, nullptr, nullptr, "machine_front") },
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
    // The custodian set: void ground, the lattice, a raw ore, archival masonry.
    { "void_static_ground", true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("void_static_ground", nullptr, nullptr, nullptr, nullptr) },
    { "custodian_lattice", true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("custodian_lattice", nullptr, nullptr, nullptr, nullptr) },
    { "raw_fragment_ore",  true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("raw_fragment_ore", nullptr, nullptr, nullptr, nullptr) },
    { "archivist_wall",    true,  false, true,  false, false, SHAPE_CUBE,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("archivist_wall", nullptr, nullptr, nullptr, nullptr) },
    // Plants: cards that turn to face the viewer (4.14). Walk-through, but targetable.
    { "wildflower_yellow",   false, false, true,  false, false, SHAPE_CARD,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("wildflower_yellow", nullptr, nullptr, nullptr, nullptr) },
    { "wildflower_blue",     false, false, true,  false, false, SHAPE_CARD,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("wildflower_blue", nullptr, nullptr, nullptr, nullptr) },
    { "glow_mushroom_cluster", false, false, true,  false, false, SHAPE_CARD,         PLACE_PLAIN, GLOW_EMBER,     false,       TEX("glow_mushroom_cluster", nullptr, nullptr, nullptr, nullptr) },
    { "fern_frond",          false, false, true,  false, false, SHAPE_CARD,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("fern_frond", nullptr, nullptr, nullptr, nullptr) },
    { "thorn_bramble",       false, false, true,  false, false, SHAPE_CARD,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("thorn_bramble", nullptr, nullptr, nullptr, nullptr) },
    { "reed_grass",          false, false, true,  false, false, SHAPE_CARD,         PLACE_PLAIN, GLOW_NONE,      false,       TEX("reed_grass", nullptr, nullptr, nullptr, nullptr) },
    // Faceted props (4.15): scatter set-dressing, anchored on the face they're placed against.
    { "moss_clump",            true,  true,  true,  false, false, SHAPE_SWELL_MOUND,   PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("moss_stone", nullptr, nullptr, nullptr, nullptr) },
    { "moss_tuft",             true,  true,  true,  false, false, SHAPE_SWELL_MOUND,   PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("moss", nullptr, nullptr, nullptr, nullptr) },
    { "meadow_tussock",        true,  true,  true,  false, false, SHAPE_SWELL_MOUND,   PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("meadow_grass", nullptr, nullptr, nullptr, nullptr) },
    { "earth_clod",            true,  true,  true,  false, false, SHAPE_SWELL_MOUND,   PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("cracked_earth", nullptr, nullptr, nullptr, nullptr) },
    { "peat_clod",             true,  true,  true,  false, false, SHAPE_SWELL_MOUND,   PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("peat_bog", nullptr, nullptr, nullptr, nullptr) },
    { "genesis_clod",          true,  true,  true,  false, false, SHAPE_SWELL_MOUND,   PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("genesis_soil", nullptr, nullptr, nullptr, nullptr) },
    { "genesis_root_knuckle",  true,  true,  true,  false, false, SHAPE_SWELL_KNOB,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("genesis_soil", nullptr, nullptr, nullptr, nullptr) },
    { "salt_blister",          true,  true,  true,  false, false, SHAPE_SWELL_BREAKER, PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("salt_flat", nullptr, nullptr, nullptr, nullptr) },
    { "coral_node",            true,  true,  true,  false, false, SHAPE_SWELL_BULB,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("coral_reef", nullptr, nullptr, nullptr, nullptr) },
    { "sore_bulb",             true,  true,  true,  false, false, SHAPE_SWELL_BULB,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("weeping_sore", nullptr, nullptr, nullptr, nullptr) },
    { "corrupted_bulb",        true,  true,  true,  false, false, SHAPE_SWELL_BULB,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("corrupted_flesh", nullptr, nullptr, nullptr, nullptr) },
    { "membrane_sac",          true,  true,  true,  false, false, SHAPE_SWELL_BULB,    PLACE_CLICKED_AXIS, GLOW_PULSE, false, TEX("pulsing_membrane", nullptr, nullptr, nullptr, nullptr) },
    { "snow_drift",            true,  true,  true,  false, false, SHAPE_SWELL_BOULDER, PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("snow", nullptr, nullptr, nullptr, nullptr) },
    { "stone_shard",           true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "basalt_shard",          true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("basalt", nullptr, nullptr, nullptr, nullptr) },
    { "magma_shard",           true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_EMBER, false, TEX("magma_rock", nullptr, nullptr, nullptr, nullptr) },
    { "sandstone_shard",       true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX(nullptr, "sandstone_top", "sandstone_top", "sandstone_layered", nullptr) },
    { "moss_stone_shard",      true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("moss_stone", nullptr, nullptr, nullptr, nullptr) },
    { "ore_shard",             true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("raw_fragment_ore", nullptr, nullptr, nullptr, nullptr) },
    { "glacier_shard",         true,  true,  true,  false, false, SHAPE_SHARD,         PLACE_CLICKED_AXIS, GLOW_NONE,  true,  TEX("glacier_ice", nullptr, nullptr, nullptr, nullptr) },
    // On a water surface: a lapping edge for shorelines, and things breaking the surface.
    { "water_ripple",          true,  true,  true,  false, false, SHAPE_RIPPLE_LIP,    PLACE_AWAY,         GLOW_NONE,  true,  TEX("shallow_water", nullptr, nullptr, nullptr, nullptr) },
    { "leaf_pad",              true,  true,  true,  false, false, SHAPE_SWELL_BREAKER, PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("jungle_canopy", nullptr, nullptr, nullptr, nullptr) },
    // Dwelling pieces.
    { "log_beam",              true,  true,  true,  false, false, SHAPE_BEAM,          PLACE_AWAY,         GLOW_NONE,  false, TEX(nullptr, "log_top", "log_top", "log_bark", nullptr) },
    { "wood_beam",             true,  true,  true,  false, false, SHAPE_BEAM,          PLACE_AWAY,         GLOW_NONE,  false, TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    { "wood_corbel",           true,  true,  true,  false, false, SHAPE_CORBEL,        PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    { "stone_corbel",          true,  true,  true,  false, false, SHAPE_CORBEL,        PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("stone", nullptr, nullptr, nullptr, nullptr) },
    { "wood_shutter",          true,  true,  true,  false, false, SHAPE_SHUTTER,       PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    { "wood_awning",           true,  true,  true,  false, false, SHAPE_AWNING,        PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    // Industry pieces.
    { "conduit_pipe",          true,  true,  true,  false, false, SHAPE_PIPE,          PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("tube", nullptr, nullptr, nullptr, nullptr) },
    { "lattice_pipe",          true,  true,  true,  false, false, SHAPE_PIPE,          PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("custodian_lattice", nullptr, nullptr, nullptr, nullptr) },
    { "machine_gear",          true,  true,  true,  false, false, SHAPE_GEAR,          PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("machine", nullptr, nullptr, nullptr, nullptr) },
    { "foundation_vent",       true,  true,  true,  false, false, SHAPE_VENT,          PLACE_PLAIN,        GLOW_NONE,  false, TEX("foundation", nullptr, nullptr, nullptr, nullptr) },
    { "ore_hopper",            true,  true,  true,  false, false, SHAPE_HOPPER,        PLACE_PLAIN,        GLOW_NONE,  false, TEX("foundation", nullptr, nullptr, nullptr, nullptr) },
    { "wood_strut",            true,  true,  true,  false, false, SHAPE_STRUT,         PLACE_FACE_PLAYER,  GLOW_NONE,  false, TEX("wood", nullptr, nullptr, nullptr, nullptr) },
    { "lattice_strut",         true,  true,  true,  false, false, SHAPE_STRUT,         PLACE_FACE_PLAYER,  GLOW_NONE,  false, TEX("custodian_lattice", nullptr, nullptr, nullptr, nullptr) },
    // Pulse logistics (Part VI): a harvester gathers pulse, pipes carry it, stores keep count, a diffuser widens The Line.
    { "pulse_harvester",       true,  true,  true,  false, true,  SHAPE_BEVEL_CUBE,         PLACE_PLAIN,        GLOW_NONE,  false, TEX("pulse_harvester_side", "pulse_harvester_top", "pulse_plate", nullptr, nullptr) },
    { "pulse_pipe",            true,  true,  true,  false, false, SHAPE_PULSE_PIPE,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("pulse_pipe", nullptr, nullptr, nullptr, nullptr) },
    { "pulse_store",           true,  true,  true,  false, true,  SHAPE_BEVEL_CUBE,         PLACE_PLAIN,        GLOW_NONE,  false, TEX("pulse_store_side", "pulse_store_top", "pulse_plate", nullptr, nullptr) },
    { "pulse_pipe_cw",         true,  true,  true,  false, false, SHAPE_PULSE_PIPE,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("pulse_pipe_cw", nullptr, nullptr, nullptr, nullptr) },
    { "pulse_pipe_ccw",        true,  true,  true,  false, false, SHAPE_PULSE_PIPE,    PLACE_CLICKED_AXIS, GLOW_NONE,  false, TEX("pulse_pipe_ccw", nullptr, nullptr, nullptr, nullptr) },
    { "pulse_diffuser",        true,  true,  true,  false, false, SHAPE_BEVEL_CUBE,    PLACE_PLAIN,        GLOW_NONE,  false, TEX("pulse_diffuser_side", "pulse_diffuser_top", "pulse_plate", nullptr, nullptr) },
};
#undef TEX

static inline bool BlockSolid(BlockID id) { return g_blocks[id].solid; }
// Solid and a full cube (collision, geometry).
static inline bool BlockFullCube(BlockID id) { return g_blocks[id].solid && g_blocks[id].shape == SHAPE_CUBE; }
static inline bool BlockIsCard(BlockID id) { return g_blocks[id].shape == SHAPE_CARD; }
// The faceted props (4.15): hull-built, anchored on the clicked face.
static inline bool ShapeIsProp(int s) { return s >= SHAPE_SWELL_MOUND && s < SHAPE_PULSE_PIPE; }
// Pulse logistics (Part VI): what a pulse pipe joins -- other pipes, and
// any face of a harvester, store, chest or machine (no face rules yet).
static inline bool BlockIsPulsePipe(BlockID id) { return id == BLOCK_PULSE_PIPE || id == BLOCK_PULSE_PIPE_CW || id == BLOCK_PULSE_PIPE_CCW; }
static inline bool BlockJoinsPipe(BlockID id) {
    return BlockIsPulsePipe(id) || id == BLOCK_PULSE_HARVESTER || id == BLOCK_PULSE_STORE || id == BLOCK_PULSE_DIFFUSER || id == BLOCK_CHEST || id == BLOCK_MACHINE;
}
// A pipe's twist, and the spin it gives pulse passing through: +1
// clockwise (seen from behind, as it travels; a right-handed screw), -1
// anticlockwise, 0 a plain pipe (which leaves pulse without spin).
static inline int PipeTwist(BlockID id) { return id == BLOCK_PULSE_PIPE_CW ? 1 : id == BLOCK_PULSE_PIPE_CCW ? -1 : 0; }
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
