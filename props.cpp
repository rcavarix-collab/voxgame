// props.cpp -- see props.h.

#include "props.h"
#include "terrain.h"
#include <algorithm>

namespace {
// Per ground type (terrain.h Ground): chance per 4 m cell of a tree, a bush, a rock.
struct Density { float tree, bush, rock; };
const Density kDensity[GROUND_COUNT] = {
    { 0.030f, 0.030f, 0.004f }, // meadow: scattered trees
    { 0.006f, 0.070f, 0.006f }, // dry grass: bushes
    { 0.140f, 0.040f, 0.006f }, // moss: forest
    { 0.012f, 0.020f, 0.003f }, // clover: open
    { 0.000f, 0.010f, 0.010f }, // loam
    { 0.000f, 0.004f, 0.020f }, // clay
    { 0.000f, 0.000f, 0.120f }, // gravel: rocks
    { 0.000f, 0.000f, 0.200f }, // rock
};
const float CELL4 = 4.0f;
}

float Prop::Height() const {
    switch (kind) {
    case PROP_PINE: return 10.6f * scale;
    case PROP_BROADLEAF: return 9.5f * scale;
    case PROP_BUSH: return 1.6f * scale;
    default: return 1.2f * scale;
    }
}
float Prop::Radius() const {
    switch (kind) {
    case PROP_PINE: return 2.2f * scale;
    case PROP_BROADLEAF: return 2.8f * scale;
    case PROP_BUSH: return 1.3f * scale;
    default: return 1.8f * scale;
    }
}

void Props::Reset(uint32_t seed) {
    m_tiles.clear();
    m_seed = seed | 1;
}

void Props::Generate(const Terrain& t, const PropTileKey& k, PropTile& tile) const {
    tile.props.clear();
    int per = (int)(TILE / CELL4);
    for (int j = 0; j < per; j++)
        for (int i = 0; i < per; i++) {
            int cx = k.x * per + i, cz = k.z * per + j;
            float x = (cx + 0.15f + 0.7f * Hash01(cx, 1, cz, m_seed)) * CELL4;
            float z = (cz + 0.15f + 0.7f * Hash01(cx, 2, cz, m_seed)) * CELL4;
            uint8_t g = t.SurfaceAt(x, z) & 0x7F;
            const Density& d = kDensity[g % GROUND_COUNT];
            float roll = Hash01(cx, 3, cz, m_seed);
            Prop p;
            p.pos = { x, t.OriginalHeight(x, z), z };
            p.yaw = Hash01(cx, 4, cz, m_seed) * 2 * kPi;
            p.seed = Hash3(cx, 5, cz, m_seed);
            p.state = PS_STANDING;
            if (roll < d.tree) {
                p.kind = Hash01(cx, 6, cz, m_seed) < (g == GROUND_MOSS ? 0.8f : 0.35f) ? PROP_PINE : PROP_BROADLEAF;
                p.scale = 0.8f + 0.5f * Hash01(cx, 7, cz, m_seed);
                p.health = 1.0f;
            } else if (roll < d.tree + d.bush) {
                p.kind = PROP_BUSH;
                p.scale = 0.7f + 0.7f * Hash01(cx, 7, cz, m_seed);
                p.health = 0.3f;
            } else if (roll < d.tree + d.bush + d.rock) {
                p.kind = PROP_ROCK;
                p.scale = 0.6f + 1.2f * Hash01(cx, 7, cz, m_seed);
                p.health = 3.0f;
            } else continue;
            tile.props.push_back(p);
        }
}

void Props::Update(const Terrain& t, Vec3 focus, float radius) {
    int fx = (int)floorf(focus.x / TILE), fz = (int)floorf(focus.z / TILE);
    int r = (int)ceilf(radius / TILE);
    for (int z = fz - r; z <= fz + r; z++)
        for (int x = fx - r; x <= fx + r; x++) {
            float dx = (x + 0.5f) * TILE - focus.x, dz = (z + 0.5f) * TILE - focus.z;
            if (dx * dx + dz * dz > (radius + TILE) * (radius + TILE)) continue;
            PropTileKey k = { x, z };
            if (m_tiles.find(k) != m_tiles.end()) continue;
            PropTile& tile = m_tiles[k];
            Generate(t, k, tile);
        }
    const float keep = radius + 2 * TILE;
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        float dx = (it->first.x + 0.5f) * TILE - focus.x, dz = (it->first.z + 0.5f) * TILE - focus.z;
        bool distant = dx * dx + dz * dz > keep * keep;
        if (distant && !it->second.modified) { it = m_tiles.erase(it); continue; }
        if (distant && !it->second.distant) { std::vector<MeshVertex>().swap(it->second.mesh); it->second.meshed = false; it->second.version++; }
        it->second.distant = distant;
        ++it;
    }
}

