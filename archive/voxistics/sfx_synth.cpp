// sfx_synth.cpp
//
// The world sound palette -- see sfx_synth.h and docs/SOUND_PALETTE.md,
// which this file implements section by section. Structure:
//
//  - Harmony helpers: safe sets, roles and ladders per chord (spec 1.2, 2).
//  - Voice: one partial-voice of the shared kit (sine, soft triangle,
//    blend, pulse, morph, saw, noise, formant VOX, BELL, THUMP) with its
//    envelope, glide, filters and modulation, rendered per sample with
//    slow parameters per 64-sample control block (like the music).
//  - Recipes: one function per sound, building its voices from the cue,
//    the harmony and the axes (spec 5).
//  - Gestures, merge, repeat softening, the interval rule, the voice
//    ceiling and the tier hierarchy (spec 4).
//  - The ambient scheduler: once per bar, spends the activity-driven
//    budget on accents, textures and rare colour for the next bar (spec 3.2).
//  - The bus: sidechain dip, the tempo-synced echo, the day's master level.

#include "sfx_synth.h"
#include "synth_kit.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace synth;

namespace {

const int kBlock = 64;
const int kMaxVoices = 48;
const int kEchoLen = 1 << 16;          // 1.49 s: longer than a dotted 8th at any tempo here
const double kOutputScale = 0.78;      // the music's own output scale
const double kCeilingDb = -21.0;       // no palette sound louder (spec 1.7)
const double kBarGapSeconds = 2.2;     // a gesture ends after ~a bar of quiet

inline double MidiHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
inline double HzMidi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }
inline double Cents(double c) { return std::pow(2.0, c / 1200.0); }
inline double Db(double db) { return std::pow(10.0, db / 20.0); }
inline double Clamp(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double Lerp(double a, double b, double f) { return a + (b - a) * f; }

// ---------------------------------------------------------------------
// Harmony (spec 1.2)
// ---------------------------------------------------------------------

enum { PC_C = 0, PC_D = 2, PC_E = 4, PC_F = 5, PC_G = 7, PC_A = 9, PC_B = 11 };
constexpr uint16_t Bit(int pc) { return (uint16_t)(1u << pc); }
const uint16_t kChordMask[MUSIC_CHORD_COUNT] = {
    Bit(PC_D) | Bit(PC_F) | Bit(PC_A) | Bit(PC_C) | Bit(PC_E), // Dm9
    Bit(PC_G) | Bit(PC_C) | Bit(PC_D) | Bit(PC_F) | Bit(PC_A), // G7sus4
    Bit(PC_E) | Bit(PC_G) | Bit(PC_B) | Bit(PC_D),             // Em7
    Bit(PC_A) | Bit(PC_D) | Bit(PC_E) | Bit(PC_G) | Bit(PC_C), // A7sus4
    Bit(PC_D) | Bit(PC_A),                                     // drone
};
// Every scale tone not a semitone from a sounding chord tone.
const uint16_t kSafeMask[MUSIC_CHORD_COUNT] = {
    Bit(PC_D) | Bit(PC_E) | Bit(PC_F) | Bit(PC_G) | Bit(PC_A) | Bit(PC_C),
    Bit(PC_G) | Bit(PC_A) | Bit(PC_C) | Bit(PC_D) | Bit(PC_F),
    Bit(PC_E) | Bit(PC_G) | Bit(PC_A) | Bit(PC_B) | Bit(PC_D),
    Bit(PC_A) | Bit(PC_C) | Bit(PC_D) | Bit(PC_E) | Bit(PC_G),
    Bit(PC_D) | Bit(PC_E) | Bit(PC_F) | Bit(PC_G) | Bit(PC_A) | Bit(PC_C),
};
// Safe under every chord: root, 4th, 5th of the key.
const uint16_t kAnchorMask = Bit(PC_D) | Bit(PC_G) | Bit(PC_A);

enum Role { R_ROOT, R_THIRD, R_FIFTH, R_SEVENTH, R_COLOUR, R_FOURTH, R_COUNT };
// Each chord's tone for each role. "Third" is the sus4 in the sus chords
// (they have none); "colour" is the brightest safe non-triad tone (the 9th
// where the scale has it, the 11th or the minor 3rd where it doesn't).
const int kRolePc[MUSIC_CHORD_COUNT][R_COUNT] = {
    { PC_D, PC_F, PC_A, PC_C, PC_E, PC_G }, // Dm9
    { PC_G, PC_C, PC_D, PC_F, PC_A, PC_C }, // G7sus4
    { PC_E, PC_G, PC_B, PC_D, PC_A, PC_A }, // Em7
    { PC_A, PC_D, PC_E, PC_G, PC_C, PC_D }, // A7sus4
    { PC_D, PC_F, PC_A, PC_C, PC_E, PC_G }, // drone (Dm9's colours: it blooms into Dm9)
};
// Mid-crossfade, roles fall back to the anchor set.
const int kAnchorRolePc[R_COUNT] = { PC_D, PC_A, PC_A, PC_G, PC_A, PC_G };

inline int AtOrAbove(int pc, int floorMidi) {
    int m = floorMidi + ((pc - floorMidi % 12) % 12 + 12) % 12;
    return m;
}

// Chord-tone ladder from A3 to A6; index 0 is at D5 (spec 2).
struct Ladder {
    int midi[40];
    int n = 0, base = 0;
    void Build(uint16_t mask) {
        n = 0; base = -1;
        for (int m = 57; m <= 93; m++)
            if (mask & Bit(m % 12)) {
                if (base < 0 && m >= 74) base = n;
                midi[n++] = m;
            }
        if (base < 0) base = n - 1;
    }
    int Clamp(int idx) const { int i = base + idx; return i < 0 ? 0 : i >= n ? n - 1 : i; }
    double Hz(int idx) const { return MidiHz(midi[Clamp(idx)]); }
    int Top() const { return n - 1 - base; }
    int Bottom() const { return -base; }
};

// ---------------------------------------------------------------------
// Voices (spec 1.3, 2)
// ---------------------------------------------------------------------

enum Wave : uint8_t { W_SINE, W_STRI, W_BLEND, W_PULSE, W_MORPH, W_SAW, W_NOISE, W_VOX, W_BELL, W_THUMP };
enum EnvMode : uint8_t { ENV_AHR, ENV_AD, ENV_SUSTAIN };
enum Glide : uint8_t { GL_NONE, GL_RAMP, GL_EXP };
enum Vowel : uint8_t { V_OO, V_OH, V_AH, V_EH, V_MM };
const float kVowelF1[] = { 300, 450, 700, 530, 250 };
const float kVowelF2[] = { 870, 800, 1150, 1850, 0 };

inline bool Tonal(uint8_t w) { return w != W_NOISE; }

struct Voice {
    bool on = false;
    uint8_t wave = W_SINE;
    SoundId id = SND_SET;
    uint8_t tier = 1;
    uint32_t group = 0;
    int note = 0;              // which note of its sound (jitter is per note)
    bool raw = false;          // skip the global axis shaping (system sounds)
    int64_t start = 0, end = 0;
    // Envelope (seconds)
    uint8_t env = ENV_AHR;
    float atk = 0.006f, hold = 0, rel = 0.1f, tau = 0.1f;
    int64_t relAt = -1;        // ENV_SUSTAIN: release began here
    float gain = 0;
    float echo = 0;
    float duck = 0.5f;         // share of the bass's sidechain dip
    // Pitch
    double hz = 440, hzFrom = 440;
    uint8_t glide = GL_NONE;
    float glideTime = 0;       // GL_RAMP: seconds; GL_EXP: time constant
    float glideStep = 0;       // cents quantum for a stepped (mechanical) glide
    float vibCents = 0, wowCents = 0;
    float twinCents = 0;       // detuned twin oscillator (+-)
    float morph = 0, width = 0.34f;
    // Colour
    float metal = -1;          // bar-mode partials, re body (-1: from the M axis)
    float p2 = 0, p3 = 0, pxRatio = 0, px = 0, partTau = 0.3f; // BELL partials
    float breath = -1;         // noise near 2f (-1: from the M axis)
    // Filters
    float pluck0 = 0, pluck1 = 0, pluckTau = 0.06f; // tone lowpass as multiples of hz (0 = none)
    float lp0 = 0, lp1 = 0, lpTime = 0;             // fixed tone lowpass, gliding lp0 -> lp1
    float capOct = 0;                               // octaves above the bus cap
    float q = -1;                                   // tone lowpass Q (-1: from the M axis)
    float bp0 = 0, bp1 = 0, bpTime = 0, bpQ = 0;    // noise band (bpQ 0 = lowpass at bp0)
    // VOX
    uint8_t vowel0 = V_AH, vowel1 = V_AH;
    float vowelAt = 0, vowelTime = 0.06f;
    bool vowelStep = false;
    float sawMix = 0.5f;
    // Amplitude modulation
    float tremDepth = 0, tremBeats = 0, tremHz = 0;
    bool tremGate = false;
    // Chord following (textures)
    int8_t followRole = -1;
    int followFloor = 0;
    bool followBass = false;   // follow the bass note (x followMul)
    float followMul = 1;
    float followGlide = 1.5f;
    // --- state ---
    double ph = 0, phT = 0, phM1 = 0, phM2 = 0, phX = 0;
    double target = 0, glideFrom = 0; int64_t glideAt = -1; // chord-follow glide
    double curHz = 0;
    double z[4][2] = {};
    Biquad f[4] = {};
    uint64_t seed = 0, n = 0;
    float fade = 1; int64_t fadeStart = -1, fadeLen = 1;
    float boost = 1;
    bool merged = false;
    float pan = 0;             // -1 left .. +1 right
    bool panSet = false;       // set by the recipe itself (else the sound's placement)
};

double NoiseNorm(double bw) {
    double norm = 0.3 / (0.577 * std::sqrt(std::max(bw, 20.0) / 22050.0));
    return std::min(norm, 14.0);
}

// Envelope level at tau seconds after onset.
double EnvAt(const Voice& v, double t, double nowSec) {
    if (t < 0) return 0;
    if (t < v.atk) return Ramp(t / v.atk);
    double u = t - v.atk;
    switch (v.env) {
    case ENV_AHR:
        if (u < v.hold) return 1;
        u -= v.hold;
        return u < v.rel ? 1.0 - Ramp(u / v.rel) : 0.0;
    case ENV_AD: {
        double len = 5.0 * v.tau;
        if (u >= len) return 0;
        double e = std::exp(-u / v.tau);
        if (u > len - v.tau) e *= 1.0 - Ramp((u - (len - v.tau)) / v.tau);
        return e;
    }
    default: { // sustain until released
        if (v.relAt < 0) return 1;
        double r = nowSec;
        return r < v.rel ? 1.0 - Ramp(r / v.rel) : 0.0;
    }
    }
}
double EnvLength(const Voice& v) {
    switch (v.env) {
    case ENV_AHR: return v.atk + v.hold + v.rel;
    case ENV_AD: return v.atk + 5.0 * v.tau;
    default: return v.atk + 4.0 + v.rel; // a safety cap on a sustain nobody released
    }
}

} // namespace

// ---------------------------------------------------------------------
// The palette
// ---------------------------------------------------------------------

struct SoundPalette::Impl {
    Voice v[kMaxVoices];
    int64_t now = 0;                 // next sample to render
    uint32_t groupCounter = 1;
    uint64_t eventCounter = 0;
    SoundAxes axes;
    AmbientScene scene;
    float intensity = 1;
    bool ambientOn = false;
    MusicHarmony h = {};
    bool haveH = false;
    double musicTime = 0;            // at `now`
    bool running = false;
    // Gesture (spec 4)
    int gIdx = 0; int64_t gLast = -1000000000; double gStartHz = 0; bool gOpen = false;
    // Merge / repeat softening per sound
    int64_t lastOnset[SND_COUNT];
    uint32_t lastGroup[SND_COUNT];
    int repeat[SND_COUNT];
    int64_t cooldownUntil[SND_COUNT];
    // Hierarchy
    int64_t tier2Until = 0, lastTier2 = -1000000000;
    // Scheduler
    int64_t lastBar = INT64_MIN; float tokens = 0; int scheduled = 0;
    int64_t lastPhrase = INT64_MIN;
    bool wasEnclosed = false;
    int64_t accentUntilBar[SND_COUNT];
    double prevMusicTime = -1;
    // Recent tonal onsets (the interval rule, and the tests)
    NoteLog log[64]; int logN = 0, logHead = 0;
    // Bus
    // A ping-pong echo: each repeat crosses to the other side.
    std::vector<float> echo = std::vector<float>(kEchoLen, 0.0f), echoB = std::vector<float>(kEchoLen, 0.0f);
    int echoW = 0; double echoDelay = 0.369 * kSR; double e1 = 0, e2 = 0, f1 = 0, f2 = 0;
    // Listener (stereo placement).
    float lx = 0, ly = 0, lz = 0, lyaw = 0;
    bool mono = false;
    float busFade = 1; int64_t busFadeStart = -1, busFadeLen = 1;
    Biquad busLp = Lowpass(4200.0, 0.7071); // the brightness ceiling (spec 1.6), two stages per side
    double bz[4][2] = {};
    double lastEnergy = 0;           // for Silent()
    int64_t quietSince = 0;
    float capBoost = 1; int64_t capBoostUntil = 0; // Rift closed: the lowpass opens for 2 bars
    bool pendingCadence = false;     // a Set streak reached its octave
    // Footsteps on the beat.
    int gait = 0; SoundMaterial gaitGround = MAT_NONE;
    double nextStepBeat = -1; uint32_t steps = 0;
    int played[SND_COUNT] = {};
    int mendStep = 0;                // Mending walks the main motif

