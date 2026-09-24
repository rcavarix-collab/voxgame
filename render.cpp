// render.cpp
//
// D3D11 setup, chunk meshing, and the procedural sky mesh.

#include "render.h"
#include "profiler.h"
#include "blocktex.h"
#include "icons.h"
#include "sky.h"
#include "theline.h"
#include "persist.h"
#include "vtex.h"
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
// Debug lines.
struct DebugVertex { float x, y, z, r, g, b, a; };
static const UINT DEBUG_VB_CAPACITY = 128;
static ID3D11VertexShader* g_debugVS = nullptr;
static ID3D11PixelShader* g_debugPS = nullptr;
static ID3D11InputLayout* g_debugLayout = nullptr;
static ID3D11Buffer* g_debugVB = nullptr;
static ID3D11Buffer* g_debugCB = nullptr;
// Bumped on every chunk mesh rebuild: the shadow map re-renders when the
// geometry it was drawn from has changed.
static uint32_t g_meshVersion = 0;
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

// World pass shader (Section 4.2). Vertices arrive packed (mesher.h):
// chunk-local position plus a per-draw chunk origin, a texture-array
// layer, and bits for u/v, ambient occlusion and shade class. Lighting is
// a fixed brightness per face direction times an AO darkening (both
// decided at mesh time), times the day/night level, times -- when
// shadows are on -- a shadow-map test toward the sun. Compiled once
// as-is and, should that fail on some driver, again with NO_SHADOWS
// (the pre-shadow shader), so a shadow problem can never cost the world.
static const char* g_shaderSrc =
    "cbuffer CB : register(b0) { row_major matrix mvp; row_major matrix lightViewProj; float4 sun; float4 params; };\n"
    // sun.xyz: toward the sun. params: x shadows on, y daylight, z sun strength, w shadow half-texel
    "cbuffer ChunkCB : register(b1) { float4 chunkOrigin; };\n"
    "struct VSIn { uint4 pos:POSITION; uint layer:TEXCOORD0; uint2 uv:TEXCOORD1; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 uvl:TEXCOORD0; float light:TEXCOORD1; float3 wpos:TEXCOORD2; float sunFacing:TEXCOORD3; };\n"
    // +X -X +Y -Y +Z -Z: top brightest, bottom darkest, X and Z sides
    // distinct so edges between two side faces still read; then slopes
    // facing up (ramps, pyramids) and down (funnels).
    "static const float faceShade[8] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.68f, 0.68f, 0.90f, 0.62f };\n"
    "static const float3 faceNormal[8] = { float3(1,0,0), float3(-1,0,0), float3(0,1,0), float3(0,-1,0),\n"
    "                                      float3(0,0,1), float3(0,0,-1), float3(0,1,0), float3(0,-1,0) };\n"
    "static const float aoCurve[4] = { 0.50f, 0.66f, 0.83f, 1.00f };\n"
    "PSIn VSMain(VSIn i) {\n"
    "    PSIn o;\n"
    "    float3 p = float3(i.pos.xyz) * 0.125f + chunkOrigin.xyz;\n"   // 1/8-block fixed point
    "    o.pos = mul(float4(p, 1.0f), mvp);\n"
    "    o.uvl = float3(float2(i.uv) * 0.125f, (float)i.layer);\n"
    "    uint face = (i.pos.w >> 2) & 7u;\n"
    "    o.light = faceShade[face] * aoCurve[i.pos.w & 3u];\n"
    "    float3 n = faceNormal[face];\n"
    "    o.wpos = p + n * 0.08f;\n"                                   // normal offset (> 1 shadow texel): no acne
    "    o.sunFacing = saturate(dot(n, sun.xyz) * 4.0f);\n"
    "    return o;\n"
    "}\n"
    "Texture2DArray tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "#ifndef NO_SHADOWS\n"
    "Texture2D<float> shadowMap : register(t1);\n"
    "SamplerComparisonState shadowSamp : register(s1);\n"
    "#endif\n"
    "float4 PSMain(PSIn i) : SV_TARGET {\n"
    "    float4 c = tex0.Sample(samp0, i.uvl);\n"
    "    float light = i.light * params.y;\n"
    "#ifndef NO_SHADOWS\n"
    "    if (params.x > 0.5f) {\n"
    "        float4 lp = mul(float4(i.wpos, 1.0f), lightViewProj);\n"
    "        float2 suv = float2(lp.x * 0.5f + 0.5f, 0.5f - lp.y * 0.5f);\n"
    "        float lit = 1.0f;\n"
    "        if (suv.x > 0.0f && suv.x < 1.0f && suv.y > 0.0f && suv.y < 1.0f && lp.z < 1.0f) {\n"
    "            float o = params.w;\n"                               // 2x2 taps of hardware PCF
    "            lit = 0.25f * (shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2(-o, -o), lp.z)\n"
    "                         + shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2( o, -o), lp.z)\n"
    "                         + shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2(-o,  o), lp.z)\n"
    "                         + shadowMap.SampleCmpLevelZero(shadowSamp, suv + float2( o,  o), lp.z));\n"
    "        }\n"
    "        lit *= i.sunFacing;\n"
    "        light *= lerp(1.0f - 0.4f * params.z, 1.0f, lit);\n"
    "    }\n"
    "#endif\n"
    "    return float4(c.rgb * light, 1.0f);\n"
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
    // p0: x outlines on, y SSAO on, z near plane, w far plane; p1: x 1/width, y 1/height, z proj[1][1], w unused
    "Texture2D sceneTex : register(t0);\n"
    "Texture2D<float> depthTex : register(t1);\n"
    "SamplerState pointSamp : register(s0);\n"
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
    "float4 PSMain(VSOut i) : SV_TARGET {\n"
    "    float3 c = sceneTex.SampleLevel(pointSamp, i.uv, 0).rgb;\n"
    "    float d = LinDepth(i.uv);\n"
    "    if (d > p0.w * 0.98f) return float4(c, 1.0f);\n"          // sky: nothing to shade
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
    "cbuffer SkyCB : register(b0) {\n"
    "    row_major matrix viewProj;\n"
    "    float4 sun;       // xyz toward the sun\n"
    "    float4 params;    // x day amount 0..1, y stars visible, z direct sun\n"
    "    float4 moon;      // xyz toward the moon, w visibility\n"
    "    float4 ghostMoon; // xyz toward The Line's ghost moon, w strength\n"
    "    float4 starRow0; float4 starRow1; float4 starRow2; // sky direction -> star-field direction\n"
    "};\n"
    "struct VSIn { float3 pos:POSITION; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 dir:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn input) { PSIn o; o.pos = mul(float4(input.pos,1.0f), viewProj); o.dir = input.pos; return o; }\n"
    "static const float3 DAY_ZENITH = float3(0.25f, 0.45f, 0.85f);\n"
    "static const float3 DAY_HORIZON = float3(0.65f, 0.75f, 0.95f);\n"
    "static const float3 NIGHT_ZENITH = float3(0.012f, 0.018f, 0.045f);\n"
    "static const float3 NIGHT_HORIZON = float3(0.045f, 0.055f, 0.10f);\n"
    "static const float3 SUNSET = float3(1.0f, 0.52f, 0.25f);\n"
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
    "float Disc(float3 d, float3 c, float cosR) { return smoothstep(cosR - 0.00008f, cosR + 0.00002f, dot(d, c)); }\n"
    "float4 PSMain(PSIn input) : SV_TARGET {\n"
    "    float3 d = normalize(input.dir);\n"
    "    float h = saturate(d.y);\n"
    "    float day = params.x;\n"
    "    float3 col = lerp(lerp(NIGHT_HORIZON, DAY_HORIZON, day), lerp(NIGHT_ZENITH, DAY_ZENITH, day), h);\n"
    "    float above = smoothstep(-0.04f, 0.04f, d.y);\n"
    // Stars, fading in as the sun goes down.
    "    float3 s = float3(dot(starRow0.xyz, d), dot(starRow1.xyz, d), dot(starRow2.xyz, d));\n"
    "    col += Stars(s) * params.y * above * float3(0.95f, 0.97f, 1.0f);\n"
    // Moon, and The Line's faint ghost of it (a soft double exposure).
    "    float3 moonCol = float3(0.86f, 0.88f, 0.95f);\n"
    "    col = lerp(col, moonCol, Disc(d, moon.xyz, 0.99966f) * moon.w * above);\n"
    "    col += moonCol * Disc(d, ghostMoon.xyz, 0.99966f) * ghostMoon.w * above;\n"
    // Sun: warm the sky around it while low, then its disc and glow.
    "    float toward = saturate(dot(d, sun.xyz));\n"
    "    float low = saturate(1.0f - abs(sun.y) * 4.0f);\n"
    "    col = lerp(col, SUNSET, low * pow(toward, 6.0f) * (1.0f - h) * 0.85f);\n"
    "    float disc = smoothstep(0.9990f, 0.9996f, toward) + pow(toward, 64.0f) * 0.35f;\n"
    "    col += disc * float3(1.0f, 0.95f, 0.80f) * saturate(sun.y * 6.0f + 0.4f);\n"
    "    return float4(col, 1.0f);\n"
    "}\n";

