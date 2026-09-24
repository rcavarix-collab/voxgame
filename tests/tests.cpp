// tests/tests.cpp
//
// Native regression tests for the engine's platform-free modules: the
// .vtex parser, block texture assembly, the save format (v5 round trip
// and legacy v4), column streaming/eviction, player spawn/unstick, and
// the chunk mesher. Built and run by tests/run.sh with the host
// compiler; D3D and Windows are replaced by tests/stub/.

#include "../world.h"
#include "../worldfile.h"
#include "../vtex.h"
#include "../blocktex.h"
#include "../mesher.h"
#include "../shapes.h"
#include "../icons.h"
#include "../sky.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void ResetWorldState(World& w) {
    w = World();
    g_residentColumns.clear();
    g_evictedChunks.clear();
    ClearScheduledUpdates();
    g_pendingColumns.clear(); g_pendingColumnSet.clear();
    g_pendingEvictions.clear(); g_pendingEvictionSet.clear();
    g_lastPlayerChunkX = g_lastPlayerChunkZ = INT32_MIN;
}

static void Stream(World& w, float px, float pz, int ticks) {
    for (int t = 0; t < ticks; t++) {
        int cx = FloorDiv16((int)floorf(px)), cz = FloorDiv16((int)floorf(pz));
        EnsureChunksLoaded(cx, cz);
        ProcessColumnGeneration(w);
        ProcessColumnEviction(w);
    }
}

static bool ChunksEqual(const Chunk& a, const Chunk& b) {
    if (memcmp(a.blocks, b.blocks, sizeof(a.blocks)) || memcmp(a.state, b.state, sizeof(a.state))) return false;
    size_t na = a.data ? a.data->size() : 0, nb = b.data ? b.data->size() : 0;
    if (na != nb) return false;
    if (na) for (auto& kv : *a.data) { auto it = b.data->find(kv.first); if (it == b.data->end() || it->second != kv.second) return false; }
    return true;
}

// ---------------------------------------------------------------------

static void TestVtex() {
    printf("vtex parser\n");
    const char* good =
        "# comment\n"
        "texture stone   # trailing comment\n"
        "size 8\n"
        "palette\n"
        "  a 7c7c82\n"
        "  b #6a6a70   # a leading # on the hex is tolerated\n"
        "  # 000000\n"            // a comment line, not a key
        "pixels\n"
        "  aaaabbbb\n  abababab\n  aaaaaaaa\n  bbbbbbbb\n  aaaabbbb\n  abababab\n  aaaaaaaa\n  bbbbbbbb\n"
        "end\n"
        "block chest\n  all stone\n  front stone\nend\n";
    VtexSet s;
    ParseVtex(good, "good.vtex", s);
    CHECK(s.errors.empty());
    CHECK(s.textures.size() == 1);
    if (!s.textures.empty()) {
        CHECK(s.textures[0].size == 8);
        CHECK(s.textures[0].rgb[0] == 0x7c7c82);
        CHECK(s.textures[0].rgb[4] == 0x6a6a70);
    }
    CHECK(s.blocks.size() == 1 && s.blocks[0].front == "stone");
    for (auto& e : s.errors) printf("    %s\n", e.c_str());

    VtexSet bad;
    ParseVtex("texture a\nsize 8\npalette\n x 000000\npixels\n xxxxxxx\nend\n", "short.vtex", bad);  // 7-char row
    ParseVtex("texture b\nsize 8\npalette\n x 000000\npixels\n xxxxxxxy\nend\n", "key.vtex", bad);   // unknown key
    ParseVtex("texture c\nsize 8\npalette\n x 000000\npixels\n xxxxxxxx\nend\n", "rows.vtex", bad);  // 1 of 8 rows
    ParseVtex("texture d\nsize 12\nend\n", "size.vtex", bad);                                        // bad size
    ParseVtex("block chest\n  lid stone\nend\n", "face.vtex", bad);                                  // unknown face
    ParseVtex("texture e\nsize 8\npalette\n", "noend.vtex", bad);                                     // no end
    CHECK(bad.textures.empty());
    CHECK(bad.errors.size() == 6);
    for (auto& e : bad.errors) printf("    (expected) %s\n", e.c_str());
}

