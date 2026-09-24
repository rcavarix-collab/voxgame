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
#include "music_synth.h"
#include "world.h"   // g_dayTimeSeconds
#include "persist.h" // g_masterVolume / g_musicVolume / g_musicIntensity
#include "musiclevel.h"
#include <cstdint>
#include <cmath>

#pragma comment(lib, "xaudio2.lib")

// The music is composed against a fixed hour; a different day length would
// silently desync every section and chord from the clock.
static_assert((double)DAY_LENGTH_SECONDS == MUSIC_DAY_LENGTH, "music day length must match world.h DAY_LENGTH_SECONDS");

static IXAudio2* g_xaudio2 = nullptr;
static IXAudio2MasteringVoice* g_masteringVoice = nullptr;
static IXAudio2SourceVoice* g_musicVoice = nullptr;
static MusicState g_musicState;
static double g_nextChunkStartTime = -1.0; // -1 = inactive (title screen / paused)
// Quarter-second chunks, 16 of lookahead (4 s buffered) -- enough slack
// for a brief message-loop stall (e.g. dragging the window) before the
// queue runs dry.
static const int MUSIC_CHUNK_SAMPLES = MUSIC_SAMPLE_RATE / 4;
static const int MUSIC_LOOKAHEAD_CHUNKS = 16;
// Chunks generated synchronously when playback (re)starts (~1.3 ms each):
// a full second of audio, so the heavy first frames after a New Game or
// Load (column generation, a burst of mesh rebuilds) can't drain the voice
// before the one-chunk-per-frame refill catches up.
static const int MUSIC_PRIME_CHUNKS = 4;

// Fixed pool of PCM buffers, reused cyclically instead of new/delete per
// chunk. XAudio2 reads a submitted buffer from its own thread until it
// stops counting it in BuffersQueued; submission and consumption are
// both FIFO, so with one more slot than the lookahead, the slot about to
// be overwritten is always older than every buffer still queued.
static const int MUSIC_POOL_SIZE = MUSIC_LOOKAHEAD_CHUNKS + 1;
static int16_t (*g_musicPool)[MUSIC_CHUNK_SAMPLES] = nullptr;
static int g_musicPoolNext = 0;
// Note onsets (musiclevel.h) in each pooled chunk, 1/64 s steps, measured
// when the chunk is synthesized; read back at the chunk actually playing,
// so anything that reacts to the music follows what's heard, not the
// audio generated seconds ahead (Section 10.4).
static const int LEVEL_STEPS = 16;
static float g_chunkLevels[MUSIC_POOL_SIZE][LEVEL_STEPS] = {};
static MusicLevelMeter g_levelMeter;  // carried from chunk to chunk, in generation order
static double g_levelLastNow = 0;     // last CurrentMusicLevel call, seconds (QPC)
static int g_levelSlot = -1;          // pool slot last seen playing
static double g_levelSlotStart = 0;   // when it started, seconds (QPC)
static float g_levelSmoothed = 0;
// Set after Stop+Flush: the flush only takes effect on the audio
// thread's next processing pass, so no pool slot may be rewritten until
// BuffersQueued has actually reached zero.
static bool g_musicNeedsDrain = false;

void ApplyAudioVolumes() {
    if (g_musicVoice) g_musicVoice->SetVolume(g_masterVolume * g_musicVolume);
}

