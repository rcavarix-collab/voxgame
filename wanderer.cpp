// wanderer.cpp -- see wanderer.h.

#include "wanderer.h"
#include "terrain.h"

namespace {
float Rand(uint64_t& s) {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return (float)((s * 2685821657736338717ull) >> 40) / (float)(1ull << 24);
}
float GroundAt(const Terrain& t, float x, float z, float from) {
    float gy;
    return t.GroundBelow(x, z, from, 200.0f, gy) ? gy : t.OriginalHeight(x, z);
}
void NewGoal(Wanderer& w) {
    float a = Rand(w.rng) * 2 * kPi, d = 30.0f + Rand(w.rng) * 90.0f;
    w.goal = w.home + Vec3{ cosf(a) * d, 0, sinf(a) * d };
}
}

void SpawnWanderer(Wanderer& w, const Terrain& t, Vec3 around, float distance) {
    uint64_t rng = w.rng ? w.rng : 1;
    float a = Rand(rng) * 2 * kPi;
    Vec3 p = around + Vec3{ cosf(a) * distance, 0, sinf(a) * distance };
    w = Wanderer();
    w.rng = rng;
    w.pos = { p.x, GroundAt(t, p.x, p.z, 120.0f), p.z };
    w.home = w.pos;
    w.yaw = Rand(w.rng) * 2 * kPi;
    w.alive = true;
    NewGoal(w);
}

void TickWanderer(Wanderer& w, const WandererTuning& tune, const Terrain& t, float dt, EventList& ev) {
    (void)ev;
    if (w.shake > 0) w.shake = std::fmax(0.0f, w.shake - dt * 3.0f);
    if (!w.alive) return;
    Vec3 to = w.goal - w.pos;
    to.y = 0;
    if (Length(to) < 6.0f) { NewGoal(w); return; }
    // Turn toward the goal at a steady rate, then walk the way it faces.
    float want = atan2f(to.x, to.z);
    float diff = remainderf(want - w.yaw, 2 * kPi);
    float step = tune.turnRate * dt;
    w.yaw += diff > step ? step : (diff < -step ? -step : diff);
    float speed = tune.speed * (fabsf(diff) > 1.0f ? 0.3f : 1.0f); // mostly turns on the spot when facing away
    Vec3 f = { sinf(w.yaw), 0, cosf(w.yaw) };
    Vec3 next = w.pos + f * (speed * dt);
    float gy = GroundAt(t, next.x, next.z, w.pos.y + 4.0f);
    if (gy > w.pos.y + 3.0f) { NewGoal(w); return; } // a wall: pick somewhere else
    w.pos = { next.x, gy, next.z };
    w.stride += speed * dt * 0.35f;
}

bool HitWanderer(const Wanderer& w, Vec3 o, Vec3 dir, float maxDist, float& tHit) {
    if (!w.alive) return false;
    dir = Normalize(dir);
    bool hit = false;
    float best = maxDist;
    // The hull: a sphere around the body.
    {
        Vec3 c = w.Center() + Vec3{ 0, 1.0f, 0 };
        float r = 4.0f;
        Vec3 d = o - c;
        float b = Dot(d, dir), cc = Dot(d, d) - r * r, disc = b * b - cc;
        if (disc >= 0) { float t = -b - sqrtf(disc); if (t < 0 && cc < 0) t = 0; if (t >= 0 && t < best) { best = t; hit = true; } }
    }
    // The legs: an upright column from the feet to the hull.
    {
        float r = 2.4f;
        Vec3 d = o - w.pos;
        float a = dir.x * dir.x + dir.z * dir.z, b = 2 * (d.x * dir.x + d.z * dir.z), c = d.x * d.x + d.z * d.z - r * r;
        if (a > 1e-8f) {
            float disc = b * b - 4 * a * c;
            if (disc >= 0) {
                float t = (-b - sqrtf(disc)) / (2 * a);
                float y = o.y + dir.y * t;
                if (t >= 0 && t < best && y >= w.pos.y && y <= w.pos.y + 8.0f) { best = t; hit = true; }
            }
        }
    }
    tHit = best;
    return hit;
}

