// sfx_synth.h
//
// The world sound palette (docs/SOUND_PALETTE.md; DESIGN.md Part X.4):
// short interaction, discovery, accent, texture and rare-colour sounds,
// synthesized with the music's own primitives (synth_kit.h) and written as
// parts of the same composition. Every pitch comes from the chord the
// track is playing at that moment (MusicHarmonyAt), every scheduled onset
// lands on its beat grid, and three slow axes -- positive/negative,
// calm/active, organic/mechanical -- shape the character predictably.
//
// Pure C++, no OS or audio dependency, deterministic: tested natively and
// rendered offline (tests/, tools/sound_demo.cpp). audio.cpp plays it on a
// second, low-latency voice beside the music.

#pragma once

#include "music_synth.h"
#include <cstdint>

// The soundscape axes (docs/SOUND_PALETTE.md 3), already smoothed by the
// caller (soundscape.h). Defaults: the neutral point.
struct SoundAxes {
    float positive = 0.2f;    // -1 neglected / distressed .. +1 healthy / cared-for
    float activity = 0.3f;    // 0 calm .. 1 high activity
    float mechanical = 0.35f; // 0 organic .. 1 mechanical
};

// What a block sounds like when set, taken or walked on (the "material
// tint", docs/SOUND_PALETTE.md 5.2).
enum SoundMaterial : uint8_t {
    MAT_NONE, MAT_EARTH, MAT_STONE, MAT_WOOD, MAT_PLANT, MAT_GLASS, MAT_METAL, MAT_FLESH, MAT_GENESIS, MAT_COUNT
};

enum SoundId : uint8_t {
    // 5.1 vocal-style ad-libs
    SND_RISE, SND_HEY, SND_HUM, SND_BLOOM, SND_ANSWER, SND_SIGH,
    // 5.2 interaction and confirmation
    SND_SET, SND_TAKE, SND_SLOT, SND_LIBRARY_OPEN, SND_LIBRARY_CLOSE, SND_PICK, SND_DROP, SND_CANT, SND_LAND, SND_SLIDE,
    // 5.3 discovery
    SND_UNVEIL, SND_HORIZON, SND_GLINT, SND_VEIN, SND_TIMESLIP, SND_OMEN,
    // 5.4 rhythmic world accents
    SND_FOOTFALL, SND_WORKS, SND_DRIP, SND_EMBER, SND_CLAVE, SND_HEARTBEAT,
    // 5.5 living-world textures
    SND_WIND, SND_CHIRPS, SND_NIGHT_SHIMMER, SND_MUSHROOM, SND_WORKS_HUM, SND_WORN, SND_ENCLOSURE,
    // 5.6 progress
    SND_CADENCE, SND_MENDING, SND_ONLINE, SND_SEALED, SND_RIFT_CLOSED,
    // 5.7 rare colour
    SND_FAR_BELL, SND_GLIMMER, SND_FALLING_STARS, SND_MURMUR, SND_FAR_CALL, SND_SEAM,
    SND_COUNT
};

// 1 interaction (immediate) .. 5 rare colour (docs/SOUND_PALETTE.md 2, 4).
enum SoundTier : uint8_t { TIER_INTERACTION = 1, TIER_EVENT, TIER_ACCENT, TIER_TEXTURE, TIER_RARE };

const char* SoundName(SoundId id);
SoundTier SoundTierOf(SoundId id);

struct SoundCue {
    SoundId id = SND_SET;
    SoundMaterial material = MAT_NONE;
    float strength = 1.0f; // 0..1: a landing's speed, a slot drop that changed the slot, ...
    int slot = 0;          // hotbar slot 0..9 (Slot, Drop)
    uint32_t key = 0;      // identity of the source (a music block's position hash, a step's parity)
    int height = 0;        // a source's y (Clave's pitch)
    // Where it happens, if anywhere in particular: placed sounds pan toward
    // their source and soften with distance (SetListener).
    bool placed = false;
    float x = 0, y = 0, z = 0;
};

// What the ground around the player offers the ambient scheduler
// (soundscape.cpp fills it each frame). Presences are 0..1.
struct AmbientScene {
    float plants = 0, water = 0, ember = 0, glow = 0, machines = 0, dark = 0;
    int musicBlockCount = 0;         // nearest music blocks (up to 3)
    uint32_t musicBlockKey[3] = {};
    int musicBlockY[3] = {};
    float musicBlockX[3] = {}, musicBlockZ[3] = {}; // centres, for stereo placement
    bool enclosed = false;           // roofed / in a cave
    bool deep = false;               // well underground
    bool lookingUp = false;          // gazing at the sky
    float stillSeconds = 0;          // how long the player has stood still
    float negativeSeconds = 0;       // how long positive has stayed below -0.3
};

class SoundPalette {
public:
    SoundPalette();
    ~SoundPalette();
    SoundPalette(const SoundPalette&) = delete;
    SoundPalette& operator=(const SoundPalette&) = delete;

    void Reset();                          // silence, clear gestures and cooldowns
    void SetAxes(const SoundAxes& axes);
    void SetScene(const AmbientScene& scene);
    void SetIntensity(float intensity);    // the Music Intensity setting: scales tiers 3-5
    void SetAmbientEnabled(bool on);       // the scheduler runs only during play
    // The listener, for stereo placement: position and view yaw (radians,
    // as world.h's Player: forward = (sin yaw, cos yaw) on the ground).
    void SetListener(float x, float y, float z, float yaw);
    void SetMono(bool mono);               // Accessibility: everything centred
    // Footsteps on the beat (5.4 R1): 0 still / airborne / sliding, 1 crouch
    // (every other beat), 2 walk (every beat), 3 sprint (8ths), on `ground`.
    enum Gait { GAIT_NONE, GAIT_CROUCH, GAIT_WALK, GAIT_SPRINT };
    void SetGait(int gait, SoundMaterial ground);

    // Starts a sound (and whatever gesture it belongs to) at the current
    // render position, harmonised with the music at that moment.
    void Play(const SoundCue& cue);
    void Release(SoundId id);              // ends a sustained sound (Slide)
    void FadeOut(float seconds);           // pausing: every tail away, raised-cosine

    // Renders `n` mono samples (roughly -1..1, the music's scale) at 44.1
    // kHz. `musicTime` = the day time the first sample will be heard at;
    // `running` = the day clock (and so the music) is advancing.
    void Render(float* out, int n, double musicTime, bool running);   // mono downmix
    void RenderStereo(float* outLR, int frames, double musicTime, bool running); // interleaved L, R
    bool Silent() const;                   // nothing sounding, echo tail gone

    // Inspection, for the F3 overlay and the tests.
    struct NoteLog { SoundId id; double hz; int chord; bool anchor; int64_t onset; };
    int RecentNotes(NoteLog* out, int max) const; // newest last
    int ActiveVoices() const;
    int64_t Now() const;                   // samples rendered so far
    SoundAxes Axes() const;
    int ScheduledAmbientEvents() const;    // total the scheduler has placed
    int GestureIndex() const;              // the current ladder index (0 = D5)
    int PlayedCount(SoundId id) const;     // sounds of this kind started so far

    struct Impl;
private:
    Impl* m;
};
