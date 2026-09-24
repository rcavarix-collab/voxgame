// theline.cpp -- see theline.h.

#include "theline.h"
#include <cmath>

LineState g_line;
LineTuning g_lineTuning;
bool g_lineDebug = false;

namespace {

long long CellKey(int cx, int cz) { return ((long long)(uint32_t)cx << 32) | (uint32_t)cz; }
void CellOf(long long key, int& cx, int& cz) {
    cx = (int)(uint32_t)((uint64_t)key >> 32);
    cz = (int)(uint32_t)((uint64_t)key & 0xFFFFFFFFu);
}

float Smooth01(float e0, float e1, float x) {
    float u = (x - e0) / (e1 - e0);
    u = u < 0 ? 0 : (u > 1 ? 1 : u);
    return u * u * (3 - 2 * u);
}

// Order-of-magnitude staircase: flat treads, short smooth risers, so each
// decade of intensity is a legible step rather than a continuous slope.
float Staircase(float u, float sharpness) {
    if (u <= 0) return 0;
    float whole = floorf(u), frac = u - whole;
    return whole + Smooth01(1.0f - sharpness, 1.0f, frac);
}

void Rotation(Vec3 axis, float angle, float m[3][3]) {
    float c = cosf(angle), s = sinf(angle), k = 1 - c;
    float x = axis.x, y = axis.y, z = axis.z;
    m[0][0] = c + x * x * k;     m[0][1] = x * y * k - z * s; m[0][2] = x * z * k + y * s;
    m[1][0] = y * x * k + z * s; m[1][1] = c + y * y * k;     m[1][2] = y * z * k - x * s;
    m[2][0] = z * x * k - y * s; m[2][1] = z * y * k + x * s; m[2][2] = c + z * z * k;
}

// The dominant pivot source supplies pivot and spin. With only the player
// as a source this is just the player; the loop is the extension point.
const PivotSource* DominantSource(const LineState& s) {
    const PivotSource* sources[] = { &s.player };
    const PivotSource* best = nullptr;
    for (const PivotSource* p : sources)
        if (!best || p->weight > best->weight) best = p;
    return best;
}

// Where the dominant source pulls the pivot, and the spin: clockwise
// unless there has been a good deal of net counterclockwise circling.
void RefreshTarget(LineState& s, const LineTuning& t) {
    const PivotSource* d = DominantSource(s);
    if (d && d->weight > 0) {
        s.targetX = (float)(d->wx / d->weight);
        s.targetZ = (float)(d->wz / d->weight);
    }
    s.spin = (d && d->angMom > t.ccwThreshold) ? 1 : -1;
}

// The player's pull: where in their favourite cell the time was actually
// spent, refined toward whichever of its neighbours also hold a lot of
// time (weights squared, relative to the favourite) -- each cell counting
// at its own time-weighted mean position, not its centre, so the pivot
// lands on the spot the player keeps coming back to in x and z alike,
// and never in the empty ground between two separate haunts, the way an
// average over everywhere would.
void RefreshPlayerSource(LineState& s, const LineTuning& t) {
    (void)t;
    if (!s.hasFavourite) return;
    auto fav = s.dwell.find(s.favourite);
    if (fav == s.dwell.end() || !(fav->second.t > 0)) return;
    double best = fav->second.t;
    int fx, fz; CellOf(s.favourite, fx, fz);
    double sw = 0, sx = 0, sz = 0;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            auto it = s.dwell.find(CellKey(fx + dx, fz + dz));
            if (it == s.dwell.end() || !(it->second.t > 0)) continue;
            const DwellCell& c = it->second;
            double w = c.t / best; w *= w;
            sw += w;
            sx += w * (c.x / c.t);
            sz += w * (c.z / c.t);
        }
    s.player.weight = best / s.dwellScale;
    s.player.wx = sx / sw * s.player.weight;
    s.player.wz = sz / sw * s.player.weight;
}

