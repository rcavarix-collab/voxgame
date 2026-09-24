// glowlight.h
//
// Light cast by glowing blocks (DESIGN.md 4.12), faked cheaply: a small
// light grid around the player, one texel per block, that the world
// shader samples once per pixel. Each glowing block lights the cells
// around it with a smooth falloff, but only those it can "see" -- a line
// traced from the block to each cell stops at the first opaque cube --
// so walls and pillars cast real shadows from it. Hardware trilinear
// filtering softens the one-block steps into soft shadow edges.
//
// Three channels, because the glow kinds are driven differently each
// frame: R = music blocks (scaled by the music playing now), G =
// timestream blocks (scaled by how near The Line passes), B = embers
// (magma: steady). Rebuilt only
// when the grid moves (the player crossed a chunk) or a block changes
// near a light -- never per frame. No D3D here; tested natively.

#pragma once

#include "world.h"
#include <cstdint>
#include <vector>

static const int GLOW_GRID = 64;       // cells per side (a 64-block cube)
static const int GLOW_RADIUS = 8;      // how far a glowing block's light reaches, blocks

struct GlowEmitter { int x, y, z; uint8_t channel; }; // world cell; 0 = music (R), 1 = timestream (G), 2 = ember (B)

struct GlowGrid {
    int ox = 0, oy = 0, oz = 0;        // world position of the grid's minimum corner (chunk-aligned)
    bool valid = false;
    std::vector<GlowEmitter> emitters; // every glowing block inside the grid
    std::vector<uint8_t> texels;       // GLOW_GRID^3 x 4 (R, G, B, unused A), x fastest then y then z; empty when there are no emitters
};

// Where the grid should sit for an eye at (x, y, z): chunk-aligned so it
// only moves when the player crosses a chunk, with the player at least a
// chunk from every face.
void GlowGridOrigin(float x, float y, float z, int& ox, int& oy, int& oz);

// Fills `g` for the grid at origin (ox, oy, oz) from the resident world.
void BuildGlowGrid(World& w, int ox, int oy, int oz, GlowGrid& g);

// Whether a change inside chunk `cc` (now holding `c`) could alter the
// grid: the chunk now holds a glowing block, or lies within a light's
// reach of one the grid already knows about.
bool ChunkAffectsGlow(const GlowGrid& g, const ChunkCoord& cc, const Chunk& c);
