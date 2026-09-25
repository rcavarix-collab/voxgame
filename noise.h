// noise.h
//
// Smooth value noise and a three-octave fbm, 0..1, built on Hash3 so the
// same coordinates always give the same value (worlds are reproducible
// from their seed). Used by the terrain generator for ground-type clumps
// and by anything else that needs gentle, natural variation. Cheap: four
// hashes and three lerps per octave.

#pragma once

#include "common.h"

static inline float ValueNoise2(float x, float z, uint32_t seed) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    float a = Hash01(ix, 0, iz, seed), b = Hash01(ix + 1, 0, iz, seed);
    float c = Hash01(ix, 0, iz + 1, seed), d = Hash01(ix + 1, 0, iz + 1, seed);
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fz);
}

static inline float Fbm2(float x, float z, uint32_t seed) {
    return 0.57f * ValueNoise2(x, z, seed) + 0.29f * ValueNoise2(x * 2.03f, z * 2.03f, seed + 1) +
           0.14f * ValueNoise2(x * 4.1f, z * 4.1f, seed + 2);
}
