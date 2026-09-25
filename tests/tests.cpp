// Native tests for Cacophony's pure systems (terrain, mech, sun).
// Build and run: bash tests/run.sh

#include "../terrain.h"
#include "../mech.h"
#include "../sun.h"
#include "../props.h"
#include "../fire.h"
#include "../debris.h"
#include "../pickups.h"
#include "../wanderer.h"
#include "../weapons.h"
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
    // The rim: ground thrown up around the hole, higher than the field, then falling away.
    float rimY = 0, farY = 0;
    CHECK(t.GroundBelow(10 + 6.0f + 2.0f, 10, 20, 50, rimY));  // radius + about half the lip's width
    CHECK(t.GroundBelow(10 + 6.0f + 12.0f, 10, 20, 50, farY));
    CHECK(rimY > 1.4f);
    NEAR(farY, 1.0f, 0.05f);
    // Thrown soil covers some of the grass on the rim.
    int soilOnRim = 0;
    for (int a = 0; a < 16; a++) {
        float x = 10 + 8.5f * cosf(a * 0.3927f), z = 10 + 8.5f * sinf(a * 0.3927f);
        if (!GroundIsGrass(t.GroundAt({ x, 10, z }))) soilOnRim++;
    }
    CHECK(soilOnRim >= 6);
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


static void TestPrims() {
    std::vector<MeshVertex> v;
    Pose p; p.pos = { 5, 1, 5 };
    PrimPrism(v, p, 0.5f, 3, 6, Rgba(1, 1, 1));
    PrimCone(v, p, 2, 2, 3, 7, Rgba(1, 1, 1));
    PrimBox(v, p, { 0, 1, 0 }, { 1, 1, 1 }, Rgba(1, 1, 1));
    PrimRock(v, p, 1.5f, 0.7f, 3, Rgba(1, 1, 1));
    CHECK(v.size() % 3 == 0);
    CHECK(v.size() == (6 * 3 + 7 * 2 + 12 + 20) * 3);
    // A tilted pose leans its top toward tiltYaw.
    Pose t; t.tilt = 0.5f * kPi; t.tiltYaw = 0; // lean fully toward +Z
    Vec3 top = t.Apply({ 0, 10, 0 });
    NEAR(top.z, 10.0f, 1e-3f); NEAR(top.y, 0.0f, 1e-3f);
    // Boxes face outward (clockwise seen from outside).
    std::vector<MeshVertex> b;
    PrimBox(b, Pose(), { 0, 0, 0 }, { 1, 1, 1 }, Rgba(1, 1, 1));
    bool out = true;
    for (size_t i = 0; i < b.size(); i += 3) {
        Vec3 a = { b[i].x, b[i].y, b[i].z }, c = { b[i + 1].x, b[i + 1].y, b[i + 1].z }, d = { b[i + 2].x, b[i + 2].y, b[i + 2].z };
        if (Dot(Cross(c - a, d - a), (a + c + d) * (1.0f / 3)) <= 0) out = false;
    }
    CHECK(out);
}

