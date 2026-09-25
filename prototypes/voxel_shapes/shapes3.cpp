// Round three of the voxel-shape study: faceted terrain done well.
// Same hillside as rounds one and two, now with blast craters (one through
// the tunnel roof), trees, rocks, a pulverized tree and a shattered rock.
// No outlines; per-facet colour by slope, sun shadows (a shadow map) and
// ambient occlusion read from the density field.
//   Six ways to cut the same density into facets:
//   kuhn      marching tetrahedra, cube grid split along its diagonal (round two)
//   (MarchBCC: tetrahedra on a body-centred grid -- evenly shaped, but 3.8x cubes' triangles; dropped)
//   nets      one vertex per cell, quads split on the shorter diagonal
//   netsj     the same, vertices slid along the ground by a fixed hash (hand-cut look)
//   nets15j   1.5 m cells, slid
//   nets2     2 m cells: larger facets
//   nets2j    2 m cells, nudged
#define SHAPES2_NO_MAIN
#include "shapes2.cpp"

struct Crater { V3 c; double r; double lift; }; // centre `lift` above the ground
static const Crater kCraters[] = {
    { { 33, 0, 12 }, 3.2, 0.6 },  // on the slope, in view of the cockpit
    { { 27, 0, 29 }, 4.2, -1.8 }, // deep enough to break into the tunnel
    { { 17, 0, 20 }, 2.2, 0.6 },
};
static V3 CraterAt(int i) { V3 c = kCraters[i].c; c.y = Terrain(c.x, c.z) + kCraters[i].lift; return c; }

static double Dens3(V3 p) {
    double d = Dens(p);
    for (int i = 0; i < 3; i++) { V3 q = p - CraterAt(i); d = std::min(d, sqrt(dot(q, q)) - kCraters[i].r); }
    return d;
}

struct F3 { std::vector<V3> v; V3 n; V3 col; bool terrain; };
static std::vector<F3> g_f3;

static void Add3(std::vector<V3> v, V3 outward, bool terrain, V3 col = { 0, 0, 0 }) {
    if (v.size() < 3) return;
    V3 n = cross(v[1] - v[0], v[2] - v[0]);
    if (dot(n, n) < 1e-14) return;
    n = norm(n);
    if (dot(outward, outward) > 0 && dot(n, outward) < 0) { std::reverse(v.begin(), v.end()); n = n * -1; }
    g_f3.push_back({ v, n, col, terrain });
}
static void AddQuad(V3 a, V3 b, V3 c, V3 d, V3 out) { // split on the shorter diagonal
    if (dot(a - c, a - c) <= dot(b - d, b - d)) { Add3({ a, b, c }, out, true); Add3({ a, c, d }, out, true); }
    else { Add3({ a, b, d }, out, true); Add3({ b, c, d }, out, true); }
}

static double H01(V3 p, uint32_t salt) { return ((Hash(p) ^ (salt * 2654435761u)) * 2246822519u >> 8) / 16777216.0; }

