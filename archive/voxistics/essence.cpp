// essence.cpp -- see essence.h.

#include "essence.h"
#include <algorithm>
#include <cmath>

EssenceNetwork g_essence;

namespace {

uint64_t Mix(uint64_t x) {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}
double Unit(uint64_t h) { return (double)(h >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)

long long RegionKey(int rx, int rz) { return ((long long)(uint32_t)rx << 32) | (uint32_t)rz; }

// Zones: region + index in the top bits flagged 0; attractors: position, flagged 1.
uint64_t ZoneId(int rx, int rz, int i) {
    return (((uint64_t)(uint32_t)rx & 0xFFFFFFull) << 36) | (((uint64_t)(uint32_t)rz & 0xFFFFFFull) << 12) | (uint64_t)(i & 0xFFF);
}
uint64_t AttractorId(int x, int y, int z) {
    return (1ull << 63) | (((uint64_t)(uint32_t)x & 0xFFFFFFull) << 32) | (((uint64_t)(uint32_t)z & 0xFFFFFFull) << 8) | (uint64_t)(y & 0xFF);
}

float Dist(const EssenceNode& a, const EssenceNode& b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

} // namespace

const char* EssenceBandWord(int band) {
    static const char* words[] = { "FAINT", "MINOR", "MODERATE", "STRONG", "GREAT", "VAST" };
    return words[band < 0 ? 0 : (band > 5 ? 5 : band)];
}

void EssenceNetwork::Reset(uint64_t worldSeed) {
    EssenceTuning keep = tuning;
    *this = EssenceNetwork();
    tuning = keep;
    m_seed = worldSeed;
}

std::vector<EssenceNode> EssenceNetwork::ZonesOfRegion(int rx, int rz) const {
    std::vector<EssenceNode> out;
    uint64_t h = Mix(m_seed ^ Mix(((uint64_t)(uint32_t)rx << 32) ^ (uint32_t)rz));
    int count = (int)(Unit(h) * (tuning.maxZonesPerRegion + 1));
    for (int i = 0; i < count; i++) {
        uint64_t hi = Mix(h + 0x9E3779B97F4A7C15ull * (uint64_t)(i + 1));
        EssenceNode n;
        n.id = ZoneId(rx, rz, i);
        n.kind = NodeKind::Convergence;
        n.x = (float)((rx + 0.1 + 0.8 * Unit(Mix(hi ^ 1))) * tuning.regionSize);
        n.z = (float)((rz + 0.1 + 0.8 * Unit(Mix(hi ^ 2))) * tuning.regionSize);
        n.y = 0;
        // Mostly faint and minor, a few great: 10^(4.5 u^2.2) spans 1..30000.
        double u = Unit(Mix(hi ^ 3));
        n.magnitude = pow(10.0, 4.5 * pow(u, 2.2));
        out.push_back(n);
    }
    return out;
}

void EssenceNetwork::GenerateRegion(int rx, int rz) {
    if (!m_generatedRegions.insert(RegionKey(rx, rz)).second) return;
    for (const EssenceNode& n : ZonesOfRegion(rx, rz))
        if (!m_discoveredIds.count(n.id)) m_undiscovered.push_back(n);
}

void EssenceNetwork::Discover(const EssenceNode& n) {
    if (!m_discoveredIds.insert(n.id).second) return;
    m_nodes.push_back(n);
    m_version++;
}

void EssenceNetwork::Update(float x, float z) {
    int rx = (int)floorf(x / tuning.regionSize), rz = (int)floorf(z / tuning.regionSize);
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) GenerateRegion(rx + dx, rz + dz);
    float r2 = tuning.discoverRadius * tuning.discoverRadius;
    for (size_t i = 0; i < m_undiscovered.size();) {
        float ddx = m_undiscovered[i].x - x, ddz = m_undiscovered[i].z - z;
        if (ddx * ddx + ddz * ddz <= r2) {
            Discover(m_undiscovered[i]);
            m_undiscovered[i] = m_undiscovered.back();
            m_undiscovered.pop_back();
        } else {
            i++;
        }
    }
    // Forget undiscovered zones of regions left far behind (they regenerate
    // identically if the player returns), so this list stays local.
    if (m_generatedRegions.size() > 64) {
        std::unordered_set<long long> keep;
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) keep.insert(RegionKey(rx + dx, rz + dz));
        m_generatedRegions = keep;
        m_undiscovered.erase(std::remove_if(m_undiscovered.begin(), m_undiscovered.end(), [&](const EssenceNode& n) {
            return !keep.count(RegionKey((int)floorf(n.x / tuning.regionSize), (int)floorf(n.z / tuning.regionSize)));
        }), m_undiscovered.end());
    }
}

void EssenceNetwork::AddAttractor(int x, int y, int z) {
    EssenceNode n;
    n.id = AttractorId(x, y, z);
    n.kind = NodeKind::Attractor;
    n.x = x + 0.5f; n.z = z + 0.5f; n.y = y;
    n.magnitude = 1.0; // set from what it draws, in RefreshRoutes
    Discover(n);       // the player built it: known by definition
}