static void TestBlockTextures() {
    printf("block textures\n");
    VtexSet none;
    BlockTextureSet t;
    BuildBlockTextures(none, t);
    CHECK(t.warnings.empty());
    CHECK(t.layerCount == 9); // foundation stone dirt wood chest chest_front machine machine_front tube
    CHECK(t.mipCount == 7);
    CHECK(t.faceLayer[BLOCK_CHEST][FACE_POS_Z][FACE_POS_Z] != t.faceLayer[BLOCK_CHEST][FACE_POS_Z][FACE_POS_X]); // front vs side
    CHECK(t.faceLayer[BLOCK_CHEST][FACE_NEG_X][FACE_NEG_X] == t.faceLayer[BLOCK_CHEST][FACE_POS_Z][FACE_POS_Z]); // front follows facing
    CHECK(t.faceLayer[BLOCK_STONE][FACE_POS_Z][FACE_POS_Y] == t.faceLayer[BLOCK_STONE][FACE_NEG_X][FACE_NEG_Z]);
    CHECK(t.mips.back().size() == (size_t)t.layerCount * 4); // 1x1 per layer

    // Authored art overrides a block's whole mapping and is upscaled.
    VtexSet art;
    ParseVtex("texture my_stone\nsize 8\npalette\n r ff0000\n g 00ff00\npixels\n"
              " rrrrrrrr\n gggggggg\n rrrrrrrr\n gggggggg\n rrrrrrrr\n gggggggg\n rrrrrrrr\n gggggggg\nend\n"
              "block stone\n all my_stone\nend\n"
              "block dirt\n all nope\nend\n"
              "block unobtainium\n all my_stone\nend\n", "art.vtex", art);
    CHECK(art.errors.empty());
    BlockTextureSet a;
    BuildBlockTextures(art, a);
    uint16_t L = a.faceLayer[BLOCK_STONE][FACE_POS_Z][FACE_POS_Y];
    CHECK(a.layerNames[L] == "my_stone");
    const uint8_t* px = a.mips[0].data() + (size_t)L * BLOCK_TEX_SIZE * BLOCK_TEX_SIZE * 4;
    CHECK(px[2] == 255 && px[1] == 0);                                  // row 0: red (BGRA)
    CHECK(px[(size_t)7 * BLOCK_TEX_SIZE * 4 + 1] == 0);                 // row 7 still red (8x upscale)
    CHECK(px[(size_t)8 * BLOCK_TEX_SIZE * 4 + 1] == 255);               // row 8: green
    CHECK(a.warnings.size() == 2); // dirt -> missing texture; unknown block
    for (auto& w : a.warnings) printf("    (expected) %s\n", w.c_str());
}

