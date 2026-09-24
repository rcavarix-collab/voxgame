// glowlight.cpp -- see glowlight.h.

#include "glowlight.h"
#include <algorithm>
#include <cmath>

namespace {

const int N = GLOW_GRID;
// Most emitters lit at once: each costs ~2k short line traces per
// rebuild, so a field of hundreds keeps the nearest and stays cheap.
const size_t MAX_EMITTERS = 128;

inline int Cell(int x, int y, int z) { return (z * N + y) * N + x; }

// Whether the straight line between the centres of cells a and b passes
// through an opaque cell (a and b themselves not counted). Amanatides-Woo
// stepping, like block picking (4.5).
bool Blocked(const uint8_t* solid, int ax, int ay, int az, int bx, int by, int bz) {
    if (ax == bx && ay == by && az == bz) return false;
    float dx = (float)(bx - ax), dy = (float)(by - ay), dz = (float)(bz - az);
    int x = ax, y = ay, z = az;
    int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0), sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0), sz = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
    const float INF = 1e30f;
    // Parameter t runs 0..1 from centre a to centre b; boundaries sit half a cell out.
    float tdx = sx ? 1.0f / fabsf(dx) : INF, tdy = sy ? 1.0f / fabsf(dy) : INF, tdz = sz ? 1.0f / fabsf(dz) : INF;
    float tx = sx ? 0.5f * tdx : INF, ty = sy ? 0.5f * tdy : INF, tz = sz ? 0.5f * tdz : INF;
    for (;;) {
        if (tx <= ty && tx <= tz) { x += sx; tx += tdx; }
        else if (ty <= tz) { y += sy; ty += tdy; }
        else { z += sz; tz += tdz; }
        if (x == bx && y == by && z == bz) return false;
        if ((unsigned)x >= (unsigned)N || (unsigned)y >= (unsigned)N || (unsigned)z >= (unsigned)N) return false;
        if (solid[Cell(x, y, z)]) return true;
    }
}

} // namespace

void GlowGridOrigin(float x, float y, float z, int& ox, int& oy, int& oz) {
    ox = (FloorDiv16((int)floorf(x)) - 2) * CHUNK_SIZE;
    oy = (FloorDiv16((int)floorf(y)) - 2) * CHUNK_SIZE;
    oz = (FloorDiv16((int)floorf(z)) - 2) * CHUNK_SIZE;
}

void BuildGlowGrid(World& w, int ox, int oy, int oz, GlowGrid& g) {
    g.ox = ox; g.oy = oy; g.oz = oz;
    g.valid = true;
    g.emitters.clear();

    // Which cells block light, and where the glowing blocks are, read
    // straight from the 4x4x4 chunks the grid covers.
    static thread_local std::vector<uint8_t> solid;
    solid.assign((size_t)N * N * N, 0);
    const int C = N / CHUNK_SIZE;
    for (int cz = 0; cz < C; cz++)
        for (int cy = 0; cy < C; cy++)
            for (int cx = 0; cx < C; cx++) {
                const Chunk* ch = w.FindChunk({ ox / CHUNK_SIZE + cx, oy / CHUNK_SIZE + cy, oz / CHUNK_SIZE + cz });
                if (!ch) continue;
                for (int ly = 0; ly < CHUNK_SIZE; ly++)
                    for (int lz = 0; lz < CHUNK_SIZE; lz++)
                        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                            BlockID id = (BlockID)ch->blocks[Chunk::LocalIndex(lx, ly, lz)];
                            if (id == BLOCK_AIR) continue;
                            int gx = cx * CHUNK_SIZE + lx, gy = cy * CHUNK_SIZE + ly, gz = cz * CHUNK_SIZE + lz;
                            solid[Cell(gx, gy, gz)] = BlockOpaqueCube(id) ? 1 : 0;
                            BlockGlow glow = g_blocks[id].glow;
                            if (glow != GLOW_NONE)
                                g.emitters.push_back({ ox + gx, oy + gy, oz + gz, (uint8_t)(glow == GLOW_MUSIC ? 0 : (glow == GLOW_TIMESTREAM ? 1 : 2)) });
                        }
            }

    g.texels.clear();
    if (g.emitters.empty()) return;
    if (g.emitters.size() > MAX_EMITTERS) {
        float mx = ox + N * 0.5f, my = oy + N * 0.5f, mz = oz + N * 0.5f;
        auto d2 = [&](const GlowEmitter& e) { float a = e.x - mx, b = e.y - my, c = e.z - mz; return a * a + b * b + c * c; };
        std::nth_element(g.emitters.begin(), g.emitters.begin() + MAX_EMITTERS, g.emitters.end(),
                         [&](const GlowEmitter& a, const GlowEmitter& b) { return d2(a) < d2(b); });
        g.emitters.resize(MAX_EMITTERS);
    }

    // Each emitter lights every open cell it can see within its reach,
    // (1 - d/r)^2 falling off to nothing at the edge; overlapping lights add.
    g.texels.assign((size_t)N * N * N * 4, 0);
    const float reach = GLOW_RADIUS + 0.5f;
    for (const GlowEmitter& e : g.emitters) {
        int ex = e.x - ox, ey = e.y - oy, ez = e.z - oz;
        for (int z = std::max(0, ez - GLOW_RADIUS); z <= std::min(N - 1, ez + GLOW_RADIUS); z++)
            for (int y = std::max(0, ey - GLOW_RADIUS); y <= std::min(N - 1, ey + GLOW_RADIUS); y++)
                for (int x = std::max(0, ex - GLOW_RADIUS); x <= std::min(N - 1, ex + GLOW_RADIUS); x++) {
                    if (solid[Cell(x, y, z)]) continue;
                    float dx = (float)(x - ex), dy = (float)(y - ey), dz = (float)(z - ez);
                    float d = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (d >= reach) continue;
                    if (Blocked(solid.data(), ex, ey, ez, x, y, z)) continue;
                    float f = 1.0f - d / reach;
                    uint8_t& t = g.texels[(size_t)Cell(x, y, z) * 4 + e.channel];
                    t = (uint8_t)std::min(255, t + (int)(f * f * 255.0f + 0.5f));
                }
    }
}

bool ChunkAffectsGlow(const GlowGrid& g, const ChunkCoord& cc, const Chunk& c) {
    if (!g.valid) return false;
    int x0 = cc.x * CHUNK_SIZE, y0 = cc.y * CHUNK_SIZE, z0 = cc.z * CHUNK_SIZE;
    int x1 = x0 + CHUNK_SIZE, y1 = y0 + CHUNK_SIZE, z1 = z0 + CHUNK_SIZE; // exclusive
    const int R = GLOW_RADIUS + 1;
    if (x1 + R <= g.ox || x0 - R >= g.ox + N || y1 + R <= g.oy || y0 - R >= g.oy + N || z1 + R <= g.oz || z0 - R >= g.oz + N)
        return false; // nowhere near the grid
    for (const GlowEmitter& e : g.emitters)
        if (e.x >= x0 - R && e.x < x1 + R && e.y >= y0 - R && e.y < y1 + R && e.z >= z0 - R && e.z < z1 + R) return true;
    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++)
        if (g_blocks[c.blocks[i]].glow != GLOW_NONE) return true;
    return false;
}
