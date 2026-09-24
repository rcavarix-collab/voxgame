// mesher.cpp -- see mesher.h.

#include "mesher.h"
#include "shapes.h"
#include <cstring>

uint16_t g_blockFaceLayer[BLOCK_COUNT][FACE_COUNT][FACE_COUNT];

namespace {

const int P = CHUNK_SIZE + 2; // padded edge: one cell of each neighbour around the chunk

inline int PIndex(int x, int y, int z) { return (y * P + z) * P + x; } // padded coords 0..17

struct FaceDef {
    int nx, ny, nz;          // outward normal
    int corners[4][3];       // unit-cube corners, in this order: uv (0,1) (0,0) (1,0) (1,1)
};

// Corner order gives each face's texture its correct orientation seen
// from outside: v = 0 along the top edge of side faces (row 0 of a
// texture is the sky side), u running to the viewer's right.
const FaceDef kFaces[FACE_COUNT] = {
    /* +X */ { 1, 0, 0, { {1,0,0}, {1,1,0}, {1,1,1}, {1,0,1} } },
    /* -X */ {-1, 0, 0, { {0,0,1}, {0,1,1}, {0,1,0}, {0,0,0} } },
    /* +Y */ { 0, 1, 0, { {0,1,0}, {0,1,1}, {1,1,1}, {1,1,0} } },
    /* -Y */ { 0,-1, 0, { {0,0,1}, {0,0,0}, {1,0,0}, {1,0,1} } },
    /* +Z */ { 0, 0, 1, { {1,0,1}, {1,1,1}, {0,1,1}, {0,0,1} } },
    /* -Z */ { 0, 0,-1, { {0,0,0}, {0,1,0}, {1,1,0}, {1,0,0} } },
};
const int kCornerU[4] = { 0, 0, 1, 1 };
const int kCornerV[4] = { 1, 0, 0, 1 };

} // namespace

