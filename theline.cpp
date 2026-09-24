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

void RefreshPivot(LineState& s) {
    const PivotSource* d = DominantSource(s);
    if (d && d->weight > 0) {
        s.pivotX = (float)(d->wx / d->weight);
        s.pivotZ = (float)(d->wz / d->weight);
    }
    s.spin = (d && d->angMom < 0) ? -1 : 1;
}

} // namespace

void ResetLine(LineState& s) { s = LineState(); }

void UpdateLine(LineState& s, const LineTuning& t, float x, float y, float z, float dt) {
    if (dt <= 0) return;

    // 1. Pivot history: time spent per cell, weighted by that time squared,
    // so a place passed through barely registers and a place lived in
    // dominates. Incremental: adding dt to a cell adds 2*T*dt + dt^2.
    int cx = (int)floorf(x / t.cellSize), cz = (int)floorf(z / t.cellSize);
    float& T = s.dwell[CellKey(cx, cz)];
    double dW = 2.0 * T * dt + (double)dt * dt;
    T += dt;
    double centreX = (cx + 0.5) * t.cellSize, centreZ = (cz + 0.5) * t.cellSize;
    s.player.weight += dW;
    s.player.wx += dW * centreX;
    s.player.wz += dW * centreZ;

    RefreshPivot(s);

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
    RefreshPivot(s);

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
    d.dwell.assign(s.dwell.begin(), s.dwell.end());
    d.angMom = s.player.angMom;
    d.theta = s.theta;
    return d;
}

void RestoreLine(LineState& s, const LineTuning& t, const LineSaveData& d) {
    ResetLine(s);
    for (const auto& kv : d.dwell) {
        if (!(kv.second > 0)) continue;
        s.dwell[kv.first] = kv.second;
        int cx, cz; CellOf(kv.first, cx, cz);
        double w = (double)kv.second * kv.second;
        s.player.weight += w;
        s.player.wx += w * (cx + 0.5) * t.cellSize;
        s.player.wz += w * (cz + 0.5) * t.cellSize;
    }
    s.player.angMom = d.angMom;
    s.theta = d.theta;
    RefreshPivot(s);
}
