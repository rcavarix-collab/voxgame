// audio.h
//
// XAudio2 playback of the day-cycle music (music_synth.cpp generates
// it). See DESIGN.md Part XIV for the full design -- the short version:
// music is generated in small chunks continuously anchored to the live
// day clock (world.h's g_dayTimeSeconds) rather than baked once, so
// playback can never drift out of sync with it.

#pragma once

#include "sfx_synth.h"

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
// Tops up the lookahead queue by at most one chunk -- called once per
// frame; a natural no-op at the title screen and while paused, since
// both have playback stopped.
void RefillMusicQueueIfNeeded();
// 0..1 pulse on each note onset in the music currently audible (not the
// audio queued ahead; musiclevel.h), fading ~0.1 s after each; 0 while
// silent or paused. Once per frame.
float CurrentMusicLevel();

// ---- World sound palette (docs/SOUND_PALETTE.md; DESIGN.md Part X.4) ----
// A second voice beside the music, on small buffers (~35-45 ms), rendering
// sfx_synth.h's palette in time and in key with what the music is playing.
void PlayWorldSound(const SoundCue& cue);
void ReleaseWorldSound(SoundId id);
// Pausing: every palette tail fades away over `seconds` (silence = time stopped).
void FadeWorldSounds(float seconds);
// Once per frame: the soundscape's axes and scene, whether play is live
// (the ambient scheduler runs only then), and a queue top-up.
void UpdateWorldSound(const SoundAxes& axes, const AmbientScene& scene, bool playing);
// The music time now audible (the chunk playing, not the audio queued ahead).
double AudibleMusicTime();
