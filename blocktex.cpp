// blocktex.cpp -- see blocktex.h.

#include "blocktex.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {

// ---- Procedural placeholder tiles ---------------------------------------
// Plotted with exact integer-pixel primitives, each clipped to its own
// square (no bleed between textures, DESIGN.md 4.3).
struct Rgb { uint8_t r, g, b; };

struct TileCanvas {
    uint8_t* px; int stride; int ox, oy, size; // BGRA8
    uint8_t alpha = 255; // what Put/Fill write: below 255 only for see-through blocks
    void Put(int x, int y, Rgb c) {
        if ((unsigned)x >= (unsigned)size || (unsigned)y >= (unsigned)size) return;
        uint8_t* p = px + (size_t)(oy + y) * stride + (size_t)(ox + x) * 4;
        p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = alpha;
    }
    // 50/50 mix with what's already there (the wood grain's soft half row).
    void Blend(int x, int y, Rgb c) {
        if ((unsigned)x >= (unsigned)size || (unsigned)y >= (unsigned)size) return;
        uint8_t* p = px + (size_t)(oy + y) * stride + (size_t)(ox + x) * 4;
        p[0] = (uint8_t)((p[0] + c.b) / 2); p[1] = (uint8_t)((p[1] + c.g) / 2); p[2] = (uint8_t)((p[2] + c.r) / 2);
    }
    void Fill(int x, int y, int w, int h, Rgb c) {
        for (int yy = y; yy < y + h; yy++)
            for (int xx = x; xx < x + w; xx++) Put(xx, yy, c);
    }
    void Border(int t, Rgb c) {
        Fill(0, 0, size, t, c);
        Fill(0, size - t, size, t, c);
        Fill(0, t, t, size - 2 * t, c);
        Fill(size - t, t, t, size - 2 * t, c);
    }
    // Filled circle inscribed in the d x d cell at (x, y).
    void Disc(int x, int y, int d, Rgb c) {
        float r = d * 0.5f;
        for (int yy = 0; yy < d; yy++)
            for (int xx = 0; xx < d; xx++) {
                float dx = xx + 0.5f - r, dy = yy + 0.5f - r;
                if (dx * dx + dy * dy <= r * r) Put(x + xx, y + yy, c);
            }
    }
};

static void DrawFoundationTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 120, 120, 120 });
    Rgb line = { 70, 70, 70 };
    t.Border(2, line);
    int step = s / 4;
    for (int i = step; i < s; i += step) {
        t.Fill(i - 1, 0, 2, s, line);
        t.Fill(0, i - 1, s, 2, line);
    }
}

// ---- Natural placeholders: 16x16 pixel art, seamless ------------------
// Drawn on a 16x16 grid of logical pixels (each block-size/16 real ones,
// the same chunky scale the art brief asks for) from our own integer
// hash -- never rand(): MSVC's rand() is a weak LCG whose consecutive
// values correlate, which lined random specks up into diagonal streaks
// across the landscape. Every pattern wraps at 16, so tiles meet with no
// seam on any side.
static uint32_t Hash3(uint32_t x, uint32_t y, uint32_t salt) {
    uint32_t h = x * 0x9E3779B1u + y * 0x85EBCA77u + salt * 0xC2B2AE3Du; // murmur3's finaliser: neighbours decorrelate fully
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
static float Hash01(int x, int y, uint32_t salt) { return (Hash3((uint32_t)x, (uint32_t)y, salt) & 0xFFFFFF) / 16777216.0f; }
// Smooth value noise that repeats every `period` logical pixels.
static float TileNoise(float x, float y, int period, uint32_t salt) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto v = [&](int ix, int iy) { return Hash01(((ix % period) + period) % period, ((iy % period) + period) % period, salt); };
    float a = v(x0, y0) + (v(x0 + 1, y0) - v(x0, y0)) * fx;
    float b = v(x0, y0 + 1) + (v(x0 + 1, y0 + 1) - v(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}
static Rgb Shade(Rgb c, float k) {
    auto f = [&](uint8_t v) { float r = v * k; return (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r)); };
    return { f(c.r), f(c.g), f(c.b) };
}
static void Pixel16(TileCanvas& t, int x, int y, Rgb c) {
    int k = t.size / 16;
    t.Fill(((x % 16) + 16) % 16 * k, ((y % 16) + 16) % 16 * k, k, k, c);
}

// Stone: cool grey, soft mottling in two scales, a few darker flecks and
// the odd lighter grain -- even overall, so it doesn't grid the hillside.
static void DrawStoneTile(TileCanvas& t) {
    const Rgb base = { 128, 129, 134 };
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float n = 0.65f * TileNoise(x / 4.0f, y / 4.0f, 4, 11) + 0.35f * TileNoise(x / 2.0f, y / 2.0f, 8, 12);
            float k = 0.86f + 0.26f * n;
            float r = Hash01(x, y, 13);
            if (r < 0.07f) k *= 0.80f; else if (r > 0.95f) k *= 1.10f;
            Pixel16(t, x, y, Shade(base, k));
        }
}

