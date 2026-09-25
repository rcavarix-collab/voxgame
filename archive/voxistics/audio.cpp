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

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#include "audio.h"
#include "music_synth.h"
#include "world.h"   // g_dayTimeSeconds
#include "persist.h" // g_masterVolume / g_musicVolume / g_musicIntensity
#include "musiclevel.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

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

// The music is synthesized on its own thread (Part XIV): the frame never
// pays for it, however busy the section. The worker keeps the lookahead
// full, generating each chunk *outside* the lock (from copies of the
// synth state, colour and intensity) and publishing it -- levels, start
// time, submission -- in a brief locked step. Starting or stopping
// playback bumps g_musicEpoch under the lock, so a chunk begun for the old
// position is simply thrown away. The main thread's readers (audible time,
// the music level) take the lock only for a few reads.
static std::mutex g_musicLock;
static std::condition_variable g_musicWake;
static std::thread g_musicThread;
static std::atomic<bool> g_musicQuit{ false };
static uint32_t g_musicEpoch = 0;

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
static MusicGlow g_musicGlow;         // the slow, flash-safe swell the block shows
static int g_levelSlot = -1;          // pool slot last seen playing
static double g_levelSlotStart = 0;   // when it started, seconds (QPC)
static float g_levelSmoothed = 0;
// The music time each pooled chunk starts at, so the palette can ask what's
// audible now (AudibleMusicTime).
static double g_chunkStartTime[MUSIC_POOL_SIZE] = {};
// The soundscape colour the track leans toward (docs/SOUND_PALETTE.md 6).
static MusicColour g_musicColour;

// ---- World sound palette: a second voice on small buffers -----------
static IXAudio2SourceVoice* g_worldVoice = nullptr;
static SoundPalette* g_palette = nullptr;
static const int WORLD_BUFFER_SAMPLES = 512;   // 11.6 ms
// Buffers kept queued: enough to cover about two frames (so a 30 fps cap
// or a slow frame never starves the voice), 3 (~35 ms) at 60 fps and up.
static const int WORLD_QUEUE_MIN = 3, WORLD_QUEUE_MAX = 8;
static const int WORLD_POOL = WORLD_QUEUE_MAX + 2;
static int g_worldQueue = WORLD_QUEUE_MIN;
static double g_worldLastPump = 0;
static int16_t (*g_worldPool)[WORLD_BUFFER_SAMPLES * 2] = nullptr; // stereo, interleaved
static int g_worldPoolNext = 0;
static bool g_worldIdle = true;                 // nothing sounding: no buffers rendered

// Set after Stop+Flush: the flush only takes effect on the audio
// thread's next processing pass, so no pool slot may be rewritten until
// BuffersQueued has actually reached zero.
static bool g_musicNeedsDrain = false;

