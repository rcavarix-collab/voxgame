// events.h
//
// What happened this tick (docs/ARCHITECTURE.md 1.1). The simulation never
// plays sounds or spawns sparks itself; it records events here, and the
// sound effects, music, visual effects and HUD each read the list in their
// own way. This is what lets actions feed the music in sync without tying
// combat code to audio code. Cleared every tick; a fixed-capacity vector,
// so nothing allocates once it has grown to its working size.

#pragma once

#include "common.h"
#include <vector>

enum EventKind : uint8_t {
    EV_FOOTFALL, EV_LANDING, EV_JUMP,
    EV_ROCKET_FIRED, EV_BLAST, EV_GUN_SHOT, EV_GUN_HIT_GROUND, EV_GUN_HIT_PROP,
    EV_TREE_FALLING, EV_TREE_LANDED, EV_PROP_SHATTERED, EV_PROP_CRUSHED,
    EV_FIRE_STARTED,
    EV_WEAPON_SWITCHED, EV_LOCK_ACQUIRED, EV_LOCK_LOST,
    EV_MECH_HIT, EV_MECH_DESTROYED,
    EV_TARGET_HIT, EV_TARGET_STAGE, EV_TARGET_DESTROYED,
    EV_PICKUP,
};

struct Event {
    EventKind kind;
    Vec3 pos;
    float strength;    // 0..1, or a size (blast radius) where it says so
    uint8_t detail;    // ground type for footfalls, prop kind, weapon index...
};

struct EventList {
    std::vector<Event> list;
    void Add(EventKind k, Vec3 p, float strength = 1.0f, uint8_t detail = 0) {
        if (list.size() < 4096) list.push_back({ k, p, strength, detail }); // governed: a runaway tick can't flood it
    }
    void Clear() { list.clear(); }
};