// Dirt: warm browns only (no green), soft clumps, darker pebbles and a
// few pale grit specks -- no direction to it, so nothing lines up.
static void DrawDirtTile(TileCanvas& t) {
    const Rgb base = { 118, 82, 54 };
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float n = 0.6f * TileNoise(x / 4.0f, y / 4.0f, 4, 21) + 0.4f * TileNoise(x / 2.0f, y / 2.0f, 8, 22);
            float k = 0.82f + 0.30f * n;
            Rgb c = Shade(base, k);
            float r = Hash01(x, y, 23);
            if (r < 0.08f) c = Shade({ 84, 58, 40 }, 0.9f + 0.2f * n);          // pebble
            else if (r > 0.95f) c = Shade({ 150, 118, 86 }, 0.95f + 0.1f * n);  // grit
            Pixel16(t, x, y, c);
        }
}

// Wood: four horizontal planks, each its own tone, with streaks of grain
// running along them and wrapping, a soft line between planks, and plank
// ends staggered (one faint joint per plank, at a different place in
// each) so nothing lines up with the block's own edges.
static void DrawWoodTile(TileCanvas& t) {
    const Rgb base = { 162, 120, 76 };
    for (int plank = 0; plank < 4; plank++) {
        float tone = 0.86f + 0.22f * Hash01(plank, 0, 31);
        int joint = (plank * 7 + 3) % 16;
        for (int row = 0; row < 4; row++) {
            int y = plank * 4 + row;
            for (int x = 0; x < 16; x++) {
                float k = tone * (0.95f + 0.08f * TileNoise(x / 4.0f, (float)y, 4, 32)); // gentle along-the-board variation
                if (row == 3) k *= 0.80f;                                             // the line between planks
                if (x == joint && row != 3) k *= 0.80f;                               // plank end
                Pixel16(t, x, y, Shade(base, k));
            }
        }
        // Two grain streaks per plank, 4-9 pixels long, wrapping.
        for (int g = 0; g < 2; g++) {
            int row = 1 + (int)(Hash01(plank, g, 34) * 2.0f);
            int x0 = (int)(Hash01(plank, g, 35) * 16.0f), len = 4 + (int)(Hash01(plank, g, 36) * 6.0f);
            for (int i = 0; i < len; i++) {
                int x = (x0 + i) % 16;
                if (x == joint) continue;
                Pixel16(t, x, plank * 4 + row, Shade(base, tone * 0.84f));
            }
        }
    }
}

static void DrawChestTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 140, 90, 40 });
    t.Border(3, { 60, 35, 15 });
    t.Fill(0, s / 2 - s / 10, s, s / 5, { 90, 60, 30 });
}

static void DrawChestFrontTile(TileCanvas& t) {
    DrawChestTile(t);
    int s = t.size;
    int latch = s / 6;
    t.Fill(s / 2 - latch / 2, s / 2 - latch / 2, latch, latch, { 200, 170, 60 });
}

static void DrawMachineTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 65, 68, 78 });
    t.Border(3, { 25, 27, 32 });
    Rgb bolt = { 200, 200, 60 };
    int b = s / 10;
    int margin = s / 8;
    t.Disc(margin, margin, b, bolt);
    t.Disc(s - margin - b, margin, b, bolt);
    t.Disc(margin, s - margin - b, b, bolt);
    t.Disc(s - margin - b, s - margin - b, b, bolt);
    // Sides and top: a vent grille.
    int pm = s / 4;
    for (int y = pm; y + 2 <= s - pm; y += s / 8)
        t.Fill(pm, y, s - 2 * pm, 2, { 30, 32, 38 });
}

