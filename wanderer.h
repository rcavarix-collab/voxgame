// wanderer.h
//
// The practice target (DESIGN.md §9): a slow two-legged walker, a little
// shorter than the mech, roaming around its home ground. It doesn't fire
// back. It tramples trees in its way, sheds armour plates in stages as it's
// hurt, and explodes when destroyed, then walks in again from elsewhere
// after a pause. One of them; a handful of primitive boxes to draw.
// Pure C++, tested natively.

#pragma once

#include "common.h"
#include "events.h"
#include "prims.h"
#include <vector>

class Terrain;

struct Wanderer {
    Vec3 pos = { 0, 0, 0 };  // feet
    Vec3 home = { 0, 0, 0 }, goal = { 0, 0, 0 };
    float yaw = 0;
    float health = 1.0f;
    int stage = 0;           // 0 whole, 1 left plates gone, 2 right plates gone
    bool alive = false;
    float respawnIn = 0;
    float stride = 0;        // walk cycle phase
    float shake = 0;         // brief recoil when hit (a jolt of the body, never a flash)
    uint64_t rng = 1;

    Vec3 Center() const { return pos + Vec3{ 0, 9.0f, 0 }; }
};

struct WandererTuning {
    float speed = 3.0f;      // m/s: slow, readable, huntable
    float turnRate = 0.5f;   // rad/s
    float roam = 120.0f;     // metres from home it wanders
    float respawnDelay = 8.0f;
};

void SpawnWanderer(Wanderer& w, const Terrain& t, Vec3 around, float distance);
void TickWanderer(Wanderer& w, const WandererTuning& tune, const Terrain& t, float dt, EventList& ev);
// Segment test against the body (a tall capsule plus the hull); t along `dir`.
bool HitWanderer(const Wanderer& w, Vec3 o, Vec3 dir, float maxDist, float& t);
void DamageWanderer(Wanderer& w, float amount, EventList& ev);
void MeshWanderer(const Wanderer& w, std::vector<MeshVertex>& out);
// Its feet, for trampling trees.
Vec3 WandererFeet(const Wanderer& w);