    Impl() { ResetAll(); }
    void ResetAll() {
        for (auto& x : v) x = Voice();
        std::fill(echo.begin(), echo.end(), 0.0f);
        std::fill(echoB.begin(), echoB.end(), 0.0f);
        e1 = e2 = f1 = f2 = 0; echoW = 0;
        std::memset(bz, 0, sizeof(bz));
        for (int i = 0; i < SND_COUNT; i++) { lastOnset[i] = -1000000000; lastGroup[i] = 0; repeat[i] = 0; cooldownUntil[i] = 0; accentUntilBar[i] = INT64_MIN; }
        gIdx = 0; gLast = -1000000000; gOpen = false;
        tier2Until = 0; lastTier2 = -1000000000;
        lastBar = INT64_MIN; lastPhrase = INT64_MIN; tokens = 0;
        logN = logHead = 0;
        busFade = 1; busFadeStart = -1;
        prevMusicTime = -1;
        wasEnclosed = false;
        capBoost = 1; capBoostUntil = 0;
    }

    // ---- time -------------------------------------------------------
    double Bps() const { return h.bpm / 60.0; }
    double BeatSec() const { return 60.0 / h.bpm; }
    int64_t Samples(double sec) const { return (int64_t)std::llround(sec * kSR); }
    // Seconds from now until the next multiple of `grid` beats at least
    // `minAhead` seconds away.
    double UntilGrid(double grid, double minAhead = 0.0) const {
        double b = h.beat + minAhead * Bps();
        double next = std::ceil(b / grid - 1e-6) * grid;
        return (next - h.beat) / Bps();
    }
    double UntilDownbeat(double minAhead = 0.0) const { return UntilGrid(4.0, minAhead); }
    // The grid a scheduled event quantises to at this activity (spec 3.2).
    double AmbientGrid() const { float A = axes.activity; return A < 0.33f ? 2.0 : A < 0.66f ? 1.0 : 0.5; }

    // ---- harmony ------------------------------------------------------
    uint16_t SafeMask() const { return h.blending ? kAnchorMask : kSafeMask[h.chord]; }
    uint16_t ChordToneMask() const { return h.blending ? kAnchorMask : (uint16_t)(kChordMask[h.chord == MUSIC_DRONE ? MUSIC_DM9 : h.chord]); }
    // The negative pool: root, 4th, 5th, flat 7th (open, hollow).
    uint16_t HollowMask() const {
        if (h.blending) return kAnchorMask;
        int c = h.chord;
        int rootPc = kRolePc[c][R_ROOT];
        return (uint16_t)(Bit(rootPc) | Bit((rootPc + 5) % 12) | Bit((rootPc + 7) % 12) | Bit((rootPc + 10) % 12)) & kSafeMask[c];
    }
    int RolePc(int role) const { return h.blending ? kAnchorRolePc[role] : kRolePc[h.chord][role]; }
    double RoleHz(int role, int floorMidi) const { return MidiHz(AtOrAbove(RolePc(role), floorMidi)); }
    double BassHz() const { return h.bassHz; }
    bool Safe(double hz) const {
        int m = (int)std::lround(HzMidi(hz));
        return (SafeMask() & Bit(((m % 12) + 12) % 12)) != 0;
    }
    // The nearest safe tone at or below (dir -1) / above (dir +1) a midi note.
    double SafeNear(int midi, int dir) const {
        uint16_t mask = SafeMask();
        for (int k = 0; k < 12; k++) { int m = midi + dir * k; if (mask & Bit(((m % 12) + 12) % 12)) return MidiHz(m); }
        return MidiHz(midi);
    }
    Ladder MakeLadder(bool hollowIfNegative = true) const {
        Ladder L;
        L.Build(hollowIfNegative && axes.positive < -0.25f ? HollowMask() : ChordToneMask());
        return L;
    }
    // The interval rule (spec 4): step along the ladder until the note is
    // not a 2nd from any palette pitch still ringing.
    int ClearOfClash(const Ladder& L, int idx, int dir) const {
        for (int tries = 0; tries < 4; tries++) {
            double hz = L.Hz(idx);
            bool clash = false;
            for (int k = 0; k < logN; k++) {
                const NoteLog& e = log[k];
                if (now - e.onset > Samples(0.3)) continue;
                double semis = std::fabs(HzMidi(hz) - HzMidi(e.hz));
                if (semis > 0.5 && semis < 2.5) { clash = true; break; }
            }
            if (!clash) return idx;
            int next = idx + dir;
            if (next > L.Top() || next < L.Bottom()) return idx;
            idx = next;
        }
        return idx;
    }

    // ---- voices -------------------------------------------------------
    Voice* Alloc(uint8_t tier) {
        for (auto& x : v) if (!x.on) return &x;
        if (tier >= TIER_ACCENT) return nullptr; // the ceiling: ambience yields
        // Steal the quietest voice of the lowest priority (a tail, usually).
        Voice* best = nullptr; double bestScore = 1e30;
        for (auto& x : v) {
            double t = (double)(now - x.start) / kSR;
            double lvl = x.start > now ? 0.5 : EnvAt(x, t, (double)(now - x.relAt) / kSR) * x.gain;
            double score = lvl - x.tier * 10.0;
            if (score < bestScore) { bestScore = score; best = &x; }
        }
        return best;
    }

    void LogNote(SoundId id, double hz, int64_t onset) {
        NoteLog& e = log[logHead];
        e = { id, hz, h.chord, h.blending, onset };
        logHead = (logHead + 1) % 64;
        if (logN < 64) logN++;
    }

    // Builder state for the sound being made.
    struct Build {
        SoundId id; uint8_t tier; uint32_t group; uint64_t ev; double levelAdj; int64_t at;
        float pan, dist; // where the sound sits: stereo position, distance gain
    } b = {};

    Voice* New(uint8_t wave, double hz, double delaySec, double levelDb, int note = 0) {
        Voice* x = Alloc(b.tier);
        if (!x) return nullptr;
        *x = Voice();
        x->on = true;
        x->wave = wave;
        x->id = b.id; x->tier = b.tier; x->group = b.group; x->note = note;
        x->hz = x->hzFrom = x->curHz = hz;
        x->start = b.at + Samples(std::max(0.0, delaySec));
        x->gain = (float)Db(std::min(levelDb + b.levelAdj, kCeilingDb));
        x->seed = Mix64(b.ev * 0x9E3779B97F4A7C15ULL + (uint64_t)(x - v) * 7919 + 1);
        if (Tonal(wave) && wave != W_THUMP) LogNote(b.id, hz, x->start);
        return x;
    }

    // Axis shaping every voice of the new sound gets (spec 3.2).
    void Shape() {
        const float P = axes.positive, A = axes.activity, M = axes.mechanical;
        double neg = P < 0 ? -P : 0;
        for (auto& x : v) {
            if (!x.on || x.group != b.group) continue;
            // Onset jitter, per note: interactions only ever late.
            double j = Hash01(b.ev * 131 + x.note, 7);
            double jit = b.tier == TIER_INTERACTION ? j * (0.009 * (1 - M) + 0.0005)
                                                    : (j - 0.5) * 2.0 * (0.001 + 0.017 * (1 - M));
            x.start = std::max(now, x.start + Samples(jit));
            if (x.raw) { x.q = x.q < 0 ? 0.7071f : x.q; x.metal = std::max(0.0f, x.metal); x.breath = std::max(0.0f, x.breath); continue; }
            x.morph = x.wave == W_VOX ? M : (x.wave == W_MORPH ? M : x.morph);
            float atkMin = x.wave == W_NOISE ? 0.003f : 0.006f;
            x.atk = std::max(atkMin, x.atk * (1.25f - 0.5f * M));
            float relScale = (1.15f - 0.3f * M) * (1.25f - 0.5f * A);
            x.rel *= relScale;
            x.tau *= relScale;
            if (x.q < 0) x.q = 0.7071f + 0.3f * M;
            if (x.metal < 0) x.metal = Tonal(x.wave) && x.wave != W_THUMP ? (float)Db(-34 + 14 * M) : 0.0f;
            if (x.breath < 0) x.breath = Tonal(x.wave) && x.wave != W_THUMP && x.wave != W_VOX ? (float)((1 - M) * Db(-26)) : 0.0f;
            if (x.wave != W_THUMP) x.wowCents += (float)(6.0 * neg);
            double var = (Hash01(b.ev * 131 + x.note, 9) - 0.5) * 2.0 * (0.5 + 2.0 * (1 - M));
            x.gain *= (float)(Db(-1.5 * neg + var));
            x.echo *= 1.0f - 0.5f * A;
        }
    }

    // A voice's peak for a unit gain (a bell's partials stack at onset).
    static double PeakFactor(const Voice& x) {
        double f = x.wave == W_BELL ? 1.0 + x.p2 + x.p3 + x.px : x.wave == W_VOX ? 1.4 : 1.0;
        return f + std::max(0.0f, x.metal) + std::max(0.0f, x.breath) * 0.5;
    }
    void Finish() {
        Shape();
        for (auto& x : v) {
            if (!x.on || x.group != b.group) continue;
            x.end = x.start + Samples(EnvLength(x)) + 64;
            x.gain *= x.boost * b.dist;
            if (!x.panSet) x.pan = b.pan;
            if (x.wave == W_BELL) x.gain /= (float)(1.0 + x.p2 + x.p3 + x.px); // level = peak
        }
        // The ceiling is per sound, not per voice (spec 1.7): voices of this
        // sound starting within 40 ms of each other add up; if they could
        // pass -21 dB, the whole sound comes down together.
        double worst = 0;
        for (auto& a : v) {
            if (!a.on || a.group != b.group) continue;
            // Everything of this sound sounding at a's peak, at its level then.
            int64_t at = a.start + Samples(a.atk);
            double sum = 0;
            for (auto& c : v) {
                if (!c.on || c.group != b.group || c.start > at) continue;
                double e = EnvAt(c, (double)(at - c.start) / kSR, 0.0);
                sum += e * c.gain * PeakFactor(c);
            }
            worst = std::max(worst, sum);
        }
        double ceiling = Db(kCeilingDb - 1.5); // margin for the echo's return
        if (worst > ceiling)
            for (auto& x : v) if (x.on && x.group == b.group) x.gain *= (float)(ceiling / worst);
        lastOnset[b.id] = b.at;
        lastGroup[b.id] = b.group;
        played[b.id]++;
        if (b.tier == TIER_EVENT) {
            int64_t endMax = b.at;
            for (auto& x : v) if (x.on && x.group == b.group) endMax = std::max(endMax, x.end);
            tier2Until = std::max(tier2Until, endMax);
            lastTier2 = b.at;
        }
    }

    // ---- recipes (spec 5) --------------------------------------------
    void Recipe(const SoundCue& c);

    // Body, sub and grain for a block's material (spec 5.2 table).
    void Tint(Voice* body, Voice* sub, Voice* grain, SoundMaterial mat) {
        if (!body) return;
        switch (mat) {
        case MAT_EARTH: body->pluck0 *= 0.7f; body->pluck1 *= 0.7f; if (grain) grain->bp0 = 900; body->metal = 0; break;
        case MAT_STONE: body->tau *= 0.7f; if (grain) grain->bp0 = 1600; break;
        case MAT_WOOD: body->lp0 = body->lp1 = 900; body->q = 1.2f; body->tau *= 1.2f; break;
        case MAT_PLANT:
            if (sub) sub->gain = 0;
            body->wave = W_SINE; body->hz *= 2; body->hzFrom = body->curHz = body->hz; body->gain *= 0.5f; break;
        case MAT_GLASS:
            body->wave = W_BELL; body->p2 = 0.3f; body->p3 = 0.12f; body->partTau = 0.3f; body->tau *= 1.6f; body->capOct = 0.5f;
            body->pluck0 = body->pluck1 = 0; break;
        case MAT_METAL: body->wave = W_PULSE; body->metal = (float)Db(-14); break;
        case MAT_FLESH: body->pluck0 *= 0.5f; body->pluck1 *= 0.5f; body->gain *= (float)Db(-3); body->atk = 0.04f; if (grain) grain->bp0 = 500; break;
        case MAT_GENESIS: {
            body->wave = W_BLEND;
            Voice* s = New(W_SINE, body->hz * 2, (double)(body->start - b.at) / kSR, -36);
            if (s) { s->env = ENV_AD; s->atk = 0.01f; s->tau = body->tau * 1.5f; s->echo = 0.3f; }
            break;
        }
        default: break;
        }
    }

    Voice* Thump(double delay, double levelDb, double fromMul = 2.0, double tau = 0.09, double glideTau = 0.035) {
        // The kick already carries the sub near a beat (spec 1.5).
        if (h.pulsed) {
            double beatAt = h.beat + delay * Bps();
            double off = std::fabs(beatAt - std::round(beatAt)) * BeatSec();
            if (off < 0.06) return nullptr;
        }
        Voice* t = New(W_THUMP, BassHz(), delay, levelDb);
        if (!t) return nullptr;
        t->hzFrom = BassHz() * fromMul; t->glide = GL_EXP; t->glideTime = (float)glideTau;
        t->env = ENV_AD; t->atk = 0.003f; t->tau = (float)tau;
        t->duck = 0; t->raw = true; t->q = 0.7071f; t->metal = 0; t->breath = 0;
        return t;
    }
    Voice* Grain(double delay, double levelDb, double lpHz, double tau) {
        Voice* g = New(W_NOISE, 0, delay, levelDb);
        if (!g) return nullptr;
        g->env = ENV_AD; g->atk = 0.003f; g->tau = (float)tau;
        g->bp0 = g->bp1 = (float)lpHz; g->bpQ = 0;
        return g;
    }
    Voice* Bell(double hz, double delay, double levelDb, double tau, int note = 0) {
        Voice* x = New(W_BELL, hz, delay, levelDb, note);
        if (!x) return nullptr;
        x->env = ENV_AD; x->atk = 0.006f; x->tau = (float)tau;
        x->p2 = 0.3f; x->p3 = 0.12f; x->partTau = (float)(tau * 0.5);
        return x;
    }
    Voice* Vox(double hz, double delay, double levelDb, uint8_t v0, uint8_t v1, int note = 0) {
        Voice* x = New(W_VOX, hz, delay, levelDb, note);
        if (!x) return nullptr;
        x->vowel0 = v0; x->vowel1 = v1;
        x->vowelStep = axes.mechanical > 0.6f;
        x->vibCents = (float)(2.0 + 8.0 * (1.0 - axes.mechanical));
        x->breath = (float)(Db(-20) * (1.0 - 0.7 * axes.mechanical));
        x->duck = 0.5f;
        return x;
    }
    // Tier-2 sounds wait for the next beat (half-bar when calm).
    double EventDelay() const { return UntilGrid(axes.activity < 0.33f ? 2.0 : 1.0, 0.02); }

