// musiclevel.h
//
// What the music-reactive block follows (DESIGN.md 10.4): not loudness --
// the day's music is mostly sustained pads, so loudness barely dips and
// the block would sit lit -- but *onsets*: the moments a note, pluck or
// beat starts. Per step (1/64 s), it takes the energy of the signal's
// change from sample to sample (a first difference, which weights the
// bright attack of a note over the smooth body of a pad) and compares it
// with its own recent average: the level is how far above normal it is
// right now. Being a ratio, a quiet passage flickers with its own notes as
// much as a loud one; a gate on absolute energy keeps near-silence dark.
// Measured on the game's own music (the attacks are soft -- a note rarely
// lifts it past 1.4x), steps sit ~10% of the time above the halfway mark.
// Pure and header-only: audio.cpp runs it as each chunk is synthesised,
// and it is tested natively.

#pragma once

#include <cmath>
#include <cstdint>

struct MusicLevelMeter {
    float average = 0;  // recent attack energy (slow)
    int16_t last = 0;   // previous sample, carried across chunks
};

static const float MUSIC_LEVEL_AVERAGE_SECONDS = 0.35f; // how quickly "normal" catches up
static const float MUSIC_LEVEL_DARK = 1.08f;  // attack energy / its average: at or below, dark...
static const float MUSIC_LEVEL_FULL = 1.35f;  // ...at or above, fully lit
static const float MUSIC_LEVEL_GATE = 0.0006f; // attack energies below this are near-silence: stay dark

// Measures `steps` equal slices of `pcm` (n samples at `sampleRate`),
// writing a 0..1 onset level per slice to `out`.
static inline void MeasureMusicLevels(MusicLevelMeter& m, const int16_t* pcm, int n, int steps, int sampleRate, float* out) {
    const int per = n / steps;
    const float stepSeconds = (float)per / (float)sampleRate;
    const float avgRate = 1.0f - expf(-stepSeconds / MUSIC_LEVEL_AVERAGE_SECONDS);
    for (int q = 0; q < steps; q++) {
        double sum = 0;
        for (int i = q * per; i < (q + 1) * per; i++) {
            double d = (double)pcm[i] - (double)m.last;
            sum += d * d;
            m.last = pcm[i];
        }
        float e = (float)(sqrt(sum / per) / 32768.0);
        float ratio = m.average > 0 ? e / m.average : 0.0f;
        m.average += (e - m.average) * avgRate;
        float u = (ratio - MUSIC_LEVEL_DARK) / (MUSIC_LEVEL_FULL - MUSIC_LEVEL_DARK);
        u = u < 0 ? 0 : (u > 1 ? 1 : u);
        float gate = (e - MUSIC_LEVEL_GATE) / MUSIC_LEVEL_GATE;
        gate = gate < 0 ? 0 : (gate > 1 ? 1 : gate);
        out[q] = u * u * (3 - 2 * u) * gate;
    }
}

// What the block actually shows: never the onsets themselves (several a
// second -- a strobe, and a seizure risk; photosensitivity guidance caps
// flashes at 3 a second), but a slow swell that follows how busy the
// notes are: rising over ~1/4 s as they cluster, fading over ~1 s. On the
// game's own music that is under one noticeable swing a second (measured
// offline over the day). On top of that a hard slew limit -- the full
// range takes at least half a second either way -- guarantees no fast
// flicker whatever the input.
struct MusicGlow {
    float envelope = 0; // follows the onset level
    float shown = 0;    // the output, slew-limited
};
static const float MUSIC_GLOW_ATTACK = 0.25f;  // seconds
static const float MUSIC_GLOW_RELEASE = 1.0f;  // seconds
static const float MUSIC_GLOW_GAIN = 2.0f;     // the envelope sits around 0.1-0.45: spread it over the full range
static const float MUSIC_GLOW_MAX_RATE = 2.0f; // output units per second, up or down

static inline float MusicGlowStep(MusicGlow& g, float onsetLevel, float dt) {
    if (dt <= 0) return g.shown;
    float tau = onsetLevel > g.envelope ? MUSIC_GLOW_ATTACK : MUSIC_GLOW_RELEASE;
    g.envelope += (onsetLevel - g.envelope) * (1.0f - expf(-dt / tau));
    float want = g.envelope * MUSIC_GLOW_GAIN;
    want = want > 1.0f ? 1.0f : want;
    float step = MUSIC_GLOW_MAX_RATE * dt;
    float d = want - g.shown;
    g.shown += d > step ? step : (d < -step ? -step : d);
    return g.shown;
}
