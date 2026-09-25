// terrain.cpp -- see terrain.h.

#include "terrain.h"
#include "noise.h"
#include <algorithm>
#include <cstring>

namespace {
inline int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline int Mod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

inline int8_t PackDensity(float metres) { return (int8_t)Clamp(roundf(metres * DENSITY_SCALE), -127.0f, 127.0f); }

const int PAD = 2;                 // mesher reads samples from -2 .. CHUNK+1 around its chunk
const int PN = CHUNK + 2 * PAD;    // 20
} // namespace

const char* GroundName(uint8_t g) {
    static const char* names[GROUND_COUNT] = { "meadow", "dry grass", "moss", "clover", "loam", "clay", "gravel", "rock" };
    return names[(g & 0x7F) % GROUND_COUNT];
}

void Terrain::Reset(uint32_t seed) {
    *this = Terrain();
    m_seed = seed | 1;
}

float Terrain::OriginalHeight(float, float) const {
    return m_groundY; // flat test world for now (owner); hills come back through here
}

namespace {
// Nearest and second-nearest of a grid of jittered points (Worley), with
// the cells they belong to: the skeleton of organic, irregular clumps.
struct Worley { float f1, f2; int ax, az, bx, bz; };
Worley NearestPoints(float x, float z, float cell, uint32_t seed) {
    int cx = (int)floorf(x / cell), cz = (int)floorf(z / cell);
    Worley w = { 1e9f, 1e9f, 0, 0, 0, 0 };
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            int ix = cx + dx, iz = cz + dz;
            float px = (ix + 0.1f + 0.8f * Hash01(ix, 11, iz, seed)) * cell;
            float pz = (iz + 0.1f + 0.8f * Hash01(ix, 12, iz, seed)) * cell;
            float d = sqrtf((px - x) * (px - x) + (pz - z) * (pz - z));
            if (d < w.f1) { w.f2 = w.f1; w.bx = w.ax; w.bz = w.az; w.f1 = d; w.ax = ix; w.az = iz; }
            else if (d < w.f2) { w.f2 = d; w.bx = ix; w.bz = iz; }
        }
    return w;
}
}

// The ground type at the surface of (x, z). Three layers, all warped by a
// slow noise so no edge runs straight:
//   meadows: large irregular regions (~64 m cells), one grass each, whose
//     borders interpenetrate over ~7 m instead of meeting on a line;
//   patches: smaller islands (~20 m cells) of another grass inside them;
//   bare ground: scattered soil patches (loam, clay, now and then gravel).
uint8_t Terrain::SurfaceType(float x, float z) const {
    float wx = x + 30.0f * (Fbm2(x / 55.0f, z / 55.0f, m_seed + 10) - 0.5f);
    float wz = z + 30.0f * (Fbm2(x / 55.0f, z / 55.0f, m_seed + 20) - 0.5f);
    Worley m = NearestPoints(wx, wz, 64.0f, m_seed + 30);
    uint8_t type = (uint8_t)(Hash3(m.ax, 0, m.az, m_seed + 31) % 4);
    float edge = m.f2 - m.f1; // metres from the border (roughly)
    if (edge < 7.0f && ValueNoise2(x / 3.5f, z / 3.5f, m_seed + 32) > 0.5f + 0.5f * (edge / 7.0f))
        type = (uint8_t)(Hash3(m.bx, 0, m.bz, m_seed + 31) % 4); // a tongue of the neighbour's grass
    Worley p = NearestPoints(wx + 500.0f, wz, 20.0f, m_seed + 40);
    if (Hash01(p.ax, 1, p.az, m_seed + 41) < 0.35f) {
        float r = 4.0f + 5.0f * Hash01(p.ax, 2, p.az, m_seed + 41);
        r *= 0.75f + 0.5f * ValueNoise2(x / 4.0f, z / 4.0f, m_seed + 42); // ragged
        if (p.f1 < r) type = (uint8_t)((type + 1 + Hash3(p.ax, 3, p.az, m_seed + 41) % 3) % 4);
    }
    Worley b = NearestPoints(x + 9.0f * (Fbm2(x / 13.0f, z / 13.0f, m_seed + 50) - 0.5f), z, 34.0f, m_seed + 60);
    if (Hash01(b.ax, 1, b.az, m_seed + 61) < 0.22f) {
        float r = 3.0f + 5.0f * Hash01(b.ax, 2, b.az, m_seed + 61);
        r *= 0.7f + 0.6f * ValueNoise2(x / 3.0f, z / 3.0f, m_seed + 62);
        if (b.f1 < r) {
            float k = Hash01(b.ax, 3, b.az, m_seed + 61);
            type = k < 0.5f ? GROUND_LOAM : (k < 0.8f ? GROUND_CLAY : GROUND_GRAVEL);
        }
    }
    return type;
}

