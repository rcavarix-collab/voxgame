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
#include "../library.h"
#include "../mesher.h"
#include "../shapes.h"
#include "../icons.h"
#include "../sky.h"
#include "../theline.h"
#include "../pulse.h"
#include "../essence.h"
#include "../essencemap.h"
#include "../music_synth.h"
#include "../sfx_synth.h"
#include "../soundscape.h"
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

    // Surface maps: height (0-9a-z), shine and glow (0-9) after the pixels.
    {
        std::string rows8 = ""; for (int i = 0; i < 8; i++) rows8 += " aaaaaaaa\n";
        std::string hrows = ""; for (int i = 0; i < 8; i++) hrows += " 0123456z\n";
        std::string srows = ""; for (int i = 0; i < 8; i++) srows += " 00000009\n";
        VtexSet m;
        ParseVtex("texture ramp\nsize 8\npalette\n a 808080\npixels\n" + rows8 + "height\n" + hrows + "shine\n" + srows + "glow\n" + srows + "end\n", "maps.vtex", m);
        CHECK(m.errors.empty() && m.textures.size() == 1);
        if (!m.textures.empty()) {
            const VtexTexture& t = m.textures[0];
            CHECK(t.height.size() == 64 && fabsf(t.height[0]) < 1e-6f && fabsf(t.height[7] - 1.0f) < 1e-6f && fabsf(t.height[3] - 3.0f / 35.0f) < 1e-6f);
            CHECK(t.shine.size() == 64 && t.shine[7] == 1.0f && t.shine[0] == 0.0f && t.glow[7] == 1.0f);
        }
        VtexSet e;
        std::string badH = ""; for (int i = 0; i < 8; i++) badH += " 0000000!\n";
        ParseVtex("texture b1\nsize 8\npalette\n a 808080\npixels\n" + rows8 + "height\n" + badH + "end\n", "badh.vtex", e);           // bad digit
        ParseVtex("texture b2\nsize 8\npalette\n a 808080\npixels\n" + rows8 + "glow\n 0000000\nend\n", "short.vtex", e);            // short row
        ParseVtex("texture b3\nsize 8\npalette\n a 808080\npixels\n" + rows8 + "shine\n" + srows + "shine\n" + srows + "end\n", "twice.vtex", e); // twice
        ParseVtex("texture b4\nsize 8\npalette\n a 808080\npixels\n" + rows8 + "glow\n 00000000\nend\n", "few.vtex", e);            // too few rows
        CHECK(e.textures.empty() && e.errors.size() == 4);
        for (auto& x : e.errors) printf("    (expected) %s\n", x.c_str());
    }

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
    CHECK(t.layerCount == 14 + 51); // 14 procedural (foundation .. crystal) + 51 more names only the generated art provides (magenta without it)

    // The natural materials' art in the repo loads cleanly and covers
    // every natural block (no magenta fallback), with seamless wrap.
    {
        FILE* fp = fopen("../assets/textures/natural.vtex", "rb");
        CHECK(fp != nullptr);
        if (fp) {
            std::string text; char buf[4096]; size_t got;
            while ((got = fread(buf, 1, sizeof buf, fp)) > 0) text.append(buf, got);
            fclose(fp);
            VtexSet nat; ParseVtex(text, "natural.vtex", nat);
            CHECK(nat.errors.empty() && nat.textures.size() == 46 && nat.blocks.size() == 44);
            for (auto& tx : nat.textures) CHECK(tx.size == 32 && !tx.height.empty()); // one density for everything (32), all with relief
            // ...with the pulse-logistics set beside it (industry.vtex, Part VI).
            FILE* fi = fopen("../assets/textures/industry.vtex", "rb");
            CHECK(fi != nullptr);
            if (fi) {
                std::string itext;
                while ((got = fread(buf, 1, sizeof buf, fi)) > 0) itext.append(buf, got);
                fclose(fi);
                size_t before = nat.textures.size();
                ParseVtex(itext, "industry.vtex", nat);
                CHECK(nat.errors.empty() && nat.textures.size() == before + 8);
            }
            BlockTextureSet nt; BuildBlockTextures(nat, nt);
            CHECK(nt.warnings.empty());
            for (auto& w : nt.warnings) printf("    %s\n", w.c_str());
            // Every natural block, and every prop or piece (4.15), wears
            // authored art -- props may also borrow the industrial
            // placeholders (tube, machine, foundation).
            for (int id = BLOCK_SNOW; id < BLOCK_COUNT; id++)
                for (int f = 0; f < FACE_COUNT; f++) {
                    const std::string& name = nt.layerNames[nt.faceLayer[id][FACE_POS_Z][f]];
                    bool art = false; for (auto& tx : nat.textures) if (tx.name == name) art = true;
                    if (id >= BLOCK_MOSS_CLUMP && (name == "tube" || name == "machine" || name == "foundation")) art = true;
                    CHECK(art);
                    if (!art) printf("    %s: %s\n", g_blocks[id].name, name.c_str());
                }
            // Every natural texture except the log's cut end tiles: each row's
            // and column's wrap step is no bigger than the steps inside it.
            for (auto& tx : nat.textures) {
                if (tx.name == "log_top") continue;
                bool card = false; for (int id = 1; id < BLOCK_COUNT; id++) if (BlockIsCard((BlockID)id) && tx.name == g_blocks[id].name) card = true;
                if (card) continue; // plant cards stand alone; they don't tile
                auto d = [&](uint32_t a, uint32_t b) { return abs((int)(a >> 16 & 255) - (int)(b >> 16 & 255)) + abs((int)(a >> 8 & 255) - (int)(b >> 8 & 255)) + abs((int)(a & 255) - (int)(b & 255)); };
                const int N = tx.size;
                double maxC = 0, maxR = 0;
                for (int b = 1; b < N; b++) {
                    double c = 0, r = 0;
                    for (int i = 0; i < N; i++) { c += d(tx.rgb[i * N + b - 1], tx.rgb[i * N + b]); r += d(tx.rgb[(b - 1) * N + i], tx.rgb[b * N + i]); }
                    maxC = std::max(maxC, c / N); maxR = std::max(maxR, r / N);
                }
                double wc = 0, wr = 0;
                for (int i = 0; i < N; i++) { wc += d(tx.rgb[i * N + N - 1], tx.rgb[i * N]); wr += d(tx.rgb[(N - 1) * N + i], tx.rgb[i]); }
                wc /= N; wr /= N;
                bool ok = wc <= maxC * 1.05 + 1 && wr <= maxR * 1.05 + 1;
                if (!ok) printf("    seam in %s: across %.1f (inner max %.1f), down %.1f (inner max %.1f)\n", tx.name.c_str(), wc, maxC, wr, maxR);
                CHECK(ok);
            }
        }
    }
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

    // The natural placeholders tile seamlessly: the step across the wrap
    // edge (last column to first, last row to first) is no bigger than the
    // biggest step already inside the tile between logical pixels. And
    // dirt is brown: never greener than it is red.
    for (BlockID id : { BLOCK_STONE, BLOCK_DIRT, BLOCK_WOOD }) {
        const uint8_t* L = t.mips[0].data() + (size_t)t.faceLayer[id][FACE_POS_Z][FACE_POS_Z] * BLOCK_TEX_SIZE * BLOCK_TEX_SIZE * 4;
        const int S = BLOCK_TEX_SIZE, k = S / 16;
        auto px = [&](int x, int y) { return L + ((size_t)y * S + x) * 4; };
        auto diff = [&](const uint8_t* a, const uint8_t* b) { return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]); };
        auto colStep = [&](int x0, int x1) { double d = 0; for (int y = 0; y < S; y++) d += diff(px(x0, y), px(x1, y)); return d / S; };
        auto rowStep = [&](int y0, int y1) { double d = 0; for (int x = 0; x < S; x++) d += diff(px(x, y0), px(x, y1)); return d / S; };
        double maxCol = 0, maxRow = 0;
        for (int b = k; b < S; b += k) { maxCol = std::max(maxCol, colStep(b - 1, b)); maxRow = std::max(maxRow, rowStep(b - 1, b)); }
        double wrapX = colStep(S - 1, 0), wrapY = rowStep(S - 1, 0);
        printf("    %s: biggest inner step %.1f / %.1f, across the wrap %.1f / %.1f\n", g_blocks[id].name, maxCol, maxRow, wrapX, wrapY);
        CHECK(wrapX <= maxCol * 1.05 + 1 && wrapY <= maxRow * 1.05 + 1);
        if (id == BLOCK_DIRT) { bool green = false; for (int i = 0; i < S * S; i++) if (L[i * 4 + 1] > L[i * 4 + 2]) green = true; CHECK(!green); }
    }

    // Surface layers: flat and matte without maps; a height ramp rising
    // along u tilts the normal back toward -u; shine and glow carry over;
    // the smallest mip of a flat surface stays flat.
    {
        const size_t S = BLOCK_TEX_SIZE;
        CHECK(t.surface.size() == (size_t)t.mipCount && t.surface[0].size() == t.mips[0].size());
        const uint8_t* flat = t.surface[0].data() + (size_t)t.faceLayer[BLOCK_STONE][FACE_POS_Z][FACE_POS_Y] * S * S * 4;
        CHECK(flat[0] == 128 && flat[1] == 128 && flat[2] == 0 && flat[3] == 0);
        std::string rows = "", h = "", g = "";
        for (int i = 0; i < 16; i++) { rows += " aaaaaaaaaaaaaaaa\n"; h += " 0011223344556677\n"; g += " 0000000000000009\n"; }
        VtexSet ramp;
        ParseVtex("texture rampy\nsize 16\npalette\n a 808080\npixels\n" + rows + "height\n" + h + "glow\n" + g + "end\nblock stone\n all rampy\nend\n", "rampy.vtex", ramp);
        CHECK(ramp.errors.empty());
        BlockTextureSet rt; BuildBlockTextures(ramp, rt);
        const uint8_t* L = rt.surface[0].data() + (size_t)rt.faceLayer[BLOCK_STONE][FACE_POS_Z][FACE_POS_Y] * S * S * 4;
        const uint8_t* mid = L + (20 * S + 8) * 4;   // on a step between two height levels
        CHECK(mid[0] < 126 && mid[1] >= 127 && mid[1] <= 129); // leans toward -u, not along v
        CHECK(L[(20 * S + 63) * 4 + 3] == 255 && L[(20 * S + 10) * 4 + 3] == 0);  // glow only in the last column
        const uint8_t* top = rt.surface.back().data() + (size_t)rt.faceLayer[BLOCK_DIRT][FACE_POS_Z][FACE_POS_Y] * 4;
        CHECK(top[0] == 128 && top[1] == 128);
    }

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

