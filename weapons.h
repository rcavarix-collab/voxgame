// weapons.h
//
// The mech's weapons (DESIGN.md §3):
//   rockets     -- projectiles from the shoulder pods; the blast craters and
//                  holes the ground, shatters and fells props, throws
//                  debris and starts weak fires. Homing when locked.
//   machine gun -- hitscan bursts; mows down trees and bushes, harms the
//                  wanderer, kicks up dust from the ground (no craters).
// Switch with the next-weapon key or the wheel; toggle lock mode, in which
// holding the wanderer near the sights locks on after a moment.
//
// Explode() is the one blast every source shares -- rockets, the wanderer's
// death, the mech's own destruction -- so every explosion touches the world
// the same way. Rockets and tracers come from capped pools (governed).
// Pure C++, tested natively.

#pragma once

#include "common.h"
#include "events.h"
#include <vector>

class Terrain;
class Props;
class Fire;
class Debris;
struct Wanderer;
struct Mech;

enum Weapon : uint8_t { WPN_ROCKETS, WPN_GUN, WPN_COUNT };

struct WeaponTuning {
    float rocketSpeed = 120.0f;     // m/s
    float rocketTurn = 1.8f;        // rad/s when homing
    float rocketLife = 6.0f;
    float rocketCooldown = 0.75f;
    float blastRadius = 8.0f;       // metres of ground removed: mech-sized craters
    float gunRate = 12.0f;          // rounds per second
    float gunSpread = 0.010f;       // radians
    float gunRange = 650.0f;
    float gunPropDamage = 0.09f;    // a tree falls after about a dozen hits
    float gunTargetDamage = 0.012f; // the wanderer takes a long burst
    float lockCone = 0.18f;         // radians from the sights to start locking
    float keepCone = 0.45f;         // ...and to stay locked
    float lockTime = 0.9f;          // seconds to lock
};

struct Rocket { Vec3 pos, vel; float age; bool homing; };
struct Tracer { Vec3 a, b; float age; };

struct WeaponState {
    Weapon current = WPN_ROCKETS;
    float cooldown = 0;
    float gunAccum = 0;
    bool lockMode = false, locked = false;
    float lockProgress = 0, lockLost = 0;
    int nextPod = 0;                 // rockets alternate shoulders
    float recoil = 0;                // 0..1, for the view kick and the foreground guns
    std::vector<Rocket> rockets;     // capped at MAX_ROCKETS
    std::vector<Tracer> tracers;     // capped at MAX_TRACERS
    uint64_t rng = 12345;
    static const int MAX_ROCKETS = 48, MAX_TRACERS = 96;
};

struct WeaponInput {
    bool fireHeld = false;
    bool switchPressed = false;
    int wheel = 0;
    bool lockPressed = false;
};

// Everything a shot or a blast can touch.
struct World {
    Terrain* terrain;
    Props* props;
    Fire* fire;
    Debris* debris;
    Wanderer* wanderer;
    Mech* mech;
    EventList* events;
    float mechDamage = 0;           // accumulated this tick; the glue applies it
};

// The shared blast. `fromMech`: the mech's own death (it isn't hurt by it again).
void Explode(World& w, Vec3 at, float radius, bool fromMech = false);
void TickWeapons(WeaponState& s, const WeaponTuning& tune, const WeaponInput& in, Vec3 eye, Vec3 forward, Vec3 right, World& w, float dt);
