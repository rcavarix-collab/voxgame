// render.cpp
//
// D3D11 setup, chunk meshing, and the procedural sky mesh.

#include "render.h"
#include "profiler.h"
#include "blocktex.h"
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
// layer, and bits for u/v, ambient occlusion and face. Lighting is a
// fixed brightness per face direction times an AO darkening, both
// decided at mesh time -- the pixel shader just multiplies.
static const char* g_shaderSrc =
    "cbuffer CB : register(b0) { row_major matrix mvp; };\n"
    "cbuffer ChunkCB : register(b1) { float4 chunkOrigin; };\n"
    "struct VSIn { uint4 pos:POSITION; uint2 attr:TEXCOORD0; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 uvl:TEXCOORD0; float light:TEXCOORD1; };\n"
    // +X -X +Y -Y +Z -Z: top brightest, bottom darkest, X and Z sides
    // distinct so edges between two side faces still read.
    "static const float faceShade[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.68f, 0.68f };\n"
    "static const float aoCurve[4] = { 0.50f, 0.66f, 0.83f, 1.00f };\n"
    "PSIn VSMain(VSIn i) {\n"
    "    PSIn o;\n"
    "    float3 p = float3(i.pos.xyz) + chunkOrigin.xyz;\n"
    "    o.pos = mul(float4(p, 1.0f), mvp);\n"
    "    uint b = i.attr.y;\n"
    "    o.uvl = float3((float)(b & 31u), (float)((b >> 5) & 31u), (float)i.attr.x);\n"
    "    o.light = faceShade[(b >> 12) & 7u] * aoCurve[(b >> 10) & 3u];\n"
    "    return o;\n"
    "}\n"
    "Texture2DArray tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "float4 PSMain(PSIn i) : SV_TARGET { float4 c = tex0.Sample(samp0, i.uvl); return float4(c.rgb * i.light, 1.0f); }\n";

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
    "cbuffer SkyCB : register(b0) { row_major matrix viewProj; };\n"
    "struct VSIn { float3 pos:POSITION; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 dir:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn input) { PSIn o; o.pos = mul(float4(input.pos,1.0f), viewProj); o.dir = input.pos; return o; }\n"
    "static const float3 ZENITH = float3(0.25f, 0.45f, 0.85f);\n"
    "static const float3 HORIZON = float3(0.65f, 0.75f, 0.95f);\n"
    "float4 PSMain(PSIn input) : SV_TARGET {\n"
    "    float3 d = normalize(input.dir);\n"
    "    float t = saturate(d.y);\n"
    "    return float4(lerp(HORIZON, ZENITH, t), 1.0f);\n"
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
}

// Capped the same way gravity (MAX_FALLS) and column generation
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

void UpdateCBuffer(const Mat4& mvp) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    CBData* data = (CBData*)mapped.pData;
    data->mvp = mvp;
    g_context->Unmap(g_cbuffer, 0);
}

void DrawWorld(World& w, const Mat4& viewProj) {
    Frustum frustum = ExtractFrustum(viewProj);
    g_context->VSSetShader(g_vs, nullptr, 0);
    g_context->PSSetShader(g_ps, nullptr, 0);
    g_context->IASetInputLayout(g_layout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[2] = { g_cbuffer, g_chunkCBuffer };
    g_context->VSSetConstantBuffers(0, 2, cbs);
    g_context->PSSetSamplers(0, 1, &g_sampler);
    UpdateCBuffer(viewProj);
    g_context->PSSetShaderResources(0, 1, &g_blockTexSRV);

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
        ProfAddCounter(PCOUNT_CHUNKS_DRAWN, 1);
        ProfAddCounter(PCOUNT_TRIANGLES_DRAWN, c.indexCount / 3);
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

bool InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = SCREEN_W;
    scd.BufferDesc.Height = SCREEN_H;
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

    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = SCREEN_W; depthDesc.Height = SCREEN_H;
    depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depthTex = nullptr;
    g_device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    g_device->CreateDepthStencilView(depthTex, nullptr, &g_dsv);
    depthTex->Release();

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)SCREEN_W; vp.Height = (float)SCREEN_H;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);

    ID3DBlob* vsBlob = nullptr, * psBlob = nullptr, * errBlob = nullptr;
    hr = D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
                     "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
                     "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
        return false;
    }
    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = { // mesher.h's packed Vertex
        { "POSITION", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R16G16_UINT, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_layout);
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
    skyCbd.ByteWidth = sizeof(Mat4);
    skyCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    skyCbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&skyCbd, nullptr, &g_skyCBuffer);

    return true;
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
