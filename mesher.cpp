// mesher.cpp -- see mesher.h.

#include "mesher.h"
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
                    std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    // Solidity of the chunk plus a one-cell shell of its 26 neighbours
    // (absent neighbours read as air). Built once; every culling and AO
    // test below is then a plain array read.
    static thread_local uint8_t solid[P * P * P];
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
                solid[PIndex(px, py, pz)] = n ? (uint8_t)BlockSolid((BlockID)n->blocks[Chunk::LocalIndex(sx, sy, sz)]) : 0;
            }
        }
    }

    for (int ly = 0; ly < CHUNK_SIZE; ly++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++)
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                int li = Chunk::LocalIndex(lx, ly, lz);
                BlockID id = (BlockID)c.blocks[li];
                if (!BlockSolid(id)) continue;
                BlockFace facing = StateFacing(c.state[li]);
                int px = lx + 1, py = ly + 1, pz = lz + 1;

                for (int f = 0; f < FACE_COUNT; f++) {
                    const FaceDef& fd = kFaces[f];
                    int nx = px + fd.nx, ny = py + fd.ny, nz = pz + fd.nz; // the cell this face looks into
                    if (solid[PIndex(nx, ny, nz)]) continue;               // hidden

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
                        int side1 = solid[PIndex(nx + t1[0], ny + t1[1], nz + t1[2])];
                        int side2 = solid[PIndex(nx + t2[0], ny + t2[1], nz + t2[2])];
                        int corner = solid[PIndex(nx + t1[0] + t2[0], ny + t1[1] + t2[1], nz + t1[2] + t2[2])];
                        ao[k] = (side1 && side2) ? 0 : 3 - (side1 + side2 + corner);
                    }

                    uint16_t layer = g_blockFaceLayer[id][facing][f];
                    uint16_t base = (uint16_t)verts.size();
                    for (int k = 0; k < 4; k++) {
                        const int* cr = fd.corners[k];
                        Vertex v;
                        v.x = (uint8_t)(lx + cr[0]); v.y = (uint8_t)(ly + cr[1]); v.z = (uint8_t)(lz + cr[2]); v.pad = 0;
                        v.layer = layer;
                        v.bits = (uint16_t)(kCornerU[k] | (kCornerV[k] << 5) | (ao[k] << 10) | (f << 12));
                        verts.push_back(v);
                    }
                    // Split the quad along its brighter diagonal: splitting
                    // through a dark corner smears that darkness along the
                    // seam into both triangles (the well-known AO anisotropy).
                    if (ao[1] + ao[3] > ao[0] + ao[2]) {
                        const uint16_t q[6] = { 1, 2, 3, 1, 3, 0 };
                        for (uint16_t i : q) indices.push_back((uint16_t)(base + i));
                    } else {
                        const uint16_t q[6] = { 0, 1, 2, 0, 2, 3 };
                        for (uint16_t i : q) indices.push_back((uint16_t)(base + i));
                    }
                }
            }
}
