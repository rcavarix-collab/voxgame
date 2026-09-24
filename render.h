// render.h
//
// D3D11 device/pipeline state, chunk meshing, and the procedural sky
// mesh. Owns every ID3D11* global -- both main.cpp's frame
// loop and game.cpp's UI pass reach into these directly (the same
// unencapsulated-globals design the project has always used; this
// split relocates that design into files, it doesn't redesign it).

#pragma once

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX // MSVC's windows.h (pulled in via d3d11.h) defines min/max macros unless this precedes it
#endif
#include "common.h"
#include "world.h"
#include "mesher.h"
#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <string>

// ---- Core device/pipeline objects (world pass) ----
extern HWND g_hwnd;
extern ID3D11Device* g_device;
extern ID3D11DeviceContext* g_context;
extern IDXGISwapChain* g_swapChain;
extern ID3D11RenderTargetView* g_rtv;
extern ID3D11DepthStencilView* g_dsv;
extern ID3D11VertexShader* g_vs;
extern ID3D11PixelShader* g_ps;
extern ID3D11InputLayout* g_layout;
extern ID3D11Buffer* g_cbuffer;
extern ID3D11SamplerState* g_sampler;
extern ID3D11RasterizerState* g_rasterState;
extern ID3D11DepthStencilState* g_depthState;
extern ID3D11Buffer* g_chunkCBuffer;          // per-draw chunk origin (b1)
extern ID3D11ShaderResourceView* g_blockTexSRV; // Texture2DArray: one layer per block face texture, mipped
extern ID3D11ShaderResourceView* g_iconSRV;     // hotbar icon strip, one cell per BlockID

struct CBData { Mat4 mvp; Mat4 lightViewProj; float params[4]; float lineA[4]; float lineB[4]; float glowGrid[4]; }; // world shader b0 (192 bytes)

// UVs of block `id`'s cell in the icon strip.
static inline void IconRect(BlockID id, float& u0, float& v0, float& u1, float& v1) {
    const float e = 1.0f / 64.0f / 64.0f; // 1/64 texel: float-error guard only
    u0 = (float)id / BLOCK_COUNT + e; u1 = (float)(id + 1) / BLOCK_COUNT - e;
    v0 = e; v1 = 1.0f - e;
}

// ---- UI pass objects (Section 4.6): own shaders/layout/cbuffer/
// sampler/blend/depth state, fully separate from the world pass's. ----
extern ID3D11VertexShader* g_uiVS;
extern ID3D11PixelShader* g_uiPS;
extern ID3D11InputLayout* g_uiLayout;
extern ID3D11Buffer* g_uiCBuffer;
extern ID3D11SamplerState* g_uiSampler;
extern ID3D11BlendState* g_uiBlendState;
extern ID3D11DepthStencilState* g_uiDepthState;
extern ID3D11ShaderResourceView* g_uiSRV; // font-glyph + white-cell atlas
extern ID3D11Buffer* g_uiVB;              // dynamic, re-mapped per UI draw batch
static const UINT UI_VB_CAPACITY = 4096;   // vertices

