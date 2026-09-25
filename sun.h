// sun.h
//
// The day (DESIGN.md §4): one hour of play from midnight to midnight. The
// sun rises in the east (+X), passes a little south of overhead and sets
// in the west; below the horizon it gives no light and no charge.
//
// SunExposure answers "is this point in direct sunlight?" for solar
// charging: a short march toward the sun through the terrain's density
// (and later, props), stopping as soon as the ray is above anything that
// could shade it. Four points at the mech's panels, ~60 steps each, per
// tick: a few hundred density reads, independent of world size.
// Pure C++, tested natively.

#pragma once

#include "common.h"

class Terrain;

static const float DAY_LENGTH_SECONDS = 3600.0f;

// Unit vector toward the sun at `dayTime` seconds (0 = midnight).
Vec3 SunDirection(float dayTime);
// 0 at night, rising through dawn to 1 in full day (a gentle ramp near the
// horizon so charge and light fade in rather than switching on).
float SunStrength(float dayTime);

// The sky's colours and light at `dayTime`, in linear light: zenith and
// horizon (the horizon doubles as the fog colour), ambient sky light, the
// sun's colour times its strength, and the haze below the horizon.
struct SkyLight { Vec3 zenith, horizon, ambient, sun, ground; };
SkyLight SkyAt(float dayTime);

// Fraction (0..1) of `count` points that see the sun, times SunStrength.
// `topY`: nothing shades above this height, so rays stop there.
float SunExposure(const Terrain& t, const Vec3* points, int count, float dayTime, float topY = 80.0f);