// The save checksum (worldfile.cpp keeps its own private), for hand-built files.
static uint32_t Fnv1a(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
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
    LineSaveData ld; ld.cells = { { 5.5f, -7.25f, 12.5f }, { -300.0f, 41.0f, 600.0f } }; ld.angMom = -42.5; ld.theta = 1.25f;
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
    CHECK(d.line.cells.size() == 2 && d.line.cells[1].x == -300.0f && d.line.cells[1].z == 41.0f && d.line.cells[1].seconds == 600.0f && d.line.angMom == -42.5 && d.line.theta == 1.25f);
    for (auto& kv : d.chunks) {
        Chunk* orig = w.FindChunk(kv.first);
        CHECK(orig && orig->modified && ChunksEqual(*orig, *kv.second));
    }

    // Corruption is caught, not loaded.
    std::vector<uint8_t> broken = buf; broken[buf.size() / 2] ^= 0x40;
    SaveData d2; CHECK(DecodeSave(broken.data(), broken.size(), d2) == DecodeResult::BadChecksum);
    SaveData d3; CHECK(DecodeSave(buf.data(), 20, d3) != DecodeResult::Ok);

    // A v8 file's Line history (32-block cell keys, no positions) still
    // loads: each cell's time is placed at its centre. Built by swapping
    // this file's line cells for the v8 layout.
    {
        const size_t n = ld.cells.size();
        // After the cells: angMom (8) theta (4), essence: 2 zones (4 + 16), 1 attractor (4 + 12), checksum (4).
        size_t after = 8 + 4 + 4 + 16 + 4 + 12 + 4;
        size_t cellsAt = buf.size() - after - 12 * n;
        std::vector<uint8_t> v8(buf.begin(), buf.begin() + cellsAt);
        v8[4] = 8; v8[5] = v8[6] = v8[7] = 0;
        auto u64 = [&](uint64_t v) { for (int i = 0; i < 8; i++) v8.push_back((uint8_t)(v >> (8 * i))); };
        auto f32 = [&](float f) { uint32_t u; memcpy(&u, &f, 4); for (int i = 0; i < 4; i++) v8.push_back((uint8_t)(u >> (8 * i))); };
        u64(((uint64_t)(uint32_t)2 << 32) | (uint32_t)-1); f32(120.0f); // cell (2, -1): centre (80, -16)
        u64(((uint64_t)(uint32_t)-4 << 32) | (uint32_t)0); f32(30.0f);  // cell (-4, 0): centre (-112, 16)
        v8.insert(v8.end(), buf.end() - after, buf.end() - 4);
        uint32_t sum = Fnv1a(v8.data(), v8.size());
        for (int i = 0; i < 4; i++) v8.push_back((uint8_t)(sum >> (8 * i)));
        SaveData old;
        CHECK(DecodeSave(v8.data(), v8.size(), old) == DecodeResult::Ok);
        CHECK(old.version == 8 && old.line.cells.size() == 2);
        CHECK(old.line.cells.size() == 2 && old.line.cells[0].x == 80.0f && old.line.cells[0].z == -16.0f && old.line.cells[0].seconds == 120.0f);
        CHECK(old.line.cells.size() == 2 && old.line.cells[1].x == -112.0f && old.line.cells[1].z == 16.0f);
        CHECK(old.line.angMom == -42.5 && old.essence.attractors.size() == 3);
        LineState ls; LineTuning lt; RestoreLine(ls, lt, old.line);
        CHECK(fabsf(ls.pivotX - 80.0f) < 1e-3f && fabsf(ls.pivotZ + 16.0f) < 1e-3f); // the favourite, at its old centre
    }
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

static void TestMovement() {
    printf("sprint, crouch and power slide\n");
    World w; ResetWorldState(w);
    g_loadRadius = 2; g_worldGen = WorldGenParams(); g_worldGen.type = GEN_FLAT;
    Stream(w, 8, 8, 200);
    const int G = TerrainHeight(8, 8) + 1;   // feet level on the flat ground
    for (int x = 10; x <= 20; x++) for (int z = 5; z <= 11; z++) w.Set(x, G + 1, z, BLOCK_STONE); // a roof 1 block up: a crawlspace
    const float dt = 1.0f / 60.0f, EAST = 1.5707963f; // yaw pi/2: facing +X
    auto run = [&](Player& p, MoveInput in, float seconds) { for (int i = 0; i < (int)(seconds * 60); i++) UpdatePlayerPhysics(w, p, dt, in); };
    auto at = [&](float x, float z) { Player p; p.x = x; p.y = (float)G; p.z = z; p.yaw = EAST; run(p, MoveInput(), 0.2f); return p; };
    MoveInput walk; walk.fwd = true;
    MoveInput crawl = walk; crawl.crouch = true;
    MoveInput sprint = walk; sprint.sprint = true;

    // Standing, the crawlspace stops you at its mouth; crouched, you get in.
    Player p = at(6.5f, 8.5f);
    run(p, walk, 2.0f);
    CHECK(p.x > 9.5f && p.x < 9.8f && !p.crouching);
    run(p, crawl, 3.0f);
    CHECK(p.x > 12.0f && p.crouching && fabsf(p.y - G) < 1e-3f);
    // Letting go of crouch under the roof: still crouched (no room to stand).
    run(p, MoveInput(), 0.3f);
    CHECK(p.crouching && fabsf(p.y - G) < 1e-3f && p.eyeHeight < 0.8f);
    // Back out into the open, and you stand up by yourself.
    MoveInput back; back.back = true;
    run(p, back, 5.0f);
    CHECK(p.x < 9.7f && !p.crouching && p.eyeHeight > 1.5f);

    // Sprinting beats walking; crouching is slow.
    Player a = at(0.5f, 2.5f), b = at(0.5f, 2.5f), c = at(0.5f, 2.5f);
    run(a, walk, 1.0f); run(b, sprint, 1.0f); run(c, crawl, 1.0f);
    float dw = a.x - 0.5f, ds = b.x - 0.5f, dc = c.x - 0.5f;
    printf("    1 s: walk %.2f, sprint %.2f, crouch %.2f blocks\n", dw, ds, dc);
    CHECK(ds > dw * 1.3f && dc < dw * 0.5f && b.sprinting && !a.sprinting);

    // Power slide: sprint, then crouch -- a burst faster than the sprint,
    // bleeding off, ending crouched while crouch is held.
    Player s = at(-20.5f, 2.5f);
    run(s, sprint, 0.5f);
    MoveInput slide = sprint; slide.crouch = true;
    float x0 = s.x;
    run(s, slide, 0.25f);
    float burst = (s.x - x0) / 0.25f;
    printf("    slide: %.2f blocks/s just after starting (sprint %.2f)\n", burst, 7.5f);
    CHECK(PlayerSliding(s) && burst > 7.0f && s.crouching);
    // Looking to the side mid-slide leans the view toward where it's carrying you.
    s.yaw = 0.0f; // now facing +Z; the slide carries toward +X, i.e. the view's right
    run(s, slide, 0.3f);
    CHECK(s.roll > 0.08f && s.eyeHeight < 0.7f);
    run(s, slide, 1.5f);
    CHECK(!PlayerSliding(s) && s.crouching && fabsf(s.roll) < 0.02f);

    // Forgiving timing: crouch first and sprint a moment later slides; a
    // crouch just after letting go of sprint slides; a crouch long after doesn't.
    {
        MoveInput crouchWalk = walk; crouchWalk.crouch = true;
        MoveInput both = sprint; both.crouch = true;
        Player q = at(-20.5f, 2.5f);
        run(q, walk, 0.3f);
        run(q, crouchWalk, 0.15f);  // crouch pressed first...
        run(q, both, 0.05f);        // ...then sprint
        CHECK(PlayerSliding(q));
        Player r2 = at(-20.5f, 2.5f);
        run(r2, sprint, 0.5f);
        run(r2, walk, 0.2f);        // let go of sprint
        run(r2, crouchWalk, 0.05f); // crouch 0.2 s later
        CHECK(PlayerSliding(r2));
        Player late = at(-20.5f, 2.5f);
        run(late, sprint, 0.5f);
        run(late, walk, 1.0f);
        run(late, crouchWalk, 0.05f);
        CHECK(!PlayerSliding(late));
    }
    // Stepping up onto a slab moves the body at once but not the view: the
    // eye's world height is continuous, then glides up to standing height.
    {
        World sw; ResetWorldState(sw); Stream(sw, 40, 8, 300);
        int gy = TerrainHeight(40, 8);
        for (int z = 6; z <= 10; z++) sw.Set(42, gy + 1, z, BLOCK_STONE_SLAB);
        Player q; q.x = 40.5f; q.z = 8.5f; q.y = (float)(gy + 1); q.yaw = 1.5708f;
        MoveInput go; go.fwd = true;
        float prevEye = q.y + q.eyeHeight, worstJump = 0;
        bool stepped = false;
        for (int i = 0; i < 60; i++) {
            float y0 = q.y;
            UpdatePlayerPhysics(sw, q, 1.0f / 60.0f, go);
            if (q.y - y0 > 0.4f) stepped = true;
            float eye = q.y + q.eyeHeight;
            worstJump = std::max(worstJump, eye - prevEye);
            prevEye = eye;
        }
        CHECK(stepped && worstJump < 0.2f && fabsf(q.eyeHeight - PLAYER_EYE) < 0.02f);
    }
    // And a slide carries you straight under the roof.
    Player u = at(0.5f, 8.5f);
    run(u, sprint, 0.9f);           // up to speed, well short of the mouth at x = 9.7
    CHECK(u.x < 9.0f);
    MoveInput dive = sprint; dive.crouch = true;
    run(u, dive, 0.05f);
    MoveInput none;
    run(u, none, 1.5f);             // let go of everything: momentum does the rest
    printf("    slid under the roof to x = %.2f\n", u.x);
    CHECK(u.x > 10.5f && u.crouching && fabsf(u.y - G) < 1e-3f);
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

    // Plant cards (4.14): one quad, all corners at the base centre, the
    // layer flagged, corners in u/v; neighbours keep their faces.
    {
        World wp; wp.Set(3, 3, 3, BLOCK_FERN_FROND); wp.Set(4, 3, 3, BLOCK_STONE);
        BuildChunkMesh(wp, { 0, 0, 0 }, *wp.FindChunk({ 0, 0, 0 }), v, idx);
        int cards = 0;
        for (auto& x : v) if (x.layer & CARD_LAYER_BIT) { cards++; CHECK(x.x == 3 * 8 + 4 && x.y == 3 * 8 && x.z == 3 * 8 + 4); }
        CHECK(cards == 4 && v.size() == 4 + 24); // the stone keeps all six faces
        CHECK(!BlockSolid(BLOCK_FERN_FROND) && BlockIsCard(BLOCK_FERN_FROND));
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
        if (BlockIsCard((BlockID)id)) { // a plant card's icon is its picture, exactly
            const uint8_t* src = t.mips[0].data() + (size_t)t.faceLayer[id][FACE_POS_Z][FACE_POS_Z] * BLOCK_TEX_SIZE * BLOCK_TEX_SIZE * 4;
            CHECK(memcmp(src, &t.icons[(size_t)id * BLOCK_TEX_SIZE * 4], BLOCK_TEX_SIZE * 4) == 0);
            continue;
        }
        CHECK(alpha(0, 0) == 0 && alpha(BLOCK_TEX_SIZE - 1, 0) == 0); // transparent corners
        int opaque = 0, drawn = 0;
        for (int y = 0; y < BLOCK_TEX_SIZE; y++) for (int x = 0; x < BLOCK_TEX_SIZE; x++) { if (alpha(x, y) == 255) opaque++; if (alpha(x, y) >= 90) drawn++; }
        if (g_blocks[id].translucent && g_blocks[id].shape == SHAPE_CUBE) CHECK(drawn > 1000 && opaque < drawn); // see-through, but visible
        else if (g_blocks[id].translucent) CHECK(drawn > 20);             // a see-through prop: small, but there
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

static void TestLibrary() {
    printf("block library and hotbar\n");
    const int W = 960, H = 680, count = g_placeableList.count;   // the smallest window
    // Hotbar: ten slots, on screen, left to right, no overlap.
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        UiRect r = HotbarSlotRect(W, H, i);
        CHECK(r.x0 >= 0 && r.x1 <= W && r.y1 <= H);
        if (i) CHECK(r.x0 >= HotbarSlotRect(W, H, i - 1).x1);
        CHECK(HotbarSlotAt(W, H, (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2) == i);
    }
    CHECK(HotbarSlotAt(W, H, 5, 5) == -1);
    // Library: every placeable block reachable, the grid above the hotbar.
    LibraryLayout L = ComputeLibraryLayout(W, H, count);
    CHECK(L.grid.y1 <= HotbarSlotRect(W, H, 0).y0 && L.panel.y0 >= 0 && L.panel.x0 >= 0 && L.panel.x1 <= W);
    int maxScroll = L.rows - L.visibleRows;
    for (int i = 0; i < count; i++) {
        int scroll = std::min(maxScroll, i / L.columns);
        UiRect c = LibraryCellRect(L, scroll, i);
        CHECK(LibraryCellAt(L, count, scroll, (c.x0 + c.x1) / 2, (c.y0 + c.y1) / 2) == i);
    }
    // A click selects; a small wobble is still a click.
    LibraryGesture g;
    LibraryPress(g, 3, 100, 100); LibraryMove(g, 103, 98);
    LibraryResult r = LibraryRelease(g, -1);
    CHECK(r.outcome == LibraryOutcome::Select && r.entry == 3);
    // A drag onto a slot assigns it there.
    LibraryPress(g, 5, 100, 100); LibraryMove(g, 180, 400);
    r = LibraryRelease(g, 7);
    CHECK(r.outcome == LibraryOutcome::Assign && r.entry == 5 && r.slot == 7);
    // A drag dropped anywhere else does nothing; a press on empty space does nothing.
    LibraryPress(g, 5, 100, 100); LibraryMove(g, 300, 100);
    CHECK(LibraryRelease(g, -1).outcome == LibraryOutcome::None);
    LibraryPress(g, -1, 100, 100);
    CHECK(LibraryRelease(g, 2).outcome == LibraryOutcome::None);
    // The default hotbar holds ten distinct placeable blocks.
    BlockID d[HOTBAR_SLOTS]; DefaultHotbar(d);
    for (int i = 0; i < HOTBAR_SLOTS; i++) { CHECK(g_blocks[d[i]].placeable); for (int j = 0; j < i; j++) CHECK(d[i] != d[j]); }
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
    // What's shown can't strobe: fed notes at 8 a second (full on, full
    // off), the output never moves faster than the slew limit, settles to
    // a gentle ripple, and a single note swells and fades over about a second.
    {
        MusicGlow g; const float fdt = 1.0f / 60.0f;
        float maxStep = 0, lo = 1, hi = 0;
        for (int i = 0; i < 60 * 10; i++) {
            float before = g.shown;
            float v = MusicGlowStep(g, (i / 4) % 2 == 0 ? 1.0f : 0.0f, fdt); // 8 Hz square
            maxStep = std::max(maxStep, fabsf(v - before));
            if (i > 60 * 5) { lo = std::min(lo, v); hi = std::max(hi, v); }
        }
        printf("    8 Hz notes -> shown ripples %.2f..%.2f, fastest %.3f per frame\n", lo, hi, maxStep);
        CHECK(maxStep <= MUSIC_GLOW_MAX_RATE * fdt + 1e-5f);
        CHECK(hi - lo < 0.15f); // no flashing: at most a faint shimmer
        MusicGlow one; float peak = 0; int peakAt = 0, darkAgain = -1;
        for (int i = 0; i < 60 * 4; i++) {
            float v = MusicGlowStep(one, i < 6 ? 1.0f : 0.0f, fdt); // one 0.1 s note
            if (v > peak) { peak = v; peakAt = i; }
            if (darkAgain < 0 && i > peakAt && peak > 0 && v < peak * 0.2f) darkAgain = i;
        }
        CHECK(peak > 0.3f && peakAt >= 6 && darkAgain > 60); // a swell, not a blink: still glowing a second later
    }
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
    auto at = [&](int x, int y, int z, int ch) { return (int)g.texels[((size_t)(((z - oz) * GLOW_GRID + (y - oy)) * GLOW_GRID + (x - ox))) * 4 + ch]; };
    CHECK(g.emitters.size() == 1 && g.texels.size() == (size_t)GLOW_GRID * GLOW_GRID * GLOW_GRID * 4);
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
    w.Set(20, 11, 3, BLOCK_MAGMA_ROCK); // an ember: the third channel
    BuildGlowGrid(w, ox, oy, oz, g);
    CHECK(g.emitters.size() == 3 && at(21, 11, 3, 2) > 150 && at(21, 11, 3, 0) == 0 && at(21, 11, 3, 1) == 0);
    World none; none.Set(0, 0, 0, BLOCK_STONE);
    BuildGlowGrid(none, ox, oy, oz, g);
    CHECK(g.emitters.empty() && g.texels.empty());
}

static void TestSky() {
    printf("sky model and shadow projection\n");
    SkyState dawn = ComputeSky(0), noon = ComputeSky(1500), dusk = ComputeSky(3000), night = ComputeSky(3300);
    CHECK(fabsf(dawn.sunDir.y) < 1e-4f && dawn.sunDir.x > 0.99f);     // rises in the east (+X)
    CHECK(noon.sunDir.y > 0.99f && fabsf(noon.sunDir.z) < 1e-6f);   // straight overhead (the equator)
    // The compass holds it all together: the sun rises east and sets west,
    // the pole is north, a player at yaw 0 faces north and turns east.
    CHECK(Dot(dawn.sunDir, kEast) > 0.999f && Dot(dusk.sunDir, kWest) > 0.999f && Dot(CelestialPole(), kNorth) > 0.999f);
    // One sky: a star that sits where the sun rose is turned exactly with the
    // sun at every hour (the star field and the sun share their motion).
    for (float t = 0; t < 3600; t += 97) {
        SkyState st = ComputeSky(t);
        float R[3][3]; AxisAngleMatrix(CelestialPole(), st.starAngle, R);
        Vec3 star = { R[0][0], R[1][0], R[2][0] }; // R * east
        CHECK(Dot(star, st.sunDir) > 0.999f);
    }
    {
        Player facing; facing.yaw = 0; facing.pitch = 0;
        Vec3 fw, rt, upv; GetCameraVectors(facing, fw, rt, upv);
        CHECK(Dot(fw, kNorth) > 0.999f && Dot(rt, kEast) > 0.999f);
        CHECK(strcmp(CompassPoint(0.0f), "NORTH") == 0 && strcmp(CompassPoint(1.5708f), "EAST") == 0 &&
              strcmp(CompassPoint(3.1416f), "SOUTH") == 0 && strcmp(CompassPoint(-1.5708f), "WEST") == 0 &&
              strcmp(CompassPoint(0.785f), "NORTH-EAST") == 0);
    }
    CHECK(fabsf(dusk.sunDir.y) < 1e-3f && dusk.sunDir.x < -0.99f);    // sets in the west
    CHECK(night.sunDir.y < -0.8f && night.moonDir.y > 0.3f);          // moon up at night
    {   // ...and still up in the west at dawn (a new world's first sunrise), setting early in the morning
        SkyState d0 = ComputeSky(0.0f), d15 = ComputeSky(900.0f), late = ComputeSky(2700.0f);
        CHECK(d0.moonDir.y > 0.3f && d0.moonDir.x < 0 && d15.moonDir.y < 0 && late.moonDir.y < 0);
    }
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

static void TestPulse() {
    printf("pulse logistics\n");
    const float dt = 1.0f / 60.0f;
    PulseTuning t;
    // Runs `seconds` of play (a harvester gathers two a second).
    auto run = [&](PulseSystem& p, World& w, float seconds, double& beats) {
        for (int i = 0; i < (int)(seconds * 60); i++) { beats += dt; p.Tick(w, t, dt); }
    };
    auto place = [](World& w, PulseSystem& p, int x, int y, int z, BlockID id, uint8_t st = 0) { w.Set(x, y, z, id, st); p.OnPlaced(x, y, z, id); };

    // The pipe's shape: a run is one bar, a bend a node with two arms, and
    // an end joined on one side only has a mouth (with a collar) opposite.
    {
        ShapePoly polys[MAX_SHAPE_POLYS];
        uint8_t run = (1u << FACE_POS_X) | (1u << FACE_NEG_X), bend = (1u << FACE_POS_X) | (1u << FACE_POS_Y);
        CHECK(PipePolys(run, 0, polys) == 4);   // one seamless tube: no end faces where it joins on
        CHECK(PipePolys(bend, 0, polys) == 12); // a low-poly elbow: six walls, three pieces a side
        CHECK(PipeMouths(run, 0) == 0 && PipeMouths(bend, 0) == 0);
        CHECK(PipeMouths(1u << FACE_NEG_Z, 0) == (1u << FACE_POS_Z));
        CHECK(PipeMouths(0, FACE_POS_Y) == ((1u << FACE_POS_Y) | (1u << FACE_NEG_Y)));
        CHECK(PipePolys(1u << FACE_NEG_Z, 0, polys) == 10); // bar + collar
        uint8_t all = 63;
        CHECK(PipePolys(all, 0, polys) == 20);  // a tube through, four branches off its sides
        // Every elbow orientation stays on the grid, inside the cell, and
        // turns through a slanted length.
        for (int fa = 0; fa < FACE_COUNT; fa++)
            for (int fb = 0; fb < FACE_COUNT; fb++) {
                if (fa / 2 == fb / 2) continue;
                int np = PipePolys((uint8_t)((1u << fa) | (1u << fb)), 0, polys), slanted = 0;
                bool inside = true;
                for (int k = 0; k < np; k++) {
                    if (polys[k].shade >= 6) slanted++;
                    for (int j = 0; j < polys[k].count; j++) inside = inside && polys[k].v[j].x <= 8 && polys[k].v[j].y <= 8 && polys[k].v[j].z <= 8;
                }
                CHECK(np == 12 && slanted == 2 && inside);
            }
        // No doubled faces: no two polygons of any pipe overlap in the
        // same plane (facing either way).
        bool doubled = false;
        for (int twist = -1; twist <= 1; twist++)
        for (int mask = 0; mask < 64; mask++)
            for (int st = 0; st < FACE_COUNT; st++) {
                int np = PipePolys((uint8_t)mask, (uint8_t)st, polys, twist);
                CHECK(np <= MAX_SHAPE_POLYS);
                for (int a = 0; a < np; a++)
                    for (int b = a + 1; b < np; b++) {
                        const ShapePoly& A = polys[a]; const ShapePoly& B = polys[b];
                        if (A.shade >= 6 || B.shade >= 6) continue; // slanted facets: not in an axis plane
                        if (A.texFace / 2 != B.texFace / 2) continue; // same axis, either facing: back-to-back flickers too
                        int ax = A.texFace / 2; // 0 x, 1 y, 2 z
                        auto coord = [&](const ShapeVertex& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; };
                        if (coord(A.v[0], ax) != coord(B.v[0], ax)) continue;
                        int u = (ax + 1) % 3, w = (ax + 2) % 3;
                        // Sample the plane finely: a point strictly inside both is a doubled face.
                        auto inside = [&](const ShapePoly& p, float pu, float pw) {
                            int sign = 0;
                            for (int i = 0; i < p.count; i++) {
                                const ShapeVertex& e0 = p.v[i]; const ShapeVertex& e1 = p.v[(i + 1) % p.count];
                                float c = (coord(e1, u) - coord(e0, u)) * (pw - coord(e0, w)) - (coord(e1, w) - coord(e0, w)) * (pu - coord(e0, u));
                                if (fabsf(c) < 1e-6f) return false;
                                int sg = c > 0 ? 1 : -1;
                                if (sign && sg != sign) return false;
                                sign = sg;
                            }
                            return true;
                        };
                        for (int iu = 0; iu < 32 && !doubled; iu++)
                            for (int iw = 0; iw < 32 && !doubled; iw++) {
                                float pu = (iu + 0.37f) / 4.0f, pw = (iw + 0.61f) / 4.0f;
                                if (inside(A, pu, pw) && inside(B, pu, pw)) doubled = true;
                            }
                    }
            }
        CHECK(!doubled);
    }

    // Harvester -> three pipes -> store: one pulse a beat arrives.
    {
        World w; PulseSystem p; double beats = 0;
        place(w, p, 0, 10, 0, BLOCK_PULSE_HARVESTER);
        for (int x = 1; x <= 3; x++) place(w, p, x, 10, 0, BLOCK_PULSE_PIPE, FACE_POS_X);
        place(w, p, 4, 10, 0, BLOCK_PULSE_STORE);
        run(p, w, 10.0f, beats);
        int got = PulseStored(w, 4, 10, 0);
        printf("    10 s: gathered %lld, stored %d, in pipes %d, lost %lld\n", p.gathered, got, p.InFlight(), p.lost);
        CHECK(p.gathered >= 19 && p.gathered <= 21);
        CHECK(got >= 17 && got + p.InFlight() == (int)p.gathered && p.lost == 0);
        CHECK(PulseStored(w, 0, 10, 0) == 0); // nothing held back
        // Counts live in the block's data record, so they're saved with the world.
        Chunk* c = w.FindChunk(World::ToChunk(4, 10, 0));
        CHECK(c && c->data && c->modified);
        // Break the middle pipe: the first pipe is now an open end, and the
        // pipe beyond the gap has a mouth facing it -- the pulse jumps the gap.
        w.Set(2, 10, 0, BLOCK_AIR);
        long long before = p.delivered;
        run(p, w, 5.0f, beats);
        CHECK(p.caught >= 8 && p.delivered >= before + 8 && p.lost == 0);
        // Break that one too: now it flies straight at the store's side,
        // which is a surface, not a mouth -- gone.
        w.Set(3, 10, 0, BLOCK_AIR);
        before = p.delivered;
        run(p, w, 5.0f, beats);
        CHECK(p.delivered <= before + 2 && p.lost >= 8);
    }

    // An open end fires pulse straight out until a surface nulls it.
    {
        World w; PulseSystem p; double beats = 0;
        place(w, p, 0, 10, 0, BLOCK_PULSE_HARVESTER);
        for (int x = 1; x <= 2; x++) place(w, p, x, 10, 0, BLOCK_PULSE_PIPE, FACE_POS_X);
        w.Set(12, 10, 0, BLOCK_STONE); // a wall ten blocks out
        run(p, w, 5.0f, beats);
        std::vector<PulseView> v; p.Views(v);
        bool straight = true;
        for (auto& pv : v) if (fabsf(pv.y - 10.5f) > 1e-3f || fabsf(pv.z - 0.5f) > 1e-3f || pv.x > 12.0f) straight = false;
        printf("    open end: %lld gone into the wall, %d in the air\n", p.lost, p.InFlight());
        CHECK(p.lost >= 6 && straight && !v.empty()); // no gravity: level all the way
    }

    // ...or a mouth facing it catches it midair, and it goes on from there.
    // Two streams cross on the way; pulses aren't really there, so both get through.
    {
        World w; PulseSystem p; double beats = 0;
        place(w, p, 0, 10, 0, BLOCK_PULSE_HARVESTER);
        place(w, p, 1, 10, 0, BLOCK_PULSE_PIPE, FACE_POS_X);          // mouth faces +X
        place(w, p, 10, 10, 0, BLOCK_PULSE_PIPE, FACE_POS_X);         // joined to the store on +X: mouth faces -X
        place(w, p, 11, 10, 0, BLOCK_PULSE_STORE);
        place(w, p, 5, 10, -6, BLOCK_PULSE_HARVESTER);                 // a second stream along +Z, through (5, 10, 0)
        place(w, p, 5, 10, -5, BLOCK_PULSE_PIPE, FACE_POS_Z);
        place(w, p, 5, 10, 6, BLOCK_PULSE_PIPE, FACE_POS_Z);
        place(w, p, 5, 10, 7, BLOCK_PULSE_STORE);
        run(p, w, 10.0f, beats);
        int a = PulseStored(w, 11, 10, 0), b = PulseStored(w, 5, 10, 7);
        printf("    caught midair: %lld; stores %d and %d; lost %lld\n", p.caught, a, b, p.lost);
        CHECK(p.caught >= 30 && a >= 14 && b >= 14 && p.lost == 0);
    }

    // Outlets take turns: a store and a machine on one network share evenly;
    // a full machine is skipped; storage holds without limit (for now).
    {
        World w; PulseSystem p; double beats = 0;
        place(w, p, 0, 10, 0, BLOCK_PULSE_HARVESTER);
        for (int x = 1; x <= 3; x++) place(w, p, x, 10, 0, BLOCK_PULSE_PIPE, FACE_POS_X);
        place(w, p, 2, 11, 0, BLOCK_PULSE_STORE);
        place(w, p, 4, 10, 0, BLOCK_MACHINE); // a machine takes pulse too (32 until it has a use)
        run(p, w, 20.0f, beats);
        int s = PulseStored(w, 2, 11, 0), m = PulseStored(w, 4, 10, 0);
        printf("    shared: store %d, machine %d\n", s, m);
        CHECK(abs(s - m) <= 2 && s > 15);
        run(p, w, 150.0f, beats);
        CHECK(PulseStored(w, 4, 10, 0) == PulseCapacity(BLOCK_MACHINE) && PulseStored(w, 2, 11, 0) > 250);
        CHECK(p.lost == 0 && PulseStored(w, 0, 10, 0) <= 1);
        // With only the (full) machine to go to, the harvester holds a few
        // (what was already on its way to the store is lost with it).
        w.Set(2, 11, 0, BLOCK_AIR);
        run(p, w, 20.0f, beats);
        long long lostThen = p.lost;
        run(p, w, 10.0f, beats);
        printf("    store gone: harvester holds %d, %d moving, %lld lost\n", PulseStored(w, 0, 10, 0), p.InFlight(), p.lost);
        CHECK(PulseStored(w, 0, 10, 0) == PulseCapacity(BLOCK_PULSE_HARVESTER) && p.InFlight() == 0 && p.lost == lostThen && lostThen <= 2);
    }

    // Spin: a pulse takes it from the last pipe it went through, and the
    // store counts each kind.
    {
        World w; PulseSystem p; double beats = 0;
        place(w, p, 0, 10, 0, BLOCK_PULSE_HARVESTER);
        place(w, p, 1, 10, 0, BLOCK_PULSE_PIPE, FACE_POS_X);
        place(w, p, 2, 10, 0, BLOCK_PULSE_PIPE_CW, FACE_POS_X);
        place(w, p, 3, 10, 0, BLOCK_PULSE_STORE);
        place(w, p, 0, 10, 2, BLOCK_PULSE_HARVESTER);
        place(w, p, 1, 10, 2, BLOCK_PULSE_PIPE_CCW, FACE_POS_X);
        place(w, p, 2, 10, 2, BLOCK_PULSE_PIPE, FACE_POS_X);          // ...and then a plain one takes it away
        place(w, p, 3, 10, 2, BLOCK_PULSE_STORE);
        place(w, p, 0, 10, 4, BLOCK_PULSE_HARVESTER);
        place(w, p, 1, 10, 4, BLOCK_PULSE_PIPE_CCW, FACE_POS_X);       // an anticlockwise one, left open...
        place(w, p, 8, 10, 4, BLOCK_PULSE_PIPE, FACE_POS_X);          // ...caught by a plain pipe: the air keeps the spin, the pipe takes it
        place(w, p, 9, 10, 4, BLOCK_PULSE_STORE);
        place(w, p, 0, 10, 6, BLOCK_PULSE_HARVESTER);
        place(w, p, 1, 10, 6, BLOCK_PULSE_PIPE_CCW, FACE_POS_X);
        place(w, p, 8, 10, 6, BLOCK_PULSE_PIPE_CCW, FACE_POS_X);      // caught by a twisted one: anticlockwise
        place(w, p, 9, 10, 6, BLOCK_PULSE_STORE);
        run(p, w, 10.0f, beats);
        PulseCounts a = PulseHeld(w, 3, 10, 0), b = PulseHeld(w, 3, 10, 2), c = PulseHeld(w, 9, 10, 4), d = PulseHeld(w, 9, 10, 6);
        printf("    spin: cw store %d/%d/%d, plain-after-ccw %d/%d/%d, caught plain %d/%d/%d, caught ccw %d/%d/%d\n",
               a.n[0], a.n[1], a.n[2], b.n[0], b.n[1], b.n[2], c.n[0], c.n[1], c.n[2], d.n[0], d.n[1], d.n[2]);
        CHECK(a.n[1] > 15 && a.n[0] == 0 && a.n[2] == 0);
        CHECK(b.n[0] > 15 && b.n[1] == 0 && b.n[2] == 0);
        CHECK(c.n[0] > 10 && c.n[2] == 0);
        CHECK(d.n[2] > 10 && d.n[0] == 0);
        // Flying pulses show their spin (the corkscrew is drawn from it).
        std::vector<PulseView> v; p.Views(v);
        bool spun = false; for (auto& pv : v) if (pv.flying && pv.spin == -1) spun = true;
        CHECK(spun);
    }

    // The Line bends time: a harvester where time runs 5x gathers 5x.
    {
        World w; PulseSystem p;
        place(w, p, 0, 10, 0, BLOCK_PULSE_HARVESTER);
        place(w, p, 1, 10, 0, BLOCK_PULSE_STORE);
        for (int i = 0; i < 600; i++) p.Tick(w, t, dt, [](int, int, int) { return 5.0f; });
        printf("    at 5x time: %lld gathered in 10 s\n", p.gathered);
        CHECK(p.gathered >= 98 && p.gathered <= 101 && PulseStored(w, 1, 10, 0) >= 97);
        LineState L; LineTuning lt; L.pivotX = 0; L.pivotZ = 0; L.theta = 0; // along +X through the origin
        CHECK(LineTimeRateAt(L, lt, 50.0f, 0.5f) > 25.0f && LineTimeRateAt(L, lt, 50.0f, 60.0f) < 1.01f);
    }

    // Twisted pipes: a raised thread winds round a run, one turn a block;
    // the two hands are mirror images, not the same mesh.
    {
        ShapePoly cw[MAX_SHAPE_POLYS], ccw[MAX_SHAPE_POLYS];
        uint8_t run2 = (1u << FACE_POS_X) | (1u << FACE_NEG_X);
        int ncw = PipePolys(run2, 0, cw, 1), nccw = PipePolys(run2, 0, ccw, -1);
        CHECK(ncw == 4 + 4 * 6 && nccw == ncw);
        bool same = true;
        for (int i = 0; i < ncw && same; i++) {
            bool found = false;
            for (int j = 0; j < nccw && !found; j++) {
                bool all = true;
                for (int k = 0; k < cw[i].count; k++) {
                    bool hit = false;
                    for (int m = 0; m < ccw[j].count; m++) hit = hit || (cw[i].v[k].x == ccw[j].v[m].x && cw[i].v[k].y == ccw[j].v[m].y && cw[i].v[k].z == ccw[j].v[m].z);
                    all = all && hit;
                }
                found = all;
            }
            same = found;
        }
        CHECK(!same);
    }

    // A harvester in a chunk that returns from storage is found again.
    {
        PulseSystem p; Chunk c;
        c.blocks[Chunk::LocalIndex(3, 4, 5)] = BLOCK_PULSE_HARVESTER;
        p.OnChunkArrived({ 1, 0, -1 }, c);
        CHECK(p.Harvesters() == 1);
    }
}

static void TestTheLine() {
    printf("the line\n");
    LineTuning t;
    const float dt = 1.0f / 60.0f;

    // Pivot: an hour lived at A outweighs a minute passing through B.
    LineState s;
    for (int i = 0; i < 3600 * 60 / 10; i++) UpdateLine(s, t, 100.0f, 13.0f, 100.0f, dt * 10); // 1 h at A
    for (int i = 0; i < 60 * 60; i++) UpdateLine(s, t, 900.0f, 13.0f, 100.0f, dt);           // 1 min at B
    CHECK(fabsf(s.pivotX - 100.0f) < 0.5f && fabsf(s.pivotZ - 100.0f) < 0.5f); // A itself, not its cell's centre

    // x and z alike: pottering about an off-centre spot puts the pivot on
    // that spot, and a spot straddling a cell edge is found on the edge.
    {
        LineState p;
        for (int i = 0; i < 1200 * 6; i++) { float a = i * 0.37f; UpdateLine(p, t, 5.0f + 3.0f * cosf(a), 13.0f, 27.0f + 3.0f * sinf(a * 1.3f), 1.0f / 6.0f); }
        printf("    off-centre haunt at (5, 27): pivot %.2f, %.2f\n", p.pivotX, p.pivotZ);
        CHECK(fabsf(p.pivotX - 5.0f) < 1.0f && fabsf(p.pivotZ - 27.0f) < 1.0f);
        LineState e;
        for (int i = 0; i < 1200 * 6; i++) UpdateLine(e, t, (i & 1) ? 14.0f : 18.0f, 13.0f, -40.0f, 1.0f / 6.0f); // either side of x = 16
        CHECK(fabsf(e.pivotX - 16.0f) < 0.5f && fabsf(e.pivotZ + 40.0f) < 0.5f);
    }

    // Two separate haunts: the pivot heads for the one with more time,
    // not the empty ground between them, and travels there rather than
    // jumping.
    {
        LineState h;
        for (int i = 0; i < 3600 * 6; i++) UpdateLine(h, t, 16.0f, 13.0f, 16.0f, 1.0f / 6.0f);    // 1 h at A (cell 0,0)
        CHECK(fabsf(h.pivotX - 16.0f) < 0.5f && fabsf(h.pivotZ - 16.0f) < 0.5f);
        for (int i = 0; i < 5400 * 6; i++) UpdateLine(h, t, 1016.0f, 13.0f, 16.0f, 1.0f / 6.0f);  // then 1.5 h at B, 1000 blocks east
        CHECK(fabsf(h.targetX - 1016.0f) < 1.0f);                     // B, not the midpoint
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
        CHECK(steady && h2.pivotX > 16.0f && h2.pivotX < 1016.0f);   // under way, toward B, at walking pace
    }

    // Where the player is these days wins: 1 h at A, then 40 min at B.
    // Unfaded, A would still lead; with a 30 min half-life, B has taken over.
    {
        LineState f;
        for (int i = 0; i < 3600 * 2; i++) UpdateLine(f, t, 16.0f, 13.0f, 16.0f, 0.5f);
        for (int i = 0; i < 2400 * 2; i++) UpdateLine(f, t, 1016.0f, 13.0f, 16.0f, 0.5f);
        CHECK(fabsf(f.targetX - 1016.0f) < 1.0f);
        LineSaveData fd = SnapshotLine(f);
        float a = 0, b = 0;
        for (auto& c : fd.cells) { if (c.seconds > 300) (a == 0 ? a : b) = c.seconds; }
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
    CHECK(fabsf(r.pivotX - s.pivotX) < 1e-2f && fabsf(r.pivotZ - s.pivotZ) < 1e-2f && r.spin == s.spin && r.theta == s.theta);

    // The sky clock. On the line the sky races (faster the closer); off
    // it, the sky finds its way back into step by the shorter way: a little
    // ahead, it runs slow; more than half a day ahead, it runs on round to
    // the next day, slowing as it gets there. Far off, it keeps time.
    {
        auto rateAt = [&](float dist) { LineState a = settle(dist); return a.skyRate; };
        float r0 = rateAt(0.0f), r6 = rateAt(6.3f), r12 = rateAt(12.3f), r30 = rateAt(30.0f);
        printf("    sky rate at 0/6.3/12.3/30 blocks: %.2f %.2f %.2f %.2f\n", r0, r6, r12, r30);
        CHECK(r0 > 25.0f && r0 > r6 && r6 > r12 && r12 > r30 && r30 > 0.99f && r30 < 1.05f);
        t.turnSeconds = 1e9f; t.pivotMaxSpeed = 0;
        auto onLine = [&](float seconds) {
            LineState a; a.hasPivot = true; a.theta = 0;
            for (int i = 0; i < (int)(seconds * 60); i++) UpdateLine(a, t, 5.0f, 13.0f, 0.0f, dt);
            return a;
        };
        // A little ahead: the sky runs slow once the player walks off.
        LineState a = onLine(20.0f);
        float lead = a.skyLead;
        for (int i = 0; i < 60 * 10; i++) UpdateLine(a, t, 5.0f, 13.0f, 200.0f, dt); // the rush dies down (the speed eases)
        float lead10 = a.skyLead;
        for (int i = 0; i < 60 * 10; i++) UpdateLine(a, t, 5.0f, 13.0f, 200.0f, dt);
        printf("    20 s on the line: %.0f s ahead; 10/20 s away: %.0f / %.0f s ahead, rate %.2f\n", lead, lead10, a.skyLead, a.skyRate);
        CHECK(lead > 400.0f && lead < 0.5f * DAY_LENGTH_SECONDS);
        CHECK(a.skyRate < 0.2f && a.skyLead < lead10 - 8.0f);
        for (int i = 0; i < 3600; i++) UpdateLine(a, t, 5.0f, 13.0f, 200.0f, 1.0f);
        CHECK(a.skyLead < 1.0f && fabsf(a.skyRate - 1.0f) < 0.02f);
        // Well ahead: it runs on to the next day, slowing, then keeps time.
        LineState b = onLine(90.0f);
        float leadB = b.skyLead;
        for (int i = 0; i < 60 * 10; i++) UpdateLine(b, t, 5.0f, 13.0f, 200.0f, dt);
        float coast = b.skyRate;
        bool slowing = true; float prev = b.skyRate; int steps = 0;
        while (b.skyLead > 0.5f * DAY_LENGTH_SECONDS && steps < 60 * 3600) {
            UpdateLine(b, t, 5.0f, 13.0f, 200.0f, dt); steps++;
            if (b.skyRate > prev + 1e-3f) slowing = false;
            prev = b.skyRate;
        }
        printf("    90 s on the line: %.0f s ahead; coasting at %.1fx, back in step after %.0f s more\n", leadB, coast, steps * dt);
        CHECK(leadB > 0.5f * DAY_LENGTH_SECONDS && coast > 1.5f && slowing && steps < 60 * 600);
        for (int i = 0; i < 600; i++) UpdateLine(b, t, 5.0f, 13.0f, 200.0f, 1.0f);
        CHECK(b.skyLead < 1.0f && fabsf(b.skyRate - 1.0f) < 0.02f);
        t.turnSeconds = 3600.0f; t.pivotMaxSpeed = LineTuning().pivotMaxSpeed;
    }
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


// ---------------------------------------------------------------------
// World sound palette (docs/SOUND_PALETTE.md)

static double FindChordTime(int chord, double from) {
    MusicHarmony h, h2;
    for (double t = from; t < from + 200; t += 0.25) {
        MusicHarmonyAt(t, &h); MusicHarmonyAt(t + 3.0, &h2);
        if (h.chord == chord && !h.blending && h2.chord == chord && !h2.blending) return t;
    }
    return -1;
}

static bool InSafeSet(int chord, bool anchor, double hz) {
    int midi = (int)lround(69 + 12 * log2(hz / 440.0));
    int pc = ((midi % 12) + 12) % 12;
    if (anchor) return pc == 2 || pc == 7 || pc == 9;
    static const int sets[5][7] = { { 2, 4, 5, 7, 9, 0, -1 }, { 7, 9, 0, 2, 5, -1, -1 }, { 4, 7, 9, 11, 2, -1, -1 },
                                    { 9, 0, 2, 4, 7, -1, -1 }, { 2, 4, 5, 7, 9, 0, -1 } };
    for (int k = 0; k < 7; k++) if (sets[chord][k] == pc) return true;
    return false;
}

// Renders `seconds` of a palette from music time t0 (the clock running).
static std::vector<float> RenderPalette(SoundPalette& p, double t0, double seconds) {
    std::vector<float> out((size_t)(seconds * 44100) / 512 * 512);
    for (size_t i = 0; i < out.size(); i += 512) p.Render(out.data() + i, 512, t0 + i / 44100.0, true);
    return out;
}

static void TestMusicHarmony() {
    printf("music harmony + colour\n");
    MusicHarmony h;
    MusicHarmonyAt(20, &h);
    CHECK(h.section == MUSIC_DAWN && h.chord == MUSIC_DM9 && !h.pulsed);
    MusicHarmonyAt(1500, &h);
    CHECK(h.section == MUSIC_MIDDAY && h.pulsed && fabs(h.bpm - 124) < 1e-9);
    MusicHarmonyAt(3400, &h);
    CHECK(h.section == MUSIC_NIGHT && !h.pulsed);
    for (int c = 0; c < 4; c++) CHECK(FindChordTime(c, 1200) > 0); // every chord of the cycle shows up in Midday
    // Beats advance at the section's tempo.
    MusicHarmony a, b; MusicHarmonyAt(1500, &a); MusicHarmonyAt(1501, &b);
    CHECK(fabs((b.beat - a.beat) - 124.0 / 60.0) < 1e-6);
    // The neutral colour renders the track exactly as composed.
    const int N = 11025;
    std::vector<int16_t> x(N), y(N), z(N);
    MusicState s1, s2, s3; ResetMusicState(&s1); ResetMusicState(&s2); ResetMusicState(&s3);
    MusicColour neutral, far; far.positive = -1; far.activity = 1; far.mechanical = 1;
    for (int k = 0; k < 8; k++) {
        GenerateMusicChunk(1500 + k * 0.25, N, 1.0, &s1, x.data());
        GenerateMusicChunk(1500 + k * 0.25, N, 1.0, &s2, y.data(), &neutral);
        GenerateMusicChunk(1500 + k * 0.25, N, 1.0, &s3, z.data(), &far);
    }
    CHECK(x == y);
    // A far colour changes it, but only a little (small, bounded shifts).
    double diff = 0, ref = 0;
    for (int i = 0; i < N; i++) { diff += fabs((double)x[i] - z[i]); ref += fabs((double)x[i]); }
    CHECK(diff > 0 && diff < ref * 0.8);
}

static void TestSoundPalette() {
    printf("sound palette\n");
    const double chordT[4] = { FindChordTime(MUSIC_DM9, 1200), FindChordTime(MUSIC_G7SUS4, 1200),
                               FindChordTime(MUSIC_EM7, 1200), FindChordTime(MUSIC_A7SUS4, 1200) };
    // Every sound, under every chord, at the axis extremes: every pitch in
    // the chord's safe set, never above the -21 dB ceiling.
    int unsafe = 0, loud = 0, silentTonal = 0;
    const double ceiling = pow(10.0, (-21.0 + 0.5) / 20.0);
    for (int id = 0; id < SND_COUNT; id++)
        for (int c = 0; c < 4; c++)
            for (int corner = 0; corner < 4; corner++) {
                SoundPalette p;
                SoundAxes ax; ax.positive = (corner & 1) ? 1.0f : -1.0f; ax.activity = 0.5f; ax.mechanical = (corner & 2) ? 1.0f : 0.0f;
                p.SetAxes(ax);
                AmbientScene sc; sc.machines = 1; sc.musicBlockCount = 1; sc.musicBlockKey[0] = 3;
                p.SetScene(sc);
                std::vector<float> warm(512);
                p.Render(warm.data(), 512, chordT[c], true);
                SoundCue cue; cue.id = (SoundId)id; cue.material = MAT_STONE; cue.slot = c * 3; cue.strength = 1;
                p.Play(cue);
                if (id == SND_SLIDE) { RenderPalette(p, chordT[c], 0.5); p.Release(SND_SLIDE); }
                std::vector<float> out = RenderPalette(p, chordT[c] + 0.0116, 5.0);
                MusicHarmony h; MusicHarmonyAt(chordT[c], &h);
                double scale = h.masterGain * 0.78, peak = 0;
                for (float v : out) peak = std::max(peak, (double)fabs(v) / scale);
                if (peak > ceiling) { loud++; printf("  loud: %s %.1f dB\n", SoundName((SoundId)id), 20 * log10(peak)); }
                SoundPalette::NoteLog log[64];
                int n = p.RecentNotes(log, 64);
                for (int i = 0; i < n; i++)
                    if (!InSafeSet(log[i].chord, log[i].anchor, log[i].hz)) { unsafe++; printf("  unsafe: %s %.1f Hz\n", SoundName(log[i].id), log[i].hz); }
                (void)silentTonal;
            }
    CHECK(unsafe == 0);
    CHECK(loud == 0);

    // Gestures: a run of Sets climbs the ladder, a run of Takes falls.
    {
        SoundPalette p;
        SoundAxes ax; ax.positive = 0.5f; p.SetAxes(ax);
        std::vector<float> buf(512);
        double t = chordT[0];
        p.Render(buf.data(), 512, t, true);
        std::vector<double> hz;
        for (int i = 0; i < 4; i++) {
            SoundCue c; c.id = SND_SET; p.Play(c);
            SoundPalette::NoteLog log[64]; int n = p.RecentNotes(log, 64);
            hz.push_back(log[n - 1].hz);
            for (int k = 0; k < 26; k++) { t += 512 / 44100.0; p.Render(buf.data(), 512, t, true); } // ~0.3 s
        }
        CHECK(hz[1] > hz[0] && hz[2] > hz[1] && hz[3] > hz[2]);
        for (int k = 0; k < 400; k++) { t += 512 / 44100.0; p.Render(buf.data(), 512, t, true); } // the gesture ends
        std::vector<double> down;
        for (int i = 0; i < 3; i++) {
            SoundCue c; c.id = SND_TAKE; p.Play(c);
            SoundPalette::NoteLog log[64]; int n = p.RecentNotes(log, 64);
            down.push_back(log[n - 2].hz); // each Take logs two notes; the first is its ladder step
            for (int k = 0; k < 26; k++) { t += 512 / 44100.0; p.Render(buf.data(), 512, t, true); }
        }
        CHECK(down[1] < down[0] && down[2] < down[1]);
    }
    // Merge: two onsets inside 30 ms are one sound, not a flam.
    {
        SoundPalette p;
        std::vector<float> buf(512);
        p.Render(buf.data(), 512, chordT[0], true);
        SoundCue c; c.id = SND_SLOT; c.slot = 4;
        p.Play(c);
        int before = p.ActiveVoices();
        p.Play(c);
        CHECK(p.ActiveVoices() == before);
    }
    // Determinism: the same inputs render the same samples.
    {
        SoundPalette a, b;
        AmbientScene sc; sc.plants = 1; sc.water = 0.5f;
        SoundAxes ax; ax.activity = 0.8f; ax.positive = 0.6f; ax.mechanical = 0.1f;
        for (SoundPalette* p : { &a, &b }) { p->SetAxes(ax); p->SetScene(sc); p->SetAmbientEnabled(true); }
        SoundCue c; c.id = SND_UNVEIL;
        a.Play(c); b.Play(c);
        CHECK(RenderPalette(a, 700, 12) == RenderPalette(b, 700, 12));
    }
    // Density: calm spends far less of the ambient budget than busy, and
    // stays within 1 event per 4 bars (plus the occasional rare colour).
    {
        int counts[2];
        for (int k = 0; k < 2; k++) {
            SoundPalette p;
            AmbientScene sc; sc.plants = 1; sc.water = 0.4f; sc.machines = 0.5f;
            SoundAxes ax; ax.activity = k ? 1.0f : 0.0f; ax.positive = 0.5f; ax.mechanical = 0.3f;
            p.SetAxes(ax); p.SetScene(sc); p.SetAmbientEnabled(true);
            RenderPalette(p, 1300, 128.0); // 66 bars at 124 BPM
            counts[k] = p.ScheduledAmbientEvents();
        }
        CHECK(counts[0] >= 8 && counts[0] <= 66 / 4 + 4);
        CHECK(counts[1] > counts[0] * 3);
    }
    // Stereo placement: a block set to the listener's right sounds louder
    // on the right; Mono centres everything; an unplaced sound sits centre.
    {
        auto energy = [&](bool placeRight, bool mono, float& l, float& r) {
            SoundPalette p;
            p.SetListener(0, 0, 0, 0); // facing +Z: right is +X
            p.SetMono(mono);
            std::vector<float> lr(1024);
            p.RenderStereo(lr.data(), 512, chordT[0], true);
            SoundCue c; c.id = SND_SET; c.material = MAT_STONE;
            if (placeRight) { c.placed = true; c.x = 4; c.y = 0; c.z = 0; }
            p.Play(c);
            lr.assign(2 * 44100, 0.0f);
            for (int i = 0; i < 44100; i += 512) p.RenderStereo(lr.data() + 2 * i, std::min(512, 44100 - i), chordT[0] + i / 44100.0, true);
            l = r = 0;
            for (int i = 0; i < 44100; i++) { l += lr[2 * i] * lr[2 * i]; r += lr[2 * i + 1] * lr[2 * i + 1]; }
        };
        float l, r;
        energy(true, false, l, r);  CHECK(r > l * 2.0f);
        energy(true, true, l, r);   CHECK(fabsf(l - r) <= 1e-6f * (l + r) + 1e-12f);
        energy(false, false, l, r); CHECK(fabsf(l - r) <= 0.3f * (l + r)); // centred; only the ping-pong echo leans
    }
    // Footsteps keep the beat: walking steps land one per beat, sprinting
    // one per 8th, crouching every other beat; stopping stops them.
    {
        int counts[4] = {};
        for (int gait = 1; gait <= 3; gait++) {
            SoundPalette p;
            p.SetAmbientEnabled(true);
            p.SetGait(gait, gait == 3 ? MAT_STONE : MAT_EARTH);
            RenderPalette(p, 1500, 8.0);   // 8 s at 124 BPM = 16.5 beats
            counts[gait] = p.PlayedCount(SND_FOOTFALL);
            p.SetGait(SoundPalette::GAIT_NONE, MAT_EARTH);
            RenderPalette(p, 1508, 2.0);
            CHECK(p.PlayedCount(SND_FOOTFALL) == counts[gait]);
        }
        CHECK(counts[2] >= 15 && counts[2] <= 18);
        CHECK(counts[3] >= 31 && counts[3] <= 35);
        CHECK(counts[1] >= 7 && counts[1] <= 9);
    }
    // Pausing fades everything to silence.
    {
        SoundPalette p;
        std::vector<float> buf(512);
        p.Render(buf.data(), 512, chordT[1], true);
        SoundCue c; c.id = SND_BLOOM; p.Play(c);
        RenderPalette(p, chordT[1], 1.0);
        p.FadeOut(0.3f);
        std::vector<float> out = RenderPalette(p, chordT[1] + 1, 1.0);
        float tail = 0;
        for (size_t i = out.size() / 2; i < out.size(); i++) tail = std::max(tail, fabsf(out[i]));
        CHECK(tail == 0.0f);
        CHECK(p.ActiveVoices() == 0);
    }
}

static void TestSoundscape() {
    printf("soundscape axes\n");
    World w;
    ResetWorldState(w);
    Stream(w, 8, 8, 300);
    int ground = TerrainHeight(8, 8);
    auto settle = [&](Soundscape& s, int seconds) {
        for (int f = 0; f < seconds * 60; f++) {
            s.CensusStep(w, 8, ground + 1, 8);
            SoundscapeInput in; in.dt = 1.0f / 60; in.musicSection = MUSIC_MORNING;
            s.Update(in);
        }
    };
    Soundscape wild;
    settle(wild, 30);
    CHECK(wild.HaveCensus());
    CHECK(wild.Axes().mechanical < 0.2f);   // open land reads organic
    CHECK(wild.Axes().positive > 0.2f);
    // A works yard: machines and tubes around the player.
    for (int x = -6; x <= 6; x += 2)
        for (int z = -6; z <= 6; z += 2) { w.Set(8 + x, ground + 1, 8 + z, BLOCK_MACHINE); w.Set(8 + x, ground + 2, 8 + z, BLOCK_TUBE); }
    Soundscape yard;
    settle(yard, 30);
    CHECK(yard.Axes().mechanical > 0.7f);
    CHECK(yard.Scene().machines > 0.9f);
    // Dark ground: flesh blocks drag positive down, and the first sight is an omen.
    for (int x = -8; x <= 8; x++)
        for (int z = 10; z <= 14; z++) w.Set(8 + x, ground, 8 + z - 20, BLOCK_CORRUPTED_FLESH);
    Soundscape dark;
    settle(dark, 40);
    CHECK(dark.Axes().positive < -0.3f);
    SoundId found[8];
    int n = dark.TakeDiscoveries(found, 8);
    bool omen = false;
    for (int i = 0; i < n; i++) omen = omen || found[i] == SND_OMEN;
    CHECK(omen);
    // Materials.
    CHECK(BlockSoundMaterial(BLOCK_STONE) == MAT_STONE);
    CHECK(BlockSoundMaterial(BLOCK_FERN_FROND) == MAT_PLANT);
    CHECK(BlockSoundClass(BLOCK_MACHINE) == SC_MECHANICAL);
    CHECK(BlockSoundClass(BLOCK_GENESIS_SOIL) == SC_GENESIS);
}

static void TestProps() {
    printf("faceted props\n");
    ShapePoly polys[MAX_SHAPE_POLYS];
    ShapeBox box;
    int bad = 0;
    for (int shape = SHAPE_SWELL_MOUND; shape < SHAPE_COUNT; shape++) if (ShapeIsProp(shape))
        for (int f = 0; f < FACE_COUNT; f++)
            for (int variant = 0; variant < 4; variant++) {
                int n = ShapePolys((BlockShape)shape, (uint8_t)f, polys, variant);
                bool ok = n > 0 && n <= MAX_SHAPE_POLYS;
                // Closed and wound outward: the signed volume about the
                // cell's centre is positive (and matches a real solid).
                double vol = 0;
                for (int i = 0; i < n; i++) {
                    const ShapePoly& p = polys[i];
                    for (int k = 0; k < p.count; k++) ok = ok && p.v[k].x <= 8 && p.v[k].y <= 8 && p.v[k].z <= 8;
                    for (int k = 1; k + 1 < p.count; k++) {
                        double a[3] = { p.v[0].x - 4.0, p.v[0].y - 4.0, p.v[0].z - 4.0 };
                        double b[3] = { p.v[k].x - 4.0, p.v[k].y - 4.0, p.v[k].z - 4.0 };
                        double c[3] = { p.v[k + 1].x - 4.0, p.v[k + 1].y - 4.0, p.v[k + 1].z - 4.0 };
                        vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
                    }
                }
                ok = ok && vol > 1.0 && vol <= 512.0;
                if (!ok) { bad++; if (bad < 5) printf("    shape %d facing %d variant %d: %d polys, volume %.1f\n", shape, f, variant, n, vol); }
            }
    CHECK(bad == 0);
    // Anchored flush: a mound on a floor lies on the cell's bottom (hidden
    // by a full block beneath); hung from a ceiling it lies on the top; on
    // a wall it lies on that wall and droops.
    auto anchorFace = [&](BlockShape s, int f) {
        int n = ShapePolys(s, (uint8_t)f, polys, 0), found = -1;
        for (int i = 0; i < n; i++) if (polys[i].boundary >= 0) found = polys[i].boundary;
        return found;
    };
    CHECK(anchorFace(SHAPE_SWELL_MOUND, FACE_POS_Y) == FACE_NEG_Y);
    CHECK(anchorFace(SHAPE_SWELL_MOUND, FACE_NEG_Y) == FACE_POS_Y);
    CHECK(anchorFace(SHAPE_SWELL_KNOB, FACE_POS_X) == FACE_NEG_X);
    CHECK(anchorFace(SHAPE_SHARD, FACE_NEG_Z) == FACE_POS_Z);
    // Collision: one box within the cell; a mound is low enough to step onto.
    CHECK(ShapeBoxes(SHAPE_SWELL_MOUND, FACE_POS_Y, &box) == 1 && box.y0 == 0 && box.y1 <= 4);
    CHECK(ShapeBoxes(SHAPE_SWELL_MOUND, FACE_NEG_Y, &box) == 1 && box.y1 == 8 && box.y0 >= 4);
    CHECK(ShapeBoxes(SHAPE_PIPE, FACE_POS_X, &box) == 1 && box.x0 == 0 && box.x1 == 8 && box.y1 - box.y0 == 6);
    // A rafter chains: its top corner meets the next cell's bottom corner.
    int n = ShapePolys(SHAPE_BEAM, FACE_POS_Z, polys, 0);
    bool low = false, high = false;
    for (int i = 0; i < n; i++) for (int k = 0; k < polys[i].count; k++) {
        if (polys[i].v[k].y == 0 && polys[i].v[k].z == 0) low = true;
        if (polys[i].v[k].y == 8 && polys[i].v[k].z == 8) high = true;
    }
    CHECK(low && high);
    // Every prop block meshes; a see-through prop lands in the blended pass.
    World w;
    for (int id = BLOCK_MOSS_CLUMP; id < BLOCK_COUNT; id++) {
        w = World();
        w.Set(3, 3, 3, (BlockID)id, FACE_POS_Y);
        std::vector<Vertex> verts; std::vector<uint16_t> idx; size_t firstClear = 0;
        BuildChunkMesh(w, { 0, 0, 0 }, *w.FindChunk({ 0, 0, 0 }), verts, idx, &firstClear);
        CHECK(!idx.empty());
        if (g_blocks[id].translucent) CHECK(firstClear == 0); else CHECK(firstClear == idx.size());
    }
}

static void TestPatchwork() {
    printf("flat v2 ground patchwork\n");
    WorldGenParams saved = g_worldGen;
    g_worldGen.type = GEN_FLAT; g_worldGen.version = 2; g_worldGen.seed = 12345;
    int counts[3] = {}, changes = 0;
    BlockID prev = BLOCK_AIR;
    for (int x = 0; x < 256; x++)
        for (int z = 0; z < 256; z++) {
            BlockID b = SurfaceBlockAt(x * 2, z * 2);
            counts[b == BLOCK_MEADOW_GRASS ? 0 : b == BLOCK_COASTAL_SAND ? 1 : 2]++;
            if (z > 0 && b != prev) changes++;
            prev = b;
        }
    const int total = 256 * 256;
    printf("    grass %.0f%%, sand %.0f%%, pebbles %.0f%%\n", 100.0 * counts[0] / total, 100.0 * counts[1] / total, 100.0 * counts[2] / total);
    CHECK(counts[0] > total * 0.4 && counts[1] > total * 0.05 && counts[2] > total * 0.05);
    CHECK(changes > 200 && changes < total / 4);   // patches, not noise and not one field
    CHECK(SurfaceBlockAt(1000, -777) == SurfaceBlockAt(1000, -777)); // a pure function
    int differ = 0;
    for (int i = 0; i < 200; i++) { g_worldGen.seed = 12345; BlockID a = SurfaceBlockAt(i * 7, i * 3); g_worldGen.seed = 999; differ += a != SurfaceBlockAt(i * 7, i * 3); }
    CHECK(differ > 20);                              // each world its own
    // Generated columns wear it on top; flat v1 worlds keep plain dirt.
    World w; ResetWorldState(w);
    g_worldGen.seed = 12345;
    GenerateColumn(w, 0, 0);
    CHECK(w.Get(5, 12, 5) == SurfaceBlockAt(5, 5) && w.Get(5, 11, 5) == BLOCK_DIRT && w.Get(5, 13, 5) == BLOCK_AIR);
    ResetWorldState(w);
    g_worldGen.version = 1;
    GenerateColumn(w, 0, 0);
    CHECK(w.Get(5, 12, 5) == BLOCK_DIRT);
    g_worldGen = saved;
}

int main() {
    TestVtex();
    TestBlockTextures();
    TestSaveRoundTrip();
    TestLegacyLoad();
    TestStreaming();
    TestPatchwork();
    TestPlayer();
    TestMovement();
    TestMesher();
    TestShapes();
    TestProps();
    TestIcons();
    TestScheduledUpdates();
    TestLibrary();
    TestMusicLevel();
    TestGlowLight();
    TestSky();
    TestTheLine();
    TestPulse();
    TestEssence();
    TestMusicHarmony();
    TestSoundPalette();
    TestSoundscape();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
