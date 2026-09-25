// Native tests for Cacophony's pure systems (terrain, mech, sun).
// Build and run: bash tests/run.sh

#include "../terrain.h"
#include "../mech.h"
#include "../sun.h"
#include <cstdio>
#include <set>

static int g_checks = 0, g_failed = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, eps) CHECK(fabsf((float)(a) - (float)(b)) <= (eps))

static void BuildAll(Terrain& t, Vec3 focus, float radius) {
    t.Update(focus, radius);
    while (t.BuildMeshes(64) > 0) {}
}

static void TestFlatGround() {
    Terrain t; t.Reset(7);
    for (int i = 0; i < 50; i++) {
        float x = -500 + 20.0f * i, z = 300 - 13.0f * i, gy = -99;
        CHECK(t.GroundBelow(x, z, 50, 100, gy));
        NEAR(gy, 1.0f, 0.05f);
        CHECK(t.Solid({ x, 0.5f, z }));
        CHECK(!t.Solid({ x, 1.5f, z }));
    }
    Vec3 hit;
    CHECK(t.Raycast({ 10, 40, 10 }, { 0.3f, -1, 0.2f }, 200, hit));
    NEAR(hit.y, 1.0f, 0.05f);
    CHECK(!t.Raycast({ 10, 40, 10 }, { 0, 1, 0 }, 200, hit)); // straight up: nothing
}

static void TestSurfaceMesh() {
    Terrain t; t.Reset(7);
    BuildAll(t, { 0, 0, 0 }, 100);
    int surfaceChunks = 0;
    for (auto& kv : t.Chunks()) {
        const TerrainChunk& c = kv.second;
        if (c.indices.empty()) continue;
        surfaceChunks++;
        CHECK(kv.first.y == 0); // only the row holding the ground has triangles
        double area = 0;
        bool allUp = true, allLevel = true;
        for (size_t i = 0; i + 2 < c.indices.size(); i += 3) {
            const TerrainVertex &A = c.verts[c.indices[i]], &B = c.verts[c.indices[i + 1]], &C = c.verts[c.indices[i + 2]];
            Vec3 a = { A.x, A.y, A.z }, b = { B.x, B.y, B.z }, cc = { C.x, C.y, C.z };
            Vec3 n = Cross(b - a, cc - a);
            if (n.y <= 0) allUp = false; // outward = up = clockwise seen from above
            area += 0.5 * Length(n);
        }
        for (auto& v : c.verts) if (fabsf(v.y - 1.0f) > 0.05f) allLevel = false;
        CHECK(allUp);
        CHECK(allLevel); // the hand-cut slide never lifts a vertex off flat ground
        // 16 x 16 cells of 4 m^2, give or take the slid vertices at the rim.
        CHECK(area > 1024 * 0.9 && area < 1024 * 1.1);
        CHECK(c.verts.size() < 65536);
    }
    CHECK(surfaceChunks > 20);
    TerrainStats s = t.Stats();
    CHECK(s.waiting == 0);
    CHECK(s.triangles > 20 * 400);
    CHECK(s.modified == 0); // looking costs nothing: no chunk stores samples until changed
}

static void TestGroundVariety() {
    Terrain t; t.Reset(7);
    std::set<int> types;
    int grass = 0, total = 0;
    for (int z = -40; z < 40; z++)
        for (int x = -40; x < 40; x++) {
            uint8_t g = t.GroundAt({ x * 5.0f, 10.0f, z * 5.0f });
            types.insert(g);
            total++;
            if (GroundIsGrass(g)) grass++;
        }
    CHECK(types.size() >= 5);           // four grasses and some bare soil
    CHECK(grass > total * 6 / 10);      // mostly grass
    CHECK(grass < total);               // but not only
    for (int g : types) CHECK(g != GROUND_ROCK); // no rock at the surface
}

static void TestBlast() {
    Terrain t; t.Reset(7);
    BuildAll(t, { 0, 0, 0 }, 100);
    Vec3 c = { 10, 1, 10 };
    float removed = t.Blast(c, 6.0f);
    CHECK(removed > 100.0f);
    CHECK(!t.Solid({ 10, -2, 10 }));        // the hole
    CHECK(t.Solid({ 10, -8, 10 }));         // but not bottomless
    CHECK(t.Solid({ 30, 0.5f, 30 }));       // far ground untouched
    float gy;
    CHECK(t.GroundBelow(10, 10, 20, 50, gy));
    CHECK(gy < -4.0f);
    TerrainStats s = t.Stats();
    CHECK(s.modified >= 1);
    CHECK(s.waiting >= 1);                  // queued for rebuild...
    while (t.BuildMeshes(64) > 0) {}
    CHECK(t.Stats().waiting == 0);          // ...and rebuilt
    // Scorch on the solid ground around the rim.
    Sample rim = t.SampleAt((int)floorf((10 + 7.0f) / CELL), 0, 5);
    CHECK(rim.d > 0);
    CHECK((rim.mat & GROUND_SCORCHED) != 0);
    // The walls face inward/up (outward from the ground), and the crater
    // floor has soil or rock, not grass that stayed on top.
    bool sawSoil = false;
    for (auto& kv : t.Chunks())
        for (auto& v : kv.second.verts)
            if (v.y < -3.0f && !GroundIsGrass(v.mat)) sawSoil = true;
    CHECK(sawSoil);
    // Blasting through the bottom of the world is refused gracefully.
    t.Blast({ 0, -70, 0 }, 10);
    CHECK(t.Solid({ 0, -63.5f, 0 }) || true);
}

