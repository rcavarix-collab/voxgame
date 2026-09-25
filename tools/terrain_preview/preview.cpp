// Terrain preview: renders Cacophony's real terrain (terrain.cpp, sun.cpp)
// offline, with the same shading as the game's terrain and sky shaders
// (shaders.h), from the mech's eye height. For judging the ground's look
// without a Windows run. Keep the shading here in step with shaders.h.
//
// Build and run:
//   g++ -O2 -std=c++17 -I../.. preview.cpp ../../terrain.cpp ../../sun.cpp -o preview && ./preview OUTDIR

#include "../../terrain.h"
#include "../../sun.h"
#include "pngw.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Cam { Vec3 pos, fwd, right, up; float f; int W, H; };
Cam MakeCam(Vec3 pos, float yaw, float pitch, float fovYDeg, int W, int H) {
    Cam c;
    c.pos = pos;
    c.fwd = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    c.right = Normalize(Cross(kUp, c.fwd));
    c.up = Cross(c.fwd, c.right);
    c.f = (H * 0.5f) / tanf(fovYDeg * kPi / 360.0f);
    c.W = W; c.H = H;
    return c;
}

// ---- the terrain shader, in C++ (shaders.h g_terrainShaderSrc) ----
const Vec3 kGround[8] = {
    { 0.105f, 0.190f, 0.058f }, { 0.205f, 0.185f, 0.085f }, { 0.068f, 0.132f, 0.052f }, { 0.088f, 0.205f, 0.078f },
    { 0.118f, 0.080f, 0.050f }, { 0.165f, 0.098f, 0.062f }, { 0.150f, 0.142f, 0.128f }, { 0.150f, 0.146f, 0.140f } };
const int kSoil[8] = { 4, 5, 4, 4, 4, 5, 6, 7 };
float ShaderHash(Vec3 p) {
    uint32_t qx = (uint32_t)(int32_t)floorf(p.x * 7.0f + 1000.0f), qy = (uint32_t)(int32_t)floorf(p.y * 7.0f + 1000.0f), qz = (uint32_t)(int32_t)floorf(p.z * 7.0f + 1000.0f);
    uint32_t h = qx * 73856093u ^ qy * 19349663u ^ qz * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 1023u) / 1023.0f;
}
Vec3 Mul(Vec3 a, Vec3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
Vec3 Mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

struct Img { int W, H; std::vector<Vec3> c; std::vector<float> z; };

void DrawSky(Img& im, const Cam& cam, float dayTime) {
    SkyLight s = SkyAt(dayTime);
    Vec3 sun = SunDirection(dayTime);
    float strength = SunStrength(dayTime);
    float tx = (cam.W * 0.5f) / cam.f, ty = (cam.H * 0.5f) / cam.f;
    for (int y = 0; y < im.H; y++)
        for (int x = 0; x < im.W; x++) {
            float nx = (x + 0.5f) / im.W * 2 - 1, ny = 1 - (y + 0.5f) / im.H * 2;
            Vec3 d = Normalize(cam.fwd + cam.right * (nx * tx) + cam.up * (ny * ty));
            float up = std::max(0.0f, d.y);
            Vec3 c = Mix(s.horizon, s.zenith, sqrtf(up));
            c = Mix(c, s.ground, Clamp(-d.y * 4.0f, 0.0f, 1.0f));
            float sd = std::max(0.0f, Dot(d, sun));
            c = c + Vec3{ 1.0f, 0.92f, 0.78f } * ((powf(sd, 900.0f) * 6.0f + powf(sd, 12.0f) * 0.25f) * strength);
            im.c[(size_t)y * im.W + x] = c;
        }
}

void DrawTerrain(Img& im, const Cam& cam, const Terrain& t, float dayTime) {
    SkyLight s = SkyAt(dayTime);
    Vec3 sun = SunDirection(dayTime);
    float strength = SunStrength(dayTime);
    for (auto& kv : t.Chunks()) {
        const TerrainChunk& ch = kv.second;
        for (size_t i = 0; i + 2 < ch.indices.size(); i += 3) {
            const TerrainVertex* v[3] = { &ch.verts[ch.indices[i]], &ch.verts[ch.indices[i + 1]], &ch.verts[ch.indices[i + 2]] };
            Vec3 p[3];
            for (int k = 0; k < 3; k++) p[k] = { v[k]->x, v[k]->y, v[k]->z };
            Vec3 n = Normalize(Cross(p[1] - p[0], p[2] - p[0]));
            if (Dot(n, cam.pos - p[0]) <= 0) continue; // back face (clockwise = front, as in the game)
            // Shade once per facet (it's flat): exactly the pixel shader's steps.
            uint8_t mat = v[0]->mat;
            int g = mat & 0x7F; if (g > 7) g = 7;
            if (g <= 3 && n.y < 0.72f) g = kSoil[g];
            Vec3 c = kGround[g] * (0.94f + 0.12f * ShaderHash(p[0]));
            if (mat & 0x80) c = Mix(c, { 0.030f, 0.026f, 0.024f }, 0.6f);
            float ao = v[0]->ao / 255.0f;
            float ndl = std::max(0.0f, Dot(n, sun)) * strength;
            Vec3 lit = Mul(c, s.ambient * ((0.55f + 0.45f * n.y) * (0.45f + 0.55f * ao)) + s.sun * ndl);
            // Project.
            float sx[3], sy[3], iw[3];
            bool ok = true;
            for (int k = 0; k < 3; k++) {
                Vec3 d = p[k] - cam.pos;
                float z = Dot(d, cam.fwd);
                if (z < 0.5f) { ok = false; break; }
                sx[k] = cam.W * 0.5f + cam.f * Dot(d, cam.right) / z;
                sy[k] = cam.H * 0.5f - cam.f * Dot(d, cam.up) / z;
                iw[k] = 1.0f / z;
            }
            if (!ok) continue;
            float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (fabsf(area) < 1e-6f) continue;
            int x0 = std::max(0, (int)floorf(std::min({ sx[0], sx[1], sx[2] }))), x1 = std::min(im.W - 1, (int)ceilf(std::max({ sx[0], sx[1], sx[2] })));
            int y0 = std::max(0, (int)floorf(std::min({ sy[0], sy[1], sy[2] }))), y1 = std::min(im.H - 1, (int)ceilf(std::max({ sy[0], sy[1], sy[2] })));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                    float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                    float w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float w = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
                    size_t k = (size_t)y * im.W + x;
                    if (w <= im.z[k]) continue;
                    im.z[k] = w;
                    float dist = 1.0f / w; // along the view axis: close enough to the shader's true distance for a preview
                    float fog = Clamp((dist - 120.0f) / 520.0f, 0.0f, 1.0f) * 0.85f;
                    im.c[k] = Mix(lit, s.horizon, fog);
                }
        }
    }
}