// ---- marching tetrahedra, one tet ----
static void Tet(const V3 v[4], const double d[4]) {
    int in = 0;
    for (int i = 0; i < 4; i++) if (d[i] > 0) in++;
    if (in == 0 || in == 4) return;
    auto cut = [&](int a, int b) { double t = d[a] / (d[a] - d[b]); return v[a] + (v[b] - v[a]) * t; };
    std::vector<int> I, O;
    V3 sc = { 0, 0, 0 }, ec = { 0, 0, 0 };
    for (int i = 0; i < 4; i++) { if (d[i] > 0) { I.push_back(i); sc = sc + v[i]; } else { O.push_back(i); ec = ec + v[i]; } }
    V3 out = ec * (1.0 / O.size()) - sc * (1.0 / I.size());
    if (I.size() == 1) Add3({ cut(I[0], O[0]), cut(I[0], O[1]), cut(I[0], O[2]) }, out, true);
    else if (I.size() == 3) Add3({ cut(O[0], I[0]), cut(O[0], I[1]), cut(O[0], I[2]) }, out, true);
    else AddQuad(cut(I[0], O[0]), cut(I[0], O[1]), cut(I[1], O[1]), cut(I[1], O[0]), out);
}
static void MarchKuhn() {
    int perm[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    for (int y = 0; y < SY; y++) for (int z = -1; z <= SZ; z++) for (int x = -1; x <= SX; x++)
        for (auto& p : perm) {
            V3 v[4]; double cur[3] = { (double)x, (double)y, (double)z }, d[4];
            v[0] = { cur[0], cur[1], cur[2] };
            for (int i = 0; i < 3; i++) { cur[p[i]] += 1; v[i + 1] = { cur[0], cur[1], cur[2] }; }
            for (int i = 0; i < 4; i++) d[i] = Dens3(v[i]);
            Tet(v, d);
        }
}
// Body-centred grid: corners plus cube centres. Each pair of face-adjacent
// centres and the 4 edges of their shared face make 4 congruent tetrahedra.
static void MarchBCC() {
    for (int y = -1; y < SY; y++) for (int z = -1; z <= SZ; z++) for (int x = -1; x <= SX; x++) {
        V3 A = { x + .5, y + .5, z + .5 };
        for (int ax = 0; ax < 3; ax++) {
            V3 e = { ax == 0 ? 1.0 : 0, ax == 1 ? 1.0 : 0, ax == 2 ? 1.0 : 0 };
            V3 u = { ax == 1 ? 1.0 : 0, ax == 2 ? 1.0 : 0, ax == 0 ? 1.0 : 0 };
            V3 w = cross(e, u);
            V3 fc = A + e * 0.5;
            V3 c[4] = { fc - u * .5 - w * .5, fc + u * .5 - w * .5, fc + u * .5 + w * .5, fc - u * .5 + w * .5 };
            V3 B = A + e;
            double dA = Dens3(A), dB = Dens3(B), dc[4];
            for (int i = 0; i < 4; i++) dc[i] = Dens3(c[i]);
            for (int i = 0; i < 4; i++) {
                V3 v[4] = { A, B, c[i], c[(i + 1) % 4] };
                double d[4] = { dA, dB, dc[i], dc[(i + 1) % 4] };
                Tet(v, d);
            }
        }
    }
}
// ---- surface nets at spacing h, vertices nudged by up to `jit` cells ----
static void Nets(double h, double jit) {
    int X0 = -1, X1 = (int)ceil(SX / h) + 1, Y0 = 0, Y1 = (int)ceil(SY / h), Z0 = -1, Z1 = (int)ceil(SZ / h) + 1;
    int NX = X1 - X0 + 1, NY = Y1 - Y0 + 1, NZ = Z1 - Z0 + 1;
    auto id = [&](int x, int y, int z) { return ((size_t)(y - Y0) * NZ + (z - Z0)) * NX + (x - X0); };
    std::vector<double> D((size_t)NX * NY * NZ);
    for (int y = Y0; y <= Y1; y++) for (int z = Z0; z <= Z1; z++) for (int x = X0; x <= X1; x++) D[id(x, y, z)] = Dens3(V3{ (double)x, (double)y, (double)z } * h);
    std::vector<V3> vert(D.size());
    std::vector<char> has(D.size(), 0);
    int ce[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (int y = Y0; y < Y1; y++) for (int z = Z0; z < Z1; z++) for (int x = X0; x < X1; x++) {
        double d[8]; V3 p[8];
        for (int k = 0; k < 8; k++) { int a = x + (k & 1), b = y + ((k >> 1) & 1), c = z + (k >> 2); d[k] = D[id(a, b, c)]; p[k] = V3{ (double)a, (double)b, (double)c } * h; }
        V3 acc = { 0, 0, 0 }; int n = 0;
        for (auto& e : ce) if ((d[e[0]] > 0) != (d[e[1]] > 0)) { double t = d[e[0]] / (d[e[0]] - d[e[1]]); acc = acc + p[e[0]] + (p[e[1]] - p[e[0]]) * t; n++; }
        if (!n) continue;
        V3 v = acc * (1.0 / n);
        if (jit > 0) {
            V3 key = { (double)x, (double)y, (double)z };
            V3 j = { H01(key, 1) - .5, H01(key, 2) - .5, H01(key, 3) - .5 };
            V3 lo = p[0], hi = p[7];
            V3 g = { Dens3(v + V3{ .05, 0, 0 }) - Dens3(v - V3{ .05, 0, 0 }), Dens3(v + V3{ 0, .05, 0 }) - Dens3(v - V3{ 0, .05, 0 }), Dens3(v + V3{ 0, 0, .05 }) - Dens3(v - V3{ 0, 0, .05 }) };
            if (dot(g, g) > 1e-12) { g = norm(g); j = j - g * dot(j, g); } // slide along the ground only: slopes (and so materials) keep
            v = v + j * (2 * jit * h);
            v = { std::min(std::max(v.x, lo.x), hi.x), std::min(std::max(v.y, lo.y), hi.y), std::min(std::max(v.z, lo.z), hi.z) }; // stay in the cell: no folds
        }
        vert[id(x, y, z)] = v; has[id(x, y, z)] = 1;
    }
    for (int y = Y0 + 1; y < Y1; y++) for (int z = Z0 + 1; z < Z1; z++) for (int x = X0 + 1; x < X1; x++)
        for (int ax = 0; ax < 3; ax++) {
            int dx = ax == 0, dy = ax == 1, dz = ax == 2;
            double a = D[id(x, y, z)], b = D[id(x + dx, y + dy, z + dz)];
            if ((a > 0) == (b > 0)) continue;
            int c[4][3];
            if (ax == 0) { int t[4][3] = { { x, y, z }, { x, y - 1, z }, { x, y - 1, z - 1 }, { x, y, z - 1 } }; memcpy(c, t, sizeof t); }
            else if (ax == 1) { int t[4][3] = { { x, y, z }, { x - 1, y, z }, { x - 1, y, z - 1 }, { x, y, z - 1 } }; memcpy(c, t, sizeof t); }
            else { int t[4][3] = { { x, y, z }, { x - 1, y, z }, { x - 1, y - 1, z }, { x, y - 1, z } }; memcpy(c, t, sizeof t); }
            V3 q[4]; bool ok = true;
            for (int i = 0; i < 4; i++) { size_t k = id(c[i][0], c[i][1], c[i][2]); if (!has[k]) { ok = false; break; } q[i] = vert[k]; }
            if (!ok) continue;
            V3 axis = { (double)dx, (double)dy, (double)dz };
            V3 out = a > 0 ? axis : axis * -1;
            if (dot(cross(q[2] - q[0], q[3] - q[1]), out) < 0) std::swap(q[1], q[3]);
            // Shorter diagonal, keeping the quad's winding for both halves.
            if (dot(q[0] - q[2], q[0] - q[2]) <= dot(q[1] - q[3], q[1] - q[3])) { Add3({ q[0], q[1], q[2] }, { 0, 0, 0 }, true); Add3({ q[0], q[2], q[3] }, { 0, 0, 0 }, true); }
            else { Add3({ q[0], q[1], q[3] }, { 0, 0, 0 }, true); Add3({ q[1], q[2], q[3] }, { 0, 0, 0 }, true); }
        }
}

// ---- props: trees, rocks, and what's left of them ----
static V3 Tint(V3 c, V3 key, double amt) { double t = H01(key, 7); return c * (1 - amt + 2 * amt * t); }
static void Cone(V3 base, double r, double hgt, int sides, V3 col, double rot) {
    V3 apex = base + V3{ 0, hgt, 0 };
    std::vector<V3> ring;
    for (int i = 0; i < sides; i++) { double a = rot + i * 2 * M_PI / sides; ring.push_back(base + V3{ r * cos(a), 0, r * sin(a) }); }
    for (int i = 0; i < sides; i++) {
        V3 a = ring[i], b = ring[(i + 1) % sides], m = (a + b + apex) * (1.0 / 3);
        Add3({ a, b, apex }, m - (base + V3{ 0, hgt * 0.3, 0 }), false, Tint(col, m, 0.08));
    }
    std::vector<V3> under(ring.rbegin(), ring.rend());
    Add3(under, { 0, -1, 0 }, false, col * 0.7);
}
static void Prism(V3 base, double r, double hgt, int sides, V3 col, double rot) {
    for (int i = 0; i < sides; i++) {
        double a0 = rot + i * 2 * M_PI / sides, a1 = rot + (i + 1) * 2 * M_PI / sides;
        V3 p0 = base + V3{ r * cos(a0), 0, r * sin(a0) }, p1 = base + V3{ r * cos(a1), 0, r * sin(a1) };
        V3 m = (p0 + p1) * 0.5;
        Add3({ p0, p1, p1 + V3{ 0, hgt, 0 }, p0 + V3{ 0, hgt, 0 } }, m - base, false, Tint(col, m, 0.06));
    }
    std::vector<V3> top;
    for (int i = 0; i < sides; i++) { double a = rot + i * 2 * M_PI / sides; top.push_back(base + V3{ r * cos(a), hgt, r * sin(a) }); }
    Add3(top, { 0, 1, 0 }, false, col * 1.15);
}
static void Tree(V3 g, double s, double rot) {
    V3 bark = { 0.36, 0.25, 0.17 }, leaf = { 0.20, 0.40, 0.20 };
    Prism(g - V3{ 0, 0.3, 0 }, 0.28 * s, 2.0 * s, 6, bark, rot);
    Cone(g + V3{ 0, 1.4 * s, 0 }, 1.7 * s, 2.6 * s, 7, leaf, rot);
    Cone(g + V3{ 0, 2.8 * s, 0 }, 1.25 * s, 2.3 * s, 7, leaf * 1.08, rot + 0.4);
}
static void Rock(V3 c, double r, uint32_t seed) {
    const double t = (1 + sqrt(5.0)) / 2;
    V3 iv[12] = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
    int fi[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                      { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    V3 w[12];
    for (int i = 0; i < 12; i++) {
        V3 u = norm(iv[i]);
        double k = 0.75 + 0.5 * H01(u + V3{ (double)seed, 0, 0 }, 11);
        w[i] = c + V3{ u.x * r * k, u.y * r * k * 0.62, u.z * r * k };
    }
    V3 stone = { 0.50, 0.49, 0.47 };
    for (auto& f : fi) { V3 m = (w[f[0]] + w[f[1]] + w[f[2]]) * (1.0 / 3); Add3({ w[f[0]], w[f[1]], w[f[2]] }, m - c, false, Tint(stone, m, 0.10)); }
}
static double Ground(double x, double z) { return Terrain(x, z); }
static void Debris(V3 at, double spread, int n, V3 col, double size, uint32_t seed, bool chunky) {
    for (int i = 0; i < n; i++) {
        V3 k = { (double)i, (double)seed, 3 };
        double a = H01(k, 21) * 2 * M_PI, r = spread * sqrt(H01(k, 22));
        double x = at.x + r * cos(a), z = at.z + r * sin(a);
        double sz = size * (0.5 + H01(k, 23));
        V3 p = { x, Ground(x, z) + 0.04, z };
        bool skip = false;
        for (int c = 0; c < 3; c++) { V3 q = p - CraterAt(c); if (dot(q, q) < kCraters[c].r * kCraters[c].r * 0.8) skip = true; } // nothing lies in the hole
        if (skip) continue;
        if (chunky) { Rock(p, sz, seed * 100 + i); continue; }
        double rot = H01(k, 24) * 2 * M_PI;
        V3 a0 = p + V3{ sz * cos(rot), 0.05 * H01(k, 25), sz * sin(rot) };
        V3 a1 = p + V3{ sz * cos(rot + 2.3), 0.12, sz * sin(rot + 2.3) };
        V3 a2 = p + V3{ sz * 0.6 * cos(rot + 4.1), 0.0, sz * 0.6 * sin(rot + 4.1) };
        Add3({ a0, a1, a2 }, { 0, 1, 0 }, false, Tint(col, p, 0.15));
    }
}
static bool NearHole(double x, double z, double pad) {
    for (int c = 0; c < 3; c++) { double dx = x - kCraters[c].c.x, dz = z - kCraters[c].c.z; if (dx * dx + dz * dz < (kCraters[c].r + pad) * (kCraters[c].r + pad)) return true; }
    if (fabs(x - 27) < 4.5 && z < 22) return true;                     // the cutting
    if ((x - 12) * (x - 12) + (z - 35) * (z - 35) < 6.5 * 6.5) return true; // the pit
    return false;
}
static void Props() {
    // Trees on a jittered 7 m grid, clear of holes and of the cockpit's nose.
    for (int gz = 0; gz < 7; gz++) for (int gx = 0; gx < 7; gx++) {
        V3 k = { (double)gx, (double)gz, 9 };
        double x = 3 + gx * 7 + H01(k, 1) * 4, z = 3 + gz * 7 + H01(k, 2) * 4;
        if (x > 46 || z > 46 || NearHole(x, z, 2.0)) continue;
        if ((x - 40) * (x - 40) + (z - 3) * (z - 3) < 8 * 8) continue;
        { double ax = 31, az = 23, bx = 27, bz = 30, t = ((x - ax) * (bx - ax) + (z - az) * (bz - az)) / ((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
          t = std::min(1.0, std::max(0.0, t)); double dx = x - (ax + t * (bx - ax)), dz = z - (az + t * (bz - az));
          if (dx * dx + dz * dz < 3.5 * 3.5) continue; } // the crater camera's view
        if (H01(k, 3) < 0.25) { Rock({ x, Ground(x, z) + 0.1, z }, 0.9 + 0.8 * H01(k, 4), gx * 10 + gz); continue; }
        Tree({ x, Ground(x, z), z }, 0.9 + 0.35 * H01(k, 5), H01(k, 6) * 2);
    }
    // What a blast leaves: a splintered stump and needles, a rock in pieces.
    V3 stump = { 29.2, Ground(29.2, 15.6), 15.6 };
    Prism(stump - V3{ 0, 0.3, 0 }, 0.3, 0.8, 6, { 0.36, 0.25, 0.17 }, 0.3);
    Debris(stump, 4.5, 60, { 0.20, 0.40, 0.20 }, 0.35, 5, false);
    Debris(stump, 3.5, 20, { 0.40, 0.29, 0.19 }, 0.30, 6, false);
    Debris({ 36.5, 0, 16.5 }, 3.0, 14, { 0.5, 0.5, 0.5 }, 0.22, 7, true);
    Rock({ 36.5, Ground(36.5, 16.5) - 0.1, 16.5 }, 0.5, 99);
}

// ---- terrain facet colour ----
static V3 TerrainColour(const F3& f, V3 fc) {
    double depth = Terrain(fc.x, fc.z) - fc.y, ny = f.n.y;
    V3 c;
    if (ny > 0.72 && depth < 1.6) {
        V3 g1 = { 0.33, 0.52, 0.22 }, g2 = { 0.42, 0.56, 0.25 };
        double t = H01(fc, 31);
        c = g1 * (1 - t) + g2 * t;
    } else if (ny < 0.42 || depth > 3.4) {
        c = V3{ 0.52, 0.51, 0.50 } * (0.88 + 0.12 * (0.5 + 0.5 * sin(fc.y * 0.9)));
        c = Tint(c, fc, 0.05);
    } else c = Tint(V3{ 0.50, 0.37, 0.25 }, fc, 0.06);
    for (int i = 0; i < 3; i++) { // scorch around the blasts, fading out past the rim
        V3 q = fc - CraterAt(i);
        double d = sqrt(dot(q, q)) - kCraters[i].r;
        if (d < 1.4) { double s = std::min(1.0, std::max(0.0, 1 - d / 1.4)); V3 soot = { 0.20, 0.17, 0.15 }; c = c * (1 - 0.4 * s) + soot * (0.4 * s); }
    }
    return c;
}

// ---- raster, shadows, lighting ----
template <class CB>
static void RasterPoly(int W, int H, const double* sx, const double* sy, const double* d, int n, CB cb) {
    double area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += sx[i] * sy[j] - sx[j] * sy[i]; }
    if (fabs(area) < 1e-9) return;
    double sgn = area > 0 ? 1 : -1;
    int a = 0, b = n / 3, c = (2 * n) / 3;
    if (n == 3) { b = 1; c = 2; }
    double det = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a]);
    if (fabs(det) < 1e-12) return;
    double gx = ((d[b] - d[a]) * (sy[c] - sy[a]) - (d[c] - d[a]) * (sy[b] - sy[a])) / det;
    double gy = ((sx[b] - sx[a]) * (d[c] - d[a]) - (sx[c] - sx[a]) * (d[b] - d[a])) / det;
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < n; i++) { minx = std::min(minx, sx[i]); maxx = std::max(maxx, sx[i]); miny = std::min(miny, sy[i]); maxy = std::max(maxy, sy[i]); }
    int x0 = std::max(0, (int)floor(minx)), x1 = std::min(W - 1, (int)ceil(maxx)), y0 = std::max(0, (int)floor(miny)), y1 = std::min(H - 1, (int)ceil(maxy));
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        double px = x + .5, py = y + .5; bool in = true;
        for (int i = 0; i < n && in; i++) { int j = (i + 1) % n; if (sgn * ((sx[j] - sx[i]) * (py - sy[i]) - (sy[j] - sy[i]) * (px - sx[i])) < 0) in = false; }
        if (in) cb(x, y, d[a] + gx * (px - sx[a]) + gy * (py - sy[a]));
    }
}

static const V3 kSun = norm(V3{ -0.45, 0.80, -0.40 });
struct ShadowMap {
    int N = 2048; double ext = 46; V3 O = { 24, 10, 24 }, su, sv;
    std::vector<float> depth;
    void Build() {
        su = norm(cross(kSun, { 0, 0, 1 })); sv = cross(su, kSun);
        depth.assign((size_t)N * N, -1e9f);
        for (auto& f : g_f3) {
            int n = (int)f.v.size(); double sx[16], sy[16], dd[16];
            for (int i = 0; i < n; i++) { V3 p = f.v[i] - O; sx[i] = (dot(p, su) / ext * 0.5 + 0.5) * N; sy[i] = (dot(p, sv) / ext * 0.5 + 0.5) * N; dd[i] = dot(p, kSun); }
            RasterPoly(N, N, sx, sy, dd, n, [&](int x, int y, double d) { float& t = depth[(size_t)y * N + x]; if (d > t) t = (float)d; });
        }
    }
    double Lit(V3 P) const { // 0..1, 3x3 filtered
        V3 p = P - O;
        double u = (dot(p, su) / ext * 0.5 + 0.5) * N, v = (dot(p, sv) / ext * 0.5 + 0.5) * N, d = dot(p, kSun);
        int cnt = 0, lit = 0;
        for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
            int x = (int)u + i, y = (int)v + j; cnt++;
            if (x < 0 || y < 0 || x >= N || y >= N) { lit++; continue; }
            if (depth[(size_t)y * N + x] <= d + 0.08) lit++;
        }
        return (double)lit / cnt;
    }
};

