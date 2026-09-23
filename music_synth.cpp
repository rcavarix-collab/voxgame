// music_synth.cpp
//
// Procedural day-cycle music (Section 10 / Part XIV of DESIGN.md). One
// continuous system spanning a full in-game day (DAY_LENGTH_SECONDS,
// world.h), synthesized in small chunks anchored to the live game clock
// rather than baked once as a fixed loop -- see DESIGN.md Part XIV for
// why (drift-free sync between the clock and playback, by construction,
// not by periodic correction).
//
// Two layers, both pure functions of absolute day-time plus a small
// amount of genuinely necessary persistent state (the resonant filter's
// history, and the arp's note scheduler) carried in MusicState -- reset
// to a clean zero-state on every discontinuity (new game/load/resume),
// never allowed to drift.
//
// Chord bed: a repeating 100-second-per-chord cycle, slowly modally
// drifting from D Dorian ("day") to D Aeolian ("night") and back across
// the hour. Arp/pulse layer: one continuous rate+amplitude curve
// spanning the whole hour (silent-ish at dawn, driving by midday) with
// scheduled breathing gaps even at its peak, rather than a per-segment
// on/off switch. Every curve is smoothstep-interpolated between anchor
// points on a many-seconds-to-minutes timescale, and filter resonance
// is capped low -- consistent with this project's hard accessibility
// rule (DESIGN.md 11.3): no sudden or uncontrolled event, ever,
// regardless of the Music Intensity setting (persist.h/game.cpp)
// applied on top.
//
// This file has no dependency on the rest of the project's types --
// it shares its ResetMusicState/GenerateMusicChunk contract with
// audio.cpp purely by convention (MusicState's layout here must match
// audio.h's declaration exactly), same as textures.cpp's contract with
// render.cpp.

#include <cstdint>
#include <cmath>

static const int kMusicSampleRate = 44100;
static const double kMusicPI = 3.14159265358979323846;
static const double kMusicDayLength = 3600.0; // must match world.h's DAY_LENGTH_SECONDS

// State that must persist across chunk-generation calls during
// continuous play, and gets reset to this same zero-state on any
// discontinuity (new game, load, resume from pause) -- see
// ResetMusicState below. Plain-old-data, identical layout assumed by
// audio.h/audio.cpp (no shared header; contract by convention, same as
// every other extern "C" entry point in this project).
struct MusicState {
    double filterZ1, filterZ2;      // biquad history (Direct Form I)
    double secondsUntilNextNote;    // arp scheduler: counts down, <=0 triggers the next note
    int arpCursor;                  // position in the up-down pattern across the current chord's tones
    double noteEnvTime;             // seconds since the current arp note's onset
    double noteHoldDur;             // this note's total hold duration, decided at trigger time
    double currentNoteFreq;         // frequency the currently-sounding arp note plays at
};

extern "C" void ResetMusicState(MusicState* s) {
    s->filterZ1 = 0.0; s->filterZ2 = 0.0;
    s->secondsUntilNextNote = 0.0; // triggers a fresh note immediately
    s->arpCursor = 0;
    s->noteEnvTime = 0.0;
    s->noteHoldDur = 0.5;
    s->currentNoteFreq = 220.0;
}

// ---- Chord tables (Hz, equal temperament A4=440), 4 slots, day/night
// pairs padded to 5 tones (0.0 = tone absent at that index). ----
struct MusicChord { double freq[5]; int count; };
static const double MUSIC_TONE_AMP[5] = { 0.45, 0.30, 0.22, 0.16, 0.12 }; // bass loudest, falling off

static MusicChord MUSIC_DAY_CHORDS[4] = {
    { {146.83, 174.61, 220.00, 261.63, 329.63}, 5 }, // Dm9
    { {98.00, 130.81, 146.83, 174.61, 220.00}, 5 },  // G7sus4
    { {164.81, 196.00, 246.94, 293.66, 0.0}, 4 },    // Em7
    { {110.00, 146.83, 164.81, 196.00, 261.63}, 5 }, // A7sus4
};
static MusicChord MUSIC_NIGHT_CHORDS[4] = {
    { {146.83, 174.61, 220.00, 261.63, 0.0}, 4 },    // Dm7
    { {116.54, 146.83, 174.61, 220.00, 0.0}, 4 },    // Bbmaj7
    { {98.00, 116.54, 146.83, 174.61, 0.0}, 4 },     // Gm7
    { {110.00, 146.83, 164.81, 196.00, 261.63}, 5 }, // A7sus4 (shared, harmonically neutral)
};