    // Recipes, grouped as in the spec.
    void Rise(); void Hey(); void Hum(); void Bloom(double delay, double levelDb); void Answer(double delay); void Sigh();
    void Set(const SoundCue& c); void Take(const SoundCue& c); void Slot(const SoundCue& c);
    void LibraryOpen(); void LibraryClose(); void Pick(); void Drop(const SoundCue& c); void Cant();
    void Land(const SoundCue& c); void Slide();
    void Unveil(); void Horizon(); void Glint(); void Vein(double delay, double levelDb); void Timeslip(); void Omen();
    void Footfall(const SoundCue& c); void Works(double barDelay); void Drip(double delay); void Ember(double barDelay);
    void Clave(double barDelay); void Heartbeat(double barDelay);
    void Wind(double delay); void Chirps(double delay); void NightShimmer(double delay); void Mushroom(double delay);
    void WorksHum(double delay); void Worn(double delay); void Enclosure(double delay);
    void Cadence(); void Mending(); void Online(); void Sealed(); void RiftClosed();
    void FarBell(double delay); void Glimmer(double delay); void FallingStars(double delay); void Murmur(double delay);
    void FarCall(double delay); void Seam();

    // ---- scheduling ----------------------------------------------------
    void Begin(SoundId id, double levelAdj = 0.0) {
        b.id = id; b.tier = SoundTierOf(id); b.group = groupCounter++; b.ev = ++eventCounter;
        b.at = now; b.levelAdj = levelAdj;
        b.dist = 1.0f;
        // Unplaced ambience spreads gently across the field; the rest sits centre.
        b.pan = b.tier >= TIER_ACCENT ? (float)((Hash01(b.ev, 77) - 0.5) * 1.0) : 0.0f;
        // Music Intensity scales the ambience -- not footsteps, which are the player's own.
        if (b.tier >= TIER_ACCENT && id != SND_FOOTFALL) b.levelAdj += 20.0 * std::log10(std::max(0.001f, intensity));
    }
    void PlayCue(const SoundCue& c);
    // Stereo placement of the sound being built: pan by the source's side
    // of the listener, gain by distance (gentle: a nearby sound barely drops).
    void Place(float x, float y, float z) {
        float dx = x - lx, dy = y - ly, dz = z - lz;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        float h = std::sqrt(dx * dx + dz * dz);
        float rx = std::cos(lyaw), rz = -std::sin(lyaw); // the listener's right, on the ground
        b.pan = h > 0.01f ? (dx * rx + dz * rz) / h : 0.0f;
        b.dist = (float)(1.0 / (1.0 + std::max(0.0, d - 3.0) / 8.0));
    }
    void Ambient(double delay, SoundId id);
    void OnBar(int64_t bar);
    void OnPhrase(int64_t phrase);
    void OnBlock();

    void RenderBlock(float* out, int len);
};

namespace {
const char* kNames[SND_COUNT] = {
    "rise", "hey", "hum", "bloom", "answer", "sigh",
    "set", "take", "slot", "library open", "library close", "pick", "drop", "can't", "land", "slide",
    "unveil", "horizon", "glint", "vein", "timeslip", "omen",
    "footfall", "works", "drip", "ember", "clave", "heartbeat",
    "wind", "chirps", "night shimmer", "mushroom", "works hum", "worn", "enclosure",
    "cadence", "mending", "online", "sealed", "rift closed",
    "far bell", "glimmer", "falling stars", "murmur", "far call", "seam",
};
const uint8_t kTiers[SND_COUNT] = {
    TIER_EVENT, TIER_ACCENT, TIER_TEXTURE, TIER_EVENT, TIER_ACCENT, TIER_TEXTURE,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    TIER_EVENT, TIER_EVENT, TIER_EVENT, TIER_EVENT, TIER_EVENT, TIER_EVENT,
    TIER_ACCENT, TIER_ACCENT, TIER_ACCENT, TIER_ACCENT, TIER_ACCENT, TIER_ACCENT,
    TIER_TEXTURE, TIER_TEXTURE, TIER_TEXTURE, TIER_TEXTURE, TIER_TEXTURE, TIER_TEXTURE, TIER_TEXTURE,
    TIER_EVENT, TIER_ACCENT, TIER_EVENT, 1, TIER_EVENT,
    TIER_RARE, TIER_RARE, TIER_RARE, TIER_RARE, TIER_RARE, TIER_RARE,
};
} // namespace

const char* SoundName(SoundId id) { return id < SND_COUNT ? kNames[id] : "?"; }
SoundTier SoundTierOf(SoundId id) { return id < SND_COUNT ? (SoundTier)kTiers[id] : TIER_RARE; }

// =====================================================================
// 5.1 Vocal-style ad-libs
// =====================================================================

void SoundPalette::Impl::Rise() {
    const float P = axes.positive, A = axes.activity;
    Ladder L = MakeLadder();
    // The first note sits an 8th (calm) or a 16th (active) before the next downbeat.
    double pickup = (A > 0.6f ? 0.25 : 0.5) * BeatSec();
    double down = UntilDownbeat(pickup + 0.02);
    bool fall = P < 0;
    double hz1 = L.Hz(fall ? 4 : 4) * 0.5, hz2 = L.Hz(fall ? 2 : 5) * 0.5;
    Voice* a = Vox(hz1, down - pickup, -26, fall ? V_OH : V_OO, fall ? V_OO : V_AH, 0);
    if (a) { a->atk = 0.06f; a->hold = 0.18f; a->rel = 0.22f; a->vowelAt = 0.02f; a->vowelTime = 0.2f; a->echo = 0.5f; }
    bool drop = P < -0.4f && Hash01(b.ev, 41) < (-P - 0.4) / 0.6 * 0.5;
    if (!drop) {
        Voice* c = Vox(hz2, down, -26, fall ? V_OO : V_AH, fall ? V_OO : V_AH, 1);
        if (c) { c->atk = 0.08f; c->hold = 0.3f; c->rel = 0.5f; c->echo = 0.5f; }
    }
    if (P > 0.5f) {
        Voice* d = Vox(hz1 * 2, down + 0.25 * BeatSec(), -32, V_AH, V_EH, 2);
        if (d) { d->atk = 0.05f; d->hold = 0.12f; d->rel = 0.3f; d->echo = 0.5f; }
    }
}

void SoundPalette::Impl::Hey() {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (A < 0.6f) return;
    Ladder L = MakeLadder();
    int idx = P >= 0.3f ? 2 : P >= 0 ? 1 : 0;
    double delay = UntilGrid(1.0, 0.02) + 0.5 * BeatSec(); // the "and" of the next beat
    if (delay > BeatSec()) delay -= BeatSec();
    if (delay < 0.02) delay += BeatSec();
    Voice* x = Vox(L.Hz(idx) * 0.5, delay, -28, P >= 0 ? V_EH : V_OH, P >= 0 ? V_EH : V_OH);
    if (!x) return;
    x->atk = 0.006f; x->hold = 0.04f; x->rel = 0.12f; x->echo = 0.35f;
    x->gain *= (float)Db(-10 + 6 * M);               // mostly breath; mechanical: more body
    x->breath = (float)(Db(10 - 6 * M) * Db(-6));    // breath carries it
}

void SoundPalette::Impl::Hum() {
    const float M = axes.mechanical;
    // The Dusk cells of the Afternoon motif, an octave down.
    int pcs[3];
    bool dmLike = !h.blending && (h.chord == MUSIC_DM9 || h.chord == MUSIC_G7SUS4 || h.chord == MUSIC_DRONE);
    if (h.blending) { pcs[0] = PC_A; pcs[1] = PC_G; pcs[2] = PC_D; }
    else if (dmLike) { pcs[0] = PC_A; pcs[1] = PC_F; pcs[2] = PC_D; }
    else { pcs[0] = PC_A; pcs[1] = PC_G; pcs[2] = PC_E; }
    double start = UntilGrid(2.0, 0.05);
    int floorM = 62; // D4..
    for (int i = 0; i < 3; i++) {
        int m = AtOrAbove(pcs[i], floorM);
        if (i == 0) m = AtOrAbove(pcs[0], 69); // A4
        else m = std::min(m, 69);
        Voice* x = Vox(MidiHz(m), start + 0.55 * i, -31, M > 0.6f ? V_OO : V_MM, M > 0.6f ? V_OO : V_MM, i);
        if (!x) continue;
        x->atk = 0.25f; x->hold = 0.4f; x->rel = 0.9f; x->echo = 0.2f;
        if (M <= 0.6f) { x->lp0 = x->lp1 = 450; }
    }
}

void SoundPalette::Impl::Bloom(double delay, double levelDb) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    int midi[4]; int n = 3;
    if (h.blending) { midi[0] = 62; midi[1] = 69; midi[2] = 74; }
    else switch (h.chord) {
        case MUSIC_G7SUS4: midi[0] = 55; midi[1] = 62; midi[2] = 69; break;
        case MUSIC_EM7: midi[0] = 64; midi[1] = 71; midi[2] = 74; break;
        case MUSIC_A7SUS4: midi[0] = 57; midi[1] = 64; midi[2] = 74; break;
        default: midi[0] = 62; midi[1] = 69; midi[2] = 76; break;
    }
    if (P < 0) n = 2;                                 // no 9th: the open 5th
    if (P > 0.6f) { midi[n] = midi[0] + 12; n++; }
    float atk = (float)(0.9 - 0.45 * A);
    for (int i = 0; i < n; i++) {
        uint8_t vow = P < 0 ? V_OO : V_AH;
        Voice* x = Vox(MidiHz(midi[i]), delay, levelDb - 5 - (i == 3 ? 8 : 0), M > 0.6f ? (uint8_t)V_OO : vow, vow, 10 + i);
        if (!x) continue;
        x->atk = atk; x->hold = 0.6f; x->rel = 1.8f; x->echo = 0.25f;
        x->hz *= Cents((i - 1) * 4.0 * (1.0 - M)); x->hzFrom = x->curHz = x->hz;
        if (M > 0.6f) { x->vowelAt = (float)(0.25 * BeatSec()); x->vowelTime = 0.02f; }
    }
}

void SoundPalette::Impl::Answer(double delay) {
    const float P = axes.positive, A = axes.activity;
    bool dmLike = !h.blending && (h.chord == MUSIC_DM9 || h.chord == MUSIC_G7SUS4 || h.chord == MUSIC_DRONE);
    double hz1 = MidiHz(81), hz2 = MidiHz(dmLike ? 77 : 79); // A5 -> F5 / G5
    double e = 0.5 * BeatSec();
    Voice* a = Vox(hz1, delay, -27, V_OH, P > 0.3f ? V_EH : V_AH, 0);
    if (a) { a->atk = 0.04f; a->hold = 0.12f; a->rel = 0.3f; a->vowelAt = 0.05f; a->echo = 0.6f; }
    Voice* c = Vox(hz2, delay + e, -27, V_AH, V_AH, 1);
    if (c) { c->atk = 0.04f; c->hold = 0.12f; c->rel = 0.3f; c->echo = 0.6f; }
    if (A > 0.8f) {
        Voice* g = Vox(MidiHz(86), delay + 2 * e, -36, V_EH, V_AH, 2);
        if (g) { g->atk = 0.02f; g->hold = 0.05f; g->rel = 0.2f; g->echo = 0.6f; }
    }
}

void SoundPalette::Impl::Sigh() {
    const float P = axes.positive, M = axes.mechanical;
    double start = MidiHz(AtOrAbove(RolePc(R_FIFTH), 62));   // the 5th, octave 4
    int m0 = (int)std::lround(HzMidi(start));
    double end = SafeNear(m0 - 2, -1);
    double delay = UntilGrid(2.0, 0.05);
    float relLen = (float)(1.2 + 0.6 * std::max(0.0f, -P - 0.5f) / 0.5f);
    if (M > 0.6f) { // a mechanical wind-down: two stepped notes
        Voice* a = Vox(start, delay, -30, V_OH, V_OO, 0);
        if (a) { a->atk = 0.2f; a->hold = 0.25f; a->rel = 0.3f; }
        Voice* c = Vox(end, delay + BeatSec(), -31, V_OO, V_OO, 1);
        if (c) { c->atk = 0.1f; c->hold = 0.2f; c->rel = relLen; }
        return;
    }
    Voice* x = Vox(end, delay, -30, V_OH, V_OO);
    if (!x) return;
    x->hzFrom = start; x->glide = GL_RAMP; x->glideTime = 0.5f + relLen;
    x->atk = 0.3f; x->hold = 0.2f; x->rel = relLen; x->vowelAt = 0.3f; x->vowelTime = 0.6f;
    LogNote(b.id, end, x->start);
}

// =====================================================================
// 5.2 Interaction and confirmation
// =====================================================================

void SoundPalette::Impl::Set(const SoundCue& c) {
    const float P = axes.positive, A = axes.activity;
    Ladder L = MakeLadder();
    bool fresh = now - gLast > Samples(kBarGapSeconds);
    if (fresh || !gOpen) { gIdx = 0; gStartHz = L.Hz(0); gOpen = true; }
    else gIdx++;
    int cap = P < 0 ? 4 : L.Top();
    bool completed = false;
    if (gIdx > cap) { gIdx = 0; }
    gIdx = ClearOfClash(L, gIdx, +1);
    double hz = L.Hz(gIdx);
    if (!fresh && gIdx > 0 && hz >= gStartHz * 2.0 - 1.0 && P >= 0) completed = true;
    gLast = now;
    if (P < -0.5f) hz *= 0.5;
    Voice* body = New(W_MORPH, hz, 0, -24);
    if (!body) return;
    body->env = ENV_AD; body->atk = 0.006f; body->tau = A > 0.6f ? 0.08f : 0.12f;
    body->pluck0 = (float)(6.0 + 2.0 * std::max(0.0f, P)); body->pluck1 = 1.5f; body->pluckTau = 0.06f;
    body->echo = 0.25f;
    Voice* sub = Thump(0, -30, 2.0, 0.09);
    Voice* grain = Grain(0, -38, 1200, 0.015);
    Tint(body, sub, grain, c.material);
    if (completed) { gIdx = 0; gOpen = false; pendingCadence = true; }
}

