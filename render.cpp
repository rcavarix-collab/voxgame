// render.cpp
//
// D3D11 setup, chunk meshing, and the procedural sky mesh.

#include "render.h"
#include "profiler.h"
#include "blocktex.h"
#include "icons.h"
#include "sky.h"
#include "theline.h"
#include "audio.h"
#include "persist.h"
#include "vtex.h"
#include "glowlight.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------------
// textures.cpp entry points (the GDI+ UI font atlas).
// Kept as plain extern declarations since the project has no shared
// header enforcing this contract with textures.cpp itself (a .cpp
// can't include another .cpp's header without one existing); the two
// agree on it by convention, same as before the multi-file split.
// ---------------------------------------------------------------------
extern "C" void FreeGeneratedPixels(uint8_t* p);
extern "C" bool GenerateUIAtlas(
    int atlasW, int atlasH, int whiteH, int cols, int bandCount,
    const int* cellW, const int* cellH, const int* bandY, const float* fontPx,
    uint8_t** outPixelsBGRA);

HWND g_hwnd = nullptr;
int g_screenW = DEFAULT_WINDOW_W, g_screenH = DEFAULT_WINDOW_H;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ID3D11DepthStencilView* g_dsv = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11InputLayout* g_layout = nullptr;
ID3D11Buffer* g_cbuffer = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11RasterizerState* g_rasterState = nullptr;
ID3D11DepthStencilState* g_depthState = nullptr;
ID3D11Buffer* g_chunkCBuffer = nullptr;
// The frame's atmosphere (sky.h ComputeAtmosphere): FrameCB, register b2,
// shared by the sky and world shaders.
static ID3D11Buffer* g_frameCB = nullptr;
struct FrameCBData {
    float sunDir[4], sunColor[4], moonDir[4], moonColor[4];
    float zenith[4], horizon[4], twilight[4], ambientUp[4], ambientDown[4];
    float camPos[4], fog[4];
};
static const float CLOUD_COVER = 0.52f; // noise threshold: higher = clearer skies

// Off-screen scene target + a readable depth buffer, for the post pass.
static ID3D11RenderTargetView* g_sceneRTV = nullptr;
static ID3D11ShaderResourceView* g_sceneSRV = nullptr;
static ID3D11ShaderResourceView* g_depthSRV = nullptr;
// Sun shadow map (Section 4.8).
static const int SHADOW_SIZE = 2048;
static ID3D11DepthStencilView* g_shadowDSV = nullptr;
static ID3D11ShaderResourceView* g_shadowSRV = nullptr;
static ID3D11SamplerState* g_shadowSampler = nullptr;
static ID3D11RasterizerState* g_shadowRaster = nullptr;
static ID3D11VertexShader* g_shadowVS = nullptr;
static ID3D11Buffer* g_shadowCB = nullptr;
static bool g_shadowsAvailable = false; // shader compiled and resources created
// Post pass.
static ID3D11VertexShader* g_postVS = nullptr;
static ID3D11PixelShader* g_postPS = nullptr;
static ID3D11Buffer* g_postCB = nullptr;
static ID3D11SamplerState* g_pointClampSampler = nullptr;
static bool g_postAvailable = false;
// Bloom (Section 4.10): two quarter-resolution targets ping-ponged by a
// downsample and a separable blur, composited by the post pass.
static ID3D11RenderTargetView* g_bloomRTV[2] = { nullptr, nullptr };
static ID3D11ShaderResourceView* g_bloomSRV[2] = { nullptr, nullptr };
static int g_bloomW = 0, g_bloomH = 0;
static ID3D11PixelShader* g_bloomDownPS = nullptr;
static ID3D11PixelShader* g_bloomBlurPS = nullptr;
static ID3D11Buffer* g_bloomCB = nullptr;
static ID3D11SamplerState* g_linearClampSampler = nullptr;
static bool g_bloomAvailable = false;
static const float BLOOM_STRENGTH = 2.0f; // screen-blended, so it brightens but never clips
// Light cast by glowing blocks (Section 4.12): a 64^3 RG8 grid around the
// player, rebuilt on the CPU only when it moves or a block near a light
// changes.
static GlowGrid g_glowGrid;
static bool g_glowDirty = true;
static ID3D11Texture3D* g_glowTex = nullptr;
static ID3D11ShaderResourceView* g_glowSRV = nullptr;
static ID3D11SamplerState* g_glowSampler = nullptr;
static bool g_glowLit = false; // the grid holds at least one light
// See-through blocks (Section 4.11).
static ID3D11BlendState* g_translucentBlend = nullptr;       // alpha blend; keeps the glow mask in dest alpha
static ID3D11DepthStencilState* g_depthNoWriteState = nullptr; // depth tested, not written
static ID3D11RasterizerState* g_cullBackRaster = nullptr;
// Debug lines.
struct DebugVertex { float x, y, z, r, g, b, a; };
static const UINT DEBUG_VB_CAPACITY = 128;
static ID3D11VertexShader* g_debugVS = nullptr;
static ID3D11PixelShader* g_debugPS = nullptr;
static ID3D11InputLayout* g_debugLayout = nullptr;
static ID3D11Buffer* g_debugVB = nullptr;
static ID3D11Buffer* g_debugCB = nullptr;
// Bumped when a chunk mesh inside the shadow map's area is rebuilt: the
// map re-renders only when geometry it actually covers has changed, not
// for every far-off chunk streaming in.
static uint32_t g_meshVersion = 0;
static float g_shadowAreaX = 0, g_shadowAreaZ = 0, g_shadowAreaHalf = -1; // -1: no map yet
ID3D11ShaderResourceView* g_blockTexSRV = nullptr;
ID3D11ShaderResourceView* g_iconSRV = nullptr;

ID3D11VertexShader* g_uiVS = nullptr;
ID3D11PixelShader* g_uiPS = nullptr;
ID3D11InputLayout* g_uiLayout = nullptr;
ID3D11Buffer* g_uiCBuffer = nullptr;
ID3D11SamplerState* g_uiSampler = nullptr;
ID3D11BlendState* g_uiBlendState = nullptr;
ID3D11DepthStencilState* g_uiDepthState = nullptr;
ID3D11ShaderResourceView* g_uiSRV = nullptr;
ID3D11Buffer* g_uiVB = nullptr;

ID3D11VertexShader* g_skyVS = nullptr;
ID3D11PixelShader* g_skyPS = nullptr;
ID3D11InputLayout* g_skyLayout = nullptr;
ID3D11Buffer* g_skyCBuffer = nullptr;
ID3D11Buffer* g_skyVB = nullptr;
ID3D11Buffer* g_skyIB = nullptr;
UINT g_skyIndexCount = 0;

// Shared by the sky and world shaders (prepended to both): the frame's
// light and sky colours (sky.h ComputeAtmosphere, linear RGB), the clear-
// sky colour in any direction, distance fog and the final tonemap. The
// fog fades distant terrain into exactly the sky colour behind it, so the
// edge of the loaded world disappears into the horizon.
static const char* g_atmosphereSrc =
    "cbuffer FrameCB : register(b2) {\n"
    "    float4 fSunDir;      // xyz toward the sun\n"
    "    float4 fSunColor;    // direct sunlight (0 at night)\n"
    "    float4 fMoonDir;     // xyz toward the moon\n"
    "    float4 fMoonColor;   // direct moonlight\n"
    "    float4 fZenith;      // clear sky straight up; w = sunrise/sunset band strength\n"
    "    float4 fHorizon;     // clear sky at the horizon\n"
    "    float4 fTwilight;    // colour of the band around a low sun\n"
    "    float4 fAmbientUp;   // sky light onto up-facing surfaces\n"
    "    float4 fAmbientDown; // bounce light onto down-facing surfaces\n"
    "    float4 fCamPos;      // xyz eye; w = time in seconds (clouds)\n"
    "    float4 fFog;         // x fog start, y fog end (blocks), z exposure, w cloud cover\n"
    "};\n"
    "float3 SkyColor(float3 d) {\n"
    "    float h = saturate(d.y);\n"
    "    float3 col = lerp(fHorizon.rgb, fZenith.rgb, pow(h, 0.5f));\n"
    "    col *= 1.0f - 0.25f * saturate(-d.y * 4.0f);\n"
    "    float mu = dot(d, fSunDir.xyz);\n"
    "    float toward = saturate(mu * 0.5f + 0.5f);\n"
    "    float band = fZenith.w * pow(saturate(1.0f - abs(d.y) * 2.0f), 2.5f) * (0.25f + 0.75f * toward * toward * toward);\n"
    "    col = lerp(col, fTwilight.rgb, saturate(band * 1.3f));\n"
    "    float s = saturate(mu);\n"
    "    col += fSunColor.rgb * (0.10f * pow(s, 8.0f) + 0.25f * pow(s, 64.0f));\n"
    "    return col;\n"
    "}\n"
    "float FogAmount(float dist) {\n"
    "    float f = saturate((dist - fFog.x) / max(fFog.y - fFog.x, 1.0f));\n"
    "    return f * f * (3.0f - 2.0f * f);\n"
    "}\n"
    "float3 ToDisplay(float3 x) {\n"
    "    x *= fFog.z;\n"
    "    x = saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));\n" // ACES fit (Narkowicz)
    "    return pow(x, 1.0f / 2.2f);\n"
    "}\n";

