// texlib.cpp -- see texlib.h.
//
// Carried from Voxistics' blocktex.cpp: DrawMissing, BuildSurface,
// Upscale and the mip chains are its code as it was (BLOCK_TEX_SIZE is
// LAYER_SIZE here). The block-face mapping, the block-named procedural
// placeholders and the hotbar icon strip belonged to Voxistics' block
// game and are left behind: Cacophony takes one layer per texture.

#include "texlib.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace {

const size_t LAYER_BYTES = (size_t)LAYER_SIZE * LAYER_SIZE * 4;

void DrawMissing(uint8_t* px) {
    // Magenta/black checker: unmistakably "no texture here".
    for (int y = 0; y < LAYER_SIZE; y++)
        for (int x = 0; x < LAYER_SIZE; x++) {
            bool m = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t* p = px + ((size_t)y * LAYER_SIZE + x) * 4;
            p[0] = m ? 255 : 0; p[1] = 0; p[2] = m ? 255 : 0; p[3] = 255;
        }
}

// The surface layer for authored art (see TextureLibrary::surface): the
// height map, scaled up by whole pixels like the colours, turned into
// normals by central differences that wrap round the edges (the tile
// repeats, so its slopes do too), plus shine and glow.
void BuildSurface(const VtexTexture& t, uint8_t* sf) {
    const int N = LAYER_SIZE, k = N / t.size, S = t.size;
    auto at = [&](const std::vector<float>& m, int x, int y) {
        x = ((x % N) + N) % N; y = ((y % N) + N) % N;
        return m[(size_t)(y / k) * t.size + (x / k)];
    };
    // Heights come in 36 steps, so a gentle slope drawn at 32 or 64 pixels
    // would light up as contour-line terraces. Soften those heights a
    // little (two wrapping [1 2 1] passes each way) before taking slopes;
    // chunky 8/16-pixel art keeps its crisp pixel bevels.
    std::vector<float> smooth;
    const std::vector<float>* height = &t.height;
    if (!t.height.empty() && S >= 32) {
        smooth = t.height;
        std::vector<float> tmp(smooth.size());
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < S; y++)
                for (int x = 0; x < S; x++)
                    tmp[(size_t)y * S + x] = 0.25f * smooth[(size_t)y * S + (x + S - 1) % S] + 0.5f * smooth[(size_t)y * S + x] + 0.25f * smooth[(size_t)y * S + (x + 1) % S];
            for (int y = 0; y < S; y++)
                for (int x = 0; x < S; x++)
                    smooth[(size_t)y * S + x] = 0.25f * tmp[(size_t)((y + S - 1) % S) * S + x] + 0.5f * tmp[(size_t)y * S + x] + 0.25f * tmp[(size_t)((y + 1) % S) * S + x];
        }
        height = &smooth;
    }
    const float slope = SURFACE_DEPTH * N * 0.5f; // height units per texel -> tangent-space slope, over a 2-texel difference
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            uint8_t* o = sf + ((size_t)y * N + x) * 4;
            if (!t.height.empty()) {
                float dx = (at(*height, x + 1, y) - at(*height, x - 1, y)) * slope;
                float dy = (at(*height, x, y + 1) - at(*height, x, y - 1)) * slope;
                float len = sqrtf(dx * dx + dy * dy + 1.0f);
                o[0] = (uint8_t)std::lround((-dx / len * 0.5f + 0.5f) * 255.0f);
                o[1] = (uint8_t)std::lround((-dy / len * 0.5f + 0.5f) * 255.0f);
            }
            if (!t.shine.empty()) o[2] = (uint8_t)std::lround(at(t.shine, x, y) * 255.0f);
            if (!t.glow.empty()) o[3] = (uint8_t)std::lround(at(t.glow, x, y) * 255.0f);
        }
}

void Upscale(const VtexTexture& t, uint8_t* px) {
    int k = LAYER_SIZE / t.size;
    for (int y = 0; y < LAYER_SIZE; y++)
        for (int x = 0; x < LAYER_SIZE; x++) {
            uint32_t c = t.rgb[(size_t)(y / k) * t.size + (x / k)];
            uint8_t* p = px + ((size_t)y * LAYER_SIZE + x) * 4;
            p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)(c >> 16); p[3] = (uint8_t)(255 - (c >> 24));
        }
}

} // namespace

