// Voxel-shape comparison: one hillside (tunnel cut in, pit dug on top) built
// from six cell shapes, rendered offline with a small z-buffer rasterizer.
// Every shape uses the same generic rule: a cell's face toward a neighbour is
// its vertices lying on the bisector plane between the two centres.
#include "pngw.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

struct V3 { double x, y, z; };
static V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 operator*(V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static V3 norm(V3 a) { double l = sqrt(dot(a, a)); return a * (1.0 / l); }

const double SX = 48, SZ = 48, SY = 30;

static double Terrain(double x, double z) {
    return 3.0 + 0.42 * z + 1.3 * sin(x * 0.21 + 0.7) * cos(z * 0.17) + 0.8 * sin(x * 0.09 - z * 0.13 + 2.0);
}
static bool InTunnel(V3 p) {
    double dx = p.x - 27, dy = p.y - 8.5;
    return dx * dx + dy * dy < 3.0 * 3.0 && p.z > 4;
}
static bool InPit(V3 p) {
    double top = Terrain(12, 35);
    double depth = top - p.y;
    if (depth < -4 || depth > 7) return false;
    double r = 4.6 - 0.25 * (depth > 0 ? depth : 0);
    double dx = p.x - 12, dz = p.z - 35;
    return dx * dx + dz * dz < r * r;
}
static bool Solid(V3 p) {
    if (p.x < 0 || p.x > SX || p.z < 0 || p.z > SZ) return false; // cut sides show the lattice
    if (p.y < 0) return true;
    if (p.y >= Terrain(p.x, p.z)) return false;
    return !InTunnel(p) && !InPit(p);
}

struct Cell { V3 c; std::vector<V3> verts; std::vector<V3> nbr; };
typedef std::function<void(const Cell&)> CellFn;

// Prisms (cube, hex, triangle): a 2D polygon extruded; sideways neighbours
// are the centroid reflected across each edge.
static void EmitPrism(const std::vector<V3>& poly2, double y0, double H, const CellFn& fn) {
    Cell c;
    V3 cen = { 0, 0, 0 };
    for (auto& p : poly2) cen = cen + p;
    cen = cen * (1.0 / poly2.size());
    c.c = { cen.x, y0 + H / 2, cen.z };
    if (c.c.x < 0 || c.c.x > SX || c.c.z < 0 || c.c.z > SZ || c.c.y > SY) return;
    for (auto& p : poly2) { c.verts.push_back({ p.x, y0, p.z }); c.verts.push_back({ p.x, y0 + H, p.z }); }
    for (size_t i = 0; i < poly2.size(); i++) {
        V3 a = poly2[i], b = poly2[(i + 1) % poly2.size()];
        V3 e = norm(b - a), q = cen - a;
        V3 foot = a + e * dot(q, e);
        V3 r = foot * 2 - cen;
        c.nbr.push_back({ r.x, c.c.y, r.z });
    }
    c.nbr.push_back(c.c + V3{ 0, H, 0 });
    c.nbr.push_back(c.c - V3{ 0, H, 0 });
    fn(c);
}

static void Cubes(const CellFn& fn) {
    for (int y = 0; y < SY; y++)
        for (int z = 0; z < SZ; z++)
            for (int x = 0; x < SX; x++)
                EmitPrism({ { (double)x, 0, (double)z }, { x + 1.0, 0, (double)z }, { x + 1.0, 0, z + 1.0 }, { (double)x, 0, z + 1.0 } }, y, 1, fn);
}
static void Hexes(double w, double H, const CellFn& fn) {
    double R = w / sqrt(3.0), dz = w * sqrt(3.0) / 2;
    for (int k = 0; k * H < SY; k++)
        for (int j = -1; j * dz < SZ + 1; j++)
            for (int i = -30; i < 60; i++) {
                double cx = i * w + j * w / 2, cz = j * dz;
                if (cx < -1 || cx > SX + 1) continue;
                std::vector<V3> p;
                for (int n = 0; n < 6; n++) { double a = (30 + 60 * n) * M_PI / 180; p.push_back({ cx + R * cos(a), 0, cz + R * sin(a) }); }
                EmitPrism(p, k * H, H, fn);
            }
}
static void Triangles(double s, double H, const CellFn& fn) {
    double dz = s * sqrt(3.0) / 2;
    for (int k = 0; k * H < SY; k++)
        for (int j = 0; j * dz < SZ; j++)
            for (int i = -30; i < 60; i++) {
                double x0 = i * s + j * s / 2 - floor(j / 2.0) * s; // keep rows over the box
                double z0 = j * dz, z1 = (j + 1) * dz;
                if (x0 < -2 || x0 > SX + 1) continue;
                EmitPrism({ { x0, 0, z0 }, { x0 + s / 2, 0, z1 }, { x0 + s, 0, z0 } }, k * H, H, fn);
                EmitPrism({ { x0 + s, 0, z0 }, { x0 + s / 2, 0, z1 }, { x0 + 1.5 * s, 0, z1 } }, k * H, H, fn);
            }
}
// Lattice Voronoi cells: points, neighbour offsets and cell vertices in lattice units.
static void LatticeCells(double scale, bool bcc, const CellFn& fn) {
    std::vector<V3> nb, vs;
    if (!bcc) { // FCC -> rhombic dodecahedra
        for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) { nb.push_back({ (double)a, (double)b, 0 }); nb.push_back({ (double)a, 0, (double)b }); nb.push_back({ 0, (double)a, (double)b }); }
        for (int a = -1; a <= 1; a += 2) { vs.push_back({ (double)a, 0, 0 }); vs.push_back({ 0, (double)a, 0 }); vs.push_back({ 0, 0, (double)a }); }
        for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) for (int c = -1; c <= 1; c += 2) vs.push_back({ a * .5, b * .5, c * .5 });
    } else { // BCC -> truncated octahedra
        for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) for (int c = -1; c <= 1; c += 2) nb.push_back({ (double)a, (double)b, (double)c });
        for (int a = -2; a <= 2; a += 4) { nb.push_back({ (double)a, 0, 0 }); nb.push_back({ 0, (double)a, 0 }); nb.push_back({ 0, 0, (double)a }); }
        for (int s1 = -1; s1 <= 1; s1 += 2) for (int s2 = -1; s2 <= 1; s2 += 2) {
            double h = .5 * s1, o = 1.0 * s2;
            V3 p[6] = { { 0, h, o }, { 0, o, h }, { h, 0, o }, { o, 0, h }, { h, o, 0 }, { o, h, 0 } };
            for (auto& q : p) vs.push_back(q);
        }
    }
    int nx = (int)(SX / scale) + 2, ny = (int)(SY / scale) + 2, nz = (int)(SZ / scale) + 2;
    for (int k = -1; k < ny; k++)
        for (int j = -1; j < nz; j++)
            for (int i = -1; i < nx; i++) {
                bool ok = bcc ? (((i & 1) == (j & 1)) && ((j & 1) == (k & 1))) : (((i + j + k) & 1) == 0);
                if (!ok) continue;
                Cell c;
                c.c = V3{ (double)i, (double)k, (double)j } * scale;
                if (c.c.x < 0 || c.c.x > SX || c.c.z < 0 || c.c.z > SZ || c.c.y < -scale) continue;
                for (auto& v : vs) c.verts.push_back(c.c + v * scale);
                for (auto& n : nb) c.nbr.push_back(c.c + n * scale);
                fn(c);
            }
}

