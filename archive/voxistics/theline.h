// theline.h
//
// The Line (DESIGN.md Part XVIII): a thin, one-dimensional distortion in
// local time, personal to the player. It lies horizontally at about the
// player's own height and sweeps around a pivot -- the place the player
// spends the most time, which the pivot travels toward as that changes
// -- clockwise seen from above, unless the player's own movement has been
// circling the pivot the other way for a good while. Its only intended
// expression is how things that already move behave; a debug marker
// exists purely for testing.
//
// That expression is time: near the line the visible sky (stars, moon,
// clouds) runs ahead of the day clock, racing faster the closer the
// player is, and once they leave it runs slow until it has fallen back
// into step. The sun, the light and the shadows keep the real clock.
//
// Pure C++, deterministic, tested natively. Only the pivot history (time
// per 16-block cell and where in it that time was spent, slowly fading),
// the spin accumulator and the line's angle persist; everything else is
// derived each tick from the player within loaded space.

#pragma once

#include "common.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

// Open parameters (the brief leaves the exact curves to prototyping).
struct LineTuning {
    float cellSize = 16.0f;          // pivot-history resolution, blocks (each cell also keeps where in it the time was spent)
    float dwellHalfLife = 30.0f * 60.0f; // seconds of play for time spent somewhere to count half: where you are this session wins over where you once were
    float pivotFollow = 60.0f;       // seconds for the pivot to close most of the way to a new favourite place...
    float pivotMaxSpeed = 2.0f;      // ...travelling at most this many blocks per second
    float spinHalfLife = 3.0f * 3600.0f; // seconds of play for the spin memory to halve
    float ccwThreshold = 600.0f;     // net counterclockwise circling (sum of r x v dt, blocks^2) needed to reverse the clockwise default
    float turnSeconds = 3600.0f;     // one sweep per in-game day
    float heightFollow = 5.0f;       // seconds for the line's height to settle on the player's
    float heightAboveFeet = 0.5f;    // through the middle of blocks at the player's own level
    // Falloff: intensity = 10^-(steps of distance / blocksPerDecade).
    float decadeWith = 12.0f;        // blocks per order of magnitude, moving with the sweep
    float decadeAgainst = 3.0f;      // ... moving against it
    float stepSharpness = 0.25f;     // width of each staircase riser, fraction of a decade (1 = smooth)
    float smoothing = 0.5f;          // seconds; intensity low-pass
    // Sky expression: the sky clock (stars, moon, clouds) races ahead near
    // the line; once the player leaves, the rush dies down and the sky
    // finds its way back into step with the day by the shorter way.
    float skyRace = 29.0f;           // extra sky seconds per second at full intensity (30x on the line: a night in 20 s)
    float skyLag = 0.9f;             // a little ahead: the sky runs this much slower than the clock (down to 0.1x) until back in step
    float skyCoast = 90.0f;          // well ahead: it runs on round to the next day, slowing as it nears it (seconds of lead per 1x extra)
    float skyEase = 2.0f;            // seconds for the sky's speed to settle on a new rate
    // Vertical reach: the line is a band this many blocks either side of its
    // height, which diffusers widen by the pulse they take each second
    // (Part VI) -- with diminishing returns, and only while they're fed.
    float baseHalfHeight = 1.0f;     // blocks above and below the line, unfed
    float halfHeightPerRootFeed = 2.0f; // + this x sqrt(pulses per second fed)
    float feedMemory = 10.0f;        // seconds over which the feed rate is judged
    float maxHalfHeight = 64.0f;
    float starWobble = 0.015f;       // radians of star precession at full intensity (faint: time is the expression)
    float moonGhostScale = 0.25f;    // ghost moon amplitude relative to the stars (always < 1)
    float wobbleRate = 0.05f;        // rad/s of precession at zero intensity...
    float wobbleRateGain = 0.2f;     // ...plus this much more at full intensity
};

