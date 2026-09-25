// weapons.cpp -- see weapons.h.

#include "weapons.h"
#include "debris.h"
#include "fire.h"
#include "mech.h"
#include "props.h"
#include "terrain.h"
#include "wanderer.h"

namespace {
float Rand(uint64_t& s) {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return (float)((s * 2685821657736338717ull) >> 40) / (float)(1ull << 24);
}

// What a segment hits first: 0 nothing, 1 ground, 2 a prop, 3 the wanderer.
struct Hit { int what = 0; float t = 0; Vec3 at = { 0, 0, 0 }; PropRef prop; };
Hit Trace(World& w, Vec3 o, Vec3 dir, float maxDist) {
    Hit h;
    float best = maxDist;
    Vec3 g;
    if (w.terrain->Raycast(o, dir, maxDist, g)) { float t = Length(g - o); if (t < best) { best = t; h.what = 1; } }
    float tp; PropRef pr;
    if (w.props->Raycast(o, dir, best, tp, pr) && tp < best) { best = tp; h.what = 2; h.prop = pr; }
    float tw;
    if (HitWanderer(*w.wanderer, o, dir, best, tw) && tw < best) { best = tw; h.what = 3; }
    h.t = best;
    h.at = o + Normalize(dir) * best;
    return h;
}

// Line of sight from `o` to `p` through the ground only (props don't block a lock).
bool Clear(const Terrain& t, Vec3 o, Vec3 p) {
    Vec3 d = p - o;
    float len = Length(d);
    Vec3 hit;
    return !t.Raycast(o, d * (1.0f / len), len - 1.0f, hit);
}
}

void Explode(World& w, Vec3 at, float radius, bool fromMech) {
    w.terrain->Blast(at, radius);
    w.props->Blast(at, radius, *w.events);
    w.fire->GroundChanged(at, radius * 1.8f);
    w.fire->IgniteArea(*w.terrain, at, radius * 1.1f, *w.events);
    uint8_t ground = w.terrain->GroundAt(at + Vec3{ 0, 2, 0 });
    uint32_t soil = (ground & 0x7F) == GROUND_CLAY ? Rgba(0.16f, 0.09f, 0.055f) : ((ground & 0x7F) == GROUND_GRAVEL ? Rgba(0.14f, 0.13f, 0.12f) : Rgba(0.11f, 0.075f, 0.045f));
    w.debris->Burst(at, 40, 22.0f, 0.9f, soil);
    w.debris->Burst(at, 8, 16.0f, 0.6f, Rgba(1.0f, 0.45f, 0.1f, true)); // glowing embers
    if (w.wanderer->alive) {
        float d = Length(w.wanderer->Center() - at);
        if (d < radius + 5.0f) DamageWanderer(*w.wanderer, 0.36f * (1.0f - d / (radius + 5.0f)) + 0.04f, *w.events);
    }
    if (!fromMech) {
        Vec3 body = w.mech->pos + Vec3{ 0, 7.0f, 0 };
        float d = Length(body - at);
        if (d < radius + 9.0f) w.mechDamage += 0.4f * (1.0f - d / (radius + 9.0f)) * (w.mech->shield ? 0.12f : 1.0f);
    }
    w.events->Add(EV_BLAST, at, radius);
}