void BuildChunkMesh(World& w, const ChunkCoord& cc, const Chunk& c,
                    std::vector<Vertex>& verts, std::vector<uint16_t>& indices,
                    size_t* translucentFirst) {
    verts.clear();
    indices.clear();
    static thread_local std::vector<uint16_t> clear; // see-through triangles, appended after the opaque ones
    clear.clear();

    // What occupies each cell -- the chunk plus a one-cell shell of its 26
    // neighbours (absent neighbours read as air): OPAQUE for a full cube
    // you can't see through, which hides any face and darkens AO; a
    // translucent cube's block ID, which hides only faces of its own kind
    // (no walls inside a pane of glass) and never darkens AO; 0 for
    // anything else (air, shaped blocks: light and sight pass). Built
    // once; every culling and AO test below is then a plain array read.
    const uint8_t OPAQUE = 255;
    static_assert(BLOCK_COUNT < 255, "cell codes need a spare value for OPAQUE");
    static thread_local uint8_t cell[P * P * P];
    const Chunk* around[3][3][3];
    for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++)
                around[dy + 1][dz + 1][dx + 1] = (dx | dy | dz) == 0 ? &c : w.FindChunk({ cc.x + dx, cc.y + dy, cc.z + dz });
    for (int py = 0; py < P; py++) {
        int ly = py - 1, oy = ly < 0 ? 0 : (ly >= CHUNK_SIZE ? 2 : 1), sy = ly - (oy - 1) * CHUNK_SIZE;
        for (int pz = 0; pz < P; pz++) {
            int lz = pz - 1, oz = lz < 0 ? 0 : (lz >= CHUNK_SIZE ? 2 : 1), sz = lz - (oz - 1) * CHUNK_SIZE;
            for (int px = 0; px < P; px++) {
                int lx = px - 1, ox = lx < 0 ? 0 : (lx >= CHUNK_SIZE ? 2 : 1), sx = lx - (ox - 1) * CHUNK_SIZE;
                const Chunk* n = around[oy][oz][ox];
                BlockID b = n ? (BlockID)n->blocks[Chunk::LocalIndex(sx, sy, sz)] : BLOCK_AIR;
                cell[PIndex(px, py, pz)] = BlockOpaqueCube(b) ? OPAQUE : (BlockFullCube(b) ? (uint8_t)b : 0);
            }
        }
    }

    // The block in a padded cell (for the few shapes that depend on their
    // neighbours: pulse pipes join what's beside them).
    auto blockAt = [&](int px, int py, int pz) -> BlockID {
        int lx = px - 1, ox = lx < 0 ? 0 : (lx >= CHUNK_SIZE ? 2 : 1), sx = lx - (ox - 1) * CHUNK_SIZE;
        int ly = py - 1, oy = ly < 0 ? 0 : (ly >= CHUNK_SIZE ? 2 : 1), sy = ly - (oy - 1) * CHUNK_SIZE;
        int lz = pz - 1, oz = lz < 0 ? 0 : (lz >= CHUNK_SIZE ? 2 : 1), sz = lz - (oz - 1) * CHUNK_SIZE;
        const Chunk* n = around[oy][oz][ox];
        return n ? (BlockID)n->blocks[Chunk::LocalIndex(sx, sy, sz)] : BLOCK_AIR;
    };

    for (int ly = 0; ly < CHUNK_SIZE; ly++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++)
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                int li = Chunk::LocalIndex(lx, ly, lz);
                BlockID id = (BlockID)c.blocks[li];
                if (id == BLOCK_AIR) continue;
                BlockFace facing = StateFacing(c.state[li]);
                int px = lx + 1, py = ly + 1, pz = lz + 1;

                if (g_blocks[id].shape == SHAPE_CARD) {
                    // A plant card (4.14): four vertices all at the cell's
                    // base centre, their corners in u/v; the vertex shader
                    // spreads them into a quad facing the viewer. The layer's
                    // top bit marks it.
                    uint16_t layer = (uint16_t)(g_blockFaceLayer[id][facing][FACE_POS_Z] | CARD_LAYER_BIT);
                    uint16_t base = (uint16_t)verts.size();
                    const uint8_t cu[4] = { 0, 0, SHAPE_UNITS, SHAPE_UNITS }, cv[4] = { SHAPE_UNITS, 0, 0, SHAPE_UNITS };
                    for (int k = 0; k < 4; k++) {
                        Vertex v;
                        v.x = (uint8_t)(lx * SHAPE_UNITS + SHAPE_UNITS / 2);
                        v.y = (uint8_t)(ly * SHAPE_UNITS);
                        v.z = (uint8_t)(lz * SHAPE_UNITS + SHAPE_UNITS / 2);
                        v.aoFace = (uint8_t)(3 | (FACE_POS_Y << 2) | (g_blocks[id].glow << 5)); // lit like an upward face
                        v.layer = layer;
                        v.u = cu[k]; v.v = cv[k];
                        verts.push_back(v);
                    }
                    const uint16_t tri[6] = { 0, 1, 2, 0, 2, 3 };
                    for (uint16_t t : tri) indices.push_back((uint16_t)(base + t));
                    continue;
                }
                if (g_blocks[id].shape != SHAPE_CUBE) {
                    // Shaped block: its oriented polygons, with any lying
                    // flat on the cell boundary hidden by a full neighbour.
                    // No AO on shapes (their faces rarely meet the grid).
                    ShapePoly polys[MAX_SHAPE_POLYS];
                    int variant = ShapeVariant(cc.x * CHUNK_SIZE + lx, cc.y * CHUNK_SIZE + ly, cc.z * CHUNK_SIZE + lz);
                    int np;
                    if (g_blocks[id].shape == SHAPE_PULSE_PIPE) {
                        BlockID nb[FACE_COUNT];
                        for (int f = 0; f < FACE_COUNT; f++) nb[f] = blockAt(px + kFaces[f].nx, py + kFaces[f].ny, pz + kFaces[f].nz);
                        np = PipePolys(PipeJoinMask(nb), c.state[li], polys, PipeTwist(id));
                    } else {
                        np = ShapePolys(g_blocks[id].shape, c.state[li], polys, variant);
                    }
                    // 16-bit indices: a chunk packed solid with the most
                    // detailed props could pass 65,536 vertices; the rest of
                    // such a chunk's props go unmeshed rather than wrap.
                    if (verts.size() + (size_t)np * 4 > 65535) continue;
                    std::vector<uint16_t>& shapeOut = g_blocks[id].translucent ? clear : indices;
                    for (int i = 0; i < np; i++) {
                        const ShapePoly& sp = polys[i];
                        if (sp.boundary >= 0) {
                            const FaceDef& bd = kFaces[sp.boundary];
                            if (cell[PIndex(px + bd.nx, py + bd.ny, pz + bd.nz)] == OPAQUE) continue;
                        }
                        uint16_t layer = g_blockFaceLayer[id][facing][sp.texFace];
                        uint16_t base = (uint16_t)verts.size();
                        for (int k = 0; k < sp.count; k++) {
                            Vertex v;
                            v.x = (uint8_t)(lx * SHAPE_UNITS + sp.v[k].x);
                            v.y = (uint8_t)(ly * SHAPE_UNITS + sp.v[k].y);
                            v.z = (uint8_t)(lz * SHAPE_UNITS + sp.v[k].z);
                            v.aoFace = (uint8_t)(3 | (sp.shade << 2) | (g_blocks[id].glow << 5));
                            v.layer = layer;
                            v.u = sp.v[k].u; v.v = sp.v[k].v;
                            verts.push_back(v);
                        }
                        const uint16_t tri[6] = { 0, 1, 2, 0, 2, 3 };
                        for (int k = 0; k < (sp.count == 4 ? 6 : 3); k++) shapeOut.push_back((uint16_t)(base + tri[k]));
                    }
                    continue;
                }

                const bool see = g_blocks[id].translucent;
                std::vector<uint16_t>& out = see ? clear : indices;
                for (int f = 0; f < FACE_COUNT; f++) {
                    const FaceDef& fd = kFaces[f];
                    int nx = px + fd.nx, ny = py + fd.ny, nz = pz + fd.nz; // the cell this face looks into
                    uint8_t beside = cell[PIndex(nx, ny, nz)];
                    if (beside == OPAQUE || (see && beside == id)) continue; // hidden

                    // Ambient occlusion per corner (the classic voxel AO):
                    // look at the two edge-neighbours and the diagonal of
                    // the open cell, on the corner's side of each tangent axis.
                    int ao[4];
                    for (int k = 0; k < 4; k++) {
                        const int* cr = fd.corners[k];
                        int s[3] = { cr[0] * 2 - 1, cr[1] * 2 - 1, cr[2] * 2 - 1 };
                        // Zero the offset along the normal axis: only tangent axes step.
                        if (fd.nx) s[0] = 0;
                        if (fd.ny) s[1] = 0;
                        if (fd.nz) s[2] = 0;
                        int t1[3] = { 0, 0, 0 }, t2[3] = { 0, 0, 0 };
                        int filled = 0;
                        for (int a = 0; a < 3; a++) {
                            if (!s[a]) continue;
                            if (filled == 0) t1[a] = s[a]; else t2[a] = s[a];
                            filled++;
                        }
                        int side1 = cell[PIndex(nx + t1[0], ny + t1[1], nz + t1[2])] == OPAQUE;
                        int side2 = cell[PIndex(nx + t2[0], ny + t2[1], nz + t2[2])] == OPAQUE;
                        int corner = cell[PIndex(nx + t1[0] + t2[0], ny + t1[1] + t2[1], nz + t1[2] + t2[2])] == OPAQUE;
                        ao[k] = (side1 && side2) ? 0 : 3 - (side1 + side2 + corner);
                    }

                    uint16_t layer = g_blockFaceLayer[id][facing][f];
                    uint16_t base = (uint16_t)verts.size();
                    for (int k = 0; k < 4; k++) {
                        const int* cr = fd.corners[k];
                        Vertex v;
                        v.x = (uint8_t)((lx + cr[0]) * SHAPE_UNITS);
                        v.y = (uint8_t)((ly + cr[1]) * SHAPE_UNITS);
                        v.z = (uint8_t)((lz + cr[2]) * SHAPE_UNITS);
                        v.aoFace = (uint8_t)(ao[k] | (f << 2) | (g_blocks[id].glow << 5));
                        v.layer = layer;
                        v.u = (uint8_t)(kCornerU[k] * SHAPE_UNITS); v.v = (uint8_t)(kCornerV[k] * SHAPE_UNITS);
                        verts.push_back(v);
                    }
                    // Split the quad along its brighter diagonal: splitting
                    // through a dark corner smears that darkness along the
                    // seam into both triangles (the well-known AO anisotropy).
                    if (ao[1] + ao[3] > ao[0] + ao[2]) {
                        const uint16_t q[6] = { 1, 2, 3, 1, 3, 0 };
                        for (uint16_t i : q) out.push_back((uint16_t)(base + i));
                    } else {
                        const uint16_t q[6] = { 0, 1, 2, 0, 2, 3 };
                        for (uint16_t i : q) out.push_back((uint16_t)(base + i));
                    }
                }
            }
    if (translucentFirst) *translucentFirst = indices.size();
    indices.insert(indices.end(), clear.begin(), clear.end());
}
