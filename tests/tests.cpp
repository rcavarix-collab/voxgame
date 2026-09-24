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
#include "../glowlight.h"
#include "../musiclevel.h"
#include "../mesher.h"
#include "../shapes.h"
#include "../icons.h"
#include "../sky.h"
#include "../theline.h"
#include "../essence.h"
#include "../essencemap.h"
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

    // Alpha (see-through blocks): "rrggbbaa", stored as 255 - alpha in the
    // top byte so plain 6-digit colours are unchanged; a 7-digit colour is
    // an error.
    VtexSet al;
    ParseVtex("texture pane\nsize 8\npalette\n g 80c0ff40\n f #a0b0c0ff # opaque\npixels\n"
              " gggggggg\n gggggggg\n gggggggg\n gggggggg\n ffffffff\n ffffffff\n ffffffff\n ffffffff\nend\n"
              "block glass\n all pane\nend\n", "alpha.vtex", al);
    CHECK(al.errors.empty() && al.textures.size() == 1);
    if (!al.textures.empty()) CHECK(al.textures[0].rgb[0] == 0xBF80C0FFu && al.textures[0].rgb[63] == 0x00A0B0C0u);
    BlockTextureSet at; BuildBlockTextures(al, at);
    const uint8_t* ap = at.mips[0].data() + (size_t)at.faceLayer[BLOCK_GLASS][0][0] * BLOCK_TEX_SIZE * BLOCK_TEX_SIZE * 4;
    CHECK(ap[0] == 0xFF && ap[1] == 0xC0 && ap[2] == 0x80 && ap[3] == 0x40);            // BGRA, alpha kept
    CHECK(ap[((size_t)(BLOCK_TEX_SIZE - 1) * BLOCK_TEX_SIZE) * 4 + 3] == 255);           // bottom rows opaque
    VtexSet al7; ParseVtex("texture q\nsize 8\npalette\n g 80c0ff4\npixels\nend\n", "seven.vtex", al7);
    CHECK(!al7.errors.empty());

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
    CHECK(t.layerCount == 14); // foundation stone dirt wood chest chest_front machine machine_front tube music_block timestream_block essence_attractor glass crystal
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

    // Mips average in linear light: a black/white checker fades to the
    // sRGB code of 50% linear (188), not to gamma-space 128; flat colour
    // survives every level unchanged.
    VtexSet checker;
    ParseVtex("texture chk\nsize 8\npalette\n k 000000\n w ffffff\n r ff0000\npixels\n"
              " kwkwkwkw\n wkwkwkwk\n kwkwkwkw\n wkwkwkwk\n kwkwkwkw\n wkwkwkwk\n kwkwkwkw\n wkwkwkwk\nend\n"
              "texture flat\nsize 8\npalette\n r 804020\npixels\n"
              " rrrrrrrr\n rrrrrrrr\n rrrrrrrr\n rrrrrrrr\n rrrrrrrr\n rrrrrrrr\n rrrrrrrr\n rrrrrrrr\nend\n"
              "block stone\n all chk\nend\nblock dirt\n all flat\nend\n", "chk.vtex", checker);
    CHECK(checker.errors.empty());
    BlockTextureSet c;
    BuildBlockTextures(checker, c);
    uint16_t LC = c.faceLayer[BLOCK_STONE][FACE_POS_Z][FACE_POS_Y], LF = c.faceLayer[BLOCK_DIRT][FACE_POS_Z][FACE_POS_Y];
    int m = 4, sz = BLOCK_TEX_SIZE >> m; // one texel = 2x2 source pixels
    const uint8_t* cm = c.mips[m].data() + (size_t)LC * sz * sz * 4;
    CHECK(cm[0] == 188 && cm[1] == 188 && cm[2] == 188 && cm[3] == 255);
    const uint8_t* fm = c.mips.back().data() + (size_t)LF * 4;
    CHECK(fm[0] == 0x20 && fm[1] == 0x40 && fm[2] == 0x80 && fm[3] == 255);
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
    LineSaveData ld; ld.dwell = { { 5, 12.5f }, { -3, 600.0f } }; ld.angMom = -42.5; ld.theta = 1.25f;
    EssenceNetwork::SaveData es; es.discoveredZones = { 42, 7 }; es.attractors = { 1, 13, -5 };
    EncodeSave(p, 1234.5f, g_worldGen, w, g_evictedChunks, pend, ld, es, buf);
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
    CHECK(d.essence.discoveredZones.size() == 2 && d.essence.discoveredZones[1] == 7 && d.essence.attractors.size() == 3 && d.essence.attractors[2] == -5);
    CHECK(d.line.dwell.size() == 2 && d.line.dwell[1].first == -3 && d.line.dwell[1].second == 600.0f && d.line.angMom == -42.5 && d.line.theta == 1.25f);
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

    // Below the world: put back on top of the column.
    Player v = p; v.y = -100.0f;
    UpdatePlayerPhysics(w, v, 1.0f / 60.0f, false, false, false, false, false);
    CHECK(fabsf(v.y - 13.0f) < 1e-4f);

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

    // Glow kinds ride in the vertex for the reactive blocks only.
    World wg; wg.Set(1, 1, 1, BLOCK_MUSIC); wg.Set(3, 1, 1, BLOCK_TIMESTREAM); wg.Set(5, 1, 1, BLOCK_STONE);
    BuildChunkMesh(wg, { 0, 0, 0 }, *wg.FindChunk({ 0, 0, 0 }), v, idx);
    int glowMusic = 0, glowLine = 0, glowNone = 0;
    for (auto& x : v) { int g = VertexGlow(x); if (g == GLOW_MUSIC) glowMusic++; else if (g == GLOW_TIMESTREAM) glowLine++; else glowNone++; }
    CHECK(glowMusic == 24 && glowLine == 24 && glowNone == 24);

    // Every cube face is wound clockwise seen from outside (the D3D front
    // face): the see-through pass culls back faces on that basis (4.11).
    {
        World wc; wc.Set(2, 2, 2, BLOCK_STONE); wc.Set(2, 3, 2, BLOCK_STONE); wc.Set(3, 2, 3, BLOCK_STONE); // mixed AO: both diagonal splits
        BuildChunkMesh(wc, { 0, 0, 0 }, *wc.FindChunk({ 0, 0, 0 }), v, idx);
        static const int nrm[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        bool wound = true;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const Vertex &a = v[idx[i]], &b = v[idx[i + 1]], &c = v[idx[i + 2]];
            int e1[3] = { b.x - a.x, b.y - a.y, b.z - a.z }, e2[3] = { c.x - a.x, c.y - a.y, c.z - a.z };
            int cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            const int* n = nrm[VertexFace(a)];
            if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] <= 0) wound = false;
        }
        CHECK(wound);
    }

    // See-through blocks (4.11): glass faces come after every opaque one;
    // glass beside glass shares no face; stone beside glass keeps its face;
    // glass beside stone loses its; glass never darkens AO.
    {
        World wt; ResetWorldState(wt);
        wt.Set(4, 4, 4, BLOCK_GLASS); wt.Set(5, 4, 4, BLOCK_GLASS); // a 2-block pane
        wt.Set(4, 3, 4, BLOCK_STONE);                               // stone under the first pane block
        wt.Set(8, 4, 4, BLOCK_STONE); wt.Set(9, 4, 4, BLOCK_CRYSTAL); // stone beside crystal
        size_t first = 0;
        BuildChunkMesh(wt, { 0, 0, 0 }, *wt.FindChunk({ 0, 0, 0 }), v, idx, &first);
        int stoneFaces = 0, glassFaces = 0;
        for (size_t i = 0; i < idx.size(); i += 6) {
            const Vertex& a = v[idx[i]];
            bool see = a.layer == t.faceLayer[BLOCK_GLASS][0][0] || a.layer == t.faceLayer[BLOCK_CRYSTAL][0][0];
            CHECK(see == (i >= first));
            if (see) glassFaces++; else stoneFaces++;
        }
        // Stone: 6 + 6 (both keep every face: glass and crystal hide nothing).
        // Glass pane: 2 blocks x 6 - 2 shared - 1 on the stone = 9; crystal: 6 - 1 on the stone = 5.
        CHECK(stoneFaces == 12 && glassFaces == 9 + 5);
        bool glassDarkens = false;
        for (auto& x : v) if (x.y == 5 * 8 && VertexFace(x) == FACE_POS_Y && x.x >= 8 * 8 && x.x <= 9 * 8 && VertexAO(x) < 3 && x.layer == t.faceLayer[BLOCK_STONE][0][0]) glassDarkens = true;
        CHECK(!glassDarkens); // the stone's top beside the crystal stays open
        size_t firstNone = 12345;
        World ws; ws.Set(1, 1, 1, BLOCK_STONE);
        BuildChunkMesh(ws, { 0, 0, 0 }, *ws.FindChunk({ 0, 0, 0 }), v, idx, &firstNone);
        CHECK(firstNone == idx.size()); // no glass: everything is opaque
    }

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
        int opaque = 0, drawn = 0;
        for (int y = 0; y < BLOCK_TEX_SIZE; y++) for (int x = 0; x < BLOCK_TEX_SIZE; x++) { if (alpha(x, y) == 255) opaque++; if (alpha(x, y) >= 90) drawn++; }
        if (g_blocks[id].translucent) CHECK(drawn > 1000 && opaque < drawn); // see-through, but visible
        else CHECK(opaque > 40); // something drawn (the thin tube is the smallest)
    }
}