void DamageWanderer(Wanderer& w, float amount, EventList& ev) {
    if (!w.alive || amount <= 0) return;
    w.health -= amount;
    w.shake = std::fmin(1.0f, w.shake + amount * 4.0f);
    ev.Add(EV_TARGET_HIT, w.Center(), amount);
    int stage = w.health < 0.33f ? 2 : (w.health < 0.66f ? 1 : 0);
    if (stage > w.stage) {
        w.stage = stage;
        ev.Add(EV_TARGET_STAGE, w.Center() + Vec3{ stage == 1 ? -3.0f : 3.0f, 0, 0 }, 1.0f, (uint8_t)stage);
    }
    if (w.health <= 0) {
        w.alive = false;
        w.respawnIn = 8.0f;
        ev.Add(EV_TARGET_DESTROYED, w.Center(), 1.0f);
    }
}

Vec3 WandererFeet(const Wanderer& w) { return w.pos; }

void MeshWanderer(const Wanderer& w, std::vector<MeshVertex>& out) {
    if (!w.alive) return;
    Pose body;
    body.pos = w.pos;
    body.yaw = w.yaw;
    // A jolt when hit: the whole body rocks back a little.
    body.tilt = -0.12f * w.shake;
    body.tiltYaw = w.yaw;
    uint32_t hull = Rgba(0.10f, 0.095f, 0.085f), plate = Rgba(0.16f, 0.12f, 0.07f), joint = Rgba(0.04f, 0.04f, 0.04f);
    uint32_t eye = Rgba(0.9f, 0.5f, 0.1f, true); // a steady glowing sensor
    float bob = 0.25f * fabsf(sinf(w.stride * 2 * kPi));
    // Legs: thigh and shin per side, swinging opposite each other.
    for (int side = -1; side <= 1; side += 2) {
        float swing = 0.45f * sinf(w.stride * 2 * kPi + (side > 0 ? kPi : 0));
        Pose leg = body;
        leg.pos = body.Apply({ side * 1.6f, 7.5f + bob, 0 });
        leg.tilt = swing; leg.tiltYaw = w.yaw;
        PrimBox(out, leg, { 0, -1.9f, 0 }, { 0.55f, 1.9f, 0.6f }, hull);  // thigh, hanging from the hip
        Pose shin = leg;
        shin.pos = leg.Apply({ 0, -3.8f, 0 });
        shin.tilt = swing * 0.3f;
        PrimBox(out, shin, { 0, -1.8f, 0.2f }, { 0.45f, 1.8f, 0.5f }, joint);
        PrimBox(out, shin, { 0, -3.6f, 0.5f }, { 0.8f, 0.2f, 1.2f }, hull); // a foot
    }
    // The hull, with its sensor and armour plates.
    Pose hullPose = body;
    hullPose.pos = body.Apply({ 0, 7.5f + bob, 0 });
    PrimBox(out, hullPose, { 0, 1.8f, 0 }, { 2.6f, 1.6f, 2.2f }, hull);
    PrimBox(out, hullPose, { 0, 2.2f, 2.3f }, { 0.9f, 0.4f, 0.15f }, eye);
    if (w.stage < 1) PrimBox(out, hullPose, { -2.8f, 1.8f, 0 }, { 0.25f, 1.4f, 2.0f }, plate); // left plates go first
    if (w.stage < 2) PrimBox(out, hullPose, { 2.8f, 1.8f, 0 }, { 0.25f, 1.4f, 2.0f }, plate);
    PrimBox(out, hullPose, { 0, 3.6f, -0.6f }, { 1.4f, 0.3f, 1.2f }, plate);                  // the roof
}