static UINT32 QueuedMusicBuffers() {
    XAUDIO2_VOICE_STATE vstate;
    g_musicVoice->GetState(&vstate, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return vstate.BuffersQueued;
}

// Normally already drained (a pause lasts far longer than one ~10 ms
// processing pass), so this rarely waits at all. If it somehow doesn't
// drain, the old pool is abandoned rather than risk rewriting memory the
// audio thread may still read -- a small leak beats a use-after-free.
static void WaitForMusicDrain() {
    if (!g_musicNeedsDrain) return;
    for (int i = 0; i < 100 && QueuedMusicBuffers() > 0; i++) Sleep(1);
    if (QueuedMusicBuffers() > 0) {
        OutputDebugStringA("audio: music voice did not drain after flush; abandoning buffer pool\n");
        g_musicPool = new int16_t[MUSIC_POOL_SIZE][MUSIC_CHUNK_SAMPLES];
        g_musicPoolNext = 0;
    }
    g_musicNeedsDrain = false;
}

static void SubmitOneMusicChunk() {
    int16_t* chunk = g_musicPool[g_musicPoolNext];
    g_musicPoolNext = (g_musicPoolNext + 1) % MUSIC_POOL_SIZE;
    GenerateMusicChunk(g_nextChunkStartTime, MUSIC_CHUNK_SAMPLES, (double)g_musicIntensity, &g_musicState, chunk);
    int slot = (int)(chunk - g_musicPool[0]) / MUSIC_CHUNK_SAMPLES;
    MeasureMusicLevels(g_levelMeter, chunk, MUSIC_CHUNK_SAMPLES, LEVEL_STEPS, MUSIC_SAMPLE_RATE, g_chunkLevels[slot]);
    g_nextChunkStartTime += (double)MUSIC_CHUNK_SAMPLES / MUSIC_SAMPLE_RATE;
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = MUSIC_CHUNK_SAMPLES * sizeof(int16_t);
    buf.pAudioData = (const BYTE*)chunk;
    g_musicVoice->SubmitSourceBuffer(&buf);
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
    g_musicVoice->FlushSourceBuffers();
    g_musicNeedsDrain = true;
    WaitForMusicDrain();
    ResetMusicState(&g_musicState);
    g_levelMeter = MusicLevelMeter();
    g_nextChunkStartTime = g_dayTimeSeconds;
    for (int i = 0; i < MUSIC_PRIME_CHUNKS; i++) SubmitOneMusicChunk();
    g_musicVoice->Start();
}

// Called whenever any menu opens during play (pause is silence, by
// request -- it signals the passage of in-game time stopping, not a
// real-time-continues-in-the-background pause) and on Quit to Title.
// Doesn't wait for the drain itself -- the next StartMusicPlayback does,
// by which point it has almost always already happened.
void StopMusicPlayback() {
    if (!g_musicVoice) return;
    g_musicVoice->Stop();
    g_musicVoice->FlushSourceBuffers();
    g_musicNeedsDrain = true;
    g_nextChunkStartTime = -1.0;
}

// Tops up the lookahead queue by at most one chunk per call, so music
// generation never costs more than one chunk in any single frame. A
// natural no-op at the title screen and while paused, since both stop
// playback (g_nextChunkStartTime < 0).
void RefillMusicQueueIfNeeded() {
    if (!g_musicVoice || g_nextChunkStartTime < 0.0) return;
    if (QueuedMusicBuffers() < (UINT32)MUSIC_LOOKAHEAD_CHUNKS) SubmitOneMusicChunk();
}

float CurrentMusicLevel() {
    if (!g_musicVoice || g_nextChunkStartTime < 0.0 || !g_musicPool) { g_levelSmoothed = 0; g_levelSlot = -1; return 0.0f; }
    LARGE_INTEGER f, n; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&n);
    double now = (double)n.QuadPart / (double)f.QuadPart;
    UINT32 queued = QueuedMusicBuffers();
    if (queued == 0) return g_levelSmoothed *= 0.9f;
    // Pool slots are used in order, so the oldest still-queued buffer --
    // the one playing -- is `queued` slots behind the next free one.
    int slot = (g_musicPoolNext - (int)queued + MUSIC_POOL_SIZE) % MUSIC_POOL_SIZE;
    if (slot != g_levelSlot) { g_levelSlot = slot; g_levelSlotStart = now; }
    int q = (int)((now - g_levelSlotStart) * MUSIC_SAMPLE_RATE / (MUSIC_CHUNK_SAMPLES / LEVEL_STEPS));
    q = q < 0 ? 0 : (q >= LEVEL_STEPS ? LEVEL_STEPS - 1 : q);
    float target = g_chunkLevels[slot][q];
    // Flashes on a note at once, then fades in ~0.1 s: a pulse per note,
    // the same at any frame rate.
    double dt = g_levelLastNow > 0 ? now - g_levelLastNow : 0.0;
    g_levelLastNow = now;
    float fade = (float)exp(-(dt < 0.25 ? dt : 0.25) / 0.1);
    g_levelSmoothed = target > g_levelSmoothed * fade ? target : g_levelSmoothed * fade;
    return g_levelSmoothed;
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
    g_musicPool = new int16_t[MUSIC_POOL_SIZE][MUSIC_CHUNK_SAMPLES];
    ApplyAudioVolumes();
    // Deliberately not started here -- the title screen is silent by
    // design (Section 13); playback only begins via StartMusicPlayback().
    return true;
}

void ShutdownAudio() {
    if (g_musicVoice) { g_musicVoice->Stop(); g_musicVoice->DestroyVoice(); g_musicVoice = nullptr; }
    if (g_masteringVoice) { g_masteringVoice->DestroyVoice(); g_masteringVoice = nullptr; }
    if (g_xaudio2) { g_xaudio2->Release(); g_xaudio2 = nullptr; }
    // DestroyVoice is synchronous, so nothing can still be reading these.
    delete[] g_musicPool;
    g_musicPool = nullptr;
}
