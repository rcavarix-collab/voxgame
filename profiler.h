// profiler.h
//
// Frame-time instrumentation (docs/ARCHITECTURE.md 1.4; carried from Voxistics): how long each engine
// system took this frame, plus a few load counters, rolled into a short
// window of history so the overlay can show both the typical cost and
// the worst frame. "Lagless" is only a design priority if it can be
// measured -- this is the thing to glance at whenever a new system goes
// in. Collection is always on (a handful of QueryPerformanceCounter
// calls per frame); only the overlay's text is optional.

#pragma once

#include <cstdint>
#include <string>

enum ProfSection {
    PROF_SIM,       // the fixed-step tick: mech, sun exposure (and later weapons, props, fire)
    PROF_TERRAIN,   // keeping chunks resident around the mech
    PROF_MESH,      // terrain meshing + GPU uploads (budgeted per frame)
    PROF_WORLD,     // sky + terrain draw submission (CPU side)
    PROF_UI,        // UI build + draw submission
    PROF_PRESENT,   // Present(): mostly vsync wait, not work
    // GPU time, from timestamp queries read back three frames late (so it
    // never stalls): what the graphics card spent on each pass. Measures only
    // our own drawing; nothing about the machine is read.
    PROF_GPU_WORLD,
    PROF_GPU_UI,
    PROF_COUNT
};

enum ProfCounter {
    PCOUNT_CHUNKS_RESIDENT,
    PCOUNT_CHUNKS_DRAWN,
    PCOUNT_TRIANGLES_DRAWN,
    PCOUNT_MESHES_BUILT,
    PCOUNT_MESH_WAITING,
    PCOUNT_COUNT
};

extern bool g_showProfiler; // overlay visibility (settings.cfg / F3)

void ProfBeginFrame();
void ProfAdd(ProfSection s, int64_t ticks);
void ProfAddMs(ProfSection s, double ms); // for times measured elsewhere (the GPU's)
void ProfSetCounter(ProfCounter c, int64_t value);
void ProfAddCounter(ProfCounter c, int64_t value);
// Closes the frame: `frameSeconds` is the full frame-to-frame time.
void ProfEndFrame(float frameSeconds);

int64_t ProfNow();

// RAII timer: ProfScope scope(PROF_MESH); times the enclosing block.
struct ProfScope {
    ProfSection section;
    int64_t start;
    explicit ProfScope(ProfSection s) : section(s), start(ProfNow()) {}
    ~ProfScope() { ProfAdd(section, ProfNow() - start); }
    ProfScope(const ProfScope&) = delete;
    ProfScope& operator=(const ProfScope&) = delete;
};

// Snapshot the overlay draws, refreshed twice a second over the last
// ~2 seconds of frames (so numbers are readable, not flickering).
struct ProfReport {
    float avgMs[PROF_COUNT];
    float maxMs[PROF_COUNT];
    float frameAvgMs, frameMaxMs;
    float workAvgMs, workMaxMs; // frame minus Present(): the part that is actually ours
    int64_t counters[PCOUNT_COUNT]; // latest frame's values
    int64_t countersMax[PCOUNT_COUNT];
};
const ProfReport& ProfGetReport();

// Start-up timeline: how long the game took to boot, phase by
// phase, so a slow start can be pinned on its cause. ProfBootMark ends
// the phase running since the previous mark (the first phase runs from
// the moment Windows created the process: loading the exe and its DLLs).
// Kept in memory; shown on the F3 overlay and in the Ctrl+F3 report.
void ProfBootMark(const char* phase);
void ProfBootNote(const std::string& note); // extra detail, e.g. the shader cache's hits
std::string ProfBootSummary(bool multiLine);
const char* ProfSectionName(ProfSection s);

// Performance capture (Ctrl+F3): records every frame for
// `seconds`, then builds a plain-text report -- frame and work time
// percentiles, hitches and what caused the worst of them, per-system
// costs, load peaks -- for the owner to save and send. `header` (build,
// settings, resolution) goes at the top.
void ProfStartCapture(float seconds, const std::string& header);
bool ProfCapturing();
float ProfCaptureSecondsLeft();
// True once, when a capture has finished, with its report.
bool ProfTakeCaptureReport(std::string& text);
const char* ProfCounterName(ProfCounter c);