// Emits one cube face (4 verts + 6 indices) for the given corners.
static void RebuildChunkMesh(World& w, const ChunkCoord& cc, Chunk& c) {
    static std::vector<Vertex> verts;     // reused across rebuilds: no per-rebuild allocation once warm
    static std::vector<uint16_t> indices;
    BuildChunkMesh(w, cc, c, verts, indices);

    if (c.vb) { c.vb->Release(); c.vb = nullptr; }
    if (c.ib) { c.ib->Release(); c.ib = nullptr; }
    c.indexCount = 0;

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
    }

    c.dirty = false;
    g_meshVersion++;
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
static void DrawChunks(World& w, const Frustum& frustum, bool countStats) {
    UINT stride = sizeof(Vertex), offset = 0;
    for (auto& kv : w.chunks) {
        Chunk& c = *kv.second;
        if (c.indexCount == 0) continue;
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
        g_context->DrawIndexed(c.indexCount, 0, 0);
        if (countStats) {
            ProfAddCounter(PCOUNT_CHUNKS_DRAWN, 1);
            ProfAddCounter(PCOUNT_TRIANGLES_DRAWN, c.indexCount / 3);
        }
    }
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
    g_shadowMeshVersion = g_meshVersion;
    ProfAddCounter(PCOUNT_SHADOW_RENDERS, 1);
}