// World pass shader (Section 4.2 / 4.8). Vertices arrive packed
// (mesher.h): chunk-local position plus a per-draw chunk origin, a
// texture-array layer, and bits for u/v, ambient occlusion and shade
// class. Lighting runs in linear light: hemisphere ambient (sky above,
// bounce below) darkened by AO, plus sun and moon by the face's facing,
// the sun shadowed when shadows are on; then distance fog into the sky
// colour and a filmic tonemap. The output alpha marks what glows (the
// bloom pass reads it). Compiled once as-is and, should that fail on some
// driver, again with NO_SHADOWS, so a shadow problem can never cost the
// world.
static const char* g_shaderSrc =
    "// uses atmosphere\n"
    "cbuffer CB : register(b0) { row_major matrix mvp; row_major matrix lightViewProj; float4 params; float4 lineA; float4 lineB; float4 glowGrid; };\n"
    // params: x shadows on, y shadow half-texel, z 1 while drawing see-through blocks (4.11).
    // lineA: The Line's pivot x, height, pivot z, intensity; lineB: its direction x, z, the music level, unused.
    // glowGrid: xyz the glow-light grid's world origin, w 1 when it holds any light (4.12).
    "cbuffer ChunkCB : register(b1) { float4 chunkOrigin; };\n"
    "struct VSIn { uint4 pos:POSITION; uint layer:TEXCOORD0; uint2 uv:TEXCOORD1; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 uvl:TEXCOORD0; float2 aoBias:TEXCOORD1; float3 wpos:TEXCOORD2; float4 glowInfo:TEXCOORD3; };\n"
    // A light touch of the old fixed per-direction shading keeps two faces
    // at the same angle to the sun from reading as one flat surface.
    "static const float faceBias[8] = { 0.94f, 0.94f, 1.00f, 0.90f, 0.88f, 0.88f, 1.00f, 0.92f };\n"
    "static const float3 faceNormal[8] = { float3(1,0,0), float3(-1,0,0), float3(0,1,0), float3(0,-1,0),\n"
    "                                      float3(0,0,1), float3(0,0,-1), float3(0,0.8f,0.6f), float3(0,-0.8f,0.6f) };\n"
    "static const float aoCurve[4] = { 0.42f, 0.62f, 0.82f, 1.00f };\n"
    "PSIn VSMain(VSIn i) {\n"
    "    PSIn o;\n"
    "    float3 p = float3(i.pos.xyz) * 0.125f + chunkOrigin.xyz;\n"   // 1/8-block fixed point
    "    o.pos = mul(float4(p, 1.0f), mvp);\n"
    "    o.uvl = float3(float2(i.uv) * 0.125f, (float)i.layer);\n"
    "    uint face = (i.pos.w >> 2) & 7u;\n"
    "    o.aoBias = float2(aoCurve[i.pos.w & 3u], faceBias[face]);\n"
    "    float3 n = normalize(faceNormal[face]);\n"
    "    o.wpos = p + n * 0.08f;\n"                                   // normal offset (> 1 shadow texel): no acne
    "    o.glowInfo = float4((float)((i.pos.w >> 5) & 3u), n);\n"   // glow kind, face normal
    "    return o;\n"
    "}\n"
    "Texture2DArray tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "Texture3D glowTex : register(t2);\n"
    "SamplerState glowSamp : register(s2);\n"
    "#ifndef NO_SHADOWS\n"
    "Texture2D<float> shadowMap : register(t1);\n"
    "SamplerComparisonState shadowSamp : register(s1);\n"
    "#endif\n"
    "float4 PSMain(PSIn i) : SV_TARGET {\n"
    "    float4 texel = tex0.Sample(samp0, i.uvl);\n"                  // sRGB texture view: already linear
    "    float3 albedo = texel.rgb;\n"
    "    float3 n = i.glowInfo.yzw;\n"
    "    float ao = i.aoBias.x;\n"
    "    float shadow = 1.0f;\n"
    // Softened falloff (sqrt of N.L): a faked wrap so a low sun still
    // lights flat ground enough for its long shadows to read at dawn/dusk.
    "    float sunLit = sqrt(saturate(dot(n, fSunDir.xyz)));\n"
    "#ifndef NO_SHADOWS\n"
    "    if (params.x > 0.5f && sunLit > 0.0f) {\n"
    "        float4 lp = mul(float4(i.wpos, 1.0f), lightViewProj);\n"
    "        float2 suv = float2(lp.x * 0.5f + 0.5f, 0.5f - lp.y * 0.5f);\n"
    "        if (suv.x > 0.0f && suv.x < 1.0f && suv.y > 0.0f && suv.y < 1.0f && lp.z < 1.0f) {\n"
    "            float o = params.y;\n"                               // 2x2 taps of hardware PCF
    "            float lit = 0.25f * (shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2(-o, -o), lp.z)\n"
    "                               + shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2( o, -o), lp.z)\n"
    "                               + shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2(-o,  o), lp.z)\n"
    "                               + shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2( o,  o), lp.z));\n"
    // Fade out toward the map's edge rather than stopping at a line.
    "            float edge = saturate(min(min(suv.x, 1.0f - suv.x), min(suv.y, 1.0f - suv.y)) * 16.0f);\n"
    "            shadow = lerp(1.0f, lit, edge);\n"
    "            sunLit *= shadow;\n"
    "        }\n"
    "    }\n"
    "#endif\n"
    "    float3 ambient = lerp(fAmbientDown.rgb, fAmbientUp.rgb, n.y * 0.5f + 0.5f) * ao;\n"
    "    float3 direct = fSunColor.rgb * sunLit * (0.55f + 0.45f * ao)\n"
    "                  + fMoonColor.rgb * saturate(dot(n, fMoonDir.xyz)) * ao;\n"
    "    float3 col = albedo * (ambient + direct) * i.aoBias.y;\n"
    // Light from glowing blocks nearby (4.12): one lookup in the light
    // grid, at the centre of the open cell this face looks into, so a wall
    // between a light and a surface leaves the surface dark. Music light
    // follows the music; timestream light counts only near The Line
    // (it lights its blocks only as it passes -- a cheap stand-in for
    // knowing which ones are lit).
    "    if (glowGrid.w > 0.5f) {\n"
    "        float2 gl = glowTex.SampleLevel(glowSamp, (i.wpos + n * 0.42f - glowGrid.xyz) / 64.0f, 0).rg;\n"
    "        float2 rl = i.wpos.xz - lineA.xz;\n"
    "        float lineNear = saturate(1.0f - abs(rl.x * lineB.y - rl.y * lineB.x) / 8.0f) * saturate(1.0f - abs(i.wpos.y - lineA.y) / 8.0f);\n"
    "        float3 emitted = gl.r * lineB.z * float3(1.0f, 0.62f, 0.25f) + gl.g * lineNear * float3(0.35f, 0.85f, 1.0f);\n"
    "        col += albedo * emitted * 1.5f * ao;\n"
    "    }\n"
    // Reactive blocks (blocks.h BlockGlow): 1 = the music playing now,
    // 2 = The Line passing through this block's cell (found per pixel from
    // the world position minus the face normal: a vertex on a corner could
    // floor into the neighbouring cell). They emit light of their own.
    "    float glow = 0.0f;\n"
    "    float3 glowCol = float3(1.0f, 0.62f, 0.25f);\n"
    "    if (i.glowInfo.x > 1.5f) {\n"
    "        float3 cell = floor(i.wpos - n * 0.58f) + 0.5f;\n"
    "        float2 r = cell.xz - lineA.xz;\n"
    "        float across = abs(r.x * lineB.y - r.y * lineB.x);\n"
    "        glow = saturate(1.0f - across / 0.75f) * saturate((0.6f - abs(cell.y - lineA.y)) * 4.0f);\n"
    "        glowCol = float3(0.35f, 0.85f, 1.0f);\n"
    "    } else if (i.glowInfo.x > 0.5f) {\n"
    "        glow = lineB.z;\n"
    "    }\n"
    "    col += glow * (albedo * 1.2f + glowCol * 0.8f);\n"
    "    float3 v = i.wpos - fCamPos.xyz;\n"
    "    float dist = length(v);\n"
    "    float3 view = v / max(dist, 1e-3f);\n"
    "    float outAlpha = saturate(glow);\n"                            // opaque pass: the bloom mask
    // See-through blocks (4.11), faked: the tinted body lets the world
    // behind show through by the texture's alpha; toward grazing angles it
    // turns into a mirror of the sky (Schlick's Fresnel) and grows more
    // opaque, and the sun leaves a hard glint unless shadowed. No
    // refraction, no second scene render.
    "    if (params.z > 0.5f) {\n"
    "        float3 r = reflect(view, n);\n"
    "        float fres = 0.04f + 0.96f * pow(1.0f - saturate(dot(-view, n)), 5.0f);\n"
    "        float3 refl = SkyColor(r) * lerp(0.35f, 1.0f, saturate(r.y * 2.0f + 0.5f));\n" // the ground reflects darker than the sky
    "        float3 glint = fSunColor.rgb * shadow * pow(saturate(dot(r, fSunDir.xyz)), 400.0f) * 6.0f;\n"
    "        col = lerp(col, refl, fres) + glint;\n"
    "        outAlpha = saturate(lerp(texel.a, 1.0f, fres) + dot(glint, float3(0.3f, 0.5f, 0.2f)));\n"
    "    }\n"
    "    col = lerp(col, SkyColor(view), FogAmount(dist));\n"
    "    return float4(ToDisplay(col), outAlpha);\n"
    "}\n";

// Depth-only pass into the shadow map, from the sun (Section 4.8).
// Same vertex layout as the world pass so the chunk buffers are shared.
static const char* g_shadowShaderSrc =
    "cbuffer ShadowCB : register(b0) { row_major matrix lightViewProj; };\n"
    "cbuffer ChunkCB : register(b1) { float4 chunkOrigin; };\n"
    "struct VSIn { uint4 pos:POSITION; uint layer:TEXCOORD0; uint2 uv:TEXCOORD1; };\n"
    "float4 VSMain(VSIn i) : SV_POSITION {\n"
    "    float3 p = float3(i.pos.xyz) * 0.125f + chunkOrigin.xyz;\n"
    "    return mul(float4(p, 1.0f), lightViewProj);\n"
    "}\n";

// Debug lines (The Line's test marker, Part XVIII): coloured line list,
// depth tested. A testing aid only.
static const char* g_debugShaderSrc =
    "cbuffer DebugCB : register(b0) { row_major matrix viewProj; };\n"
    "struct VSIn { float3 pos:POSITION; float4 col:COLOR0; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float4 col:COLOR0; };\n"
    "PSIn VSMain(VSIn i) { PSIn o; o.pos = mul(float4(i.pos, 1.0f), viewProj); o.col = i.col; return o; }\n"
    "float4 PSMain(PSIn i) : SV_TARGET { return i.col; }\n";

// Screen-space post pass (Section 4.8): edge outlines and ambient
// occlusion from the depth buffer alone. A full-screen triangle made
// from SV_VertexID, so it needs no vertex buffer.
static const char* g_postShaderSrc =
    "cbuffer PostCB : register(b0) { float4 p0; float4 p1; };\n"
    // p0: x outlines on, y SSAO on, z near plane, w far plane; p1: x 1/width, y 1/height, z proj[1][1], w bloom strength (0 = off)
    "Texture2D sceneTex : register(t0);\n"
    "Texture2D<float> depthTex : register(t1);\n"
    "Texture2D bloomTex : register(t2);\n"
    "SamplerState pointSamp : register(s0);\n"
    "SamplerState linearSamp : register(s1);\n"
    "struct VSOut { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };\n"
    "VSOut VSMain(uint id : SV_VertexID) {\n"
    "    VSOut o;\n"
    "    float2 uv = float2((float)((id << 1) & 2u), (float)(id & 2u));\n"
    "    o.uv = uv;\n"
    "    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "    return o;\n"
    "}\n"
    "float LinDepth(float2 uv) {\n"
    "    float z = depthTex.SampleLevel(pointSamp, uv, 0);\n"
    "    return p0.z * p0.w / (p0.w - z * (p0.w - p0.z));\n"
    "}\n"
    "float3 Bloom(float3 c, float2 uv) {\n"
    // Screen blend: glow brightens without ever clipping to flat white.
    "    if (p1.w <= 0.0f) return c;\n"
    "    float3 b = bloomTex.SampleLevel(linearSamp, uv, 0).rgb * p1.w;\n"
    "    return 1.0f - (1.0f - c) * (1.0f - saturate(b));\n"
    "}\n"
    "float4 PSMain(VSOut i) : SV_TARGET {\n"
    "    float3 c = sceneTex.SampleLevel(pointSamp, i.uv, 0).rgb;\n"
    "    float d = LinDepth(i.uv);\n"
    "    if (d > p0.w * 0.98f) return float4(Bloom(c, i.uv), 1.0f);\n" // sky: nothing to shade, but the sun glows
    "    float2 px = p1.xy;\n"
    "    float l = LinDepth(i.uv - float2(px.x, 0.0f)), r = LinDepth(i.uv + float2(px.x, 0.0f));\n"
    "    float u = LinDepth(i.uv - float2(0.0f, px.y)), dn = LinDepth(i.uv + float2(0.0f, px.y));\n"
    "    if (p0.x > 0.5f) {\n"
    // Outlines: the depth Laplacian is ~0 across any flat surface, however
    // steeply it's viewed, and spikes at silhouettes and block edges.
    "        float lap = abs(l + r + u + dn - 4.0f * d) / d;\n"
    "        float edge = saturate((lap - 0.012f) * 30.0f);\n"
    "        c *= 1.0f - 0.55f * edge;\n"
    "    }\n"
    "    if (p0.y > 0.5f) {\n"
    // SSAO from depth only: compare each sample to the depth the local
    // plane predicts there (central-difference gradient), so flat ground
    // seen at a grazing angle doesn't occlude itself.
    "        float2 grad = float2(r - l, dn - u) * 0.5f;\n"
    "        float radius = clamp(0.6f * p1.z * 0.5f / (d * px.y), 3.0f, 48.0f);\n" // 0.6 blocks, in pixels
    "        float ang = frac(52.9829189f * frac(dot(i.pos.xy, float2(0.06711056f, 0.00583715f)))) * 6.2831853f;\n"
    "        float occ = 0.0f;\n"
    "        [unroll] for (int k = 0; k < 12; k++) {\n"
    "            float t = (k + 0.5f) / 12.0f;\n"
    "            float a = ang + k * 2.39996f;\n"
    "            float2 offPx = float2(cos(a), sin(a)) * radius * sqrt(t);\n"
    "            float s = LinDepth(i.uv + offPx * px);\n"
    "            float diff = (d + dot(grad, offPx)) - s;\n"             // how far in front of the plane
    "            occ += saturate(diff * 3.0f) * saturate(1.5f - diff);\n"
    "        }\n"
    "        c *= 1.0f - 0.55f * occ / 12.0f;\n"
    "    }\n"
    "    return float4(Bloom(c, i.uv), 1.0f);\n"
    "}\n";

