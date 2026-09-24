// profiler.cpp -- see profiler.h.

#define NOMINMAX
#include <windows.h>
#include "profiler.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <vector>

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

// Capture state (ProfStartCapture).
struct CaptureFrame { FrameSample s; int64_t counters[PCOUNT_COUNT]; };
bool g_capturing = false, g_captureReady = false;
float g_captureSeconds = 0, g_captureElapsed = 0;
std::string g_captureHeader, g_captureText;
std::vector<CaptureFrame> g_captureFrames;
void FinishCapture();
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
    g_counters[PCOUNT_SHADOW_RENDERS] = 0;
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
    if (g_capturing) {
        CaptureFrame cf;
        cf.s = f;
        memcpy(cf.counters, g_counters, sizeof(g_counters));
        g_captureFrames.push_back(cf);
        g_captureElapsed += frameSeconds;
        if (g_captureElapsed >= g_captureSeconds) FinishCapture();
    }

    g_sinceRefresh += frameSeconds;
    if (g_sinceRefresh >= REFRESH_SECONDS) { g_sinceRefresh = 0.0f; Refresh(); }
}

const ProfReport& ProfGetReport() { return g_report; }

const char* ProfSectionName(ProfSection s) {
    static const char* names[PROF_COUNT] = {
        "TERRAIN", "EVICT", "PHYSICS", "UPDATES", "MUSIC", "WORLD SOUND", "MESH", "SHADOW MAP", "WORLD DRAW", "POST", "UI", "PRESENT",
    };
    return names[s];
}

const char* ProfCounterName(ProfCounter c) {
    static const char* names[PCOUNT_COUNT] = {
        "CHUNKS RESIDENT", "CHUNKS DRAWN", "TRIANGLES", "MESHES BUILT", "DIRTY WAITING", "COLUMNS WAITING", "UPDATES WAITING", "SHADOW RENDERS",
    };
    return names[c];
}

// ---------------------------------------------------------------------
// Performance capture
// ---------------------------------------------------------------------

namespace {

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)(p * (double)(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

void FinishCapture() {
    g_capturing = false;
    const double k = MsPerTick();
    const size_t n = g_captureFrames.size();
    std::string out = g_captureHeader;
    char line[256];
    auto add = [&](const char* fmt, auto... args) { snprintf(line, sizeof line, fmt, args...); out += line; out += "\n"; };
    add("frames: %zu over %.1f s (%.1f fps average)", n, g_captureElapsed, n / std::max(0.001f, g_captureElapsed));
    if (n == 0) { g_captureText = out; g_captureReady = true; return; }
    std::vector<double> frame(n), work(n);
    for (size_t i = 0; i < n; i++) {
        frame[i] = g_captureFrames[i].s.frameSeconds * 1000.0;
        work[i] = std::max(0.0, frame[i] - g_captureFrames[i].s.ticks[PROF_PRESENT] * k);
    }
    add("%s", "");
    add("%-16s %8s %8s %8s %8s %8s", "ms", "median", "p95", "p99", "worst", "average");
    auto row = [&](const char* name, const std::vector<double>& v) {
        double sum = 0; for (double x : v) sum += x;
        add("%-16s %8.2f %8.2f %8.2f %8.2f %8.2f", name, Percentile(v, 0.5), Percentile(v, 0.95), Percentile(v, 0.99), Percentile(v, 1.0), sum / v.size());
    };
    row("FRAME", frame);
    row("WORK (NO VSYNC)", work);
    for (int s = 0; s < PROF_COUNT; s++) {
        std::vector<double> v(n);
        for (size_t i = 0; i < n; i++) v[i] = g_captureFrames[i].s.ticks[s] * k;
        char nm[40]; snprintf(nm, sizeof nm, "  %s", ProfSectionName((ProfSection)s));
        row(nm, v);
    }
    int over33 = 0, over50 = 0, over100 = 0;
    for (double w : work) { over33 += w > 33.3; over50 += w > 50.0; over100 += w > 100.0; }
    add("%s", "");
    add("hitches (work time): %d over 33 ms, %d over 50 ms, %d over 100 ms", over33, over50, over100);
    // The five worst frames and which systems took the time.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return work[a] > work[b]; });
    add("worst frames:");
    for (size_t r = 0; r < std::min<size_t>(5, n); r++) {
        size_t i = order[r];
        std::string parts;
        for (int s = 0; s < PROF_COUNT; s++) {
            if (s == PROF_PRESENT) continue;
            double ms = g_captureFrames[i].s.ticks[s] * k;
            if (ms < 0.5) continue;
            char b[64]; snprintf(b, sizeof b, " %s %.1f", ProfSectionName((ProfSection)s), ms);
            parts += b;
        }
        add("  #%zu at %.1f s: work %.1f ms --%s", i, [&] { double t = 0; for (size_t j = 0; j < i; j++) t += g_captureFrames[j].s.frameSeconds; return t; }(), work[i], parts.c_str());
    }
    add("%s", "");
    add("%-16s %8s", "peak load", "max");
    for (int c = 0; c < PCOUNT_COUNT; c++) {
        int64_t mx = 0;
        for (size_t i = 0; i < n; i++) mx = std::max(mx, g_captureFrames[i].counters[c]);
        add("%-16s %8lld", ProfCounterName((ProfCounter)c), (long long)mx);
    }
    g_captureText = out;
    g_captureReady = true;
    g_captureFrames.clear();
    g_captureFrames.shrink_to_fit();
}

} // namespace

void ProfStartCapture(float seconds, const std::string& header) {
    g_capturing = true;
    g_captureReady = false;
    g_captureSeconds = seconds;
    g_captureElapsed = 0;
    g_captureHeader = header;
    g_captureFrames.clear();
    g_captureFrames.reserve((size_t)(seconds * 250));
}
bool ProfCapturing() { return g_capturing; }
float ProfCaptureSecondsLeft() { return g_capturing ? std::max(0.0f, g_captureSeconds - g_captureElapsed) : 0.0f; }
bool ProfTakeCaptureReport(std::string& text) {
    if (!g_captureReady) return false;
    g_captureReady = false;
    text = g_captureText;
    return true;
}
