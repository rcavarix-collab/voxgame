// pickups.cpp -- see pickups.h.

#include "pickups.h"
#include "terrain.h"

void Pickups::Reset() { m_items.clear(); m_items.reserve(POOL); }

void Pickups::Drop(Vec3 at, Vec3 vel, uint8_t kind, uint32_t seed) {
    Pickup p;
    p.pos = at; p.vel = vel;
    p.yaw = Hash01((int)seed, 1, 2, 3) * 6.28f;
    p.spin = (Hash01((int)seed, 4, 5, 6) - 0.5f) * 6.0f;
    p.kind = kind; p.count = 1; p.resting = false;
    if ((int)m_items.size() >= POOL) m_items.erase(m_items.begin()); // full: the oldest makes way
    m_items.push_back(p);
}

int Pickups::Tick(const Terrain& t, float dt, Vec3 feet, float reach, uint8_t kind, EventList& ev) {
    int got = 0;
    for (size_t i = 0; i < m_items.size();) {
        Pickup& p = m_items[i];
        if (!p.resting) {
            p.vel.y -= 20.0f * dt;
            Vec3 next = p.pos + p.vel * dt;
            if (t.Solid(next)) {
                p.vel = { p.vel.x * 0.4f, -p.vel.y * 0.25f, p.vel.z * 0.4f };
                if (Length(p.vel) < 1.0f) {
                    p.resting = true;
                    // Lying near another resting plate: merge, so a pile is one item.
                    for (size_t j = 0; j < m_items.size(); j++) {
                        if (j == i || !m_items[j].resting || m_items[j].kind != p.kind) continue;
                        if (Length(m_items[j].pos - p.pos) < 2.5f) { m_items[j].count += p.count; p.count = 0; break; }
                    }
                }
            } else p.pos = next;
            p.yaw += p.spin * dt;
        }
        float dx = p.pos.x - feet.x, dz = p.pos.z - feet.z;
        bool collect = p.count > 0 && p.kind == kind && dx * dx + dz * dz < reach * reach && p.pos.y < feet.y + 6.0f && p.pos.y > feet.y - 4.0f;
        if (collect) { got += p.count; ev.Add(EV_PICKUP, p.pos, (float)p.count, p.kind); }
        if (collect || p.count == 0) { m_items[i] = m_items.back(); m_items.pop_back(); continue; }
        i++;
    }
    return got;
}

void Pickups::Mesh(std::vector<MeshVertex>& out) const {
    for (const Pickup& p : m_items) {
        Pose pose;
        pose.pos = p.pos + Vec3{ 0, 0.25f, 0 };
        pose.yaw = p.yaw;
        uint32_t plate = Rgba(0.16f, 0.12f, 0.07f);
        PrimBox(out, pose, { 0, 0, 0 }, { 1.4f, 0.22f, 1.0f }, plate);
        if (p.count > 1) PrimBox(out, pose, { 0.2f, 0.45f, 0.1f }, { 1.3f, 0.2f, 0.95f }, plate); // a pile looks like one
    }
}
