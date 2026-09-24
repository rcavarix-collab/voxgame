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