static void TestSaveRoundTrip() {
    printf("save format v5 round trip\n");
    World w; ResetWorldState(w);
    g_loadRadius = 2;
    g_worldGen.type = GEN_FLAT; g_worldGen.version = 1; g_worldGen.seed = 0x1234567890ABCDEFull;
    Stream(w, 8, 8, 20);
    size_t generated = w.chunks.size();
    CHECK(generated > 0);
    for (auto& kv : w.chunks) CHECK(!kv.second->modified);

    // Edits with state and data in two chunks.
    w.Set(3, 12, 3, BLOCK_AIR);
    w.Set(5, 13, 5, BLOCK_CHEST, FACE_NEG_X);
    Chunk* c = w.FindChunk({ 0, 0, 0 });
    c->data = std::make_unique<std::unordered_map<uint16_t, std::vector<uint8_t>>>();
    (*c->data)[(uint16_t)Chunk::LocalIndex(5, 13, 5)] = { 1, 2, 3, 250 };
    w.Set(20, 40, 20, BLOCK_WOOD); // a chunk the generator never made (cy = 2)

    Player p; p.x = 1.5f; p.y = 13; p.z = 2.5f; p.yaw = 0.7f; p.hotbarIndex = 3;
    std::vector<uint8_t> buf;
    std::vector<PendingUpdate> pend = { { 7, 20, 7, UPD_GRAVITY, 3 }, { -1, 5, 9, UPD_GRAVITY, 0 } };
    EncodeSave(p, 1234.5f, g_worldGen, w, g_evictedChunks, pend, buf);
    printf("    %zu generated chunks, 2 modified -> %zu bytes\n", generated, buf.size());
    CHECK(buf.size() < 3000);

    SaveData d;
    CHECK(DecodeSave(buf.data(), buf.size(), d) == DecodeResult::Ok);
    CHECK(d.version == SAVE_VERSION);
    CHECK(d.player.x == p.x && d.player.yaw == p.yaw && d.player.hotbarIndex == 3);
    CHECK(d.dayTime == 1234.5f);
    CHECK(d.gen.type == GEN_FLAT && d.gen.seed == g_worldGen.seed);
    CHECK(d.chunks.size() == 2);
    CHECK(d.updates.size() == 2 && d.updates[0].x == 7 && d.updates[0].delay == 3 && d.updates[1].y == 5);
    for (auto& kv : d.chunks) {
        Chunk* orig = w.FindChunk(kv.first);
        CHECK(orig && orig->modified && ChunksEqual(*orig, *kv.second));
    }

    // Corruption is caught, not loaded.
    std::vector<uint8_t> broken = buf; broken[buf.size() / 2] ^= 0x40;
    SaveData d2; CHECK(DecodeSave(broken.data(), broken.size(), d2) == DecodeResult::BadChecksum);
    SaveData d3; CHECK(DecodeSave(buf.data(), 20, d3) != DecodeResult::Ok);
}

// Writes a v4 file the way the old SaveGame did (every non-air block).
static std::vector<uint8_t> MakeLegacyV4(const std::vector<std::array<int, 4>>& blocks) {
    std::vector<uint8_t> b;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); };
    auto f32 = [&](float v) { uint32_t x; memcpy(&x, &v, 4); u32(x); };
    auto str = [&](const char* s) { uint16_t n = (uint16_t)strlen(s); b.push_back((uint8_t)n); b.push_back((uint8_t)(n >> 8)); b.insert(b.end(), s, s + n); };
    u32(('G' << 24) | ('L' << 16) | ('X' << 8) | 'V'); u32(4);
    f32(8); f32(50); f32(8); f32(0); f32(0); u32(0); f32(99.0f);
    const char* names[] = { "air", "foundation", "stone", "dirt", "wood", "chest", "machine", "pipe_straight" };
    u32(8); for (const char* n : names) str(n);
    u32((uint32_t)blocks.size());
    for (auto& bl : blocks) { u32((uint32_t)bl[0]); u32((uint32_t)bl[1]); u32((uint32_t)bl[2]); b.push_back((uint8_t)bl[3]); }
    uint32_t h = 2166136261u; for (uint8_t x : b) { h ^= x; h *= 16777619u; }
    u32(h);
    return b;
}