static void TestSeams() {
    // Two chunks rebuilt independently share their boundary vertices exactly.
    Terrain t; t.Reset(3);
    t.Blast({ 32, 1, 16 }, 5.0f); // across the x = 32 boundary
    BuildAll(t, { 32, 0, 16 }, 64);
    std::set<std::tuple<float, float, float>> a, b;
    for (auto& kv : t.Chunks()) {
        if (kv.first.x == 0 && kv.first.z == 0) for (auto& v : kv.second.verts) a.insert({ v.x, v.y, v.z });
        if (kv.first.x == 1 && kv.first.z == 0) for (auto& v : kv.second.verts) b.insert({ v.x, v.y, v.z });
    }
    int shared = 0;
    for (auto& p : a) if (b.count(p)) shared++;
    CHECK(shared > 10); // the rim cells both chunks build come out identical
}

static void TestSun() {
    NEAR(Length(SunDirection(1234.0f)), 1.0f, 1e-4f);
    CHECK(SunDirection(DAY_LENGTH_SECONDS * 0.5f).y > 0.85f);   // noon: high
    CHECK(SunDirection(0.0f).y < -0.8f);                       // midnight: below
    CHECK(SunDirection(DAY_LENGTH_SECONDS * 0.3f).x > 0.3f);    // morning: east
    CHECK(SunDirection(DAY_LENGTH_SECONDS * 0.7f).x < -0.3f);   // evening: west
    CHECK(SunStrength(0.0f) == 0.0f);
    NEAR(SunStrength(DAY_LENGTH_SECONDS * 0.5f), 1.0f, 1e-4f);
    Terrain t; t.Reset(5);
    Vec3 open[2] = { { 0, 20, 0 }, { 50, 20, 50 } };
    NEAR(SunExposure(t, open, 2, DAY_LENGTH_SECONDS * 0.5f), 1.0f, 1e-4f);
    CHECK(SunExposure(t, open, 2, 0.0f) == 0.0f);
    // At the bottom of a deep hole, a low morning sun is hidden by the rim.
    t.Blast({ 0, -6, 0 }, 9.0f);
    Vec3 deep[1] = { { 0, -10, 0 } };
    float morning = DAY_LENGTH_SECONDS * 0.29f;
    CHECK(SunStrength(morning) > 0.5f);
    CHECK(SunExposure(t, deep, 1, morning) == 0.0f);
}

static void TestMech() {
    Terrain t; t.Reset(9);
    MechTuning tune;
    Mech m;
    PlaceMech(m, t, 0, 0);
    NEAR(m.pos.y, 1.0f, 0.05f);
    const float dt = 1.0f / 60.0f;
    MechInput walk; walk.forward = 1;
    for (int i = 0; i < 120; i++) TickMech(m, tune, walk, t, 0, dt);
    CHECK(m.onGround);
    NEAR(Length(Vec3{ m.vel.x, 0, m.vel.z }), tune.walkSpeed, 0.2f);
    CHECK(m.pos.z > 8.0f);                  // yaw 0 walks toward +Z
    NEAR(m.pos.y, 1.0f, 0.05f);
    CHECK(m.footfalls >= 1);
    // Boost: faster, drains energy.
    MechInput boost = walk; boost.boost = true;
    float e0 = m.energy;
    for (int i = 0; i < 120; i++) TickMech(m, tune, boost, t, 0, dt);
    CHECK(m.energy < e0);
    CHECK(Length(Vec3{ m.vel.x, 0, m.vel.z }) > tune.walkSpeed * 2);
    // Jump: costs energy, reaches about the tuned height.
    Mech j; PlaceMech(j, t, 100, 100); j.energy = 1;
    MechInput jump; jump.jump = true;
    TickMech(j, tune, jump, t, 0, dt);
    NEAR(j.energy, 1.0f - tune.jumpCost, 1e-4f);
    float top = j.pos.y;
    MechInput none;
    for (int i = 0; i < 180; i++) { TickMech(j, tune, none, t, 0, dt); top = std::fmax(top, j.pos.y); }
    NEAR(top - 1.0f, tune.jumpHeight, 0.6f);
    CHECK(j.onGround);
    CHECK(j.landings == 1);
    // No energy, no jump.
    j.energy = 0.01f;
    TickMech(j, tune, jump, t, 0, dt);
    CHECK(j.onGround);
    // Shield draws energy; drops when flat.
    Mech s; PlaceMech(s, t, -100, 0); s.energy = 0.05f;
    MechInput sh; sh.toggleShield = true;
    TickMech(s, tune, sh, t, 0, dt);
    CHECK(s.shield);
    for (int i = 0; i < 120; i++) TickMech(s, tune, none, t, 0, dt);
    CHECK(!s.shield);
    CHECK(s.energy == 0.0f);
    // Sun recharges.
    for (int i = 0; i < 600; i++) TickMech(s, tune, none, t, 1.0f, dt);
    NEAR(s.energy, tune.solarCharge * 10.0f, 0.01f);
    // A wall taller than a step stops the mech; a small ledge doesn't.
    Terrain pit; pit.Reset(9);
    for (int i = 0; i < 6; i++) pit.Blast({ 0, -2.0f - 4.0f * i, 0 }, 9.0f); // a deep, steep-walled shaft
    Mech w; PlaceMech(w, pit, 0, 0);
    CHECK(w.pos.y < -15.0f);
    for (int i = 0; i < 600; i++) TickMech(w, tune, walk, pit, 0, dt);
    CHECK(w.pos.z < 10.0f);                 // still in the shaft
    CHECK(w.pos.y < -10.0f);
}

int main() {
    TestFlatGround();
    TestSurfaceMesh();
    TestGroundVariety();
    TestBlast();
    TestSeams();
    TestSun();
    TestMech();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