// ---- rendering ----
struct Cam { V3 pos, fwd, right, up; double f; int W, H; };
static Cam MakeCam(V3 pos, V3 at, double fovDeg, int W, int H) {
    Cam c; c.pos = pos; c.fwd = norm(at - pos); c.right = norm(cross(c.fwd, { 0, 1, 0 })); c.up = cross(c.right, c.fwd);
    c.f = (H / 2.0) / tan(fovDeg * M_PI / 360); c.W = W; c.H = H; return c;
}
struct Img { int W, H; std::vector<float> rgb, z; };

static uint32_t Hash(V3 p) {
    int32_t a = (int32_t)floor(p.x * 97 + 0.5), b = (int32_t)floor(p.y * 89 + 0.5), c = (int32_t)floor(p.z * 83 + 0.5);
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)c * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return h;
}

struct Stats { long cells = 0, faces = 0, tris = 0; double area = 0; };

static void DrawPoly(Img& im, const Cam& cam, const std::vector<V3>& poly, V3 col) {
    int n = (int)poly.size();
    std::vector<double> sx(n), sy(n), iw(n);
    for (int i = 0; i < n; i++) {
        V3 d = poly[i] - cam.pos;
        double z = dot(d, cam.fwd); if (z < 0.1) return;
        sx[i] = cam.W / 2.0 + cam.f * dot(d, cam.right) / z;
        sy[i] = cam.H / 2.0 - cam.f * dot(d, cam.up) / z;
        iw[i] = 1.0 / z;
    }
    // Screen-space area sign (front faces were already chosen; skip slivers).
    double area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += sx[i] * sy[j] - sx[j] * sy[i]; }
    if (fabs(area) < 1e-6) return;
    double sgn = area > 0 ? 1 : -1;
    // 1/z is affine in screen space over a plane: fit from three well-spread vertices.
    int a = 0, b = n / 3, c = (2 * n) / 3;
    double det = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a]);
    if (fabs(det) < 1e-9) return;
    double gx = ((iw[b] - iw[a]) * (sy[c] - sy[a]) - (iw[c] - iw[a]) * (sy[b] - sy[a])) / det;
    double gy = ((sx[b] - sx[a]) * (iw[c] - iw[a]) - (sx[c] - sx[a]) * (iw[b] - iw[a])) / det;
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < n; i++) { minx = std::min(minx, sx[i]); maxx = std::max(maxx, sx[i]); miny = std::min(miny, sy[i]); maxy = std::max(maxy, sy[i]); }
    int x0 = std::max(0, (int)floor(minx)), x1 = std::min(im.W - 1, (int)ceil(maxx));
    int y0 = std::max(0, (int)floor(miny)), y1 = std::min(im.H - 1, (int)ceil(maxy));
    std::vector<double> ex(n), ey(n), el(n);
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; ex[i] = sx[j] - sx[i]; ey[i] = sy[j] - sy[i]; el[i] = sqrt(ex[i] * ex[i] + ey[i] * ey[i]) + 1e-12; }
    double lineW = 1.3;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            double px = x + 0.5, py = y + 0.5, md = 1e9; bool in = true;
            for (int i = 0; i < n; i++) {
                double d = sgn * (ex[i] * (py - sy[i]) - ey[i] * (px - sx[i])) / el[i];
                if (d < 0) { in = false; break; }
                md = std::min(md, d);
            }
            if (!in) continue;
            double w = iw[a] + gx * (px - sx[a]) + gy * (py - sy[a]);
            size_t k = (size_t)y * im.W + x;
            if (w <= im.z[k]) continue;
            im.z[k] = (float)w;
            double dist = 1.0 / w;
            double edge = md < lineW ? 0.62 + 0.38 * std::min(1.0, dist / 90.0) : 1.0; // outlines fade with distance
            V3 o = col * edge;
            double fog = std::min(0.55, std::max(0.0, (dist - 40) / 160));
            V3 sky = { 0.72, 0.80, 0.88 };
            o = o * (1 - fog) + sky * fog;
            im.rgb[k * 3] = (float)o.x; im.rgb[k * 3 + 1] = (float)o.y; im.rgb[k * 3 + 2] = (float)o.z;
        }
}