// The generator: the untouched world. Ground type by depth: the surface
// type in the top layer, its soil below, rock under that.
Sample Terrain::Generate(int gx, int gy, int gz) const {
    float x = gx * CELL, y = gy * CELL, z = gz * CELL;
    float depth = OriginalHeight(x, z) - y;
    Sample s;
    s.d = PackDensity(depth);
    s.mat = GROUND_ROCK;
    if (depth <= 0.0f) { s.mat = GROUND_MEADOW; return s; } // air: type unused
    if (depth > 8.0f) return s;
    uint8_t surface = SurfaceType(x, z);
    s.mat = depth <= 1.2f * CELL ? surface : GroundSoil(surface);
    return s;
}

Sample Terrain::SampleAt(int gx, int gy, int gz) const {
    ChunkKey k = { FloorDiv(gx, CHUNK), FloorDiv(gy, CHUNK), FloorDiv(gz, CHUNK) };
    auto it = m_chunks.find(k);
    if (it != m_chunks.end() && it->second.Modified())
        return it->second.samples[((size_t)Mod(gy, CHUNK) * CHUNK + Mod(gz, CHUNK)) * CHUNK + Mod(gx, CHUNK)];
    return Generate(gx, gy, gz);
}

float Terrain::Density(Vec3 p) const {
    float fx = p.x / CELL, fy = p.y / CELL, fz = p.z / CELL;
    int ix = (int)floorf(fx), iy = (int)floorf(fy), iz = (int)floorf(fz);
    float tx = fx - ix, ty = fy - iy, tz = fz - iz;
    float c[8];
    for (int k = 0; k < 8; k++) c[k] = SampleAt(ix + (k & 1), iy + ((k >> 1) & 1), iz + (k >> 2)).d;
    float x00 = Lerp(c[0], c[1], tx), x10 = Lerp(c[2], c[3], tx), x01 = Lerp(c[4], c[5], tx), x11 = Lerp(c[6], c[7], tx);
    return Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz) / DENSITY_SCALE;
}

Vec3 Terrain::Normal(Vec3 p) const {
    const float h = 0.5f;
    Vec3 g = { Density(p - Vec3{ h, 0, 0 }) - Density(p + Vec3{ h, 0, 0 }),
               Density(p - Vec3{ 0, h, 0 }) - Density(p + Vec3{ 0, h, 0 }),
               Density(p - Vec3{ 0, 0, h }) - Density(p + Vec3{ 0, 0, h }) };
    Vec3 n = Normalize(g);
    return Length(n) > 0 ? n : kUp;
}

bool Terrain::GroundBelow(float x, float z, float fromY, float maxDrop, float& groundY) const {
    const float step = 0.5f;
    float prev = fromY;
    if (Solid({ x, fromY, z })) return false; // starting inside the ground: no floor "below"
    for (float y = fromY - step; y >= fromY - maxDrop; y -= step) {
        if (Solid({ x, y, z })) {
            float lo = y, hi = prev; // lo solid, hi air
            for (int i = 0; i < 8; i++) { float m = 0.5f * (lo + hi); if (Solid({ x, m, z })) lo = m; else hi = m; }
            groundY = 0.5f * (lo + hi);
            return true;
        }
        prev = y;
    }
    return false;
}