static double AO(V3 fc, V3 n) {
    double open = 0;
    const double s[3] = { 0.8, 1.8, 3.5 };
    for (double k : s) open += std::min(1.0, std::max(0.0, -Dens3(fc + n * k) / k));
    return 0.35 + 0.65 * open / 3;
}

static void Shade(std::vector<Img>& imgs, const std::vector<Cam>& cams, const ShadowMap& sm) {
    V3 sunCol = { 1.0, 0.95, 0.86 }, skyCol = { 0.80, 0.87, 1.0 };
    for (auto& f : g_f3) {
        V3 fc = { 0, 0, 0 };
        for (auto& p : f.v) fc = fc + p;
        fc = fc * (1.0 / f.v.size());
        V3 base = f.terrain ? TerrainColour(f, fc) : f.col;
        double ao = AO(fc, f.n), ndl = std::max(0.0, dot(f.n, kSun));
        V3 amb = { skyCol.x * base.x, skyCol.y * base.y, skyCol.z * base.z };
        amb = amb * (0.40 * ao * (0.8 + 0.2 * f.n.y));
        V3 dir = { sunCol.x * base.x, sunCol.y * base.y, sunCol.z * base.z };
        dir = dir * (0.78 * ndl);
        for (size_t ci = 0; ci < cams.size(); ci++) {
            const Cam& cam = cams[ci];
            if (dot(f.n, cam.pos - fc) <= 0) continue;
            int n = (int)f.v.size(); double sx[16], sy[16], iw[16]; bool ok = true;
            for (int i = 0; i < n; i++) {
                V3 d = f.v[i] - cam.pos; double z = dot(d, cam.fwd);
                if (z < 0.1) { ok = false; break; }
                sx[i] = cam.W / 2.0 + cam.f * dot(d, cam.right) / z; sy[i] = cam.H / 2.0 - cam.f * dot(d, cam.up) / z; iw[i] = 1 / z;
            }
            if (!ok) continue;
            Img& im = imgs[ci];
            RasterPoly(im.W, im.H, sx, sy, iw, n, [&](int x, int y, double w) {
                size_t k = (size_t)y * im.W + x;
                if (w <= im.z[k]) return;
                im.z[k] = (float)w;
                double z = 1 / w;
                V3 ray = cam.fwd + cam.right * ((x + .5 - cam.W / 2.0) / cam.f) + cam.up * (-(y + .5 - cam.H / 2.0) / cam.f);
                V3 P = cam.pos + ray * z;
                double sh = ndl > 0 ? sm.Lit(P + f.n * 0.05) : 0;
                V3 o = amb + dir * sh;
                double fog = std::min(0.5, std::max(0.0, (z - 45) / 170));
                V3 sky = { 0.72, 0.80, 0.88 };
                o = o * (1 - fog) + sky * fog;
                im.rgb[k * 3] = (float)o.x; im.rgb[k * 3 + 1] = (float)o.y; im.rgb[k * 3 + 2] = (float)o.z;
            });
        }
    }
}

