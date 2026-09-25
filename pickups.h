// pickups.h
//
// Things to collect (DESIGN.md §3, owner: items are allowed if governed).
// For now: armour plates the wanderer sheds as it's hurt. They tumble to
// the ground and wait; the mech collects them by walking over them. They
// aren't used yet -- the mech just keeps them.
//
// A capped pool (governed): at most POOL pieces; resting plates close
// together merge into one (carrying their count); when full, the oldest
// makes way. Pure C++, tested natively.

#pragma once

#include "common.h"
#include "events.h"
#include "prims.h"
#include <vector>

class Terrain;

enum PickupKind : uint8_t { PICK_PLATE };

struct Pickup {
    Vec3 pos, vel;
    float yaw, spin;
    uint8_t kind;
    int count;
    bool resting;
};

class Pickups {
public:
    static const int POOL = 64;
    void Reset();
    void Drop(Vec3 at, Vec3 vel, uint8_t kind, uint32_t seed);
    // Moves falling pieces; collects what's within `reach` of the mech's feet.
    // Returns how many of `kind` were collected this tick.
    int Tick(const Terrain& t, float dt, Vec3 feet, float reach, uint8_t kind, EventList& ev);
    void Mesh(std::vector<MeshVertex>& out) const;
    int Count() const { return (int)m_items.size(); }

private:
    std::vector<Pickup> m_items;
};
