// essencemap.h
//
// The essence network map (DESIGN.md Part XIX): a top-down, pannable,
// zoomable view of the player's discovered essence network at real
// world coordinates -- a legible abstraction, not a dump of the
// simulation. Everything is qualitative (order-of-magnitude bands),
// minor sources merge into belts, and only the most significant things
// get labels. This module turns the network plus a camera into plain
// coloured triangles and label requests; the UI pass (game.cpp) just
// batches them. Pure C++, tested natively.

#pragma once

#include "essence.h"
#include <string>
#include <vector>

struct MapCamera {
    float centerX = 0, centerZ = 0; // world position at the screen centre
    float scale = 0.5f;             // pixels per block
    int screenW = 1280, screenH = 720;
};
static const float MAP_MIN_SCALE = 0.04f, MAP_MAX_SCALE = 8.0f;

struct MapLabel {
    std::string text;
    float x, y;       // pixel position of the text's top-left
    float scale;      // UI text scale
    float r, g, b, a;
};

struct MapDrawList {
    std::vector<float> tris;          // x, y, r, g, b, a per vertex; 3 vertices per triangle
    std::vector<MapLabel> labels;
    // For tests and the legibility rules.
    int nodesDrawn = 0, nodesLabeled = 0, routesDrawn = 0, routesLabeled = 0, belts = 0;
};

struct MapTuning {
    int labelTopNodes = 6;            // labels for the N most significant nodes on screen...
    int labelTopRoutes = 3;           // ...and routes
    float labelAllScale = 1.5f;       // zoomed in past this (px/block), every visible node is labeled
    float minorDotScale = 2.0f;       // zoomed in past this, minor nodes show as dots inside their belts
};

// Node size: order-of-magnitude compression of essence into a readable
// on-screen radius (the same compression as ScaleRadius in the older
// prototypes, applied to size rather than position), mildly zoom-aware.
float MapNodeRadius(double magnitude, float cameraScale);

// World <-> screen (top-down: +X right, +Z up).
static inline float MapScreenX(const MapCamera& c, float x) { return c.screenW * 0.5f + (x - c.centerX) * c.scale; }
static inline float MapScreenY(const MapCamera& c, float z) { return c.screenH * 0.5f - (z - c.centerZ) * c.scale; }
static inline float MapWorldX(const MapCamera& c, float sx) { return c.centerX + (sx - c.screenW * 0.5f) / c.scale; }
static inline float MapWorldZ(const MapCamera& c, float sy) { return c.centerZ - (sy - c.screenH * 0.5f) / c.scale; }

// Zoom by `factor` keeping the world point under screen (sx, sy) fixed.
void MapZoomAt(MapCamera& c, float factor, float sx, float sy);

// `time` animates route pulses. `textWidth(text, scale)` measures labels
// (the UI font), used to avoid overlapping labels.
void BuildMapDrawList(const EssenceNetwork& net, const MapCamera& cam, const MapTuning& t, float time,
                      float playerX, float playerZ, float playerYaw, float pivotX, float pivotZ,
                      float (*textWidth)(const std::string&, float), float textHeight,
                      MapDrawList& out);
