// profiler.cpp -- see profiler.h.

#define NOMINMAX
#include <windows.h>
#include "profiler.h"
#include <cstring>

bool g_showProfiler = false;

namespace {

// ~2 s of history at 60 fps; a slower frame rate simply covers longer.
const int HISTORY = 128;
const float REFRESH_SECONDS = 0.5f;

struct FrameSample {
    int64_t ticks[PROF_COUNT];
    float frameSeconds;
};

FrameSample g_history[HISTORY];
int g_historyCount = 0, g_historyNext = 0;
int64_t g_current[PROF_COUNT];
int64_t g_counters[PCOUNT_COUNT];
int64_t g_counterMaxBuilding[PCOUNT_COUNT];
float g_sinceRefresh = 0.0f;
ProfReport g_report;
double g_msPerTick = 0.0;

double MsPerTick() {
    if (g_msPerTick == 0.0) {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        g_msPerTick = 1000.0 / (double)f.QuadPart;
    }
    return g_msPerTick;
}

void Refresh() {
    double k = MsPerTick();
    ProfReport r = {};
    if (g_historyCount > 0) {
        for (int s = 0; s < PROF_COUNT; s++) {
            int64_t sum = 0, mx = 0;
            for (int i = 0; i < g_historyCount; i++) {
                int64_t t = g_history[i].ticks[s];
                sum += t; if (t > mx) mx = t;
            }
            r.avgMs[s] = (float)(sum * k / g_historyCount);
            r.maxMs[s] = (float)(mx * k);
        }
        float fSum = 0, fMax = 0, wSum = 0, wMax = 0;
        for (int i = 0; i < g_historyCount; i++) {
            float fm = g_history[i].frameSeconds * 1000.0f;
            float wm = fm - (float)(g_history[i].ticks[PROF_PRESENT] * k);
            if (wm < 0) wm = 0;
            fSum += fm; if (fm > fMax) fMax = fm;
            wSum += wm; if (wm > wMax) wMax = wm;
        }
        r.frameAvgMs = fSum / g_historyCount; r.frameMaxMs = fMax;
        r.workAvgMs = wSum / g_historyCount;  r.workMaxMs = wMax;
    }
    memcpy(r.counters, g_counters, sizeof(g_counters));
    memcpy(r.countersMax, g_counterMaxBuilding, sizeof(g_counterMaxBuilding));
    g_report = r;
    memset(g_counterMaxBuilding, 0, sizeof(g_counterMaxBuilding));
}

} // namespace

int64_t ProfNow() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void ProfBeginFrame() {
    memset(g_current, 0, sizeof(g_current));
    // Per-frame counts (drawn, built) restart; queue lengths are Set each frame.
    g_counters[PCOUNT_CHUNKS_DRAWN] = 0;
    g_counters[PCOUNT_TRIANGLES_DRAWN] = 0;
    g_counters[PCOUNT_MESHES_BUILT] = 0;
}

void ProfAdd(ProfSection s, int64_t ticks) { g_current[s] += ticks; }
void ProfSetCounter(ProfCounter c, int64_t value) { g_counters[c] = value; }
void ProfAddCounter(ProfCounter c, int64_t value) { g_counters[c] += value; }

void ProfEndFrame(float frameSeconds) {
    FrameSample& f = g_history[g_historyNext];
    memcpy(f.ticks, g_current, sizeof(g_current));
    f.frameSeconds = frameSeconds;
    g_historyNext = (g_historyNext + 1) % HISTORY;
    if (g_historyCount < HISTORY) g_historyCount++;
    for (int c = 0; c < PCOUNT_COUNT; c++)
        if (g_counters[c] > g_counterMaxBuilding[c]) g_counterMaxBuilding[c] = g_counters[c];

    g_sinceRefresh += frameSeconds;
    if (g_sinceRefresh >= REFRESH_SECONDS) { g_sinceRefresh = 0.0f; Refresh(); }
}

const ProfReport& ProfGetReport() { return g_report; }

const char* ProfSectionName(ProfSection s) {
    static const char* names[PROF_COUNT] = {
        "TERRAIN", "EVICT", "PHYSICS", "FALLS", "MUSIC", "MESH", "WORLD DRAW", "UI", "PRESENT",
    };
    return names[s];
}

const char* ProfCounterName(ProfCounter c) {
    static const char* names[PCOUNT_COUNT] = {
        "CHUNKS RESIDENT", "CHUNKS DRAWN", "TRIANGLES", "MESHES BUILT", "DIRTY WAITING", "COLUMNS WAITING", "FALLS WAITING",
    };
    return names[c];
}