// Bloom (Section 4.10), at quarter resolution, drawn with the post pass's
// full-screen triangle. Only what the scene marks as glowing (its alpha:
// reactive blocks, the sun's disc) blooms -- no brightness threshold, so
// a sunlit wall never smears.
// Downsample: 4 bilinear taps = a 4x4 box of the full-res scene.
static const char* g_bloomDownShaderSrc =
    "cbuffer BloomCB : register(b0) { float4 texStep; };\n" // xy: one full-res texel
    "Texture2D sceneTex : register(t0);\n"
    "SamplerState linearSamp : register(s1);\n"
    "struct VSOut { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };\n"
    "float3 Tap(float2 uv) { float4 s = sceneTex.SampleLevel(linearSamp, uv, 0); return s.rgb * s.a; }\n"
    "float4 PSMain(VSOut i) : SV_TARGET {\n"
    "    float2 o = texStep.xy;\n"
    "    return float4(0.25f * (Tap(i.uv + float2(-o.x, -o.y)) + Tap(i.uv + float2(o.x, -o.y))\n"
    "                         + Tap(i.uv + float2(-o.x,  o.y)) + Tap(i.uv + float2(o.x,  o.y))), 1.0f);\n"
    "}\n";
// Separable 9-tap Gaussian in 5 bilinear fetches, along step.xy.
static const char* g_bloomBlurShaderSrc =
    "cbuffer BloomCB : register(b0) { float4 texStep; };\n" // xy: texel step along the blur direction
    "Texture2D srcTex : register(t0);\n"
    "SamplerState linearSamp : register(s1);\n"
    "struct VSOut { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };\n"
    "float4 PSMain(VSOut i) : SV_TARGET {\n"
    "    float2 d1 = texStep.xy * 1.3846154f, d2 = texStep.xy * 3.2307692f;\n"
    "    float3 c = srcTex.SampleLevel(linearSamp, i.uv, 0).rgb * 0.2270270f\n"
    "             + (srcTex.SampleLevel(linearSamp, i.uv + d1, 0).rgb + srcTex.SampleLevel(linearSamp, i.uv - d1, 0).rgb) * 0.3162162f\n"
    "             + (srcTex.SampleLevel(linearSamp, i.uv + d2, 0).rgb + srcTex.SampleLevel(linearSamp, i.uv - d2, 0).rgb) * 0.0702703f;\n"
    "    return float4(c, 1.0f);\n"
    "}\n";

// UI pass shader: takes vertex positions already in pixel space and
// maps them to NDC directly (an orthographic projection in all but
// name -- Section 4.6), plus a per-vertex color tint so the same
// textured quad can draw plain glyphs, tinted panels/borders, and
// full-color icons through one pipeline.
static const char* g_uiShaderSrc =
    "cbuffer UICB : register(b0) { float4 screenSize; };\n"
    "struct VSIn { float2 pos:POSITION; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
    "PSIn VSMain(VSIn input) {\n"
    "    PSIn o;\n"
    "    float2 ndc = float2(input.pos.x / screenSize.x * 2.0f - 1.0f, 1.0f - input.pos.y / screenSize.y * 2.0f);\n"
    "    o.pos = float4(ndc, 0.0f, 1.0f);\n"
    "    o.uv = input.uv;\n"
    "    o.col = input.col;\n"
    "    return o;\n"
    "}\n"
    "Texture2D tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "float4 PSMain(PSIn input) : SV_TARGET { return tex0.Sample(samp0, input.uv) * input.col; }\n";

// Third pass state: a basic skybox, drawn before the world so normal
// depth-tested opaque geometry always overdraws it. Untextured -- a
// gradient computed per-pixel from the interpolated (camera-relative)
// position reused as a view direction -- so its own tiny pipeline
// (position only, no sampler/texture) rather than reusing either the
// world or UI shader. Per-pixel rather than per-vertex specifically to
// avoid the box-corner artifact a coarse per-vertex gradient on a
// 6-quad box shows: near a geometric box corner (shared by 3 faces,
// all zenith-colored there), vertex interpolation alone pulls in extra
// zenith color even at screen positions whose actual view direction is
// nowhere near straight up. Computing the gradient from the true
// (renormalized) direction instead makes it smooth in every direction,
// independent of the mesh's face boundaries.
static const char* g_skyShaderSrc =
    "// uses atmosphere\n"
    "cbuffer SkyCB : register(b0) {\n"
    "    row_major matrix viewProj;\n"
    "    float4 params;    // x stars visible, y direct-sun amount (disc brightness)\n"
    "    float4 moon;      // xyz toward the moon, w visibility\n"
    "    float4 ghostMoon; // xyz toward The Line's ghost moon, w strength\n"
    "    float4 starRow0; float4 starRow1; float4 starRow2; // sky direction -> star-field direction\n"
    "};\n"
    "struct VSIn { float3 pos:POSITION; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 dir:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn input) { PSIn o; o.pos = mul(float4(input.pos,1.0f), viewProj); o.dir = input.pos; return o; }\n"
    "float Hash(float3 p) { p = frac(p * 0.3183099f + 0.1f); p *= 17.0f; return frac(p.x * p.y * p.z * (p.x + p.y + p.z)); }\n"
    // Procedural stars: each face of a cube around the sky is a grid of
    // ~1-degree cells; some cells hold one star at a hashed spot, size and
    // brightness. Looked up in star-field space, so the whole field turns
    // with the clock (and The Line's wobble) by rotating the lookup.
    "float Stars(float3 s) {\n"
    "    float3 a = abs(s);\n"
    "    float2 uv; float face;\n"
    "    if (a.x >= a.y && a.x >= a.z) { uv = s.yz / a.x; face = s.x > 0.0f ? 0.0f : 1.0f; }\n"
    "    else if (a.y >= a.z)          { uv = s.xz / a.y; face = s.y > 0.0f ? 2.0f : 3.0f; }\n"
    "    else                          { uv = s.xy / a.z; face = s.z > 0.0f ? 4.0f : 5.0f; }\n"
    "    float2 g = (uv * 0.5f + 0.5f) * 90.0f;\n"
    "    float3 key = float3(floor(g), face);\n"
    "    if (Hash(key) > 0.18f) return 0.0f;\n"
    "    float2 spot = float2(Hash(key + 7.1f), Hash(key + 3.7f)) * 0.7f + 0.15f;\n"
    "    float size = 0.10f + 0.14f * Hash(key + 1.3f);\n"
    "    float b = saturate(1.0f - length(frac(g) - spot) / size);\n"
    "    return b * b * (0.5f + 0.9f * Hash(key + 9.2f));\n"
    "}\n"
    // Clouds: four octaves of value noise on a plane above the world,
    // drifting with time -- sky pixels only, so the cost is fixed.
    "float Hash2(float2 p) { p = frac(p * float2(0.1031f, 0.1030f)); p += dot(p, p.yx + 33.33f); return frac((p.x + p.y) * p.x); }\n"
    "float Noise2(float2 p) {\n"
    "    float2 i = floor(p), f = frac(p), u = f * f * (3.0f - 2.0f * f);\n"
    "    return lerp(lerp(Hash2(i), Hash2(i + float2(1, 0)), u.x), lerp(Hash2(i + float2(0, 1)), Hash2(i + float2(1, 1)), u.x), u.y);\n"
    "}\n"
    "float CloudNoise(float3 d) {\n"
    "    float2 uv = d.xz / (d.y + 0.12f) * 0.9f + float2(fCamPos.w * 0.006f, fCamPos.w * 0.002f);\n"
    "    return 0.5f * Noise2(uv) + 0.25f * Noise2(uv * 2.03f + 17.1f) + 0.125f * Noise2(uv * 4.1f + 5.3f) + 0.0625f * Noise2(uv * 8.2f + 9.7f);\n"
    "}\n"
    "float Disc(float3 d, float3 c, float cosR) { return smoothstep(cosR - 0.00008f, cosR + 0.00002f, dot(d, c)); }\n"
    "float4 PSMain(PSIn input) : SV_TARGET {\n"
    "    float3 d = normalize(input.dir);\n"
    "    float3 col = SkyColor(d);\n"
    "    float above = smoothstep(-0.04f, 0.04f, d.y);\n"
    "    float mu = dot(d, fSunDir.xyz);\n"
    "    float cloud = 0.0f, cloudN = 0.0f;\n"
    "    if (d.y > 0.0f) {\n"
    "        cloudN = CloudNoise(d);\n"
    "        cloud = smoothstep(fFog.w, fFog.w + 0.22f, cloudN) * smoothstep(0.0f, 0.15f, d.y);\n"
    "    }\n"
    // Stars, moon and its ghost behind the clouds.
    "    float3 s = float3(dot(starRow0.xyz, d), dot(starRow1.xyz, d), dot(starRow2.xyz, d));\n"
    "    float veil = 1.0f - cloud * 0.9f;\n"
    "    col += Stars(s) * params.x * above * veil * float3(0.8f, 0.85f, 1.0f);\n"
    "    float3 moonCol = float3(0.9f, 0.92f, 1.0f) * 1.4f;\n"
    "    col = lerp(col, moonCol, Disc(d, moon.xyz, 0.99966f) * moon.w * above * veil);\n"
    "    col += moonCol * Disc(d, ghostMoon.xyz, 0.99966f) * ghostMoon.w * above * veil;\n"
    // The sun's disc (bright enough to bloom), dimmed by cloud.
    "    float sunDisc = smoothstep(0.9990f, 0.9996f, mu) * params.y * above;\n"
    "    col += sunDisc * float3(1.0f, 0.9f, 0.7f) * 30.0f * (1.0f - cloud * 0.85f);\n"
    // Clouds lit by the sky and the sun: silver lining toward the sun,
    // warmer at dusk, darker where they're thickest.
    "    float3 cloudLit = fAmbientUp.rgb * 1.1f + fSunColor.rgb * (0.30f + 0.55f * pow(saturate(mu), 4.0f)) + fMoonColor.rgb * 0.8f;\n"
    "    cloudLit *= lerp(1.0f, 0.72f, smoothstep(fFog.w + 0.12f, fFog.w + 0.35f, cloudN));\n"
    "    col = lerp(col, cloudLit, cloud * 0.92f);\n"
    // Glow mask for bloom: the disc, plus a softer halo around it.
    "    float sunGlow = saturate(sunDisc + 0.5f * smoothstep(0.985f, 0.9996f, mu) * params.y * above);\n"
    "    return float4(ToDisplay(col), sunGlow * (1.0f - cloud));\n"
    "}\n";

