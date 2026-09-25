// game.cpp -- see game.h.

#include "game.h"
#include "debris.h"
#include "events.h"
#include "fire.h"
#include "input.h"
#include "mech.h"
#include "persist.h"
#include "pickups.h"
#include "profiler.h"
#include "props.h"
#include "render.h"
#include "sun.h"
#include "terrain.h"
#include "wanderer.h"
#include "weapons.h"
#include <cstdio>
#include <string>

namespace {
Terrain g_terrain;
Props g_props;
Fire g_fire;
Debris g_debris;
Pickups g_pickups;
Wanderer g_wanderer;
Mech g_mech;
MechTuning g_tune;
WeaponState g_weapons;
WeaponTuning g_weaponTune;
WandererTuning g_wandererTune;
FireTuning g_fireTune;
EventList g_events;
uint32_t g_seed = 1;
float g_dayTime = DAY_LENGTH_SECONDS * 0.30f; // start in the morning
bool g_paused = false;
float g_deadFor = 0;          // > 0 while the mech is wrecked, counting up to the respawn
int g_plates = 0;             // armour plates collected (kept, not used yet)
float g_shake = 0;            // view shake from nearby blasts, 0..1 (a jolt, never a flash)
float g_simTime = 0;
std::string g_toast;
float g_toastTime = 0;

// Fireballs: a glowing ball that swells and dies over most of a second.
struct Fireball { Vec3 pos; float radius, age; };
std::vector<Fireball> g_fireballs;
std::vector<MeshVertex> g_dynamic;  // this frame's dynamic meshes, reused
std::vector<Vec3> g_burningProps;   // reused
size_t g_handled = 0;               // events already turned into effects this tick

const float VIEW_RADIUS = 420.0f;   // terrain kept around the mech (the view is above the treeline)
const float PROP_RADIUS = 300.0f;   // props kept and drawn
const int MESH_BUDGET = 6;          // terrain chunk rebuilds per frame
const int PROP_MESH_BUDGET = 8;     // prop tile rebuilds per frame
const float RESPAWN_SECONDS = 4.0f;

void Toast(const std::string& s, float seconds = 5.0f) { g_toast = s; g_toastTime = seconds; }

World MakeWorld() {
    World w;
    w.terrain = &g_terrain; w.props = &g_props; w.fire = &g_fire; w.debris = &g_debris;
    w.wanderer = &g_wanderer; w.mech = &g_mech; w.events = &g_events;
    return w;
}

FrameView MakeView() {
    FrameView v;
    Vec3 eye = g_mech.Eye(g_tune);
    // Shake and recoil: small, smooth offsets that die away quickly.
    float s = g_shake * g_shake;
    eye = eye + Vec3{ sinf(g_simTime * 37.0f) * 0.25f * s, sinf(g_simTime * 29.0f) * 0.2f * s, 0 };
    float pitch = g_mech.pitch + g_weapons.recoil * 0.012f;
    v.eye = eye;
    v.forward = { sinf(g_mech.yaw) * cosf(pitch), sinf(pitch), cosf(g_mech.yaw) * cosf(pitch) };
    v.right = Normalize(Cross(kUp, v.forward));
    v.up = Cross(v.forward, v.right);
    float aspect = (float)g_screenW / (float)(g_screenH > 0 ? g_screenH : 1);
    float fovY = g_fov * kPi / 180.0f;
    v.tanHalfFovY = tanf(fovY * 0.5f);
    v.tanHalfFovX = v.tanHalfFovY * aspect;
    v.view = MatLookToLH(v.eye, v.forward, kUp);
    v.proj = MatPerspectiveFovLH(fovY, aspect, 0.5f, 1600.0f);
    v.dayTime = g_dayTime;
    return v;
}

// ---- what happened this tick turns into debris, drops, fireballs ----
// Picks up where the last call stopped, so calling it twice in a tick
// never turns the same event into effects twice.
void HandleEvents() {
    World w = MakeWorld();
    for (; g_handled < g_events.list.size(); g_handled++) {
        Event e = g_events.list[g_handled]; // a copy: handlers may add events
        switch (e.kind) {
        case EV_BLAST: {
            if (g_fireballs.size() < 32) g_fireballs.push_back({ e.pos, e.strength, 0 });
            float d = Length(e.pos - g_mech.Eye(g_tune));
            g_shake = std::fmin(1.0f, g_shake + Clamp(1.0f - d / 120.0f, 0.0f, 1.0f) * 0.8f);
            break;
        }
        case EV_PROP_SHATTERED:
            if (e.detail == PROP_ROCK) g_debris.Burst(e.pos, 14, 12.0f, 0.7f, Rgba(0.13f, 0.125f, 0.12f));
            else if (e.detail == PROP_BUSH) g_debris.Burst(e.pos, 10, 7.0f, 0.4f, Rgba(0.055f, 0.095f, 0.03f));
            else g_debris.Burst(e.pos, 16, 10.0f, 0.6f, Rgba(0.075f, 0.05f, 0.032f)); // splinters
            break;
        case EV_TREE_LANDED:
            g_debris.Burst(e.pos, 8, 5.0f, 0.4f, Rgba(0.04f, 0.08f, 0.035f)); // needles and leaves
            break;
        case EV_TARGET_STAGE:
            // A plate breaks away: armour you can collect.
            g_pickups.Drop(e.pos, { (e.pos.x - g_wanderer.Center().x) * 2.0f, 6.0f, 2.0f }, PICK_PLATE, (uint32_t)(g_simTime * 1000));
            g_debris.Burst(e.pos, 10, 9.0f, 0.5f, Rgba(0.10f, 0.095f, 0.085f));
            break;
        case EV_TARGET_DESTROYED:
            Explode(w, e.pos, 9.0f);
            for (int k = 0; k < 2; k++)
                g_pickups.Drop(e.pos + Vec3{ k ? 2.0f : -2.0f, 2, 0 }, { k ? 5.0f : -5.0f, 9.0f, 1.0f }, PICK_PLATE, (uint32_t)(g_simTime * 1000) + k);
            g_debris.Burst(e.pos, 40, 18.0f, 0.9f, Rgba(0.10f, 0.095f, 0.085f));
            break;
        default: break;
        }
    }
    g_mech.health -= w.mechDamage;
}

// ---- dynamic meshes for this frame ----
void BuildDynamic() {
    g_dynamic.clear();
    g_debris.Mesh(g_dynamic);
    g_pickups.Mesh(g_dynamic);
    MeshWanderer(g_wanderer, g_dynamic);
    for (const Rocket& r : g_weapons.rockets) {
        Pose p;
        p.pos = r.pos;
        Vec3 d = Normalize(r.vel);
        p.yaw = atan2f(d.x, d.z);
        p.tilt = acosf(Clamp(d.y, -1.0f, 1.0f)); p.tiltYaw = p.yaw; // point the body along its flight
        PrimPrism(g_dynamic, p, 0.35f, 2.4f, 6, Rgba(0.10f, 0.10f, 0.09f));
        Pose tail = p; tail.pos = r.pos - d * 0.4f;
        PrimCone(g_dynamic, tail, -1.6f, 0.4f, 1.2f, 6, Rgba(1.0f, 0.6f, 0.2f, true)); // the motor's glow
    }
    for (const Tracer& t : g_weapons.tracers) {
        // A short glowing streak travelling out along the shot.
        Vec3 d = t.b - t.a;
        float len = Length(d);
        if (len < 1) continue;
        Vec3 dir = d * (1.0f / len);
        float head = std::fmin(len, (t.age / 0.06f) * len + 20.0f);
        Vec3 a = t.a + dir * std::fmax(0.0f, head - 18.0f), b = t.a + dir * head;
        Pose p; p.pos = a; p.yaw = atan2f(dir.x, dir.z); p.tilt = acosf(Clamp(dir.y, -1.0f, 1.0f)); p.tiltYaw = p.yaw;
        PrimPrism(g_dynamic, p, 0.08f, Length(b - a), 4, Rgba(1.0f, 0.8f, 0.45f, true));
    }
    // Flames: steady-coloured cones that sway in height (slowly: no flicker).
    for (const FireCell& c : g_fire.Cells()) {
        float x = (c.x + 0.5f) * g_fireTune.cell, z = (c.z + 0.5f) * g_fireTune.cell;
        float grow = Clamp(c.age / 1.0f, 0.0f, 1.0f) * Clamp((c.life - c.age) / 1.5f, 0.0f, 1.0f);
        float h = (1.0f + 0.35f * sinf(g_simTime * 7.0f + (float)(c.x * 3 + c.z * 7))) * grow;
        if (h < 0.05f) continue;
        Pose p; p.pos = { x, g_terrain.OriginalHeight(x, z), z }; p.yaw = (float)(c.x * 13 + c.z * 5);
        PrimCone(g_dynamic, p, 0.0f, 0.9f, 1.6f * h, 5, Rgba(1.0f, 0.42f, 0.08f, true));
    }
    g_burningProps.clear();
    g_props.BurningPositions(g_burningProps);
    for (size_t i = 0; i < g_burningProps.size() && i < 200; i++) {
        Pose p; p.pos = g_burningProps[i]; p.yaw = (float)i;
        float h = 2.0f + 0.5f * sinf(g_simTime * 6.0f + (float)i);
        PrimCone(g_dynamic, p, 0.0f, 1.8f, h * 1.8f, 6, Rgba(1.0f, 0.38f, 0.07f, true));
    }
    for (const Fireball& f : g_fireballs) {
        // Swells fast, then shrinks and darkens: a bloom of heat, not a flash.
        float t = f.age / 0.8f;
        float r = f.radius * (t < 0.25f ? t / 0.25f : 1.0f - (t - 0.25f) * 0.9f);
        float heat = 1.0f - t;
        Pose p; p.pos = f.pos + Vec3{ 0, f.radius * 0.3f + t * 4.0f, 0 };
        PrimRock(g_dynamic, p, r, 0.85f, (uint32_t)(f.pos.x * 7 + f.pos.z * 3), Rgba(1.0f * heat + 0.1f, 0.5f * heat * heat + 0.05f, 0.12f * heat, true));
    }
}

void DrawOverlay() {
    const ProfReport& r = ProfGetReport();
    TerrainStats ts = g_terrain.Stats();
    char buf[2560];
    int n = snprintf(buf, sizeof buf, "FRAME %.2f MS (MAX %.2f)  WORK %.2f MS (MAX %.2f)\n%s\n",
                     r.frameAvgMs, r.frameMaxMs, r.workAvgMs, r.workMaxMs, ProfBootSummary(false).c_str());
    for (int s = 0; s < PROF_COUNT && n < (int)sizeof buf - 64; s++)
        n += snprintf(buf + n, sizeof buf - n, "%-11s %6.2f  %6.2f\n", ProfSectionName((ProfSection)s), r.avgMs[s], r.maxMs[s]);
    int hour = (int)(g_dayTime / DAY_LENGTH_SECONDS * 24.0f), minute = (int)(fmodf(g_dayTime / DAY_LENGTH_SECONDS * 24.0f, 1.0f) * 60.0f);
    snprintf(buf + n, sizeof buf - n,
             "CHUNKS %d (%d CHANGED)  DRAWN %lld  TRIS %lld  WAITING %d\n"
             "PROPS %d  DEBRIS %d  FIRE %d (BURNT %d)  PICKUPS %d\n"
             "MECH %.0f %.0f %.0f  SPEED %.1f  %s%s\n"
             "ENERGY %.2f  SUN %.2f  HEALTH %.2f  SHIELD %s  PLATES %d\n"
             "WEAPON %s  LOCK %s  WANDERER %s %.2f\n"
             "DAY %02d:%02d\n"
             "F3 OVERLAY  CTRL+F3 REPORT  R RESET  T +1H GAME TIME",
             ts.resident, ts.modified, (long long)r.counters[PCOUNT_CHUNKS_DRAWN], (long long)r.counters[PCOUNT_TRIANGLES_DRAWN], ts.waiting,
             g_props.Count(), g_debris.Live(), (int)g_fire.Cells().size(), g_fire.Burnt(), g_pickups.Count(),
             g_mech.pos.x, g_mech.pos.y, g_mech.pos.z, Length(Vec3{ g_mech.vel.x, 0, g_mech.vel.z }),
             g_mech.onGround ? "GROUND" : "AIR", g_mech.boosting ? " BOOST" : "",
             g_mech.energy, g_mech.sun, g_mech.health, g_mech.shield ? "UP" : "DOWN", g_plates,
             g_weapons.current == WPN_ROCKETS ? "ROCKETS" : "GUN", g_weapons.locked ? "LOCKED" : (g_weapons.lockMode ? "SEEKING" : "OFF"),
             g_wanderer.alive ? "ALIVE" : "DOWN", g_wanderer.health, hour, minute);
    float w = 0, line = UILineHeight();
    int lines = 1;
    { float lw = 0; for (const char* p = buf; *p; p++) { if (*p == '\n') { lines++; w = lw > w ? lw : w; lw = 0; } else lw += 8; } w = lw > w ? lw : w; }
    UIRect(6, 6, w + 12, lines * line + 8, 0x000000A0);
    UIText(12, 10, buf, 0xE8E8E8FF);
}

// The HUD: a projected gunsight on the glass and a few wordless bands
// (DESIGN.md §7). The cockpit's real dials come in their own step.
void DrawHud(const FrameView& v) {
    float cx = g_screenW * 0.5f, cy = g_screenH * 0.5f;
    const uint32_t sight = 0xFFC87AB0;
    if (g_weapons.current == WPN_ROCKETS) {
        UIRing(cx, cy, 20.0f, 1.5f, sight);
        for (int i = 0; i < 4; i++) {
            float a = i * kPi * 0.5f;
            UIRect(cx + cosf(a) * 26 - 3, cy + sinf(a) * 26 - 3, 6, 6, sight);
        }
    } else {
        UIRing(cx, cy, 7.0f, 1.5f, sight);
        UIRect(cx - 1, cy - 1, 2, 2, sight);
    }
    if (g_weapons.lockMode) UIRing(cx, cy, 90.0f, 1.0f, 0xFFC87A50, 64); // the seeker's field
    // Lock brackets around the wanderer: closing in while locking, solid when locked.
    float sx, sy;
    if (g_weapons.lockMode && g_wanderer.alive && ProjectToScreen(v, g_wanderer.Center(), sx, sy)) {
        float k = g_weapons.locked ? 1.0f : g_weapons.lockProgress;
        float half = 70.0f - 40.0f * k;
        uint32_t c = g_weapons.locked ? 0xFF5A3CE0 : 0xFFC87A90;
        float len = 14, th = 2;
        for (int corner = 0; corner < 4; corner++) {
            float dx = (corner & 1) ? half : -half, dy = (corner & 2) ? half : -half;
            float ox = sx + dx, oy = sy + dy;
            UIRect(dx < 0 ? ox : ox - len, oy - th * 0.5f, len, th, c);
            UIRect(ox - th * 0.5f, dy < 0 ? oy : oy - len, th, len, c);
        }
    }
    // Bands, bottom left: energy (pale blue with the shield up), sun, health.
    float bw = 200, x = 24, y = (float)g_screenH - 56;
    UIRect(x - 2, y - 2, bw + 4, 10, 0x00000080);
    UIRect(x, y, bw * g_mech.energy, 6, g_mech.shield ? 0x9FD4FFE0 : 0xFFB45AE0);
    UIRect(x - 2, y + 12 - 2, bw + 4, 8, 0x00000080);
    UIRect(x, y + 12, bw * g_mech.sun, 4, 0xFFF0B0C0);
    UIRect(x - 2, y + 22 - 2, bw + 4, 10, 0x00000080);
    UIRect(x, y + 22, bw * Clamp(g_mech.health, 0.0f, 1.0f), 6, 0xD04A3AE0);
    // Plates collected: a row of small plates, no numbers.
    for (int i = 0; i < g_plates && i < 24; i++) UIRect(x + i * 9.0f, y - 16, 7, 9, 0xA0784AE0);
    if (g_deadFor > 0) UIRect(0, 0, (float)g_screenW, (float)g_screenH, (uint32_t)(0x10080600 | (uint32_t)(Clamp(g_deadFor / 1.5f, 0.0f, 0.8f) * 255)));
}
} // namespace