static void TestLegacyLoad() {
    printf("legacy v4 load\n");
    std::vector<std::array<int, 4>> blocks = { { 1, 0, 1, 1 }, { 1, 1, 1, 2 }, { 1, 2, 1, 7 }, { -5, 3, -5, 4 } };
    std::vector<uint8_t> buf = MakeLegacyV4(blocks);
    SaveData d;
    CHECK(DecodeSave(buf.data(), buf.size(), d) == DecodeResult::Ok);
    CHECK(d.version == 4 && d.dayTime == 99.0f);
    CHECK(d.gen.type == GEN_HILLS && d.gen.version == 1);
    CHECK(d.unknownBlockNames.size() == 1); // pipe_straight -> air
    // 2 columns x chunk rows 0..3 (hills v1 max height), all explicit.
    CHECK(d.chunks.size() == 8);
    Chunk* c = d.chunks[{ 0, 0, 0 }].get();
    CHECK(c && c->blocks[Chunk::LocalIndex(1, 0, 1)] == BLOCK_FOUNDATION && c->blocks[Chunk::LocalIndex(1, 1, 1)] == BLOCK_STONE);
    CHECK(c && c->blocks[Chunk::LocalIndex(1, 2, 1)] == BLOCK_AIR);
    Chunk* e = d.chunks[{ 0, 2, 0 }].get();
    bool empty = true; for (int i = 0; i < CHUNK_CELLS; i++) if (e->blocks[i]) empty = false;
    CHECK(e && e->modified && empty); // a dug-out chunk stays dug out
}

static void TestStreaming() {
    printf("streaming, eviction, regeneration\n");
    World w; ResetWorldState(w);
    g_loadRadius = 2;
    g_worldGen = WorldGenParams(); g_worldGen.type = GEN_HILLS;
    Stream(w, 8, 8, 40);
    CHECK(g_residentColumns.size() == 49); // (2*(2+1)+1)^2: one ring past the view radius
    CHECK(g_residentColumns.count(ColumnKey(0, 0)));

    // Snapshot one unmodified and one modified chunk, then walk away.
    int h = TerrainHeight(40, 8);
    w.Set(40, h, 8, BLOCK_MACHINE, FACE_POS_X);
    ChunkCoord editedCC = World::ToChunk(40, h, 8);
    Chunk snapEdited = Chunk(); memcpy(snapEdited.blocks, w.FindChunk(editedCC)->blocks, CHUNK_CELLS); memcpy(snapEdited.state, w.FindChunk(editedCC)->state, CHUNK_CELLS);
    ChunkCoord plainCC = { 1, 1, 0 };
    Chunk snapPlain = Chunk(); memcpy(snapPlain.blocks, w.FindChunk(plainCC)->blocks, CHUNK_CELLS);

    Stream(w, 8 + 16 * 20, 8, 400);
    CHECK(!g_residentColumns.count(ColumnKey(1, 0)) && !g_residentColumns.count(ColumnKey(2, 0)));
    CHECK(g_evictedChunks.size() == 1); // only the modified chunk is kept
    CHECK(g_evictedChunks.count(editedCC));
    for (auto& kv : w.chunks) CHECK(ColumnDistance(kv.first.x, kv.first.z, g_lastPlayerChunkX, g_lastPlayerChunkZ) <= g_loadRadius + 1 + CHUNK_EVICT_MARGIN);

    Stream(w, 8, 8, 400); // come back
    Chunk* ce = w.FindChunk(editedCC); Chunk* cp = w.FindChunk(plainCC);
    CHECK(ce && ce->modified && ChunksEqual(*ce, snapEdited));
    CHECK(cp && !cp->modified && memcmp(cp->blocks, snapPlain.blocks, CHUNK_CELLS) == 0);
    CHECK(g_evictedChunks.empty());
}

static void TestPlayer() {
    printf("player spawn and unstick\n");
    World w; ResetWorldState(w);
    g_loadRadius = 1; g_worldGen = WorldGenParams(); g_worldGen.type = GEN_FLAT;
    Player p; p.y = (float)(TerrainHeight(8, 8) + 1);
    float minY = p.y;
    for (int t = 0; t < 120; t++) {
        Stream(w, p.x, p.z, 1);
        UpdatePlayerPhysics(w, p, 1.0f / 60.0f, false, false, false, false, false);
        minY = std::min(minY, p.y);
    }
    CHECK(p.onGround && fabsf(p.y - 13.0f) < 1e-4f && minY >= 13.0f - 1e-4f);

    p.y = 8.0f; // buried
    int ticks = 0;
    while (ticks < 60) { UpdatePlayerPhysics(w, p, 1.0f / 60.0f, false, false, false, false, false); ticks++; if (p.onGround) break; }
    CHECK(p.onGround && fabsf(p.y - 13.0f) < 1e-4f);
}