void TickWeapons(WeaponState& s, const WeaponTuning& tune, const WeaponInput& in, Vec3 eye, Vec3 forward, Vec3 right, World& w, float dt) {
    // ---- switching ----
    int steps = (in.switchPressed ? 1 : 0) + (in.wheel != 0 ? 1 : 0);
    for (int i = 0; i < steps; i++) {
        s.current = (Weapon)((s.current + 1) % WPN_COUNT);
        s.cooldown = std::fmax(s.cooldown, 0.25f); // a moment to change over
        w.events->Add(EV_WEAPON_SWITCHED, eye, 1.0f, s.current);
    }
    s.cooldown = std::fmax(0.0f, s.cooldown - dt);
    s.recoil = std::fmax(0.0f, s.recoil - dt * 4.0f);

    // ---- lock-on ----
    if (in.lockPressed) {
        s.lockMode = !s.lockMode;
        if (!s.lockMode && s.locked) w.events->Add(EV_LOCK_LOST, eye);
        s.locked = false; s.lockProgress = 0;
    }
    if (s.lockMode && w.wanderer->alive) {
        Vec3 to = w.wanderer->Center() - eye;
        float ang = acosf(Clamp(Dot(Normalize(to), forward), -1.0f, 1.0f));
        bool seen = Clear(*w.terrain, eye, w.wanderer->Center());
        if (!s.locked) {
            if (ang < tune.lockCone && seen) {
                s.lockProgress += dt / tune.lockTime;
                if (s.lockProgress >= 1.0f) { s.locked = true; s.lockLost = 0; w.events->Add(EV_LOCK_ACQUIRED, w.wanderer->Center()); }
            } else s.lockProgress = std::fmax(0.0f, s.lockProgress - dt * 2.0f);
        } else {
            s.lockLost = (ang > tune.keepCone || !seen) ? s.lockLost + dt : 0.0f;
            if (s.lockLost > 1.0f) { s.locked = false; s.lockProgress = 0; w.events->Add(EV_LOCK_LOST, eye); }
        }
    } else if (s.locked) { s.locked = false; s.lockProgress = 0; w.events->Add(EV_LOCK_LOST, eye); }

    // ---- firing ----
    Vec3 up = Cross(forward, right);
    if (in.fireHeld && s.cooldown <= 0) {
        if (s.current == WPN_ROCKETS) {
            float side = (s.nextPod++ & 1) ? 1.0f : -1.0f;
            Rocket r;
            r.pos = eye + right * (3.2f * side) - up * 1.2f + forward * 2.0f;
            r.vel = forward * tune.rocketSpeed + Vec3{ w.mech->vel.x, 0, w.mech->vel.z };
            r.age = 0;
            r.homing = s.locked;
            if ((int)s.rockets.size() < WeaponState::MAX_ROCKETS) s.rockets.push_back(r);
            s.cooldown = tune.rocketCooldown;
            s.recoil = 1.0f;
            w.events->Add(EV_ROCKET_FIRED, r.pos, 1.0f);
        }
    }
    if (s.current == WPN_GUN && in.fireHeld && s.cooldown <= 0) {
        s.gunAccum += dt * tune.gunRate;
        while (s.gunAccum >= 1.0f) {
            s.gunAccum -= 1.0f;
            Vec3 muzzle = eye + right * 2.4f - up * 1.8f + forward * 2.5f;
            Vec3 dir = Normalize(forward + right * ((Rand(s.rng) - 0.5f) * 2 * tune.gunSpread) + up * ((Rand(s.rng) - 0.5f) * 2 * tune.gunSpread));
            Hit h = Trace(w, eye, dir, tune.gunRange); // aimed from the eye, so it lands on the sights
            if (h.what == 2) { w.props->Damage(h.prop, tune.gunPropDamage, eye, *w.events); w.events->Add(EV_GUN_HIT_PROP, h.at); }
            else if (h.what == 3) DamageWanderer(*w.wanderer, tune.gunTargetDamage, *w.events);
            else if (h.what == 1) {
                w.debris->Burst(h.at + Vec3{ 0, 0.3f, 0 }, 2, 6.0f, 0.25f, Rgba(0.10f, 0.08f, 0.05f));
                w.events->Add(EV_GUN_HIT_GROUND, h.at, 0.3f, w.terrain->GroundAt(h.at + Vec3{ 0, 1, 0 }));
            }
            Vec3 end = h.what ? h.at : eye + dir * tune.gunRange;
            if ((int)s.tracers.size() >= WeaponState::MAX_TRACERS) s.tracers.erase(s.tracers.begin());
            s.tracers.push_back({ muzzle, end, 0 });
            s.recoil = std::fmin(1.0f, s.recoil + 0.25f);
            w.events->Add(EV_GUN_SHOT, muzzle, 1.0f);
        }
    } else s.gunAccum = std::fmin(s.gunAccum, 0.99f); // the next press fires at once

    // ---- rockets in flight ----
    for (size_t i = 0; i < s.rockets.size();) {
        Rocket& r = s.rockets[i];
        r.age += dt;
        if (r.homing && w.wanderer->alive) {
            // Turn toward the target at a limited rate.
            Vec3 want = Normalize(w.wanderer->Center() - r.pos);
            Vec3 cur = Normalize(r.vel);
            float ang = acosf(Clamp(Dot(want, cur), -1.0f, 1.0f));
            float step = tune.rocketTurn * dt;
            if (ang > 1e-4f) {
                float k = ang <= step ? 1.0f : step / ang;
                cur = Normalize(cur + (want - cur) * k);
            }
            r.vel = cur * Length(r.vel);
        } else r.vel.y -= 2.0f * dt; // a slight drop unguided
        Vec3 step = r.vel * dt;
        float len = Length(step);
        Hit h = Trace(w, r.pos, step * (1.0f / len), len);
        if (h.what || r.age > tune.rocketLife) {
            Explode(w, h.what ? h.at : r.pos, tune.blastRadius);
            s.rockets[i] = s.rockets.back();
            s.rockets.pop_back();
            continue;
        }
        r.pos = r.pos + step;
        i++;
    }
    // ---- tracers fade ----
    for (size_t i = 0; i < s.tracers.size();) {
        s.tracers[i].age += dt;
        if (s.tracers[i].age > 0.06f) { s.tracers.erase(s.tracers.begin() + (long)i); continue; }
        i++;
    }
}