// The pivot travels toward its target rather than jumping: most of the
// way in pivotFollow seconds, never faster than pivotMaxSpeed.
void MovePivot(LineState& s, const LineTuning& t, float dt) {
    if (!s.hasPivot) { s.pivotX = s.targetX; s.pivotZ = s.targetZ; s.hasPivot = true; return; }
    float dx = s.targetX - s.pivotX, dz = s.targetZ - s.pivotZ;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 1e-4f) return;
    float step = dist * (1.0f - expf(-dt / t.pivotFollow));
    if (step > t.pivotMaxSpeed * dt) step = t.pivotMaxSpeed * dt;
    s.pivotX += dx / dist * step;
    s.pivotZ += dz / dist * step;
}

} // namespace

void ResetLine(LineState& s) { s = LineState(); }

void UpdateLine(LineState& s, const LineTuning& t, float x, float y, float z, float dt) {
    if (dt <= 0) return;

    // 1. Pivot history: time spent per cell, fading with a long half-life
    // so where the player spends time these days outweighs where they
    // once did. Every cell fades at the same rate, so instead of touching
    // them all, new time is added inflated by a growing scale; the
    // favourite cell can then only change to the one being added to.
    s.dwellScale *= exp2((double)dt / t.dwellHalfLife);
    if (s.dwellScale > 1e100) { // renormalise, every few thousand hours of play
        for (auto& kv : s.dwell) { kv.second.t /= s.dwellScale; kv.second.x /= s.dwellScale; kv.second.z /= s.dwellScale; }
        s.dwellScale = 1.0;
    }
    int cx = (int)floorf(x / t.cellSize), cz = (int)floorf(z / t.cellSize);
    long long key = CellKey(cx, cz);
    DwellCell& cell = s.dwell[key];
    double w = (double)dt * s.dwellScale;
    cell.t += w; cell.x += w * x; cell.z += w * z;
    if (!s.hasFavourite || key == s.favourite || cell.t > s.dwell[s.favourite].t) { s.favourite = key; s.hasFavourite = true; }
    RefreshPlayerSource(s, t);
    RefreshTarget(s, t);
    MovePivot(s, t, dt);

    // 2. Spin: net angular momentum of the player's actual movement about
    // the pivot, top-down, with a slow memory.
    float vx = 0, vz = 0;
    if (s.hasLast) { vx = (x - s.lastX) / dt; vz = (z - s.lastZ) / dt; }
    // A teleport-sized jump (load, respawn) isn't movement.
    if (vx * vx + vz * vz > 400.0f) { vx = vz = 0; }
    s.lastX = x; s.lastZ = z; s.hasLast = true;
    float rx = x - s.pivotX, rz = z - s.pivotZ;
    double decay = exp(-(double)dt * 0.69314718 / t.spinHalfLife);
    s.player.angMom = s.player.angMom * decay + (double)(rx * vz - rz * vx) * dt;
    RefreshTarget(s, t);

    // 3. The line sweeps around the pivot in the spin's direction and
    // settles at the player's height.
    const float TWO_PI = 6.2831853f;
    float omega = TWO_PI / t.turnSeconds;
    s.theta = fmodf(s.theta + s.spin * omega * dt, TWO_PI);
    if (s.theta < 0) s.theta += TWO_PI;
    float follow = 1.0f - expf(-dt / t.heightFollow);
    float targetY = y + t.heightAboveFeet;
    s.lineY = s.lineY != 0 ? s.lineY + (targetY - s.lineY) * follow : targetY;

    // 4. Intensity: exponential decay in whole orders of magnitude of the
    // horizontal distance to the line, stretched when moving with the
    // sweep and compressed against it.
    float ux = cosf(s.theta), uz = sinf(s.theta);
    float along = rx * ux + rz * uz;           // position along the line from the pivot
    float signedDist = rx * (-uz) + rz * ux;   // across it (+ on the counterclockwise side)
    s.distance = fabsf(signedDist);
    // The line's own motion where it's nearest the player: perpendicular to
    // itself, spin * omega * along.
    float sweepDir = (float)s.spin * (along >= 0 ? 1.0f : -1.0f);
    float nx = -uz * sweepDir, nz = ux * sweepDir;
    float speed = sqrtf(vx * vx + vz * vz);
    s.alignment = speed > 1e-3f ? (vx * nx + vz * nz) / speed : 0.0f;
    float moving = speed / 4.5f; moving = moving > 1 ? 1 : moving;
    float blend = 0.5f + 0.5f * s.alignment * moving;  // 0 against .. 0.5 still .. 1 with
    s.blocksPerDecade = expf(logf(t.decadeAgainst) + (logf(t.decadeWith) - logf(t.decadeAgainst)) * blend);
    float target = powf(10.0f, -Staircase(s.distance / s.blocksPerDecade, t.stepSharpness));
    s.intensity += (target - s.intensity) * (1.0f - expf(-dt / t.smoothing));

    // 5. The sky's precession phase: rotational sense from the spin, rate
    // rising with intensity.
    s.wobblePhase += s.spin * (t.wobbleRate + t.wobbleRateGain * s.intensity) * dt;
    s.wobblePhase = fmodf(s.wobblePhase, TWO_PI);

    // 6. The sky clock: near the line the visible sky runs ahead, faster
    // the closer the player is (the square root lets the race build over
    // the approach rather than only at the last step), easing off as the
    // lead nears its limit; away from it the sky runs slow, by as much as
    // it is ahead, until it has fallen back into step with the day.
    float nearness = sqrtf(s.intensity > 0 ? s.intensity : 0.0f);
    float fill = s.skyLead / t.skyLeadMax;
    s.skyRate = 1.0f + t.skyRace * nearness * (1.0f - fill) - t.skyLag * (1.0f - nearness) * fill;
    s.skyLead += (s.skyRate - 1.0f) * dt;
    s.skyLead = s.skyLead < 0 ? 0 : (s.skyLead > t.skyLeadMax ? t.skyLeadMax : s.skyLead);
}