void Props::Tick(float dt, EventList& ev) {
    for (auto& kv : m_tiles) {
        PropTile& tile = kv.second;
        if (!tile.active) continue;
        bool still = false;
        for (Prop& p : tile.props) {
            if (p.state == PS_FALLING) {
                // A toppling trunk: angular speed grows as it leans further over.
                p.fallSpeed += (0.6f + 2.4f * sinf(p.fallAngle)) * dt;
                p.fallAngle += p.fallSpeed * dt;
                if (p.fallAngle >= 0.5f * kPi) {
                    p.fallAngle = 0.5f * kPi;
                    p.state = PS_FALLEN;
                    Vec3 lean = { sinf(p.fallYaw), 0, cosf(p.fallYaw) };
                    ev.Add(EV_TREE_LANDED, p.pos + lean * (p.Height() * 0.6f), p.scale, p.kind);
                } else still = true;
                tile.meshed = false; // animate: this tile rebuilds while the tree falls
            }
            if (p.burning) {
                p.burnTime += dt;
                if (p.burnTime > (p.IsTree() ? 22.0f : 8.0f)) {
                    p.burning = false;
                    p.charred = true;
                    if (p.kind == PROP_BUSH) p.state = PS_GONE;
                    tile.meshed = false;
                } else still = true;
            }
        }
        tile.active = still;
    }
}

bool Props::Raycast(Vec3 o, Vec3 dir, float maxDist, float& tHit, PropRef& hit) const {
    dir = Normalize(dir);
    Vec3 e = o + dir * maxDist;
    float bestT = maxDist;
    bool found = false;
    for (const auto& kv : m_tiles) {
        // Skip tiles the segment can't touch (their box, padded for crowns).
        float x0 = kv.first.x * TILE - 4, x1 = x0 + TILE + 8, z0 = kv.first.z * TILE - 4, z1 = z0 + TILE + 8;
        if (std::max(o.x, e.x) < x0 || std::min(o.x, e.x) > x1 || std::max(o.z, e.z) < z0 || std::min(o.z, e.z) > z1) continue;
        const std::vector<Prop>& ps = kv.second.props;
        for (size_t i = 0; i < ps.size(); i++) {
            const Prop& p = ps[i];
            if (p.state != PS_STANDING) continue;
            // Standing props as upright cylinders.
            float r = p.IsTree() ? p.Radius() * 0.75f : p.Radius(), h = p.Height();
            Vec3 d = o - p.pos;
            float a = dir.x * dir.x + dir.z * dir.z, b = 2 * (d.x * dir.x + d.z * dir.z), c = d.x * d.x + d.z * d.z - r * r;
            float t;
            if (c <= 0) t = 0; // starts inside the column
            else {
                if (a < 1e-8f) continue;
                float disc = b * b - 4 * a * c;
                if (disc < 0) continue;
                t = (-b - sqrtf(disc)) / (2 * a);
                if (t < 0) continue;
            }
            float y = o.y + dir.y * t;
            if (y < p.pos.y - 0.5f || y > p.pos.y + h) {
                // Through the top? Hit the cap if the ray descends into the column.
                if (dir.y < 0 && y > p.pos.y + h) {
                    float tc = (p.pos.y + h - o.y) / dir.y;
                    Vec3 q = o + dir * tc;
                    float qx = q.x - p.pos.x, qz = q.z - p.pos.z;
                    if (tc >= 0 && qx * qx + qz * qz <= r * r) t = tc; else continue;
                } else continue;
            }
            if (t < bestT) { bestT = t; hit.tile = kv.first; hit.index = (int)i; found = true; }
        }
    }
    tHit = bestT;
    return found;
}

Prop* Props::Get(const PropRef& r) {
    auto it = m_tiles.find(r.tile);
    if (it == m_tiles.end() || r.index < 0 || r.index >= (int)it->second.props.size()) return nullptr;
    return &it->second.props[r.index];
}