bool Terrain::Raycast(Vec3 o, Vec3 dir, float maxDist, Vec3& hit) const {
    dir = Normalize(dir);
    const float step = 0.5f;
    if (Solid(o)) { hit = o; return true; }
    float prev = 0;
    for (float t = step; t <= maxDist; t += step) {
        if (Solid(o + dir * t)) {
            float lo = prev, hi = t; // lo air, hi solid
            for (int i = 0; i < 8; i++) { float m = 0.5f * (lo + hi); if (Solid(o + dir * m)) hi = m; else lo = m; }
            hit = o + dir * hi;
            return true;
        }
        prev = t;
    }
    return false;
}

uint8_t Terrain::GroundAt(Vec3 p) const {
    float gy;
    if (!GroundBelow(p.x, p.z, p.y + 0.5f, 80.0f, gy)) return GROUND_ROCK;
    int ix = (int)floorf(p.x / CELL + 0.5f), iz = (int)floorf(p.z / CELL + 0.5f);
    for (int iy = (int)floorf(gy / CELL); iy >= (int)floorf(gy / CELL) - 2; iy--) {
        Sample s = SampleAt(ix, iy, iz);
        if (s.d > 0) return s.mat & 0x7F;
    }
    return GROUND_ROCK;
}

// ---------------------------------------------------------------------
// Meshing: surface nets on a padded block of samples.
// ---------------------------------------------------------------------
void Terrain::Mesh(const ChunkKey& k, TerrainChunk& c) const {
    c.verts.clear();
    c.indices.clear();
    const int bx = k.x * CHUNK, by = k.y * CHUNK, bz = k.z * CHUNK;
    // Padded samples: own chunk from its store (or the generator), the
    // rim from whichever chunk owns it.
    static thread_local Sample P[PN * PN * PN];
    auto pid = [](int x, int y, int z) { return ((y + PAD) * PN + (z + PAD)) * PN + (x + PAD); };
    bool anySolid = false, anyAir = false;
    for (int y = -PAD; y < CHUNK + PAD; y++)
        for (int z = -PAD; z < CHUNK + PAD; z++)
            for (int x = -PAD; x < CHUNK + PAD; x++) {
                Sample s;
                bool own = x >= 0 && x < CHUNK && y >= 0 && y < CHUNK && z >= 0 && z < CHUNK;
                if (own && c.Modified()) s = c.samples[((size_t)y * CHUNK + z) * CHUNK + x];
                else if (own) s = Generate(bx + x, by + y, bz + z);
                else s = SampleAt(bx + x, by + y, bz + z);
                P[pid(x, y, z)] = s;
                if (s.d > 0) anySolid = true; else anyAir = true;
            }
    c.meshed = true;
    c.version++;
    if (!anySolid || !anyAir) return;

    // One vertex per cell the surface crosses, for cells -1 .. CHUNK-1.
    const int CN = CHUNK + 1;
    static thread_local int V[(CHUNK + 1) * (CHUNK + 1) * (CHUNK + 1)];
    auto cid = [](int x, int y, int z) { return ((y + 1) * (CHUNK + 1) + (z + 1)) * (CHUNK + 1) + (x + 1); };
    static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (int y = -1; y < CHUNK; y++)
        for (int z = -1; z < CHUNK; z++)
            for (int x = -1; x < CHUNK; x++) {
                int& vi = V[cid(x, y, z)];
                vi = -1;
                float d[8];
                int solidMask = 0;
                for (int n = 0; n < 8; n++) {
                    const Sample& s = P[pid(x + (n & 1), y + ((n >> 1) & 1), z + (n >> 2))];
                    d[n] = s.d;
                    if (s.d > 0) solidMask |= 1 << n;
                }
                if (solidMask == 0 || solidMask == 255) continue;
                Vec3 acc = { 0, 0, 0 };
                int cnt = 0;
                for (auto& e : E) {
                    if (((solidMask >> e[0]) & 1) == ((solidMask >> e[1]) & 1)) continue;
                    float t = d[e[0]] / (d[e[0]] - d[e[1]]);
                    Vec3 a = { (float)(e[0] & 1), (float)((e[0] >> 1) & 1), (float)(e[0] >> 2) };
                    Vec3 b = { (float)(e[1] & 1), (float)((e[1] >> 1) & 1), (float)(e[1] >> 2) };
                    acc = acc + a + (b - a) * t;
                    cnt++;
                }
                Vec3 local = acc * (1.0f / cnt); // 0..1 within the cell
                // Hand-cut look: slide the vertex along the ground (never
                // across it, which would tilt facets and change their
                // material) by a fixed hash of the cell, staying inside it.
                {
                    int gx = bx + x, gy = by + y, gz = bz + z;
                    Vec3 j = { Hash01(gx, gy, gz, 1) - 0.5f, Hash01(gx, gy, gz, 2) - 0.5f, Hash01(gx, gy, gz, 3) - 0.5f };
                    Vec3 g = { (d[1] + d[3] + d[5] + d[7]) - (d[0] + d[2] + d[4] + d[6]),
                               (d[2] + d[3] + d[6] + d[7]) - (d[0] + d[1] + d[4] + d[5]),
                               (d[4] + d[5] + d[6] + d[7]) - (d[0] + d[1] + d[2] + d[3]) };
                    Vec3 gn = Normalize(g);
                    j = j - gn * Dot(j, gn);
                    local = local + j * 0.36f; // up to 0.18 of a cell each way
                    local = { Clamp(local.x, 0.02f, 0.98f), Clamp(local.y, 0.02f, 0.98f), Clamp(local.z, 0.02f, 0.98f) };
                }
                TerrainVertex v;
                v.x = (bx + x + local.x) * CELL; v.y = (by + y + local.y) * CELL; v.z = (bz + z + local.z) * CELL;
                // Ground type: the solid corner nearest the surface.
                int best = -1;
                for (int n = 0; n < 8; n++) if ((solidMask >> n) & 1) if (best < 0 || d[n] < d[best]) best = n;
                v.mat = P[pid(x + (best & 1), y + ((best >> 1) & 1), z + (best >> 2))].mat;
                // Openness: air among the 4x4x4 samples around the cell.
                int open = 0;
                for (int oy = -1; oy <= 2; oy++) for (int oz = -1; oz <= 2; oz++) for (int ox = -1; ox <= 2; ox++)
                    if (P[pid(x + ox, y + oy, z + oz)].d <= 0) open++;
                v.ao = (uint8_t)(Clamp(open / 32.0f, 0.0f, 1.0f) * 255.0f); // flat open ground (half air) reads as fully open
                v.pad0 = v.pad1 = 0;
                vi = (int)c.verts.size();
                c.verts.push_back(v);
            }
    (void)CN;

    // A quad for every grid edge (based in this chunk) the surface crosses.
    for (int y = 0; y < CHUNK; y++)
        for (int z = 0; z < CHUNK; z++)
            for (int x = 0; x < CHUNK; x++) {
                bool s0 = P[pid(x, y, z)].d > 0;
                for (int ax = 0; ax < 3; ax++) {
                    int ex = ax == 0, ey = ax == 1, ez = ax == 2;
                    bool s1 = P[pid(x + ex, y + ey, z + ez)].d > 0;
                    if (s0 == s1) continue;
                    int q[4];
                    if (ax == 0) { q[0] = V[cid(x, y, z)]; q[1] = V[cid(x, y - 1, z)]; q[2] = V[cid(x, y - 1, z - 1)]; q[3] = V[cid(x, y, z - 1)]; }
                    else if (ax == 1) { q[0] = V[cid(x, y, z)]; q[1] = V[cid(x - 1, y, z)]; q[2] = V[cid(x - 1, y, z - 1)]; q[3] = V[cid(x, y, z - 1)]; }
                    else { q[0] = V[cid(x, y, z)]; q[1] = V[cid(x - 1, y, z)]; q[2] = V[cid(x - 1, y - 1, z)]; q[3] = V[cid(x, y - 1, z)]; }
                    if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) continue; // can't happen for a crossing edge; guards a bad sample
                    auto P3 = [&](int i) { const TerrainVertex& t = c.verts[q[i]]; return Vec3{ t.x, t.y, t.z }; };
                    // Out = from the solid end toward the air end. Order the quad so
                    // its triangles face out (clockwise on screen when seen from outside).
                    Vec3 out = { (float)ex, (float)ey, (float)ez };
                    if (s1) out = -out;
                    if (Dot(Cross(P3(2) - P3(0), P3(3) - P3(1)), out) < 0) std::swap(q[1], q[3]);
                    Vec3 a = P3(0), b = P3(1), cc = P3(2), dd = P3(3);
                    if (Dot(a - cc, a - cc) <= Dot(b - dd, b - dd)) {
                        c.indices.insert(c.indices.end(), { (uint16_t)q[0], (uint16_t)q[1], (uint16_t)q[2], (uint16_t)q[0], (uint16_t)q[2], (uint16_t)q[3] });
                    } else {
                        c.indices.insert(c.indices.end(), { (uint16_t)q[0], (uint16_t)q[1], (uint16_t)q[3], (uint16_t)q[1], (uint16_t)q[2], (uint16_t)q[3] });
                    }
                }
            }
}

