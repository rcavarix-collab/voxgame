// icons.h
//
// Hotbar icons rendered at load time: each block's real chunk mesh (so
// shapes, orientation and textures all show) drawn by a tiny software
// rasterizer from a three-quarter view into a transparent cell. One-off
// cost at startup, a fraction of a millisecond for the whole roster.
// Pure C++, no D3D.

#pragma once

#include "blocktex.h"

// Replaces `set.icons` (one BLOCK_TEX_SIZE cell per BlockID in a strip,
// BGRA with alpha) with rendered icons. Needs g_blockFaceLayer filled
// from `set` first.
void RenderBlockIcons(BlockTextureSet& set);