static Vec3 XformPoint(const Mat4& m, Vec3 p) {
    float x = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
    float y = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
    float z = p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2];
    float w = p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3];
    return { x / w, y / w, z / w };
}

static void TestMusicLevel() {
    printf("music onset level\n");
    const int SR = 44100, CH = SR / 4;
    std::vector<int16_t> pcm(CH);
    float out[16];
    auto run = [&](MusicLevelMeter& m, auto sample, int chunks, float* maxOut, float* meanOut) {
        float mx = 0, sum = 0; int n = 0;
        for (int c = 0; c < chunks; c++) {
            for (int i = 0; i < CH; i++) pcm[i] = (int16_t)sample(c * CH + i);
            MeasureMusicLevels(m, pcm.data(), CH, 16, SR, out);
            if (c < 4) continue; // settle
            for (float v : out) { mx = std::max(mx, v); sum += v; n++; }
        }
        *maxOut = mx; *meanOut = sum / n;
    };
    // A sustained pad (the bulk of the day's music): dark once settled.
    MusicLevelMeter pad; float mx, mean;
    run(pad, [&](int i) { return 6000.0 * sin(i * 2 * 3.14159265 * 220.0 / SR); }, 20, &mx, &mean);
    CHECK(mx < 0.05f);
    // The same pad with a soft plucked note every half second: flashes on
    // each note, dark most of the time in between.
    MusicLevelMeter pl;
    run(pl, [&](int i) {
        double t = (double)i / SR, since = fmod(t, 0.5);
        return 6000.0 * sin(i * 2 * 3.14159265 * 220.0 / SR) + 2500.0 * exp(-since * 30.0) * sin(i * 2 * 3.14159265 * 1760.0 / SR);
    }, 20, &mx, &mean);
    printf("    plucks over a pad: peak %.2f, mean %.2f\n", mx, mean);
    CHECK(mx > 0.9f && mean < 0.35f);
    // Near-silence never flashes, however it wobbles.
    MusicLevelMeter q;
    run(q, [&](int i) { return (i / 441) % 7 == 0 ? 3.0 : -2.0; }, 20, &mx, &mean);
    CHECK(mx == 0.0f);
}

