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
    BLOCK_COUNT
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

//                      name          solid  found. place  orient data   all           top     bottom  side    front
inline const BlockDef g_blocks[BLOCK_COUNT] = {
    /* air        */ { "air",        false, false, false, false, false, nullptr,      nullptr, nullptr, nullptr, nullptr },
    /* foundation */ { "foundation", true,  true,  true,  false, false, "foundation", nullptr, nullptr, nullptr, nullptr },
    /* stone      */ { "stone",      true,  false, true,  false, false, "stone",      nullptr, nullptr, nullptr, nullptr },
    /* dirt       */ { "dirt",       true,  false, true,  false, false, "dirt",       nullptr, nullptr, nullptr, nullptr },
    /* wood       */ { "wood",       true,  false, true,  false, false, "wood",       nullptr, nullptr, nullptr, nullptr },
    /* chest      */ { "chest",      true,  true,  true,  true,  true,  "chest",      nullptr, nullptr, nullptr, "chest_front" },
    /* machine    */ { "machine",    true,  true,  true,  true,  true,  "machine",    nullptr, nullptr, nullptr, "machine_front" },
};

static inline bool BlockSolid(BlockID id) { return g_blocks[id].solid; }

// The placeable blocks in registry order: the hotbar's contents.
struct PlaceableList {
    BlockID ids[BLOCK_COUNT];
    int count;
};
static inline PlaceableList BuildPlaceableList() {
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