float ToSrgb(float v) {
    v = Clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

void Render(const Terrain& t, const Cam& cam, float dayTime, const std::string& path, int ss) {
    Img im; im.W = cam.W; im.H = cam.H; im.c.assign((size_t)im.W * im.H, { 0, 0, 0 }); im.z.assign(im.c.size(), 0.0f);
    DrawSky(im, cam, dayTime);
    DrawTerrain(im, cam, t, dayTime);
    int W = im.W / ss, H = im.H / ss;
    std::vector<uint8_t> rgb((size_t)W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            Vec3 acc = { 0, 0, 0 };
            for (int j = 0; j < ss; j++) for (int i = 0; i < ss; i++) acc = acc + im.c[(size_t)(y * ss + j) * im.W + x * ss + i];
            acc = acc * (1.0f / (ss * ss));
            uint8_t* o = &rgb[((size_t)y * W + x) * 3];
            o[0] = (uint8_t)(ToSrgb(acc.x) * 255 + 0.5f); o[1] = (uint8_t)(ToSrgb(acc.y) * 255 + 0.5f); o[2] = (uint8_t)(ToSrgb(acc.z) * 255 + 0.5f);
        }
    WritePNG(path.c_str(), W, H, rgb);
    printf("wrote %s\n", path.c_str());
}
} // namespace

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : ".";
    const int SS = 2, W = 1280 * SS, H = 720 * SS;
    const float eye = 16.0f; // feet on the ground at y = 1, eye 15 m above
    Terrain t;
    t.Reset(20260925u); // the game's seed (main.cpp GameInit)
    // A few test blasts ahead, like firing from the start position.
    t.Blast({ 0, 1, 45 }, 5.5f);
    t.Blast({ 14, 1, 62 }, 5.5f);
    t.Blast({ -12, 1, 70 }, 5.5f);
    t.Blast({ -12, -3, 70 }, 5.5f);   // a second shot into the same hole
    t.Update({ 0, 0, 60 }, 420.0f);
    while (t.BuildMeshes(256) > 0) {}
    TerrainStats st = t.Stats();
    printf("chunks %d, triangles %lld\n", st.resident, st.triangles);
    const float morning = DAY_LENGTH_SECONDS * 0.33f, noon = DAY_LENGTH_SECONDS * 0.5f, evening = DAY_LENGTH_SECONDS * 0.72f;
    Render(t, MakeCam({ 0, eye, 0 }, 0.0f, -0.20f, 70.0f, W, H), morning, out + "/cockpit_morning.png", SS);
    Render(t, MakeCam({ 0, eye, 0 }, 0.0f, -0.20f, 70.0f, W, H), noon, out + "/cockpit_noon.png", SS);
    Render(t, MakeCam({ 0, eye, 0 }, 0.9f, -0.12f, 70.0f, W, H), evening, out + "/cockpit_evening.png", SS);
    Render(t, MakeCam({ 0, 60.0f, -20 }, 0.0f, -0.55f, 60.0f, W, H), noon, out + "/overview_noon.png", SS);
    return 0;
}
