// mesher.h
//
// Chunk meshing (DESIGN.md 4.2): one chunk's blocks -> a packed vertex /
// index list, with hidden faces culled, per-face shading data and baked
// ambient occlusion. No D3D here (render.cpp uploads the result), so it
// is tested natively (tests/).

#pragma once

#include "world.h"
#include <cstdint>
#include <vector>

// Chunk mesh vertex, 8 bytes. Position is chunk-local (0..16 on each
// axis); the chunk's world origin comes from a per-draw constant.
// `bits`: u (5) | v (5) << 5 | ambient occlusion 0-3 (2) << 10 |
// face (3) << 12. u/v go up to 16 so merged (greedy) quads can tile a
// texture across several blocks without a format change. AO 3 = open,
// 0 = fully occluded corner.
struct Vertex {
    uint8_t x, y, z, pad;
    uint16_t layer;
    uint16_t bits;
};
static_assert(sizeof(Vertex) == 8, "chunk vertex must stay 8 bytes");

static inline int VertexU(const Vertex& v) { return v.bits & 31; }
static inline int VertexV(const Vertex& v) { return (v.bits >> 5) & 31; }
static inline int VertexAO(const Vertex& v) { return (v.bits >> 10) & 3; }
static inline int VertexFace(const Vertex& v) { return (v.bits >> 12) & 7; }

// Texture-array layer per [block][facing][face]; filled once at load
// (InitTextures, from blocktex.h). The mesher's only texture lookup.
extern uint16_t g_blockFaceLayer[BLOCK_COUNT][FACE_COUNT][FACE_COUNT];

// Builds the mesh for chunk `cc` of `w`. Reads the 26 neighbouring
// chunks once into a padded solidity grid, so no per-face hash lookups.
// 16-bit indices always suffice: the worst case (a 3D checkerboard) is
// 2048 blocks x 6 faces x 4 = 49152 vertices.
void BuildChunkMesh(World& w, const ChunkCoord& cc, const Chunk& c,
                    std::vector<Vertex>& verts, std::vector<uint16_t>& indices);