// Font-glyph atlas layout (Section 4.6). Text is drawn 1:1 -- one atlas
// texel per screen pixel, point-sampled, snapped to whole pixels -- so
// glyphs stay crisp instead of being resampled from one master size. To
// still offer several text sizes, the atlas holds the full ASCII 32..126
// set baked once per size ("band"), each in a 16 x 6 grid; a requested
// scale picks the nearest band. Each glyph sits centred in a cell padded
// UI_GLYPH_PAD px each side, but advances only by the font's own
// monospace advance, so letters sit at normal text spacing rather than a
// full cell apart. The top UI_WHITE_H rows are solid white: untextured
// tinted rectangles sample their centre (Section 4.6).
// The struct and these helpers live here (not in game.h, where the
// UIDraw* helper *functions* that use them live) because InitD3D needs
// UIVertex to size g_uiVB and InitTextures needs the atlas dimensions
// to generate it -- both purely rendering concerns.
static const int UI_ATLAS_COLS = 16;
static const int UI_ATLAS_ROWS = 6;
static const int UI_GLYPH_PAD = 2;
static const int UI_WHITE_H = 8;
static const int UI_FONT_BAND_COUNT = 6;
// Cell height of each band; scale 1.0 == 28 px, the old single size.
static const int UI_BAND_CELL_H[UI_FONT_BAND_COUNT] = { 14, 18, 22, 28, 34, 44 };
struct UIFontBand { int cellW, cellH, advance, atlasY; float fontPx; };
static inline UIFontBand UIGetFontBand(int band) {
    UIFontBand b = {};
    int y = UI_WHITE_H;
    for (int i = 0; i <= band; i++) {
        b.cellH = UI_BAND_CELL_H[i];
        b.fontPx = b.cellH * 0.62f;
        // Consolas' advance is 0.55 em; round up and keep 1px of air.
        b.advance = (int)(b.fontPx * 0.55f + 0.999f) + 1;
        b.cellW = b.advance + 2 * UI_GLYPH_PAD;
        b.atlasY = y;
        y += UI_ATLAS_ROWS * b.cellH;
    }
    return b;
}
static inline int UIAtlasWidth() { return UI_ATLAS_COLS * UIGetFontBand(UI_FONT_BAND_COUNT - 1).cellW; }
static inline int UIAtlasHeight() {
    UIFontBand last = UIGetFontBand(UI_FONT_BAND_COUNT - 1);
    return last.atlasY + UI_ATLAS_ROWS * last.cellH;
}
struct UIVertex { float x, y, u, v, r, g, b, a; };

// ---- Sky pass objects (a third pass: depth off, drawn before the
// world so opaque geometry always overdraws it) ----
struct SkyVertex { float x, y, z; };
extern ID3D11VertexShader* g_skyVS;
extern ID3D11PixelShader* g_skyPS;
extern ID3D11InputLayout* g_skyLayout;
extern ID3D11Buffer* g_skyCBuffer;
extern ID3D11Buffer* g_skyVB;
extern ID3D11Buffer* g_skyIB;
extern UINT g_skyIndexCount;

bool InitD3D(HWND hwnd);
// Follows the window's client size (WM_SIZE); ignores a minimised (0x0) window.
void ResizeRenderTargets(int w, int h);
// Whether each optional effect compiled and was set up on this machine
// (the Graphics menu shows the ones that didn't as unavailable), and the
// compiler's complaints if any (also written to shader_errors.txt).
bool ShadowsAvailable();
bool PostEffectsAvailable();
bool BloomAvailable();
const std::string& ShaderErrors();
// Builds block textures (authored .vtex art from assets/textures plus
// procedural fallbacks) and the UI atlas. `problems` receives a one-line
// summary if any .vtex file had errors (details are written to
// assets/textures/_errors.txt), else stays empty.
bool InitTextures(std::string& problems);
void BuildSkyMesh();
void UpdateCBuffer(const CBData& data);
// The whole 3D frame: shadow map (when stale), sky, world, and the post
// pass when an effect is on. The UI pass draws over the result.
void RenderScene(World& w, const Mat4& view, const Mat4& proj, Vec3 eye, Vec3 forward, Vec3 up, float dayTime);
// In place of RenderScene when a full-screen screen (the essence map)
// covers the world: just clears the backbuffer for the UI pass.
void RenderEmptyScene();

// Capped per-frame chunk mesh rebuild (Section 4.2/4-perf) -- see
// render.cpp for the full reasoning; this is the single entry point
// the game loop calls once per frame.
void RebuildDirtyChunks(World& w, int camCx, int camCy, int camCz);

// ---- View-frustum culling (Section 4.2-perf) ----
//
// Six planes extracted directly from the combined view-projection
// matrix (Gribb/Hartmann), each as (a,b,c,d) with "inside" meaning
// a*x + b*y + c*z + d >= 0 -- world-space coordinates plug in directly,
// with no per-chunk transform needed to test against them. Keeps the
// world draw loop's GPU submissions to what the camera can actually see
// rather than every resident chunk around the player.
struct FrustumPlane { float a, b, c, d; };
struct Frustum { FrustumPlane planes[6]; };
Frustum ExtractFrustum(const Mat4& viewProj);
// True if the AABB is at least partially inside the frustum (a
// conservative test -- may pass a few actually-outside chunks near the
// frustum's edges, but never rejects one that's actually visible).
bool FrustumIntersectsAABB(const Frustum& f, Vec3 minB, Vec3 maxB);
