// sun.cpp -- see sun.h.

#include "sun.h"
#include "terrain.h"

Vec3 SunDirection(float dayTime) {
    // phi = 0 at 6:00 (east horizon), pi/2 at noon, pi at 18:00 (west).
    float phi = 2.0f * kPi * (dayTime / DAY_LENGTH_SECONDS) - 0.5f * kPi;
    // Tilted toward the south (-Z) so noon shadows have some length and
    // crater walls shade their north side.
    return Normalize({ cosf(phi), sinf(phi) * 0.94f, -0.34f });
}

float SunStrength(float dayTime) {
    float y = SunDirection(dayTime).y;
    float t = Clamp((y + 0.03f) / 0.18f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float SunExposure(const Terrain& terrain, const Vec3* points, int count, float dayTime, float topY) {
    float strength = SunStrength(dayTime);
    if (strength <= 0.0f || count <= 0) return 0.0f;
    Vec3 d = SunDirection(dayTime);
    int lit = 0;
    for (int i = 0; i < count; i++) {
        Vec3 p = points[i];
        bool blocked = false;
        // 2 m steps: half a terrain cell, fine enough for crater lips.
        for (int s = 1; s <= 120; s++) {
            Vec3 q = p + d * (2.0f * s);
            if (q.y > topY) break;
            if (terrain.Solid(q)) { blocked = true; break; }
        }
        if (!blocked) lit++;
    }
    return strength * (float)lit / (float)count;
}

namespace {
Vec3 Mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
}

SkyLight SkyAt(float dayTime) {
    float y = SunDirection(dayTime).y;
    float day = Clamp((y + 0.05f) / 0.35f, 0.0f, 1.0f);             // night -> full day
    float dusk = Clamp(1.0f - fabsf(y - 0.02f) / 0.22f, 0.0f, 1.0f); // near the horizon
    SkyLight s;
    s.zenith = Mix({ 0.004f, 0.006f, 0.018f }, { 0.16f, 0.32f, 0.72f }, day);
    s.horizon = Mix({ 0.010f, 0.014f, 0.030f }, { 0.52f, 0.63f, 0.80f }, day);
    s.horizon = Mix(s.horizon, { 0.80f, 0.42f, 0.20f }, dusk * 0.7f);
    s.ambient = Mix({ 0.030f, 0.040f, 0.075f }, { 0.34f, 0.40f, 0.52f }, day); // moonlit enough to move by
    s.sun = Mix({ 2.4f, 2.2f, 1.9f }, { 2.2f, 1.1f, 0.45f }, dusk) * SunStrength(dayTime);
    s.ground = s.horizon * 0.55f;
    return s;
}
