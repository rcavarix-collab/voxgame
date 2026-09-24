// sky.h
//
// The day/night model (DESIGN.md Part XIII): where the sun and moon are,
// how bright the day is, and how far the star field has turned -- all
// pure functions of the one day clock, so the sky, world lighting and
// shadows can never disagree with each other or with the music (whose
// sections they line up with: sunrise as Dawn begins, sunset inside
// Dusk, ten minutes of real night). Header-only, no D3D; tested natively.

#pragma once

#include "world.h" // DAY_LENGTH_SECONDS

struct SkyState {
    Vec3 sunDir;      // unit vector toward the sun (below the horizon at night)
    Vec3 moonDir;     // unit vector toward the moon
    float daylight;   // world light multiplier: NIGHT_LIGHT at night .. 1 in full day
    float sunLight;   // 0..1: how much direct sun there is (drives shadows; 0 once set)
    float starsVisible; // 0..1 star field opacity
    float starAngle;  // radians the star field has turned about the pole (normal E-W streaming)
};

static const float SUNRISE_SECONDS = 0.0f;    // Dawn begins
static const float SUNSET_SECONDS = 3000.0f;  // 50:00, inside Dusk (47-55)
static const float NIGHT_LIGHT = 0.30f;       // darkest the world gets (still playable)

static inline float SkySmooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3 - 2 * t);
}

static inline SkyState ComputeSky(float dayTime) {
    const float PI = 3.14159265f;
    float t = fmodf(dayTime, DAY_LENGTH_SECONDS);
    if (t < 0) t += DAY_LENGTH_SECONDS;
    // Sun angle along its arc: 0 at sunrise (east, +X), pi at sunset
    // (west), carrying on below the horizon through the night.
    float a = t < SUNSET_SECONDS ? PI * (t - SUNRISE_SECONDS) / (SUNSET_SECONDS - SUNRISE_SECONDS)
                                 : PI + PI * (t - SUNSET_SECONDS) / (DAY_LENGTH_SECONDS - SUNSET_SECONDS);
    SkyState s;
    // Tilted toward -Z (south) at noon, so shadows never point straight down.
    s.sunDir = Normalize({ cosf(a), sinf(a), -0.35f * sinf(a) });
    // The moon trails the sun by ~140 degrees: up through the night and
    // into the morning, the way a waning moon lingers after dawn.
    float m = a - 2.45f;
    s.moonDir = Normalize({ cosf(m), sinf(m), 0.30f * sinf(m) });
    float up = SkySmooth(-0.12f, 0.25f, s.sunDir.y);
    s.daylight = NIGHT_LIGHT + (1.0f - NIGHT_LIGHT) * up;
    s.sunLight = SkySmooth(-0.02f, 0.15f, s.sunDir.y);
    s.starsVisible = 1.0f - SkySmooth(-0.20f, 0.05f, s.sunDir.y);
    s.starAngle = 2.0f * PI * t / DAY_LENGTH_SECONDS;
    return s;
}

// The sun's orthographic view-projection for the shadow map: centred on
// `eye`, covering +-`extent` blocks across and +-`depthHalf` along the
// light, with the centre snapped to whole texels of a `mapSize` map so
// the texel grid doesn't crawl (shimmer) as the player moves.
static inline Mat4 ShadowLightViewProj(Vec3 eye, Vec3 sun, float extent, float depthHalf, int mapSize) {
    Vec3 up = fabsf(sun.y) > 0.99f ? Vec3{ 0, 0, 1 } : Vec3{ 0, 1, 0 };
    Mat4 view = MatLookToLH({ 0, 0, 0 }, { -sun.x, -sun.y, -sun.z }, up);
    float texel = 2.0f * extent / mapSize;
    float lx = eye.x * view.m[0][0] + eye.y * view.m[1][0] + eye.z * view.m[2][0];
    float ly = eye.x * view.m[0][1] + eye.y * view.m[1][1] + eye.z * view.m[2][1];
    float lz = eye.x * view.m[0][2] + eye.y * view.m[1][2] + eye.z * view.m[2][2];
    lx = floorf(lx / texel) * texel;
    ly = floorf(ly / texel) * texel;
    return MatMul(MatMul(view, MatTranslation(-lx, -ly, -(lz - depthHalf))), MatOrthoLH(2 * extent, 2 * extent, 0.0f, 2 * depthHalf));
}

// Rotation by `angle` about unit `axis` (Rodrigues), for column vectors:
// v' = m * v.
static inline void AxisAngleMatrix(Vec3 axis, float angle, float m[3][3]) {
    float c = cosf(angle), s = sinf(angle), k = 1 - c;
    float x = axis.x, y = axis.y, z = axis.z;
    m[0][0] = c + x * x * k;     m[0][1] = x * y * k - z * s; m[0][2] = x * z * k + y * s;
    m[1][0] = y * x * k + z * s; m[1][1] = c + y * y * k;     m[1][2] = y * z * k - x * s;
    m[2][0] = z * x * k - y * s; m[2][1] = z * y * k + x * s; m[2][2] = c + z * z * k;
}

// The celestial pole: perpendicular to the sun's path (ComputeSky), so the
// stars turn about the same axis, in the same sense, as the sun does --
// the normal east-to-west streaming.
static inline Vec3 CelestialPole() { return Normalize({ 0.0f, 0.35f, 1.0f }); }
