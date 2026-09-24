// worldsound.cpp -- see worldsound.h.

#include "worldsound.h"
#include "audio.h"
#include "soundscape.h"
#include "music_synth.h"
#include "theline.h"
#include "world.h"
#include <cmath>

static Soundscape g_soundscape;
static bool g_placedThisSession[BLOCK_COUNT] = {};

// Movement state from the previous tick.
static bool g_wasOnGround = true, g_wasSliding = false, g_wasSprinting = false;
static float g_prevVelY = 0.0f;
static float g_lastX = 0.0f, g_lastZ = 0.0f;
static bool g_haveLast = false;
static float g_stride = 0.0f;
static uint32_t g_steps = 0;
static float g_speed = 0.0f;           // smoothed horizontal speed
static bool g_lineNear = false;
static float g_lineCooldown = 0.0f;

static SoundMaterial MaterialUnderFeet() {
    const Player& p = g_player;
    BlockID id = g_world.Get((int)floorf(p.x), (int)floorf(p.y - 0.05f), (int)floorf(p.z));
    return BlockSoundMaterial(id);
}

void WorldSoundReset() {
    g_soundscape.Reset();
    for (bool& b : g_placedThisSession) b = false;
    g_wasOnGround = true; g_wasSliding = g_wasSprinting = false;
    g_prevVelY = 0; g_haveLast = false; g_stride = 0; g_speed = 0;
    g_lineNear = false; g_lineCooldown = 0;
}

void WorldSoundTick(float dt) {
    const Player& p = g_player;
    float dx = 0, dz = 0;
    if (g_haveLast) { dx = p.x - g_lastX; dz = p.z - g_lastZ; }
    g_lastX = p.x; g_lastZ = p.z; g_haveLast = true;
    float moved = sqrtf(dx * dx + dz * dz);
    if (moved > 2.0f) moved = 0; // a teleport (load, unstick), not a step
    g_speed += (moved / dt - g_speed) * (1.0f - expf(-dt / 0.2f));

    bool sliding = PlayerSliding(p);
    // Landing: the impact speed is last tick's fall speed.
    if (p.onGround && !g_wasOnGround && g_prevVelY < -4.0f) {
        SoundCue c; c.id = SND_LAND; c.material = MaterialUnderFeet();
        c.strength = fminf(1.0f, (-g_prevVelY - 4.0f) / 16.0f);
        PlayWorldSound(c);
        g_stride = 0;
    }
    if (sliding && !g_wasSliding) {
        SoundCue c; c.id = SND_SLIDE; PlayWorldSound(c);
        SoundCue h; h.id = SND_HEY; PlayWorldSound(h); // only when activity is high (5.1 V2)
    }
    if (!sliding && g_wasSliding) ReleaseWorldSound(SND_SLIDE);
    bool sprinting = p.sprinting && g_speed > 5.0f;
    if (sprinting && !g_wasSprinting) { SoundCue h; h.id = SND_HEY; PlayWorldSound(h); }
    // Footfalls: one per stride on the ground (not while sliding).
    if (p.onGround && !sliding && moved > 0) {
        float stride = p.crouching ? 1.2f : p.sprinting ? 2.6f : 2.2f;
        g_stride += moved;
        if (g_stride >= stride) {
            g_stride -= stride;
            SoundCue c; c.id = SND_FOOTFALL; c.material = MaterialUnderFeet(); c.key = g_steps++;
            PlayWorldSound(c);
        }
    }
    // The Line passing through the player's cell (Timeslip).
    g_lineCooldown -= dt;
    bool near = g_line.distance < 0.8f && fabsf(p.y + 0.5f - g_line.lineY) < 2.5f;
    if (near && !g_lineNear && g_lineCooldown <= 0) {
        SoundCue c; c.id = SND_TIMESLIP; PlayWorldSound(c);
        g_lineCooldown = 60.0f;
    }
    g_lineNear = near;

    g_wasOnGround = p.onGround; g_wasSliding = sliding; g_wasSprinting = sprinting;
    g_prevVelY = p.velY;
}

void WorldSoundFrame(float dt, bool playing) {
    const Player& p = g_player;
    if (playing) {
        int px = (int)floorf(p.x), py = (int)floorf(p.y), pz = (int)floorf(p.z);
        g_soundscape.CensusStep(g_world, px, py, pz);
        g_soundscape.ProbeSky(g_world, px, py + 1, pz);
        MusicHarmony h;
        MusicHarmonyAt(g_dayTimeSeconds, &h);
        SoundscapeInput in;
        in.dt = dt;
        in.speed = g_speed;
        in.sprinting = p.sprinting;
        in.sliding = PlayerSliding(p);
        in.pitch = p.pitch;
        in.musicSection = h.section;
        g_soundscape.Update(in);
        SoundId found[8];
        int n = g_soundscape.TakeDiscoveries(found, 8);
        for (int i = 0; i < n; i++) { SoundCue c; c.id = found[i]; PlayWorldSound(c); }
    }
    UpdateWorldSound(g_soundscape.Axes(), g_soundscape.Scene(), playing);
}

void WorldSoundPlace(BlockID id) {
    g_soundscape.NoteInteraction();
    SoundCue c; c.id = SND_SET; c.material = BlockSoundMaterial(id);
    PlayWorldSound(c);
    if (id < BLOCK_COUNT && !g_placedThisSession[id]) {
        g_placedThisSession[id] = true;
        SoundCue u; u.id = SND_UNVEIL; PlayWorldSound(u);
    }
}

void WorldSoundBreak(BlockID id) {
    g_soundscape.NoteInteraction();
    SoundCue c; c.id = SND_TAKE; c.material = BlockSoundMaterial(id);
    PlayWorldSound(c);
}

void WorldSoundCue(SoundId id, int slot, float strength) {
    SoundCue c; c.id = id; c.slot = slot; c.strength = strength;
    PlayWorldSound(c);
}

SoundAxes WorldSoundAxes() { return g_soundscape.Axes(); }