static void TestGlowLight() {
    printf("glow light grid\n");
    World w;
    for (int z = 0; z < 32; z++) for (int x = 0; x < 32; x++) w.Set(x, 10, z, BLOCK_STONE); // floor
    w.Set(10, 11, 10, BLOCK_MUSIC);
    for (int z = 7; z <= 13; z++) for (int y = 11; y <= 13; y++) w.Set(13, y, z, BLOCK_STONE); // a wall east of it
    int ox, oy, oz; GlowGridOrigin(10.5f, 12.6f, 10.5f, ox, oy, oz);
    CHECK(ox == -32 && oy == -32 && oz == -32);
    GlowGrid g; BuildGlowGrid(w, ox, oy, oz, g);
    auto at = [&](int x, int y, int z, int ch) { return (int)g.texels[((size_t)(((z - oz) * GLOW_GRID + (y - oy)) * GLOW_GRID + (x - ox))) * 2 + ch]; };
    CHECK(g.emitters.size() == 1 && g.texels.size() == (size_t)GLOW_GRID * GLOW_GRID * GLOW_GRID * 2);
    CHECK(at(11, 11, 10, 0) > 150 && at(11, 11, 10, 1) == 0);     // beside it: bright, music channel only
    CHECK(at(7, 11, 10, 0) > 0 && at(7, 11, 10, 0) < at(9, 11, 10, 0)); // falls off with distance
    CHECK(at(15, 11, 10, 0) == 0);                                  // behind the wall: in its shadow
    CHECK(at(14, 16, 10, 0) > 0);                                   // over the top of the wall: lit again
    CHECK(at(10, 11, 19, 0) == 0);                                  // beyond its reach
    CHECK(at(10, 10, 11, 0) == 0);                                  // inside the (opaque) floor
    CHECK(at(10, 9, 10, 0) == 0);                                   // under the floor: shadowed
    // Change detection: its own chunk and a neighbour within reach count; far chunks don't.
    CHECK(ChunkAffectsGlow(g, { 0, 0, 0 }, *w.FindChunk({ 0, 0, 0 })));
    CHECK(ChunkAffectsGlow(g, { 1, 0, 0 }, *w.FindChunk({ 1, 0, 0 })));
    Chunk empty;
    CHECK(!ChunkAffectsGlow(g, { 6, 0, 6 }, empty));
    // A timestream block lights the other channel; no emitters, no texels.
    w.Set(20, 11, 20, BLOCK_TIMESTREAM);
    BuildGlowGrid(w, ox, oy, oz, g);
    CHECK(g.emitters.size() == 2 && at(21, 11, 20, 1) > 150 && at(21, 11, 20, 0) == 0);
    World none; none.Set(0, 0, 0, BLOCK_STONE);
    BuildGlowGrid(none, ox, oy, oz, g);
    CHECK(g.emitters.empty() && g.texels.empty());
}