void SoundPalette::Impl::Take(const SoundCue& c) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    Ladder L = MakeLadder();
    bool fresh = now - gLast > Samples(kBarGapSeconds);
    if (fresh || !gOpen) { gIdx = 0; gOpen = true; }
    else gIdx--;
    if (gIdx < L.Bottom() + 1) gIdx = 0;
    gIdx = ClearOfClash(L, gIdx, -1);
    gLast = now;
    double hz1 = L.Hz(gIdx);
    double hz2;
    if (P > 0.4f) hz2 = hz1 * 0.5;                                  // the octave down: "done"
    else if (P < 0) hz2 = SafeNear((int)std::lround(HzMidi(hz1)) - 5, -1); // a 4th
    else hz2 = L.Hz(gIdx - 1);
    if (P < -0.5f) { hz1 *= 0.5; hz2 *= 0.5; }
    double e32 = BeatSec() / 8.0;
    float sc = A > 0.6f ? 0.8f : 1.0f;
    Voice* n1 = New(W_MORPH, hz1, 0, -25, 0);
    if (n1) { n1->env = ENV_AD; n1->tau = 0.09f * sc; n1->pluck0 = 4; n1->pluck1 = 1.2f; n1->echo = 0.2f; }
    Voice* n2 = New(W_MORPH, hz2, e32, -27, 1);
    if (n2) { n2->env = ENV_AD; n2->tau = 0.09f * sc; n2->pluck0 = 4; n2->pluck1 = 1.2f; n2->echo = 0.2f; }
    Tint(n1, nullptr, nullptr, c.material);
    Tint(n2, nullptr, nullptr, c.material);
    int grains = M > 0.6f ? 2 : 3;
    double grainLp = c.material == MAT_STONE ? 1600 : c.material == MAT_EARTH ? 900 : 1200;
    if (P < 0) grainLp *= 0.7;
    for (int i = 0; i < grains; i++) {
        double d = i * (M > 0.6f ? 0.03 : 0.025);
        Voice* g = Grain(d, -37 - 3 * i, grainLp, 0.012 * sc);
        if (g && M > 0.6f) g->tau = 0.006f;
    }
    if (c.material == MAT_STONE || c.material == MAT_EARTH || c.material == MAT_WOOD) Thump(0, -35, 2.0, 0.07);
}

// Slots 1-10 each own a note of the anchor set, low to high.
static const int kSlotMidi[10] = { 57, 62, 67, 69, 74, 79, 81, 86, 91, 93 };

void SoundPalette::Impl::Slot(const SoundCue& c) {
    int s = std::max(0, std::min(9, c.slot));
    Voice* x = New(W_MORPH, MidiHz(kSlotMidi[s]), 0, -30 - 1.5 * std::max(0, s - 4));
    if (!x) return;
    x->atk = axes.mechanical > 0.6f ? 0.004f : 0.006f; x->hold = 0.02f; x->rel = 0.06f;
    if (axes.positive < 0) x->rel *= 0.7f;
    x->capOct = 0.5f;
    x->metal = 0; x->breath = 0;
}

void SoundPalette::Impl::LibraryOpen() {
    bool low = axes.positive < 0;
    double hz[2] = { MidiHz(low ? 62 : 74), MidiHz(low ? 69 : 81) };
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_MORPH, hz[i], 0, -30, i);
        if (!x) continue;
        x->atk = 0.03f; x->hold = 0.15f; x->rel = 0.35f;
        x->lp0 = 500; x->lp1 = 1400; x->lpTime = axes.mechanical > 0.6f ? 0.12f : 0.18f;
    }
}
void SoundPalette::Impl::LibraryClose() {
    bool low = axes.positive < 0;
    double hz[2] = { MidiHz(low ? 69 : 81), MidiHz(low ? 62 : 74) };
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_MORPH, hz[i], 0, -31, i);
        if (!x) continue;
        x->atk = 0.02f; x->hold = 0.06f; x->rel = 0.25f;
        x->lp0 = 1400; x->lp1 = 500; x->lpTime = axes.mechanical > 0.6f ? 0.12f : 0.18f;
    }
}
void SoundPalette::Impl::Pick() {
    Voice* x = New(W_SINE, MidiHz(81), 0, -30);
    if (!x) return;
    x->hzFrom = x->curHz = MidiHz(74); x->glide = GL_RAMP; x->glideTime = 0.04f;
    x->atk = 0.006f; x->hold = 0.03f; x->rel = 0.12f;
}
void SoundPalette::Impl::Drop(const SoundCue& c) {
    int s = std::max(0, std::min(9, c.slot));
    double hz = MidiHz(kSlotMidi[s]);
    Voice* x = New(W_MORPH, hz, 0, -27, 0);
    if (x) { x->env = ENV_AD; x->tau = 0.15f; x->pluck0 = 6; x->pluck1 = 1.5f; x->echo = 0.25f; }
    Voice* t = New(W_THUMP, MidiHz(38), 0, -34, 0);
    if (t) { t->hzFrom = MidiHz(50); t->glide = GL_EXP; t->glideTime = 0.035f; t->env = ENV_AD; t->atk = 0.003f; t->tau = 0.08f; t->raw = true; t->duck = 0; }
    if (axes.positive > 0.4f && c.strength > 0.5f) {
        Voice* g = New(W_MORPH, hz * 2 > 1800 ? hz : hz * 2, BeatSec() / 4, -35, 1);
        if (g) { g->env = ENV_AD; g->tau = 0.12f; g->pluck0 = 6; g->pluck1 = 1.5f; g->echo = 0.3f; }
    }
}
void SoundPalette::Impl::Cant() {
    const float M = axes.mechanical;
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_MORPH, MidiHz(62), i * BeatSec() / 4, -28 - 6 * i, i);
        if (!x) continue;
        x->atk = 0.006f; x->hold = M > 0.6f ? 0.02f : 0.03f; x->rel = M > 0.6f ? 0.06f : 0.08f;
        x->lp0 = x->lp1 = 500; x->echo = 0; x->metal = 0;
    }
}
void SoundPalette::Impl::Land(const SoundCue& c) {
    double s = Clamp(c.strength, 0, 1);
    double lvl = -30 + 7 * s;
    Thump(0, lvl, 2.0, 0.09);
    double lp = c.material == MAT_STONE || c.material == MAT_METAL ? 1600 : 900;
    Voice* g = Grain(0, lvl - 12, lp * (1 + 0.5 * axes.mechanical), 0.03);
    (void)g;
    if (s > 0.7 && axes.positive >= 0) {
        Voice* r = New(W_BLEND, BassHz() * 2, 0, lvl - 6);
        if (r) { r->env = ENV_AD; r->atk = 0.01f; r->tau = 0.25f; r->echo = 0.2f; }
    }
}
void SoundPalette::Impl::Slide() {
    const float P = axes.positive, M = axes.mechanical;
    Voice* x = New(W_NOISE, 0, 0, -30);
    if (x) {
        x->env = ENV_SUSTAIN; x->atk = 0.06f; x->rel = 0.25f;
        x->bp0 = (float)RoleHz(R_FIFTH, 81);
        x->bp1 = (float)RoleHz(P > 0.4f ? R_COLOUR : R_ROOT, 81);
        x->bpTime = 1.0f; x->bpQ = M > 0.6f ? 2.4f : 1.6f;
    }
    Voice* t = New(W_BLEND, BassHz() * 3, 0, -42);
    if (t) { t->env = ENV_SUSTAIN; t->atk = 0.08f; t->rel = 0.25f; }
}

// =====================================================================
// 5.3 Discovery
// =====================================================================

void SoundPalette::Impl::Unveil() {
    const float P = axes.positive, A = axes.activity;
    double d = EventDelay();
    double step = (A > 0.6f ? 0.25 : 0.5) * BeatSec();
    double root = RoleHz(R_ROOT, 74), fifth = RoleHz(R_FIFTH, (int)std::lround(HzMidi(root)) + 1);
    double colour = RoleHz(R_COLOUR, (int)std::lround(HzMidi(fifth)) + 1);
    if (colour > 1800) colour *= 0.5;
    std::vector<double> notes;
    if (P < 0) notes = { root, RoleHz(R_FOURTH, (int)std::lround(HzMidi(root)) + 1), root };
    else { notes = { root, fifth, colour }; if (P > 0.5f) notes.push_back(root * 2); }
    for (size_t i = 0; i < notes.size(); i++) {
        Voice* x = Bell(notes[i], d + i * step, -26, 0.6, (int)i);
        if (x) x->echo = 0.5f;
    }
}
void SoundPalette::Impl::Horizon() {
    double d = UntilDownbeat(0.05);
    Bloom(d, -25);
    bool dmLike = !h.blending && (h.chord == MUSIC_DM9 || h.chord == MUSIC_G7SUS4 || h.chord == MUSIC_DRONE);
    double hz[2] = { MidiHz(74), MidiHz(dmLike ? 77 : 76) };
    if (h.blending) hz[1] = MidiHz(79);
    for (int i = 0; i < 2; i++) {
        double at = d + (2 + i) * BeatSec();
        Voice* x = axes.mechanical > 0.6f ? New(W_PULSE, hz[i], at, -29, 20 + i) : Bell(hz[i], at, -29, 0.7, 20 + i);
        if (!x) continue;
        if (x->wave == W_PULSE) { x->width = 0.3f; x->env = ENV_AD; x->tau = 0.35f; }
        x->echo = 0.5f;
    }
}
void SoundPalette::Impl::Glint() {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double lo, hi;
    if (P < 0) { lo = MidiHz(86); hi = MidiHz(91); }
    else if (!h.blending && (h.chord == MUSIC_DM9 || h.chord == MUSIC_A7SUS4 || h.chord == MUSIC_DRONE)) { lo = MidiHz(81); hi = MidiHz(88); }
    else { lo = MidiHz(86); hi = MidiHz(93); }
    double d = EventDelay();
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_SINE, i ? hi : lo, d, -34, i);
        if (!x) continue;
        x->atk = 0.04f; x->hold = 0.2f; x->rel = 1.4f; x->capOct = 1;
        x->tremDepth = 0.3f; x->tremBeats = (float)(P < 0 ? 1.0 : (A > 0.5f ? 0.25 : 0.5)); x->tremGate = M > 0.6f;
        x->echo = 0.3f;
    }
}
void SoundPalette::Impl::Vein(double delay, double levelDb) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    uint16_t safe = SafeMask();
    bool full = (safe & Bit(PC_F)) && (safe & Bit(PC_C));
    int mids[4] = { 74, 77, 81, 84 };
    if (!full) { mids[1] = 79; mids[2] = 81; mids[3] = 86; }
    double step = (A < 0.3f ? 0.5 : 0.25) * BeatSec();
    int n = 4;
    if (P < 0) { n = 3; std::swap(mids[0], mids[2]); } // descending: 3 notes from the top
    for (int i = 0; i < n; i++) {
        Voice* x = Bell(MidiHz(mids[i]), delay + i * step, levelDb, 0.5, i);
        if (!x) continue;
        x->echo = 0.6f;
        if (M > 0.5f) x->metal = (float)Db(-20);
    }
}
void SoundPalette::Impl::Timeslip() {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double hz = RoleHz(P >= 0 ? R_FIFTH : R_FOURTH, 74);
    float swell = A > 0.6f ? 0.45f : 0.9f;
    double peakIn = UntilGrid(1.0, 1.5 * BeatSec() + swell + 0.05);
    // The main note, then two ghosts arriving *before* it (the echo reversed).
    const double ghostBeats[2] = { 1.5, 0.75 };
    const double ghostDb[2] = { -18, -12 };
    for (int i = 0; i < 3; i++) {
        double peak = i < 2 ? peakIn - ghostBeats[i] * BeatSec() : peakIn;
        float sw = i < 2 ? swell * 0.4f : swell;
        Voice* x = New(W_BLEND, hz, peak - sw, i < 2 ? -27 + ghostDb[i] : -27, i);
        if (!x) continue;
        x->atk = sw; x->hold = 0; x->rel = 0.06f;
        x->hzFrom = hz * Cents(50); x->glide = GL_RAMP; x->glideTime = sw;
        if (M > 0.6f) x->glideStep = 25;
        x->metal = 0;
    }
}
void SoundPalette::Impl::Omen() {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double root = RoleHz(R_ROOT, 43), fifth = root * 1.4983;
    if (h.blending) { root = MidiHz(50); fifth = MidiHz(57); }
    float atk = (float)(1.2 - 0.6 * A);
    float rel = (float)(3.0 + 1.5 * std::max(0.0f, -P - 0.5f));
    for (int i = 0; i < 2; i++) {
        Voice* x;
        if (M > 0.6f) {
            x = New(W_SAW, BassHz() * 2 * (i ? 1.4983 : 1.0), 0.05, -31, i);
            if (x) { x->lp0 = x->lp1 = 400; }
        } else {
            x = Vox(i ? fifth : root, 0.05, -32, V_OO, V_OO, i);
        }
        if (!x) continue;
        x->atk = atk; x->hold = 0.5f; x->rel = rel;
        x->tremDepth = 0.25f; x->tremHz = 0.5f;
    }
    double t = atk + 0.5 + 0.4;
    Voice* a = New(W_STRI, MidiHz(62), t, -31, 5);
    if (a) { a->atk = 0.05f; a->hold = 0.2f; a->rel = 0.6f; }
    Voice* c = New(W_STRI, MidiHz(57), t + BeatSec(), -32, 6);
    if (c) { c->atk = 0.05f; c->hold = 0.3f; c->rel = 1.2f; }
}

// =====================================================================
// 5.4 Rhythmic world accents
// =====================================================================