void GameInit(uint32_t seed) {
    g_seed = seed;
    g_terrain.Reset(seed);
    g_props.Reset(seed + 1);
    g_fire.Reset(seed + 2);
    g_debris.Reset(seed + 3);
    g_pickups.Reset();
    g_events.Clear();
    g_fireballs.clear();
    g_weapons = WeaponState();
    g_mech = Mech();
    PlaceMech(g_mech, g_terrain, 0, 0);
    g_wanderer.rng = seed | 1;
    SpawnWanderer(g_wanderer, g_terrain, g_mech.pos, 110.0f);
    g_deadFor = 0;
    g_plates = 0;
    g_terrain.Update(g_mech.pos, VIEW_RADIUS);
    g_props.Update(g_terrain, g_mech.pos, PROP_RADIUS);
}

bool GamePaused() { return g_paused; }
void GameSetPaused(bool p) { g_paused = p; InputReleaseAll(); }
float GameDayTime() { return g_dayTime; }

void GameDebugKey(int vk, bool ctrl) {
    if (vk == VK_F3_CODE) {
        if (ctrl) {
            ProfStartCapture(10.0f, "Cacophony performance report\n\n");
            Toast("RECORDING 10 SECONDS...", 10.5f);
        } else {
            g_showProfiler = !g_showProfiler;
            SaveSettings();
        }
    } else if (vk == 'R' && !g_paused) {
        GameInit(g_seed);
        Toast("FIELD RESET", 2.0f);
    } else if (vk == 'T' && !g_paused) {
        g_dayTime = fmodf(g_dayTime + DAY_LENGTH_SECONDS / 24.0f, DAY_LENGTH_SECONDS);
    }
}