// Emits one cube face (4 verts + 6 indices) for the given corners.
static void RebuildChunkMesh(World& w, const ChunkCoord& cc, Chunk& c) {
    static std::vector<Vertex> verts;     // reused across rebuilds: no per-rebuild allocation once warm
    static std::vector<uint16_t> indices;
    size_t translucentFirst = 0;
    BuildChunkMesh(w, cc, c, verts, indices, &translucentFirst);

    if (c.vb) { c.vb->Release(); c.vb = nullptr; }
    if (c.ib) { c.ib->Release(); c.ib = nullptr; }
    c.indexCount = 0; c.opaqueIndexCount = 0;

    if (!verts.empty()) {
        D3D11_BUFFER_DESC vbd = {};
        vbd.Usage = D3D11_USAGE_IMMUTABLE;
        vbd.ByteWidth = (UINT)(verts.size() * sizeof(Vertex));
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vinit = {};
        vinit.pSysMem = verts.data();
        g_device->CreateBuffer(&vbd, &vinit, &c.vb);

        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint16_t));
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA iinit = {};
        iinit.pSysMem = indices.data();
        g_device->CreateBuffer(&ibd, &iinit, &c.ib);

        c.indexCount = (UINT)indices.size();
        c.opaqueIndexCount = (UINT)translucentFirst;
    }

    c.dirty = false;
    if (ChunkAffectsGlow(g_glowGrid, cc, c)) g_glowDirty = true;
    float cx = (cc.x + 0.5f) * CHUNK_SIZE, cz = (cc.z + 0.5f) * CHUNK_SIZE;
    float reach = g_shadowAreaHalf + CHUNK_SIZE; // a chunk overlapping the area's edge counts
    if (g_shadowAreaHalf < 0 || (fabsf(cx - g_shadowAreaX) < reach && fabsf(cz - g_shadowAreaZ) < reach)) g_meshVersion++;
}

// Capped the same way block updates (MAX_UPDATES_PER_TICK) and column generation
// (MAX_COLUMN_GENS_PER_TICK) already are: entering a large unexplored
// area can generate several new columns in a single tick (each up to a
// few vertical chunks tall, per GenerateColumn), all newly dirty at
// once -- rebuilding all of them here in the same frame means several
// full 4096-cell mesh passes *and* several pairs of synchronous GPU
// CreateBuffer calls back to back, which is exactly the kind of
// single-frame spike that shows up as a stutter when moving into new
// terrain. Capping it spreads that same total work across a handful of
// frames (a burst of ~16 chunks at this cap resolves in ~3 frames, well
// under 60ms) instead of paying for all of it at once; any chunk left
// dirty this frame simply isn't drawn yet (the world draw loop already
// skips a zero-index-count chunk) and gets its turn next frame.
static const int MAX_CHUNK_REBUILDS_PER_FRAME = 6;
// Nearest-first: of the chunks waiting (w.dirtyChunks -- nothing to scan
// at all while the world is static), rebuild the few closest to the
// camera. New ground, a load, or a render-distance change then fills in
// outward from the player instead of in hash-map order, and the chunk
// being edited under the cursor is never queued behind distant ones.
void RebuildDirtyChunks(World& w, int camCx, int camCy, int camCz) {
    if (w.dirtyChunks.empty()) return;
    struct Pending { long long d2; ChunkCoord cc; };
    static std::vector<Pending> pending; // reused: no per-frame allocation once warm
    pending.clear();
    for (auto it = w.dirtyChunks.begin(); it != w.dirtyChunks.end();) {
        const ChunkCoord& cc = *it;
        // A chunk's mesh reads all 8 neighbouring columns (culling, AO);
        // until they exist it waits outside the set, still flagged dirty,
        // and GenerateColumn re-queues it when the last neighbour arrives.
        // One build per chunk instead of one per neighbour arrival.
        if (!ColumnNeighborhoodResident(cc.x, cc.z)) { it = w.dirtyChunks.erase(it); continue; }
        long long dx = cc.x - camCx, dy = cc.y - camCy, dz = cc.z - camCz;
        pending.push_back({ dx * dx + dy * dy + dz * dz, cc });
        ++it;
    }
    size_t n = std::min<size_t>(MAX_CHUNK_REBUILDS_PER_FRAME, pending.size());
    auto nearer = [](const Pending& a, const Pending& b) { return a.d2 < b.d2; };
    if (n < pending.size()) std::nth_element(pending.begin(), pending.begin() + n, pending.end(), nearer);
    for (size_t i = 0; i < n; i++) {
        const ChunkCoord& cc = pending[i].cc;
        w.dirtyChunks.erase(cc);
        if (Chunk* c = w.FindChunk(cc)) { RebuildChunkMesh(w, cc, *c); ProfAddCounter(PCOUNT_MESHES_BUILT, 1); }
    }
}

static void AddSkyQuad(std::vector<SkyVertex>& v, std::vector<uint32_t>& idx,
                        float x0, float y0, float z0, float x1, float y1, float z1,
                        float x2, float y2, float z2, float x3, float y3, float z3) {
    uint32_t base = (uint32_t)v.size();
    v.push_back({ x0, y0, z0 });
    v.push_back({ x1, y1, z1 });
    v.push_back({ x2, y2, z2 });
    v.push_back({ x3, y3, z3 });
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}

// A large inverted box centered on the camera each frame (Section 4.6-
// adjacent: a third pass, drawn with depth off before the opaque world
// pass so normal depth-tested geometry always overdraws it, and with
// its view matrix's translation stripped so it never appears to move
// as the player walks -- only as they look around, exactly like a
// conventional skybox). No per-vertex color needed -- the pixel shader
// computes the zenith/horizon gradient itself from the interpolated
// position, reused as a view direction.
void BuildSkyMesh() {
    std::vector<SkyVertex> verts;
    std::vector<uint32_t> indices;
    const float E = 50.0f; // arbitrary -- depth test is off, so size only has to clear the near plane

    AddSkyQuad(verts, indices, -E, E, -E, -E, E, E, E, E, E, E, E, -E); // top
    AddSkyQuad(verts, indices, -E, -E, E, -E, -E, -E, E, -E, -E, E, -E, E); // bottom
    AddSkyQuad(verts, indices, E, E, -E, E, E, E, E, -E, E, E, -E, -E); // +X
    AddSkyQuad(verts, indices, -E, E, E, -E, E, -E, -E, -E, -E, -E, -E, E); // -X
    AddSkyQuad(verts, indices, E, E, E, -E, E, E, -E, -E, E, E, -E, E); // +Z
    AddSkyQuad(verts, indices, -E, E, -E, E, E, -E, E, -E, -E, -E, -E, -E); // -Z

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(verts.size() * sizeof(SkyVertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = {}; vinit.pSysMem = verts.data();
    g_device->CreateBuffer(&vbd, &vinit, &g_skyVB);

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = {}; iinit.pSysMem = indices.data();
    g_device->CreateBuffer(&ibd, &iinit, &g_skyIB);

    g_skyIndexCount = (UINT)indices.size();
}

void UpdateCBuffer(const CBData& data) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &data, sizeof(CBData));
    g_context->Unmap(g_cbuffer, 0);
}

// Every resident chunk whose bounds touch `frustum`, setting the per-draw
// chunk origin. Shared by the world pass and the shadow pass; whoever
// calls it has already bound shaders, layout and constant buffers.
// Opaque parts only (the shadow map uses this too, so glass casts no
// shadow); see-through parts are DrawTranslucent's.
static void DrawChunks(World& w, const Frustum& frustum, bool countStats) {
    UINT stride = sizeof(Vertex), offset = 0;
    for (auto& kv : w.chunks) {
        Chunk& c = *kv.second;
        if (c.opaqueIndexCount == 0) continue;
        const ChunkCoord& cc = kv.first;
        Vec3 minB = { (float)(cc.x * CHUNK_SIZE), (float)(cc.y * CHUNK_SIZE), (float)(cc.z * CHUNK_SIZE) };
        Vec3 maxB = { minB.x + CHUNK_SIZE, minB.y + CHUNK_SIZE, minB.z + CHUNK_SIZE };
        if (!FrustumIntersectsAABB(frustum, minB, maxB)) continue;
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_chunkCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        float* o = (float*)mapped.pData;
        o[0] = minB.x; o[1] = minB.y; o[2] = minB.z; o[3] = 0.0f;
        g_context->Unmap(g_chunkCBuffer, 0);
        g_context->IASetVertexBuffers(0, 1, &c.vb, &stride, &offset);
        g_context->IASetIndexBuffer(c.ib, DXGI_FORMAT_R16_UINT, 0);
        g_context->DrawIndexed(c.opaqueIndexCount, 0, 0);
        if (countStats) {
            ProfAddCounter(PCOUNT_CHUNKS_DRAWN, 1);
            ProfAddCounter(PCOUNT_TRIANGLES_DRAWN, c.opaqueIndexCount / 3);
        }
    }
}

// See-through blocks (Section 4.11), after everything opaque: blended,
// depth-tested but not depth-writing, and back faces culled so a glass
// cube shows one layer, not two. Chunks go far to near so nearer glass
// blends over farther glass; within a chunk faces aren't sorted (same-
// kind neighbours share no faces, so overlaps are rare and alike). Only
// chunks that actually hold glass are visited twice.
static void DrawTranslucent(World& w, const Frustum& frustum, Vec3 eye) {
    struct Item { float dist2; Chunk* c; float origin[4]; };
    static std::vector<Item> order; // reused: no per-frame allocation once warm
    order.clear();
    for (auto& kv : w.chunks) {
        Chunk& c = *kv.second;
        if (c.indexCount <= c.opaqueIndexCount) continue;
        const ChunkCoord& cc = kv.first;
        Vec3 minB = { (float)(cc.x * CHUNK_SIZE), (float)(cc.y * CHUNK_SIZE), (float)(cc.z * CHUNK_SIZE) };
        Vec3 maxB = { minB.x + CHUNK_SIZE, minB.y + CHUNK_SIZE, minB.z + CHUNK_SIZE };
        if (!FrustumIntersectsAABB(frustum, minB, maxB)) continue;
        float dx = minB.x + CHUNK_SIZE * 0.5f - eye.x, dy = minB.y + CHUNK_SIZE * 0.5f - eye.y, dz = minB.z + CHUNK_SIZE * 0.5f - eye.z;
        order.push_back({ dx * dx + dy * dy + dz * dz, &c, { minB.x, minB.y, minB.z, 0.0f } });
    }
    if (order.empty()) return;
    std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.dist2 > b.dist2; });
    float blendFactor[4] = { 0, 0, 0, 0 };
    g_context->OMSetBlendState(g_translucentBlend, blendFactor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depthNoWriteState, 0);
    g_context->RSSetState(g_cullBackRaster);
    UINT stride = sizeof(Vertex), offset = 0;
    for (const Item& e : order) {
        Chunk& c = *e.c;
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_chunkCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, e.origin, sizeof(e.origin));
        g_context->Unmap(g_chunkCBuffer, 0);
        g_context->IASetVertexBuffers(0, 1, &c.vb, &stride, &offset);
        g_context->IASetIndexBuffer(c.ib, DXGI_FORMAT_R16_UINT, 0);
        g_context->DrawIndexed(c.indexCount - c.opaqueIndexCount, c.opaqueIndexCount, 0);
        ProfAddCounter(PCOUNT_TRIANGLES_DRAWN, (c.indexCount - c.opaqueIndexCount) / 3);
    }
    g_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depthState, 0);
    g_context->RSSetState(g_rasterState);
}

// ---- Sun shadow map (Section 4.8) ----
// An orthographic depth map from the sun over the area around the
// player. The sun crosses the sky in 50 minutes, so the map is only
// re-rendered when it has actually gone stale: the sun moved a quarter
// of a degree, the player moved a quarter of the covered area, or chunk
// meshes changed. Most frames just reuse it.
static Mat4 g_lightViewProj = {};
static bool g_shadowValid = false;
static Vec3 g_shadowSun = { 0, 1, 0 };
static float g_shadowCenterX = 0, g_shadowCenterZ = 0, g_shadowExtent = 0;
static uint32_t g_shadowMeshVersion = 0;

