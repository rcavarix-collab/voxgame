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
    // Sun angle along its arc: 0 at sunrise (due east), pi at sunset
    // (due west), carrying on below the horizon through the night.
    float a = t < SUNSET_SECONDS ? PI * (t - SUNRISE_SECONDS) / (SUNSET_SECONDS - SUNRISE_SECONDS)
                                 : PI + PI * (t - SUNSET_SECONDS) / (DAY_LENGTH_SECONDS - SUNSET_SECONDS);
    SkyState s;
    // As at the equator (owner's call: the sky reads more plainly): the sun
    // rises due east, passes straight overhead and sets due west, its whole
    // path in one vertical plane. Noon shadows fall straight down.
    s.sunDir = Normalize(kEast * cosf(a) + kUp * sinf(a));
    // The moon trails the sun by ~140 degrees: up through the night and
    // into the morning, the way a waning moon lingers after dawn. Its path
    // leans a few degrees off the sun's, as a real moon's does.
    float m = a - 2.45f;
    s.moonDir = Normalize(kEast * cosf(m) + kUp * sinf(m) + kNorth * (0.09f * sinf(m)));
    float up = SkySmooth(-0.12f, 0.25f, s.sunDir.y);
    s.daylight = NIGHT_LIGHT + (1.0f - NIGHT_LIGHT) * up;
    // Direct sun arrives within a minute or two of sunrise (low, orange,
    // long shadows) rather than after the sun has climbed ~9 degrees. It
    // ends exactly at the horizon: below it the sun moves 5x faster (the
    // night is short), which would turn the last of the fade into a snap.
    s.sunLight = SkySmooth(0.0f, 0.10f, s.sunDir.y);
    s.starsVisible = 1.0f - SkySmooth(-0.20f, 0.05f, s.sunDir.y);
    // The whole sky turns as one: the stars ride the same angle as the sun
    // (slow through the day, quick through the short night), so a star and
    // the sun never drift against each other; the moon trails at a fixed offset.
    s.starAngle = a;
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
// the normal east-to-west streaming. At the equator it lies on the
// northern horizon: every star rises straight up out of the east.
static inline Vec3 CelestialPole() { return kNorth; }

// ---- Atmosphere: the colours of light at this time of day (Part XIII) ----
// All linear-light RGB (the shaders tonemap and convert to sRGB at the
// end). One function feeds the sky shader, the world shader's lighting
// and fog, and the CPU preview used to tune them -- so the fog always
// matches the sky behind it and nothing drifts apart.
struct Atmosphere {
    Vec3 sunColor;     // direct sunlight reaching the ground (0 at night)
    Vec3 moonColor;    // direct moonlight (0 by day / when set)
    Vec3 zenith;       // clear sky straight up
    Vec3 horizon;      // clear sky at the horizon
    Vec3 twilight;     // colour of the band around a low sun
    Vec3 ambientUp;    // light from the sky onto up-facing surfaces
    Vec3 ambientDown;  // bounce light onto down-facing surfaces
    float exposure;    // a fake eye adaptation: brighter at night
    float twilightAmount; // 0..1 how strong the sunset/sunrise band is
};

static inline Vec3 SkyLerp(Vec3 a, Vec3 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
static inline Vec3 SkyScale(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

static inline Atmosphere ComputeAtmosphere(const SkyState& s) {
    Atmosphere a;
    float day = (s.daylight - NIGHT_LIGHT) / (1.0f - NIGHT_LIGHT);   // 0 night .. 1 day
    float high = SkySmooth(0.0f, 0.45f, s.sunDir.y);                 // sun well up
    // Sunlight: white-gold high in the sky, orange as it nears the horizon.
    Vec3 sunLow = { 1.00f, 0.50f, 0.22f }, sunHigh = { 1.00f, 0.95f, 0.86f };
    a.sunColor = SkyScale(SkyLerp(sunLow, sunHigh, high), 2.3f * s.sunLight);
    // Moonlight: faint and blue, only while the moon is up and the sun isn't.
    float moonUp = SkySmooth(-0.02f, 0.15f, s.moonDir.y) * (1.0f - day);
    a.moonColor = SkyScale({ 0.55f, 0.65f, 1.0f }, 0.28f * moonUp);
    a.zenith = SkyLerp({ 0.004f, 0.006f, 0.018f }, { 0.10f, 0.28f, 0.78f }, day);
    a.horizon = SkyLerp({ 0.012f, 0.016f, 0.035f }, { 0.55f, 0.68f, 0.90f }, day);
    a.twilightAmount = (1.0f - high) * SkySmooth(-0.18f, 0.02f, s.sunDir.y) * (1.0f - SkySmooth(0.25f, 0.45f, s.sunDir.y));
    a.twilight = { 1.00f, 0.36f, 0.11f };
    // Ambient: the sky's own light from above, a warm dim bounce from below.
    a.ambientUp = SkyLerp({ 0.035f, 0.045f, 0.09f }, { 0.42f, 0.50f, 0.64f }, day);
    a.ambientDown = SkyLerp({ 0.012f, 0.012f, 0.02f }, { 0.20f, 0.17f, 0.13f }, day);
    a.exposure = 1.0f + 1.6f * (1.0f - day) * (1.0f - day); // twilight keeps its colour; only real night is lifted
    return a;
}