void EssenceNetwork::RemoveAttractor(int x, int y, int z) {
    uint64_t id = AttractorId(x, y, z);
    if (!m_discoveredIds.erase(id)) return;
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [&](const EssenceNode& n) { return n.id == id; }), m_nodes.end());
    m_version++;
}

void EssenceNetwork::RefreshRoutes() {
    if (m_routesVersion == m_version) return;
    m_routesVersion = m_version;
    m_routes.clear();
    const EssenceTuning& t = tuning;
    int n = (int)m_nodes.size();

    // Attractors: draw from every discovered zone in reach (active routes),
    // with flow falling off with distance; their own magnitude is what
    // they draw. Zones a little further out are planned routes.
    for (int i = 0; i < n; i++) {
        EssenceNode& a = m_nodes[i];
        if (a.kind != NodeKind::Attractor) continue;
        a.magnitude = 1.0;
        for (int j = 0; j < n; j++) {
            const EssenceNode& z = m_nodes[j];
            if (z.kind != NodeKind::Convergence) continue;
            float d = Dist(a, z);
            if (d <= t.attractorReach) {
                double flow = z.magnitude * exp(-d / 32.0);
                a.magnitude += flow;
                int band = EssenceBand(flow);
                m_routes.push_back({ j, i, band >= 2 ? RouteStyle::Active : RouteStyle::Intermittent, band, false });
            } else if (d <= t.plannedReach && EssenceBand(z.magnitude) > t.minorBand) {
                m_routes.push_back({ j, i, RouteStyle::Planned, EssenceBand(z.magnitude * exp(-d / 32.0)), false });
            }
        }
    }

    // Major zones exchange essence with major neighbours; a few distant
    // pairs are entangled (bound) -- linked with no physical path between.
    for (int i = 0; i < n; i++) {
        const EssenceNode& a = m_nodes[i];
        if (a.kind != NodeKind::Convergence || EssenceBand(a.magnitude) <= t.minorBand) continue;
        for (int j = i + 1; j < n; j++) {
            const EssenceNode& b = m_nodes[j];
            if (b.kind != NodeKind::Convergence || EssenceBand(b.magnitude) <= t.minorBand) continue;
            float d = Dist(a, b);
            double flow = sqrt(a.magnitude * b.magnitude) * exp(-d / 80.0);
            int band = EssenceBand(flow);
            if (d <= t.naturalRouteReach) {
                if (band >= 1) m_routes.push_back({ i, j, band >= 2 ? RouteStyle::Active : RouteStyle::Intermittent, band, false });
            } else if (d <= t.boundMaxDist) {
                uint64_t lo = std::min(a.id, b.id), hi = std::max(a.id, b.id);
                if (Mix(m_seed ^ Mix(lo) ^ (hi * 0x9E3779B97F4A7C15ull)) % t.boundOneIn == 0) {
                    int bb = EssenceBand(sqrt(a.magnitude * b.magnitude) / 10.0);
                    m_routes.push_back({ i, j, bb >= 2 ? RouteStyle::Active : RouteStyle::Intermittent, bb, true });
                }
            }
        }
    }

    // Hierarchy (grouping metadata, never position): every node rolls up
    // into the strongest stronger node within reach; top-level otherwise.
    for (int i = 0; i < n; i++) {
        EssenceNode& a = m_nodes[i];
        a.parent = -1;
        double best = a.magnitude;
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            const EssenceNode& b = m_nodes[j];
            if (b.magnitude > best && Dist(a, b) <= t.naturalRouteReach) { best = b.magnitude; a.parent = j; }
        }
    }
}

EssenceNetwork::SaveData EssenceNetwork::Snapshot() const {
    SaveData d;
    for (const EssenceNode& n : m_nodes) {
        if (n.kind == NodeKind::Convergence) d.discoveredZones.push_back(n.id);
        else { d.attractors.push_back((int32_t)floorf(n.x)); d.attractors.push_back(n.y); d.attractors.push_back((int32_t)floorf(n.z)); }
    }
    return d;
}

void EssenceNetwork::Restore(uint64_t worldSeed, const SaveData& d) {
    Reset(worldSeed);
    // Regenerate each discovered zone from its id's region.
    std::unordered_set<uint64_t> want(d.discoveredZones.begin(), d.discoveredZones.end());
    std::unordered_set<long long> regions;
    for (uint64_t id : d.discoveredZones) {
        int rx = (int)((int32_t)(((id >> 36) & 0xFFFFFFull) << 8) >> 8); // sign-extend 24 bits
        int rz = (int)((int32_t)(((id >> 12) & 0xFFFFFFull) << 8) >> 8);
        if (!regions.insert(RegionKey(rx, rz)).second) continue;
        for (const EssenceNode& z : ZonesOfRegion(rx, rz))
            if (want.count(z.id)) Discover(z);
    }
    for (size_t i = 0; i + 2 < d.attractors.size(); i += 3) AddAttractor(d.attractors[i], d.attractors[i + 1], d.attractors[i + 2]);
}