static double MusicSmoothstep(double a, double b, double x) {
    if (x <= a) return 0.0;
    if (x >= b) return 1.0;
    double t = (x - a) / (b - a);
    return t * t * (3.0 - 2.0 * t);
}

// 0 = fully D Dorian ("day"), 1 = fully D Aeolian ("night"). Both
// endpoints (t=0, t=kMusicDayLength) evaluate to 0, so the hour loops
// cleanly without a seam in this curve.
static double MusicModeMix(double t) {
    if (t < 2400.0) return 0.0;
    if (t < 3000.0) return MusicSmoothstep(2400.0, 3000.0, t);
    if (t < 3300.0) return 1.0;
    return 1.0 - MusicSmoothstep(3300.0, 3600.0, t);
}

// Slow overall swell on the chord bed -- one cycle per 180s, and
// 3600/180 = 20 exact cycles, so it also loops with no seam.
static double MusicChordSwell(double t) {
    return 0.85 + 0.15 * std::sin(2.0 * kMusicPI * t / 180.0);
}

static const int MUSIC_CHORD_HARMONICS = 6;
static const int MUSIC_ARP_HARMONICS = 6;

// A band-limited saw-ish tone: a truncated additive Fourier sum, same
// technique the original ambient track used, just with several
// harmonics per note instead of one -- alias-safe by construction
// (every harmonic here stays far under Nyquist) and needs no new
// synthesis paradigm.
static double MusicAdditiveTone(double freq, double t, int harmonics) {
    double s = 0.0;
    for (int k = 1; k <= harmonics; k++) s += std::sin(2.0 * kMusicPI * k * freq * t) / k;
    return s;
}

static double MusicChordBed(double t) {
    int slot = (int)std::floor(t / 100.0);
    slot = ((slot % 36) + 36) % 36;
    int cyclePos = slot % 4;
    double dayW = 1.0 - MusicModeMix(t);
    double nightW = MusicModeMix(t);
    double out = 0.0;
    if (dayW > 1e-4) {
        const MusicChord& c = MUSIC_DAY_CHORDS[cyclePos];
        for (int i = 0; i < c.count; i++) out += dayW * MUSIC_TONE_AMP[i] * MusicAdditiveTone(c.freq[i], t, MUSIC_CHORD_HARMONICS);
    }
    if (nightW > 1e-4) {
        const MusicChord& c = MUSIC_NIGHT_CHORDS[cyclePos];
        for (int i = 0; i < c.count; i++) out += nightW * MUSIC_TONE_AMP[i] * MusicAdditiveTone(c.freq[i], t, MUSIC_CHORD_HARMONICS);
    }
    return out * MusicChordSwell(t);
}

// ---- Arp rate (notes/min) and amplitude (linear) curves: one
// continuous shape spanning the whole hour, smoothstep-interpolated
// between anchors, silent-ish at dawn/night, driving by midday. Both
// endpoints match exactly for a seamless loop. ----
struct MusicAnchor { double tMin, rateNotesPerMin, ampLinear; };
static const MusicAnchor MUSIC_ANCHORS[7] = {
    { 0.0,  2.0,   0.0316 },
    { 8.0,  2.0,   0.0316 },
    { 20.0, 240.0, 0.5012 },
    { 35.0, 270.0, 0.7079 },
    { 47.0, 90.0,  0.2512 },
    { 55.0, 6.0,   0.0631 },
    { 60.0, 2.0,   0.0316 },
};
static double MusicInterpCurve(double tMinutes, bool wantRate) {
    for (int i = 0; i < 6; i++) {
        if (tMinutes >= MUSIC_ANCHORS[i].tMin && tMinutes <= MUSIC_ANCHORS[i + 1].tMin) {
            double span = MUSIC_ANCHORS[i + 1].tMin - MUSIC_ANCHORS[i].tMin;
            double s = MusicSmoothstep(0.0, span, tMinutes - MUSIC_ANCHORS[i].tMin);
            double a = wantRate ? MUSIC_ANCHORS[i].rateNotesPerMin : MUSIC_ANCHORS[i].ampLinear;
            double b = wantRate ? MUSIC_ANCHORS[i + 1].rateNotesPerMin : MUSIC_ANCHORS[i + 1].ampLinear;
            return a + (b - a) * s;
        }
    }
    return wantRate ? MUSIC_ANCHORS[6].rateNotesPerMin : MUSIC_ANCHORS[6].ampLinear;
}
static double MusicArpRateHz(double t) { return MusicInterpCurve(t / 60.0, true) / 60.0; }
static double MusicArpAmp(double t) { return MusicInterpCurve(t / 60.0, false); }

