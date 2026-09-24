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
    void Put(int x, int y, Rgb c) {
        if ((unsigned)x >= (unsigned)size || (unsigned)y >= (unsigned)size) return;
        uint8_t* p = px + (size_t)(oy + y) * stride + (size_t)(ox + x) * 4;
        p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = 255;
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

static void DrawStoneTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 140, 140, 145 });
    for (int i = 0; i < s * 3; i++) {
        int px = rand() % s;
        int py = rand() % s;
        int shade = 100 + rand() % 80;
        t.Fill(px, py, 2, 2, { (uint8_t)shade, (uint8_t)shade, (uint8_t)(shade + 5) });
    }
}

static void DrawDirtTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 101, 67, 33 });
    for (int i = 0; i < s * 3; i++) {
        int px = rand() % s;
        int py = rand() % s;
        int r = 80 + rand() % 50, gr = 50 + rand() % 40, b = 20 + rand() % 25;
        t.Fill(px, py, 3, 3, { (uint8_t)r, (uint8_t)gr, (uint8_t)b });
    }
}

static void DrawWoodTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 165, 120, 75 });
    Rgb grain = { 120, 85, 50 };
    for (int i = 3; i < s; i += 6) {
        int drift = rand() % 3 - 1; // -1, 0 or +1 px across the whole tile
        for (int x = 0; x < s; x++) {
            // Round-to-nearest of i + drift * x / s, done in integers.
            int y = i + (drift * (2 * x + 1) + (drift < 0 ? -s : s)) / (2 * s);
            t.Put(x, y, grain);
            t.Blend(x, y + 1, grain);
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
            p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)(c >> 16); p[3] = 255;
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