void Props::Damage(const PropRef& r, float amount, Vec3 from, EventList& ev) {
    auto it = m_tiles.find(r.tile);
    if (it == m_tiles.end() || r.index < 0 || r.index >= (int)it->second.props.size()) return;
    PropTile& tile = it->second;
    Prop& p = tile.props[r.index];
    if (p.state != PS_STANDING) return;
    p.health -= amount;
    if (p.health > 0) return;
    Changed(tile);
    if (p.IsTree()) {
        Vec3 away = p.pos - from;
        p.fallYaw = atan2f(away.x, away.z);
        p.state = PS_FALLING;
        p.fallSpeed = 0.3f;
        tile.active = true;
        ev.Add(EV_TREE_FALLING, p.pos, p.scale, p.kind);
    } else {
        p.state = PS_GONE;
        ev.Add(EV_PROP_SHATTERED, p.pos + Vec3{ 0, p.Height() * 0.5f, 0 }, p.scale, p.kind);
    }
}

void Props::Blast(Vec3 c, float radius, EventList& ev) {
    float reach = radius * 1.8f;
    for (auto& kv : m_tiles) {
        float x0 = kv.first.x * TILE, z0 = kv.first.z * TILE;
        if (c.x + reach < x0 || c.x - reach > x0 + TILE || c.z + reach < z0 || c.z - reach > z0 + TILE) continue;
        PropTile& tile = kv.second;
        for (size_t i = 0; i < tile.props.size(); i++) {
            Prop& p = tile.props[i];
            if (p.state == PS_GONE) continue;
            float d = Length(p.pos - c);
            if (d > reach) continue;
            if (d < radius * 0.8f) {
                // The core: nothing stands. Trees leave a stump, the rest is gone.
                Changed(tile);
                ev.Add(EV_PROP_SHATTERED, p.pos + Vec3{ 0, std::min(p.Height(), 3.0f) * 0.5f, 0 }, p.scale, p.kind);
                if (p.IsTree() && p.state != PS_STUMP) { p.state = PS_STUMP; p.burning = false; }
                else if (!p.IsTree()) p.state = PS_GONE;
                if (d < radius * 0.5f) p.state = PS_GONE; // right under it: not even a stump
            } else if (p.state == PS_STANDING) {
                float k = 1.0f - (d - radius * 0.8f) / (reach - radius * 0.8f);
                Damage({ kv.first, (int)i }, 2.0f * k, c, ev);
            }
        }
    }
}

void Props::Trample(Vec3 feet, float radius, Vec3 vel, EventList& ev) {
    float speed = Length(Vec3{ vel.x, 0, vel.z });
    if (speed < 1.5f) return; // standing still, or barely moving: nothing gives way
    for (auto& kv : m_tiles) {
        float x0 = kv.first.x * TILE, z0 = kv.first.z * TILE;
        if (feet.x + radius + 3 < x0 || feet.x - radius - 3 > x0 + TILE || feet.z + radius + 3 < z0 || feet.z - radius - 3 > z0 + TILE) continue;
        PropTile& tile = kv.second;
        for (Prop& p : tile.props) {
            if (p.state != PS_STANDING || p.kind == PROP_ROCK) continue;
            float dx = p.pos.x - feet.x, dz = p.pos.z - feet.z;
            float reach = radius + (p.IsTree() ? 0.6f * p.scale : 0.8f * p.scale);
            if (dx * dx + dz * dz > reach * reach) continue;
            Changed(tile);
            if (p.IsTree()) {
                // Pushed over the way the mech is walking.
                p.fallYaw = atan2f(vel.x, vel.z);
                p.state = PS_FALLING;
                p.fallSpeed = 0.5f;
                tile.active = true;
                ev.Add(EV_TREE_FALLING, p.pos, p.scale, p.kind);
            } else {
                p.state = PS_GONE;
                ev.Add(EV_PROP_CRUSHED, p.pos, p.scale, p.kind);
            }
        }
    }
}

int Props::Ignite(Vec3 c, float radius) {
    int caught = 0;
    for (auto& kv : m_tiles) {
        float x0 = kv.first.x * TILE, z0 = kv.first.z * TILE;
        if (c.x + radius + 3 < x0 || c.x - radius - 3 > x0 + TILE || c.z + radius + 3 < z0 || c.z - radius - 3 > z0 + TILE) continue;
        PropTile& tile = kv.second;
        for (Prop& p : tile.props) {
            if (p.kind == PROP_ROCK || p.burning || p.charred || p.state == PS_GONE) continue;
            float dx = p.pos.x - c.x, dz = p.pos.z - c.z;
            if (dx * dx + dz * dz > radius * radius) continue;
            p.burning = true;
            p.burnTime = 0;
            tile.active = true;
            Changed(tile);
            caught++;
        }
    }
    return caught;
}

