// mech.h
//
// The player's mech (DESIGN.md §4): a large walker whose view is above the
// treeline. Walks, strafes, jumps and boosts across the terrain; stands on
// the ground found by marching down through its density; steps over
// anything lower than its knee and is stopped by anything higher.
//
// Energy (0..1, a full battery) is drawn by jumping, boosting and a raised
// shield, and recharges only in direct sunlight (sun.h SunExposure, fed in
// by the caller each tick). Cost per tick: a handful of density reads.
// Pure C++, tested natively.

#pragma once

#include "common.h"

class Terrain;

struct MechTuning {
    float eyeHeight = 15.0f;      // above the feet: over 8-12 m trees
    float stepHeight = 3.0f;      // walks over ledges up to this
    float walkSpeed = 10.0f;      // m/s
    float boostSpeed = 30.0f;
    float accel = 14.0f;          // m/s^2 toward the wanted speed: heavy, not sluggish
    float brake = 22.0f;
    float boostAccel = 40.0f;
    float airControl = 0.3f;      // fraction of ground acceleration in the air
    float gravity = 20.0f;        // m/s^2, a little heavy for weight
    float jumpHeight = 8.0f;      // metres at the top of a standing jump
    float jumpCost = 0.08f;       // energy per jump
    float boostDrain = 0.12f;     // energy per second of boosting
    float shieldDrain = 0.06f;    // energy per second with the shield up
    float solarCharge = 0.05f;    // energy per second in full, direct sun
    float stride = 7.0f;          // metres between footfalls
};

struct MechInput {
    float forward = 0, strafe = 0; // -1..1
    bool jump = false;             // pressed this tick
    bool boost = false;            // held
    bool toggleShield = false;     // pressed this tick
};

struct Mech {
    Vec3 pos = { 0, 0, 0 };        // feet
    Vec3 vel = { 0, 0, 0 };
    float yaw = 0, pitch = 0;      // radians; yaw 0 faces +Z, grows toward +X
    bool onGround = false;
    bool shield = false;
    bool boosting = false;
    float energy = 1.0f;
    float sun = 0.0f;              // last sun exposure, 0..1 (the charge dial)
    float strideLeft = 0.0f;       // metres until the next footfall
    int footfalls = 0;             // counts up; the game turns changes into sounds
    int landings = 0;

    Vec3 Eye(const MechTuning& t) const { return pos + Vec3{ 0, t.eyeHeight, 0 }; }
    Vec3 Forward() const { return { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) }; }
    Vec3 Flat() const { return { sinf(yaw), 0, cosf(yaw) }; }
    Vec3 Right() const { return { cosf(yaw), 0, -sinf(yaw) }; }
    // Where the solar panels are (for SunExposure): shoulders and back.
    void PanelPoints(const MechTuning& t, Vec3 out[4]) const;
};

// `sunExposure`: 0..1 from SunExposure at PanelPoints this tick.
void TickMech(Mech& m, const MechTuning& t, const MechInput& in, const Terrain& terrain, float sunExposure, float dt);
// Stand the mech on the ground at (x, z).
void PlaceMech(Mech& m, const Terrain& terrain, float x, float z);
