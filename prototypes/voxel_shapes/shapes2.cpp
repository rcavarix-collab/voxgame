// Round two of the voxel-shape study: pyramid-like cells and sloped surfaces.
// Same hillside, tunnel, pit, cameras and rasterizer as shapes.cpp.
//   pyramid   cube split into 6 square pyramids meeting at its centre
//   tet       cube split into 6 tetrahedra around its diagonal
//   tetoct    octahedra + tetrahedra honeycomb
//   sloped    blocks stored as blocks; the surface shapes into ramps and
//             corner pyramids (marching tetrahedra on block corners, cut at midpoints)
//   faceted   marching tetrahedra on a stored density (low-poly triangles)
//   smooth    surface nets on the same density (rounded quads)
#define SHAPES_NO_MAIN
#include "shapes.cpp"

// Signed density: positive inside ground. Its zero set is the same hillside.
static double Dens(V3 p) {
    double d = Terrain(p.x, p.z) - p.y;
    if (p.z > 4) { double dx = p.x - 27, dy = p.y - 8.5; d = std::min(d, sqrt(dx * dx + dy * dy) - 3.0); }
    double top = Terrain(12, 35), depth = top - p.y;
    if (depth > -4 && depth < 7) { double r = 4.6 - 0.25 * std::max(0.0, depth), dx = p.x - 12, dz = p.z - 35; d = std::min(d, sqrt(dx * dx + dz * dz) - r); }
    else if (depth >= 7) { double dx = p.x - 12, dz = p.z - 35; double r = 4.6 - 0.25 * 7; d = std::min(d, std::max(sqrt(dx * dx + dz * dz) - r, depth - 7)); }
    d = std::min(d, std::min(std::min(p.x, SX - p.x), std::min(p.z, SZ - p.z))); // closed sides
    return d;
}

struct Face { std::vector<V3> v; V3 n; double depth; };
static std::vector<Face> g_faces;

static void AddFace(std::vector<V3> v, V3 outward, double depth) {
    if (v.size() < 3) return;
    V3 n = cross(v[1] - v[0], v[2] - v[0]);
    for (size_t i = 3; i < v.size() && dot(n, n) < 1e-12; i++) n = cross(v[1] - v[0], v[i] - v[0]);
    if (dot(n, n) < 1e-14) return;
    n = norm(n);
    if (dot(outward, outward) > 0 && dot(n, outward) < 0) { std::reverse(v.begin(), v.end()); n = n * -1; }
    g_faces.push_back({ v, n, depth });
}