// How hard a surface is, 0 (yielding) .. 1 (ringing): footsteps and
// landings darken, soften and lengthen toward 0, sharpen toward 1.
static double Hardness(SoundMaterial m) {
    switch (m) {
    case MAT_FLESH: return 0.05;
    case MAT_PLANT: return 0.1;
    case MAT_EARTH: return 0.2;
    case MAT_GENESIS: return 0.25;
    case MAT_WOOD: return 0.6;
    case MAT_STONE: return 0.85;
    case MAT_GLASS: return 0.95;
    case MAT_METAL: return 1.0;
    default: return 0.5;
    }
}

// One footstep (5.4 R1), placed on the beat by the gait scheduler (OnBlock)
// or, played directly, locked to an 8th when one is just ahead. `key` is
// the step count: steps alternate a little left and right, every other one
// a touch softer (a walk's natural lilt). `strength` scales it (a crouch).
void SoundPalette::Impl::Footfall(const SoundCue& c) {
    const float P = axes.positive, M = axes.mechanical;
    double delay = 0;
    if (h.pulsed && c.height == 0) { // played directly: phase-lock to an 8th just ahead
        double toGrid = UntilGrid(0.5, 0.0);
        if (toGrid <= 0.04) delay = toGrid;
    }
    const double hard = Hardness(c.material);
    double lvl = -29 + 3 * hard - ((c.key & 1) ? 2 : 0) + 20 * std::log10(std::max(0.2f, c.strength)); // owner: they must be heard
    // The scuff: soft ground dull, low and long; hard ground crisp and short.
    double lp = (700 + 1700 * hard) * (P < 0 ? 0.8 : 1.0) * (1 + 0.3 * M);
    double tau = 0.028 - 0.020 * hard;
    Voice* g = Grain(delay, lvl, lp, tau);
    if (g) { g->pan = (c.key & 1) ? 0.12f : -0.12f; g->panSet = true; }
    if (hard < 0.3) { // snow, sand, moss: a second grain just after, the crunch
        Voice* g2 = Grain(delay + 0.012, lvl - 5, lp * 0.8, tau * 0.8);
        if (g2) { g2->pan = g ? g->pan : 0; g2->panSet = true; }
    }
    // The weight: a sub on the bass note, heavier on soft ground.
    Voice* s = New(W_SINE, BassHz(), delay, lvl - 12 - 8 * hard);
    if (s) { s->env = ENV_AD; s->atk = 0.008f; s->tau = 0.04f; s->raw = true; s->duck = 0; }
    // Hard ground knocks, very quietly, on the chord's root or 5th in turn:
    // walking on stone taps along with the harmony.
    if (hard >= 0.5) {
        double hz = RoleHz((c.key & 1) ? R_FIFTH : R_ROOT, 62);
        Voice* k = New(hard >= 0.95 ? W_SINE : W_STRI, hz, delay, lvl - 9);
        if (k) {
            k->env = ENV_AD; k->atk = 0.006f; k->tau = (float)(0.010 + 0.010 * hard);
            k->lp0 = k->lp1 = (float)(900 + 1400 * (hard - 0.5));
            k->metal = hard >= 0.99 || M > 0.6f ? (float)Db(-8) : 0.0f; k->breath = 0; k->echo = 0;
            k->pan = g ? g->pan : 0; k->panSet = true;
        }
    }
}

// Euclidean rhythm: k hits spread over n steps (Bjorklund by rounding).
static bool EuclidHit(int k, int n, int step, int rot) {
    int s = ((step + rot) % n + n) % n;
    return ((s * k) % n) < k;
}

void SoundPalette::Impl::Works(double barDelay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    int k = A < 0.5f ? 2 : A < 0.8f ? 3 : 5, n = A < 0.8f ? 8 : 16;
    double stepSec = 4.0 * BeatSec() / n;
    double lvl = -34 + std::min(3.0, 3.0 * scene.machines);
    int dropStep = P < 0 ? (int)(Hash01(b.ev, 3) * n) : -1;
    int hits = 0;
    for (int s = 0; s < n; s++) {
        if (!EuclidHit(k, n, s, 1) || s == dropStep) continue;
        double d = barDelay + s * stepSec;
        if (M < 0.4f) { // toward organic: a soft wood-block click
            Voice* x = New(W_STRI, RoleHz(R_ROOT, 81), d, lvl - 2, s);
            if (x) { x->env = ENV_AD; x->atk = 0.002f; x->tau = 0.015f; x->lp0 = x->lp1 = 900; x->q = 1.2f; x->metal = 0; x->breath = 0; }
        } else {
            Voice* g = New(W_NOISE, 0, d, lvl, s);
            if (g) { g->env = ENV_AD; g->atk = 0.003f; g->tau = 0.008f; g->bp0 = g->bp1 = (float)(1600 * std::pow(2.0, 0.4 * P)); g->bpQ = 1.5f; }
            Voice* m = New(W_SINE, RoleHz(R_ROOT, 86), d, lvl - 18, s);
            if (m) { m->env = ENV_AD; m->atk = 0.002f; m->tau = 0.02f; m->metal = (float)Db(-4); m->breath = 0; }
        }
        hits++;
    }
    (void)hits;
}
void SoundPalette::Impl::Drip(double delay) {
    const float P = axes.positive, M = axes.mechanical;
    Ladder L = MakeLadder();
    int idx = 5 + (int)(Hash01(b.ev, 5) * 3.0);
    double hz = L.Hz(idx);
    if (P < 0) hz *= 0.5;
    Voice* x = M > 0.6f ? Bell(hz, delay, -33, 0.07) : New(W_SINE, hz, delay, -33);
    if (!x) return;
    if (M <= 0.6f) {
        x->env = ENV_AD; x->atk = 0.003f; x->tau = P < 0 ? 0.1f : 0.07f;
        x->hzFrom = hz / 1.4983; x->glide = GL_RAMP; x->glideTime = 0.025f;
    }
    x->echo = 0.5f; x->capOct = 1;
}
void SoundPalette::Impl::Ember(double barDelay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double stepSec = BeatSec() / 4.0;
    double prob = (0.35 + 0.25 * A) * (P < 0 ? 0.7 : 1.0);
    for (int s = 0; s < 16; s++) {
        bool lift = s == 14;
        if (!lift && Hash01(b.ev * 17 + s, 11) > prob) continue;
        double vel = (Hash01(b.ev * 17 + s, 12) - 0.5) * 6.0;
        Voice* g = New(W_NOISE, 0, barDelay + s * stepSec, -35 + vel, s);
        if (!g) continue;
        g->env = ENV_AD; g->atk = 0.002f; g->tau = M > 0.6f ? 0.004f : 0.006f;
        g->bp0 = g->bp1 = P < 0 ? 800.0f : 1200.0f; g->bpQ = 1.0f;
    }
}
void SoundPalette::Impl::Clave(double barDelay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (!h.pulsed || scene.musicBlockCount <= 0) return;
    int players = std::min(scene.musicBlockCount, 1 + (int)std::lround(2.0 * A));
    double e8 = BeatSec() / 2.0;
    for (int p = 0; p < players; p++) {
        int rot = (int)(scene.musicBlockKey[p] % 8);
        static const int pcs[3] = { PC_D, PC_G, PC_A };
        double hz = MidiHz(AtOrAbove(pcs[((scene.musicBlockY[p] % 3) + 3) % 3], 74));
        int last = -1;
        for (int s = 0; s < 8; s++) if (EuclidHit(3, 8, s, rot)) last = s;
        for (int s = 0; s < 8; s++) {
            if (!EuclidHit(3, 8, s, rot)) continue;
            if (P < 0 && s == last) continue;
            Voice* x = M < 0.4f ? New(W_STRI, hz, barDelay + s * e8, -32, s) : Bell(hz, barDelay + s * e8, -32, 0.045, s);
            if (!x) continue;
            { // each music block plays from where it stands
                float dx = scene.musicBlockX[p] - lx, dz = scene.musicBlockZ[p] - lz, hh = std::sqrt(dx * dx + dz * dz);
                x->pan = hh > 0.01f ? (dx * std::cos(lyaw) - dz * std::sin(lyaw)) / hh : 0.0f;
                x->panSet = true;
            }
            x->env = ENV_AD; x->atk = 0.002f; x->tau = 0.045f;
            x->lp0 = x->lp1 = M < 0.4f ? 900.0f : 1200.0f;
            x->p3 = 0; x->p2 = 0.3f;
        }
    }
}
void SoundPalette::Impl::Heartbeat(double barDelay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (P > -0.3f) return;
    double lvl = -31 + 4 * Clamp((-P - 0.3) / 0.7, 0, 1);
    int beats = A < 0.5f ? 1 : 2;
    for (int i = 0; i < beats; i++) {
        double d = barDelay + i * 2.0 * BeatSec();
        Voice* lub = New(W_THUMP, BassHz(), d, lvl, i * 2);
        if (lub) { lub->hzFrom = BassHz() * 2; lub->glide = GL_EXP; lub->glideTime = 0.035f; lub->env = ENV_AD; lub->atk = 0.003f; lub->tau = 0.09f; lub->raw = true; lub->duck = 0; }
        double d2 = d + BeatSec() / 4.0;
        if (M > 0.6f) {
            Voice* c = New(W_PULSE, BassHz() * 4, d2, lvl - 8, i * 2 + 1);
            if (c) { c->env = ENV_AD; c->atk = 0.002f; c->tau = 0.01f; c->width = 0.34f; }
        } else {
            Voice* dub = New(W_THUMP, BassHz(), d2, lvl - 4, i * 2 + 1);
            if (dub) { dub->hzFrom = BassHz() * 1.5; dub->glide = GL_EXP; dub->glideTime = 0.03f; dub->env = ENV_AD; dub->atk = 0.003f; dub->tau = 0.08f; dub->raw = true; dub->duck = 0; }
        }
    }
}

// =====================================================================
// 5.5 Living-world textures
// =====================================================================

void SoundPalette::Impl::Wind(double delay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double bar = 4.0 * BeatSec();
    double len = bar * (2.0 - A);
    int roles[2] = { R_FIFTH, R_COLOUR };
    if (P < 0) { roles[0] = R_ROOT; roles[1] = R_FOURTH; }
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_NOISE, 0, delay, -33 - 6 * std::max(0.0, (M - 0.5) * 2), i);
        if (!x) continue;
        x->atk = (float)(len * 0.5); x->hold = 0; x->rel = (float)(len * 0.5);
        x->bpQ = M > 0.5f ? 1.6f : (P < 0 ? 4.0f : 6.0f);
        x->followRole = (int8_t)roles[i]; x->followFloor = 72 + 5 * i; x->followGlide = 1.5f;
        x->bp0 = x->bp1 = (float)RoleHz(roles[i], x->followFloor);
    }
}
void SoundPalette::Impl::Chirps(double delay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (P <= 0.2f || M >= 0.5f) return;
    Ladder L = MakeLadder();
    int n = P > 0.6f ? 4 : P > 0.3f ? 3 : 2;
    double step = (A < 0.4f ? 0.5 : 0.25) * BeatSec();
    int start = 5 + (int)(Hash01(b.ev, 13) * 2.0);
    for (int i = 0; i < n; i++) {
        int idx = P > 0.25f ? start + i : start + (i == 1 ? 1 : 0) - (i == n - 1 ? 1 : 0);
        double hz = L.Hz(idx);
        Voice* x = New(W_SINE, hz, delay + i * step, -33, i);
        if (!x) continue;
        x->atk = 0.003f; x->hold = 0.02f; x->rel = 0.04f;
        x->hzFrom = hz * std::pow(2.0, 3.0 / 12.0); x->glide = GL_RAMP; x->glideTime = 0.04f;
        x->capOct = 1; x->echo = 0.35f; x->breath = 0;
    }
}
void SoundPalette::Impl::NightShimmer(double delay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (P < 0) return;
    uint16_t safe = SafeMask();
    bool ea = (safe & Bit(PC_E)) && (safe & Bit(PC_A));
    double hz[2] = { MidiHz(ea ? 88 : 86), MidiHz(ea ? 93 : 91) };
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_SINE, hz[i], delay, -39, i);
        if (!x) continue;
        x->atk = 2.0f; x->hold = 1.0f; x->rel = 3.0f; x->capOct = 1;
        if (M <= 0.6f) { x->tremDepth = 0.6f; x->tremBeats = (float)(A < 0.4f ? 0.5 : 1.0 / 6.0); x->tremGate = true; }
        x->breath = 0;
    }
}
void SoundPalette::Impl::Mushroom(double delay) {
    const float P = axes.positive, M = axes.mechanical;
    bool neg = P < 0;
    double root = RoleHz(R_ROOT, neg ? 50 : 60);
    double other = RoleHz(neg ? R_FOURTH : R_FIFTH, (int)std::lround(HzMidi(root)) + 1);
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_SINE, i ? other : root, delay, -37, i);
        if (!x) continue;
        x->atk = 1.0f; x->hold = 0; x->rel = 1.5f;
        x->breath = M > 0.6f ? 0.0f : (float)Db(neg ? -6 : -12);
    }
}
void SoundPalette::Impl::WorksHum(double delay) {
    const float P = axes.positive, A = axes.activity;
    double lvl = -36 + 6 * Clamp(scene.machines, 0, 1);
    if (P < 0) lvl -= 2;
    Voice* x = New(W_PULSE, BassHz() * 2, delay, lvl, 0);
    if (x) {
        x->width = 0.45f; x->lp0 = x->lp1 = 400; x->atk = 2; x->hold = (float)(8 * BeatSec()); x->rel = 3;
        x->followBass = true; x->followMul = 2; x->followGlide = (float)(2 * BeatSec());
        x->tremDepth = 0.15f * A; x->tremBeats = 0.25f; x->metal = 0; x->breath = 0;
    }
    Voice* s = New(W_SINE, BassHz() * 4, delay, lvl - 10, 1);
    if (s) {
        s->atk = 2; s->hold = (float)(8 * BeatSec()); s->rel = 3;
        s->followBass = true; s->followMul = 4; s->followGlide = (float)(2 * BeatSec()); s->breath = 0;
    }
    if (P > 0.4f) {
        Voice* f = New(W_SINE, BassHz() * 3, delay, lvl - 8, 2);
        if (f) { f->atk = 2; f->hold = (float)(8 * BeatSec()); f->rel = 3; f->followBass = true; f->followMul = 3; f->followGlide = (float)(2 * BeatSec()); f->breath = 0; }
    }
}
void SoundPalette::Impl::Worn(double delay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (P > -0.3f) return;
    double bar = 4.0 * BeatSec();
    double lvl = -33 + 4 * Clamp((-P - 0.3) / 0.7, 0, 1);
    Voice* x = New(W_SAW, BassHz() * 2, delay, lvl);
    if (!x) return;
    x->lp0 = x->lp1 = 300; x->twinCents = (float)(1 + 6 * (1 - M));
    float sw = (float)(bar * (4 - 2 * A));
    x->atk = sw; x->hold = 0; x->rel = sw;
    x->followBass = true; x->followMul = 2; x->followGlide = (float)(2 * BeatSec());
    x->metal = 0; x->breath = 0;
}
void SoundPalette::Impl::Enclosure(double delay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    int floorM = (int)std::lround(HzMidi(BassHz() * 2));
    double lo = RoleHz(R_ROOT, floorM), hi = RoleHz(P >= 0 ? R_FIFTH : R_FOURTH, floorM + 1);
    for (int i = 0; i < 2; i++) {
        Voice* x = New(M > 0.6f ? W_PULSE : W_SINE, i ? hi : lo, delay, -36 - (M > 0.6f ? 6 : 0), i);
        if (!x) continue;
        x->atk = (float)(1.5 - 0.7 * A); x->hold = 0.5f; x->rel = 2.5f; x->breath = 0;
        if (x->wave == W_PULSE) { x->lp0 = x->lp1 = 500; }
    }
}