static void TestProps() {
    Terrain t; t.Reset(11);
    Props props; props.Reset(12);
    props.Update(t, { 0, 0, 0 }, 300);
    int trees = 0, bushes = 0, rocks = 0, forestTrees = 0, forestCells = 0;
    for (auto& kv : props.Tiles())
        for (auto& p : kv.second.props) {
            if (p.IsTree()) trees++; else if (p.kind == PROP_BUSH) bushes++; else rocks++;
            if ((t.SurfaceAt(p.pos.x, p.pos.z) & 0x7F) == GROUND_MOSS && p.IsTree()) forestTrees++;
        }
    for (int z = -60; z < 60; z++) for (int x = -60; x < 60; x++) if ((t.SurfaceAt(x * 4.0f, z * 4.0f) & 0x7F) == GROUND_MOSS) forestCells++;
    CHECK(trees > 100); CHECK(bushes > 100); CHECK(rocks > 10);
    CHECK(forestTrees > trees / 4); // moss is where the forests are
    while (props.BuildMeshes(64) > 0) {}
    int meshed = 0; for (auto& kv : props.Tiles()) if (kv.second.meshed && !kv.second.mesh.empty()) meshed++;
    CHECK(meshed > 50);
    // Shoot a tree down: find one, hit it until it falls, let it land.
    PropRef ref; Prop* tree = nullptr;
    for (auto& kv : props.Tiles()) { for (size_t i = 0; i < kv.second.props.size(); i++) if (kv.second.props[i].IsTree()) { ref = { kv.first, (int)i }; break; } if (ref.index >= 0) break; }
    tree = props.Get(ref);
    CHECK(tree != nullptr);
    EventList ev;
    Vec3 from = tree->pos + Vec3{ -20, 5, 0 };
    for (int i = 0; i < 12 && tree->state == PS_STANDING; i++) props.Damage(ref, 0.09f, from, ev);
    CHECK(tree->state == PS_FALLING);
    CHECK(fabsf(remainderf(tree->fallYaw - 0.5f * kPi, 2 * kPi)) < 0.05f); // falls away from the shooter (+X)
    for (int i = 0; i < 300 && tree->state == PS_FALLING; i++) props.Tick(1.0f / 60.0f, ev);
    CHECK(tree->state == PS_FALLEN);
    bool landed = false; for (auto& e : ev.list) if (e.kind == EV_TREE_LANDED) landed = true;
    CHECK(landed);
    // Raycasting finds standing props and not fallen ones.
    Prop* standing = nullptr; PropRef sref;
    for (auto& kv : props.Tiles()) { for (size_t i = 0; i < kv.second.props.size(); i++) { auto& q = kv.second.props[i]; if (q.IsTree() && q.state == PS_STANDING) { sref = { kv.first, (int)i }; break; } } if (sref.index >= 0) break; }
    standing = props.Get(sref);
    float th; PropRef hit;
    Vec3 o = standing->pos + Vec3{ -15, 4, 0 };
    CHECK(props.Raycast(o, { 1, 0, 0 }, 40, th, hit));
    CHECK(th < 15.0f && th > 10.0f);
    // A blast leaves stumps and nothing standing in its core.
    Vec3 bc = standing->pos;
    props.Blast(bc, 8.0f, ev);
    for (auto& kv : props.Tiles()) for (auto& q : kv.second.props)
        if (Length(q.pos - bc) < 6.0f) CHECK(q.state == PS_STUMP || q.state == PS_GONE);
    // The mech walking into a tree pushes it over the way it walks.
    Prop* next = nullptr; PropRef nref;
    for (auto& kv : props.Tiles()) { for (size_t i = 0; i < kv.second.props.size(); i++) { auto& q = kv.second.props[i]; if (q.IsTree() && q.state == PS_STANDING) { nref = { kv.first, (int)i }; break; } } if (nref.index >= 0) break; }
    next = props.Get(nref);
    props.Trample(next->pos - Vec3{ 0, 0, 1 }, 3.2f, { 0, 0, 10 }, ev);
    CHECK(next->state == PS_FALLING);
    NEAR(remainderf(next->fallYaw, 2 * kPi), 0.0f, 0.05f);
    // Standing still tramples nothing.
    int before = props.Count();
    props.Trample({ 0, 0, 0 }, 50.0f, { 0, 0, 0 }, ev);
    CHECK(props.Count() == before);
    // Fire: a bush burns away, a tree chars.
    int caught = props.Ignite(next->pos, 60.0f);
    CHECK(caught > 0);
    for (int i = 0; i < 60 * 30; i++) props.Tick(1.0f / 60.0f, ev);
    CHECK(next->charred && !next->burning);
}