void Props::BurningPositions(std::vector<Vec3>& out) const {
    for (const auto& kv : m_tiles) {
        if (!kv.second.active) continue;
        for (const Prop& p : kv.second.props)
            if (p.burning) {
                Vec3 at = p.pos + Vec3{ 0, p.state == PS_STANDING ? p.Height() * 0.55f : 0.8f, 0 };
                if (p.state == PS_FALLEN || p.state == PS_FALLING) {
                    Vec3 lean = { sinf(p.fallYaw), 0, cosf(p.fallYaw) };
                    at = p.pos + lean * (p.Height() * 0.5f) + Vec3{ 0, 0.8f, 0 };
                }
                out.push_back(at);
            }
    }
}

int Props::Count() const {
    int n = 0;
    for (const auto& kv : m_tiles) for (const Prop& p : kv.second.props) if (p.state != PS_GONE) n++;
    return n;
}

// ---- meshes ----
void Props::MeshTile(PropTile& tile) const {
    tile.mesh.clear();
    for (const Prop& p : tile.props) {
        if (p.state == PS_GONE) continue;
        Pose pose;
        pose.pos = p.pos;
        pose.yaw = p.yaw;
        pose.scale = p.scale;
        if (p.state == PS_FALLING || p.state == PS_FALLEN) { pose.tilt = p.fallAngle; pose.tiltYaw = p.fallYaw; }
        float v = 0.9f + 0.2f * Hash01((int)p.seed, 1, 2, 3);
        uint32_t bark = p.charred ? Rgba(0.020f, 0.018f, 0.016f) : Rgba(0.075f * v, 0.050f * v, 0.032f * v);
        uint32_t needles = Rgba(0.030f * v, 0.070f * v, 0.035f * v), leaves = Rgba(0.060f * v, 0.110f * v, 0.035f * v);
        uint32_t embers = Rgba(0.9f, 0.32f, 0.06f, true);
        if (p.burning) { needles = leaves = embers; }
        switch (p.kind) {
        case PROP_PINE:
            if (p.state == PS_STUMP) { PrimPrism(tile.mesh, pose, 0.45f, 0.9f, 6, bark); break; }
            PrimPrism(tile.mesh, pose, 0.42f, 3.2f, 6, bark);
            if (!p.charred) {
                PrimCone(tile.mesh, pose, 2.2f, 2.6f, 4.4f, 7, needles);
                PrimCone(tile.mesh, pose, 4.6f, 2.0f, 3.9f, 7, needles);
                PrimCone(tile.mesh, pose, 7.0f, 1.3f, 3.6f, 7, needles);
            } else PrimCone(tile.mesh, pose, 3.2f, 0.42f, 5.0f, 6, bark); // a charred spike
            break;
        case PROP_BROADLEAF:
            if (p.state == PS_STUMP) { PrimPrism(tile.mesh, pose, 0.5f, 0.9f, 6, bark); break; }
            PrimPrism(tile.mesh, pose, 0.5f, 5.0f, 6, bark);
            if (!p.charred) {
                Pose crown = pose;
                crown.pos = pose.Apply({ 0, 6.6f, 0 }); crown.tilt = 0; crown.scale = p.scale;
                if (p.state == PS_FALLING || p.state == PS_FALLEN) { crown.tilt = pose.tilt; crown.tiltYaw = pose.tiltYaw; }
                PrimRock(tile.mesh, crown, 3.1f, 0.8f, p.seed, leaves);
            } else PrimCone(tile.mesh, pose, 5.0f, 0.5f, 2.5f, 6, bark);
            break;
        case PROP_BUSH: {
            Pose b = pose; b.pos = pose.pos + Vec3{ 0, 0.5f * p.scale, 0 };
            PrimRock(tile.mesh, b, 1.2f, 0.7f, p.seed, p.burning ? embers : Rgba(0.055f * v, 0.095f * v, 0.030f * v));
            break;
        }
        case PROP_ROCK: {
            Pose r = pose; r.pos = pose.pos + Vec3{ 0, 0.35f * p.scale, 0 }; // half sunk
            PrimRock(tile.mesh, r, 1.3f, 0.65f, p.seed, Rgba(0.13f * v, 0.125f * v, 0.12f * v));
            break;
        }
        }
    }
    tile.meshed = true;
    tile.version++;
}

int Props::BuildMeshes(int budget) {
    int built = 0;
    for (auto& kv : m_tiles) {
        if (kv.second.meshed || kv.second.distant) continue;
        if (built >= budget && !kv.second.active) continue; // animating tiles always rebuild
        MeshTile(kv.second);
        built++;
    }
    return built;
}