void ApplyAudioVolumes() {
    if (g_musicVoice) g_musicVoice->SetVolume(g_masterVolume * g_musicVolume);
    if (g_worldVoice) g_worldVoice->SetVolume(g_masterVolume * g_worldVolume);
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

static void MusicWorker() {
    std::unique_lock<std::mutex> lk(g_musicLock);
    while (!g_musicQuit) {
        bool due = g_musicVoice && g_musicPool && g_nextChunkStartTime >= 0.0 && !g_musicNeedsDrain
                   && QueuedMusicBuffers() < (UINT32)MUSIC_LOOKAHEAD_CHUNKS;
        if (!due) { g_musicWake.wait_for(lk, std::chrono::milliseconds(20)); continue; }
        // Take the job: which slot, from when, with what -- then let go.
        uint32_t epoch = g_musicEpoch;
        int slot = g_musicPoolNext;
        int16_t* chunk = g_musicPool[slot];
        double start = g_nextChunkStartTime;
        double intensity = (double)g_musicIntensity;
        MusicColour colour = g_musicColour;
        MusicState state = g_musicState;
        MusicLevelMeter meter = g_levelMeter;
        lk.unlock();
        float levels[LEVEL_STEPS];
        GenerateMusicChunk(start, MUSIC_CHUNK_SAMPLES, intensity, &state, chunk, &colour);
        MeasureMusicLevels(meter, chunk, MUSIC_CHUNK_SAMPLES, LEVEL_STEPS, MUSIC_SAMPLE_RATE, levels);
        lk.lock();
        if (epoch != g_musicEpoch || g_musicQuit) continue; // playback restarted or stopped meanwhile: stale
        g_musicState = state;
        g_levelMeter = meter;
        memcpy(g_chunkLevels[slot], levels, sizeof(levels));
        g_chunkStartTime[slot] = start;
        g_musicPoolNext = (slot + 1) % MUSIC_POOL_SIZE;
        g_nextChunkStartTime = start + (double)MUSIC_CHUNK_SAMPLES / MUSIC_SAMPLE_RATE;
        XAUDIO2_BUFFER buf = {};
        buf.AudioBytes = MUSIC_CHUNK_SAMPLES * sizeof(int16_t);
        buf.pAudioData = (const BYTE*)chunk;
        g_musicVoice->SubmitSourceBuffer(&buf);
    }
}

// Called on New Game, Load Game, Resume from Pause, and Quick Load --
// every path that either starts a game or changes g_dayTimeSeconds out
// from under the music. Always resets MusicState to a clean zero-state
// (Section 10's filter/arp-scheduler reset-on-discontinuity policy) and
// re-anchors to whatever g_dayTimeSeconds is *right now*, so there is
// never a stale, independently-advancing audio position to reconcile.
void StartMusicPlayback() {
    if (!g_musicVoice) return;
    {
        std::lock_guard<std::mutex> lk(g_musicLock);
        g_musicVoice->Stop();
        g_musicVoice->FlushSourceBuffers();
        g_musicNeedsDrain = true;
        WaitForMusicDrain();
        ResetMusicState(&g_musicState);
        g_levelMeter = MusicLevelMeter();
        g_nextChunkStartTime = g_dayTimeSeconds;
        g_musicEpoch++; // anything the worker had begun belongs to the old position
        // Playing an empty queue is silence; the worker's first chunk (a
        // millisecond or two) starts the sound.
        g_musicVoice->Start();
    }
    g_musicWake.notify_one();
}

// Called whenever any menu opens during play (pause is silence, by
// request -- it signals the passage of in-game time stopping, not a
// real-time-continues-in-the-background pause) and on Quit to Title.
// Doesn't wait for the drain itself -- the next StartMusicPlayback does,
// by which point it has almost always already happened.
void StopMusicPlayback() {
    if (!g_musicVoice) return;
    std::lock_guard<std::mutex> lk(g_musicLock);
    g_musicVoice->Stop();
    g_musicVoice->FlushSourceBuffers();
    g_musicNeedsDrain = true;
    g_nextChunkStartTime = -1.0;
    g_musicEpoch++;
}

// The worker keeps the queue topped up by itself; a nudge each frame just
// means a freshly drained queue is noticed without waiting out its timeout.
void RefillMusicQueueIfNeeded() {
    if (g_musicVoice) g_musicWake.notify_one();
}

static double NowSeconds() {
    LARGE_INTEGER f, n; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&n);
    return (double)n.QuadPart / (double)f.QuadPart;
}
// The pool slot XAudio2 is playing now and when it started (first seen),
// shared by the music level and the audible-time readback. -1: none.
static int PlayingSlot(double now, UINT32 queued) {
    if (queued == 0) return -1;
    // Pool slots are used in order, so the oldest still-queued buffer --
    // the one playing -- is `queued` slots behind the next free one.
    int slot = (g_musicPoolNext - (int)queued + MUSIC_POOL_SIZE) % MUSIC_POOL_SIZE;
    if (slot != g_levelSlot) { g_levelSlot = slot; g_levelSlotStart = now; }
    return slot;
}

