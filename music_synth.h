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
};

void ResetMusicState(MusicState* s);

// Generates `sampleCount` mono samples starting at absolute day time
// `startTime` (seconds; any value, wrapped to the day internally).
// `intensity` in [0,1] scales only the rhythmic/melodic layers -- 0 is
// the ambient bed alone, 1 is the full designed arrangement, never more.
void GenerateMusicChunk(double startTime, int sampleCount, double intensity,
                        MusicState* state, int16_t* outPCM);
