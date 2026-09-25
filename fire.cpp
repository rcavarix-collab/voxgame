// fire.cpp -- see fire.h.

#include "fire.h"
#include "props.h"
#include "terrain.h"
#include <algorithm>

namespace {
int64_t Key(int x, int z) { return ((int64_t)x << 32) ^ (int64_t)(uint32_t)z; }
}

float Fire::Flammability(uint8_t g) {
    if (g & GROUND_SCORCHED) return 0.0f;
    switch (g & 0x7F) {
    case GROUND_DRYGRASS: return 0.9f;
    case GROUND_MEADOW: return 0.45f;
    case GROUND_CLOVER: return 0.3f;
    case GROUND_MOSS: return 0.25f;
    default: return 0.0f; // soils and rock don't burn
    }
}

void Fire::Reset(uint32_t seed) {
    m_cells.clear(); m_lit.clear(); m_burnt.clear(); m_fuel.clear();
    m_rng = seed | 1;
    m_cursor = 0;
}

float Fire::Rand() {
    m_rng ^= m_rng >> 12; m_rng ^= m_rng << 25; m_rng ^= m_rng >> 27;
    return (float)((m_rng * 2685821657736338717ull) >> 40) / (float)(1ull << 24);
}

float Fire::FuelAt(const Terrain& t, int cx, int cz) {
    int64_t k = Key(cx, cz);
    auto it = m_fuel.find(k);
    if (it != m_fuel.end()) return it->second;
    float x = (cx + 0.5f) * m_tune.cell, z = (cz + 0.5f) * m_tune.cell;
    float f = Flammability(t.GroundAt({ x, t.OriginalHeight(x, z) + 6.0f, z }));
    if (m_fuel.size() > 200000) m_fuel.clear(); // governed: the cache can't grow without bound
    m_fuel[k] = f;
    return f;
}

bool Fire::Light(const Terrain& t, int cx, int cz, const FireTuning& tune) {
    if ((int)m_cells.size() >= tune.maxBurning) return false;
    int64_t k = Key(cx, cz);
    if (m_lit.count(k) || m_burnt.count(k)) return false;
    if (FuelAt(t, cx, cz) <= 0.0f) return false;
    m_lit.insert(k);
    m_cells.push_back({ cx, cz, 0.0f, tune.burnMin + (tune.burnMax - tune.burnMin) * Rand() });
    return true;
}

int Fire::IgniteArea(const Terrain& t, Vec3 c, float radius, EventList& ev) {
    int lit = 0;
    int r = (int)ceilf(radius / m_tune.cell);
    int ccx = (int)floorf(c.x / m_tune.cell), ccz = (int)floorf(c.z / m_tune.cell);
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
            float ox = (dx) * m_tune.cell, oz = (dz) * m_tune.cell;
            if (ox * ox + oz * oz > radius * radius) continue;
            // Weak: only some of the disc catches, the rest waits for spread.
            if (Rand() < 0.45f && Light(t, ccx + dx, ccz + dz, m_tune)) lit++;
        }
    if (lit) ev.Add(EV_FIRE_STARTED, c, (float)lit);
    return lit;
}

void Fire::GroundChanged(Vec3 c, float radius) {
    int r = (int)ceilf(radius / m_tune.cell) + 1;
    int ccx = (int)floorf(c.x / m_tune.cell), ccz = (int)floorf(c.z / m_tune.cell);
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) m_fuel.erase(Key(ccx + dx, ccz + dz));
}

bool Fire::BurningAt(Vec3 p) const {
    return m_lit.count(Key((int)floorf(p.x / m_tune.cell), (int)floorf(p.z / m_tune.cell))) != 0;
}

void Fire::Tick(Terrain& t, Props& props, const FireTuning& tune, float dt, EventList& ev) {
    m_tune = tune;
    if (m_cells.empty()) {
        // Burning trees and bushes can still light the ground around them.
        std::vector<Vec3> burning;
        props.BurningPositions(burning);
        for (size_t i = 0; i < burning.size() && i < 32; i++)
            if (Rand() < 0.15f * dt) // about once every seven seconds each
                Light(t, (int)floorf(burning[i].x / tune.cell), (int)floorf(burning[i].z / tune.cell), tune);
        if (m_cells.empty()) return;
    }
    // Age every burning cell; spread from a budgeted few.
    static const int N[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    int attempts = std::min(tune.attemptsPerTick, (int)m_cells.size());
    // Each chosen cell stands in for everyone this tick, so scale the odds.
    float share = (float)m_cells.size() / (float)std::max(1, attempts);
    for (int a = 0; a < attempts; a++) {
        if (m_cursor >= m_cells.size()) m_cursor = 0;
        FireCell c = m_cells[m_cursor++]; // a copy: Light() may grow the vector
        int n = (int)(Rand() * 8.0f) & 7;
        int nx = c.x + N[n][0], nz = c.z + N[n][1];
        float f = FuelAt(t, nx, nz);
        if (f > 0 && Rand() < tune.spreadPerSecond * f * dt * 8.0f * share) Light(t, nx, nz, tune);
    }
    for (size_t i = 0; i < m_cells.size();) {
        FireCell& c = m_cells[i];
        c.age += dt;
        float x = (c.x + 0.5f) * tune.cell, z = (c.z + 0.5f) * tune.cell;
        if (c.age > 1.0f && c.age - dt <= 1.0f) props.Ignite({ x, 0, z }, tune.cell * 0.9f); // catches what stands in it
        if (c.age >= c.life) {
            t.Scorch(x, z);
            int64_t k = Key(c.x, c.z);
            m_lit.erase(k);
            m_burnt.insert(k);
            m_fuel[k] = 0.0f;
            m_cells[i] = m_cells.back(); // order doesn't matter: swap-remove
            m_cells.pop_back();
            continue;
        }
        i++;
    }
    (void)ev;
}