double AudibleMusicTime() {
    std::lock_guard<std::mutex> lk(g_musicLock);
    if (!g_musicVoice || g_nextChunkStartTime < 0.0 || !g_musicPool) return g_dayTimeSeconds;
    double now = NowSeconds();
    int slot = PlayingSlot(now, QueuedMusicBuffers());
    if (slot < 0) return g_dayTimeSeconds;
    double into = now - g_levelSlotStart;
    double chunk = (double)MUSIC_CHUNK_SAMPLES / MUSIC_SAMPLE_RATE;
    return g_chunkStartTime[slot] + (into < 0 ? 0 : into > chunk ? chunk : into);
}

float CurrentMusicLevel() {
    std::lock_guard<std::mutex> lk(g_musicLock);
    // Silent (title, menus, paused): the glow resets; it swells back in
    // from dark when the music starts again.
    if (!g_musicVoice || g_nextChunkStartTime < 0.0 || !g_musicPool) {
        g_levelSmoothed = 0; g_levelSlot = -1; g_musicGlow = MusicGlow(); g_levelLastNow = 0;
        return 0.0f;
    }
    double now = NowSeconds();
    double dt = g_levelLastNow > 0 ? now - g_levelLastNow : 0.0;
    g_levelLastNow = now;
    float step = (float)(dt < 0.25 ? dt : 0.25);
    // A slow swell with the notes, never a flash per note (musiclevel.h:
    // photosensitivity), the same at any frame rate.
    UINT32 queued = QueuedMusicBuffers();
    if (queued == 0) return g_levelSmoothed = MusicGlowStep(g_musicGlow, 0.0f, step); // starved: fade out gently
    int slot = PlayingSlot(now, queued);
    int q = (int)((now - g_levelSlotStart) * MUSIC_SAMPLE_RATE / (MUSIC_CHUNK_SAMPLES / LEVEL_STEPS));
    q = q < 0 ? 0 : (q >= LEVEL_STEPS ? LEVEL_STEPS - 1 : q);
    g_levelSmoothed = MusicGlowStep(g_musicGlow, g_chunkLevels[slot][q], step);
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
    // The palette's voice is optional: without it the game just has music.
    // Stereo (the music stays mono): world sounds sit where they happen.
    WAVEFORMATEX wfx2 = wfx;
    wfx2.nChannels = 2;
    wfx2.nBlockAlign = (WORD)(2 * wfx.wBitsPerSample / 8);
    wfx2.nAvgBytesPerSec = wfx2.nSamplesPerSec * wfx2.nBlockAlign;
    if (SUCCEEDED(g_xaudio2->CreateSourceVoice(&g_worldVoice, &wfx2))) {
        g_worldPool = new int16_t[WORLD_POOL][WORLD_BUFFER_SAMPLES * 2];
        g_palette = new SoundPalette();
        g_worldVoice->Start();
    } else {
        g_worldVoice = nullptr;
    }
    ApplyAudioVolumes();
    g_musicQuit = false;
    g_musicThread = std::thread(MusicWorker); // idles until playback starts
    // Deliberately not started here -- the title screen is silent by
    // design (Section 13); playback only begins via StartMusicPlayback().
    return true;
}

void ShutdownAudio() {
    if (g_musicThread.joinable()) { // the worker first: it submits to the music voice
        g_musicQuit = true;
        g_musicWake.notify_one();
        g_musicThread.join();
    }
    if (g_worldVoice) { g_worldVoice->Stop(); g_worldVoice->DestroyVoice(); g_worldVoice = nullptr; }
    delete[] g_worldPool; g_worldPool = nullptr;
    delete g_palette; g_palette = nullptr;
    if (g_musicVoice) { g_musicVoice->Stop(); g_musicVoice->DestroyVoice(); g_musicVoice = nullptr; }
    if (g_masteringVoice) { g_masteringVoice->DestroyVoice(); g_masteringVoice = nullptr; }
    if (g_xaudio2) { g_xaudio2->Release(); g_xaudio2 = nullptr; }
    // DestroyVoice is synchronous, so nothing can still be reading these.
    delete[] g_musicPool;
    g_musicPool = nullptr;
}

// ---------------------------------------------------------------------
// World sound palette (docs/SOUND_PALETTE.md)
// ---------------------------------------------------------------------

