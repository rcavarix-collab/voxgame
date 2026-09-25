// texlib.h
//
// The texture library (Voxistics DESIGN.md 4.3 and 4.13, carried): every
// authored .vtex texture in assets/textures (spec: TEXTURE_BRIEF.md) as a
// layer of one texture array with a full mip chain, plus its surface
// layer -- a normal map from the height map, shine and glow. Cacophony
// looks textures up by name (prims.h's surfaces name the ones it uses).
// Pure C++ -- render.cpp uploads the result; tests/ check it natively.
// Cost: built once at start-up, a few MB of video memory.

#pragma once

#include "vtex.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Every layer is this size; authored art (8/16/32) is scaled up by whole
// pixels (nearest), so it stays exactly as drawn.
static const int LAYER_SIZE = 64;
// How deep a texture's full height range (0 to 1) reads, in texture
// tiles: the normal map's strength.
static const float SURFACE_DEPTH = 0.05f;

struct TextureLibrary {
    int layerCount = 0;
    int mipCount = 0;
    // mips[m] holds all layers at mip m, layer after layer, BGRA8,
    // (LAYER_SIZE >> m)^2 pixels per layer.
    std::vector<std::vector<uint8_t>> mips;
    // The matching surface layers, same layout, RGBA8 linear: R, G = the
    // tangent-space normal's x (along u) and y (along v, down the texture)
    // as n * 0.5 + 0.5 (z is rebuilt in the shader); B = shine, A = glow.
    // Flat and matte where a texture has no maps.
    std::vector<std::vector<uint8_t>> surface;
    std::vector<std::string> layerNames;     // layer 0 is "missing" (a checker)
    std::map<std::string, uint16_t> index;   // texture name -> layer
    std::vector<std::string> warnings;       // problems worth showing whoever authors textures
    uint16_t Layer(const std::string& name) const; // 0 (missing) for an unknown name
};

// `authored` is every parsed .vtex file (its own parse errors are
// reported by the caller). Deterministic: same input, same layers.
void BuildTextureLibrary(const VtexSet& authored, TextureLibrary& out);