static void UpdateShadowMap(World& w, Vec3 eye, Vec3 sun) {
    float extent = (float)std::min(std::max((g_loadRadius + 1) * CHUNK_SIZE, 48), 112); // half-width, blocks
    bool stale = !g_shadowValid || extent != g_shadowExtent || g_meshVersion != g_shadowMeshVersion ||
                 Dot(sun, g_shadowSun) < 0.99999f /* ~0.25 degrees */ ||
                 fabsf(eye.x - g_shadowCenterX) > extent * 0.25f || fabsf(eye.z - g_shadowCenterZ) > extent * 0.25f;
    if (!stale) return;

    g_lightViewProj = ShadowLightViewProj(eye, sun, extent, 200.0f, SHADOW_SIZE);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    g_context->PSSetShaderResources(1, 1, &nullSRV); // the map can't be read while it's the target
    g_context->OMSetRenderTargets(0, nullptr, g_shadowDSV);
    g_context->ClearDepthStencilView(g_shadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp = {}; vp.Width = vp.Height = (float)SHADOW_SIZE; vp.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_shadowRaster);
    g_context->OMSetDepthStencilState(g_depthState, 0);
    g_context->VSSetShader(g_shadowVS, nullptr, 0);
    g_context->PSSetShader(nullptr, nullptr, 0); // depth only
    g_context->IASetInputLayout(g_layout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_shadowCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &g_lightViewProj, sizeof(Mat4));
    g_context->Unmap(g_shadowCB, 0);
    ID3D11Buffer* cbs[2] = { g_shadowCB, g_chunkCBuffer };
    g_context->VSSetConstantBuffers(0, 2, cbs);
    DrawChunks(w, ExtractFrustum(g_lightViewProj), false);

    D3D11_VIEWPORT svp = {}; svp.Width = (float)g_screenW; svp.Height = (float)g_screenH; svp.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &svp);
    g_shadowValid = true;
    g_shadowSun = sun;
    g_shadowCenterX = eye.x; g_shadowCenterZ = eye.z;
    g_shadowExtent = extent;
    // The ortho box is tilted toward the sun, so the ground it covers
    // extends past `extent` -- well past it when the sun is low. This
    // square catches nearly every rebuild that could show in the map; one
    // that slips through is picked up at the next sun-angle re-render,
    // at most ~4.5 s later.
    g_shadowAreaX = eye.x; g_shadowAreaZ = eye.z; g_shadowAreaHalf = extent * 2.0f;
    g_shadowMeshVersion = g_meshVersion;
    ProfAddCounter(PCOUNT_SHADOW_RENDERS, 1);
}

static void DrawLineDebug(const Mat4& viewProj, Vec3 player); // below InitD3D, beside its pipeline

// Bloom (Section 4.10): the scene's glow (rgb * alpha) down to quarter
// resolution, then blurred three times, each pass twice as wide as the
// last, for a soft core with a long falloff. Leaves the result in
// g_bloomSRV[0]. Six quarter-res passes plus one downsample: a fixed cost
// set by the window.
// Expects the post pass's VS, samplers and depth state already bound.
static void RenderBloom() {
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_bloomW; vp.Height = (float)g_bloomH; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);
    g_context->PSSetConstantBuffers(0, 1, &g_bloomCB);
    ID3D11ShaderResourceView* none = nullptr;
    auto pass = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, int dst, float sx, float sy) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_bloomCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        float step[4] = { sx, sy, 0, 0 };
        memcpy(mapped.pData, step, sizeof(step));
        g_context->Unmap(g_bloomCB, 0);
        g_context->PSSetShaderResources(0, 1, &none);     // dst may still be bound as the last source
        g_context->OMSetRenderTargets(1, &g_bloomRTV[dst], nullptr);
        g_context->PSSetShader(ps, nullptr, 0);
        g_context->PSSetShaderResources(0, 1, &src);
        g_context->Draw(3, 0);
    };
    float bx = 1.0f / g_bloomW, by = 1.0f / g_bloomH;
    pass(g_bloomDownPS, g_sceneSRV, 0, 1.0f / g_screenW, 1.0f / g_screenH);
    pass(g_bloomBlurPS, g_bloomSRV[0], 1, bx, 0);
    pass(g_bloomBlurPS, g_bloomSRV[1], 0, 0, by);
    pass(g_bloomBlurPS, g_bloomSRV[0], 1, 2 * bx, 0);
    pass(g_bloomBlurPS, g_bloomSRV[1], 0, 0, 2 * by);
    pass(g_bloomBlurPS, g_bloomSRV[0], 1, 4 * bx, 0);
    pass(g_bloomBlurPS, g_bloomSRV[1], 0, 0, 4 * by);
    g_context->PSSetShaderResources(0, 1, &none);
    vp.Width = (float)g_screenW; vp.Height = (float)g_screenH;
    g_context->RSSetViewports(1, &vp);
}

// Keeps the glow-light grid (4.12) around the player: rebuilt when the
// player crosses into another chunk or a block near a light changed --
// a few hundred microseconds and one small upload, a few times a
// minute at most; nothing at all on other frames.
static void UpdateGlowLight(World& w, Vec3 eye) {
    int ox, oy, oz;
    GlowGridOrigin(eye.x, eye.y, eye.z, ox, oy, oz);
    if (g_glowGrid.valid && !g_glowDirty && ox == g_glowGrid.ox && oy == g_glowGrid.oy && oz == g_glowGrid.oz) return;
    BuildGlowGrid(w, ox, oy, oz, g_glowGrid);
    g_glowDirty = false;
    g_glowLit = !g_glowGrid.texels.empty();
    if (g_glowLit && g_glowTex)
        g_context->UpdateSubresource(g_glowTex, 0, nullptr, g_glowGrid.texels.data(), GLOW_GRID * 2, GLOW_GRID * GLOW_GRID * 2);
}