// =====================================================================
// 5.6 Progress
// =====================================================================

void SoundPalette::Impl::Cadence() {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    if (P < 0) return;
    double down = UntilDownbeat(0.6 * BeatSec());
    double first = down - (A > 0.6f ? 1.0 : 2.0) * BeatSec();
    if (first < 0.02) { down += 4 * BeatSec(); first += 4 * BeatSec(); }
    double hz1, hz2;
    if (!h.blending && (h.chord == MUSIC_DM9 || h.chord == MUSIC_DRONE)) { hz1 = MidiHz(88); hz2 = MidiHz(86); }
    else { hz2 = RoleHz(R_ROOT, 84); hz1 = RoleHz(R_FIFTH, 79); }
    double hz[2] = { hz1, hz2 }, at[2] = { first, down };
    for (int i = 0; i < 2; i++) {
        Voice* x = Bell(hz[i], at[i], -26, i ? 0.9 : 0.5, i);
        if (x) { x->echo = 0.45f; if (M > 0.5f) x->metal = (float)Db(-20); }
        if (M <= 0.7f) {
            Voice* v2 = Vox(hz[i] * 0.5, at[i], -34, V_AH, V_AH, i);
            if (v2) { v2->atk = 0.03f; v2->hold = 0.15f; v2->rel = i ? 0.8f : 0.3f; }
        }
    }
}
void SoundPalette::Impl::Mending() {
    static const int kMotifPc[8] = { PC_D, PC_F, PC_A, PC_C, PC_A, PC_F, PC_E, PC_G };
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    int pc = kMotifPc[mendStep % 8];
    mendStep++;
    int m = AtOrAbove(pc, 74);
    double hz = (SafeMask() & Bit(pc)) ? MidiHz(m) : SafeNear(m, -1);
    double d = UntilGrid(A > 0.6f ? 0.25 : 0.5, 0.01);
    Voice* x = New(M > 0.6f ? W_PULSE : W_BLEND, hz, d, -29 + 2 * Clamp(P, 0, 1));
    if (!x) return;
    x->env = ENV_AD; x->tau = 0.3f; x->pluck0 = 6; x->pluck1 = 1.5f; x->width = 0.3f; x->echo = 0.35f;
}
void SoundPalette::Impl::Online() {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double root = RoleHz(R_ROOT, 74);
    double notes[3] = { root, P < 0 ? RoleHz(R_FOURTH, (int)std::lround(HzMidi(root)) + 1) : RoleHz(R_FIFTH, (int)std::lround(HzMidi(root)) + 1),
                        P < 0 ? root : root * 2 };
    if (notes[2] > 1800) notes[2] *= 0.5;
    double step = (A > 0.85f ? 0.125 : 0.25) * BeatSec();
    double d = EventDelay();
    for (int i = 0; i < 3; i++) {
        Voice* x = New(M < 0.4f ? W_STRI : W_PULSE, notes[i], d + i * step, -27, i);
        if (!x) continue;
        x->width = 0.3f; x->atk = 0.006f; x->hold = i == 2 ? 0.6f : 0.06f; x->rel = i == 2 ? 0.4f : 0.12f; x->echo = 0.3f;
    }
}
void SoundPalette::Impl::Sealed() {
    double hz[2] = { MidiHz(74), MidiHz(86) };
    for (int i = 0; i < 2; i++) {
        Voice* x = New(W_SINE, hz[i], 0, i ? -37 : -31, i);
        if (!x) continue;
        x->atk = 0.02f; x->hold = 0.1f; x->rel = 0.7f; x->raw = true; x->echo = 0.3f;
    }
    Voice* t = New(W_THUMP, MidiHz(38), 0, -35);
    if (t) { t->hzFrom = MidiHz(50); t->glide = GL_EXP; t->glideTime = 0.035f; t->env = ENV_AD; t->atk = 0.003f; t->tau = 0.09f; t->raw = true; t->duck = 0; }
}
void SoundPalette::Impl::RiftClosed() {
    double d = UntilDownbeat(0.1);
    Bloom(d, -22);
    Vein(d + BeatSec(), -26);
    Voice* t = New(W_THUMP, BassHz(), d, -24);
    if (t) { t->hzFrom = BassHz() * 2; t->glide = GL_EXP; t->glideTime = 0.035f; t->env = ENV_AD; t->atk = 0.003f; t->tau = 0.12f; t->raw = true; t->duck = 0; }
    capBoost = 1.4f; capBoostUntil = now + Samples(d + 8 * BeatSec());
}

// =====================================================================
// 5.7 Rare colour
// =====================================================================

void SoundPalette::Impl::FarBell(double delay) {
    const float P = axes.positive, M = axes.mechanical;
    double hz = RoleHz(R_ROOT, P < 0 ? 48 : 60);
    Voice* x = Bell(hz, delay, -32, 2.5);
    if (!x) return;
    x->atk = 0.02f; x->p2 = 0.25f; x->p3 = 0.1f;
    if (P >= 0) { x->pxRatio = 4.2f; x->px = (float)Db(-24); }
    x->capOct = -0.7f; x->echo = 0.9f;
    if (M > 0.5f) x->metal = (float)Db(-22);
}
void SoundPalette::Impl::Glimmer(double delay) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double root = RoleHz(R_ROOT, 74);
    double notes[4];
    if (P >= 0) {
        notes[0] = RoleHz(R_FIFTH, 74);
        notes[1] = RoleHz(R_COLOUR, (int)std::lround(HzMidi(notes[0])) + 1);
        notes[2] = RoleHz(R_ROOT, (int)std::lround(HzMidi(notes[1])) + 1);
        notes[3] = RoleHz(R_FIFTH, (int)std::lround(HzMidi(notes[2])) + 1);
    } else {
        notes[0] = root; notes[1] = RoleHz(R_FIFTH, 75); notes[2] = root * 2; notes[3] = notes[1] * 2;
    }
    double step = (A > 0.5f ? 0.5 : 1.0) * BeatSec();
    for (int i = 0; i < 4; i++) {
        if (notes[i] > 1800) continue;
        Voice* x = New(M > 0.6f && i == 3 ? W_PULSE : W_SINE, notes[i], delay + i * step, -38, i);
        if (!x) continue;
        x->atk = 1.5f; x->hold = 0.5f; x->rel = 4.0f; x->capOct = 1; x->breath = 0;
        if (x->wave == W_PULSE) x->lp0 = x->lp1 = 1500;
    }
}
void SoundPalette::Impl::FallingStars(double delay) {
    const float P = axes.positive, M = axes.mechanical;
    Ladder L = MakeLadder();
    int top = 0;
    for (int i = 0; i <= L.Top(); i++) if (L.Hz(i) <= MidiHz(88) + 0.5) top = i;
    int n = P < 0 ? 3 : 4;
    double step = 0.5 * BeatSec();
    for (int i = 0; i < n; i++) {
        double hz = P < 0 ? MidiHz(86 - (i == 0 ? 0 : i == 1 ? 5 : 7)) : L.Hz(top - i);
        Voice* x = M > 0.5f ? Bell(hz, delay + i * step, -32 - 3 * i, 0.4, i) : New(W_SINE, hz, delay + i * step, -32 - 3 * i, i);
        if (!x) continue;
        x->env = ENV_AD; x->atk = 0.01f; x->tau = 0.4f; x->echo = 0.7f; x->capOct = 1; x->breath = 0;
    }
}
void SoundPalette::Impl::Murmur(double delay) {
    const float P = axes.positive, M = axes.mechanical;
    Voice* x = New(W_SINE, BassHz(), delay, -30);
    if (x) { x->hzFrom = BassHz() * 1.06; x->glide = GL_RAMP; x->glideTime = 1.5f; x->atk = 1.5f; x->hold = 0.5f; x->rel = 3; x->breath = 0; x->metal = 0; }
    if (M < 0.6f) {
        Voice* n = New(W_NOISE, 0, delay, -40);
        if (n) { n->atk = 1.5f; n->hold = 0.5f; n->rel = 3; n->bp0 = n->bp1 = 180; n->bpQ = 0; }
    }
    (void)P;
}
void SoundPalette::Impl::FarCall(double delay) {
    const float M = axes.mechanical;
    if (M > 0.6f) {
        Voice* a = Vox(MidiHz(62), delay, -33, V_OO, V_OO, 0);
        if (a) { a->atk = 0.8f; a->hold = 0.6f; a->rel = 0.4f; a->echo = 0.9f; }
        Voice* c = Vox(MidiHz(69), delay + 1.6, -33, V_OO, V_OO, 1);
        if (c) { c->atk = 0.3f; c->hold = 1.0f; c->rel = 3.0f; c->echo = 0.9f; }
        return;
    }
    Voice* x = Vox(MidiHz(69), delay, -33, V_OO, V_OO);
    if (!x) return;
    x->hzFrom = MidiHz(62); x->glide = GL_RAMP; x->glideTime = 2.0f;
    x->atk = 1.0f; x->hold = 2.0f; x->rel = 3.0f; x->echo = 0.9f;
    LogNote(b.id, MidiHz(62), x->start);
}
void SoundPalette::Impl::Seam() {
    double hz[2] = { MidiHz(74), MidiHz(81) };
    for (int i = 0; i < 2; i++) {
        Voice* x = Bell(hz[i], 0, -32, 4.0, i);
        if (!x) continue;
        x->atk = 0.03f; x->echo = 0.9f; x->raw = true;
    }
}

// =====================================================================
// Dispatch, gestures, scheduling (spec 4)
// =====================================================================

void SoundPalette::Impl::Recipe(const SoundCue& c) {
    switch (c.id) {
    case SND_RISE: Rise(); break;
    case SND_HEY: Hey(); break;
    case SND_HUM: Hum(); break;
    case SND_BLOOM: Bloom(UntilDownbeat(0.05), -25); break;
    case SND_ANSWER: Answer(UntilGrid(1.0, 0.02)); break;
    case SND_SIGH: Sigh(); break;
    case SND_SET: Set(c); break;
    case SND_TAKE: Take(c); break;
    case SND_SLOT: Slot(c); break;
    case SND_LIBRARY_OPEN: LibraryOpen(); break;
    case SND_LIBRARY_CLOSE: LibraryClose(); break;
    case SND_PICK: Pick(); break;
    case SND_DROP: Drop(c); break;
    case SND_CANT: Cant(); break;
    case SND_LAND: Land(c); break;
    case SND_SLIDE: Slide(); break;
    case SND_UNVEIL: Unveil(); break;
    case SND_HORIZON: Horizon(); break;
    case SND_GLINT: Glint(); break;
    case SND_VEIN: Vein(EventDelay(), -27); break;
    case SND_TIMESLIP: Timeslip(); break;
    case SND_OMEN: Omen(); break;
    case SND_FOOTFALL: Footfall(c); break;
    case SND_WORKS: Works(UntilDownbeat(0.02)); break;
    case SND_DRIP: Drip(UntilGrid(AmbientGrid() > 1 ? 1.0 : 0.5, 0.02)); break;
    case SND_EMBER: Ember(UntilDownbeat(0.02)); break;
    case SND_CLAVE: Clave(UntilDownbeat(0.02)); break;
    case SND_HEARTBEAT: Heartbeat(UntilDownbeat(0.02)); break;
    case SND_WIND: Wind(UntilDownbeat(0.02)); break;
    case SND_CHIRPS: Chirps(UntilGrid(1.0, 0.02)); break;
    case SND_NIGHT_SHIMMER: NightShimmer(UntilGrid(2.0, 0.02)); break;
    case SND_MUSHROOM: Mushroom(0.02); break;
    case SND_WORKS_HUM: WorksHum(UntilDownbeat(0.02)); break;
    case SND_WORN: Worn(UntilDownbeat(0.02)); break;
    case SND_ENCLOSURE: Enclosure(UntilGrid(2.0, 0.02)); break;
    case SND_CADENCE: Cadence(); break;
    case SND_MENDING: Mending(); break;
    case SND_ONLINE: Online(); break;
    case SND_SEALED: Sealed(); break;
    case SND_RIFT_CLOSED: RiftClosed(); break;
    case SND_FAR_BELL: FarBell(UntilDownbeat(0.02)); break;
    case SND_GLIMMER: Glimmer(UntilGrid(1.0, 0.02)); break;
    case SND_FALLING_STARS: FallingStars(UntilGrid(1.0, 0.02)); break;
    case SND_MURMUR: Murmur(UntilDownbeat(0.02)); break;
    case SND_FAR_CALL: FarCall(UntilDownbeat(0.02)); break;
    case SND_SEAM: Seam(); break;
    default: break;
    }
}

