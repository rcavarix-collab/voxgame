// mech.cpp -- see mech.h.

#include "mech.h"
#include "terrain.h"

void Mech::PanelPoints(const MechTuning& t, Vec3 out[4]) const {
    Vec3 f = Flat(), r = Right();
    float h = t.eyeHeight * 0.9f;
    out[0] = pos + r * 3.0f + Vec3{ 0, h, 0 };
    out[1] = pos - r * 3.0f + Vec3{ 0, h, 0 };
    out[2] = pos - f * 2.5f + r * 1.5f + Vec3{ 0, h - 1.5f, 0 };
    out[3] = pos - f * 2.5f - r * 1.5f + Vec3{ 0, h - 1.5f, 0 };
}

void PlaceMech(Mech& m, const Terrain& terrain, float x, float z) {
    float gy = 0;
    if (!terrain.GroundBelow(x, z, 200.0f, 400.0f, gy)) gy = 0;
    m.pos = { x, gy, z };
    m.vel = { 0, 0, 0 };
    m.onGround = true;
}

namespace {
// Move `v` toward `target` by at most `step`.
Vec3 Approach(Vec3 v, Vec3 target, float step) {
    Vec3 d = target - v;
    float len = Length(d);
    if (len <= step || len < 1e-6f) return target;
    return v + d * (step / len);
}
}

void TickMech(Mech& m, const MechTuning& t, const MechInput& in, const Terrain& terrain, float sunExposure, float dt) {
    // ---- energy ----
    m.sun = sunExposure;
    if (in.toggleShield) m.shield = !m.shield;
    m.energy += t.solarCharge * sunExposure * dt;
    if (m.shield) {
        m.energy -= t.shieldDrain * dt;
        if (m.energy <= 0) { m.energy = 0; m.shield = false; } // the shield drops when the battery's flat
    }
    m.boosting = in.boost && m.energy > 0.0f && (in.forward != 0 || in.strafe != 0);
    if (m.boosting) m.energy = std::fmax(0.0f, m.energy - t.boostDrain * dt);
    m.energy = Clamp(m.energy, 0.0f, 1.0f);

    // ---- horizontal: accelerate toward the wanted velocity ----
    Vec3 wish = m.Flat() * in.forward + m.Right() * in.strafe;
    float wl = Length(wish);
    if (wl > 1.0f) wish = wish * (1.0f / wl);
    float speed = m.boosting ? t.boostSpeed : t.walkSpeed;
    Vec3 target = wish * speed;
    Vec3 horiz = { m.vel.x, 0, m.vel.z };
    float rate = (wl > 0.01f) ? (m.boosting ? t.boostAccel : t.accel) : t.brake;
    if (!m.onGround) rate *= t.airControl;
    horiz = Approach(horiz, target, rate * dt);

    // ---- jump ----
    if (in.jump && m.onGround && m.energy >= t.jumpCost) {
        m.energy -= t.jumpCost;
        m.vel.y = sqrtf(2.0f * t.gravity * t.jumpHeight);
        m.onGround = false;
    }

    // ---- move horizontally, stopped by anything taller than a step ----
    Vec3 next = m.pos + horiz * dt;
    Vec3 knee = { next.x, m.pos.y + t.stepHeight, next.z };
    if (terrain.Solid(knee)) {
        // Try each axis alone, so a wall at an angle lets the mech slide along it.
        Vec3 kx = { m.pos.x + horiz.x * dt, knee.y, m.pos.z };
        Vec3 kz = { m.pos.x, knee.y, m.pos.z + horiz.z * dt };
        bool okX = !terrain.Solid(kx), okZ = !terrain.Solid(kz);
        next = { okX ? kx.x : m.pos.x, m.pos.y, okZ ? kz.z : m.pos.z };
        if (!okX) horiz.x = 0;
        if (!okZ) horiz.z = 0;
    }
    float moved = Length(Vec3{ next.x - m.pos.x, 0, next.z - m.pos.z });
    m.pos.x = next.x;
    m.pos.z = next.z;
    m.vel.x = horiz.x;
    m.vel.z = horiz.z;

    // ---- vertical: gravity, and the ground under the new position ----
    float ground = -1e9f;
    bool hasGround = terrain.GroundBelow(m.pos.x, m.pos.z, m.pos.y + t.stepHeight, t.stepHeight + 400.0f, ground);
    if (m.onGround && hasGround && ground >= m.pos.y - t.stepHeight) {
        // Walking: follow the ground down small drops and up small steps.
        m.pos.y = ground;
        m.vel.y = 0;
    } else {
        bool wasAir = !m.onGround;
        m.onGround = false;
        m.vel.y -= t.gravity * dt;
        m.pos.y += m.vel.y * dt;
        if (hasGround && m.pos.y <= ground) {
            m.pos.y = ground;
            if (wasAir && m.vel.y < -4.0f) m.landings++;
            m.vel.y = 0;
            m.onGround = true;
        }
    }
    if (m.pos.y < -200.0f) PlaceMech(m, terrain, m.pos.x, m.pos.z); // fell out of the world: stand back up

    // ---- footfalls ----
    if (m.onGround && moved > 0) {
        m.strideLeft -= moved;
        if (m.strideLeft <= 0) { m.strideLeft += t.stride; m.footfalls++; }
    }
}
