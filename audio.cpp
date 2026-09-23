// audio.cpp
//
// Section 10 / Part XIV - Audio (chunked, clock-synced day-cycle music).
//
// One persistent source voice plays the day-cycle music (music_synth.cpp
// generates it), generated in small chunks continuously anchored to the
// live day clock (g_dayTimeSeconds) rather than baked once as a fixed
// loop -- see DESIGN.md Part XIV for why: this is what makes it
// structurally impossible for playback to drift out of sync with the
// clock, rather than merely unlikely. Master and Music are separate
// settings/sliders so adding an SFX channel later is just another source
// voice under the same mastering voice, not a change to the mixing model.
//
// Silent at the title screen by construction: nothing ever calls
// StartMusicPlayback() until a game actually begins (New Game/Load
// Game), and nothing resumes it after Quit to Title. Silent during any
// paused menu too, per an explicit request -- losing the music when you
// pause reads as "time itself stopped," which is the point.

#define NOMINMAX
#include <windows.h>
#include <xaudio2.h>
#include "audio.h"
#include "world.h"   // g_dayTimeSeconds
#include "persist.h" // g_masterVolume / g_musicVolume / g_musicIntensity
#include <cstdint>
#include <deque>

#pragma comment(lib, "xaudio2.lib")

// Defined in music_synth.cpp; MusicState's layout above must match its
// use there exactly (no shared header enforces this, by convention).
extern "C" void ResetMusicState(MusicState* s);
extern "C" void GenerateMusicChunk(double startTime, int sampleCount, double intensity, MusicState* state, int16_t* outPCM);

static IXAudio2* g_xaudio2 = nullptr;
static IXAudio2MasteringVoice* g_masteringVoice = nullptr;
static IXAudio2SourceVoice* g_musicVoice = nullptr;
static MusicState g_musicState;
static double g_nextChunkStartTime = -1.0; // -1 = inactive (title screen / paused)
static std::deque<int16_t*> g_musicPendingBuffers; // FIFO, oldest-submitted first; freed once XAudio2 finishes each one
static const int MUSIC_SAMPLE_RATE = 44100; // must match music_synth.cpp's kMusicSampleRate
// Quarter-second chunks rather than whole-second ones: each chord-bed
// sample can cost dozens of sin() calls (up to 5 tones x 6 harmonics x
// 2 during a mode-crossfade window, plus the arp's own 6), so a whole
// second of it generated in one synchronous call is real, occasionally
// visible work on the main thread. Four times as many, four times
// smaller calls spread that same total cost more evenly across frames
// instead of risking one periodic ~1-second-cadence hitch. 16 chunks
// of lookahead (4s buffered) also gives more tolerance for a brief
// stall (e.g. dragging the window, which blocks the message loop
// entirely) before the queue actually runs dry and goes quiet.
static const int MUSIC_CHUNK_SAMPLES = MUSIC_SAMPLE_RATE / 4;
static const int MUSIC_LOOKAHEAD_CHUNKS = 16;

void ApplyAudioVolumes() {
    if (g_musicVoice) g_musicVoice->SetVolume(g_masterVolume * g_musicVolume);
}

static void FreeAllPendingMusicBuffers() {
    for (int16_t* p : g_musicPendingBuffers) delete[] p;
    g_musicPendingBuffers.clear();
}

static void SubmitOneMusicChunk() {
    int16_t* chunk = new int16_t[MUSIC_CHUNK_SAMPLES];
    GenerateMusicChunk(g_nextChunkStartTime, MUSIC_CHUNK_SAMPLES, (double)g_musicIntensity, &g_musicState, chunk);
    g_nextChunkStartTime += (double)MUSIC_CHUNK_SAMPLES / MUSIC_SAMPLE_RATE;
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = MUSIC_CHUNK_SAMPLES * sizeof(int16_t);
    buf.pAudioData = (const BYTE*)chunk;
    g_musicVoice->SubmitSourceBuffer(&buf);
    g_musicPendingBuffers.push_back(chunk);
}

// Called on New Game, Load Game, Resume from Pause, and Quick Load --
// every path that either starts a game or changes g_dayTimeSeconds out
// from under the music. Always resets MusicState to a clean zero-state
// (Section 10's filter/arp-scheduler reset-on-discontinuity policy) and
// re-anchors to whatever g_dayTimeSeconds is *right now*, so there is
// never a stale, independently-advancing audio position to reconcile.
void StartMusicPlayback() {
    if (!g_musicVoice) return;
    g_musicVoice->Stop();
    // Every current call site already routes through StopMusicPlayback
    // (or has never started at all) before reaching here, so the
    // voice's internal queue should already be empty -- but flushing
    // defensively costs nothing and means freeing g_musicPendingBuffers
    // right after can never race a still-referenced buffer, regardless
    // of how future call sites end up wired.
    g_musicVoice->FlushSourceBuffers();
    FreeAllPendingMusicBuffers();
    ResetMusicState(&g_musicState);
    g_nextChunkStartTime = g_dayTimeSeconds;
    for (int i = 0; i < MUSIC_LOOKAHEAD_CHUNKS; i++) SubmitOneMusicChunk();
    g_musicVoice->Start();
}

// Called whenever any menu opens during play (pause is silence, by
// request -- it signals the passage of in-game time stopping, not a
// real-time-continues-in-the-background pause) and on Quit to Title.
void StopMusicPlayback() {
    if (!g_musicVoice) return;
    g_musicVoice->Stop();
    g_musicVoice->FlushSourceBuffers();
    FreeAllPendingMusicBuffers();
    g_nextChunkStartTime = -1.0;
}

// Tops up the lookahead queue -- called once per simulation tick, only
// ever while a game is actually running and unpaused (Section 13's
// clock-advance gate), so it's a natural no-op at the title screen and
// while paused without needing its own separate condition.
void RefillMusicQueueIfNeeded() {
    if (!g_musicVoice || g_nextChunkStartTime < 0.0) return;
    XAUDIO2_VOICE_STATE vstate;
    g_musicVoice->GetState(&vstate);
    UINT32 queued = vstate.BuffersQueued;
    while (g_musicPendingBuffers.size() > (size_t)queued) {
        delete[] g_musicPendingBuffers.front();
        g_musicPendingBuffers.pop_front();
    }
    while (queued < (UINT32)MUSIC_LOOKAHEAD_CHUNKS) {
        SubmitOneMusicChunk();
        queued++;
    }
}

// Failure anywhere here (no audio device, driver issue, etc.) leaves
// g_musicVoice null and every subsequent audio call a silent no-op via
// the null checks above -- a machine with no usable audio device still
// gets a fully playable game, just a silent one, rather than a startup
// failure.
bool InitAudio() {
    if (FAILED(XAudio2Create(&g_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
    if (FAILED(g_xaudio2->CreateMasteringVoice(&g_masteringVoice))) return false;

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = MUSIC_SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)((wfx.nChannels * wfx.wBitsPerSample) / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    if (FAILED(g_xaudio2->CreateSourceVoice(&g_musicVoice, &wfx))) return false;
    ApplyAudioVolumes();
    // Deliberately not started here -- the title screen is silent by
    // design (Section 13); playback only begins via StartMusicPlayback().
    return true;
}

void ShutdownAudio() {
    if (g_musicVoice) { g_musicVoice->Stop(); g_musicVoice->DestroyVoice(); g_musicVoice = nullptr; }
    if (g_masteringVoice) { g_masteringVoice->DestroyVoice(); g_masteringVoice = nullptr; }
    if (g_xaudio2) { g_xaudio2->Release(); g_xaudio2 = nullptr; }
    FreeAllPendingMusicBuffers();
}