uint16_t TextureLibrary::Layer(const std::string& name) const {
    auto it = index.find(name);
    return it == index.end() ? 0 : it->second;
}

void BuildTextureLibrary(const VtexSet& authored, TextureLibrary& out) {
    out = TextureLibrary();
    // Among authored duplicates the last one loaded wins (files load in name order).
    std::map<std::string, const VtexTexture*> art;
    for (const VtexTexture& t : authored.textures) {
        if (art.count(t.name)) out.warnings.push_back(t.source + ": texture '" + t.name + "' defined again; this one replaces the earlier one");
        art[t.name] = &t;
    }
    std::vector<std::vector<uint8_t>> layers, surfaces; // colour and surface, one of each per layer
    auto flatSurface = [] {
        std::vector<uint8_t> sf(LAYER_BYTES);
        for (size_t i = 0; i < LAYER_BYTES; i += 4) { sf[i] = 128; sf[i + 1] = 128; sf[i + 2] = 0; sf[i + 3] = 0; } // flat, matte, dark
        return sf;
    };
    // Layer 0: the "missing" checker, what an unknown name gets.
    layers.emplace_back(LAYER_BYTES);
    DrawMissing(layers.back().data());
    surfaces.push_back(flatSurface());
    out.layerNames.push_back("missing");
    // Then every texture in name order, so the set is deterministic.
    for (const auto& kv : art) {
        const VtexTexture& t = *kv.second;
        if (t.size <= 0 || LAYER_SIZE % t.size != 0) { out.warnings.push_back(t.source + ": texture '" + t.name + "' size doesn't divide the layer size"); continue; }
        std::vector<uint8_t> px(LAYER_BYTES), sf = flatSurface();
        Upscale(t, px.data());
        BuildSurface(t, sf.data());
        out.index[t.name] = (uint16_t)layers.size();
        layers.push_back(std::move(px));
        surfaces.push_back(std::move(sf));
        out.layerNames.push_back(t.name);
    }

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
    int mips = 1; for (int s = LAYER_SIZE; s > 1; s >>= 1) mips++;
    out.mipCount = mips;
    out.mips.resize(mips);
    out.mips[0].reserve(LAYER_BYTES * layers.size());
    for (auto& l : layers) out.mips[0].insert(out.mips[0].end(), l.begin(), l.end());
    for (int m = 1; m < mips; m++) {
        int src = LAYER_SIZE >> (m - 1), dst = src >> 1;
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

    // Surface mips: normals are averaged as vectors and renormalised (a
    // bumpy surface far away flattens out, the right way round); shine and
    // glow average plainly.
    out.surface.resize(mips);
    out.surface[0].reserve(LAYER_BYTES * surfaces.size());
    for (auto& l : surfaces) out.surface[0].insert(out.surface[0].end(), l.begin(), l.end());
    for (int m = 1; m < mips; m++) {
        int src = LAYER_SIZE >> (m - 1), dst = src >> 1;
        out.surface[m].resize((size_t)dst * dst * 4 * surfaces.size());
        for (size_t L = 0; L < surfaces.size(); L++) {
            const uint8_t* s = out.surface[m - 1].data() + L * (size_t)src * src * 4;
            uint8_t* d = out.surface[m].data() + L * (size_t)dst * dst * 4;
            for (int y = 0; y < dst; y++)
                for (int x = 0; x < dst; x++) {
                    float nx = 0, ny = 0, nz = 0, sh = 0, gl = 0;
                    for (int k = 0; k < 4; k++) {
                        const uint8_t* q = s + ((size_t)(2 * y + (k >> 1)) * src + 2 * x + (k & 1)) * 4;
                        float ax = q[0] / 127.5f - 1.0f, ay = q[1] / 127.5f - 1.0f;
                        nx += ax; ny += ay; nz += sqrtf(std::max(0.0f, 1.0f - ax * ax - ay * ay));
                        sh += q[2]; gl += q[3];
                    }
                    float len = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (len < 1e-6f) { nx = 0; ny = 0; len = 1; }
                    uint8_t* o = d + ((size_t)y * dst + x) * 4;
                    o[0] = (uint8_t)std::lround((nx / len * 0.5f + 0.5f) * 255.0f);
                    o[1] = (uint8_t)std::lround((ny / len * 0.5f + 0.5f) * 255.0f);
                    o[2] = (uint8_t)std::lround(sh / 4); o[3] = (uint8_t)std::lround(gl / 4);
                }
        }
    }

}
