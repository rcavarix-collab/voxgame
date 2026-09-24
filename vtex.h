// vtex.h
//
// Parser for authored block textures: the plain-text .vtex format
// specified in assets/textures/TEXTURE_BRIEF.md (a palette plus a grid of
// palette keys per texture, and `block` entries mapping textures onto
// faces). Pure C++, no OS or graphics dependency, so it's tested
// natively (tests/). textures.cpp turns the result into texture layers.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct VtexTexture {
    std::string name;
    int size = 0;                // pixels per side (8, 16, 32 or 64)
    std::vector<uint32_t> rgb;   // size*size, 0xTTRRGGBB, row 0 = top; TT = 255 - alpha (0 = opaque, so opaque art reads as plain RGB)
    // Optional surface maps (each size*size, 0..1, empty when not given):
    // height drives the normal map, shine a sun glint, glow what lights up
    // on its own (DESIGN.md 4.13).
    std::vector<float> height, shine, glow;
    std::string source;          // "file:line" where it was defined
};

struct VtexBlockFaces {
    std::string block;           // registry name (blocks.h)
    std::string all, top, bottom, side, front; // texture names, "" = not set
    std::string source;
};

struct VtexSet {
    std::vector<VtexTexture> textures;
    std::vector<VtexBlockFaces> blocks;
    std::vector<std::string> errors; // "file:line: message"; a bad entry is skipped, the rest still load
};

// Parses one file's text, appending to `out`. `fileName` only labels errors.
void ParseVtex(const std::string& text, const std::string& fileName, VtexSet& out);
