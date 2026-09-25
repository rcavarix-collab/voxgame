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
