// render.h
//
// Direct3D 11 presentation (docs/ARCHITECTURE.md 1.2): the device and swap
// chain, the compiled-shader cache (carried from Voxistics: bytecode kept
// in Documents\My Games\Cacophony\ShaderCache, compiled at most once per
// machine), the sky and terrain passes, and a small immediate-mode 2D
// layer for text and shapes.
//
// Cost: terrain chunk buffers are created only when a chunk's mesh
// changes (its version bumps) and freed when it leaves; drawing is one
// call per visible chunk after frustum culling. GPU time per pass is
// measured with timestamp queries read three frames late, so measuring
// never stalls.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "common.h"
#include "prims.h"
#include <vector>

class Terrain;
class Props;

bool InitRender(HWND hwnd);
void ShutdownRender();
void ResizeRender(int w, int h);          // follows the client area; ignores 0x0 (minimised)
const std::string& ShaderErrors();        // compiler complaints, if any (also in shader_errors.txt)

struct FrameView {
    Mat4 view, proj;
    Vec3 eye, right, up, forward;
    float tanHalfFovX, tanHalfFovY;
    float dayTime;
};

// Creates/frees GPU buffers for chunks whose meshes changed. Returns how many uploaded.
int SyncTerrain(const Terrain& t);
void GpuFrameBegin();
void RenderWorld(const Terrain& t, const FrameView& v);
// Props (baked per tile, uploaded only when a tile's mesh changes) and this
// frame's dynamic meshes (debris, the wanderer, rockets, tracers, flames,
// fireballs), drawn after the terrain with the same light and fog.
int SyncProps(const Props& p);
void RenderMeshes(const FrameView& v, const std::vector<MeshVertex>& dynamic);
// Where a world point lands on screen (pixels); false if behind the eye.
bool ProjectToScreen(const FrameView& v, Vec3 p, float& sx, float& sy);
void GpuMarkWorldDone();
void GpuFrameEnd();
void PresentFrame(bool vsync);

// ---- 2D, in pixels from the top-left; drawn over the world in order ----
void UIBegin();
void UIRect(float x, float y, float w, float h, uint32_t rgba);            // 0xRRGGBBAA, sRGB
void UIText(float x, float y, const char* text, uint32_t rgba);           // debug and menus only (DESIGN.md §7)
float UITextWidth(const char* text);
float UILineHeight();
void UIRing(float cx, float cy, float radius, float thickness, uint32_t rgba, int segments = 48);
void UIEnd();