void GameTick(float dt) {
    ProfScope prof(PROF_SIM);
    g_events.Clear();
    g_handled = 0;
    g_simTime += dt;
    g_shake = std::fmax(0.0f, g_shake - dt * 2.5f);
    float mx, my;
    TakeMouse(mx, my);
    bool alive = g_deadFor <= 0;
    MechInput in;
    WeaponInput win;
    if (alive) {
        const float k = 0.0022f * g_sensitivity;
        g_mech.yaw += mx * k;
        g_mech.pitch = Clamp(g_mech.pitch + (g_invertY ? my : -my) * k, -1.2f, 1.2f);
        in.forward = (ActionHeld(ACT_FORWARD) ? 1.0f : 0.0f) - (ActionHeld(ACT_BACK) ? 1.0f : 0.0f);
        in.strafe = (ActionHeld(ACT_RIGHT) ? 1.0f : 0.0f) - (ActionHeld(ACT_LEFT) ? 1.0f : 0.0f);
        in.jump = TakePress(ACT_JUMP);
        in.boost = ActionHeld(ACT_BOOST);
        in.toggleShield = TakePress(ACT_SHIELD);
        win.fireHeld = ActionHeld(ACT_FIRE);
        win.switchPressed = TakePress(ACT_NEXT_WEAPON);
        win.wheel = TakeWheel();
        win.lockPressed = TakePress(ACT_LOCK);
    } else {
        // Wrecked: controls are dead until the respawn.
        TakePress(ACT_JUMP); TakePress(ACT_SHIELD); TakePress(ACT_NEXT_WEAPON); TakePress(ACT_LOCK); TakeWheel();
    }

    Vec3 panels[4];
    g_mech.PanelPoints(g_tune, panels);
    float sun = SunExposure(g_terrain, panels, 4, g_dayTime);
    int steps = g_mech.footfalls;
    TickMech(g_mech, g_tune, in, g_terrain, alive ? sun : 0.0f, dt);
    if (g_mech.footfalls != steps) g_events.Add(EV_FOOTFALL, g_mech.pos, 1.0f, g_terrain.GroundAt(g_mech.pos + Vec3{ 0, 1, 0 }));
    if (alive) g_props.Trample(g_mech.pos, 3.2f, g_mech.vel, g_events);

    TickWanderer(g_wanderer, g_wandererTune, g_terrain, dt, g_events);
    if (g_wanderer.alive) {
        Vec3 f = { sinf(g_wanderer.yaw), 0, cosf(g_wanderer.yaw) };
        g_props.Trample(WandererFeet(g_wanderer), 2.6f, f * g_wandererTune.speed, g_events);
    } else if ((g_wanderer.respawnIn -= dt) <= 0) {
        SpawnWanderer(g_wanderer, g_terrain, g_mech.pos, 150.0f);
    }

    FrameView v = MakeView();
    World w = MakeWorld();
    if (alive) TickWeapons(g_weapons, g_weaponTune, win, v.eye, v.forward, v.right, w, dt);
    else { WeaponInput none; TickWeapons(g_weapons, g_weaponTune, none, v.eye, v.forward, v.right, w, dt); }
    g_mech.health -= w.mechDamage;

    g_props.Tick(dt, g_events);
    g_fire.Tick(g_terrain, g_props, g_fireTune, dt, g_events);
    float debrisHit = g_debris.Tick(g_terrain, dt, g_mech.pos, g_tune.eyeHeight, 3.5f);
    if (alive) {
        g_mech.health -= debrisHit * (g_mech.shield ? 0.1f : 1.0f);
        if (g_fire.BurningAt(g_mech.pos) && !g_mech.shield) g_mech.health -= 0.02f * dt; // standing in fire scorches the legs
        g_plates += g_pickups.Tick(g_terrain, dt, g_mech.pos, 7.0f, PICK_PLATE, g_events);
    } else g_pickups.Tick(g_terrain, dt, { 1e9f, 0, 1e9f }, 0, PICK_PLATE, g_events);

    HandleEvents();

    // The mech's own end: it explodes where it stands, then walks in again.
    if (alive && g_mech.health <= 0) {
        g_mech.health = 0;
        g_mech.shield = false;
        World ww = MakeWorld();
        Explode(ww, g_mech.pos + Vec3{ 0, 6, 0 }, 9.0f, true);
        g_events.Add(EV_MECH_DESTROYED, g_mech.pos);
        HandleEvents();
        g_deadFor = 0.0001f;
    } else if (!alive) {
        g_deadFor += dt;
        if (g_deadFor >= RESPAWN_SECONDS) {
            float x = g_mech.pos.x - 40.0f, z = g_mech.pos.z - 40.0f;
            float yaw = g_mech.yaw;
            g_mech = Mech();
            PlaceMech(g_mech, g_terrain, x, z);
            g_mech.yaw = yaw;
            g_deadFor = 0;
        }
    }
    for (size_t i = 0; i < g_fireballs.size();) {
        g_fireballs[i].age += dt;
        if (g_fireballs[i].age > 0.8f) { g_fireballs[i] = g_fireballs.back(); g_fireballs.pop_back(); continue; }
        i++;
    }
    g_dayTime = fmodf(g_dayTime + dt, DAY_LENGTH_SECONDS);
}