#ifndef SHAPES3_NO_MAIN
int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    int SS = 2, W = 720, H = 480;
    std::vector<Cam> cams = {
        MakeCam({ 78, 44, -30 }, { 24, 8, 22 }, 46, W * SS, H * SS),
        MakeCam({ 40, Terrain(40, 3) + 4.0, 3 }, { 22, Terrain(22, 26) + 1.0, 26 }, 70, W * SS, H * SS),
        MakeCam({ 31, Terrain(31, 23) + 11, 23 }, { 27, Terrain(27, 29) - 5, 29.5 }, 55, W * SS, H * SS), // down into the roof hole
    };
    const char* camNames[] = { "overview", "cockpit", "crater" };
    struct Shape { const char* id; std::function<void()> gen; };
    std::vector<Shape> shapes = {
        { "kuhn", [] { MarchKuhn(); } },
        { "nets", [] { Nets(1.0, 0); } },
        { "netsj", [] { Nets(1.0, 0.18); } },
        { "nets15j", [] { Nets(1.5, 0.18); } },
        { "nets2", [] { Nets(2.0, 0); } },
        { "nets2j", [] { Nets(2.0, 0.18); } },
    };
    FILE* sf = fopen((std::string(out) + "/stats3.txt").c_str(), "w");
    for (auto& s : shapes) {
        g_f3.clear();
        s.gen();
        long tris = 0; for (auto& f : g_f3) tris += (long)f.v.size() - 2;
        Props();
        fprintf(sf, "%s tris=%ld\n", s.id, tris);
        printf("%s terrain tris=%ld\n", s.id, tris);
        ShadowMap sm; sm.Build();
        std::vector<Img> imgs(cams.size());
        for (auto& im : imgs) {
            im.W = W * SS; im.H = H * SS; im.z.assign((size_t)im.W * im.H, 0.0f); im.rgb.resize((size_t)im.W * im.H * 3);
            for (int y = 0; y < im.H; y++) for (int x = 0; x < im.W; x++) {
                double t = (double)y / im.H; size_t k = ((size_t)y * im.W + x) * 3;
                im.rgb[k] = (float)(0.55 + 0.17 * t); im.rgb[k + 1] = (float)(0.68 + 0.12 * t); im.rgb[k + 2] = (float)(0.86 + 0.02 * t);
            }
        }
        Shade(imgs, cams, sm);
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
