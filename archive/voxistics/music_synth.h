// music_synth.h
//
// The day-cycle music generator (DESIGN.md Part XIV). audio.cpp owns
// playback; this module only turns "absolute day time" into PCM.

#pragma once

#include <cstdint>

static const int MUSIC_SAMPLE_RATE = 44100;
// The hour every curve and the chord timeline are written against.
// audio.cpp static_asserts this equals world.h's DAY_LENGTH_SECONDS.
constexpr double MUSIC_DAY_LENGTH = 3600.0;

// Everything the music plays is a pure function of day time except the
// IIR filter histories and the resume fade-in, which is all that has to
// persist between chunks. ResetMusicState on every discontinuity (new
// game, load, resume) -- a fresh state fades in from silence.
struct MusicState {
    double masterZ1, masterZ2;
    double bedZ1, bedZ2;
    double airZ1, airZ2;
    double padSawZ, bassSawZ;
    double secondsSinceStart;
    // The soundscape colour the last chunk ended on, so the next one
    // glides from it rather than stepping (colourValid = false: none yet).
    float colourP, colourA, colourM;
    bool colourValid;
};

// The three soundscape axes (docs/SOUND_PALETTE.md 3): positive -1..+1,
// activity 0..1, mechanical 0..1. The defaults are the neutral point, at
// which the track renders exactly as composed (Part XIV).
struct MusicColour {
    float positive = 0.2f;
    float activity = 0.3f;
    float mechanical = 0.35f;
};

// The score at one moment, for anything that has to play *with* the music
// (the world sound palette, sfx_synth.h). Pure function of day time.
enum MusicChord { MUSIC_DM9, MUSIC_G7SUS4, MUSIC_EM7, MUSIC_A7SUS4, MUSIC_DRONE, MUSIC_CHORD_COUNT };
enum MusicSection { MUSIC_DAWN, MUSIC_MORNING, MUSIC_MIDDAY, MUSIC_AFTERNOON, MUSIC_DUSK, MUSIC_NIGHT };
struct MusicHarmony {
    double weight[MUSIC_CHORD_COUNT]; // chord crossfade weights (sum 1)
    int chord;          // the dominant chord
    int other;          // the next-strongest (== chord when not crossfading)
    bool blending;      // mid-crossfade: both chords above 0.2
    double bassHz;      // the dominant chord's bass note
    double beat;        // beats since 0:00 (continuous across sections)
    double bpm;         // this section's tempo (every section has one, pulsed or not)
    int section;        // MusicSection
    double sectionStartBeat; // beat at which this section began
    bool pulsed;        // the soft 4/4 pulse is audible
    double cutoffHz;    // the shared lowpass, as composed
    double masterGain;  // the day's master level (linear)
    double duckDepth;   // the bass's sidechain dip depth, 0..~0.4 (before intensity)
    double motifGain;   // the melodic layer's level (0 = no motif playing)
};
void MusicHarmonyAt(double t, MusicHarmony* out);

void ResetMusicState(MusicState* s);

// Generates `sampleCount` mono samples starting at absolute day time
// `startTime` (seconds; any value, wrapped to the day internally).
// `intensity` in [0,1] scales only the rhythmic/melodic layers -- 0 is
// the ambient bed alone, 1 is the full designed arrangement, never more.
// `colour` (optional) is the soundscape the track leans toward (Part X.4,
// docs/SOUND_PALETTE.md 6): small, bounded shifts, glided across the chunk
// from the state's last value. nullptr = neutral = exactly as composed.
void GenerateMusicChunk(double startTime, int sampleCount, double intensity,
                        MusicState* state, int16_t* outPCM, const MusicColour* colour = nullptr);