// ---------------------------------------------------------------------
// Change
// ---------------------------------------------------------------------
float Terrain::Blast(Vec3 centre, float radius) {
    const float scorch = 1.5f;          // metres of blackened ground beyond the hole
    const float rimW = 0.7f * radius;   // the lip of thrown-up ground around it...
    const float rimH = 0.22f * radius;  // ...and its height at the crest
    const float reach = radius + std::max(scorch, rimW * 1.4f) + CELL;
    int g0x = (int)floorf((centre.x - reach) / CELL), g1x = (int)ceilf((centre.x + reach) / CELL);
    int g0y = (int)floorf((centre.y - reach) / CELL), g1y = (int)ceilf((centre.y + reach) / CELL);
    int g0z = (int)floorf((centre.z - reach) / CELL), g1z = (int)ceilf((centre.z + reach) / CELL);
    g0y = std::max(g0y, CHUNK_Y_MIN * CHUNK + 1); // never through the bottom of the world
    g1y = std::min(g1y, (CHUNK_Y_MAX + 1) * CHUNK - 1);
    if (g0y > g1y) return 0.0f;
    int removed = 0;
    // Give every chunk in reach its own samples (from the generator).
    for (int cy = FloorDiv(g0y, CHUNK); cy <= FloorDiv(g1y, CHUNK); cy++)
        for (int cz = FloorDiv(g0z, CHUNK); cz <= FloorDiv(g1z, CHUNK); cz++)
            for (int cx = FloorDiv(g0x, CHUNK); cx <= FloorDiv(g1x, CHUNK); cx++) {
                TerrainChunk& ch = m_chunks[{ cx, cy, cz }];
                if (ch.Modified()) continue;
                ch.samples.resize((size_t)CHUNK * CHUNK * CHUNK);
                for (int y = 0; y < CHUNK; y++) for (int z = 0; z < CHUNK; z++) for (int x = 0; x < CHUNK; x++)
                    ch.samples[((size_t)y * CHUNK + z) * CHUNK + x] = Generate(cx * CHUNK + x, cy * CHUNK + y, cz * CHUNK + z);
            }
    for (int gy = g0y; gy <= g1y; gy++) // bottom up: a raised sample can take its soil from the one below
        for (int gz = g0z; gz <= g1z; gz++)
            for (int gx = g0x; gx <= g1x; gx++) {
                TerrainChunk& ch = m_chunks[{ FloorDiv(gx, CHUNK), FloorDiv(gy, CHUNK), FloorDiv(gz, CHUNK) }];
                Sample& s = ch.samples[((size_t)Mod(gy, CHUNK) * CHUNK + Mod(gz, CHUNK)) * CHUNK + Mod(gx, CHUNK)];
                Vec3 p = { gx * CELL, gy * CELL, gz * CELL };
                float dist = Length(p - centre);
                bool wasSolid = s.d > 0;
                if (dist < radius) {
                    int8_t carved = PackDensity(dist - radius);
                    if (carved < s.d) s.d = carved;
                    if (wasSolid && s.d <= 0) removed++;
                } else if (dist < radius + rimW && s.d > -(int)((rimH + CELL) * DENSITY_SCALE) && s.d < (int)(CELL * DENSITY_SCALE)) {
                    // The rim: ground thrown up and out, a lip that rises and falls away.
                    float h = rimH * sinf(kPi * (dist - radius) / rimW);
                    int d = s.d + (int)(h * DENSITY_SCALE);
                    s.d = (int8_t)(d > 127 ? 127 : d);
                    if (!wasSolid && s.d > 0) s.mat = SoilUnder(gx, gy, gz);
                }
                if (s.d > 0 && dist < radius + rimW * 1.4f && GroundIsGrass(s.mat) && Hash01(gx, gy, gz, m_seed + 90) < 0.75f)
                    s.mat = (uint8_t)(GroundSoil(s.mat) | (s.mat & GROUND_SCORCHED)); // thrown soil covers the grass, patchily
                if (s.d > 0 && dist < radius + scorch + CELL) s.mat |= GROUND_SCORCHED;
            }
    // Rebuild every chunk whose padded block saw a change.
    for (int cy = FloorDiv(g0y - PAD, CHUNK); cy <= FloorDiv(g1y + PAD, CHUNK); cy++) {
        if (cy < CHUNK_Y_MIN || cy > CHUNK_Y_MAX) continue;
        for (int cz = FloorDiv(g0z - PAD, CHUNK); cz <= FloorDiv(g1z + PAD, CHUNK); cz++)
            for (int cx = FloorDiv(g0x - PAD, CHUNK); cx <= FloorDiv(g1x + PAD, CHUNK); cx++) {
                m_chunks[{ cx, cy, cz }]; // resident, even if it was an empty chunk nobody stored
                Queue({ cx, cy, cz });
            }
    }
    return removed * CELL * CELL * CELL;
}

