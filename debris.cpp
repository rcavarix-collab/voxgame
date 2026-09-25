// debris.cpp -- see debris.h.

#include "debris.h"
#include "terrain.h"

void Debris::Reset(uint32_t seed) {
    m_pool.clear();
    m_pool.reserve(POOL);
    m_next = 0;
    m_rng = seed | 1;
}

float Debris::Rand() {
    m_rng ^= m_rng >> 12; m_rng ^= m_rng << 25; m_rng ^= m_rng >> 27;
    return (float)((m_rng * 2685821657736338717ull) >> 40) / (float)(1ull << 24);
}

void Debris::Burst(Vec3 at, int count, float speed, float size, uint32_t rgba) {
    for (int i = 0; i < count; i++) {
        Shard s;
        float a = Rand() * 2 * kPi, up = 0.35f + 0.65f * Rand();
        float sp = speed * (0.4f + 0.8f * Rand());
        s.pos = at + Vec3{ (Rand() - 0.5f), Rand() * 0.5f, (Rand() - 0.5f) };
        s.vel = Vec3{ cosf(a) * (1 - up * 0.6f), up, sinf(a) * (1 - up * 0.6f) } * sp;
        s.spinAxis = Normalize({ Rand() - 0.5f, Rand() - 0.5f, Rand() - 0.5f });
        if (Length(s.spinAxis) < 0.5f) s.spinAxis = { 0, 1, 0 };
        s.angle = Rand() * 6.28f;
        s.spin = (Rand() - 0.5f) * 14.0f;
        s.size = size * (0.5f + Rand());
        s.life = 4.0f + 3.0f * Rand();
        s.age = 0;
        s.rgba = rgba;
        s.resting = false;
        s.hitMech = false;
        if ((int)m_pool.size() < POOL) m_pool.push_back(s);
        else { m_pool[m_next] = s; m_next = (m_next + 1) % POOL; } // full: the oldest makes way
    }
}

float Debris::Tick(const Terrain& t, float dt, Vec3 feet, float height, float radius) {
    float damage = 0;
    for (size_t i = 0; i < m_pool.size();) {
        Shard& s = m_pool[i];
        s.age += dt;
        if (s.age >= s.life) {
            // Swap-remove; keep the recycling cursor on a live slot.
            m_pool[i] = m_pool.back();
            m_pool.pop_back();
            if (m_next >= m_pool.size()) m_next = 0;
            continue;
        }
        if (!s.resting) {
            s.vel.y -= 20.0f * dt;
            Vec3 next = s.pos + s.vel * dt;
            if (t.Solid(next)) {
                // Bounce: lose most of the fall, some of the slide; rest when slow.
                s.vel = { s.vel.x * 0.55f, -s.vel.y * 0.3f, s.vel.z * 0.55f };
                s.spin *= 0.6f;
                if (Length(s.vel) < 1.5f) s.resting = true;
            } else s.pos = next;
            s.angle += s.spin * dt;
            // Fast pieces striking the mech's body.
            float speed = Length(s.vel);
            if (!s.hitMech && speed > 12.0f && s.pos.y > feet.y && s.pos.y < feet.y + height) {
                float dx = s.pos.x - feet.x, dz = s.pos.z - feet.z;
                if (dx * dx + dz * dz < radius * radius) { damage += 0.004f * speed * s.size; s.hitMech = true; }
            }
        }
        i++;
    }
    return damage;
}

void Debris::Mesh(std::vector<MeshVertex>& out) const {
    for (const Shard& s : m_pool) {
        float fade = s.life - s.age < 1.0f ? (s.life - s.age) : 1.0f; // shrink away in the last second
        float r = s.size * fade;
        // A tumbling tetrahedron: four corners turned about the spin axis.
        Vec3 c[4] = { { 1, 1, 1 }, { -1, -1, 1 }, { -1, 1, -1 }, { 1, -1, -1 } };
        float ca = cosf(s.angle), sa = sinf(s.angle);
        for (Vec3& v : c) {
            v = v * (r * 0.6f);
            Vec3 k = s.spinAxis;
            v = v * ca + Cross(k, v) * sa + k * (Dot(k, v) * (1 - ca)); // Rodrigues
            v = v + s.pos;
        }
        static const int F[4][3] = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 } };
        for (auto& f : F) {
            Vec3 a = c[f[0]], b = c[f[1]], d = c[f[2]];
            if (Dot(Cross(b - a, d - a), a - s.pos) < 0) std::swap(b, d);
            out.push_back({ a.x, a.y, a.z, s.rgba });
            out.push_back({ b.x, b.y, b.z, s.rgba });
            out.push_back({ d.x, d.y, d.z, s.rgba });
        }
    }
}

int Debris::Live() const { return (int)m_pool.size(); }