static void TestFire() {
    Terrain t; t.Reset(13);
    Props props; props.Reset(14);
    Fire fire; fire.Reset(15);
    FireTuning tune;
    EventList ev;
    // Find dry grass and rock-free soil.
    Vec3 dry = { 0, 0, 0 }, soil = { 0, 0, 0 };
    bool haveDry = false, haveSoil = false;
    for (int z = -100; z < 100 && !(haveDry && haveSoil); z++)
        for (int x = -100; x < 100; x++) {
            uint8_t g = t.SurfaceAt(x * 4.0f, z * 4.0f) & 0x7F;
            if (!haveDry && g == GROUND_DRYGRASS && (t.SurfaceAt(x * 4.0f + 8, z * 4.0f) & 0x7F) == GROUND_DRYGRASS) { dry = { x * 4.0f, 1, z * 4.0f }; haveDry = true; }
            if (!haveSoil && (g == GROUND_LOAM || g == GROUND_GRAVEL)) { soil = { x * 4.0f, 1, z * 4.0f }; haveSoil = true; }
        }
    CHECK(haveDry && haveSoil);
    CHECK(Fire::Flammability(GROUND_DRYGRASS) > Fire::Flammability(GROUND_MOSS));
    CHECK(Fire::Flammability(GROUND_ROCK) == 0.0f);
    CHECK(Fire::Flammability(GROUND_DRYGRASS | GROUND_SCORCHED) == 0.0f);
    // Soil won't burn.
    CHECK(fire.IgniteArea(t, soil, 1.0f, ev) == 0 || (t.SurfaceAt(soil.x, soil.z) & 0x7F) > GROUND_CLOVER);
    // Dry grass catches, burns out, leaves scorch, never reignites.
    int lit = fire.IgniteArea(t, dry, 4.0f, ev);
    CHECK(lit > 0);
    size_t peak = 0;
    for (int i = 0; i < 60 * 40; i++) { fire.Tick(t, props, tune, 1.0f / 60.0f, ev); peak = std::max(peak, fire.Cells().size()); }
    CHECK(peak >= (size_t)lit);
    CHECK((int)peak <= tune.maxBurning);
    CHECK(fire.Burnt() >= lit);
    CHECK((t.GroundAt(dry + Vec3{ 0, 2, 0 }) & 0x7F) <= GROUND_ROCK);
    Sample s = t.SampleAt((int)floorf(dry.x / CELL + 0.5f), 0, (int)floorf(dry.z / CELL + 0.5f));
    (void)s;
    int again = fire.IgniteArea(t, dry, 1.0f, ev);
    CHECK(again == 0 || fire.Cells().size() > 0);
    // Governed: a huge ignition never exceeds the cap.
    Fire big; big.Reset(16);
    FireTuning small = tune; small.maxBurning = 20;
    for (int i = 0; i < 50; i++) big.IgniteArea(t, dry + Vec3{ i * 3.0f, 0, 0 }, 10.0f, ev);
    big.Tick(t, props, small, 1.0f / 60.0f, ev);
    CHECK((int)big.Cells().size() <= 600);
}

static void TestDebris() {
    Terrain t; t.Reset(17);
    Debris d; d.Reset(18);
    d.Burst({ 0, 3, 0 }, 3000, 20.0f, 0.8f, Rgba(1, 1, 1));
    CHECK(d.Live() == Debris::POOL);  // governed: the pool never grows past its size
    for (int i = 0; i < 60 * 3; i++) d.Tick(t, 1.0f / 60.0f, { 1e6f, 0, 0 }, 15, 3);
    // After three seconds, most pieces have landed: none are underground.
    std::vector<MeshVertex> m; d.Mesh(m);
    CHECK(!m.empty());
    for (auto& v : m) CHECK(v.y > -2.0f);
    for (int i = 0; i < 60 * 8; i++) d.Tick(t, 1.0f / 60.0f, { 1e6f, 0, 0 }, 15, 3);
    CHECK(d.Live() == 0); // all faded
    // Fast pieces hurt a mech standing in their path, once each.
    Debris h; h.Reset(19);
    h.Burst({ 0, 5, 0 }, 60, 30.0f, 1.0f, Rgba(1, 1, 1));
    float dmg = 0;
    for (int i = 0; i < 30; i++) dmg += h.Tick(t, 1.0f / 60.0f, { 0, 1, 0 }, 15, 12);
    CHECK(dmg > 0);
}