void Terrain::Scorch(float x, float z) {
    float gy;
    if (!GroundBelow(x, z, OriginalHeight(x, z) + 8.0f, 24.0f, gy)) return;
    int gx = (int)floorf(x / CELL + 0.5f), gz = (int)floorf(z / CELL + 0.5f);
    for (int iy = (int)floorf(gy / CELL); iy >= (int)floorf(gy / CELL) - 1; iy--) {
        Sample s = SampleAt(gx, iy, gz);
        if (s.d <= 0) continue;
        if (s.mat & GROUND_SCORCHED) return; // already black: nothing to rebuild
        ChunkKey k = { FloorDiv(gx, CHUNK), FloorDiv(iy, CHUNK), FloorDiv(gz, CHUNK) };
        TerrainChunk& ch = m_chunks[k];
        if (!ch.Modified()) {
            ch.samples.resize((size_t)CHUNK * CHUNK * CHUNK);
            for (int y = 0; y < CHUNK; y++) for (int zz = 0; zz < CHUNK; zz++) for (int xx = 0; xx < CHUNK; xx++)
                ch.samples[((size_t)y * CHUNK + zz) * CHUNK + xx] = Generate(k.x * CHUNK + xx, k.y * CHUNK + y, k.z * CHUNK + zz);
        }
        ch.samples[((size_t)Mod(iy, CHUNK) * CHUNK + Mod(gz, CHUNK)) * CHUNK + Mod(gx, CHUNK)].mat |= GROUND_SCORCHED;
        // This sample's material shows in its own chunk and its neighbours' rims.
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            ChunkKey n = { FloorDiv(gx + dx * PAD, CHUNK), k.y, FloorDiv(gz + dz * PAD, CHUNK) };
            if (m_chunks.count(n)) Queue(n);
        }
        return;
    }
}