void RenderEmptyScene() {
    float black[4] = { 0, 0, 0, 1 };
    g_context->OMSetRenderTargets(1, &g_rtv, g_dsv);
    g_context->ClearRenderTargetView(g_rtv, black);
    g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void RenderScene(World& w, const Mat4& view, const Mat4& proj, Vec3 eye, Vec3 forward, Vec3 up, float dayTime) {
    SkyState sky = ComputeSky(dayTime);
    bool shadows = g_shadows && g_shadowsAvailable && sky.sunLight > 0.001f;
    if (shadows) { ProfScope prof(PROF_SHADOW); UpdateShadowMap(w, eye, sky.sunDir); }
    if (!g_shadows) g_shadowValid = false; // re-render on re-enable

    // The frame's atmosphere, shared by sky and world (b2).
    {
        Atmosphere atm = ComputeAtmosphere(sky);
        float fogEnd = (float)std::max(2, g_loadRadius) * CHUNK_SIZE; // the loaded world's guaranteed edge
        auto v4 = [](float* d, Vec3 v, float w) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = w; };
        FrameCBData f;
        v4(f.sunDir, sky.sunDir, 0);
        v4(f.sunColor, atm.sunColor, 0);
        v4(f.moonDir, sky.moonDir, 0);
        v4(f.moonColor, atm.moonColor, 0);
        v4(f.zenith, atm.zenith, atm.twilightAmount);
        v4(f.horizon, atm.horizon, 0);
        v4(f.twilight, atm.twilight, 0);
        v4(f.ambientUp, atm.ambientUp, 0);
        v4(f.ambientDown, atm.ambientDown, 0);
        v4(f.camPos, eye, (float)(g_worldTick / 60.0));
        f.fog[0] = fogEnd * 0.5f; f.fog[1] = fogEnd; f.fog[2] = atm.exposure; f.fog[3] = CLOUD_COVER;
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_frameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &f, sizeof(f));
        g_context->Unmap(g_frameCB, 0);
        g_context->VSSetConstantBuffers(2, 1, &g_frameCB);
        g_context->PSSetConstantBuffers(2, 1, &g_frameCB);
    }

    int64_t worldStart = ProfNow();
    UpdateGlowLight(w, eye);
    // With no post effect on, draw straight to the backbuffer: the post
    // path costs nothing at all while it's switched off.
    bool bloom = g_bloom && g_bloomAvailable && g_bloomRTV[0] && g_bloomRTV[1];
    bool post = (g_postEdges || g_postSSAO || bloom) && g_postAvailable;
    ID3D11RenderTargetView* target = post ? g_sceneRTV : g_rtv;
    ID3D11ShaderResourceView* nulls[2] = { nullptr, nullptr };
    g_context->PSSetShaderResources(0, 2, nulls); // last frame's post inputs
    float clearColor[4] = { 0, 0, 0, 1 };
    g_context->OMSetRenderTargets(1, &target, g_dsv);
    g_context->ClearRenderTargetView(target, clearColor);
    g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    g_context->RSSetState(g_rasterState);

    // Sky: depth off, drawn first so the world always overdraws it; its
    // view drops the eye position so it turns with the camera but never
    // translates with it.
    {
        Mat4 skyViewProj = MatMul(MatLookToLH({ 0, 0, 0 }, forward, up), proj);
        float day = (sky.daylight - NIGHT_LIGHT) / (1.0f - NIGHT_LIGHT);
        // Star field: shown = W * R * star, where R is the normal turning
        // about the pole and W The Line's precession; the shader needs the
        // inverse, (W R)^T = R^T W^T.
        float R[3][3], W[3][3], G[3][3];
        AxisAngleMatrix(CelestialPole(), sky.starAngle, R);
        LineSkyWobble(g_line, g_lineTuning, 1.0f, W);
        LineSkyWobble(g_line, g_lineTuning, g_lineTuning.moonGhostScale, G);
        float M[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                M[i][j] = 0;
                for (int k = 0; k < 3; k++) M[i][j] += R[k][i] * W[j][k];
            }
        Vec3 md = sky.moonDir;
        Vec3 ghost = { G[0][0] * md.x + G[0][1] * md.y + G[0][2] * md.z,
                       G[1][0] * md.x + G[1][1] * md.y + G[1][2] * md.z,
                       G[2][0] * md.x + G[2][1] * md.y + G[2][2] * md.z };
        float moonVis = SkySmooth(-0.03f, 0.05f, md.y) * (1.0f - 0.75f * day);
        struct { Mat4 viewProj; float params[4]; float moon[4]; float ghost[4]; float rows[3][4]; } cb = {
            skyViewProj,
            { sky.starsVisible, sky.sunLight, 0, 0 },
            { md.x, md.y, md.z, moonVis },
            { ghost.x, ghost.y, ghost.z, 0.22f * g_line.intensity * moonVis }, // always fainter than the moon
            { { M[0][0], M[0][1], M[0][2], 0 }, { M[1][0], M[1][1], M[1][2], 0 }, { M[2][0], M[2][1], M[2][2], 0 } },
        };
        g_context->OMSetDepthStencilState(g_uiDepthState, 0);
        g_context->VSSetShader(g_skyVS, nullptr, 0);
        g_context->PSSetShader(g_skyPS, nullptr, 0);
        g_context->IASetInputLayout(g_skyLayout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_skyCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &cb, sizeof(cb));
        g_context->Unmap(g_skyCBuffer, 0);
        g_context->VSSetConstantBuffers(0, 1, &g_skyCBuffer);
        g_context->PSSetConstantBuffers(0, 1, &g_skyCBuffer);
        UINT skyStride = sizeof(SkyVertex), skyOffset = 0;
        g_context->IASetVertexBuffers(0, 1, &g_skyVB, &skyStride, &skyOffset);
        g_context->IASetIndexBuffer(g_skyIB, DXGI_FORMAT_R32_UINT, 0);
        g_context->DrawIndexed(g_skyIndexCount, 0, 0);
        g_context->OMSetDepthStencilState(g_depthState, 0);
    }

    // World.
    {
        Mat4 viewProj = MatMul(view, proj);
        CBData cb;
        cb.mvp = viewProj;
        cb.lightViewProj = g_lightViewProj;
        cb.params[0] = shadows ? 1.0f : 0.0f;
        cb.params[1] = 0.5f / SHADOW_SIZE;
        cb.params[2] = 0.0f;
        cb.params[3] = 0.0f;
        Vec3 ld = LineDirection(g_line);
        cb.lineA[0] = g_line.pivotX; cb.lineA[1] = g_line.lineY; cb.lineA[2] = g_line.pivotZ; cb.lineA[3] = g_line.intensity;
        cb.lineB[0] = ld.x; cb.lineB[1] = ld.z; cb.lineB[2] = CurrentMusicLevel(); cb.lineB[3] = 0.0f;
        cb.glowGrid[0] = (float)g_glowGrid.ox; cb.glowGrid[1] = (float)g_glowGrid.oy; cb.glowGrid[2] = (float)g_glowGrid.oz;
        cb.glowGrid[3] = g_glowLit && g_glowSRV ? 1.0f : 0.0f;
        UpdateCBuffer(cb);
        g_context->VSSetShader(g_vs, nullptr, 0);
        g_context->PSSetShader(g_ps, nullptr, 0);
        g_context->IASetInputLayout(g_layout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cbs[2] = { g_cbuffer, g_chunkCBuffer };
        g_context->VSSetConstantBuffers(0, 2, cbs);
        g_context->PSSetConstantBuffers(0, 1, &g_cbuffer);
        ID3D11SamplerState* samplers[3] = { g_sampler, g_shadowSampler, g_glowSampler };
        g_context->PSSetSamplers(0, 3, samplers);
        ID3D11ShaderResourceView* srvs[3] = { g_blockTexSRV, shadows ? g_shadowSRV : nullptr, g_glowSRV };
        g_context->PSSetShaderResources(0, 3, srvs);
        Frustum frustum = ExtractFrustum(viewProj);
        DrawChunks(w, frustum, true);
        if (g_lineDebug) DrawLineDebug(viewProj, eye);
        // See-through blocks last: same shader, told by params.z to shade
        // as glass. Rebind the world pipeline (the debug lines change it).
        cb.params[2] = 1.0f;
        UpdateCBuffer(cb);
        g_context->VSSetShader(g_vs, nullptr, 0);
        g_context->PSSetShader(g_ps, nullptr, 0);
        g_context->IASetInputLayout(g_layout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetConstantBuffers(0, 2, cbs);
        g_context->PSSetConstantBuffers(0, 1, &g_cbuffer);
        DrawTranslucent(w, frustum, eye);
    }
    ProfAdd(PROF_WORLD, ProfNow() - worldStart);

    // Post pass: scene + depth in, backbuffer out.
    if (post) {
        ProfScope prof(PROF_POST);
        g_context->VSSetShader(g_postVS, nullptr, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->OMSetDepthStencilState(g_uiDepthState, 0);
        ID3D11SamplerState* postSamplers[2] = { g_pointClampSampler, g_linearClampSampler };
        g_context->PSSetSamplers(0, 2, postSamplers);
        if (bloom) RenderBloom();
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        struct { float p0[4]; float p1[4]; } cb = {
            { g_postEdges ? 1.0f : 0.0f, g_postSSAO ? 1.0f : 0.0f, 0.1f, 500.0f }, // near/far: main.cpp's projection
            { 1.0f / g_screenW, 1.0f / g_screenH, proj.m[1][1], bloom ? BLOOM_STRENGTH : 0.0f },
        };
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_postCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &cb, sizeof(cb));
        g_context->Unmap(g_postCB, 0);
        g_context->PSSetShader(g_postPS, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_postCB);
        ID3D11ShaderResourceView* srvs[3] = { g_sceneSRV, g_depthSRV, bloom ? g_bloomSRV[0] : nullptr };
        g_context->PSSetShaderResources(0, 3, srvs);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nulls3[3] = { nullptr, nullptr, nullptr };
        g_context->PSSetShaderResources(0, 3, nulls3);
        g_context->OMSetDepthStencilState(g_depthState, 0);
    }
}

// =======================================================================
// View-frustum culling
// =======================================================================
//
// Mat4 here is row-major with the row-vector convention this project
// uses throughout (clip = pos * viewProj, see common.h's MatMul/
// MatLookToLH/MatPerspectiveFovLH) -- clip[j] = x*M[0][j] + y*M[1][j] +
// z*M[2][j] + w*M[3][j], so a plane's (a,b,c,d) coefficients for a given
// clip component come from that COLUMN of M, not its row (the transpose
// of the usual column-vector-convention Gribb/Hartmann derivation).
// Near/far use clip_z directly rather than clip_z +/- clip_w, matching
// MatPerspectiveFovLH's D3D-style [0,1] depth range (not OpenGL's
// [-1,1]) -- using the OpenGL form here would cull most of the view.
Frustum ExtractFrustum(const Mat4& M) {
    Frustum f;
    auto set = [](FrustumPlane& p, float a, float b, float c, float d) { p.a = a; p.b = b; p.c = c; p.d = d; };
    set(f.planes[0], M.m[0][3] + M.m[0][0], M.m[1][3] + M.m[1][0], M.m[2][3] + M.m[2][0], M.m[3][3] + M.m[3][0]); // left
    set(f.planes[1], M.m[0][3] - M.m[0][0], M.m[1][3] - M.m[1][0], M.m[2][3] - M.m[2][0], M.m[3][3] - M.m[3][0]); // right
    set(f.planes[2], M.m[0][3] + M.m[0][1], M.m[1][3] + M.m[1][1], M.m[2][3] + M.m[2][1], M.m[3][3] + M.m[3][1]); // bottom
    set(f.planes[3], M.m[0][3] - M.m[0][1], M.m[1][3] - M.m[1][1], M.m[2][3] - M.m[2][1], M.m[3][3] - M.m[3][1]); // top
    set(f.planes[4], M.m[0][2], M.m[1][2], M.m[2][2], M.m[3][2]);                                                 // near
    set(f.planes[5], M.m[0][3] - M.m[0][2], M.m[1][3] - M.m[1][2], M.m[2][3] - M.m[2][2], M.m[3][3] - M.m[3][2]); // far
    return f;
}

bool FrustumIntersectsAABB(const Frustum& f, Vec3 minB, Vec3 maxB) {
    for (int i = 0; i < 6; i++) {
        const FrustumPlane& p = f.planes[i];
        // The AABB corner furthest along this plane's normal -- if even
        // that corner is outside, the whole box is.
        float px = p.a >= 0 ? maxB.x : minB.x;
        float py = p.b >= 0 ? maxB.y : minB.y;
        float pz = p.c >= 0 ? maxB.z : minB.z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) return false;
    }
    return true;
}

// =======================================================================
// D3D11 initialization
// =======================================================================

// Compiles one entry point; on failure logs the compiler's message and
// returns nullptr, leaving the caller to decide whether that's fatal.
// Every compile failure this run, for shader_errors.txt (a failed optional
// effect would otherwise just quietly not appear).
static std::string g_shaderErrors;

static ID3DBlob* CompileShader(const char* src, const char* entry, const char* profile,
                               const D3D_SHADER_MACRO* macros = nullptr, const char* what = "shader") {
    ID3DBlob* blob = nullptr, * err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, macros, nullptr, entry, profile, 0, 0, &blob, &err);
    if (FAILED(hr)) {
        std::string msg = std::string(what) + " (" + entry + ", " + profile + (macros ? ", " + std::string(macros[0].Name) : std::string()) + "):\n";
        msg += err ? (const char*)err->GetBufferPointer() : "no compiler output\n";
        g_shaderErrors += msg + "\n";
        OutputDebugStringA(("Shader compile failed: " + msg).c_str());
        if (err) err->Release();
        if (blob) blob->Release();
        return nullptr;
    }
    if (err) err->Release();
    return blob;
}

bool ShadowsAvailable() { return g_shadowsAvailable; }
bool PostEffectsAvailable() { return g_postAvailable; }
bool BloomAvailable() { return g_bloomAvailable; }
const std::string& ShaderErrors() { return g_shaderErrors; }

// The backbuffer's render target, the depth buffer and the viewport --
// everything whose size is the window's. Recreated on every resize.
static void CreateSizeDependentTargets() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();

    // Depth is typeless so the post pass can also read it.
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_screenW; depthDesc.Height = g_screenH;
    depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* depthTex = nullptr;
    g_device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
    dsvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(depthTex, &dsvd, &g_dsv);
    D3D11_SHADER_RESOURCE_VIEW_DESC dsrv = {};
    dsrv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    dsrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    dsrv.Texture2D.MipLevels = 1;
    g_device->CreateShaderResourceView(depthTex, &dsrv, &g_depthSRV);
    depthTex->Release();

    // Off-screen colour target the post pass reads.
    D3D11_TEXTURE2D_DESC sceneDesc = {};
    sceneDesc.Width = g_screenW; sceneDesc.Height = g_screenH;
    sceneDesc.MipLevels = 1; sceneDesc.ArraySize = 1;
    sceneDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sceneDesc.SampleDesc.Count = 1;
    sceneDesc.Usage = D3D11_USAGE_DEFAULT;
    sceneDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* sceneTex = nullptr;
    g_device->CreateTexture2D(&sceneDesc, nullptr, &sceneTex);
    g_device->CreateRenderTargetView(sceneTex, nullptr, &g_sceneRTV);
    g_device->CreateShaderResourceView(sceneTex, nullptr, &g_sceneSRV);
    sceneTex->Release();

    // Bloom's quarter-resolution ping-pong pair.
    g_bloomW = std::max(1, g_screenW / 4); g_bloomH = std::max(1, g_screenH / 4);
    D3D11_TEXTURE2D_DESC bloomDesc = sceneDesc;
    bloomDesc.Width = g_bloomW; bloomDesc.Height = g_bloomH;
    bloomDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT; // float: three blur passes over dim halos would band in 8 bits
    for (int i = 0; i < 2; i++) {
        ID3D11Texture2D* bt = nullptr;
        if (FAILED(g_device->CreateTexture2D(&bloomDesc, nullptr, &bt))) continue;
        g_device->CreateRenderTargetView(bt, nullptr, &g_bloomRTV[i]);
        g_device->CreateShaderResourceView(bt, nullptr, &g_bloomSRV[i]);
        bt->Release();
    }

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_screenW; vp.Height = (float)g_screenH;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);
}

void ResizeRenderTargets(int w, int h) {
    if (w <= 0 || h <= 0) return;           // minimised: keep the old size
    if (!g_swapChain) { g_screenW = w; g_screenH = h; return; } // before InitD3D
    if (w == g_screenW && h == g_screenH) return;
    g_screenW = w; g_screenH = h;
    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; }
    if (g_depthSRV) { g_depthSRV->Release(); g_depthSRV = nullptr; }
    if (g_sceneRTV) { g_sceneRTV->Release(); g_sceneRTV = nullptr; }
    if (g_sceneSRV) { g_sceneSRV->Release(); g_sceneSRV = nullptr; }
    for (int i = 0; i < 2; i++) {
        if (g_bloomRTV[i]) { g_bloomRTV[i]->Release(); g_bloomRTV[i] = nullptr; }
        if (g_bloomSRV[i]) { g_bloomSRV[i]->Release(); g_bloomSRV[i] = nullptr; }
    }
    g_swapChain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    CreateSizeDependentTargets();
}