static void TestSky() {
    printf("sky model and shadow projection\n");
    SkyState dawn = ComputeSky(0), noon = ComputeSky(1500), dusk = ComputeSky(3000), night = ComputeSky(3300);
    CHECK(fabsf(dawn.sunDir.y) < 1e-4f && dawn.sunDir.x > 0.99f);     // rises in the east (+X)
    CHECK(noon.sunDir.y > 0.8f && noon.sunDir.y < 0.9f && noon.sunDir.z < 0); // high (~60 degrees), tilted south
    CHECK(fabsf(dusk.sunDir.y) < 1e-3f && dusk.sunDir.x < -0.99f);    // sets in the west
    CHECK(night.sunDir.y < -0.8f && night.moonDir.y > 0.3f);          // moon up at night
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

    // Atmosphere (4.9): a white-gold high sun, an orange low one, none at
    // night; moonlight only at night; the sunset band only near sunset;
    // exposure lifted only at night; and no pops anywhere in the day.
    Atmosphere an = ComputeAtmosphere(noon), ad = ComputeAtmosphere(ComputeSky(2940)), ah = ComputeAtmosphere(night);
    CHECK(an.sunColor.x > ad.sunColor.x && an.sunColor.z / an.sunColor.x > 0.8f && ad.sunColor.z / ad.sunColor.x < 0.4f);
    CHECK(ah.sunColor.x == 0.0f && ah.moonColor.z > 0.1f && an.moonColor.z == 0.0f);
    CHECK(an.twilightAmount == 0.0f && ad.twilightAmount > 0.5f && ah.twilightAmount == 0.0f);
    CHECK(an.exposure == 1.0f && ah.exposure > 2.0f && ad.exposure < 1.5f);
    CHECK(an.zenith.z > an.zenith.x && an.horizon.x > an.zenith.x);    // deep blue overhead, paler at the horizon
    CHECK(an.ambientUp.x > an.ambientDown.x && ah.ambientUp.z > ah.ambientUp.x); // sky above; night light is blue
    Atmosphere prev = ComputeAtmosphere(ComputeSky(0));
    float worst = 0;
    for (int t = 1; t <= 3600; t++) {
        Atmosphere a = ComputeAtmosphere(ComputeSky((float)t));
        const Vec3* cur[] = { &a.sunColor, &a.moonColor, &a.zenith, &a.horizon, &a.ambientUp, &a.ambientDown };
        const Vec3* old[] = { &prev.sunColor, &prev.moonColor, &prev.zenith, &prev.horizon, &prev.ambientUp, &prev.ambientDown };
        for (int k = 0; k < 6; k++)
            worst = std::max({ worst, fabsf(cur[k]->x - old[k]->x), fabsf(cur[k]->y - old[k]->y), fabsf(cur[k]->z - old[k]->z) });
        worst = std::max({ worst, fabsf(a.exposure - prev.exposure), fabsf(a.twilightAmount - prev.twilightAmount) });
        prev = a;
    }
    CHECK(worst < 0.05f); // per second of game time: every change is a fade (the steepest, exposure at dusk, ~3%/s)
}