uint8_t Terrain::SoilUnder(int gx, int gy, int gz) const {
    Sample below = SampleAt(gx, gy - 1, gz);
    return below.d > 0 ? GroundSoil(below.mat) : (uint8_t)GROUND_LOAM;
}

void Terrain::Queue(const ChunkKey& k) {
    TerrainChunk& c = m_chunks[k];
    c.meshed = false;
    if (!c.queued) { c.queued = true; m_queue.push_back(k); }
}

bool Terrain::ChunkHasSurface(const ChunkKey& k) const {
    // Pristine flat ground: the surface's quads are based at the grid row
    // just under it, so exactly one chunk row holds them. (Hills later:
    // the column's height range against the chunk's rows.)
    int gy = (int)floorf(m_groundY / CELL);
    return FloorDiv(gy, CHUNK) == k.y;
}

void Terrain::Update(Vec3 focus, float radius) {
    m_focus = focus;
    m_meshedThisFrame = 0;
    const float span = CHUNK * CELL;
    int fcx = (int)floorf(focus.x / span), fcz = (int)floorf(focus.z / span);
    int r = (int)ceilf(radius / span);
    for (int cz = fcz - r; cz <= fcz + r; cz++)
        for (int cx = fcx - r; cx <= fcx + r; cx++) {
            float dx = (cx + 0.5f) * span - focus.x, dz = (cz + 0.5f) * span - focus.z;
            if (dx * dx + dz * dz > (radius + span) * (radius + span)) continue;
            for (int cy = CHUNK_Y_MIN; cy <= CHUNK_Y_MAX; cy++) {
                ChunkKey k = { cx, cy, cz };
                auto it = m_chunks.find(k);
                if (it == m_chunks.end()) {
                    if (ChunkHasSurface(k)) Queue(k);
                } else if (!it->second.meshed) Queue(k);
            }
        }
    // Let go of what's far behind: pristine chunks entirely, modified ones
    // keep their samples (the change is real) but drop their mesh.
    const float keep = radius + 2 * span;
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        Vec3 mn = ChunkMin(it->first);
        float dx = mn.x + span * 0.5f - focus.x, dz = mn.z + span * 0.5f - focus.z;
        if (dx * dx + dz * dz > keep * keep && !it->second.queued) {
            if (!it->second.Modified()) { it = m_chunks.erase(it); continue; }
            if (it->second.meshed) {
                it->second.meshed = false;
                std::vector<TerrainVertex>().swap(it->second.verts);
                std::vector<uint16_t>().swap(it->second.indices);
                it->second.version++;
            }
        }
        ++it;
    }
}