static void TestMesher() {
    printf("mesher\n");
    VtexSet none; BlockTextureSet t; BuildBlockTextures(none, t);
    memcpy(g_blockFaceLayer, t.faceLayer, sizeof(g_blockFaceLayer));

    World w; ResetWorldState(w);
    std::vector<Vertex> v; std::vector<uint16_t> idx;

    // A lone block: 6 faces, nothing occluded.
    w.Set(5, 5, 5, BLOCK_STONE);
    BuildChunkMesh(w, { 0, 0, 0 }, *w.FindChunk({ 0, 0, 0 }), v, idx);
    CHECK(v.size() == 24 && idx.size() == 36);
    bool allOpen = true; for (auto& x : v) if (VertexAO(x) != 3) allOpen = false;
    CHECK(allOpen);

    // A neighbour hides the shared faces and darkens corners beside it.
    w.Set(6, 5, 5, BLOCK_STONE);
    w.Set(5, 4, 6, BLOCK_STONE); // diagonal (edge-adjacent) to (5,5,5): shares no face, only shading
    BuildChunkMesh(w, { 0, 0, 0 }, *w.FindChunk({ 0, 0, 0 }), v, idx);
    CHECK(v.size() == (6 + 6 + 6 - 2) * 4); // one touching pair hides 2 faces
    // The (5,5,5) +Z face: its two bottom corners sit over the block at (5,4,6).
    int darkened = 0;
    for (auto& x : v) if (VertexFace(x) == FACE_POS_Z && x.z == 6 * 8 && x.y == 5 * 8 && x.x <= 6 * 8 && VertexAO(x) < 3) darkened++;
    CHECK(darkened >= 2);

    // Across a chunk boundary: a block at x=15 next to one at x=16 in the
    // next chunk hides the shared face in both meshes.
    w.Set(15, 5, 0, BLOCK_STONE); w.Set(16, 5, 0, BLOCK_STONE);
    BuildChunkMesh(w, { 1, 0, 0 }, *w.FindChunk({ 1, 0, 0 }), v, idx);
    CHECK(v.size() == 5 * 4);

    // Front face follows facing.
    World w2; w2.Set(1, 1, 1, BLOCK_CHEST, FACE_NEG_X);
    BuildChunkMesh(w2, { 0, 0, 0 }, *w2.FindChunk({ 0, 0, 0 }), v, idx);
    uint16_t front = t.faceLayer[BLOCK_CHEST][FACE_NEG_X][FACE_NEG_X];
    for (auto& x : v) CHECK((x.layer == front) == (VertexFace(x) == FACE_NEG_X));

    // Worst case fits 16-bit indices.
    World w3;
    for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++)
        if ((x + y + z) % 2 == 0) w3.Set(x, y, z, BLOCK_STONE);
    BuildChunkMesh(w3, { 0, 0, 0 }, *w3.FindChunk({ 0, 0, 0 }), v, idx);
    CHECK(v.size() == 2048u * 24u && v.size() <= 65536u);
}