bool InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = g_screenW;
    scd.BufferDesc.Height = g_screenH;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL chosen;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &scd, &g_swapChain, &g_device, &chosen, &g_context);
    if (FAILED(hr)) return false;

    // DXGI's own Alt+Enter would flip into exclusive fullscreen behind the
    // game's back; fullscreen is borderless, via F11 / Display settings.
    {
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(g_swapChain->GetParent(__uuidof(IDXGIFactory), (void**)&factory)) && factory) {
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            factory->Release();
        }
    }

    CreateSizeDependentTargets();

    ID3DBlob* errBlob = nullptr;
    // The sky and world shaders share the atmosphere block (fog must match
    // the sky exactly), prepended at compile time.
    const std::string worldSrc = std::string(g_atmosphereSrc) + g_shaderSrc;
    const std::string skySrc = std::string(g_atmosphereSrc) + g_skyShaderSrc;
    ID3DBlob* vsBlob = CompileShader(worldSrc.c_str(), "VSMain", "vs_4_0", nullptr, "world");
    ID3DBlob* psBlob = CompileShader(worldSrc.c_str(), "PSMain", "ps_4_0", nullptr, "world");
    bool worldShadows = psBlob != nullptr;
    if (!psBlob) {
        const D3D_SHADER_MACRO noShadows[] = { { "NO_SHADOWS", "1" }, { nullptr, nullptr } };
        psBlob = CompileShader(worldSrc.c_str(), "PSMain", "ps_4_0", noShadows, "world");
    }
    if (!vsBlob || !psBlob) return false;
    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = { // mesher.h's packed Vertex
        { "POSITION", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // x, y, z, aoFace
        { "TEXCOORD", 0, DXGI_FORMAT_R16_UINT, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0 },      // layer
        { "TEXCOORD", 1, DXGI_FORMAT_R8G8_UINT, 0, 6, D3D11_INPUT_PER_VERTEX_DATA, 0 },     // u, v
    };
    g_device->CreateInputLayout(layoutDesc, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_layout);
    vsBlob->Release();
    psBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(CBData);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&cbd, nullptr, &g_cbuffer);
    cbd.ByteWidth = 16; // float4 chunk origin
    g_device->CreateBuffer(&cbd, nullptr, &g_chunkCBuffer);
    cbd.ByteWidth = sizeof(FrameCBData);
    g_device->CreateBuffer(&cbd, nullptr, &g_frameCB);

    // Point-sampled up close (crisp pixel art), blended between mip levels
    // so distant blocks don't shimmer. Wrap addressing: each block face
    // is its own whole texture layer, so there's no neighbour to bleed
    // into, and merged quads can repeat a texture across blocks.
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_device->CreateSamplerState(&sampDesc, &g_sampler);

    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = TRUE;
    g_device->CreateRasterizerState(&rastDesc, &g_rasterState);

    D3D11_DEPTH_STENCIL_DESC depthStateDesc = {};
    depthStateDesc.DepthEnable = TRUE;
    depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    g_device->CreateDepthStencilState(&depthStateDesc, &g_depthState);

    // Glow-light grid (4.12): filled on the CPU, sampled trilinearly so
    // one-block steps become soft light and soft shadow edges. Optional.
    {
        D3D11_TEXTURE3D_DESC gd = {};
        gd.Width = gd.Height = gd.Depth = GLOW_GRID;
        gd.MipLevels = 1;
        gd.Format = DXGI_FORMAT_R8G8_UNORM;
        gd.Usage = D3D11_USAGE_DEFAULT;
        gd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_device->CreateTexture3D(&gd, nullptr, &g_glowTex)))
            g_device->CreateShaderResourceView(g_glowTex, nullptr, &g_glowSRV);
        D3D11_SAMPLER_DESC gs = {};
        gs.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        gs.AddressU = gs.AddressV = gs.AddressW = D3D11_TEXTURE_ADDRESS_BORDER; // outside the grid: no glow light
        gs.MaxLOD = D3D11_FLOAT32_MAX;
        g_device->CreateSamplerState(&gs, &g_glowSampler);
    }

    // See-through blocks (4.11): blended over the opaque world, depth
    // tested but not written, back faces culled. Destination alpha (the
    // bloom glow mask) is left as the opaque world wrote it.
    depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    g_device->CreateDepthStencilState(&depthStateDesc, &g_depthNoWriteState);
    D3D11_BLEND_DESC tb = {};
    tb.RenderTarget[0].BlendEnable = TRUE;
    tb.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    tb.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    tb.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    tb.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    tb.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    tb.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    tb.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&tb, &g_translucentBlend);
    rastDesc.CullMode = D3D11_CULL_BACK; // mesher.cpp winds every cube face clockwise seen from outside (tested)
    g_device->CreateRasterizerState(&rastDesc, &g_cullBackRaster);

    // --- UI pass pipeline objects (Section 4.6: a second pass, its own
    // shaders, orthographic-in-pixel-space, depth off, alpha blend on) ---
    ID3DBlob* uiVsBlob = nullptr, * uiPsBlob = nullptr;
    hr = D3DCompile(g_uiShaderSrc, strlen(g_uiShaderSrc), nullptr, nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &uiVsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(g_uiShaderSrc, strlen(g_uiShaderSrc), nullptr, nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &uiPsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    g_device->CreateVertexShader(uiVsBlob->GetBufferPointer(), uiVsBlob->GetBufferSize(), nullptr, &g_uiVS);
    g_device->CreatePixelShader(uiPsBlob->GetBufferPointer(), uiPsBlob->GetBufferSize(), nullptr, &g_uiPS);

    D3D11_INPUT_ELEMENT_DESC uiLayoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(uiLayoutDesc, 3, uiVsBlob->GetBufferPointer(), uiVsBlob->GetBufferSize(), &g_uiLayout);
    uiVsBlob->Release();
    uiPsBlob->Release();

    D3D11_BUFFER_DESC uiCbd = {};
    uiCbd.Usage = D3D11_USAGE_DYNAMIC;
    uiCbd.ByteWidth = sizeof(float) * 4; // screenSize.xy, padding
    uiCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    uiCbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&uiCbd, nullptr, &g_uiCBuffer);

    D3D11_BUFFER_DESC uiVbd = {};
    uiVbd.Usage = D3D11_USAGE_DYNAMIC;
    uiVbd.ByteWidth = UI_VB_CAPACITY * sizeof(UIVertex);
    uiVbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    uiVbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&uiVbd, nullptr, &g_uiVB);

    // Point-sampled: UI text and icons are drawn at whole-texel ratios on
    // whole pixels (see UIDrawText), where linear filtering only blurs.
    D3D11_SAMPLER_DESC uiSampDesc = {};
    uiSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    uiSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    uiSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    uiSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    uiSampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_device->CreateSamplerState(&uiSampDesc, &g_uiSampler);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&blendDesc, &g_uiBlendState);

    D3D11_DEPTH_STENCIL_DESC uiDepthDesc = {};
    uiDepthDesc.DepthEnable = FALSE;
    uiDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    g_device->CreateDepthStencilState(&uiDepthDesc, &g_uiDepthState);

    // --- Sky pass pipeline objects: depth off (g_uiDepthState is reused
    // here -- it's the same DepthEnable=FALSE state the UI pass already
    // needed, no reason to create a second identical one) ---
    ID3DBlob* skyVsBlob = CompileShader(skySrc.c_str(), "VSMain", "vs_4_0", nullptr, "sky");
    ID3DBlob* skyPsBlob = CompileShader(skySrc.c_str(), "PSMain", "ps_4_0", nullptr, "sky");
    if (!skyVsBlob || !skyPsBlob) {
        if (skyVsBlob) skyVsBlob->Release();
        if (skyPsBlob) skyPsBlob->Release();
        return false;
    }
    g_device->CreateVertexShader(skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), nullptr, &g_skyVS);
    g_device->CreatePixelShader(skyPsBlob->GetBufferPointer(), skyPsBlob->GetBufferSize(), nullptr, &g_skyPS);

    D3D11_INPUT_ELEMENT_DESC skyLayoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(skyLayoutDesc, 1, skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), &g_skyLayout);
    skyVsBlob->Release();
    skyPsBlob->Release();

    D3D11_BUFFER_DESC skyCbd = {};
    skyCbd.Usage = D3D11_USAGE_DYNAMIC;
    skyCbd.ByteWidth = sizeof(Mat4) + 6 * 4 * sizeof(float); // viewProj + 6 float4s (SkyCB)
    skyCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    skyCbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&skyCbd, nullptr, &g_skyCBuffer);

    // --- Sun shadows (Section 4.8). Optional: any failure here just
    // leaves shadows unavailable (the Graphics toggle then does nothing).
    if (worldShadows) {
        ID3DBlob* sv = CompileShader(g_shadowShaderSrc, "VSMain", "vs_4_0", nullptr, "shadow map");
        if (sv) {
            g_device->CreateVertexShader(sv->GetBufferPointer(), sv->GetBufferSize(), nullptr, &g_shadowVS);
            sv->Release();
        }
        D3D11_TEXTURE2D_DESC sd = {};
        sd.Width = SHADOW_SIZE; sd.Height = SHADOW_SIZE; sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Format = DXGI_FORMAT_R32_TYPELESS; sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_DEFAULT; sd.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* st = nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&sd, nullptr, &st))) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dd = {}; dd.Format = DXGI_FORMAT_D32_FLOAT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            g_device->CreateDepthStencilView(st, &dd, &g_shadowDSV);
            D3D11_SHADER_RESOURCE_VIEW_DESC rd = {}; rd.Format = DXGI_FORMAT_R32_FLOAT; rd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; rd.Texture2D.MipLevels = 1;
            g_device->CreateShaderResourceView(st, &rd, &g_shadowSRV);
            st->Release();
        }
        D3D11_SAMPLER_DESC cs = {};
        cs.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; // hardware 2x2 PCF per tap
        cs.AddressU = cs.AddressV = cs.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        cs.BorderColor[0] = cs.BorderColor[1] = cs.BorderColor[2] = cs.BorderColor[3] = 1.0f; // outside = lit
        cs.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        cs.MaxLOD = D3D11_FLOAT32_MAX;
        g_device->CreateSamplerState(&cs, &g_shadowSampler);
        D3D11_RASTERIZER_DESC rs = {};
        rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE; rs.DepthClipEnable = TRUE;
        rs.DepthBias = 40; rs.SlopeScaledDepthBias = 1.5f; rs.DepthBiasClamp = 0.0f;
        g_device->CreateRasterizerState(&rs, &g_shadowRaster);
        D3D11_BUFFER_DESC sb = {};
        sb.Usage = D3D11_USAGE_DYNAMIC; sb.ByteWidth = sizeof(Mat4);
        sb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; sb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_device->CreateBuffer(&sb, nullptr, &g_shadowCB);
        g_shadowsAvailable = g_shadowVS && g_shadowDSV && g_shadowSRV && g_shadowSampler && g_shadowRaster && g_shadowCB;
    }

    // --- Post pass (Section 4.8): outlines and SSAO. Optional too.
    {
        ID3DBlob* pv = CompileShader(g_postShaderSrc, "VSMain", "vs_4_0", nullptr, "post");
        ID3DBlob* pp = CompileShader(g_postShaderSrc, "PSMain", "ps_4_0", nullptr, "post");
        if (pv) { g_device->CreateVertexShader(pv->GetBufferPointer(), pv->GetBufferSize(), nullptr, &g_postVS); pv->Release(); }
        if (pp) { g_device->CreatePixelShader(pp->GetBufferPointer(), pp->GetBufferSize(), nullptr, &g_postPS); pp->Release(); }
        D3D11_BUFFER_DESC pb = {};
        pb.Usage = D3D11_USAGE_DYNAMIC; pb.ByteWidth = 2 * 4 * sizeof(float);
        pb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; pb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_device->CreateBuffer(&pb, nullptr, &g_postCB);
        D3D11_SAMPLER_DESC ps = {};
        ps.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        ps.AddressU = ps.AddressV = ps.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ps.MaxLOD = D3D11_FLOAT32_MAX;
        g_device->CreateSamplerState(&ps, &g_pointClampSampler);
        ps.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        g_device->CreateSamplerState(&ps, &g_linearClampSampler);
        g_postAvailable = g_postVS && g_postPS && g_postCB && g_pointClampSampler && g_linearClampSampler;

        // Bloom (Section 4.10) rides on the post pass.
        ID3DBlob* bd = CompileShader(g_bloomDownShaderSrc, "PSMain", "ps_4_0", nullptr, "bloom downsample");
        ID3DBlob* bb = CompileShader(g_bloomBlurShaderSrc, "PSMain", "ps_4_0", nullptr, "bloom blur");
        if (bd) { g_device->CreatePixelShader(bd->GetBufferPointer(), bd->GetBufferSize(), nullptr, &g_bloomDownPS); bd->Release(); }
        if (bb) { g_device->CreatePixelShader(bb->GetBufferPointer(), bb->GetBufferSize(), nullptr, &g_bloomBlurPS); bb->Release(); }
        pb.ByteWidth = 4 * sizeof(float);
        g_device->CreateBuffer(&pb, nullptr, &g_bloomCB);
        g_bloomAvailable = g_postAvailable && g_bloomDownPS && g_bloomBlurPS && g_bloomCB;
    }

    // --- Debug line pipeline (optional).
    {
        ID3DBlob* dv = CompileShader(g_debugShaderSrc, "VSMain", "vs_4_0", nullptr, "debug lines");
        ID3DBlob* dp = CompileShader(g_debugShaderSrc, "PSMain", "ps_4_0", nullptr, "debug lines");
        if (dv && dp) {
            g_device->CreateVertexShader(dv->GetBufferPointer(), dv->GetBufferSize(), nullptr, &g_debugVS);
            g_device->CreatePixelShader(dp->GetBufferPointer(), dp->GetBufferSize(), nullptr, &g_debugPS);
            D3D11_INPUT_ELEMENT_DESC dl[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            g_device->CreateInputLayout(dl, 2, dv->GetBufferPointer(), dv->GetBufferSize(), &g_debugLayout);
            D3D11_BUFFER_DESC vb = {};
            vb.Usage = D3D11_USAGE_DYNAMIC; vb.ByteWidth = DEBUG_VB_CAPACITY * sizeof(DebugVertex);
            vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            g_device->CreateBuffer(&vb, nullptr, &g_debugVB);
            D3D11_BUFFER_DESC cb = {};
            cb.Usage = D3D11_USAGE_DYNAMIC; cb.ByteWidth = sizeof(Mat4);
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            g_device->CreateBuffer(&cb, nullptr, &g_debugCB);
        }
        if (dv) dv->Release();
        if (dp) dp->Release();
    }

    // Any effect that failed to compile on this machine is written up
    // beside the working directory (and the menu shows it as unavailable)
    // rather than silently not appearing; a stale report is removed.
    std::error_code ec;
    if (g_shaderErrors.empty()) std::filesystem::remove("shader_errors.txt", ec);
    else {
        std::ofstream f("shader_errors.txt", std::ios::trunc);
        f << "Voxistics: these shaders failed to compile on this machine. The effects that need them are switched off.\n\n"
          << g_shaderErrors;
    }
    return true;
}

