// prims.cpp -- see prims.h.

#include "prims.h"

Vec3 Pose::Apply(Vec3 l) const {
    l = l * scale;
    // Yaw about Y.
    float cy = cosf(yaw), sy = sinf(yaw);
    Vec3 v = { l.x * cy + l.z * sy, l.y, -l.x * sy + l.z * cy };
    if (tilt != 0.0f) {
        // Tilt: rotate about the horizontal axis perpendicular to the lean
        // direction (tiltYaw), so the top leans toward tiltYaw.
        Vec3 lean = { sinf(tiltYaw), 0, cosf(tiltYaw) };
        float along = Dot(v, lean);
        Vec3 side = v - lean * along;              // part that doesn't turn (plus y)
        float up = v.y;
        side.y = 0;
        float ct = cosf(tilt), st = sinf(tilt);
        float newAlong = along * ct + up * st;
        float newUp = -along * st + up * ct;
        v = side + lean * newAlong + Vec3{ 0, newUp, 0 };
    }
    return pos + v;
}

namespace {
void Tri(std::vector<MeshVertex>& out, Vec3 a, Vec3 b, Vec3 c, uint32_t rgba) {
    out.push_back({ a.x, a.y, a.z, rgba });
    out.push_back({ b.x, b.y, b.z, rgba });
    out.push_back({ c.x, c.y, c.z, rgba });
}
// Emit so the triangle faces away from `inside` (clockwise seen from outside).
void TriOut(std::vector<MeshVertex>& out, Vec3 a, Vec3 b, Vec3 c, Vec3 inside, uint32_t rgba) {
    if (Dot(Cross(b - a, c - a), a - inside) < 0) std::swap(b, c);
    Tri(out, a, b, c, rgba);
}
}

void PrimPrism(std::vector<MeshVertex>& out, const Pose& p, float r, float h, int sides, uint32_t rgba) {
    Vec3 mid = p.Apply({ 0, h * 0.5f, 0 });
    for (int i = 0; i < sides; i++) {
        float a0 = 2 * kPi * i / sides, a1 = 2 * kPi * (i + 1) / sides;
        Vec3 b0 = p.Apply({ r * cosf(a0), 0, r * sinf(a0) }), b1 = p.Apply({ r * cosf(a1), 0, r * sinf(a1) });
        Vec3 t0 = p.Apply({ r * cosf(a0), h, r * sinf(a0) }), t1 = p.Apply({ r * cosf(a1), h, r * sinf(a1) });
        TriOut(out, b0, b1, t1, mid, rgba);
        TriOut(out, b0, t1, t0, mid, rgba);
        TriOut(out, p.Apply({ 0, h, 0 }), t0, t1, mid, rgba);
    }
}

void PrimCone(std::vector<MeshVertex>& out, const Pose& p, float base, float r, float h, int sides, uint32_t rgba) {
    Vec3 apex = p.Apply({ 0, base + h, 0 }), c = p.Apply({ 0, base, 0 }), mid = p.Apply({ 0, base + h * 0.3f, 0 });
    for (int i = 0; i < sides; i++) {
        float a0 = 2 * kPi * i / sides, a1 = 2 * kPi * (i + 1) / sides;
        Vec3 e0 = p.Apply({ r * cosf(a0), base, r * sinf(a0) }), e1 = p.Apply({ r * cosf(a1), base, r * sinf(a1) });
        TriOut(out, e0, e1, apex, mid, rgba);
        TriOut(out, e0, e1, c, apex, rgba); // the underside
    }
}

void PrimBox(std::vector<MeshVertex>& out, const Pose& p, Vec3 c, Vec3 h, uint32_t rgba) {
    Vec3 corner[8];
    for (int i = 0; i < 8; i++) corner[i] = p.Apply({ c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z) });
    Vec3 mid = p.Apply(c);
    static const int F[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
    for (auto& f : F) {
        TriOut(out, corner[f[0]], corner[f[1]], corner[f[2]], mid, rgba);
        TriOut(out, corner[f[0]], corner[f[2]], corner[f[3]], mid, rgba);
    }
}

void PrimRock(std::vector<MeshVertex>& out, const Pose& p, float radius, float flat, uint32_t seed, uint32_t rgba) {
    const float t = (1 + sqrtf(5.0f)) / 2;
    Vec3 iv[12] = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
    static const int fi[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                   { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    Vec3 w[12];
    for (int i = 0; i < 12; i++) {
        Vec3 u = Normalize(iv[i]);
        float k = 0.72f + 0.56f * Hash01(i, (int)seed, 7, 99);
        w[i] = p.Apply({ u.x * radius * k, u.y * radius * k * flat, u.z * radius * k });
    }
    Vec3 mid = p.Apply({ 0, 0, 0 });
    for (auto& f : fi) {
        // A little tone per face, so the rock's facets read apart.
        uint32_t c = rgba;
        float v = 0.9f + 0.2f * Hash01(f[0], f[1], (int)seed, 5);
        uint32_t r = (uint32_t)((c & 255) * v), g = (uint32_t)(((c >> 8) & 255) * v), b = (uint32_t)(((c >> 16) & 255) * v);
        c = (r > 255 ? 255 : r) | ((g > 255 ? 255 : g) << 8) | ((b > 255 ? 255 : b) << 16) | (c & 0xFF000000u);
        TriOut(out, w[f[0]], w[f[1]], w[f[2]], mid, c);
    }
}