// A pull on the pivot. Today only the player's own dwelling feeds one;
// player-built structures are meant to add their own later (same
// mechanism, another source), and whichever source dominates supplies
// both the pivot position and the spin.
struct PivotSource {
    double weight = 0;   // strength: seconds (faded) spent in its favourite cell
    double wx = 0, wz = 0; // where it pulls the pivot, times weight
    double angMom = 0;   // decaying net angular momentum about the pivot (top-down, + = counterclockwise)
};

// Time spent in one cell, and where: t seconds, and the sums of x*dt and
// z*dt, so x / t is the time-weighted mean position within the cell.
// All three are stored inflated by dwellScale (see LineState).
struct DwellCell { double t = 0, x = 0, z = 0; };

struct LineState {
    // Persistent.
    // Time spent per cell, stored inflated by dwellScale so fading every
    // cell costs nothing: real (faded) seconds = t / dwellScale.
    std::unordered_map<long long, DwellCell> dwell;
    PivotSource player;
    float theta = 0.0f;       // line direction angle, radians from +X toward +Z

    // Derived / transient.
    double dwellScale = 1.0;  // grows by 2^(dt / dwellHalfLife) each tick; renormalised long before overflow
    long long favourite = 0;  // cell with the most (faded) time
    bool hasFavourite = false;
    float targetX = 0, targetZ = 0; // where the pivot is heading: the favourite place
    bool hasPivot = false;
    float pivotX = 0, pivotZ = 0;   // travels toward the target
    int spin = -1;            // -1 clockwise (the default), +1 counterclockwise (from above: +X right, +Z up)
    float lineY = 0;          // height of the line
    float distance = 0;       // player's horizontal distance to the line
    float alignment = 0;      // -1 moving against the sweep .. +1 with it
    float blocksPerDecade = 6;
    float intensity = 0;      // smoothed 0..1
    float wobblePhase = 0;
    float feedRate = 0;       // pulses per second diffusers have been taking (smoothed)
    int pendingFeed = 0;      // pulses taken since the last tick
    float halfHeight = 1.0f;  // how far up and down the line reaches, blocks
    float skyLead = 0;        // seconds the visible sky is ahead of the day clock, 0..one day (the sky repeats daily)
    float cloudLead = 0;      // the same lead, never wrapped: clouds don't repeat, so they mustn't jump
    float skyRate = 1;        // how fast the visible sky is running (1 = with the clock)
    float lastX = 0, lastZ = 0;
    bool hasLast = false;
};

// One simulation tick for a player at (x, y, z).
void UpdateLine(LineState& s, const LineTuning& t, float x, float y, float z, float dt);

// How strongly the line reaches a place right now, 0..1: the same
// order-of-magnitude falloff as the player feels standing still there,
// measured from the band the line fills (its height, +- halfHeight).
float LineIntensityAt(const LineState& s, const LineTuning& t, float x, float y, float z);
// How fast time runs at a place because of the line: the rate the sky
// races at (1 far away, up to 1 + skyRace on the line). Pulse harvesters
// gather at it (Part VI).
float LineTimeRateAt(const LineState& s, const LineTuning& t, float x, float y, float z);
// Pulse a diffuser took (it widens the line's band while the feed lasts).
static inline void FeedLine(LineState& s, int pulses) { s.pendingFeed += pulses; }

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
struct LineCellSave { float x = 0, z = 0, seconds = 0; }; // mean position of the time, faded seconds
struct LineSaveData {
    std::vector<LineCellSave> cells; // one per cell visited; cells re-derive from x, z
    double angMom = 0;
    float theta = 0;
};
LineSaveData SnapshotLine(const LineState& s);
void RestoreLine(LineState& s, const LineTuning& t, const LineSaveData& d);

extern LineState g_line;
extern LineTuning g_lineTuning;
extern bool g_lineDebug; // F7: draw the debug marker and readout
