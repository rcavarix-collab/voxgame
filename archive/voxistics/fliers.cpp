// fliers.cpp -- see fliers.h.

#include "fliers.h"
#include <cmath>

FlierSystem g_fliers;
FlierTuning g_flierTuning;

namespace {
const float kTwoPi = 6.2831853f;
bool IsGrass(BlockID b) { return b == BLOCK_MEADOW_GRASS; }
bool GrowsMold(BlockID b) { return b == BLOCK_DIRT || b == BLOCK_WOOD || b == BLOCK_LOG || b == BLOCK_NEW_LOG; }
} // namespace

void FlierSystem::Reset(uint64_t seed) {
    *this = FlierSystem();
    m_rng = seed | 1;
}

float FlierSystem::Rand() {
    m_rng ^= m_rng >> 12; m_rng ^= m_rng << 25; m_rng ^= m_rng >> 27;
    return (float)((m_rng * 2685821657736338717ull) >> 40) / (float)(1ull << 24);
}

// The top of the ground under (x, z), looking down from a little above y:
// its height and what it is. False if there's nothing within reach (an
// unloaded column, or a void).
bool FlierSystem::Ground(World& w, float x, float y, float z, int& gy, BlockID& top) const {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    for (int yy = (int)floorf(y) + 4; yy >= (int)floorf(y) - 24 && yy >= Y_MIN; yy--) {
        BlockID b = w.Get(ix, yy, iz);
        if (b != BLOCK_AIR && g_blocks[b].solid) { gy = yy; top = b; return true; }
    }
    return false;
}

void FlierSystem::Spawn(World& w, const FlierTuning& t, float px, float py, float pz, bool anyAge) {
    for (int attempt = 0; attempt < 6; attempt++) {
        float a = Rand() * kTwoPi, d = t.spawnNear + Rand() * (t.spawnFar - t.spawnNear);
        float x = px + cosf(a) * d, z = pz + sinf(a) * d;
        int gy; BlockID top;
        if (!Ground(w, x, py + 16.0f, z, gy, top)) continue;
        Flier f;
        f.x = x; f.z = z;
        f.cruise = t.minHeight + Rand() * (t.maxHeight - t.minHeight);
        f.y = gy + 1.0f + f.cruise;
        f.heading = Rand() * kTwoPi;
        f.hue = Rand();
        f.flapPhase = Rand() * kTwoPi;
        f.bobPhase = Rand() * kTwoPi;
        // Wild creatures come in every age, so natural deaths happen in a
        // session rather than all an hour after the world began.
        f.age = anyAge ? Rand() * 0.9f * t.lifeSeconds : 0.0f;
        m_fliers.push_back(f);
        return;
    }
}

void FlierSystem::Die(World& w, const FlierTuning& t, const Flier& f, bool byLine) {
    deaths++;
    if (byLine) deathsByLine++;
    int gy; BlockID top;
    if (!Ground(w, f.x, f.y, f.z, gy, top)) return;
    if (IsGrass(top)) {
        // The grass takes in what fell: a vivid patch that glows, then fades.
        NutrientSpot s;
        s.x = f.x; s.y = gy + 1.0f; s.z = f.z; s.hue = f.hue;
        if (m_spots.size() >= 8) m_spots.erase(m_spots.begin()); // at most eight at once: the oldest goes
        m_spots.push_back(s);
    } else if (GrowsMold(top)) {
        int ix = (int)floorf(f.x), iz = (int)floorf(f.z);
        if (w.Get(ix, gy + 1, iz) == BLOCK_AIR) { w.Set(ix, gy + 1, iz, BLOCK_MOLD_PATCH, FACE_POS_Y); molds++; }
    }
    (void)t;
}

float FlierSystem::SpotStrength(const NutrientSpot& s, const FlierTuning& t) {
    float in = s.age / 3.0f;                                 // swells in over a few seconds
    float out = (t.spotSeconds - s.age) / (0.4f * t.spotSeconds); // and fades over the last 40%
    float v = in < out ? in : out;
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

void FlierSystem::Tick(World& w, const FlierTuning& t, float px, float py, float pz, float dt, TimeRateFn timeRate) {
    for (size_t i = 0; i < m_spots.size();) {
        m_spots[i].age += dt;
        if (m_spots[i].age >= t.spotSeconds) m_spots.erase(m_spots.begin() + (long)i); else i++;
    }
    for (size_t i = 0; i < m_fliers.size();) {
        Flier& f = m_fliers[i];
        // Life runs at the local rate of time: The Line burns it fast.
        float rate = timeRate ? timeRate(f.x, f.y, f.z) : 1.0f;
        f.age += dt * rate;
        if (f.age >= t.lifeSeconds) {
            Die(w, t, f, rate > 1.5f);
            m_fliers[i] = m_fliers.back(); m_fliers.pop_back();
            continue;
        }
        float dx = f.x - px, dz = f.z - pz;
        float far = sqrtf(dx * dx + dz * dz);
        if (far > t.leaveDistance) { m_fliers[i] = m_fliers.back(); m_fliers.pop_back(); continue; } // wandered off: gone, no trace
        // Wander: the turning rate drifts, smoothly; strays too far and it
        // leans back toward the player's surroundings.
        f.turn += (Rand() - 0.5f) * 3.0f * dt;
        f.turn *= expf(-0.7f * dt);
        if (far > 0.6f * t.leaveDistance) {
            float want = atan2f(-dz, -dx), diff = remainderf(want - f.heading, kTwoPi);
            f.turn += (diff > 0 ? 0.8f : -0.8f) * dt;
        }
        f.turn = f.turn > 1.4f ? 1.4f : (f.turn < -1.4f ? -1.4f : f.turn);
        // Something solid just ahead: veer off and climb.
        float ax = f.x + cosf(f.heading) * 1.2f, az = f.z + sinf(f.heading) * 1.2f;
        bool blocked = w.Solid((int)floorf(ax), (int)floorf(f.y), (int)floorf(az));
        if (blocked) f.turn += (f.turn >= 0 ? 2.5f : -2.5f) * dt;
        f.heading = remainderf(f.heading + f.turn * dt, kTwoPi);
        f.x += cosf(f.heading) * t.speed * dt * (blocked ? 0.3f : 1.0f);
        f.z += sinf(f.heading) * t.speed * dt * (blocked ? 0.3f : 1.0f);
        // Low over the ground, bobbing gently.
        f.bobPhase += dt * 1.3f;
        int gy; BlockID top;
        float target = f.y;
        if (Ground(w, f.x, f.y + 2.0f, f.z, gy, top)) target = gy + 1.0f + f.cruise + 0.25f * sinf(f.bobPhase);
        if (blocked) target += 1.5f;
        float dy = target - f.y, maxStep = 1.8f * dt;
        f.y += dy > maxStep ? maxStep : (dy < -maxStep ? -maxStep : dy);
        if (w.Solid((int)floorf(f.x), (int)floorf(f.y), (int)floorf(f.z))) f.y = floorf(f.y) + 1.05f; // never inside a block
        f.flapPhase = fmodf(f.flapPhase + dt * kTwoPi * 4.0f, kTwoPi);
        i++;
    }
    while ((int)m_fliers.size() < t.population) {
        size_t before = m_fliers.size();
        Spawn(w, t, px, py, pz, true);
        if (m_fliers.size() == before) break; // no ground loaded out there yet: try again next tick
    }
}
