// audio.h
//
// XAudio2 playback of the day-cycle music (music_synth.cpp generates
// it). See DESIGN.md Part XIV for the full design -- the short version:
// music is generated in small chunks continuously anchored to the live
// day clock (world.h's g_dayTimeSeconds) rather than baked once, so
// playback can never drift out of sync with it.

#pragma once

// MusicState is plain-old-data shared with music_synth.cpp by
// convention (no shared header reaches across to that file either) --
// its field-by-field meaning lives there.
struct MusicState {
    double filterZ1, filterZ2;
    double secondsUntilNextNote;
    int arpCursor;
    double noteEnvTime;
    double noteHoldDur;
    double currentNoteFreq;
};

bool InitAudio();
void ShutdownAudio();

// Re-applies g_masterVolume * g_musicVolume (persist.h) to the live
// voice -- called whenever either slider changes, and once by
// LoadGame's legacy (v2) settings-migration path.
void ApplyAudioVolumes();

// Called on New Game, Load Game, Resume from Pause, and Quick Load --
// every path that either starts a game or changes g_dayTimeSeconds out
// from under the music. Re-anchors to whatever g_dayTimeSeconds is
// right now and resets MusicState to a clean zero-state.
void StartMusicPlayback();
// Called whenever any menu opens during play (pause is silence, by
// request) and on Quit to Title.
void StopMusicPlayback();
// Tops up the lookahead queue -- called once per simulation tick, only
// ever while a game is actually running and unpaused, so it's a
// natural no-op at the title screen and while paused.
void RefillMusicQueueIfNeeded();