static void DrawLineDebug(const Mat4& viewProj, Vec3 player); // below InitD3D, beside its pipeline

void RenderScene(World& w, const Mat4& view, const Mat4& proj, Vec3 eye, Vec3 forward, Vec3 up, float dayTime) {
    SkyState sky = ComputeSky(dayTime);
    bool shadows = g_shadows && g_shadowsAvailable && sky.sunLight > 0.001f;
    if (shadows) { ProfScope prof(PROF_SHADOW); UpdateShadowMap(w, eye, sky.sunDir); }
    if (!g_shadows) g_shadowValid = false; // re-render on re-enable

    int64_t worldStart = ProfNow();
    // With no post effect on, draw straight to the backbuffer: the post
    // path costs nothing at all while it's switched off.
    bool post = (g_postEdges || g_postSSAO) && g_postAvailable;
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
        struct { Mat4 viewProj; float sun[4]; float params[4]; float moon[4]; float ghost[4]; float rows[3][4]; } cb = {
            skyViewProj,
            { sky.sunDir.x, sky.sunDir.y, sky.sunDir.z, 0 },
            { day, sky.starsVisible, sky.sunLight, 0 },
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
        cb.sun[0] = sky.sunDir.x; cb.sun[1] = sky.sunDir.y; cb.sun[2] = sky.sunDir.z; cb.sun[3] = 0;
        cb.params[0] = shadows ? 1.0f : 0.0f;
        cb.params[1] = sky.daylight;
        cb.params[2] = sky.sunLight;
        cb.params[3] = 0.5f / SHADOW_SIZE;
        UpdateCBuffer(cb);
        g_context->VSSetShader(g_vs, nullptr, 0);
        g_context->PSSetShader(g_ps, nullptr, 0);
        g_context->IASetInputLayout(g_layout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cbs[2] = { g_cbuffer, g_chunkCBuffer };
        g_context->VSSetConstantBuffers(0, 2, cbs);
        g_context->PSSetConstantBuffers(0, 1, &g_cbuffer);
        ID3D11SamplerState* samplers[2] = { g_sampler, g_shadowSampler };
        g_context->PSSetSamplers(0, 2, samplers);
        ID3D11ShaderResourceView* srvs[2] = { g_blockTexSRV, shadows ? g_shadowSRV : nullptr };
        g_context->PSSetShaderResources(0, 2, srvs);
        DrawChunks(w, ExtractFrustum(viewProj), true);
        if (g_lineDebug) DrawLineDebug(viewProj, eye);
    }
    ProfAdd(PROF_WORLD, ProfNow() - worldStart);

    // Post pass: scene + depth in, backbuffer out.
    if (post) {
        ProfScope prof(PROF_POST);
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        struct { float p0[4]; float p1[4]; } cb = {
            { g_postEdges ? 1.0f : 0.0f, g_postSSAO ? 1.0f : 0.0f, 0.1f, 500.0f }, // near/far: main.cpp's projection
            { 1.0f / g_screenW, 1.0f / g_screenH, proj.m[1][1], 0.0f },
        };
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_postCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &cb, sizeof(cb));
        g_context->Unmap(g_postCB, 0);
        g_context->VSSetShader(g_postVS, nullptr, 0);
        g_context->PSSetShader(g_postPS, nullptr, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->PSSetConstantBuffers(0, 1, &g_postCB);
        g_context->PSSetSamplers(0, 1, &g_pointClampSampler);
        ID3D11ShaderResourceView* srvs[2] = { g_sceneSRV, g_depthSRV };
        g_context->PSSetShaderResources(0, 2, srvs);
        g_context->OMSetDepthStencilState(g_uiDepthState, 0);
        g_context->Draw(3, 0);
        g_context->PSSetShaderResources(0, 2, nulls);
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
static ID3DBlob* CompileShader(const char* src, const char* entry, const char* profile,
                               const D3D_SHADER_MACRO* macros = nullptr) {
    ID3DBlob* blob = nullptr, * err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, macros, nullptr, entry, profile, 0, 0, &blob, &err);
    if (FAILED(hr)) {
        OutputDebugStringA("Shader compile failed: ");
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        if (err) err->Release();
        if (blob) blob->Release();
        return nullptr;
    }
    if (err) err->Release();
    return blob;
}

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

    CreateSizeDependentTargets();

    ID3DBlob* errBlob = nullptr;
    ID3DBlob* vsBlob = CompileShader(g_shaderSrc, "VSMain", "vs_4_0");
    ID3DBlob* psBlob = CompileShader(g_shaderSrc, "PSMain", "ps_4_0");
    bool worldShadows = psBlob != nullptr;
    if (!psBlob) {
        const D3D_SHADER_MACRO noShadows[] = { { "NO_SHADOWS", "1" }, { nullptr, nullptr } };
        psBlob = CompileShader(g_shaderSrc, "PSMain", "ps_4_0", noShadows);
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
    ID3DBlob* skyVsBlob = nullptr, * skyPsBlob = nullptr;
    hr = D3DCompile(g_skyShaderSrc, strlen(g_skyShaderSrc), nullptr, nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &skyVsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(g_skyShaderSrc, strlen(g_skyShaderSrc), nullptr, nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &skyPsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
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
    skyCbd.ByteWidth = sizeof(Mat4) + 7 * 4 * sizeof(float); // viewProj + 7 float4s (SkyCB)
    skyCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    skyCbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&skyCbd, nullptr, &g_skyCBuffer);

    // --- Sun shadows (Section 4.8). Optional: any failure here just
    // leaves shadows unavailable (the Graphics toggle then does nothing).
    if (worldShadows) {
        ID3DBlob* sv = CompileShader(g_shadowShaderSrc, "VSMain", "vs_4_0");
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
        ID3DBlob* pv = CompileShader(g_postShaderSrc, "VSMain", "vs_4_0");
        ID3DBlob* pp = CompileShader(g_postShaderSrc, "PSMain", "ps_4_0");
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
        g_postAvailable = g_postVS && g_postPS && g_postCB && g_pointClampSampler;
    }

    // --- Debug line pipeline (optional).
    {
        ID3DBlob* dv = CompileShader(g_debugShaderSrc, "VSMain", "vs_4_0");
        ID3DBlob* dp = CompileShader(g_debugShaderSrc, "PSMain", "ps_4_0");
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
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = BLOCK_TEX_SIZE; td.Height = BLOCK_TEX_SIZE;
    td.MipLevels = set.mipCount; td.ArraySize = set.layerCount;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