static void TestTheLine() {
    printf("the line\n");
    LineTuning t;
    const float dt = 1.0f / 60.0f;

    // Pivot: an hour lived at A outweighs a minute passing through B.
    LineState s;
    for (int i = 0; i < 3600 * 60 / 10; i++) UpdateLine(s, t, 100.0f, 13.0f, 100.0f, dt * 10); // 1 h at A
    for (int i = 0; i < 60 * 60; i++) UpdateLine(s, t, 900.0f, 13.0f, 100.0f, dt);           // 1 min at B
    CHECK(fabsf(s.pivotX - 112.0f) < 2.0f && fabsf(s.pivotZ - 112.0f) < 2.0f); // A's cell centre (96..128)

    // Two separate haunts: the pivot heads for the one with more time,
    // not the empty ground between them, and travels there rather than
    // jumping.
    {
        LineState h;
        for (int i = 0; i < 3600 * 6; i++) UpdateLine(h, t, 16.0f, 13.0f, 16.0f, 1.0f / 6.0f);    // 1 h at A (cell 0,0)
        CHECK(fabsf(h.pivotX - 16.0f) < 0.5f && fabsf(h.pivotZ - 16.0f) < 0.5f);
        for (int i = 0; i < 5400 * 6; i++) UpdateLine(h, t, 1016.0f, 13.0f, 16.0f, 1.0f / 6.0f);  // then 1.5 h at B, 1000 blocks east
        CHECK(fabsf(h.targetX - 1008.0f) < 1.0f);                     // B's cell centre (992..1024), not the midpoint
        CHECK(fabsf(h.pivotX - h.targetX) < 1.0f);                    // arrived by now
        // The trip itself: at most pivotMaxSpeed, so ~500 s for 1000 blocks.
        LineState h2;
        for (int i = 0; i < 3600 * 6; i++) UpdateLine(h2, t, 16.0f, 13.0f, 16.0f, 1.0f / 6.0f);
        bool steady = true; int steps = 0;
        while (h2.targetX < 500.0f && steps < 100000) { UpdateLine(h2, t, 1016.0f, 13.0f, 16.0f, 1.0f / 6.0f); steps++; }
        float prev = h2.pivotX;
        for (int i = 0; i < 60 * 6; i++) {
            UpdateLine(h2, t, 1016.0f, 13.0f, 16.0f, 1.0f / 6.0f);
            float moved = h2.pivotX - prev; prev = h2.pivotX;
            if (moved < 0 || moved > t.pivotMaxSpeed / 6.0f + 1e-3f) steady = false;
        }
        printf("    pivot trip: switched after %.0f s at B, then 60 s later at x=%.1f (steady %d)\n", steps / 6.0f, h2.pivotX, (int)steady);
        CHECK(steady && h2.pivotX > 16.0f && h2.pivotX < 1008.0f);   // under way, toward B, at walking pace
    }

    // Where the player is these days wins: 2 h at A, then 1.8 h at B.
    // Unfaded, A would still lead; with a 4 h half-life, B has taken over.
    {
        LineState f;
        for (int i = 0; i < 7200 * 2; i++) UpdateLine(f, t, 16.0f, 13.0f, 16.0f, 0.5f);
        for (int i = 0; i < 6480 * 2; i++) UpdateLine(f, t, 1016.0f, 13.0f, 16.0f, 0.5f);
        CHECK(fabsf(f.targetX - 1008.0f) < 1.0f);
        LineSaveData fd = SnapshotLine(f);
        float a = 0, b = 0;
        for (auto& kv : fd.dwell) { if (kv.second > 1000) (a == 0 ? a : b) = kv.second; }
        printf("    faded hours at the two haunts: %.2f %.2f\n", a / 3600.0f, b / 3600.0f);
    }

    // Spin follows the sense of movement around one's own centre.
    auto circle = [&](int dir) {
        LineState c;
        for (int i = 0; i < 600 * 60; i++) UpdateLine(c, t, 16.0f, 13.0f, 16.0f, dt); // settle a pivot
        float r = 10.0f, w = 0.3f * dir;
        for (int i = 0; i < 120 * 60; i++) {
            float a = w * i * dt;
            UpdateLine(c, t, c.pivotX + r * cosf(a), 13.0f, c.pivotZ + r * sinf(a), dt);
        }
        return c.spin;
    };
    CHECK(circle(+1) == +1); // angle increasing from +X toward +Z: counterclockwise from above
    CHECK(circle(-1) == -1);
    // Clockwise by default: standing still, or barely circling the other way.
    {
        LineState still;
        for (int i = 0; i < 600; i++) UpdateLine(still, t, 16.0f, 13.0f, 16.0f, dt);
        CHECK(still.spin == -1);
        still.player.angMom = t.ccwThreshold * 0.5f;
        UpdateLine(still, t, 16.0f, 13.0f, 16.0f, dt);
        CHECK(still.spin == -1);
    }

    // The line sweeps in the spin's direction.
    LineState sw; sw.player.angMom = -5;
    float before = 1.0f; sw.theta = before;
    UpdateLine(sw, t, 0.0f, 13.0f, 0.0f, 1.0f);
    CHECK(sw.spin == -1 && sw.theta < before);

    // Falloff: standing still, one decade of intensity per blocksPerDecade,
    // flat treads between steps.
    auto settle = [&](float dist) {
        LineState a; a.hasPivot = true; a.pivotX = 0; a.pivotZ = 0; a.theta = 0; // pivot at the origin; line along +X
        t.turnSeconds = 1e9f; t.pivotMaxSpeed = 0; // freeze the sweep and the pivot for this test
        for (int i = 0; i < 600; i++) UpdateLine(a, t, 5.0f, 13.0f, dist, dt);
        t.turnSeconds = 3600.0f; t.pivotMaxSpeed = LineTuning().pivotMaxSpeed;
        return a;
    };
    LineState on = settle(0.0f), near = settle(3.0f), dec1 = settle(6.3f), tread = settle(9.0f), dec2 = settle(12.3f);
    printf("    intensity at 0/3/6.3/9/12.3 blocks: %.3f %.3f %.3f %.3f %.4f\n", on.intensity, near.intensity, dec1.intensity, tread.intensity, dec2.intensity);
    CHECK(on.intensity > 0.99f);
    CHECK(fabsf(near.intensity - 1.0f) < 0.02f);          // still on the first tread
    CHECK(fabsf(dec1.intensity - 0.1f) < 0.01f);          // one decade down
    CHECK(fabsf(tread.intensity - 0.1f) < 0.01f);         // flat until the next riser
    CHECK(fabsf(dec2.intensity - 0.01f) < 0.002f);

    // Asymmetry: moving with the sweep stretches the falloff, against shrinks it.
    auto walk = [&](float vz) {
        LineState a; a.hasPivot = true; a.pivotX = 0; a.pivotZ = 0; a.theta = 0; a.player.angMom = 1e9; // pivot at the origin, ccw
        t.pivotMaxSpeed = 0;
        float z = 4.0f;
        for (int i = 0; i < 10; i++) { UpdateLine(a, t, 20.0f, 13.0f, z, dt); z += vz * dt; }
        t.pivotMaxSpeed = LineTuning().pivotMaxSpeed;
        return a.blocksPerDecade;
    };
    // Line along +X, ccw spin: at x = +20 it sweeps toward +Z.
    float with = walk(+4.5f), against = walk(-4.5f), still = walk(0.0f);
    printf("    blocks per decade: with %.2f, still %.2f, against %.2f\n", with, still, against);
    CHECK(with > still && still > against);
    CHECK(fabsf(with - t.decadeWith) < 0.5f && fabsf(against - t.decadeAgainst) < 0.2f);

    // Sky wobble: a rotation (orthonormal), none at zero intensity, and the
    // moon's ghost always weaker than the stars.
    float m[3][3];
    LineState q; q.intensity = 0; LineSkyWobble(q, t, 1.0f, m);
    CHECK(fabsf(m[0][0] - 1) < 1e-6f && fabsf(m[1][1] - 1) < 1e-6f && fabsf(m[0][1]) < 1e-6f);
    q.intensity = 1; q.theta = 0.7f; q.wobblePhase = 0.3f;
    float stars[3][3], ghost[3][3];
    LineSkyWobble(q, t, 1.0f, stars); LineSkyWobble(q, t, t.moonGhostScale, ghost);
    float det = stars[0][0] * (stars[1][1] * stars[2][2] - stars[1][2] * stars[2][1]) - stars[0][1] * (stars[1][0] * stars[2][2] - stars[1][2] * stars[2][0]) + stars[0][2] * (stars[1][0] * stars[2][1] - stars[1][1] * stars[2][0]);
    CHECK(fabsf(det - 1) < 1e-4f);
    CHECK(acosf(ghost[1][1]) < acosf(stars[1][1])); // smaller tilt of the vertical

    // Save/restore keeps pivot, spin and angle.
    LineSaveData d = SnapshotLine(s);
    LineState r; RestoreLine(r, t, d);
    CHECK(fabsf(r.pivotX - s.pivotX) < 1e-3f && fabsf(r.pivotZ - s.pivotZ) < 1e-3f && r.spin == s.spin && r.theta == s.theta);
}