static void DrawMachineFrontTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 65, 68, 78 });
    t.Border(3, { 25, 27, 32 });
    Rgb bolt = { 200, 200, 60 };
    int b = s / 10;
    int margin = s / 8;
    t.Disc(margin, margin, b, bolt);
    t.Disc(s - margin - b, margin, b, bolt);
    t.Disc(margin, s - margin - b, b, bolt);
    t.Disc(s - margin - b, s - margin - b, b, bolt);
    // Front: the control panel.
    int pm = s / 4;
    t.Fill(pm, pm, s - 2 * pm, s - 2 * pm, { 90, 140, 150 });
}


// Brushed metal with a lit band and a shadow band across the middle rows
// (the slice a tube's narrow faces show) and darker coupling rings.
void DrawTubeTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 140, 145, 155 });
    t.Fill(0, s * 7 / 16, s, s / 16, { 185, 190, 200 });
    t.Fill(0, s * 9 / 16, s, s / 16, { 95, 100, 110 });
    for (int x = 0; x < s; x += s / 2) t.Fill(x, 0, s / 16, s, { 105, 110, 120 });
}

// A speaker-like face: dark casing, a round grille in the middle.
void DrawMusicBlockTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 46, 40, 52 });
    t.Border(3, { 24, 20, 28 });
    t.Disc(s / 6, s / 6, s * 2 / 3, { 150, 110, 70 });
    t.Disc(s / 4, s / 4, s / 2, { 70, 55, 45 });
    for (int y = s / 4 + 2; y < s * 3 / 4; y += 4)
        for (int x = s / 4 + 2; x < s * 3 / 4; x += 4) t.Fill(x, y, 2, 2, { 40, 32, 30 });
    t.Disc(s / 2 - s / 12, s / 2 - s / 12, s / 6, { 190, 150, 90 });
}

// Pale, faintly veined stone, like something that remembers light.
void DrawTimestreamBlockTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 150, 165, 180 });
    for (int i = 0; i < s; i++) {
        t.Put(i, (i * 3 / 4 + s / 5) % s, { 185, 205, 225 });
        t.Put((i * 5 / 7 + s / 3) % s, i, { 120, 135, 155 });
        t.Put(i, (s - 1 - i / 2 + s / 2) % s, { 175, 195, 215 });
    }
    t.Border(1, { 110, 125, 145 });
}

// A dark frame around a violet core: the placeholder essence attractor.
void DrawAttractorTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 52, 48, 60 });
    t.Border(4, { 30, 27, 36 });
    t.Disc(s / 4, s / 4, s / 2, { 120, 90, 200 });
    t.Disc(s * 3 / 8, s * 3 / 8, s / 4, { 200, 175, 255 });
    for (int i = 6; i < s - 6; i += 8) { t.Fill(i, 6, 2, 2, { 90, 80, 110 }); t.Fill(i, s - 8, 2, 2, { 90, 80, 110 }); }
}

// Clear glass: a faint blue-green body, a firmer frame, and two soft
// diagonal glints (even in both directions, so they don't imply a light).
void DrawGlassTile(TileCanvas& t) {
    int s = t.size;
    t.alpha = 40;  t.Fill(0, 0, s, s, { 200, 228, 235 });
    t.alpha = 95;
    for (int i = 0; i < s / 3; i++) { t.Fill(s / 5 + i, s / 2 - i, 2, 1, { 245, 252, 255 }); t.Fill(s / 2 + i, s - s / 5 - i, 2, 1, { 245, 252, 255 }); }
    t.alpha = 215; t.Border(2, { 160, 190, 200 });
    t.alpha = 255;
}

// Tinted crystal: violet, cloudier than glass, with pale facet lines.
void DrawCrystalTile(TileCanvas& t) {
    int s = t.size;
    t.alpha = 140; t.Fill(0, 0, s, s, { 140, 80, 210 });
    t.alpha = 175;
    for (int i = 0; i < s; i++) {
        t.Put(i, i / 2 + s / 4, { 205, 165, 250 });
        t.Put(i, s - 1 - i / 3, { 185, 140, 240 });
        t.Put(s / 3 + i / 3, i, { 200, 160, 250 });
    }
    t.alpha = 225; t.Border(1, { 85, 45, 140 });
    t.alpha = 255;
}