int Terrain::BuildMeshes(int budget) {
    if (m_queue.empty() || budget <= 0) return 0;
    // Nearest last, so the loop pops from the back.
    Vec3 f = m_focus;
    auto dist = [&](const ChunkKey& k) {
        Vec3 c = ChunkMin(k) + Vec3{ CHUNK * CELL * 0.5f, CHUNK * CELL * 0.5f, CHUNK * CELL * 0.5f };
        Vec3 d = c - f; return Dot(d, d);
    };
    std::sort(m_queue.begin(), m_queue.end(), [&](const ChunkKey& a, const ChunkKey& b) { return dist(a) > dist(b); });
    int built = 0;
    while (!m_queue.empty() && built < budget) {
        ChunkKey k = m_queue.back();
        m_queue.pop_back();
        auto it = m_chunks.find(k);
        if (it == m_chunks.end()) it = m_chunks.emplace(k, TerrainChunk()).first;
        it->second.queued = false;
        Mesh(k, it->second);
        built++;
    }
    m_meshedThisFrame += built;
    return built;
}

TerrainStats Terrain::Stats() const {
    TerrainStats s;
    for (auto& kv : m_chunks) {
        s.resident++;
        if (kv.second.Modified()) s.modified++;
        s.triangles += (long long)kv.second.indices.size() / 3;
    }
    s.meshedThisFrame = m_meshedThisFrame;
    s.waiting = (int)m_queue.size();
    return s;
}