static float TestTextWidth(const std::string& s, float scale) { return (float)s.size() * 8.0f * scale / 0.65f; }

static void TestEssence() {
    printf("essence network and map\n");
    CHECK(EssenceBand(0.5) == 0 && EssenceBand(9.99) == 0 && EssenceBand(10) == 1 && EssenceBand(999) == 2 && EssenceBand(1000) == 3);

    EssenceNetwork a, b;
    a.Reset(12345); b.Reset(12345);
    // Deterministic zones, spanning orders of magnitude.
    int bands[6] = {};
    for (int rx = -10; rx < 10; rx++) for (int rz = -10; rz < 10; rz++) {
        std::vector<EssenceNode> za = a.ZonesOfRegion(rx, rz), zb = b.ZonesOfRegion(rx, rz);
        CHECK(za.size() == zb.size());
        for (size_t i = 0; i < za.size() && i < zb.size(); i++) {
            CHECK(za[i].id == zb[i].id && za[i].x == zb[i].x && za[i].magnitude == zb[i].magnitude);
            CHECK(za[i].x >= rx * 128.0f && za[i].x < (rx + 1) * 128.0f);
            bands[std::min(5, EssenceBand(za[i].magnitude))]++;
        }
    }
    printf("    zones by band (400 regions): %d %d %d %d %d\n", bands[0], bands[1], bands[2], bands[3], bands[4]);
    CHECK(bands[0] + bands[1] > bands[2] + bands[3] + bands[4]); // mostly minor
    CHECK(bands[3] + bands[4] > 0);                              // but some strong

    // Discovery: nothing is known until the player comes close.
    EssenceNetwork n; n.Reset(777);
    CHECK(n.Nodes().empty());
    n.Update(-1000, -1000);
    size_t seen = n.Nodes().size();
    for (const EssenceNode& z : n.Nodes()) CHECK(sqrtf((z.x + 1000) * (z.x + 1000) + (z.z + 1000) * (z.z + 1000)) <= n.tuning.discoverRadius + 1e-3f);
    // Walk a long line: discovers what passes within the radius only.
    for (float x = -2000; x < 2000; x += 4) n.Update(x, 64.0f);
    CHECK(n.Nodes().size() > seen);
    for (const EssenceNode& z : n.Nodes()) CHECK(fabsf(z.z - 64.0f) <= n.tuning.discoverRadius + 1e-3f || (fabsf(z.x + 1000) < 60 && fabsf(z.z + 1000) < 60));

    // Attractors: built, so known at once; they draw from zones in reach.
    const EssenceNode* strong = nullptr;
    for (const EssenceNode& z : n.Nodes()) if (EssenceBand(z.magnitude) >= 2 && (!strong || z.magnitude > strong->magnitude)) strong = &z;
    CHECK(strong != nullptr);
    if (strong) {
        int ax = (int)strong->x + 10, az = (int)strong->z;
        n.AddAttractor(ax, 13, az);
        n.RefreshRoutes();
        int att = -1;
        for (int i = 0; i < (int)n.Nodes().size(); i++) if (n.Nodes()[i].kind == NodeKind::Attractor) att = i;
        CHECK(att >= 0);
        bool fed = false;
        for (const EssenceRoute& r : n.Routes()) if (r.b == att && r.style != RouteStyle::Planned && !r.bound) fed = true;
        CHECK(fed);
        CHECK(n.Nodes()[att].magnitude > 1.0);
        // Bound (curved) routes are only ever between distant zones.
        for (const EssenceRoute& r : n.Routes()) {
            float d = sqrtf(powf(n.Nodes()[r.a].x - n.Nodes()[r.b].x, 2) + powf(n.Nodes()[r.a].z - n.Nodes()[r.b].z, 2));
            if (r.bound) CHECK(d > n.tuning.naturalRouteReach);
        }
        // Hierarchy is grouping only: a parent is always stronger and near.
        for (const EssenceNode& x : n.Nodes()) if (x.parent >= 0) CHECK(n.Nodes()[x.parent].magnitude > x.magnitude);

        // Save/restore: same discovered set and attractors.
        EssenceNetwork::SaveData sd = n.Snapshot();
        EssenceNetwork r; r.Restore(777, sd);
        CHECK(r.Nodes().size() == n.Nodes().size());
        n.RemoveAttractor(ax, 13, az);
        CHECK(n.Nodes().size() == r.Nodes().size() - 1);
    }

    // Map: node size by decade; labels limited to the top N unless zoomed in.
    CHECK(MapNodeRadius(1000, 0.5f) - MapNodeRadius(100, 0.5f) > 2.5f);
    CHECK(MapNodeRadius(1e4, 0.5f) < 3.0f * MapNodeRadius(10, 0.5f));  // compressed, not linear
    n.RefreshRoutes();
    MapCamera cam; cam.centerX = 0; cam.centerZ = 64; cam.scale = 0.12f;
    MapTuning mt;
    MapDrawList dl;
    BuildMapDrawList(n, cam, mt, 1.0f, 0, 64, 0, 0, 64, TestTextWidth, 12.0f, dl);
    printf("    map at 0.12 px/block: %d nodes, %d labeled, %d routes, %d belts, %zu triangles\n",
           dl.nodesDrawn, dl.nodesLabeled, dl.routesDrawn, dl.belts, dl.tris.size() / 18);
    CHECK(dl.nodesDrawn > 0 && dl.nodesLabeled <= mt.labelTopNodes && dl.routesLabeled <= mt.labelTopRoutes);
    CHECK(dl.tris.size() % 18 == 0);
    for (const MapLabel& l : dl.labels) CHECK(l.text.find_first_of("0123456789") == std::string::npos); // qualitative only
    // Labels never overlap.
    for (size_t i = 0; i < dl.labels.size(); i++) for (size_t j = i + 1; j < dl.labels.size(); j++) {
        const MapLabel& p = dl.labels[i]; const MapLabel& q = dl.labels[j];
        float pw = TestTextWidth(p.text, p.scale), qw = TestTextWidth(q.text, q.scale);
        bool overlap = p.x < q.x + qw && q.x < p.x + pw && p.y < q.y + 12 && q.y < p.y + 12;
        CHECK(!overlap);
    }
    // Zoom keeps the point under the cursor fixed.
    MapCamera z = cam;
    float wx = MapWorldX(z, 300), wz = MapWorldZ(z, 200);
    MapZoomAt(z, 2.0f, 300, 200);
    CHECK(fabsf(MapWorldX(z, 300) - wx) < 1e-2f && fabsf(MapWorldZ(z, 200) - wz) < 1e-2f && fabsf(z.scale - 0.24f) < 1e-5f);
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
    TestMusicLevel();
    TestGlowLight();
    TestSky();
    TestTheLine();
    TestEssence();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