using DrawFn = void (*)(TileCanvas&);
struct Procedural { const char* name; DrawFn draw; };
// Drawn in this fixed order after srand(1234), so the random speckle is
// identical every run regardless of which textures end up used.
const Procedural kProcedural[] = {
    { "foundation", DrawFoundationTile },
    { "stone", DrawStoneTile },
    { "dirt", DrawDirtTile },
    { "wood", DrawWoodTile },
    { "chest", DrawChestTile },
    { "chest_front", DrawChestFrontTile },
    { "machine", DrawMachineTile },
    { "machine_front", DrawMachineFrontTile },
    { "tube", DrawTubeTile },
    { "music_block", DrawMusicBlockTile },
    { "timestream_block", DrawTimestreamBlockTile },
    { "essence_attractor", DrawAttractorTile },
    { "glass", DrawGlassTile },
    { "crystal", DrawCrystalTile },
};

const size_t LAYER_BYTES = (size_t)BLOCK_TEX_SIZE * BLOCK_TEX_SIZE * 4;

void DrawMissing(uint8_t* px) {
    // Magenta/black checker: unmistakably "no texture here".
    for (int y = 0; y < BLOCK_TEX_SIZE; y++)
        for (int x = 0; x < BLOCK_TEX_SIZE; x++) {
            bool m = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t* p = px + ((size_t)y * BLOCK_TEX_SIZE + x) * 4;
            p[0] = m ? 255 : 0; p[1] = 0; p[2] = m ? 255 : 0; p[3] = 255;
        }
}

void Upscale(const VtexTexture& t, uint8_t* px) {
    int k = BLOCK_TEX_SIZE / t.size;
    for (int y = 0; y < BLOCK_TEX_SIZE; y++)
        for (int x = 0; x < BLOCK_TEX_SIZE; x++) {
            uint32_t c = t.rgb[(size_t)(y / k) * t.size + (x / k)];
            uint8_t* p = px + ((size_t)y * BLOCK_TEX_SIZE + x) * 4;
            p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)(c >> 16); p[3] = (uint8_t)(255 - (c >> 24));
        }
}

} // namespace

