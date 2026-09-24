// blocktex.h
//
// Builds every block face texture the renderer needs (DESIGN.md 4.3):
// authored .vtex art where it exists, procedural placeholders otherwise,
// resolved per (block, facing, face) into layers of one texture array
// with a full mip chain. Pure C++ -- render.cpp uploads the result,
// tests/ can render it to an image.

#pragma once

#include "blocks.h"
#include "vtex.h"
#include <cstdint>
#include <string>
#include <vector>

// Every layer is this size; authored art (8/16/32) is scaled up by whole
// pixels (nearest), so it stays exactly as drawn.
static const int BLOCK_TEX_SIZE = 64;
// How deep a texture's full height range (0 to 1) reads, in blocks: the
// normal map's strength.
static const float SURFACE_DEPTH = 0.05f;

struct BlockTextureSet {
    int layerCount = 0;
    int mipCount = 0;
    // mips[m] holds all layers at mip m, layer after layer, BGRA8,
    // (BLOCK_TEX_SIZE >> m)^2 pixels per layer.
    std::vector<std::vector<uint8_t>> mips;
    // The matching surface layers (DESIGN.md 4.13), same layout, RGBA8
    // linear: R, G = the tangent-space normal's x (along u) and y (along v,
    // down the texture) as n * 0.5 + 0.5 (z is rebuilt in the shader);
    // B = shine, A = glow. Flat and matte where a texture has no maps.
    std::vector<std::vector<uint8_t>> surface;
    std::vector<std::string> layerNames;
    // Texture-array layer for face `face` of block `id` placed with
    // facing `facing` (both BlockFace). The mesher's only texture lookup.
    uint16_t faceLayer[BLOCK_COUNT][FACE_COUNT][FACE_COUNT] = {};
    // Hotbar icons: one BLOCK_TEX_SIZE cell per block ID in a horizontal
    // strip (BGRA8), showing the face a placed block turns toward you.
    std::vector<uint8_t> icons;
    int iconsW = 0, iconsH = 0;
    // Problems worth showing the person authoring textures.
    std::vector<std::string> warnings;
};

// `authored` is every parsed .vtex file (its own parse errors are
// reported by the caller). Deterministic: same input, same layers.
void BuildBlockTextures(const VtexSet& authored, BlockTextureSet& out);