void SoundPalette::Impl::PlayCue(const SoundCue& c) {
    if (c.id >= SND_COUNT) return;
    MusicHarmonyAt(musicTime, &h); // the moment this will be heard
    haveH = true;
    int64_t since = now - lastOnset[c.id];
    // Merge: a second onset within 30 ms joins the first (spec 4).
    if (since < Samples(0.03) && c.id != SND_FOOTFALL) {
        for (auto& x : v)
            if (x.on && x.group == lastGroup[c.id] && !x.merged) { x.gain *= 1.19f; x.merged = true; }
        return;
    }
    // Repeat softening.
    repeat[c.id] = since < Samples(0.15) ? std::min(3, repeat[c.id] + 1) : 0;
    Begin(c.id, -3.0 * repeat[c.id]);
    if (c.placed) Place(c.x, c.y, c.z);
    // The hierarchy: an event sound owns the moment (spec 4).
    if (b.tier >= TIER_ACCENT && now < tier2Until && c.id != SND_FOOTFALL) return;
    pendingCadence = false;
    Recipe(c);
    Finish();
    if (pendingCadence) { pendingCadence = false; SoundCue cad; cad.id = SND_CADENCE; PlayCue(cad); }
}

void SoundPalette::Impl::Ambient(double delay, SoundId id) {
    Begin(id);
    if (b.tier >= TIER_ACCENT && now < tier2Until) return;
    switch (id) {
    case SND_WORKS: Works(delay); break;
    case SND_EMBER: Ember(delay); break;
    case SND_CLAVE: Clave(delay); break;
    case SND_HEARTBEAT: Heartbeat(delay); break;
    case SND_DRIP: Drip(delay); break;
    case SND_WIND: Wind(delay); break;
    case SND_CHIRPS: Chirps(delay); break;
    case SND_NIGHT_SHIMMER: NightShimmer(delay); break;
    case SND_MUSHROOM: Mushroom(delay); break;
    case SND_WORKS_HUM: WorksHum(delay); break;
    case SND_WORN: Worn(delay); break;
    case SND_FAR_BELL: FarBell(delay); break;
    case SND_FALLING_STARS: FallingStars(delay); break;
    case SND_MURMUR: Murmur(delay); break;
    case SND_FAR_CALL: FarCall(delay); break;
    default: { SoundCue c; c.id = id; Recipe(c); } break;
    }
    Finish();
    scheduled++;
}

// Once per bar: plan the next bar's ambience (spec 3.2, 4).
void SoundPalette::Impl::OnBar(int64_t bar) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    const AmbientScene& s = scene;
    double toNextBar = ((bar + 1) * 4.0 - h.beat) / Bps();
    if (toNextBar < 0.01) toNextBar += 4.0 / Bps();
    bool event = now < tier2Until;
    bool day = h.section <= MUSIC_AFTERNOON;
    bool night = h.section == MUSIC_NIGHT || (h.section == MUSIC_DUSK && h.beat - h.sectionStartBeat > 600);
    int64_t barsSinceEvent = (now - lastTier2) / std::max<int64_t>(1, Samples(4.0 / Bps()));

    // Rare colour: calm, no event in 8 bars, a small chance per bar.
    if (A < 0.4f && barsSinceEvent >= 8 && Hash01((uint64_t)bar, 101) < 0.08) {
        SoundId pick = SND_COUNT;
        auto ok = [&](SoundId id) { return now >= cooldownUntil[id]; };
        if (s.lookingUp && night && ok(SND_FALLING_STARS)) pick = SND_FALLING_STARS;
        else if (s.deep && ok(SND_MURMUR)) pick = SND_MURMUR;
        else if (night && P > 0.3f && ok(SND_FAR_CALL)) pick = SND_FAR_CALL;
        else if (!s.deep && !s.enclosed && ok(SND_FAR_BELL)) pick = SND_FAR_BELL;
        if (pick != SND_COUNT) {
            static const double cool[] = { 90, 180, 300, 360 };
            double cd = pick == SND_FALLING_STARS ? cool[0] : pick == SND_MURMUR ? cool[1] : pick == SND_FAR_CALL ? cool[2] : cool[3];
            cooldownUntil[pick] = now + Samples(cd);
            Ambient(toNextBar, pick);
            return;
        }
    }
    if (event) return;

    // Ad-lib moments the scene asks for.
    if (s.stillSeconds > 8 && P > 0.3f && A < 0.3f && now >= cooldownUntil[SND_HUM]) {
        cooldownUntil[SND_HUM] = now + Samples(60);
        Ambient(toNextBar, SND_HUM);
        return;
    }
    if (s.negativeSeconds > 10 && now >= cooldownUntil[SND_SIGH]) {
        cooldownUntil[SND_SIGH] = now + Samples(120);
        Ambient(toNextBar, SND_SIGH);
        return;
    }

    // The budget: 1 event per 4 bars when calm, 6 when busy.
    tokens = std::min(2.0f, tokens + (1.0f + 5.0f * A) / 4.0f);
    if (tokens < 1.0f) return;
    struct Cand { SoundId id; double w; int gapBars; };
    Cand c[12]; int n = 0;
    auto add = [&](SoundId id, double w, int gap) { if (w > 0.02 && bar >= accentUntilBar[id]) c[n++] = { id, w, gap }; };
    add(SND_WIND, s.plants * (1.0 - M) * (P >= -0.3f ? 1 : 0.5), 4);
    add(SND_CHIRPS, day && P > 0.2f && M < 0.5f && !s.enclosed ? s.plants : 0, 2);
    add(SND_NIGHT_SHIMMER, night && P >= 0 && !s.enclosed ? 0.3 + s.plants : 0, 4);
    add(SND_MUSHROOM, s.glow, 2);
    add(SND_WORKS_HUM, s.machines, 4);
    add(SND_WORKS, s.machines * (0.5 + A), 1);
    add(SND_WORN, P < -0.3f ? s.dark : 0, 8);
    add(SND_HEARTBEAT, P < -0.3f ? s.dark : 0, 2);
    add(SND_DRIP, s.water * (s.enclosed ? 1.0 : 0.3), 2);
    add(SND_EMBER, s.ember, 1);
    add(SND_CLAVE, h.pulsed && s.musicBlockCount > 0 ? 0.8 : 0, 1);
    if (n == 0) return;
    double total = 0;
    for (int i = 0; i < n; i++) total += c[i].w;
    double r = Hash01((uint64_t)bar, 202) * total;
    int pick = 0;
    for (; pick < n - 1; pick++) { if (r < c[pick].w) break; r -= c[pick].w; }
    tokens -= 1.0f;
    accentUntilBar[c[pick].id] = bar + c[pick].gapBars;
    Ambient(toNextBar, c[pick].id);
}

// The Afternoon motif's 12-beat phrase: an answer in its rests (V5).
void SoundPalette::Impl::OnPhrase(int64_t phrase) {
    const float P = axes.positive, A = axes.activity;
    if (h.section != MUSIC_AFTERNOON || P <= 0 || A <= 0.5f) return;
    if (Hash01((uint64_t)phrase, 303) > 0.5) return;
    double phraseStart = h.sectionStartBeat + phrase * 12.0;
    double at = (phraseStart + 7.0 - h.beat) / Bps();
    if (at < 0.02) return;
    Begin(SND_ANSWER);
    if (now < tier2Until) return;
    Answer(at);
    Finish();
    scheduled++;
}

void SoundPalette::Impl::OnBlock() {
    if (!running || !ambientOn) return;
    // The day's seam and the sun's horizon crossings.
    if (prevMusicTime >= 0) {
        if (musicTime < prevMusicTime - 1000.0) { Begin(SND_SEAM); Seam(); Finish(); }
        auto crossed = [&](double t) { return prevMusicTime < t && musicTime >= t; };
        if (crossed(30.0) || crossed(2980.0)) {
            Begin(SND_GLIMMER); Glimmer(UntilGrid(1.0, 0.02));
            if (musicTime < 100.0) Bloom(UntilDownbeat(0.05), -31);
            Finish();
        }
    }
    prevMusicTime = musicTime;
    // Footsteps on the beat: crouch every other beat, walk every beat,
    // sprint on 8ths -- whatever the section, the grid always exists.
    if (gait != GAIT_NONE) {
        double grid = gait == GAIT_CROUCH ? 2.0 : gait == GAIT_SPRINT ? 0.5 : 1.0;
        if (nextStepBeat < 0) nextStepBeat = std::ceil(h.beat / grid - 1e-9) * grid;
        double blockEnd = h.beat + kBlock * Bps() / kSR;
        if (nextStepBeat < blockEnd) {
            SoundCue c; c.id = SND_FOOTFALL; c.material = gaitGround; c.key = steps++;
            c.strength = gait == GAIT_CROUCH ? 0.6f : 1.0f;
            c.height = 1; // placed by the grid, not locked again
            Begin(SND_FOOTFALL);
            b.pan = 0; // steps sit under the listener, alternating a little
            b.at = now + Samples(std::max(0.0, (nextStepBeat - h.beat) / Bps()));
            Footfall(c);
            Finish();
            nextStepBeat += grid;
        }
    } else nextStepBeat = -1;
    int64_t bar = (int64_t)std::floor(h.beat / 4.0);
    if (bar != lastBar) { if (lastBar != INT64_MIN) OnBar(bar); lastBar = bar; }
    if (h.section == MUSIC_AFTERNOON) {
        int64_t phrase = (int64_t)std::floor((h.beat - h.sectionStartBeat) / 12.0);
        if (phrase != lastPhrase) { if (lastPhrase != INT64_MIN) OnPhrase(phrase); lastPhrase = phrase; }
    }
    if (scene.enclosed && !wasEnclosed && now >= cooldownUntil[SND_ENCLOSURE]) {
        cooldownUntil[SND_ENCLOSURE] = now + Samples(30);
        Ambient(UntilGrid(2.0, 0.02), SND_ENCLOSURE);
    }
    wasEnclosed = scene.enclosed;
}

// =====================================================================
// Rendering
// =====================================================================