// The Line's debug marker: the line across the loaded area at its height
// (cyan = counterclockwise, orange = clockwise), short strokes showing
// which way it's sweeping, and a white pole at the pivot.
static void DrawLineDebug(const Mat4& viewProj, Vec3 player) {
    if (!g_debugVS || !g_debugPS || !g_debugLayout || !g_debugVB || !g_debugCB) return;
    const LineState& L = g_line;
    Vec3 u = LineDirection(L);
    float halfLen = (float)((g_loadRadius + 1) * CHUNK_SIZE);
    float along = (player.x - L.pivotX) * u.x + (player.z - L.pivotZ) * u.z;
    float cx = L.pivotX + u.x * along, cz = L.pivotZ + u.z * along, y = L.lineY;
    float r = L.spin > 0 ? 0.3f : 1.0f, g = L.spin > 0 ? 0.9f : 0.6f, b = L.spin > 0 ? 1.0f : 0.2f;
    DebugVertex v[DEBUG_VB_CAPACITY];
    UINT n = 0;
    auto seg = [&](float x0, float y0, float z0, float x1, float y1, float z1, float cr, float cg, float cb) {
        if (n + 2 > DEBUG_VB_CAPACITY) return;
        v[n++] = { x0, y0, z0, cr, cg, cb, 1 };
        v[n++] = { x1, y1, z1, cr, cg, cb, 1 };
    };
    seg(cx - u.x * halfLen, y, cz - u.z * halfLen, cx + u.x * halfLen, y, cz + u.z * halfLen, r, g, b);
    // Sweep strokes every 8 blocks: the line moves perpendicular to itself,
    // opposite ways on either side of the pivot.
    for (float t = -halfLen; t <= halfLen; t += 8.0f) {
        float px = cx + u.x * t, pz = cz + u.z * t;
        float side = ((px - L.pivotX) * u.x + (pz - L.pivotZ) * u.z) >= 0 ? 1.0f : -1.0f;
        float nx = -u.z * L.spin * side, nz = u.x * L.spin * side;
        seg(px, y, pz, px + nx * 1.2f, y, pz + nz * 1.2f, r, g, b);
    }
    seg(L.pivotX, y - 3.0f, L.pivotZ, L.pivotX, y + 12.0f, L.pivotZ, 1, 1, 1);

    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_debugVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, v, n * sizeof(DebugVertex));
    g_context->Unmap(g_debugVB, 0);
    g_context->Map(g_debugCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &viewProj, sizeof(Mat4));
    g_context->Unmap(g_debugCB, 0);
    g_context->VSSetShader(g_debugVS, nullptr, 0);
    g_context->PSSetShader(g_debugPS, nullptr, 0);
    g_context->IASetInputLayout(g_debugLayout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    g_context->VSSetConstantBuffers(0, 1, &g_debugCB);
    UINT stride = sizeof(DebugVertex), offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_debugVB, &stride, &offset);
    g_context->Draw(n, 0);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// assets/textures, looked for next to the working directory first (a
// Visual Studio run starts in the project folder), then beside the exe
// and up to three folders above it (bin/Debug layouts).
static std::filesystem::path FindTextureDirectory() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> roots = { fs::current_path(ec) };
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        fs::path dir = fs::path(exe).parent_path();
        for (int i = 0; i < 4 && !dir.empty(); i++) { roots.push_back(dir); dir = dir.parent_path(); }
    }
    for (const fs::path& r : roots) {
        fs::path candidate = r / "assets" / "textures";
        if (fs::is_directory(candidate, ec)) return candidate;
    }
    return {};
}

// Parses every .vtex file (sorted by name, so a later file's duplicate
// wins predictably), writes any problems to _errors.txt beside them, and
// removes a stale _errors.txt when there are none.
static void LoadAuthoredTextures(VtexSet& set, std::vector<std::string>& problems) {
    namespace fs = std::filesystem;
    fs::path dir = FindTextureDirectory();
    if (dir.empty()) return;
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".vtex") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        ParseVtex(ss.str(), f.filename().string(), set);
    }
    problems.insert(problems.end(), set.errors.begin(), set.errors.end());
}

static void WriteTextureProblems(const std::vector<std::string>& problems) {
    namespace fs = std::filesystem;
    fs::path dir = FindTextureDirectory();
    if (dir.empty()) return;
    std::error_code ec;
    fs::path log = dir / "_errors.txt";
    if (problems.empty()) { fs::remove(log, ec); return; }
    std::ofstream out(log, std::ios::trunc);
    out << "Texture problems found at startup (the rest loaded normally):\n\n";
    for (const std::string& p : problems) out << p << "\n";
}

bool InitTextures(std::string& problemSummary) {
    VtexSet authored;
    std::vector<std::string> problems;
    LoadAuthoredTextures(authored, problems);
    BlockTextureSet set;
    BuildBlockTextures(authored, set);
    problems.insert(problems.end(), set.warnings.begin(), set.warnings.end());
    WriteTextureProblems(problems);
    if (!problems.empty())
        problemSummary = std::to_string(problems.size()) + " TEXTURE PROBLEM" + (problems.size() == 1 ? "" : "S") +
                         " - SEE ASSETS\\TEXTURES\\_ERRORS.TXT";
    memcpy(g_blockFaceLayer, set.faceLayer, sizeof(g_blockFaceLayer));
    RenderBlockIcons(set); // hotbar icons from the real meshes (needs the face layers above)

    // Block faces: one Texture2DArray, a layer per distinct face texture,
    // full mip chain uploaded from the CPU-built mips.
    // sRGB: textures are authored in display colour; the sampler hands the
    // shader linear values, so lighting maths (and mip filtering) happen in
    // linear light.
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = BLOCK_TEX_SIZE; td.Height = BLOCK_TEX_SIZE;
    td.MipLevels = set.mipCount; td.ArraySize = set.layerCount;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init((size_t)set.layerCount * set.mipCount);
    for (int L = 0; L < set.layerCount; L++)
        for (int m = 0; m < set.mipCount; m++) {
            int sz = BLOCK_TEX_SIZE >> m;
            D3D11_SUBRESOURCE_DATA& sd = init[(size_t)L * set.mipCount + m]; // D3D11CalcSubresource order
            sd.pSysMem = set.mips[m].data() + (size_t)L * sz * sz * 4;
            sd.SysMemPitch = sz * 4;
        }
    ID3D11Texture2D* blockTex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&td, init.data(), &blockTex))) return false;
    g_device->CreateShaderResourceView(blockTex, nullptr, &g_blockTexSRV);
    blockTex->Release();

    D3D11_TEXTURE2D_DESC itd = {};
    itd.Width = set.iconsW; itd.Height = set.iconsH;
    itd.MipLevels = 1; itd.ArraySize = 1;
    itd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    itd.SampleDesc.Count = 1;
    itd.Usage = D3D11_USAGE_IMMUTABLE;
    itd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA isd = {};
    isd.pSysMem = set.icons.data();
    isd.SysMemPitch = set.iconsW * 4;
    ID3D11Texture2D* iconTex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&itd, &isd, &iconTex))) return false;
    g_device->CreateShaderResourceView(iconTex, nullptr, &g_iconSRV);
    iconTex->Release();

    int bandW[UI_FONT_BAND_COUNT], bandH[UI_FONT_BAND_COUNT], bandY[UI_FONT_BAND_COUNT];
    float bandPx[UI_FONT_BAND_COUNT];
    for (int i = 0; i < UI_FONT_BAND_COUNT; i++) {
        UIFontBand b = UIGetFontBand(i);
        bandW[i] = b.cellW; bandH[i] = b.cellH; bandY[i] = b.atlasY; bandPx[i] = b.fontPx;
    }
    uint8_t* uiPixels = nullptr;
    int uiW = UIAtlasWidth(), uiH = UIAtlasHeight();
    if (!GenerateUIAtlas(uiW, uiH, UI_WHITE_H, UI_ATLAS_COLS, UI_FONT_BAND_COUNT,
                         bandW, bandH, bandY, bandPx, &uiPixels))
        return false;

    D3D11_TEXTURE2D_DESC td3 = {};
    td3.Width = uiW; td3.Height = uiH;
    td3.MipLevels = 1; td3.ArraySize = 1;
    td3.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td3.SampleDesc.Count = 1;
    td3.Usage = D3D11_USAGE_IMMUTABLE;
    td3.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd3 = {};
    sd3.pSysMem = uiPixels;
    sd3.SysMemPitch = uiW * 4;
    ID3D11Texture2D* uiTex = nullptr;
    g_device->CreateTexture2D(&td3, &sd3, &uiTex);
    g_device->CreateShaderResourceView(uiTex, nullptr, &g_uiSRV);
    uiTex->Release();

    FreeGeneratedPixels(uiPixels);
    return true;
}
