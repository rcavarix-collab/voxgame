// theline.h
//
// The Line (DESIGN.md Part XVIII): a thin, one-dimensional distortion in
// local time, personal to the player. It lies horizontally at about the
// player's own height and sweeps around a pivot -- the player's
// dwell-weighted centre of gravity -- clockwise or counterclockwise
// (seen from above) according to the net sense of the player's own
// movement around that pivot. Its only intended expression is how
// things that already move behave (the star field's turning, a ghost
// of the moon); a debug marker exists purely for testing.
//
// Pure C++, deterministic, tested natively. Only the pivot history (a
// float per 32-block cell the player has spent time in), the spin
// accumulator and the line's angle persist; everything else is derived
// each tick from the player within loaded space.

#pragma once

#include "common.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

// Open parameters (the brief leaves the exact curves to prototyping).
struct LineTuning {
    float cellSize = 32.0f;          // pivot-history resolution, blocks
    float spinHalfLife = 3.0f * 3600.0f; // seconds of play for the spin memory to halve
    float turnSeconds = 3600.0f;     // one sweep per in-game day
    float heightFollow = 5.0f;       // seconds for the line's height to settle on the player's
    float heightAboveFeet = 0.5f;    // through the middle of blocks at the player's own level
    // Falloff: intensity = 10^-(steps of distance / blocksPerDecade).
    float decadeWith = 12.0f;        // blocks per order of magnitude, moving with the sweep
    float decadeAgainst = 3.0f;      // ... moving against it
    float stepSharpness = 0.25f;     // width of each staircase riser, fraction of a decade (1 = smooth)
    float smoothing = 0.5f;          // seconds; intensity low-pass
    // Sky expression.
    float starWobble = 0.10f;        // radians of star precession at full intensity
    float moonGhostScale = 0.25f;    // ghost moon amplitude relative to the stars (always < 1)
    float wobbleRate = 0.15f;        // rad/s of precession at zero intensity...
    float wobbleRateGain = 0.6f;     // ...plus this much more at full intensity
};

// A pull on the pivot. Today only the player's own dwelling feeds one;
// player-built structures are meant to add their own later (same
// mechanism, another source), and whichever source dominates supplies
// both the pivot position and the spin.
struct PivotSource {
    double weight = 0;   // sum of dwell^2 over cells
    double wx = 0, wz = 0;
    double angMom = 0;   // decaying net angular momentum about the pivot (top-down, + = counterclockwise)
};

struct LineState {
    // Persistent.
    std::unordered_map<long long, float> dwell; // cell key -> seconds spent
    PivotSource player;
    float theta = 0.0f;       // line direction angle, radians from +X toward +Z

    // Derived / transient.
    float pivotX = 0, pivotZ = 0;
    int spin = 1;             // +1 counterclockwise, -1 clockwise (from above: +X right, +Z up)
    float lineY = 0;          // height of the line
    float distance = 0;       // player's horizontal distance to the line
    float alignment = 0;      // -1 moving against the sweep .. +1 with it
    float blocksPerDecade = 6;
    float intensity = 0;      // smoothed 0..1
    float wobblePhase = 0;
    float lastX = 0, lastZ = 0;
    bool hasLast = false;
};

// One simulation tick for a player at (x, y, z).
void UpdateLine(LineState& s, const LineTuning& t, float x, float y, float z, float dt);

// Forget everything (new game).
void ResetLine(LineState& s);

// Horizontal direction of the line, and its sweep axis for the sky:
// the unit vector along the line.
static inline Vec3 LineDirection(const LineState& s) { return { cosf(s.theta), 0.0f, sinf(s.theta) }; }

// The star-field precession (a small rotation about a horizontal axis
// that itself circles with the wobble phase), as a 3x3 row-major matrix
// applied to directions. `amount` scales it (1 = stars, moonGhostScale =
// the ghost moon).
void LineSkyWobble(const LineState& s, const LineTuning& t, float amount, float out[3][3]);

// Save support.
struct LineSaveData {
    std::vector<std::pair<long long, float>> dwell;
    double angMom = 0;
    float theta = 0;
};
LineSaveData SnapshotLine(const LineState& s);
void RestoreLine(LineState& s, const LineTuning& t, const LineSaveData& d);

extern LineState g_line;
extern LineTuning g_lineTuning;
extern bool g_lineDebug; // F7: draw the debug marker and readout
