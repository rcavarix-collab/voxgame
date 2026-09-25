// soundscape.cpp -- see soundscape.h and docs/SOUND_PALETTE.md 3.1.

#include "soundscape.h"
#include <cmath>
#include <cstring>

// (Voxistics' per-block material and class tables are left behind: the
// census source says what each cell is -- Cacophony seam.)

static inline float Presence(float count, float scale) { return 1.0f - expf(-count / scale); }
static inline float Clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
// Eases toward a target with time constant tau (seconds).
static inline float Ease(float v, float target, float tau, float dt) { return v + (target - v) * (1.0f - expf(-dt / tau)); }

void Soundscape::Reset() {
    *this = Soundscape();
}

void Soundscape::NoteInteraction() { interactions += 1.0f; }

void Soundscape::CensusStep(CensusSource get, int px, int py, int pz) {
    if (slab == 0) {
        ox = px - BOX_XZ / 2; oy = py - BOX_Y / 2; oz = pz - BOX_XZ / 2;
        cur = {};
        musicN = 0;
        memset(seenThisSweep, 0, sizeof(seenThisSweep));
        newThisSweep = 0;
    }
    // Top-down, so "exposed" (open to the air above) is one read per cell.
    int y = oy + BOX_Y - 1 - slab;
    { // Cacophony seam: the source answers "empty" outside the world (Voxistics: y > Y_MIN)
        for (int x = 0; x < BOX_XZ; x++)
            for (int z = 0; z < BOX_XZ; z++) {
                int wx = ox + x, wz = oz + z;
                CensusCell c = get(wx, y, wz);
                if (!c.filled) continue;
                int id = c.kind % CENSUS_KINDS;
                if (!seenThisSweep[id]) {
                    seenThisSweep[id] = true;
                    if (!seenBlock[id]) { seenBlock[id] = true; newThisSweep++; }
                }
                bool exposed = !get(wx, y + 1, wz).solid;
                switch (c.cls) {
                case SC_NATURAL: if (exposed) cur.natural++; break;
                case SC_MECHANICAL: cur.mechanical++; break;
                case SC_DARK: cur.dark++; break;
                case SC_GENESIS: cur.genesis++; break;
                default: break;
                }
                if (c.plant) cur.plants++;
                if (c.water) cur.water++;
                if (c.ember) cur.ember++;
                if (c.glow) cur.glow++;
                if (c.machine) cur.machines++;
                if (c.ore) cur.ore++;
                if (c.emissive) {
                    cur.emissive++;
                    // Glint: the first emissive thing of each kind this session.
                    if (!glinted[id]) { glinted[id] = true; glintPending = true; }
                }
            }
    }
    if (++slab >= BOX_Y) { slab = 0; EndSweep(); }
}

void Soundscape::EndSweep() {
    last = cur;
    sweeps++;
    scene.musicBlockCount = musicN;
    for (int k = 0; k < 3; k++) { scene.musicBlockKey[k] = musicKey[k]; scene.musicBlockY[k] = musicY[k]; scene.musicBlockX[k] = musicX[k]; scene.musicBlockZ[k] = musicZ[k]; }
    // Discoveries, one per sweep at most, most important first.
    bool grace = sessionSeconds < 20.0f; // the first look around is not "new"
    if (last.dark > 0) { if (sinceDark > 600.0f) Discover(SND_OMEN); sinceDark = 0; }
    else if (last.ore > 0 && sinceOre > 120.0f && !grace) { Discover(SND_VEIN); sinceOre = 0; }
    else if (!grace && newThisSweep >= 3) Discover(SND_HORIZON);
    if (last.ore > 0) sinceOre = 0;
    // Glint: the first emissive thing of each kind this session (noted in the census).
    if (glintPending) { glintPending = false; if (!grace) Discover(SND_GLINT); }
}

void Soundscape::ProbeSky(CensusSource get, int px, int py, int pz) {
    roof = false; rockAbove = 0;
    for (int dy = 2; dy <= 40; dy++) {
        if (get(px, py + dy, pz).solid) { rockAbove++; if (dy <= 12) roof = true; }
    }
}

void Soundscape::Update(const SoundscapeInput& in) {
    float dt = in.dt;
    sessionSeconds += dt;
    sinceDark += dt; sinceOre += dt;
    interactions *= expf(-dt / 8.0f);
    const Counts& c = last;
    float mechP = Presence((float)c.mechanical, 20.0f);
    float natP = Presence((float)c.natural, 150.0f);
    float plantP = Presence((float)c.plants, 20.0f);
    float genP = Presence((float)c.genesis, 15.0f);
    float darkP = Presence((float)c.dark, 15.0f);
    // docs/SOUND_PALETTE.md 3.1.
    float mTarget = Clampf(0.35f + 0.6f * mechP - 0.3f * natP * (1.0f - mechP), 0.0f, 1.0f);
    float pTarget = Clampf(0.1f + 0.2f * natP + 0.2f * plantP + 0.5f * genP - 1.3f * darkP, -1.0f, 1.0f);
    float move = in.sliding ? 1.0f : in.sprinting && in.speed > 1.0f ? 0.8f : in.speed > 0.5f ? 0.4f : 0.0f;
    static const float sectionEnergy[6] = { 0.25f, 0.6f, 1.0f, 0.6f, 0.25f, 0.0f };
    float energy = sectionEnergy[in.musicSection < 0 || in.musicSection > 5 ? 0 : in.musicSection];
    float aTarget = Clampf(0.12f + 0.35f * move + 0.3f * fminf(1.0f, interactions / 12.0f) + 0.12f * energy + 0.15f * mechP, 0.0f, 1.0f);
    if (!HaveCensus()) { mTarget = axes.mechanical; pTarget = axes.positive; }
    axes.mechanical = Ease(axes.mechanical, mTarget, 5.0f, dt);
    axes.positive = Ease(axes.positive, pTarget, 10.0f, dt);
    axes.activity = Ease(axes.activity, aTarget, aTarget > axes.activity ? 1.5f : 6.0f, dt);

    scene.plants = plantP;
    scene.water = Presence((float)c.water, 10.0f);
    scene.ember = Presence((float)c.ember, 4.0f);
    scene.glow = Presence((float)c.glow, 4.0f);
    scene.machines = Presence((float)c.machines, 12.0f);
    scene.dark = darkP;
    scene.enclosed = roof;
    scene.deep = rockAbove >= 12;
    lookUpSeconds = in.pitch > 0.6f && !roof ? lookUpSeconds + dt : 0.0f;
    scene.lookingUp = lookUpSeconds >= 4.0f;
    scene.stillSeconds = in.speed < 0.3f ? scene.stillSeconds + dt : 0.0f;
    scene.negativeSeconds = axes.positive < -0.3f ? scene.negativeSeconds + dt : 0.0f;
}

int Soundscape::TakeDiscoveries(SoundId* out, int max) {
    int n = pendingN < max ? pendingN : max;
    for (int i = 0; i < n; i++) out[i] = pending[i];
    pendingN = 0;
    return n;
}