// Scheduled breathing gaps even during the midday plateau (~25s each,
// 8s fades in/out) -- a deliberate, reproducible "something just went
// quiet, something's about to reappear" moment rather than a
// constantly-running line.
static double MusicGateEnvelope(double t) {
    static const double centers[5] = { 24 * 60.0, 29 * 60.0, 34 * 60.0, 39 * 60.0, 44 * 60.0 };
    for (double c : centers) {
        const double halfHold = 4.5, fade = 8.0;
        double ad = std::fabs(t - c);
        if (ad < halfHold) return 0.0;
        if (ad < halfHold + fade) return (ad - halfHold) / fade;
    }
    return 1.0;
}

// ---- Resonant lowpass (RBJ cookbook biquad). Q is capped by the
// caller well below self-oscillation -- see GenerateMusicChunk's fixed
// filter call site below. ----
struct MusicBiquad { double b0, b1, b2, a1, a2; };
static MusicBiquad MusicMakeLowpass(double cutoffHz, double q, double sr) {
    double w0 = 2.0 * kMusicPI * cutoffHz / sr;
    double alpha = std::sin(w0) / (2.0 * q);
    double cosw0 = std::cos(w0);
    double a0 = 1.0 + alpha;
    MusicBiquad bq;
    bq.b0 = ((1.0 - cosw0) / 2.0) / a0;
    bq.b1 = (1.0 - cosw0) / a0;
    bq.b2 = ((1.0 - cosw0) / 2.0) / a0;
    bq.a1 = (-2.0 * cosw0) / a0;
    bq.a2 = (1.0 - alpha) / a0;
    return bq;
}
static double MusicFilterCutoff(double tMinutes) {
    struct P { double t, c; };
    static const P F[5] = { {0, 500}, {20, 2200}, {35, 3500}, {47, 1400}, {60, 500} };
    for (int i = 0; i < 4; i++) {
        if (tMinutes >= F[i].t && tMinutes <= F[i + 1].t) {
            double s = MusicSmoothstep(F[i].t, F[i + 1].t, tMinutes);
            return F[i].c + (F[i + 1].c - F[i].c) * s;
        }
    }
    return F[4].c;
}
static const double MUSIC_FILTER_Q = 0.8; // fixed, well below self-oscillation -- never varies with intensity

// Empirically-derived fixed scale (Section 10.4/DESIGN.md): a full-hour
// offline simulation of this exact signal path measured peak~=2.498 at
// intensity 1.0 (the worst case -- reducing intensity only ever reduces
// level). 0.35 leaves ~13% headroom below full scale after that peak,
// consistent with this project's RMS-normalization philosophy but
// computed once and baked in, since a chunked/streaming generator has
// no single finished buffer to measure and normalize against the way
// the old bake-once ambient track did.
static const double MUSIC_OUTPUT_SCALE = 0.35;

static const int MUSIC_ARP_UPDOWN[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };

// Generates `sampleCount` samples starting at absolute day-time
// `startTime` seconds, continuing `state` forward. `intensity` in
// [0,1]: 0 mutes the arp/pulse layer entirely (ambient bed only), 1 is
// the full curve above (already the maximum energy/brightness this
// track ever reaches by design -- intensity is a floor control, never
// a ceiling-breaker, per the accessibility rule this whole system is
// built around).
extern "C" void GenerateMusicChunk(double startTime, int sampleCount, double intensity, MusicState* state, int16_t* outPCM) {
    // g_nextChunkStartTime (audio.cpp) grows unboundedly across a whole
    // session rather than wrapping itself -- wrapping happens here, per
    // chunk. Every per-sample curve below wraps its own `t` the same
    // way, but this one is computed once per chunk (cutoff moves slowly
    // enough not to need per-sample precision) from `startTime`
    // directly, so it must be wrapped explicitly too -- without this,
    // any session running past the first in-game hour would see
    // startTime/60 permanently exceed the curve's [0,60] domain and
    // silently freeze the filter at its fallback value forever.
    double wrappedStart = std::fmod(startTime, kMusicDayLength);
    if (wrappedStart < 0.0) wrappedStart += kMusicDayLength;
    double cutoff = MusicFilterCutoff(wrappedStart / 60.0);
    MusicBiquad bq = MusicMakeLowpass(cutoff, MUSIC_FILTER_Q, (double)kMusicSampleRate);

    for (int i = 0; i < sampleCount; i++) {
        double t = std::fmod(startTime + (double)i / kMusicSampleRate, kMusicDayLength);

        double chord = MusicChordBed(t);

        state->secondsUntilNextNote -= 1.0 / kMusicSampleRate;
        state->noteEnvTime += 1.0 / kMusicSampleRate;
        if (state->secondsUntilNextNote <= 0.0) {
            int slot = ((int)std::floor(t / 100.0) % 36 + 36) % 36;
            int cyclePos = slot % 4;
            double dayW = 1.0 - MusicModeMix(t);
            const MusicChord& c = (dayW >= 0.5) ? MUSIC_DAY_CHORDS[cyclePos] : MUSIC_NIGHT_CHORDS[cyclePos];
            int idx = MUSIC_ARP_UPDOWN[state->arpCursor % 8] % (c.count > 0 ? c.count : 1);
            state->currentNoteFreq = c.freq[idx];
            state->arpCursor++;
            double rate = MusicArpRateHz(t);
            if (rate < 0.01) rate = 0.01;
            state->noteHoldDur = 1.0 / rate;
            state->secondsUntilNextNote += state->noteHoldDur;
            state->noteEnvTime = 0.0;
        }
        // Attack+release can exceed the note's own hold duration at the
        // top of the rate curve (e.g. 270 notes/min => ~0.22s notes,
        // shorter than 0.15+0.30=0.45s) -- scaling both down
        // proportionally so they always meet exactly at the envelope's
        // peak keeps this continuous (a triangle instead of a trapezoid)
        // rather than jumping straight from mid-attack into a release
        // ramp computed against a duration shorter than the attack
        // itself, which produced an audible discontinuity right at the
        // busiest, most audible part of the whole track -- precisely
        // the kind of sudden event this system exists to never produce.
        double effAttack = 0.15, effRelease = 0.30;
        double envSpan = effAttack + effRelease;
        if (envSpan > state->noteHoldDur && envSpan > 1e-9) {
            double k = state->noteHoldDur / envSpan;
            effAttack *= k;
            effRelease *= k;
        }
        double env;
        if (effAttack > 1e-9 && state->noteEnvTime < effAttack) env = state->noteEnvTime / effAttack;
        else if (state->noteEnvTime < state->noteHoldDur - effRelease) env = 1.0;
        else if (effRelease > 1e-9 && state->noteEnvTime < state->noteHoldDur) env = (state->noteHoldDur - state->noteEnvTime) / effRelease;
        else env = 0.0;
        if (env < 0.0) env = 0.0;
        if (env > 1.0) env = 1.0;

        double arp = MusicAdditiveTone(state->currentNoteFreq, t, MUSIC_ARP_HARMONICS)
                   * env * MusicArpAmp(t) * MusicGateEnvelope(t) * intensity;

        double mix = chord * 0.85 + arp * 0.9;

        // Biquad, Direct Form I with the two-history-term formulation
        // used throughout (matches the validated offline simulation).
        double y = bq.b0 * mix + state->filterZ1;
        state->filterZ1 = bq.b1 * mix - bq.a1 * y + state->filterZ2;
        state->filterZ2 = bq.b2 * mix - bq.a2 * y;

        double v = y * MUSIC_OUTPUT_SCALE;
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        outPCM[i] = (int16_t)(v * 32767.0);
    }
}