static void TestShapes() {
    printf("shapes\n");
    ShapePoly polys[MAX_SHAPE_POLYS];
    ShapeBox boxes[MAX_SHAPE_BOXES];

    // Slab: lower half by default, upper with STATE_UPPER; its top face is
    // interior (never culled), its bottom lies on the boundary.
    CHECK(ShapeBoxes(SHAPE_SLAB, 0, boxes) == 1 && boxes[0].y0 == 0 && boxes[0].y1 == 4);
    CHECK(ShapeBoxes(SHAPE_SLAB, STATE_UPPER, boxes) == 1 && boxes[0].y0 == 4 && boxes[0].y1 == 8);
    int n = ShapePolys(SHAPE_SLAB, 0, polys);
    CHECK(n == 6);
    for (int i = 0; i < n; i++) {
        if (polys[i].texFace == FACE_POS_Y) CHECK(polys[i].boundary == -1);
        if (polys[i].texFace == FACE_NEG_Y) CHECK(polys[i].boundary == FACE_NEG_Y);
        if (polys[i].texFace == FACE_POS_X) CHECK(polys[i].v[0].v >= 4); // side shows the texture's lower half
    }

    // Ramp: rises toward its facing -- the full-height wall sits on that side.
    for (BlockFace f : { FACE_POS_X, FACE_NEG_X, FACE_POS_Z, FACE_NEG_Z }) {
        n = ShapePolys(SHAPE_RAMP, f, polys);
        CHECK(n == 5);
        bool wall = false;
        for (int i = 0; i < n; i++) if (polys[i].boundary == f && polys[i].count == 4 && polys[i].texFace == f) wall = true;
        CHECK(wall);
        int nb = ShapeBoxes(SHAPE_RAMP, f, boxes);
        CHECK(nb == 2);
        // The upper step is on the facing side.
        const ShapeBox& up = boxes[1];
        if (f == FACE_POS_X) CHECK(up.x0 == 4 && up.x1 == 8);
        if (f == FACE_NEG_X) CHECK(up.x0 == 0 && up.x1 == 4);
        if (f == FACE_POS_Z) CHECK(up.z0 == 4 && up.z1 == 8);
        if (f == FACE_NEG_Z) CHECK(up.z0 == 0 && up.z1 == 4);
    }

    // Tube: runs along its facing's axis.
    ShapeBoxes(SHAPE_TUBE, FACE_POS_Y, boxes); CHECK(boxes[0].y0 == 0 && boxes[0].y1 == 8 && boxes[0].x1 - boxes[0].x0 == 2);
    ShapeBoxes(SHAPE_TUBE, FACE_NEG_X, boxes); CHECK(boxes[0].x0 == 0 && boxes[0].x1 == 8 && boxes[0].y1 - boxes[0].y0 == 2);

    // Meshing: a slab on the ground hides the ground's top? No -- only full
    // cubes hide faces. But the ground hides the slab's bottom.
    VtexSet none; BlockTextureSet t; BuildBlockTextures(none, t);
    memcpy(g_blockFaceLayer, t.faceLayer, sizeof(g_blockFaceLayer));
    World w; ResetWorldState(w);
    w.Set(4, 4, 4, BLOCK_STONE);
    w.Set(4, 5, 4, BLOCK_STONE_SLAB);
    std::vector<Vertex> v; std::vector<uint16_t> idx;
    BuildChunkMesh(w, { 0, 0, 0 }, *w.FindChunk({ 0, 0, 0 }), v, idx);
    CHECK(v.size() == (6 + 5) * 4); // cube keeps its top (slab isn't full); slab loses its bottom
    int slabTop = 0;
    for (auto& x : v) if (VertexFace(x) == FACE_POS_Y && x.y == 5 * 8 + 4) slabTop++;
    CHECK(slabTop == 4); // at y = 5.5
    // A pyramid is 1 quad + 4 triangles.
    World w2; w2.Set(1, 1, 1, BLOCK_STONE_PYRAMID);
    BuildChunkMesh(w2, { 0, 0, 0 }, *w2.FindChunk({ 0, 0, 0 }), v, idx);
    CHECK(v.size() == 4 + 4 * 3 && idx.size() == 6 + 4 * 3);

    // Collision: stand on a slab at half height; step up onto it from the
    // ground; a full block can't be stepped onto.
    World g; ResetWorldState(g);
    g_loadRadius = 1; g_worldGen = WorldGenParams(); g_worldGen.type = GEN_FLAT;
    Stream(g, 8, 8, 20);
    g.Set(10, 13, 8, BLOCK_STONE_SLAB);
    g.Set(8, 13, 11, BLOCK_STONE);
    Player p; p.x = 8.5f; p.z = 8.5f; p.y = 13.0f; p.yaw = 1.5707963f; // facing +X
    for (int i = 0; i < 10; i++) UpdatePlayerPhysics(g, p, 1.0f / 60.0f, false, false, false, false, false);
    CHECK(p.onGround && fabsf(p.y - 13.0f) < 1e-3f);
    for (int i = 0; i < 30; i++) UpdatePlayerPhysics(g, p, 1.0f / 60.0f, true, false, false, false, false);
    CHECK(p.x > 10.3f && p.x < 11.0f && fabsf(p.y - 13.5f) < 1e-3f); // walked up onto the slab (and is standing on it)
    Player q; q.x = 8.5f; q.z = 8.5f; q.y = 13.0f; q.yaw = 0.0f; // facing +Z, toward the cube
    for (int i = 0; i < 10; i++) UpdatePlayerPhysics(g, q, 1.0f / 60.0f, false, false, false, false, false);
    for (int i = 0; i < 90; i++) UpdatePlayerPhysics(g, q, 1.0f / 60.0f, true, false, false, false, false);
    CHECK(q.z < 11.0f - PLAYER_HALFW + 1e-3f && fabsf(q.y - 13.0f) < 1e-3f); // stopped at the full block
}