static UINT32 QueuedWorldBuffers() {
    XAUDIO2_VOICE_STATE vs;
    g_worldVoice->GetState(&vs, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return vs.BuffersQueued;
}

// Tops the palette's queue up to WORLD_QUEUE buffers. Each buffer is
// rendered for the music time it will be heard at: what's audible now plus
// what's already queued ahead of it.
static void PumpWorldSound() {
    if (!g_worldVoice || !g_palette) return;
    UINT32 queued = QueuedWorldBuffers();
    {   // Follow the frame time: ~two frames of audio queued, never fewer than 3 buffers.
        double nowS = NowSeconds(), frame = g_worldLastPump > 0 ? nowS - g_worldLastPump : 1.0 / 60.0;
        g_worldLastPump = nowS;
        if (frame > 0.1) frame = 0.1;
        int want = (int)ceil(2.0 * frame * MUSIC_SAMPLE_RATE / WORLD_BUFFER_SAMPLES) + 1;
        g_worldQueue = want < WORLD_QUEUE_MIN ? WORLD_QUEUE_MIN : want > WORLD_QUEUE_MAX ? WORLD_QUEUE_MAX : want;
    }
    if (g_worldIdle && g_palette->Silent()) return; // nothing to say: render nothing
    bool running;
    { std::lock_guard<std::mutex> lk(g_musicLock); running = g_nextChunkStartTime >= 0.0; }
    double t = AudibleMusicTime() + (double)queued * WORLD_BUFFER_SAMPLES / MUSIC_SAMPLE_RATE;
    float buf[WORLD_BUFFER_SAMPLES * 2];
    while (queued < (UINT32)g_worldQueue) {
        g_palette->RenderStereo(buf, WORLD_BUFFER_SAMPLES, t, running);
        int16_t* out = g_worldPool[g_worldPoolNext];
        g_worldPoolNext = (g_worldPoolNext + 1) % WORLD_POOL;
        for (int i = 0; i < WORLD_BUFFER_SAMPLES * 2; i++) {
            float v = buf[i] > 1.0f ? 1.0f : buf[i] < -1.0f ? -1.0f : buf[i];
            out[i] = (int16_t)(v * 32767.0f);
        }
        XAUDIO2_BUFFER xb = {};
        xb.AudioBytes = WORLD_BUFFER_SAMPLES * 2 * sizeof(int16_t);
        xb.pAudioData = (const BYTE*)out;
        g_worldVoice->SubmitSourceBuffer(&xb);
        queued++;
        if (running) t += (double)WORLD_BUFFER_SAMPLES / MUSIC_SAMPLE_RATE;
    }
    g_worldIdle = g_palette->Silent();
}

void PlayWorldSound(const SoundCue& cue) {
    if (!g_palette) return;
    g_palette->Play(cue);
    g_worldIdle = false;
    PumpWorldSound(); // start it now, not next frame
}
void ReleaseWorldSound(SoundId id) { if (g_palette) g_palette->Release(id); }
void SetWorldGait(int gait, SoundMaterial ground) { if (g_palette) g_palette->SetGait(gait, ground); }
void FadeWorldSounds(float seconds) { if (g_palette) { g_palette->FadeOut(seconds); g_palette->SetAmbientEnabled(false); } }

void UpdateWorldSound(const SoundAxes& axes, const AmbientScene& scene, bool playing, const float listener[4]) {
    bool musicRunning;
    {
        std::lock_guard<std::mutex> lk(g_musicLock);
        g_musicColour.positive = axes.positive;
        g_musicColour.activity = axes.activity;
        g_musicColour.mechanical = axes.mechanical;
        musicRunning = g_nextChunkStartTime >= 0.0;
    }
    if (!g_palette) return;
    g_palette->SetAxes(axes);
    g_palette->SetScene(scene);
    g_palette->SetIntensity(g_musicIntensity);
    g_palette->SetListener(listener[0], listener[1], listener[2], listener[3]);
    g_palette->SetMono(g_monoAudio);
    bool live = playing && musicRunning;
    g_palette->SetAmbientEnabled(live);
    if (live) g_worldIdle = false; // the scheduler may place something this bar
    PumpWorldSound();
}
