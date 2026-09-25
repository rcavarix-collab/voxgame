// game.cpp -- see game.h.

#include "game.h"
#include "terrain.h"
#include "mech.h"
#include "sun.h"
#include "input.h"
#include "render.h"
#include "persist.h"
#include "profiler.h"
#include <cstdio>
#include <string>

namespace {
Terrain g_terrain;
Mech g_mech;
MechTuning g_tune;
uint32_t g_seed = 1;
float g_dayTime = DAY_LENGTH_SECONDS * 0.30f; // start in the morning
bool g_paused = false;
std::string g_toast;
float g_toastTime = 0;
int g_blasts = 0;

const float VIEW_RADIUS = 420.0f;   // metres of terrain kept around the mech (the view is above the treeline)
const int MESH_BUDGET = 6;          // chunk rebuilds per frame: a blast's few chunks land within a frame
const float BLAST_RADIUS = 5.5f;    // the placeholder test blast (rockets come with the weapons step)

void Toast(const std::string& s, float seconds = 5.0f) { g_toast = s; g_toastTime = seconds; }

FrameView MakeView() {
    FrameView v;
    v.eye = g_mech.Eye(g_tune);
    v.forward = g_mech.Forward();
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

void DrawOverlay() {
    const ProfReport& r = ProfGetReport();
    TerrainStats ts = g_terrain.Stats();
    char buf[2048];
    int n = snprintf(buf, sizeof buf,
        "FRAME %.2f MS (MAX %.2f)  WORK %.2f MS (MAX %.2f)\n"
        "%s\n",
        r.frameAvgMs, r.frameMaxMs, r.workAvgMs, r.workMaxMs, ProfBootSummary(false).c_str());
    for (int s = 0; s < PROF_COUNT && n < (int)sizeof buf - 64; s++)
        n += snprintf(buf + n, sizeof buf - n, "%-11s %6.2f  %6.2f\n", ProfSectionName((ProfSection)s), r.avgMs[s], r.maxMs[s]);
    int hour = (int)(g_dayTime / DAY_LENGTH_SECONDS * 24.0f), minute = (int)(fmodf(g_dayTime / DAY_LENGTH_SECONDS * 24.0f, 1.0f) * 60.0f);
    snprintf(buf + n, sizeof buf - n,
        "CHUNKS %d (%d CHANGED)  DRAWN %lld  TRIS %lld  WAITING %d\n"
        "MECH %.0f %.0f %.0f  SPEED %.1f  %s%s\n"
        "ENERGY %.2f  SUN %.2f  SHIELD %s\n"
        "DAY %02d:%02d  BLASTS %d\n"
        "F3 OVERLAY  CTRL+F3 REPORT  R RESET  T +1H GAME TIME",
        ts.resident, ts.modified, (long long)r.counters[PCOUNT_CHUNKS_DRAWN], (long long)r.counters[PCOUNT_TRIANGLES_DRAWN], ts.waiting,
        g_mech.pos.x, g_mech.pos.y, g_mech.pos.z, Length(Vec3{ g_mech.vel.x, 0, g_mech.vel.z }),
        g_mech.onGround ? "GROUND" : "AIR", g_mech.boosting ? " BOOST" : "",
        g_mech.energy, g_mech.sun, g_mech.shield ? "UP" : "DOWN", hour, minute, g_blasts);
    float w = 0, h = 0, line = UILineHeight();
    { float lw = 0; int lines = 1; for (const char* p = buf; *p; p++) { if (*p == '\n') { lines++; w = lw > w ? lw : w; lw = 0; } else lw += 8; } w = lw > w ? lw : w; h = lines * line; }
    UIRect(6, 6, w + 12, h + 8, 0x000000A0);
    UIText(12, 10, buf, 0xE8E8E8FF);
}

// Placeholder HUD (the cockpit and projected gunsight come in their own
// step): a projected aiming ring, and two slim bands -- energy and sun --
// with no words or numbers (DESIGN.md §7).
void DrawHud() {
    float cx = g_screenW * 0.5f, cy = g_screenH * 0.5f;
    UIRing(cx, cy, 18.0f, 1.5f, 0xFFC87AB0);
    UIRect(cx - 1, cy - 1, 2, 2, 0xFFC87AD0);
    float bw = 180, bh = 6, x = 24, y = (float)g_screenH - 40;
    UIRect(x - 2, y - 2, bw + 4, bh + 4, 0x00000080);
    UIRect(x, y, bw * g_mech.energy, bh, g_mech.shield ? 0x9FD4FFE0 : 0xFFB45AE0);
    UIRect(x - 2, y + 12 - 2, bw + 4, 4 + 4, 0x00000080);
    UIRect(x, y + 12, bw * g_mech.sun, 4, 0xFFF0B0C0);
}
} // namespace

void GameInit(uint32_t seed) {
    g_seed = seed;
    g_terrain.Reset(seed);
    PlaceMech(g_mech, g_terrain, 0, 0);
    g_mech.energy = 1.0f;
    g_terrain.Update(g_mech.pos, VIEW_RADIUS);
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
        g_blasts = 0;
        Toast("FIELD RESET", 2.0f);
    } else if (vk == 'T' && !g_paused) {
        g_dayTime = fmodf(g_dayTime + DAY_LENGTH_SECONDS / 24.0f, DAY_LENGTH_SECONDS);
    }
}

void GameTick(float dt) {
    ProfScope prof(PROF_SIM);
    float mx, my;
    TakeMouse(mx, my);
    const float k = 0.0022f * g_sensitivity;
    g_mech.yaw += mx * k;
    g_mech.pitch += (g_invertY ? my : -my) * k;
    g_mech.pitch = Clamp(g_mech.pitch, -1.2f, 1.2f);

    MechInput in;
    in.forward = (ActionHeld(ACT_FORWARD) ? 1.0f : 0.0f) - (ActionHeld(ACT_BACK) ? 1.0f : 0.0f);
    in.strafe = (ActionHeld(ACT_RIGHT) ? 1.0f : 0.0f) - (ActionHeld(ACT_LEFT) ? 1.0f : 0.0f);
    in.jump = TakePress(ACT_JUMP);
    in.boost = ActionHeld(ACT_BOOST);
    in.toggleShield = TakePress(ACT_SHIELD);

    Vec3 panels[4];
    g_mech.PanelPoints(g_tune, panels);
    float sun = SunExposure(g_terrain, panels, 4, g_dayTime);
    TickMech(g_mech, g_tune, in, g_terrain, sun, dt);

    // Placeholder until the weapons step: fire blows a hole where you aim.
    if (TakePress(ACT_FIRE)) {
        Vec3 hit;
        if (g_terrain.Raycast(g_mech.Eye(g_tune), g_mech.Forward(), 700.0f, hit)) {
            g_terrain.Blast(hit, BLAST_RADIUS);
            g_blasts++;
        }
    }
    TakePress(ACT_NEXT_WEAPON); TakeWheel(); TakePress(ACT_LOCK); // bound, used in the weapons step

    g_dayTime = fmodf(g_dayTime + dt, DAY_LENGTH_SECONDS);
}

void GameFrame(float frameSeconds) {
    if (g_toastTime > 0) g_toastTime -= frameSeconds;
    {
        ProfScope prof(PROF_TERRAIN);
        g_terrain.Update(g_mech.pos, VIEW_RADIUS);
    }
    {
        ProfScope prof(PROF_MESH);
        int built = g_terrain.BuildMeshes(MESH_BUDGET);
        SyncTerrain(g_terrain);
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
    GpuMarkWorldDone();
    UIBegin();
    DrawHud();
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
