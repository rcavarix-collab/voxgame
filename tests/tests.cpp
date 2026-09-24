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
    ClearFallQueue();
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
    CHECK(t.layerCount == 8); // foundation stone dirt wood chest chest_front machine machine_front
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
    EncodeSave(p, 1234.5f, g_worldGen, w, g_evictedChunks, buf);
    printf("    %zu generated chunks, 2 modified -> %zu bytes\n", generated, buf.size());
    CHECK(buf.size() < 3000);

    SaveData d;
    CHECK(DecodeSave(buf.data(), buf.size(), d) == DecodeResult::Ok);
    CHECK(d.version == SAVE_VERSION);
    CHECK(d.player.x == p.x && d.player.yaw == p.yaw && d.player.hotbarIndex == 3);
    CHECK(d.dayTime == 1234.5f);
    CHECK(d.gen.type == GEN_FLAT && d.gen.seed == g_worldGen.seed);
    CHECK(d.chunks.size() == 2);
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
    for (auto& x : v) if (VertexFace(x) == FACE_POS_Z && x.z == 6 && x.y == 5 && x.x <= 6 && VertexAO(x) < 3) darkened++;
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

int main() {
    TestVtex();
    TestBlockTextures();
    TestSaveRoundTrip();
    TestLegacyLoad();
    TestStreaming();
    TestPlayer();
    TestMesher();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
