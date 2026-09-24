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

// Chunk mesh vertex, 8 bytes. Positions and texture coordinates are in
// 1/8-block fixed point (shapes.h), chunk-local: 0..128 per axis, the
// chunk's world origin coming from a per-draw constant. u/v reach 255
// (~32 blocks), so merged (greedy) quads can later tile a texture
// across several blocks with no format change. `aoFace`: ambient
// occlusion 0-3 (3 = open) in bits 0-1, shade class (BlockFace, or a
// shapes.h slope class) in bits 2-4, glow kind (blocks.h BlockGlow) in
// bits 5-7.
struct Vertex {
    uint8_t x, y, z;
    uint8_t aoFace;
    uint16_t layer;
    uint8_t u, v;
};
static_assert(sizeof(Vertex) == 8, "chunk vertex must stay 8 bytes");

static inline int VertexAO(const Vertex& v) { return v.aoFace & 3; }
static inline int VertexFace(const Vertex& v) { return (v.aoFace >> 2) & 7; }
static inline int VertexGlow(const Vertex& v) { return (v.aoFace >> 5) & 7; }

// Texture-array layer per [block][facing][face]; filled once at load
// (InitTextures, from blocktex.h). The mesher's only texture lookup.
extern uint16_t g_blockFaceLayer[BLOCK_COUNT][FACE_COUNT][FACE_COUNT];

// Builds the mesh for chunk `cc` of `w`. Reads the 26 neighbouring
// chunks once into a padded occupancy grid, so no per-face hash lookups.
// 16-bit indices always suffice: the worst case (a 3D checkerboard) is
// 2048 blocks x 6 faces x 4 = 49152 vertices.
// Opaque triangles come first in `indices`, then the see-through ones
// (translucent blocks, 4.11) from `*translucentFirst` on, so one buffer
// serves both the opaque pass and the later blended pass.
void BuildChunkMesh(World& w, const ChunkCoord& cc, const Chunk& c,
                    std::vector<Vertex>& verts, std::vector<uint16_t>& indices,
                    size_t* translucentFirst = nullptr);
