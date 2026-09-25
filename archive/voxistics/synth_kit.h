// synth_kit.h
//
// The synthesis primitives shared by the day-cycle music (music_synth.cpp)
// and the world sound palette (sfx_synth.cpp): one set of oscillators,
// envelopes, noise and filters, so everything the game plays shares the
// same timbral DNA (docs/SOUND_PALETTE.md 1.3). Header-only, no state.

#pragma once

#include <cmath>
#include <cstdint>

namespace synth {

const double kPi = 3.14159265358979323846;
const double kSR = 44100.0;


// floor() via truncation: a single SSE2 conversion, where std::floor is an
// out-of-line library call on baseline x64 targets (both MSVC and GCC
// without SSE4.1) -- and every oscillator needs two per sample. Valid for
// |x| < 2^63; phases here stay below ~1e7.
inline double FastFloor(double x) {
    double r = (double)(long long)x;
    return r > x ? r - 1.0 : r;
}
inline double Frac(double x) { return x - FastFloor(x); }

inline double Smooth(double u) {
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return 1.0;
    return u * u * (3.0 - 2.0 * u);
}

// sin(2*pi*p) for any p: branchless reduction to a quarter period, then a
// 9th-order odd polynomial (max error ~4e-6, below -100 dB).
inline double Sin01(double p) {
    double q = p - FastFloor(p + 0.5);                 // [-0.5, 0.5)
    double r = 0.25 - std::fabs(0.25 - std::fabs(q));  // [0, 0.25], same |sin|
    double x = r * (2.0 * kPi);
    double x2 = x * x;
    double v = x * (1.0 + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0)))));
    return std::copysign(v, q);
}

// Raised-cosine 0->1 over u in [0,1]: zero slope at both ends, so no
// envelope corner ever clicks.
inline double Ramp(double u) {
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return 1.0;
    return 0.5 - 0.5 * Sin01(0.5 * u + 0.25);
}

const double kTriNorm = 1.0 / (1.0 + 1.0 / 9.0 + 1.0 / 25.0);

// Additive soft triangle: the triangle series' odd partials 1, 3, 5 at
// their 1/n^2 amplitudes, normalized to unit peak. The 3rd and 5th
// partials come from the fundamental s = sin(x) through the exact
// multiple-angle identities sin 3x = 3s - 4s^3 and
// sin 5x = 5s - 20s^3 + 16s^5 -- the same partials, one sine evaluation.
inline double SoftTriFrom(double s) {
    double s2 = s * s;
    double s3 = s * (3.0 - 4.0 * s2);
    double s5 = s * (5.0 + s2 * (-20.0 + 16.0 * s2));
    return (s - s3 * (1.0 / 9.0) + s5 * (1.0 / 25.0)) * kTriNorm;
}
inline double SoftTri(double p) { return SoftTriFrom(Sin01(p)); }

// PolyBLEP correction for a unit discontinuity at phase 0 (p in [0,1)).
inline double PolyBlep(double p, double dt) {
    if (p < dt) { p /= dt; return p + p - p * p - 1.0; }
    if (p > 1.0 - dt) { p = (p - 1.0) / dt; return p * p + p + p + 1.0; }
    return 0.0;
}
inline double BlepSaw(double p, double dt) { return 2.0 * p - 1.0 - PolyBlep(p, dt); }
// Pulse of duty `w`, DC removed so a width change never shifts the mix.
inline double BlepPulse(double p, double dt, double w) {
    double v = p < w ? 1.0 : -1.0;
    v += PolyBlep(p, dt);
    v -= PolyBlep(Frac(p + 1.0 - w), dt);
    return v - (2.0 * w - 1.0);
}

inline uint64_t Mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
// Deterministic "random" in [0,1) keyed by event identity, so the same
// event always gets the same micro-timing and variation.
inline double Hash01(uint64_t a, uint64_t b) {
    return (double)(Mix64(Mix64(a + 0x9E3779B97F4A7C15ULL) ^ (b * 0xD1B54A32D192ED03ULL)) >> 11) * (1.0 / 9007199254740992.0);
}
// White noise as a pure function of the (wrapped) sample index.
inline double Noise(uint64_t n, uint64_t seed) {
    return (double)(Mix64(n * 0x9E3779B97F4A7C15ULL + seed) >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

struct Biquad { double b0, b1, b2, a1, a2; };

// RBJ cookbook lowpass.
inline Biquad Lowpass(double fc, double q) {
    double w0 = 2.0 * kPi * fc / kSR;
    double alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0), a0 = 1.0 + alpha;
    return { (1.0 - c) * 0.5 / a0, (1.0 - c) / a0, (1.0 - c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
}
// RBJ cookbook bandpass, constant 0 dB peak.
inline Biquad Bandpass(double fc, double q) {
    double w0 = 2.0 * kPi * fc / kSR;
    double alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0), a0 = 1.0 + alpha;
    return { alpha / a0, 0.0, -alpha / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
}
inline double RunBiquad(const Biquad& f, double x, double& z1, double& z2) {
    double y = f.b0 * x + z1;
    z1 = f.b1 * x - f.a1 * y + z2;
    z2 = f.b2 * x - f.a2 * y;
    return y;
}
// Decaying IIR state eventually goes denormal, which is dramatically slow
// on x86 -- flushed once per control block.
inline void FlushDenormal(double& z) { if (std::fabs(z) < 1e-15) z = 0.0; }

} // namespace synth