static Stats Render(const std::function<void(const CellFn&)>& gen, const std::vector<Cam>& cams, std::vector<Img>& imgs) {
    Stats st;
    V3 sun = norm({ -0.45, 0.80, -0.40 });
    gen([&](const Cell& cell) {
        if (!Solid(cell.c)) return;
        st.cells++;
        double depth = Terrain(cell.c.x, cell.c.z) - cell.c.y;
        uint32_t h = Hash(cell.c);
        double jit = 0.94 + 0.12 * ((h & 255) / 255.0);
        for (auto& nc : cell.nbr) {
            if (Solid(nc)) continue;
            V3 d = nc - cell.c, dn = norm(d), mid = (cell.c + nc) * 0.5;
            double tol = 1e-6 * (1 + sqrt(dot(d, d)));
            std::vector<V3> f;
            for (auto& v : cell.verts) if (fabs(dot(v - mid, dn)) < tol * 1000) f.push_back(v);
            if (f.size() < 3) continue;
            V3 fc = { 0, 0, 0 };
            for (auto& v : f) fc = fc + v;
            fc = fc * (1.0 / f.size());
            V3 u = norm(f[0] - fc), vv = cross(dn, u);
            std::sort(f.begin(), f.end(), [&](const V3& p, const V3& q) {
                return atan2(dot(p - fc, vv), dot(p - fc, u)) < atan2(dot(q - fc, vv), dot(q - fc, u));
            });
            st.faces++; st.tris += (long)f.size() - 2;
            for (size_t i = 1; i + 1 < f.size(); i++) st.area += 0.5 * sqrt(dot(cross(f[i] - f[0], f[i + 1] - f[0]), cross(f[i] - f[0], f[i + 1] - f[0])));
            // Material by depth under the original ground.
            V3 base;
            if (dn.y > 0.35 && depth < 1.6) base = { 0.36, 0.55, 0.24 };
            else if (depth < 3.2) base = { 0.50, 0.38, 0.26 };
            else {
                double band = 0.5 + 0.5 * sin(cell.c.y * 0.9);
                base = V3{ 0.50, 0.50, 0.52 } * (0.85 + 0.15 * band);
            }
            base = base * jit;
            double lit = 0.42 + 0.58 * std::max(0.0, dot(dn, sun));
            lit *= 0.85 + 0.15 * dn.y;
            // Fake cave light: faces deep in the tunnel dim with distance from its mouth.
            V3 fp = fc + dn * 0.4;
            if (InTunnel(fp)) { double mouth = 4 + (8.5 + 3 - 3.0) / 0.42; double in = std::max(0.0, fp.z - mouth); lit *= std::max(0.25, 1 - in / 18); }
            V3 col = base * lit;
            for (size_t ci = 0; ci < cams.size(); ci++) {
                if (dot(dn, cams[ci].pos - fc) <= 0) continue; // back face
                DrawPoly(imgs[ci], cams[ci], f, col);
            }
        }
    });
    return st;
}