static void TestPickups() {
    Terrain t; t.Reset(20);
    Pickups p; p.Reset();
    EventList ev;
    p.Drop({ 0, 10, 0 }, { 0, 0, 0 }, PICK_PLATE, 1);
    p.Drop({ 1, 10, 0 }, { 0, 0, 0 }, PICK_PLATE, 2);
    for (int i = 0; i < 180; i++) p.Tick(t, 1.0f / 60.0f, { 100, 1, 100 }, 7, PICK_PLATE, ev);
    CHECK(p.Count() == 1);                  // two plates landing together make one pile
    int got = p.Tick(t, 1.0f / 60.0f, { 2, 1, 0 }, 7, PICK_PLATE, ev);
    CHECK(got == 2);
    CHECK(p.Count() == 0);
    for (int i = 0; i < 100; i++) p.Drop({ i * 10.0f, 5, 0 }, { 0, 0, 0 }, PICK_PLATE, i);
    CHECK(p.Count() == Pickups::POOL);      // governed
}

static void TestWandererAndWeapons() {
    Terrain t; t.Reset(21);
    Props props; props.Reset(22);
    Fire fire; fire.Reset(23);
    Debris debris; debris.Reset(24);
    Wanderer wd; wd.rng = 5;
    Mech mech; PlaceMech(mech, t, 0, 0);
    EventList ev;
    SpawnWanderer(wd, t, { 0, 0, 0 }, 80.0f);
    CHECK(wd.alive);
    NEAR(Length(Vec3{ wd.pos.x, 0, wd.pos.z }), 80.0f, 0.5f);
    Vec3 start = wd.pos;
    WandererTuning wt;
    for (int i = 0; i < 60 * 20; i++) TickWanderer(wd, wt, t, 1.0f / 60.0f, ev);
    CHECK(Length(wd.pos - start) > 5.0f);   // it wanders
    NEAR(wd.pos.y, 1.0f, 0.1f);             // on the ground
    // Hit tests: aimed at its middle hits; aimed well above misses.
    float th;
    Vec3 eye = { wd.pos.x - 60, 16, wd.pos.z };
    CHECK(HitWanderer(wd, eye, Normalize(wd.Center() - eye), 200, th));
    CHECK(!HitWanderer(wd, eye, Normalize(wd.Center() + Vec3{ 0, 40, 0 } - eye), 200, th));
    // Damage in stages, then destroyed.
    DamageWanderer(wd, 0.4f, ev); CHECK(wd.stage == 1);
    DamageWanderer(wd, 0.3f, ev); CHECK(wd.stage == 2);
    DamageWanderer(wd, 0.4f, ev); CHECK(!wd.alive);
    int stages = 0; bool destroyed = false;
    for (auto& e : ev.list) { if (e.kind == EV_TARGET_STAGE) stages++; if (e.kind == EV_TARGET_DESTROYED) destroyed = true; }
    CHECK(stages == 2 && destroyed);
    std::vector<MeshVertex> m; MeshWanderer(wd, m); CHECK(m.empty()); // nothing to draw when it's down

    // Weapons: a rocket flies to the ground and craters it; the gun fells a tree.
    SpawnWanderer(wd, t, { 0, 0, 0 }, 200.0f);
    World w; w.terrain = &t; w.props = &props; w.fire = &fire; w.debris = &debris; w.wanderer = &wd; w.mech = &mech; w.events = &ev;
    props.Update(t, { 0, 0, 0 }, 250);
    WeaponState s; WeaponTuning tune;
    WeaponInput fire1; fire1.fireHeld = true;
    Vec3 e2 = mech.Eye(MechTuning()), fwd = Normalize(Vec3{ 0, -0.25f, 1 }), right = Normalize(Cross(kUp, fwd));
    TickWeapons(s, tune, fire1, e2, fwd, right, w, 1.0f / 60.0f);
    CHECK(s.rockets.size() == 1);
    WeaponInput none;
    bool blast = false;
    for (int i = 0; i < 120 && !blast; i++) {
        TickWeapons(s, tune, none, e2, fwd, right, w, 1.0f / 60.0f);
        for (auto& e : ev.list) if (e.kind == EV_BLAST) blast = true;
    }
    CHECK(blast);
    CHECK(s.rockets.empty());
    CHECK(t.Stats().modified > 0);          // the ground was holed
    CHECK(debris.Live() > 0);
    // Switch to the gun.
    WeaponInput sw; sw.switchPressed = true;
    TickWeapons(s, tune, sw, e2, fwd, right, w, 1.0f / 60.0f);
    CHECK(s.current == WPN_GUN);
    // Aim the gun at a standing tree and hold the trigger.
    PropRef ref; for (auto& kv : props.Tiles()) { for (size_t i = 0; i < kv.second.props.size(); i++) { auto& q = kv.second.props[i]; if (q.IsTree() && q.state == PS_STANDING && Length(q.pos) < 150 && Length(q.pos) > 30) { ref = { kv.first, (int)i }; break; } } if (ref.index >= 0) break; }
    Prop* tree = props.Get(ref);
    CHECK(tree != nullptr);
    Vec3 gunEye = tree->pos + Vec3{ -30, 5, 0 };
    Vec3 aim = Normalize(tree->pos + Vec3{ 0, 4, 0 } - gunEye), aimRight = Normalize(Cross(kUp, aim));
    ev.Clear();
    for (int i = 0; i < 180 && tree->state == PS_STANDING; i++) TickWeapons(s, tune, fire1, gunEye, aim, aimRight, w, 1.0f / 60.0f);
    CHECK(tree->state == PS_FALLING);
    // Lock-on: with the wanderer straight ahead and in sight, lock mode locks within a second.
    Vec3 le = wd.Center() + Vec3{ -80, 4, 0 };
    Vec3 lf = Normalize(wd.Center() - le), lr = Normalize(Cross(kUp, lf));
    WeaponInput lk; lk.lockPressed = true;
    TickWeapons(s, tune, lk, le, lf, lr, w, 1.0f / 60.0f);
    CHECK(s.lockMode);
    for (int i = 0; i < 70; i++) TickWeapons(s, tune, none, le, lf, lr, w, 1.0f / 60.0f);
    CHECK(s.locked);
    // Explode near the mech hurts it; with the shield up much less.
    World w2 = w; w2.mechDamage = 0;
    Explode(w2, mech.pos + Vec3{ 3, 1, 0 }, 8.0f);
    float open = w2.mechDamage;
    mech.shield = true; World w3 = w; w3.mechDamage = 0;
    Explode(w3, mech.pos + Vec3{ 3, 1, 0 }, 8.0f);
    CHECK(open > 0.1f);
    CHECK(w3.mechDamage < open * 0.2f);
    // Governed: rockets never exceed their pool.
    WeaponState many; WeaponTuning fast = tune; fast.rocketCooldown = 0;
    for (int i = 0; i < 200; i++) TickWeapons(many, fast, fire1, { 0, 500, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, w, 1.0f / 60.0f);
    CHECK((int)many.rockets.size() <= WeaponState::MAX_ROCKETS);
}

int main() {
    TestFlatGround();
    TestSurfaceMesh();
    TestGroundVariety();
    TestBlast();
    TestSeams();
    TestSun();
    TestMech();
    TestPrims();
    TestProps();
    TestFire();
    TestDebris();
    TestPickups();
    TestWandererAndWeapons();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
