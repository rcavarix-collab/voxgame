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
    std::vector<uint32_t> rgb;   // size*size, 0x00RRGGBB, row 0 = top
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
