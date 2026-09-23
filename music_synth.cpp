// music_synth.cpp
//
// Day-cycle music (DESIGN.md Part XIV): one continuous hour in six
// sections -- Dawn, Morning, Midday, Afternoon, Dusk, Night -- whose final
// minute blends back into Dawn's opening so the day loops with no seam.
//
// Two synthesis families run side by side:
//  - Additive: tones built as explicit sums of sine partials -- pure sine,
//    a soft triangle from odd partials 1/3/5, and a sine+triangle blend.
//    Band-limited by construction; no filter needed to tame them.
//  - Subtractive: spectrally rich sources -- PolyBLEP band-limited saw and
//    pulse, and white noise -- shaped by filters: one shared resonant
//    lowpass over the tonal mix, plus a dedicated lowpass (noise bed) and
//    bandpass (air layer) that bypass it.
//
// Everything is a pure function of wrapped day time: chord weights, beat
// position, every note event and its envelope, oscillator phases. The only
// state carried between chunks is the three filters' histories and the
// resume fade-in (MusicState), so playback cannot drift from the day clock.
//
// Cost model: all per-sample oscillators use a polynomial sine and PolyBLEP
// (no libm calls in the sample loop); chord tones live in a fixed bank per
// register so a chord change crossfades gains rather than adding voices;
// every slow parameter (levels, chord weights, filter coefficients) is
// evaluated once per 64-sample control block and interpolated.