// ---- binary cells, hidden faces removed by pairing identical faces ----
struct Key { int64_t a, b, c; bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KeyHash { size_t operator()(const Key& k) const { return (size_t)(k.a * 1000003 ^ k.b * 999983 ^ k.c * 998244353); } };
static Key FaceKey(const std::vector<V3>& v) {
    // Order-independent: sums of vertex coordinates and of their squares.
    double sx = 0, sy = 0, sz = 0, q = 0;
    for (auto& p : v) { sx += p.x; sy += p.y; sz += p.z; q += p.x * p.x * 0.37 + p.y * p.y * 0.61 + p.z * p.z * 0.83; }
    return { (int64_t)llround(sx * 4096 + q * 64), (int64_t)llround(sy * 4096 + v.size()), (int64_t)llround(sz * 4096 - q * 16) };
}
struct Pending { std::vector<V3> v; V3 out; double depth; };
static std::unordered_map<Key, Pending, KeyHash> g_open;
static void BinaryCell(V3 c, const std::vector<std::vector<V3>>& faces) {
    if (!(Dens(c) > 0)) return;
    double depth = Terrain(c.x, c.z) - c.y;
    for (auto& f : faces) {
        Key k = FaceKey(f);
        auto it = g_open.find(k);
        if (it != g_open.end()) { g_open.erase(it); continue; } // shared by two solid cells: hidden
        V3 fc = { 0, 0, 0 };
        for (auto& p : f) fc = fc + p;
        fc = fc * (1.0 / f.size());
        g_open[k] = { f, fc - c, depth };
    }
}
static void FlushBinary() { for (auto& kv : g_open) AddFace(kv.second.v, kv.second.out, kv.second.depth); g_open.clear(); }

static void Pyramids() {
    for (int y = 0; y < SY; y++) for (int z = 0; z < SZ; z++) for (int x = 0; x < SX; x++) {
        V3 C = { x + .5, y + .5, z + .5 };
        for (int ax = 0; ax < 3; ax++) for (int s = -1; s <= 1; s += 2) {
            V3 n = { ax == 0 ? (double)s : 0, ax == 1 ? (double)s : 0, ax == 2 ? (double)s : 0 };
            V3 u = { ax == 1 ? 1.0 : 0, ax == 2 ? 1.0 : 0, ax == 0 ? 1.0 : 0 }, w = cross(n, u) * (double)s;
            w = { fabs(w.x), fabs(w.y), fabs(w.z) };
            V3 bc = C + n * .5;
            std::vector<V3> base = { bc - u * .5 - w * .5, bc + u * .5 - w * .5, bc + u * .5 + w * .5, bc - u * .5 + w * .5 };
            std::vector<std::vector<V3>> faces = { base };
            for (int i = 0; i < 4; i++) faces.push_back({ base[i], base[(i + 1) % 4], C });
            BinaryCell(bc + (C - bc) * 0.25, faces);
        }
    }
}
static void Tets() {
    int perm[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    for (int y = 0; y < SY; y++) for (int z = 0; z < SZ; z++) for (int x = 0; x < SX; x++)
        for (auto& p : perm) {
            double o[3] = { (double)x, (double)y, (double)z };
            V3 v[4]; v[0] = { o[0], o[1], o[2] };
            double cur[3] = { o[0], o[1], o[2] };
            for (int i = 0; i < 3; i++) { cur[p[i]] += 1; v[i + 1] = { cur[0], cur[1], cur[2] }; }
            V3 c = (v[0] + v[1] + v[2] + v[3]) * 0.25;
            BinaryCell(c, { { v[0], v[1], v[2] }, { v[0], v[1], v[3] }, { v[0], v[2], v[3] }, { v[1], v[2], v[3] } });
        }
}
static void TetOct() {
    for (int y = -1; y <= SY; y++) for (int z = -1; z <= SZ; z++) for (int x = -1; x <= SX; x++) {
        // A tetrahedron in every unit cube, on its even corners.
        std::vector<V3> e;
        for (int k = 0; k < 8; k++) { int a = x + (k & 1), b = y + ((k >> 1) & 1), c = z + (k >> 2); if (((a + b + c) & 1) == 0) e.push_back({ (double)a, (double)b, (double)c }); }
        V3 tc = (e[0] + e[1] + e[2] + e[3]) * 0.25;
        if (tc.x > 0 && tc.x < SX && tc.z > 0 && tc.z < SZ && tc.y > 0)
            BinaryCell(tc, { { e[0], e[1], e[2] }, { e[0], e[1], e[3] }, { e[0], e[2], e[3] }, { e[1], e[2], e[3] } });
        // An octahedron on every odd point.
        if (((x + y + z) & 1) == 1 && x > 0 && x < SX && z > 0 && z < SZ && y > 0) {
            V3 c = { (double)x, (double)y, (double)z };
            std::vector<std::vector<V3>> faces;
            for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) for (int sz = -1; sz <= 1; sz += 2)
                faces.push_back({ c + V3{ (double)sx, 0, 0 }, c + V3{ 0, (double)sy, 0 }, c + V3{ 0, 0, (double)sz } });
            BinaryCell(c, faces);
        }
    }
}

// ---- marching tetrahedra on a corner grid ----
static void MarchTets(bool interp) {
    int perm[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    auto S = [&](V3 p) { double d = Dens(p); return interp ? d : (d > 0 ? 1.0 : -1.0); };
    for (int y = 0; y < SY; y++) for (int z = -1; z <= SZ; z++) for (int x = -1; x <= SX; x++)
        for (auto& p : perm) {
            V3 v[4]; double o[3] = { (double)x, (double)y, (double)z }, cur[3] = { o[0], o[1], o[2] };
            v[0] = { o[0], o[1], o[2] };
            for (int i = 0; i < 3; i++) { cur[p[i]] += 1; v[i + 1] = { cur[0], cur[1], cur[2] }; }
            double d[4]; int in = 0;
            for (int i = 0; i < 4; i++) { d[i] = S(v[i]); if (d[i] > 0) in++; }
            if (in == 0 || in == 4) continue;
            auto cut = [&](int a, int b) { double t = interp ? d[a] / (d[a] - d[b]) : 0.5; return v[a] + (v[b] - v[a]) * t; };
            V3 sc = { 0, 0, 0 }, ec = { 0, 0, 0 }; int ns = 0, ne = 0;
            std::vector<int> I, O;
            for (int i = 0; i < 4; i++) { if (d[i] > 0) { I.push_back(i); sc = sc + v[i]; ns++; } else { O.push_back(i); ec = ec + v[i]; ne++; } }
            V3 out = ec * (1.0 / ne) - sc * (1.0 / ns);
            std::vector<V3> poly;
            if (I.size() == 1) poly = { cut(I[0], O[0]), cut(I[0], O[1]), cut(I[0], O[2]) };
            else if (I.size() == 3) poly = { cut(O[0], I[0]), cut(O[0], I[1]), cut(O[0], I[2]) };
            else poly = { cut(I[0], O[0]), cut(I[0], O[1]), cut(I[1], O[1]), cut(I[1], O[0]) };
            V3 fc = { 0, 0, 0 }; for (auto& q : poly) fc = fc + q; fc = fc * (1.0 / poly.size());
            if (poly.size() == 4) { AddFace({ poly[0], poly[1], poly[2] }, out, Terrain(fc.x, fc.z) - fc.y); AddFace({ poly[0], poly[2], poly[3] }, out, Terrain(fc.x, fc.z) - fc.y); }
            else AddFace(poly, out, Terrain(fc.x, fc.z) - fc.y);
        }
}

// ---- surface nets ----
static void SurfaceNets() {
    const int X0 = -1, X1 = (int)SX + 1, Y0 = 0, Y1 = (int)SY, Z0 = -1, Z1 = (int)SZ + 1;
    int NX = X1 - X0 + 1, NY = Y1 - Y0 + 1, NZ = Z1 - Z0 + 1;
    std::vector<double> D((size_t)NX * NY * NZ);
    auto id = [&](int x, int y, int z) { return ((size_t)(y - Y0) * NZ + (z - Z0)) * NX + (x - X0); };
    for (int y = Y0; y <= Y1; y++) for (int z = Z0; z <= Z1; z++) for (int x = X0; x <= X1; x++) D[id(x, y, z)] = Dens({ (double)x, (double)y, (double)z });
    std::vector<V3> vert((size_t)NX * NY * NZ);
    std::vector<char> has(vert.size(), 0);
    int ce[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (int y = Y0; y < Y1; y++) for (int z = Z0; z < Z1; z++) for (int x = X0; x < X1; x++) {
        double d[8]; V3 p[8];
        for (int k = 0; k < 8; k++) { int a = x + (k & 1), b = y + ((k >> 1) & 1), c = z + (k >> 2); d[k] = D[id(a, b, c)]; p[k] = { (double)a, (double)b, (double)c }; }
        V3 acc = { 0, 0, 0 }; int n = 0;
        for (auto& e : ce) if ((d[e[0]] > 0) != (d[e[1]] > 0)) { double t = d[e[0]] / (d[e[0]] - d[e[1]]); acc = acc + p[e[0]] + (p[e[1]] - p[e[0]]) * t; n++; }
        if (n) { vert[id(x, y, z)] = acc * (1.0 / n); has[id(x, y, z)] = 1; }
    }
    // A quad for every grid edge the surface crosses, joining the 4 cells around it.
    for (int y = Y0 + 1; y < Y1; y++) for (int z = Z0 + 1; z < Z1; z++) for (int x = X0 + 1; x < X1; x++)
        for (int ax = 0; ax < 3; ax++) {
            int dx = ax == 0, dy = ax == 1, dz = ax == 2;
            if (x + dx > X1 || y + dy > Y1 || z + dz > Z1) continue;
            double a = D[id(x, y, z)], b = D[id(x + dx, y + dy, z + dz)];
            if ((a > 0) == (b > 0)) continue;
            int c[4][3];
            if (ax == 0) { int t[4][3] = { { x, y, z }, { x, y - 1, z }, { x, y - 1, z - 1 }, { x, y, z - 1 } }; memcpy(c, t, sizeof t); }
            else if (ax == 1) { int t[4][3] = { { x, y, z }, { x - 1, y, z }, { x - 1, y, z - 1 }, { x, y, z - 1 } }; memcpy(c, t, sizeof t); }
            else { int t[4][3] = { { x, y, z }, { x - 1, y, z }, { x - 1, y - 1, z }, { x, y - 1, z } }; memcpy(c, t, sizeof t); }
            std::vector<V3> q; bool ok = true;
            for (auto& cc : c) { size_t k = id(cc[0], cc[1], cc[2]); if (!has[k]) { ok = false; break; } q.push_back(vert[k]); }
            if (!ok) continue;
            V3 axis = { (double)dx, (double)dy, (double)dz };
            V3 out = a > 0 ? axis : axis * -1;
            V3 fc = (q[0] + q[1] + q[2] + q[3]) * 0.25;
            double dep = Terrain(fc.x, fc.z) - fc.y;
            V3 qn = cross(q[2] - q[0], q[3] - q[1]);
            if (dot(qn, out) < 0) std::swap(q[1], q[3]);
            AddFace({ q[0], q[1], q[2] }, V3{ 0, 0, 0 }, dep);
            AddFace({ q[0], q[2], q[3] }, V3{ 0, 0, 0 }, dep);
        }
}

static void Draw(const std::vector<Cam>& cams, std::vector<Img>& imgs, bool smoothMat) {
    V3 sun = norm({ -0.45, 0.80, -0.40 });
    for (auto& f : g_faces) {
        V3 fc = { 0, 0, 0 };
        for (auto& p : f.v) fc = fc + p;
        fc = fc * (1.0 / f.v.size());
        double depth = smoothMat ? Terrain(fc.x, fc.z) - fc.y : f.depth;
        V3 base;
        if (f.n.y > (smoothMat ? 0.6 : 0.35) && depth < 1.6) base = { 0.36, 0.55, 0.24 };
        else if (depth < 3.2) base = { 0.50, 0.38, 0.26 };
        else base = V3{ 0.50, 0.50, 0.52 } * (0.85 + 0.15 * (0.5 + 0.5 * sin(fc.y * 0.9)));
        uint32_t h = Hash(smoothMat ? V3{ floor(fc.x), floor(fc.y), floor(fc.z) } : fc);
        base = base * ((smoothMat ? 0.97 : 0.94) + (smoothMat ? 0.06 : 0.12) * ((h & 255) / 255.0));
        double lit = 0.42 + 0.58 * std::max(0.0, dot(f.n, sun));
        lit *= 0.85 + 0.15 * f.n.y;
        V3 fp = fc + f.n * 0.4;
        if (InTunnel(fp)) { double mouth = 4 + (8.5 + 3 - 3.0) / 0.42; double in = std::max(0.0, fp.z - mouth); lit *= std::max(0.25, 1 - in / 18); }
        for (size_t ci = 0; ci < cams.size(); ci++) {
            if (dot(f.n, cams[ci].pos - fc) <= 0) continue;
            DrawPoly(imgs[ci], cams[ci], f.v, base * lit);
        }
    }
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    int SS = 2, W = 720, H = 480;
    std::vector<Cam> cams = {
        MakeCam({ 78, 44, -30 }, { 24, 8, 22 }, 46, W * SS, H * SS),
        MakeCam({ 31.5, 10.5, 7 }, { 26.5, 8.5, 24 }, 60, W * SS, H * SS),
        MakeCam({ 22, 30, 24 }, { 12, 14, 35 }, 55, W * SS, H * SS),
        MakeCam({ 40, Terrain(40, 3) + 4.0, 3 }, { 22, Terrain(22, 26) + 1.0, 26 }, 70, W * SS, H * SS), // cockpit height, ~4 m
    };
    const char* camNames[] = { "overview", "tunnel", "pit", "cockpit" };
    struct Shape { const char* id; std::function<void()> gen; bool smooth; };
    std::vector<Shape> shapes = {
        { "pyramid", [] { Pyramids(); FlushBinary(); }, false },
        { "tet", [] { Tets(); FlushBinary(); }, false },
        { "tetoct", [] { TetOct(); FlushBinary(); }, false },
        { "sloped", [] { MarchTets(false); }, true },
        { "faceted", [] { MarchTets(true); }, true },
        { "smooth", [] { SurfaceNets(); }, true },
    };
    FILE* sf = fopen((std::string(out) + "/stats2.txt").c_str(), "w");
    for (auto& s : shapes) {
        g_faces.clear();
        s.gen();
        long tris = 0; for (auto& f : g_faces) tris += (long)f.v.size() - 2;
        fprintf(sf, "%s faces=%zu tris=%ld\n", s.id, g_faces.size(), tris);
        printf("%s faces=%zu tris=%ld\n", s.id, g_faces.size(), tris);
        std::vector<Img> imgs(cams.size());
        for (auto& im : imgs) {
            im.W = W * SS; im.H = H * SS; im.z.assign((size_t)im.W * im.H, 0.0f); im.rgb.resize((size_t)im.W * im.H * 3);
            for (int y = 0; y < im.H; y++) for (int x = 0; x < im.W; x++) {
                double t = (double)y / im.H; size_t k = ((size_t)y * im.W + x) * 3;
                im.rgb[k] = (float)(0.55 + 0.17 * t); im.rgb[k + 1] = (float)(0.68 + 0.12 * t); im.rgb[k + 2] = (float)(0.86 + 0.02 * t);
            }
        }
        Draw(cams, imgs, s.smooth);
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