void BuildBlockTextures(const VtexSet& authored, BlockTextureSet& out) {
    out = BlockTextureSet();

    // Sources by name: authored art beats a procedural placeholder of the
    // same name; among authored duplicates the last one loaded wins.
    std::map<std::string, std::vector<uint8_t>> procedural;
    srand(1234);
    for (const Procedural& p : kProcedural) {
        std::vector<uint8_t>& px = procedural[p.name];
        px.assign(LAYER_BYTES, 0);
        TileCanvas t = { px.data(), BLOCK_TEX_SIZE * 4, 0, 0, BLOCK_TEX_SIZE };
        p.draw(t);
    }
    std::map<std::string, const VtexTexture*> art;
    for (const VtexTexture& t : authored.textures) {
        if (art.count(t.name)) out.warnings.push_back(t.source + ": texture '" + t.name + "' defined again; this one replaces the earlier one");
        art[t.name] = &t;
    }

    // Face mappings: an authored `block` entry replaces the registry's
    // mapping for that block outright, so a block drawn in the new style
    // never mixes in a placeholder face.
    const VtexBlockFaces* override_[BLOCK_COUNT] = {};
    for (const VtexBlockFaces& b : authored.blocks) {
        int id = -1;
        for (int i = 1; i < BLOCK_COUNT; i++) if (b.block == g_blocks[i].name) id = i;
        if (id < 0) { out.warnings.push_back(b.source + ": no block named '" + b.block + "' (ignored)"); continue; }
        override_[id] = &b;
    }
    auto faceName = [&](int id, int facing, int face) -> std::string {
        const VtexBlockFaces* o = override_[id];
        if (!o) return BlockFaceTextureName((BlockID)id, (BlockFace)facing, (BlockFace)face);
        std::string n;
        if (face == FACE_POS_Y) n = o->top;
        else if (face == FACE_NEG_Y) n = o->bottom;
        else {
            if (g_blocks[id].orientable && face == facing) n = o->front;
            if (n.empty()) n = o->side;
        }
        if (n.empty()) n = o->all;
        if (n.empty()) n = g_blocks[id].name;
        return n;
    };

    // Resolve every (block, facing, face) to a layer, assigning layers in
    // first-use order so the set is deterministic.
    std::map<std::string, uint16_t> layerOf;
    std::vector<std::vector<uint8_t>> layers;
    auto layerFor = [&](const std::string& wanted, int id) -> uint16_t {
        auto it = layerOf.find(wanted);
        if (it != layerOf.end()) return it->second;
        std::vector<uint8_t> px(LAYER_BYTES);
        auto a = art.find(wanted);
        auto p = procedural.find(wanted);
        if (a != art.end()) Upscale(*a->second, px.data());
        else if (p != procedural.end()) px = p->second;
        else {
            auto own = procedural.find(g_blocks[id].name);
            if (own != procedural.end()) px = own->second; else DrawMissing(px.data());
            if (override_[id]) out.warnings.push_back(override_[id]->source + ": block '" + g_blocks[id].name + "' uses texture '" + wanted + "', which doesn't exist");
        }
        uint16_t layer = (uint16_t)layers.size();
        layers.push_back(std::move(px));
        out.layerNames.push_back(wanted);
        layerOf[wanted] = layer;
        return layer;
    };
    for (int id = 1; id < BLOCK_COUNT; id++)
        for (int facing = 0; facing < FACE_COUNT; facing++)
            for (int face = 0; face < FACE_COUNT; face++)
                out.faceLayer[id][facing][face] = layerFor(faceName(id, facing, face), id);
    for (const auto& kv : art)
        if (!layerOf.count(kv.first)) out.warnings.push_back(kv.second->source + ": texture '" + kv.first + "' isn't used by any block");

    // Mip chain, 2x2 box filter down to 1x1. Mips are what keep distant
    // blocks from shimmering under point sampling. The texels are sRGB
    // (the GPU reads them as _SRGB), so colour is averaged in linear light
    // -- a gamma-space average darkens every contrasty texture with
    // distance. Alpha is already linear.
    float toLinear[256];
    for (int i = 0; i < 256; i++) {
        float c = i / 255.0f;
        toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    auto toSrgb = [&](float lin) {
        // Nearest byte: the first code whose linear value passes the
        // midpoint to the next one (256 entries, binary search).
        int lo = 0, hi = 255;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (lin > 0.5f * (toLinear[mid] + toLinear[mid + 1])) lo = mid + 1; else hi = mid;
        }
        return (uint8_t)lo;
    };
    out.layerCount = (int)layers.size();
    int mips = 1; for (int s = BLOCK_TEX_SIZE; s > 1; s >>= 1) mips++;
    out.mipCount = mips;
    out.mips.resize(mips);
    out.mips[0].reserve(LAYER_BYTES * layers.size());
    for (auto& l : layers) out.mips[0].insert(out.mips[0].end(), l.begin(), l.end());
    for (int m = 1; m < mips; m++) {
        int src = BLOCK_TEX_SIZE >> (m - 1), dst = src >> 1;
        out.mips[m].resize((size_t)dst * dst * 4 * layers.size());
        for (size_t L = 0; L < layers.size(); L++) {
            const uint8_t* s = out.mips[m - 1].data() + L * (size_t)src * src * 4;
            uint8_t* d = out.mips[m].data() + L * (size_t)dst * dst * 4;
            for (int y = 0; y < dst; y++)
                for (int x = 0; x < dst; x++)
                    for (int c = 0; c < 4; c++) {
                        const uint8_t a = s[((2 * y) * src + 2 * x) * 4 + c], b = s[((2 * y) * src + 2 * x + 1) * 4 + c],
                                      e = s[((2 * y + 1) * src + 2 * x) * 4 + c], f = s[((2 * y + 1) * src + 2 * x + 1) * 4 + c];
                        d[(y * dst + x) * 4 + c] = c == 3
                            ? (uint8_t)((a + b + e + f + 2) / 4)
                            : toSrgb(0.25f * (toLinear[a] + toLinear[b] + toLinear[e] + toLinear[f]));
                    }
        }
    }

    // Icon strip: the face a freshly placed block shows the player (its
    // front, for orientable blocks) -- placed facing +Z, seen from +Z.
    out.iconsW = BLOCK_TEX_SIZE * BLOCK_COUNT;
    out.iconsH = BLOCK_TEX_SIZE;
    out.icons.assign((size_t)out.iconsW * out.iconsH * 4, 0);
    for (int id = 1; id < BLOCK_COUNT; id++) {
        const uint8_t* l = layers[out.faceLayer[id][FACE_POS_Z][FACE_POS_Z]].data();
        for (int y = 0; y < BLOCK_TEX_SIZE; y++)
            memcpy(out.icons.data() + ((size_t)y * out.iconsW + (size_t)id * BLOCK_TEX_SIZE) * 4,
                   l + (size_t)y * BLOCK_TEX_SIZE * 4, (size_t)BLOCK_TEX_SIZE * 4);
    }
}