void SoundPalette::Impl::RenderBlock(float* outLR, int len) {
    const float P = axes.positive, A = axes.activity, M = axes.mechanical;
    double capBase = Clamp(1.4 * h.cutoffHz * std::pow(2.0, 0.5 * P) * (0.85 + 0.3 * M), 500, 4200);
    if (now < capBoostUntil) capBase = std::min(4200.0, capBase * capBoost);
    if (scene.enclosed) capBase = std::max(500.0, capBase * 0.7);
    double bps = running ? Bps() : 0.0;
    double beat0 = h.beat;
    float dryL[kBlock], dryR[kBlock], send[kBlock];
    std::memset(dryL, 0, sizeof(dryL));
    std::memset(dryR, 0, sizeof(dryR));
    std::memset(send, 0, sizeof(send));

    for (auto& x : v) {
        if (!x.on) continue;
        if (x.start >= now + len) continue;
        if (now >= x.end) { x.on = false; continue; }
        int j0 = (int)std::max<int64_t>(0, x.start - now);
        // ---- control rate --------------------------------------------
        double tv = (double)(now + j0 - x.start) / kSR;  // seconds since onset at this block
        double tm = (double)(now + len / 2 - x.start) / kSR;
        if (tm < 0) tm = 0;
        // Pitch: glide, chord follow, vibrato, wow.
        double hz = x.hz;
        if (x.followRole >= 0 || x.followBass) {
            double want = x.followBass ? BassHz() * x.followMul : RoleHz(x.followRole, x.followFloor);
            if (x.target == 0) { x.target = want; x.glideFrom = want; x.glideAt = now; }
            if (std::fabs(want - x.target) > 0.01) { x.glideFrom = x.curHz > 0 ? x.curHz : want; x.target = want; x.glideAt = now; }
            double u = Ramp((double)(now - x.glideAt) / kSR / std::max(0.01f, x.followGlide));
            hz = x.glideFrom * std::pow(x.target / x.glideFrom, u);
        } else if (x.glide == GL_RAMP) {
            double u = Ramp(tm / std::max(0.001f, x.glideTime));
            double c = 1200.0 * std::log2(x.hzFrom / x.hz) * (1.0 - u);
            if (x.glideStep > 0) c = std::round(c / x.glideStep) * x.glideStep;
            hz = x.hz * Cents(c);
        } else if (x.glide == GL_EXP) {
            hz = x.hz + (x.hzFrom - x.hz) * std::exp(-tm / std::max(0.001f, x.glideTime));
        }
        double cents = 0;
        if (x.vibCents > 0) cents += x.vibCents * Ramp((tm - 0.15) / 0.2) * Sin01(5.2 * tm);
        if (x.wowCents > 0) cents += x.wowCents * Sin01(0.55 * tm + 0.13 * (double)(x.seed & 7));
        hz *= Cents(cents);
        x.curHz = hz;
        if (x.wave == W_NOISE && (x.followRole >= 0)) { x.bp0 = x.bp1 = (float)hz; }
        double dt = hz / kSR;
        // Filters.
        double cap = capBase * std::pow(2.0, x.capOct);
        double tone = cap;
        if (x.pluck0 > 0) tone = std::min(tone, hz * (x.pluck1 + (x.pluck0 - x.pluck1) * std::exp(-tm / x.pluckTau)));
        if (x.lp0 > 0) {
            double u = x.lpTime > 0 ? Ramp(tm / x.lpTime) : 1.0;
            tone = std::min(tone, x.lp0 * std::pow((double)x.lp1 / x.lp0, u));
        }
        tone = Clamp(tone, 40.0, 0.45 * kSR);
        x.f[0] = Lowpass(tone, x.q > 0 ? x.q : 0.7071);
        double norm = 1.0;
        if (x.wave == W_NOISE) {
            double u = x.bpTime > 0 ? Ramp(tm / x.bpTime) : 1.0;
            double c = x.bp0 * std::pow((double)x.bp1 / x.bp0, u);
            if (x.bpQ > 0) { x.f[1] = Bandpass(c, x.bpQ); norm = NoiseNorm(c / x.bpQ); }
            else { x.f[1] = Lowpass(Clamp(c, 40, 0.45 * kSR), 0.7071); norm = NoiseNorm(c); }
        } else if (x.wave == W_VOX) {
            double u = x.vowelTime > 0 ? Ramp((tm - x.vowelAt) / x.vowelTime) : (tm >= x.vowelAt ? 1.0 : 0.0);
            if (x.vowelStep) u = tm >= x.vowelAt ? Ramp((tm - x.vowelAt) / 0.02) : 0.0;
            double f1 = Lerp(kVowelF1[x.vowel0], kVowelF1[x.vowel1], u);
            double f2 = Lerp(kVowelF2[x.vowel0], kVowelF2[x.vowel1], u);
            x.f[1] = Bandpass(f1, 5.0);
            if (f2 > 0) x.f[2] = Bandpass(f2, 4.0);
            x.f[3] = Bandpass(f2 > 0 ? f2 : 600.0, 2.0);
        }
        if (x.breath > 0 && x.wave != W_VOX) x.f[3] = Bandpass(Clamp(2.0 * hz, 100, 4000), 2.0);
        double breathNorm = x.breath > 0 ? NoiseNorm((x.wave == W_VOX ? 1000.0 : 2.0 * hz) / 2.0) : 0.0;
        // Envelope at block ends, for the decaying parts (per-sample raised
        // cosines handle the corners).
        double relT = x.relAt >= 0 ? (double)(now - x.relAt) / kSR : 0.0;
        double metalOk = hz * 5.4 < 4200 ? 1.0 : hz * 2.76 < 4200 ? 0.5 : 0.0;
        double twin = x.twinCents > 0 ? Cents(x.twinCents) : 1.0;
        // Equal-power pan, kept within +-60 % (nothing hard in one ear) and
        // scaled so the centre is exactly the mono level.
        double pn = mono ? 0.0 : Clamp(x.pan, -1.0, 1.0) * 0.6;
        double th = (pn + 1.0) * (kPi / 4.0);
        float gl = (float)(std::cos(th) * 1.41421356), gr = (float)(std::sin(th) * 1.41421356);
        double inv = 1.0 / len;
        (void)inv;
        // ---- sample rate ---------------------------------------------
        for (int j = j0; j < len; j++) {
            double t = tv + (double)(j - j0) / kSR;
            double env = EnvAt(x, t, relT + (double)j / kSR);
            if (x.fadeStart >= 0) {
                double fu = (double)(now + j - x.fadeStart) / (double)x.fadeLen;
                env *= 1.0 - Ramp(fu);
                if (fu >= 1.0) { x.on = false; break; }
            }
            x.ph = Frac(x.ph + dt);
            double s1, val = 0;
            switch (x.wave) {
            case W_SINE: case W_THUMP: val = Sin01(x.ph); break;
            case W_STRI: val = SoftTri(x.ph); break;
            case W_BLEND: s1 = Sin01(x.ph); val = 0.65 * s1 + 0.45 * SoftTriFrom(s1); break;
            case W_PULSE: val = 0.55 * BlepPulse(x.ph, dt, x.width); break;
            case W_MORPH: {
                s1 = Sin01(x.ph);
                double tri = SoftTriFrom(s1), bl = 0.65 * s1 + 0.45 * tri;
                val = x.morph < 0.5f ? Lerp(tri, bl, 2.0 * x.morph) : Lerp(bl, 0.55 * BlepPulse(x.ph, dt, 0.34), 2.0 * x.morph - 1.0);
                break;
            }
            case W_SAW: {
                val = BlepSaw(x.ph, dt);
                if (x.twinCents > 0) { x.phT = Frac(x.phT + dt * twin); val = 0.5 * (val + BlepSaw(x.phT, dt * twin)); }
                break;
            }
            case W_NOISE: val = RunBiquad(x.f[1], Noise(x.n++, x.seed), x.z[1][0], x.z[1][1]) * norm; break;
            case W_VOX: {
                s1 = Sin01(x.ph);
                double src = Lerp(SoftTriFrom(s1), 0.55 * BlepPulse(x.ph, dt, 0.4), x.morph) + x.sawMix * BlepSaw(x.ph, dt);
                double f1 = RunBiquad(x.f[1], src, x.z[1][0], x.z[1][1]);
                double f2 = kVowelF2[x.vowel0] > 0 ? RunBiquad(x.f[2], src, x.z[2][0], x.z[2][1]) : 0.0;
                val = 2.6 * (f1 + 0.63 * f2);
                break;
            }
            case W_BELL: {
                s1 = Sin01(x.ph);
                double pe = std::exp(-t / std::max(0.01f, x.partTau));
                val = s1 + (x.p2 * Sin01(2.0 * x.ph) + x.p3 * Sin01(3.0 * x.ph)) * pe;
                if (x.px > 0) { x.phX = Frac(x.phX + dt * x.pxRatio); val += x.px * Sin01(x.phX) * pe; }
                break;
            }
            }
            if (x.metal > 0 && metalOk > 0 && Tonal(x.wave)) {
                x.phM1 = Frac(x.phM1 + dt * 2.76);
                x.phM2 = Frac(x.phM2 + dt * 5.40);
                double me = std::exp(-t / 0.04);
                val += x.metal * me * (Sin01(x.phM1) + (metalOk >= 1.0 ? 0.6 * Sin01(x.phM2) : 0.0));
            }
            if (x.breath > 0) val += x.breath * breathNorm * RunBiquad(x.f[3], Noise(x.n++, x.seed ^ 0x55), x.z[3][0], x.z[3][1]);
            // Tone lowpass (the shared filter character).
            val = RunBiquad(x.f[0], val, x.z[0][0], x.z[0][1]);
            // Tremolo / gate.
            if (x.tremDepth > 0) {
                double ph = x.tremHz > 0 ? t * x.tremHz : (beat0 + j * bps / kSR) / x.tremBeats;
                double fr = Frac(ph), am;
                if (x.tremGate) {
                    double periodSec = x.tremHz > 0 ? 1.0 / x.tremHz : x.tremBeats * 60.0 / h.bpm;
                    double r = std::min(0.45, 0.005 / periodSec);
                    am = fr < 0.5 ? Ramp(fr / r) * (1.0 - Ramp((fr - (0.5 - r)) / r)) : 0.0;
                } else am = 0.5 + 0.5 * Sin01(fr + 0.25);
                val *= 1.0 - x.tremDepth * (1.0 - am);
            }
            double y = val * env * x.gain;
            if (x.duck > 0 && h.pulsed && running) {
                double bf = Frac(beat0 + j * bps / kSR);
                double d = bf < 0.03 ? Ramp(bf / 0.03) : bf < 0.5 ? 1.0 - Ramp((bf - 0.03) / 0.47) : 0.0;
                y *= 1.0 - x.duck * h.duckDepth * intensity * d;
            }
            dryL[j] += (float)y * gl;
            dryR[j] += (float)y * gr;
            send[j] += (float)(y * x.echo);
        }
        for (int k = 0; k < 4; k++) { FlushDenormal(x.z[k][0]); FlushDenormal(x.z[k][1]); }
    }

    // ---- bus: echo, master --------------------------------------------
    double target = 0.75 * 60.0 / h.bpm * kSR;
    double fb = 0.38 - 0.14 * A;
    double toneHz = std::min(1800.0 * std::pow(2.0, 0.47 * P), capBase * 1.2);
    double aLp = 1.0 - std::exp(-2.0 * kPi * toneHz / kSR);
    double aHp = 1.0 - std::exp(-2.0 * kPi * 180.0 / kSR);
    double master = h.masterGain * kOutputScale;
    double energy = 0;
    for (int j = 0; j < len; j++) {
        echoDelay += (target - echoDelay) * 0.0002;
        double rp = echoW - echoDelay;
        while (rp < 0) rp += kEchoLen;
        int i0 = (int)rp; double fr = rp - i0;
        int ia = i0 & (kEchoLen - 1), ib = (i0 + 1) & (kEchoLen - 1);
        double ya = echo[ia] * (1.0 - fr) + echo[ib] * fr;
        double yb = echoB[ia] * (1.0 - fr) + echoB[ib] * fr;
        e1 += aLp * (ya - e1); e2 += aHp * (e1 - e2);
        f1 += aLp * (yb - f1); f2 += aHp * (f1 - f2);
        double wetL = e1 - e2, wetR = f1 - f2;
        // Ping-pong: the left repeat feeds the right line and back.
        echo[echoW] = (float)(send[j] + fb * wetR);
        echoB[echoW] = (float)(fb * wetL);
        echoW = (echoW + 1) & (kEchoLen - 1);
        double l = dryL[j] + wetL, r = dryR[j] + wetR;
        if (mono) { double m = 0.5 * (l + r); l = r = m; }
        l = RunBiquad(busLp, RunBiquad(busLp, l, bz[0][0], bz[0][1]), bz[1][0], bz[1][1]) * master;
        r = RunBiquad(busLp, RunBiquad(busLp, r, bz[2][0], bz[2][1]), bz[3][0], bz[3][1]) * master;
        if (busFadeStart >= 0) {
            double fu = (double)(now + j - busFadeStart) / (double)busFadeLen;
            double g = 1.0 - Ramp(fu);
            l *= g; r *= g;
        }
        energy = std::max(energy, std::max(std::fabs(dryL[j] + wetL), std::fabs(dryR[j] + wetR)));
        outLR[2 * j] = (float)l;
        outLR[2 * j + 1] = (float)r;
    }
    if (busFadeStart >= 0 && now + len - busFadeStart >= busFadeLen) {
        for (auto& x : v) x.on = false;
        std::fill(echo.begin(), echo.end(), 0.0f);
        std::fill(echoB.begin(), echoB.end(), 0.0f);
        e1 = e2 = f1 = f2 = 0;
        busFadeStart = -1;
    }
    for (auto& zz : bz) { FlushDenormal(zz[0]); FlushDenormal(zz[1]); }
    FlushDenormal(e1); FlushDenormal(e2); FlushDenormal(f1); FlushDenormal(f2);
    lastEnergy = energy;
    if (energy > 1e-4) quietSince = now + len;
    now += len;
    if (running) musicTime += (double)len / kSR;
}

// =====================================================================
// Public API
// =====================================================================

SoundPalette::SoundPalette() : m(new Impl()) {}
SoundPalette::~SoundPalette() { delete m; }
void SoundPalette::Reset() { int64_t n = m->now; m->ResetAll(); m->now = n; }
void SoundPalette::SetAxes(const SoundAxes& a) {
    m->axes.positive = (float)Clamp(a.positive, -1, 1);
    m->axes.activity = (float)Clamp(a.activity, 0, 1);
    m->axes.mechanical = (float)Clamp(a.mechanical, 0, 1);
}
void SoundPalette::SetScene(const AmbientScene& s) { m->scene = s; }
void SoundPalette::SetIntensity(float i) { m->intensity = (float)Clamp(i, 0, 1); }
void SoundPalette::SetAmbientEnabled(bool on) { m->ambientOn = on; }
void SoundPalette::Play(const SoundCue& c) { m->PlayCue(c); }
void SoundPalette::Release(SoundId id) {
    for (auto& x : m->v)
        if (x.on && x.id == id && x.env == ENV_SUSTAIN && x.relAt < 0) {
            x.relAt = std::max(m->now, x.start + m->Samples(x.atk));
            x.end = x.relAt + m->Samples(x.rel) + 64;
        }
}
void SoundPalette::FadeOut(float seconds) {
    m->busFadeStart = m->now;
    m->busFadeLen = std::max<int64_t>(1, m->Samples(seconds));
}
void SoundPalette::RenderStereo(float* outLR, int frames, double musicTime, bool running) {
    m->running = running;
    m->musicTime = musicTime;
    int done = 0;
    while (done < frames) {
        int len = std::min(kBlock, frames - done);
        MusicHarmonyAt(m->musicTime, &m->h);
        m->haveH = true;
        m->OnBlock();
        m->RenderBlock(outLR + 2 * done, len);
        done += len;
    }
}
void SoundPalette::Render(float* out, int n, double musicTime, bool running) {
    float lr[2 * kBlock];
    int done = 0;
    while (done < n) {
        int len = std::min(kBlock, n - done);
        RenderStereo(lr, len, musicTime + (running ? done / kSR : 0.0), running);
        for (int j = 0; j < len; j++) out[done + j] = 0.5f * (lr[2 * j] + lr[2 * j + 1]);
        done += len;
    }
}
void SoundPalette::SetListener(float x, float y, float z, float yaw) { m->lx = x; m->ly = y; m->lz = z; m->lyaw = yaw; }
void SoundPalette::SetMono(bool mono) { m->mono = mono; }
void SoundPalette::SetGait(int gait, SoundMaterial ground) {
    if (gait != m->gait) m->nextStepBeat = -1; // a new gait starts on its own next grid line
    m->gait = gait; m->gaitGround = ground;
}
int SoundPalette::PlayedCount(SoundId id) const { return id < SND_COUNT ? m->played[id] : 0; }
bool SoundPalette::Silent() const {
    for (auto& x : m->v) if (x.on) return false;
    return m->now - m->quietSince > m->Samples(2.0);
}
int SoundPalette::RecentNotes(NoteLog* out, int max) const {
    int n = std::min(max, m->logN);
    for (int i = 0; i < n; i++) out[i] = m->log[(m->logHead - n + i + 64) % 64];
    return n;
}
int SoundPalette::ActiveVoices() const { int c = 0; for (auto& x : m->v) if (x.on) c++; return c; }
int64_t SoundPalette::Now() const { return m->now; }
SoundAxes SoundPalette::Axes() const { return m->axes; }
int SoundPalette::ScheduledAmbientEvents() const { return m->scheduled; }
int SoundPalette::GestureIndex() const { return m->gIdx; }
