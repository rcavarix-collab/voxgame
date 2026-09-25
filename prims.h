// prims.h
//
// Primitive shapes (DESIGN.md §2 props: trees, rocks and plants are built
// from primitives): hexagonal prisms, cones, boxes and rough rocks, emitted
// as flat-shaded triangles into a vertex list. Used by the props (baked
// per tile, rebuilt only when a prop changes), the debris, the wanderer and
// the effects (rebuilt per frame, but only a few hundred triangles).
// Triangles are clockwise seen from outside (the game's front face).
// Pure C++, tested natively.

#pragma once

#include "common.h"
#include <vector>

// 16 bytes. `rgba` is linear colour 0..255; alpha < 128 marks a glowing
// (unlit) surface -- fire, tracers, hot metal.
struct MeshVertex { float x, y, z; uint32_t rgba; };

static inline uint32_t Rgba(float r, float g, float b, bool glow = false) {
    auto c = [](float v) { return (uint32_t)(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | ((glow ? 64u : 255u) << 24);
}

// A placement: position, a yaw about Y, then a tilt (radians) about the
// horizontal axis `tiltYaw` -- enough for standing and falling trees.
struct Pose {
    Vec3 pos = { 0, 0, 0 };
    float yaw = 0, tilt = 0, tiltYaw = 0, scale = 1;
    Vec3 Apply(Vec3 local) const;
};

void PrimPrism(std::vector<MeshVertex>& out, const Pose& p, float radius, float height, int sides, uint32_t rgba);
void PrimCone(std::vector<MeshVertex>& out, const Pose& p, float base, float radius, float height, int sides, uint32_t rgba);
void PrimBox(std::vector<MeshVertex>& out, const Pose& p, Vec3 center, Vec3 half, uint32_t rgba);
// A rough rock: an icosahedron with its corners pushed in and out by `seed`, squashed by `flat`.
void PrimRock(std::vector<MeshVertex>& out, const Pose& p, float radius, float flat, uint32_t seed, uint32_t rgba);