#include "music_synth.h"
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace {

const double kDayLength = MUSIC_DAY_LENGTH;
const double kPi = 3.14159265358979323846;
const double kSR = (double)MUSIC_SAMPLE_RATE;
const int kBlock = 64; // control-rate interval (~1.45 ms)
const double kSilentDb = -60.0;
// Resonance is part of the accessibility ceiling: Q never exceeds this, at
// any time of day or intensity (a Butterworth 0.707 plus a gentle bump).
const double kMaxQ = 1.2;
// Fixed output scale, set from a full-hour offline render at intensity
// 1.0 (the loudest case): peak ~0.85 FS, loudness close to the previous
// track so existing volume settings still feel the same. The final clamp
// is only a backstop and never engages (DESIGN.md Part XIV).
const double kOutputScale = 0.78;
const double kResumeFadeSeconds = 1.5;

// ---------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------

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
Biquad Lowpass(double fc, double q) {
    double w0 = 2.0 * kPi * fc / kSR;
    double alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0), a0 = 1.0 + alpha;
    return { (1.0 - c) * 0.5 / a0, (1.0 - c) / a0, (1.0 - c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
}
// RBJ cookbook bandpass, constant 0 dB peak.
Biquad Bandpass(double fc, double q) {
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

// ---------------------------------------------------------------------
// Automation curves: (seconds, value) keyframes, smoothstep between, and
// every curve's value at 3600 equals its value at 0 so the day loops.
// ---------------------------------------------------------------------

struct Key { double t, v; };

// Finds the keyframe segment containing t: returns i and the smoothstepped
// position u within [k[i], k[i+1]] (i = N-2, u = 1 past the last key).
template <size_t N>
size_t Locate(const Key (&k)[N], double t, double& u) {
    for (size_t i = 0; i + 1 < N; i++) {
        if (t < k[i + 1].t) { u = Smooth((t - k[i].t) / (k[i + 1].t - k[i].t)); return i; }
    }
    u = 1.0;
    return N - 2;
}
template <size_t N>
double Eval(const Key (&k)[N], double t) {
    double u;
    size_t i = Locate(k, t, u);
    return k[i].v + (k[i + 1].v - k[i].v) * u;
}
// Frequency curves interpolate in log2(Hz), so sweeps sound even.
template <size_t N>
double EvalHz(const Key (&k)[N], double t) {
    double u;
    size_t i = Locate(k, t, u);
    return k[i].v * std::pow(k[i + 1].v / k[i].v, u);
}
// Continuous through zero: exactly 0 at kSilentDb, so a layer fading to
// "silent" actually reaches silence (and can be skipped) without a step.
inline double DbToGain(double db) {
    if (db <= kSilentDb) return 0.0; // exact, independent of the CRT's pow rounding
    double g = std::pow(10.0, db / 20.0) - 0.001;
    return g > 0.0 ? g : 0.0;
}

// Section boundaries: Dawn 0, Morning 480, Midday 1200, Afternoon 2100,
// Dusk 2820, Night 3300.
const Key kMasterDb[] = { {0,-4},{480,-2},{1200,0},{2100,-1},{2820,-3},{3220,-3},{3300,-8},{3540,-8},{3600,-4} };
const Key kPulseDb[] = { {0,kSilentDb},{180,-18},{390,-18},{480,-10},{1080,-10},{1200,-9.5},{2100,-9},{2720,-9},{2820,-14},{3030,kSilentDb},{3600,kSilentDb} };
const Key kMotifDb[] = { {0,kSilentDb},{240,kSilentDb},{270,-19},{480,-16},{1200,-15},{2100,-16},{2720,-16},{2820,-15},{3240,-17},{3300,kSilentDb},{3600,kSilentDb} };
const Key kCounterDb[] = { {0,kSilentDb},{1200,kSilentDb},{1260,-16},{1950,-16},{2100,-21},{2130,kSilentDb},{3600,kSilentDb} };
const Key kNightDb[] = { {0,kSilentDb},{3290,kSilentDb},{3310,-12},{3540,-12},{3560,kSilentDb},{3600,kSilentDb} };
const Key kBassSineDb[] = { {0,-13},{390,-13},{480,-18},{1200,-19},{2100,-18},{2820,-13},{3600,-13} };
const Key kBassTriDb[] = { {0,-17},{480,-15},{1200,-14},{2100,-15},{2820,-17},{3300,-21},{3540,-21},{3600,-17} };
const Key kBassSawDb[] = { {0,kSilentDb},{390,kSilentDb},{480,-24},{1080,-23},{1200,-21},{2100,-22},{2720,-23},{2820,-30},{3000,kSilentDb},{3600,kSilentDb} };
const Key kDuckDepth[] = { {0,0},{480,0},{540,0.30},{1200,0.36},{2100,0.32},{2720,0.30},{2820,0.15},{3030,0},{3600,0} };
const Key kPadLowDb[] = { {0,-21},{480,-20},{1200,-19},{2100,-20},{2820,-21},{3600,-21} };
const Key kPadMidDb[] = { {0,-22},{480,-22},{1200,-21},{2100,-22},{2820,-22},{3300,-27},{3540,-27},{3600,-22} };
const Key kPadHighDb[] = { {0,kSilentDb},{480,kSilentDb},{600,-31},{1080,-30},{1200,-27},{1950,-26},{2100,-30},{2400,-33},{2820,kSilentDb},{3600,kSilentDb} };
const Key kBedDb[] = { {0,kSilentDb},{480,kSilentDb},{600,-38},{2720,-38},{2820,-42},{3060,kSilentDb},{3600,kSilentDb} };
const Key kAirDb[] = { {0,kSilentDb},{840,kSilentDb},{1000,-40},{1080,-38},{1200,-35},{1950,-34},{2100,-38},{2820,kSilentDb},{3600,kSilentDb} };
// Night's spec opens to 620 Hz in its last two minutes while Dawn starts at
// 380 Hz; the seam wins (no sudden events), so it opens to 620 and glides
// back to 380 through the final Dawn-matching crossfade.
const Key kCutoffHz[] = { {0,380},{360,720},{480,735},{900,1600},{1200,1600},{1680,3100},{1920,3100},{2100,2200},{2820,1000},{3180,480},{3300,470},{3480,470},{3545,620},{3600,380} };
const Key kResonance[] = { {0,0.25},{450,0.25},{510,0.30},{1170,0.30},{1230,0.33},{2070,0.33},{2130,0.28},{2790,0.28},{2850,0.22},{3270,0.22},{3330,0.20},{3540,0.20},{3600,0.25} };

// The level tables read in dB; each is converted once to linear gain so
// the control-rate path never calls pow (interpolation is then linear in
// amplitude, smoothstep-shaped).
template <size_t N>
struct GainCurve {
    Key k[N];
    explicit GainCurve(const Key (&db)[N]) { for (size_t i = 0; i < N; i++) k[i] = { db[i].t, DbToGain(db[i].v) }; }
    double operator()(double t) const { return Eval(k, t); }
};
const GainCurve kMasterGain(kMasterDb);
const GainCurve kPulseGain(kPulseDb);
const GainCurve kMotifGain(kMotifDb);
const GainCurve kCounterGain(kCounterDb);
const GainCurve kNightGain(kNightDb);
const GainCurve kBassSineGain(kBassSineDb);
const GainCurve kBassTriGain(kBassTriDb);
const GainCurve kBassSawGain(kBassSawDb);
const GainCurve kPadLowGain(kPadLowDb);
const GainCurve kPadMidGain(kPadMidDb);
const GainCurve kPadHighGain(kPadHighDb);
const GainCurve kBedGain(kBedDb);
const GainCurve kAirGain(kAirDb);

// ---------------------------------------------------------------------
// Harmony and form
// ---------------------------------------------------------------------

enum { CH_DM9, CH_G7SUS4, CH_EM7, CH_A7SUS4, CH_DRONE, CH_COUNT };

// Voicings as specified. Masks pick which tones feed each pad register;
// the drone is just open D3 + A3. Every frequency here (and every octave
// fold of it, down to /4) times 3600 is an integer, which is what lets
// absolute-time oscillator phases wrap from 3600 to 0 with no jump.
struct ChordDef { double tones[5]; int count; double bass; unsigned lowMask, midMask, highMask; };
const ChordDef kChords[CH_COUNT] = {
    { {146.83, 174.61, 220.00, 261.63, 329.63}, 5, 73.415,  0x05, 0x1F, 0x1C },
    { { 98.00, 130.81, 146.83, 174.61, 220.00}, 5, 49.0,    0x05, 0x1F, 0x1C },
    { {164.81, 196.00, 246.94, 293.66,   0.00}, 4, 41.2025, 0x05, 0x0F, 0x0C },
    { {110.00, 146.83, 164.81, 196.00, 261.63}, 5, 55.0,    0x05, 0x1F, 0x1C },
    { {146.83, 220.00,   0.00,   0.00,   0.00}, 2, 73.415,  0x01, 0x02, 0x00 },
};

const int kMaxBank = 8;
// One oscillator per distinct frequency in a register. A tone shared by
// two chords is one oscillator whose gain is the sum of both chords'
// weights, so it simply sustains through the change.
struct Bank {
    int n = 0;
    double freq[kMaxBank] = {};
    double member[CH_COUNT][kMaxBank] = {};
    void Add(int chord, double f) {
        if (f <= 0.0) return;
        for (int i = 0; i < n; i++)
            if (std::fabs(freq[i] - f) < 0.05) { member[chord][i] = 1.0; return; }
        if (n == kMaxBank) return;
        freq[n] = f;
        member[chord][n] = 1.0;
        n++;
    }
};

double FoldInto(double f, double lo, double hi) {
    if (f <= 0.0) return 0.0;
    while (f < lo) f *= 2.0;
    while (f >= hi) f *= 0.5;
    return f >= lo ? f : 0.0;
}

struct Section { double start, end, bpm, beatStart; };
struct Segment { double start, fade; int chord; };

const int kSections = 6;
enum { SEC_DAWN, SEC_MORNING, SEC_MIDDAY, SEC_AFTERNOON, SEC_DUSK, SEC_NIGHT };

// The soft 4/4 pulse's thump, pre-rendered once: a sine gliding 104 -> 52
// Hz under a 6 ms raised-cosine attack and ~110 ms decay, windowed to
// exactly zero by 0.45 s -- shorter than the shortest beat (124 BPM), so
// one beat's thump never needs the previous one's tail.
const double kThumpSeconds = 0.45;
const int kThumpLen = (int)(kThumpSeconds * MUSIC_SAMPLE_RATE) + 2;

struct Score {
    Section sec[kSections];
    std::vector<Segment> segs;
    Bank low, mid, high, bass;
    std::vector<float> thump;

    Score() {
        const double bounds[kSections + 1] = { 0, 480, 1200, 2100, 2820, 3300, 3600 };
        const double bpm[kSections] = { 120, 122, 124, 122, 122, 122 };
        double beat = 0.0;
        for (int i = 0; i < kSections; i++) {
            sec[i] = { bounds[i], bounds[i + 1], bpm[i], beat };
            beat += (bounds[i + 1] - bounds[i]) * bpm[i] / 60.0;
        }

        for (int c = 0; c < CH_COUNT; c++) {
            const ChordDef& d = kChords[c];
            for (int i = 0; i < d.count; i++) {
                if (d.lowMask & (1u << i)) low.Add(c, FoldInto(d.tones[i], 80.0, 160.0));
                if (d.midMask & (1u << i)) mid.Add(c, FoldInto(d.tones[i], 175.0, 350.0));
                if (d.highMask & (1u << i)) high.Add(c, FoldInto(d.tones[i], 390.0, 720.0));
            }
            bass.Add(c, d.bass);
        }

        // Chord timeline. Each segment's crossfade runs from its start
        // for `fade` seconds. Slow sections change on the clock; the
        // pulsed ones change on bar lines (their sections all begin on
        // whole bars).
        auto add = [&](double start, double fade, int chord) { segs.push_back({ start, fade, chord }); };
        const int prog[4] = { CH_DM9, CH_G7SUS4, CH_EM7, CH_A7SUS4 };
        for (int i = 0; i < 4; i++) add(sec[SEC_DAWN].start + 120.0 * i, i == 0 ? 0.0 : 16.0, prog[i]);
        const int barsPerChord[3] = { 4, 6, 4 };
        for (int s = SEC_MORNING; s <= SEC_AFTERNOON; s++) {
            double beatSec = 60.0 / sec[s].bpm;
            double chordLen = barsPerChord[s - SEC_MORNING] * 4.0 * beatSec;
            for (int k = 0; sec[s].start + k * chordLen < sec[s].end; k++)
                add(sec[s].start + k * chordLen, k == 0 ? 4.0 : 2.0 * beatSec, prog[k % 4]);
        }
        for (int i = 0; i < 4; i++) add(sec[SEC_DUSK].start + 120.0 * i, 16.0, prog[i]);
        add(3300, 16, CH_DM9);
        add(3375, 16, CH_EM7);
        add(3450, 16, CH_A7SUS4);
        add(3525, 16, CH_DRONE);
        add(3550, 50, CH_DM9); // blooms back into Dawn's opening chord by 3600

        thump.resize(kThumpLen);
        for (int i = 0; i < kThumpLen; i++) {
            double tau = i / kSR;
            double env = tau < 0.006 ? Ramp(tau / 0.006) : std::exp(-(tau - 0.006) / 0.11);
            env *= 1.0 - Ramp((tau - 0.30) / 0.15);
            double phase = 52.0 * tau + 52.0 * 0.035 * (1.0 - std::exp(-tau / 0.035));
            thump[i] = (float)(env * std::sin(2.0 * kPi * phase));
        }
    }

    int SectionAt(double t) const {
        for (int i = kSections - 1; i > 0; i--) if (t >= sec[i].start) return i;
        return 0;
    }
    double BeatAt(double t) const {
        const Section& s = sec[SectionAt(t)];
        return s.beatStart + (t - s.start) * s.bpm / 60.0;
    }
    void ChordWeights(double t, double w[CH_COUNT]) const {
        for (int c = 0; c < CH_COUNT; c++) w[c] = 0.0;
        size_t k = std::upper_bound(segs.begin(), segs.end(), t,
            [](double v, const Segment& s) { return v < s.start; }) - segs.begin();
        k = k == 0 ? 0 : k - 1;
        const Segment& cur = segs[k];
        double u = cur.fade > 0.0 ? (t - cur.start) / cur.fade : 1.0;
        if (u >= 1.0 || k == 0) { w[cur.chord] = 1.0; return; }
        double s = Smooth(u);
        w[cur.chord] += s;
        w[segs[k - 1].chord] += 1.0 - s;
    }
    double Thump(double tau) const {
        if (tau < 0.0 || tau >= kThumpSeconds) return 0.0;
        double x = tau * kSR;
        int i = (int)x;
        double f = x - i;
        return thump[i] + (thump[i + 1] - thump[i]) * f;
    }
};

const Score& GetScore() { static const Score s; return s; }

// ---------------------------------------------------------------------
// Note events
// ---------------------------------------------------------------------

enum : uint8_t { V_SINE, V_TRI, V_PULSE, V_SINETRI };
enum : uint8_t { L_MOTIF, L_COUNTER, L_NIGHT };

struct Note {
    double onset, attack, hold, release, freq, amp, width;
    uint8_t voice, layer;
    bool tremolo; // soft 8th-note re-articulation (the legato countermelody)
    double End() const { return onset + attack + hold + release; }
};

inline double NoteEnv(const Note& n, double tau) {
    if (tau <= 0.0) return 0.0;
    if (tau < n.attack) return Ramp(tau / n.attack);
    tau -= n.attack;
    if (tau < n.hold) return 1.0;
    tau -= n.hold;
    if (tau < n.release) return 1.0 - Ramp(tau / n.release);
    return 0.0;
}

const double kDawnMotif[4] = { 293.66, 329.63, 349.23, 440.00 };
const double kMainMotif[8] = { 293.66, 349.23, 440.00, 523.25, 440.00, 349.23, 329.63, 392.00 };
const double kCounterMelody[8] = { 440.00, 523.25, 659.26, 783.99, 659.26, 523.25, 440.00, 392.00 };
const double kAfternoonMotif[8] = { 523.25, 440.00, 349.23, 293.66, 329.63, 392.00, 440.00, 349.23 };
const double kNightTones[3] = { 587.33, 440.00, 349.23 };

// Scales a generator's notes up over its first `fadeIn` seconds and down
// over its last `fadeOut`, so a motif entering or handing off never
// appears or vanishes at full level (no sudden events).
inline double EntryExit(double onset, double start, double end, double fadeIn, double fadeOut) {
    double g = 1.0;
    if (fadeIn > 0.0) g *= Ramp((onset - start) / fadeIn);
    if (fadeOut > 0.0) g *= Ramp((end - onset) / fadeOut);
    return g;
}

inline void Keep(std::vector<Note>& out, const Note& n, double t0, double t1) {
    if (n.End() > t0 && n.onset < t1) out.push_back(n);
}

// Dawn, 4:00 on: D4-E4-F4-A4 as 8ths, each note blooming (2.5 s attack,
// 4 s release) into an overlapping cluster. Every 8 bars, every 6 in the
// last 90 s. +/-10-20 ms timing variation.
void GatherDawnMotif(double t0, double t1, std::vector<Note>& out) {
    const double barSec = 2.0; // 4 beats at 120 BPM
    int b0 = std::max(120, (int)std::floor((t0 - 7.0) / barSec));
    int b1 = std::min(239, (int)std::ceil(t1 / barSec));
    for (int b = b0; b <= b1; b++) {
        bool phrase = b < 196 ? (b - 120) % 8 == 0 : (b - 196) % 6 == 0;
        if (!phrase) continue;
        for (int i = 0; i < 4; i++) {
            double h = Hash01((uint64_t)b * 8 + i, 1);
            double jitter = (h < 0.5 ? -1.0 : 1.0) * (0.010 + 0.010 * Hash01((uint64_t)b * 8 + i, 2));
            Note n = { b * barSec + 0.25 * i + jitter, 2.5, 0.0, 4.0, kDawnMotif[i],
                       0.85 + 0.15 * Hash01((uint64_t)b * 8 + i, 3), 0.0, V_TRI, L_MOTIF, false };
            Keep(out, n, t0, t1);
        }
    }
}

// Morning/Midday: the main motif as continuous 8ths, each pitch sounded
// twice (D D F F A A C C | A A F F E E G G = one cycle per 2 bars), on a
// soft pulse wave. Note length and timing drift gradually.
void GatherPulseMotif(const Score& sc, int si, double width, double fadeIn, double fadeOut,
                      double t0, double t1, std::vector<Note>& out) {
    const Section& s = sc.sec[si];
    double eighth = 30.0 / s.bpm;
    double lo = std::max(t0 - 0.6, s.start), hi = std::min(t1, s.end);
    if (lo >= hi) return;
    long e0 = std::max(0L, (long)std::floor((lo - s.start) / eighth));
    long e1 = (long)std::ceil((hi - s.start) / eighth);
    for (long e = e0; e <= e1; e++) {
        double nominal = s.start + e * eighth;
        if (nominal >= s.end) break;
        uint64_t id = (uint64_t)si * 100000 + (uint64_t)e;
        double gate = eighth * (0.66 + 0.08 * Sin01(e / 37.0) + 0.06 * (Hash01(id, 11) - 0.5));
        Note n = { nominal + (Hash01(id, 12) - 0.5) * 0.012, 0.010, gate - 0.010, 0.09,
                   kMainMotif[(e / 2) % 8],
                   (e % 2 == 0 ? 1.0 : 0.82) * EntryExit(nominal, s.start, s.end, fadeIn, fadeOut),
                   width, V_PULSE, L_MOTIF, false };
        Keep(out, n, t0, t1);
    }
}

// Midday countermelody: A4-C5-E5-G5-E5-C5-A4-G4, one pitch per half note
// (one cycle per 4 bars), legato -- each note overlaps the next -- with a
// soft 8th-note re-articulation. Sine + soft triangle.
void GatherCounter(const Score& sc, double t0, double t1, std::vector<Note>& out) {
    const Section& s = sc.sec[SEC_MIDDAY];
    double step = 2.0 * 60.0 / s.bpm;
    double lo = std::max(t0 - step - 0.3, s.start), hi = std::min(t1, s.end);
    if (lo >= hi) return;
    long k0 = std::max(0L, (long)std::floor((lo - s.start) / step));
    long k1 = (long)std::ceil((hi - s.start) / step);
    for (long k = k0; k <= k1; k++) {
        double onset = s.start + k * step;
        if (onset >= s.end) break;
        Note n = { onset, 0.06, step + 0.05 - 0.06, 0.12, kCounterMelody[k % 8], 1.0, 0.0, V_SINETRI, L_COUNTER, true };
        Keep(out, n, t0, t1);
    }
}

// Afternoon: the transformed motif, 8th / dotted-8th alternating, one
// phrase every 3 bars -- every 6 in the last 100 s.
void GatherAfternoonMotif(const Score& sc, double t0, double t1, std::vector<Note>& out) {
    const Section& s = sc.sec[SEC_AFTERNOON];
    const double offsets[8] = { 0.0, 0.5, 1.25, 1.75, 2.5, 3.0, 3.75, 4.25 };
    double beat = 60.0 / s.bpm, phraseLen = 12.0 * beat;
    double lo = std::max(t0 - 6.0 * beat, s.start), hi = std::min(t1, s.end);
    if (lo >= hi) return;
    long p0 = std::max(0L, (long)std::floor((lo - s.start) / phraseLen));
    long p1 = (long)std::ceil((hi - s.start) / phraseLen);
    for (long p = p0; p <= p1; p++) {
        double start = s.start + p * phraseLen;
        if (start >= s.end) break;
        if (start >= 2720.0 && (p % 2) == 1) continue;
        for (int i = 0; i < 8; i++) {
            uint64_t id = (uint64_t)p * 8 + i;
            double gate = (i % 2 == 0 ? 0.5 : 0.75) * beat * 0.8;
            Note n = { start + offsets[i] * beat + (Hash01(id, 13) - 0.5) * 0.012, 0.015, gate - 0.015, 0.15,
                       kAfternoonMotif[i], (i % 2 == 0 ? 1.0 : 0.9) * EntryExit(start, s.start, s.end, 15.0, 0.0),
                       0.30, V_PULSE, L_MOTIF, false };
            Keep(out, n, t0, t1);
        }
    }
}

// Dusk: 2-3 note cells of the Afternoon motif every 16-24 s, long
// envelopes, slight timing variation.
void GatherDuskFragments(double t0, double t1, std::vector<Note>& out) {
    const double base = 2826.0, spacing = 20.0;
    long j0 = std::max(0L, (long)std::floor((t0 - 6.0 - base) / spacing) - 1);
    long j1 = (long)std::ceil((t1 - base) / spacing) + 1;
    for (long j = j0; j <= j1; j++) {
        double start = base + j * spacing + (Hash01(j, 21) - 0.5) * 4.0;
        if (start >= 3280.0) break;
        int first = (int)(Hash01(j, 22) * 8.0);
        int count = Hash01(j, 23) < 0.5 ? 2 : 3;
        for (int i = 0; i < count; i++) {
            Note n = { start + 0.55 * i + (Hash01(j * 4 + i, 24) - 0.5) * 0.03, 0.9, 0.0, 3.0,
                       kAfternoonMotif[(first + i) % 8], 1.0 - 0.14 * i, 0.0, V_TRI, L_MOTIF, false };
            Keep(out, n, t0, t1);
        }
    }
}

// Night: a single pure sine (D5, A4 or F4), 7 s envelope, 25-30 s apart.
// Stops before the final Dawn-matching crossfade.
void GatherNightTones(double t0, double t1, std::vector<Note>& out) {
    const double base = 3312.0, spacing = 27.5;
    long j0 = std::max(0L, (long)std::floor((t0 - 8.0 - base) / spacing) - 1);
    long j1 = (long)std::ceil((t1 - base) / spacing) + 1;
    for (long j = j0; j <= j1; j++) {
        double start = base + j * spacing + (Hash01(j, 31) - 0.5) * 2.5;
        if (start > 3540.0) break;
        Note n = { start, 2.5, 0.0, 4.5, kNightTones[(int)(Hash01(j, 32) * 3.0)], 1.0, 0.0, V_SINE, L_NIGHT, false };
        Keep(out, n, t0, t1);
    }
}

void GatherNotes(const Score& sc, double t0, double t1, std::vector<Note>& out) {
    GatherDawnMotif(t0, t1, out);
    GatherPulseMotif(sc, SEC_MORNING, 0.32, 20.0, 0.0, t0, t1, out); // enters over 20 s
    GatherPulseMotif(sc, SEC_MIDDAY, 0.28, 0.0, 15.0, t0, t1, out);  // continues Morning's line; hands off to Afternoon
    GatherCounter(sc, t0, t1, out);
    GatherAfternoonMotif(sc, t0, t1, out);
    GatherDuskFragments(t0, t1, out);
    GatherNightTones(t0, t1, out);
}

// ---------------------------------------------------------------------
// Control-rate state and rendering
// ---------------------------------------------------------------------

// Per-register slow amplitude drift, one period per bank slot; every
// period divides 3600 so the drift loops with the day.
const double kShimmerPeriod[kMaxBank] = { 60, 72, 80, 90, 100, 120, 144, 150 };

struct Ctrl {
    double low[kMaxBank], mid[kMaxBank], high[kMaxBank], bass[kMaxBank];
    double bassSine, bassTri, bassSaw;
    double pulse, motif, counter, night, bed, air, airTone, duck, master;
    double cutoff, q, bedFc, airFc;
};

void ComputeCtrl(const Score& sc, double t, double intensity, Ctrl& c) {
    double w[CH_COUNT];
    sc.ChordWeights(t, w);
    double gLow = kPadLowGain(t), gMid = kPadMidGain(t), gHigh = kPadHighGain(t);
    auto fill = [&](const Bank& b, double layer, double out[kMaxBank], bool shimmer) {
        for (int i = 0; i < kMaxBank; i++) out[i] = 0.0;
        if (layer <= 0.0) return;
        for (int i = 0; i < b.n; i++) {
            double m = 0.0;
            for (int ch = 0; ch < CH_COUNT; ch++) m += w[ch] * b.member[ch][i];
            if (m <= 0.0) continue;
            double s = shimmer ? 1.0 + 0.15 * Sin01(t / kShimmerPeriod[i] + 0.37 * i) : 1.0;
            out[i] = layer * m * s;
        }
    };
    fill(sc.low, gLow, c.low, false);
    fill(sc.mid, gMid, c.mid, true);
    fill(sc.high, gHigh, c.high, true);
    fill(sc.bass, 1.0, c.bass, false);

    double drift = Sin01(t / 150.0);
    c.bassSine = kBassSineGain(t) * (1.0 + 0.25 * drift);
    c.bassTri = kBassTriGain(t) * (1.0 - 0.25 * drift);
    c.bassSaw = kBassSawGain(t) * (1.0 + 0.2 * Sin01(t / 120.0 + 0.3));

    c.pulse = kPulseGain(t) * intensity;
    c.motif = kMotifGain(t) * intensity;
    c.counter = kCounterGain(t) * intensity;
    c.night = kNightGain(t) * intensity;
    c.duck = Eval(kDuckDepth, t) * (1.0 + 0.2 * Sin01(t / 150.0 + 0.5)) * intensity;
    c.bed = kBedGain(t) * (0.8 + 0.2 * Sin01(t / 45.0 + 0.1));
    c.air = kAirGain(t);
    c.airTone = c.air * (0.5 + 0.5 * Sin01(t / 40.0));
    c.master = kMasterGain(t);

    c.cutoff = EvalHz(kCutoffHz, t);
    c.q = std::min(kMaxQ, 0.70710678 + 1.2 * Eval(kResonance, t));
    c.bedFc = c.bed > 0.0 ? 500.0 * std::pow(2.0, 0.55 + 0.55 * Sin01(t / 90.0)) : 0.0;
    c.airFc = c.air > 0.0 ? 2600.0 * std::pow(2.0, 0.2 * Sin01(t / 120.0)) : 0.0;
}

inline double Lerp(double a, double b, double f) { return a + (b - a) * f; }

// One-pole tone filters softening the two saw layers.
const double kPadSawA = 1.0 - std::exp(-2.0 * kPi * 900.0 / kSR);
const double kBassSawA = 1.0 - std::exp(-2.0 * kPi * 400.0 / kSR);

// Fixed tones in the air layer (A5, E6); both x 3600 are integers.
const double kAirTone1 = 880.00, kAirTone2 = 1318.51;
const uint64_t kBedSeed = 0x1234567ULL, kAirSeed = 0x89ABCDEFULL;

// Renders `count` samples starting at wrapped time `tStart`; the caller
// guarantees the run doesn't cross the 3600 -> 0 wrap.
void RenderRun(double tStart, int count, double intensity, MusicState* st, int16_t* out) {
    const Score& sc = GetScore();
    static std::vector<Note> notes; // reused across calls: no per-chunk allocation
    notes.clear();
    GatherNotes(sc, tStart, tStart + count / kSR, notes);

    Ctrl cur, next;
    ComputeCtrl(sc, tStart, intensity, cur);

    for (int b0 = 0; b0 < count; b0 += kBlock) {
        int len = std::min(kBlock, count - b0);
        double tb = tStart + b0 / kSR;
        double te = tb + len / kSR;
        ComputeCtrl(sc, te, intensity, next);
        double invLen = 1.0 / len;

        Biquad master = Lowpass(cur.cutoff, cur.q);
        bool bedOn = cur.bed > 0.0 || next.bed > 0.0;
        bool airOn = cur.air > 0.0 || next.air > 0.0;
        Biquad bedF = {}, airF = {};
        // A layer fading in from silence has cutoff 0 at its first block;
        // use the block end's value then.
        if (bedOn) bedF = Lowpass(cur.bedFc > 0.0 ? cur.bedFc : next.bedFc, 0.7071);
        else st->bedZ1 = st->bedZ2 = 0.0;
        if (airOn) airF = Bandpass(cur.airFc > 0.0 ? cur.airFc : next.airFc, 1.6);
        else st->airZ1 = st->airZ2 = 0.0;

        // Active oscillators this block (either endpoint non-zero).
        int lowIdx[kMaxBank], midIdx[kMaxBank], highIdx[kMaxBank], bassIdx[kMaxBank];
        int nLow = 0, nMid = 0, nHigh = 0, nBass = 0;
        for (int i = 0; i < sc.low.n; i++) if (cur.low[i] > 0.0 || next.low[i] > 0.0) lowIdx[nLow++] = i;
        for (int i = 0; i < sc.mid.n; i++) if (cur.mid[i] > 0.0 || next.mid[i] > 0.0) midIdx[nMid++] = i;
        for (int i = 0; i < sc.high.n; i++) if (cur.high[i] > 0.0 || next.high[i] > 0.0) highIdx[nHigh++] = i;
        for (int i = 0; i < sc.bass.n; i++) if (cur.bass[i] > 0.0 || next.bass[i] > 0.0) bassIdx[nBass++] = i;

        // Notes sounding at any point in this block.
        static std::vector<const Note*> active; // reused: no allocation once warm
        active.clear();
        for (const Note& n : notes)
            if (n.End() > tb && n.onset < te) active.push_back(&n);
        const int nActive = (int)active.size();

        bool pulseOn = cur.pulse > 0.0 || next.pulse > 0.0;
        bool duckOn = cur.duck > 0.0 || next.duck > 0.0;
        bool needBeat = pulseOn || duckOn || cur.counter > 0.0 || next.counter > 0.0;
        int si = sc.SectionAt(tb);
        double bps = sc.sec[si].bpm / 60.0;
        double beatAtStart = needBeat ? sc.BeatAt(tb) : 0.0;
        double beatSec = 1.0 / bps;

        for (int j = 0; j < len; j++) {
            double t = tb + j / kSR;
            double f = j * invLen;
            double pre = 0.0, post = 0.0;

            // Low pad: additive soft triangle plus a saw softened by its own
            // one-pole tone filter (the shared lowpass alone would leave it
            // buzzy once the day's cutoff opens up past 1.5 kHz).
            double lowTri = 0.0, lowSaw = 0.0;
            for (int k = 0; k < nLow; k++) {
                int i = lowIdx[k];
                double fr = sc.low.freq[i], p = Frac(fr * t);
                double g = Lerp(cur.low[i], next.low[i], f);
                lowTri += g * SoftTriFrom(Sin01(p));
                lowSaw += g * BlepSaw(p, fr / kSR);
            }
            st->padSawZ += kPadSawA * (lowSaw - st->padSawZ);
            pre += 0.7 * lowTri + 0.5 * st->padSawZ;
            for (int k = 0; k < nMid; k++) {
                int i = midIdx[k];
                pre += Lerp(cur.mid[i], next.mid[i], f) * Sin01(sc.mid.freq[i] * t);
            }
            for (int k = 0; k < nHigh; k++) {
                int i = highIdx[k];
                pre += Lerp(cur.high[i], next.high[i], f) * Sin01(sc.high.freq[i] * t);
            }

            double beat = needBeat ? beatAtStart + (t - tb) * bps : 0.0;
            double beatFrac = Frac(beat);

            if (nBass > 0) {
                double gs = Lerp(cur.bassSine, next.bassSine, f);
                double gt = Lerp(cur.bassTri, next.bassTri, f);
                double gw = Lerp(cur.bassSaw, next.bassSaw, f);
                double sum = 0.0, saw = 0.0;
                for (int k = 0; k < nBass; k++) {
                    int i = bassIdx[k];
                    double fr = sc.bass.freq[i], p = Frac(fr * t), s1 = Sin01(p);
                    double g = Lerp(cur.bass[i], next.bass[i], f);
                    sum += g * (gs * s1 + gt * SoftTriFrom(s1));
                    if (gw > 0.0) saw += g * gw * BlepSaw(p, fr / kSR);
                }
                // The "soft saw": its own one-pole tone filter, same idea as
                // the low pad's.
                st->bassSawZ += kBassSawA * (saw - st->bassSawZ);
                sum += st->bassSawZ;
                if (duckOn) {
                    // Sidechain-style dip on every beat: 3% of a beat down,
                    // recovering by mid-beat, both edges raised-cosine.
                    double d = beatFrac < 0.03 ? Ramp(beatFrac / 0.03)
                             : beatFrac < 0.5 ? 1.0 - Ramp((beatFrac - 0.03) / 0.47) : 0.0;
                    sum *= 1.0 - Lerp(cur.duck, next.duck, f) * d;
                }
                pre += sum;
            }

            if (pulseOn) {
                static const double accent[4] = { 1.0, 0.78, 0.9, 0.78 };
                int beatInBar = (int)((long long)FastFloor(beat) & 3);
                pre += Lerp(cur.pulse, next.pulse, f) * accent[beatInBar] * sc.Thump(beatFrac * beatSec);
            }

            for (int k = 0; k < nActive; k++) {
                const Note& n = *active[k];
                double tau = t - n.onset;
                double env = NoteEnv(n, tau);
                if (env <= 0.0) continue;
                double p = Frac(n.freq * tau), v;
                switch (n.voice) {
                case V_SINE: v = Sin01(p); break;
                case V_TRI: v = SoftTri(p); break;
                case V_PULSE: v = 0.55 * BlepPulse(p, n.freq / kSR, n.width); break;
                default: { double s1 = Sin01(p); v = 0.65 * s1 + 0.45 * SoftTriFrom(s1); } break;
                }
                if (n.tremolo) v *= 0.9 + 0.1 * Sin01(2.0 * beat + 0.25);
                double layer = n.layer == L_MOTIF ? Lerp(cur.motif, next.motif, f)
                             : n.layer == L_COUNTER ? Lerp(cur.counter, next.counter, f)
                             : Lerp(cur.night, next.night, f);
                v *= env * n.amp * layer;
                if (n.layer == L_NIGHT) post += v; else pre += v;
            }

            if (bedOn || airOn) {
                uint64_t si64 = (uint64_t)(t * kSR + 0.5);
                if (bedOn) post += RunBiquad(bedF, Noise(si64, kBedSeed) * Lerp(cur.bed, next.bed, f), st->bedZ1, st->bedZ2);
                if (airOn) {
                    post += RunBiquad(airF, Noise(si64, kAirSeed) * Lerp(cur.air, next.air, f), st->airZ1, st->airZ2);
                    post += Lerp(cur.airTone, next.airTone, f) * (0.35 * Sin01(kAirTone1 * t) + 0.25 * Sin01(kAirTone2 * t));
                }
            }

            double y = RunBiquad(master, pre, st->masterZ1, st->masterZ2) + post;
            double fade = 1.0;
            if (st->secondsSinceStart < kResumeFadeSeconds) {
                fade = Ramp(st->secondsSinceStart / kResumeFadeSeconds);
                st->secondsSinceStart += 1.0 / kSR;
            }
            y *= Lerp(cur.master, next.master, f) * kOutputScale * fade;
            if (y > 1.0) y = 1.0;
            if (y < -1.0) y = -1.0;
            out[b0 + j] = (int16_t)(y * 32767.0);
        }

        FlushDenormal(st->masterZ1); FlushDenormal(st->masterZ2);
        FlushDenormal(st->bedZ1); FlushDenormal(st->bedZ2);
        FlushDenormal(st->airZ1); FlushDenormal(st->airZ2);
        FlushDenormal(st->padSawZ); FlushDenormal(st->bassSawZ);
        cur = next;
    }
}

} // namespace

void ResetMusicState(MusicState* s) {
    *s = MusicState{};
}

void GenerateMusicChunk(double startTime, int sampleCount, double intensity, MusicState* state, int16_t* outPCM) {
    double t = std::fmod(startTime, kDayLength);
    if (t < 0.0) t += kDayLength;
    int done = 0;
    while (done < sampleCount) {
        // Samples left before the day wraps; the next run starts just past 0.
        int untilWrap = (int)std::ceil((kDayLength - t) * kSR);
        if (untilWrap <= 0) { t -= kDayLength; continue; }
        int n = std::min(sampleCount - done, untilWrap);
        RenderRun(t, n, intensity, state, outPCM + done);
        done += n;
        t += n / kSR;
        if (t >= kDayLength) t -= kDayLength;
    }
}