void LineSkyWobble(const LineState& s, const LineTuning& t, float amount, float out[3][3]) {
    // Tilt by (intensity * amplitude) about a horizontal axis that starts
    // along the line and circles with the phase: the star field keeps its
    // normal turning but its pole traces a small circle -- subtly wrong.
    float a = s.intensity * t.starWobble * amount;
    float ang = s.theta + s.wobblePhase;
    Vec3 axis = { cosf(ang), 0.0f, sinf(ang) };
    Rotation(axis, a, out);
}

LineSaveData SnapshotLine(const LineState& s) {
    LineSaveData d;
    d.cells.reserve(s.dwell.size());
    for (const auto& kv : s.dwell) {
        const DwellCell& c = kv.second;
        if (!(c.t > 0)) continue;
        d.cells.push_back({ (float)(c.x / c.t), (float)(c.z / c.t), (float)(c.t / s.dwellScale) });
    }
    d.angMom = s.player.angMom;
    d.theta = s.theta;
    return d;
}

void RestoreLine(LineState& s, const LineTuning& t, const LineSaveData& d) {
    ResetLine(s);
    // Cells re-derive from where their time was spent, so a history saved
    // at another cell size (v7/v8 used 32) folds into this one.
    for (const LineCellSave& c : d.cells) {
        if (!(c.seconds > 0) || !std::isfinite(c.x) || !std::isfinite(c.z)) continue;
        long long key = CellKey((int)floorf(c.x / t.cellSize), (int)floorf(c.z / t.cellSize));
        DwellCell& cell = s.dwell[key];
        cell.t += c.seconds; cell.x += (double)c.seconds * c.x; cell.z += (double)c.seconds * c.z;
        if (!s.hasFavourite || cell.t > s.dwell[s.favourite].t) { s.favourite = key; s.hasFavourite = true; }
    }
    s.player.angMom = d.angMom;
    s.theta = d.theta;
    RefreshPlayerSource(s, t);
    RefreshTarget(s, t);
    MovePivot(s, t, 0.0f); // first placement: straight onto the target
}
