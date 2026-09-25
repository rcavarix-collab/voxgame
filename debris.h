// debris.h
//
// What blasts, shattered props and a dying wanderer throw about (DESIGN.md
// §3): shards, splinters and clods that fly, tumble, bounce off the ground
// and fade. Fast pieces hurt the mech unless its shield is up.
//
// A fixed pool (governed, per the owner's rule on entities): at most
// POOL pieces; when full, the oldest is recycled. Each tick moves every
// live piece and tests it against the ground with one density read, so
// cost is bounded by the pool size, never by what happened. Pure C++,
// tested natively.

#pragma once

#include "common.h"
#include "prims.h"
#include <vector>

class Terrain;

struct Shard {
    Vec3 pos, vel, spinAxis;
    float angle, spin, size, life, age;
    uint32_t rgba;
    bool resting, hitMech;
};

class Debris {
public:
    static const int POOL = 1024;
    void Reset(uint32_t seed);
    // Throw `count` pieces from `at`, outward and up at about `speed` m/s.
    void Burst(Vec3 at, int count, float speed, float size, uint32_t rgba);
    // Moves the pieces. Returns damage dealt to a mech standing at `feet`
    // (height `height`, radius `radius`) by pieces moving faster than a toss.
    float Tick(const Terrain& t, float dt, Vec3 feet, float height, float radius);
    void Mesh(std::vector<MeshVertex>& out) const;
    int Live() const;

private:
    float Rand();
    std::vector<Shard> m_pool;
    size_t m_next = 0;
    uint64_t m_rng = 1;
};