void GameFrame(float frameSeconds) {
    if (g_toastTime > 0) g_toastTime -= frameSeconds;
    {
        ProfScope prof(PROF_TERRAIN);
        g_terrain.Update(g_mech.pos, VIEW_RADIUS);
        g_props.Update(g_terrain, g_mech.pos, PROP_RADIUS);
    }
    {
        ProfScope prof(PROF_MESH);
        int built = g_terrain.BuildMeshes(MESH_BUDGET);
        built += g_props.BuildMeshes(PROP_MESH_BUDGET);
        SyncTerrain(g_terrain);
        SyncProps(g_props);
        ProfAddCounter(PCOUNT_MESHES_BUILT, built);
    }
    TerrainStats ts = g_terrain.Stats();
    ProfSetCounter(PCOUNT_CHUNKS_RESIDENT, ts.resident);
    ProfSetCounter(PCOUNT_MESH_WAITING, ts.waiting);
    std::string report;
    if (ProfTakeCaptureReport(report)) {
        std::string path = WriteTextFile("perf_report.txt", report);
        Toast(path.empty() ? "COULD NOT WRITE THE REPORT" : "REPORT SAVED: " + path, 10.0f);
    }
}

void GameRender() {
    FrameView v = MakeView();
    GpuFrameBegin();
    RenderWorld(g_terrain, v);
    BuildDynamic();
    RenderMeshes(v, g_dynamic);
    GpuMarkWorldDone();
    UIBegin();
    DrawHud(v);
    if (g_showProfiler) DrawOverlay();
    if (g_toastTime > 0 && !g_toast.empty()) {
        float w = UITextWidth(g_toast.c_str());
        UIRect(g_screenW * 0.5f - w * 0.5f - 8, 40, w + 16, UILineHeight() + 8, 0x000000B0);
        UIText(g_screenW * 0.5f - w * 0.5f, 44, g_toast.c_str(), 0xFFFFFFFF);
    }
    if (g_paused) {
        // The pause screen is a menu: a few plain words are allowed here.
        UIRect(0, 0, (float)g_screenW, (float)g_screenH, 0x00000070);
        const char* msg = "PAUSED - CLICK OR ESC TO RETURN";
        float w = UITextWidth(msg);
        UIText(g_screenW * 0.5f - w * 0.5f, g_screenH * 0.5f - 9, msg, 0xFFFFFFFF);
    }
    UIEnd();
    GpuFrameEnd();
}