#ifndef SHAPES_NO_MAIN
int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    int SS = 2, W = 720, H = 480;
    std::vector<Cam> cams = {
        MakeCam({ 78, 44, -30 }, { 24, 8, 22 }, 46, W * SS, H * SS),     // overview
        MakeCam({ 31.5, 10.5, 7 }, { 26.5, 8.5, 24 }, 60, W * SS, H * SS),  // tunnel mouth close-up
        MakeCam({ 22, 30, 24 }, { 12, 14, 35 }, 55, W * SS, H * SS),      // looking into the pit
        MakeCam({ 40, Terrain(40, 3) + 4.0, 3 }, { 22, Terrain(22, 26) + 1.0, 26 }, 70, W * SS, H * SS), // cockpit height, ~4 m
    };
    const char* camNames[] = { "overview", "tunnel", "pit", "cockpit" };
    struct Shape { const char* id; std::function<void(const CellFn&)> gen; };
    double hexW1 = sqrt(2 / sqrt(3.0));      // hex prism of volume 1 at height 1
    double triS = sqrt(4 / sqrt(3.0));       // triangle of area 1 (volume 0.5 at height 0.5)
    std::vector<Shape> shapes = {
        { "cube", [](const CellFn& f) { Cubes(f); } },
        { "hex", [=](const CellFn& f) { Hexes(hexW1, 1.0, f); } },
        { "hexhalf", [=](const CellFn& f) { Hexes(hexW1, 0.5, f); } },
        { "tri", [=](const CellFn& f) { Triangles(triS, 0.5, f); } },
        { "rhombic", [](const CellFn& f) { LatticeCells(pow(2.0, -1.0 / 3), false, f); } },
        { "truncoct", [](const CellFn& f) { LatticeCells(pow(4.0, -1.0 / 3), true, f); } },
    };
    FILE* sf = fopen((std::string(out) + "/stats.txt").c_str(), "w");
    for (auto& s : shapes) {
        std::vector<Img> imgs(cams.size());
        for (auto& im : imgs) {
            im.W = W * SS; im.H = H * SS; im.z.assign((size_t)im.W * im.H, 0.0f); im.rgb.resize((size_t)im.W * im.H * 3);
            for (int y = 0; y < im.H; y++) for (int x = 0; x < im.W; x++) {
                double t = (double)y / im.H; size_t k = ((size_t)y * im.W + x) * 3;
                im.rgb[k] = (float)(0.55 + 0.17 * t); im.rgb[k + 1] = (float)(0.68 + 0.12 * t); im.rgb[k + 2] = (float)(0.86 + 0.02 * t);
            }
        }
        Stats st = Render(s.gen, cams, imgs);
        fprintf(sf, "%s cells=%ld faces=%ld tris=%ld area=%.1f\n", s.id, st.cells, st.faces, st.tris, st.area);
        printf("%s cells=%ld faces=%ld tris=%ld area=%.1f\n", s.id, st.cells, st.faces, st.tris, st.area);
        for (size_t ci = 0; ci < cams.size(); ci++) {
            std::vector<uint8_t> rgb((size_t)W * H * 3);
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) for (int ch = 0; ch < 3; ch++) {
                double acc = 0;
                for (int sy = 0; sy < SS; sy++) for (int sx = 0; sx < SS; sx++) acc += imgs[ci].rgb[(((size_t)(y * SS + sy)) * imgs[ci].W + x * SS + sx) * 3 + ch];
                acc /= SS * SS;
                rgb[((size_t)y * W + x) * 3 + ch] = (uint8_t)std::min(255.0, std::max(0.0, pow(acc, 1 / 1.1) * 255));
            }
            WritePNG((std::string(out) + "/" + s.id + "_" + camNames[ci] + ".png").c_str(), W, H, rgb);
        }
    }
    fclose(sf);
}
#endif