static void TestScheduledUpdates() {
    printf("scheduled updates (gravity)\n");
    World w; ResetWorldState(w);
    g_loadRadius = 1; g_worldGen = WorldGenParams(); g_worldGen.type = GEN_FLAT;
    Stream(w, 8, 8, 20);
    // A 5-high stone column on a wood block; remove the wood.
    for (int y = 14; y < 19; y++) w.Set(8, y, 8, BLOCK_STONE);
    w.Set(8, 13, 8, BLOCK_WOOD);
    LiveEdit(w, 8, 13, 8, BLOCK_AIR);
    CHECK(ScheduledUpdateCount() == 1);
    int ticks = 0;
    while (ScheduledUpdateCount() > 0 && ticks < 200) { ProcessScheduledUpdates(w); ticks++; }
    CHECK(ScheduledUpdateCount() == 0);
    for (int y = 13; y < 18; y++) CHECK(w.Get(8, y, 8) == BLOCK_STONE);
    CHECK(w.Get(8, 18, 8) == BLOCK_AIR);
    printf("    5-block column settled in %d ticks\n", ticks);

    // The per-tick cap spreads a big collapse over several ticks.
    ClearScheduledUpdates();
    for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) { w.Set(x, 20, z, BLOCK_DIRT); ScheduleUpdate(x, 20, z, UPD_GRAVITY, 0); }
    ProcessScheduledUpdates(w);
    CHECK(ScheduledUpdateCount() == 256); // 64 fell one cell and re-queued themselves; 192 still waiting their turn
    int fell = 0; for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) if (w.Get(x, 19, z) == BLOCK_DIRT) fell++;
    CHECK(fell == MAX_UPDATES_PER_TICK);

    // Snapshot/restore keeps remaining delays.
    ClearScheduledUpdates();
    ScheduleUpdate(1, 2, 3, UPD_GRAVITY, 5);
    ProcessScheduledUpdates(w); ProcessScheduledUpdates(w);
    std::vector<PendingUpdate> snap = SnapshotScheduledUpdates();
    CHECK(snap.size() == 1 && snap[0].delay == 3);
    ClearScheduledUpdates();
    RestoreScheduledUpdates(snap);
    CHECK(ScheduledUpdateCount() == 1);
}

static void TestIcons() {
    printf("icons\n");
    VtexSet none; BlockTextureSet t; BuildBlockTextures(none, t);
    memcpy(g_blockFaceLayer, t.faceLayer, sizeof(g_blockFaceLayer));
    RenderBlockIcons(t);
    for (int id = 1; id < BLOCK_COUNT; id++) {
        auto alpha = [&](int x, int y) { return t.icons[((size_t)y * t.iconsW + (size_t)id * BLOCK_TEX_SIZE + x) * 4 + 3]; };
        CHECK(alpha(0, 0) == 0 && alpha(BLOCK_TEX_SIZE - 1, 0) == 0); // transparent corners
        int opaque = 0;
        for (int y = 0; y < BLOCK_TEX_SIZE; y++) for (int x = 0; x < BLOCK_TEX_SIZE; x++) if (alpha(x, y) == 255) opaque++;
        CHECK(opaque > 40); // something drawn (the thin tube is the smallest)
    }
}

static Vec3 XformPoint(const Mat4& m, Vec3 p) {
    float x = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
    float y = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
    float z = p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2];
    float w = p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3];
    return { x / w, y / w, z / w };
}

static void TestSky() {
    printf("sky model and shadow projection\n");
    SkyState dawn = ComputeSky(0), noon = ComputeSky(1500), dusk = ComputeSky(3000), night = ComputeSky(3300);
    CHECK(fabsf(dawn.sunDir.y) < 1e-4f && dawn.sunDir.x > 0.99f);     // rises in the east (+X)
    CHECK(noon.sunDir.y > 0.9f && noon.sunDir.z < 0);                 // high, tilted south
    CHECK(fabsf(dusk.sunDir.y) < 1e-3f && dusk.sunDir.x < -0.99f);    // sets in the west
    CHECK(night.sunDir.y < -0.9f && night.moonDir.y > 0.3f);          // moon up at night
    CHECK(noon.daylight == 1.0f && fabsf(night.daylight - NIGHT_LIGHT) < 1e-5f);
    CHECK(night.starsVisible == 1.0f && noon.starsVisible == 0.0f);
    CHECK(night.sunLight == 0.0f && noon.sunLight == 1.0f);
    SkyState wrap = ComputeSky(3600.0f);
    CHECK(fabsf(wrap.sunDir.x - dawn.sunDir.x) < 1e-4f && fabsf(wrap.sunDir.y - dawn.sunDir.y) < 1e-4f); // loops

    Vec3 eye = { 100.3f, 30.0f, -42.7f };
    Mat4 lvp = ShadowLightViewProj(eye, noon.sunDir, 64.0f, 200.0f, 2048);
    Vec3 c = XformPoint(lvp, eye);
    CHECK(fabsf(c.x) < 0.01f && fabsf(c.y) < 0.01f && c.z > 0.4f && c.z < 0.6f); // centred, mid-depth
    Vec3 towardSun = XformPoint(lvp, { eye.x + noon.sunDir.x * 10, eye.y + noon.sunDir.y * 10, eye.z + noon.sunDir.z * 10 });
    CHECK(towardSun.z < c.z && fabsf(towardSun.x - c.x) < 1e-3f);    // nearer the light, same texel
    Vec3 edge = XformPoint(lvp, { eye.x + 60, eye.y, eye.z });
    CHECK(fabsf(edge.x) < 1.0f && fabsf(edge.y) < 1.0f);              // 60 blocks out is still on the map
    // Moving less than a texel doesn't move the map.
    Mat4 lvp2 = ShadowLightViewProj({ eye.x + 0.001f, eye.y, eye.z }, noon.sunDir, 64.0f, 200.0f, 2048);
    CHECK(fabsf(lvp2.m[3][0] - lvp.m[3][0]) < 1e-3f || fabsf(lvp2.m[3][0] - lvp.m[3][0]) > 2.0f / 2048 * 0.9f);
}

int main() {
    TestVtex();
    TestBlockTextures();
    TestSaveRoundTrip();
    TestLegacyLoad();
    TestStreaming();
    TestPlayer();
    TestMesher();
    TestShapes();
    TestIcons();
    TestScheduledUpdates();
    TestSky();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
