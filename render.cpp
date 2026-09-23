// render.cpp
//
// D3D11 setup, chunk meshing, and the procedural sky mesh.

#include "render.h"
#include <d3dcompiler.h>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------------
// textures.cpp entry points (procedural texture generation via GDI+).
// Kept as plain extern declarations since the project has no shared
// header enforcing this contract with textures.cpp itself (a .cpp
// can't include another .cpp's header without one existing); the two
// agree on it by convention, same as before the multi-file split.
// ---------------------------------------------------------------------
extern "C" bool GenerateGameTextures(
    int tileSize, int atlasCols, int atlasRows,
    uint8_t** outAtlasPixelsBGRA, int* outAtlasW, int* outAtlasH);
extern "C" void FreeGeneratedPixels(uint8_t* p);
extern "C" bool GenerateUIAtlas(
    int cellW, int cellH, int cols, int rows,
    uint8_t** outPixelsBGRA, int* outW, int* outH);

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
ID3D11ShaderResourceView* g_atlasSRV = nullptr;

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

static const char* g_shaderSrc =
    "cbuffer CB : register(b0) { row_major matrix mvp; };\n"
    "struct VSIn { float3 pos:POSITION; float2 uv:TEXCOORD0; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn input) { PSIn o; o.pos = mul(float4(input.pos,1.0f), mvp); o.uv = input.uv; return o; }\n"
    "Texture2D tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "float4 PSMain(PSIn input) : SV_TARGET { return tex0.Sample(samp0, input.uv); }\n";

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
static void EmitFace(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                      float x, float y, float z,
                      float c0x, float c0y, float c0z,
                      float c1x, float c1y, float c1z,
                      float c2x, float c2y, float c2z,
                      float c3x, float c3y, float c3z,
                      float u0, float v0, float u1, float v1) {
    uint32_t base = (uint32_t)verts.size();
    verts.push_back({ x + c0x, y + c0y, z + c0z, u0, v1 });
    verts.push_back({ x + c1x, y + c1y, z + c1z, u0, v0 });
    verts.push_back({ x + c2x, y + c2y, z + c2z, u1, v0 });
    verts.push_back({ x + c3x, y + c3y, z + c3z, u1, v1 });
    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
}

// Neighbor solidity for face culling during meshing. The overwhelming
// majority of a chunk's blocks (the 14x14x14 interior, ~67% of all
// cells) have all 6 neighbors inside the same chunk -- reading straight
// out of `c.blocks[]` for that case skips World::Solid's ToChunk
// (a floor-divide per axis) plus an unordered_map lookup entirely, only
// paying that cost for the minority of checks that actually cross a
// chunk boundary. This is the single biggest cost in RebuildChunkMesh:
// unconditionally routing every one of a chunk's up-to-24576 neighbor
// checks (4096 cells x 6 faces) through the generic hash-map lookup was
// real, measurable, and entirely avoidable work.
static bool NeighborSolid(World& w, Chunk& c, int lx, int ly, int lz, int dx, int dy, int dz, int wx, int wy, int wz) {
    int nlx = lx + dx, nly = ly + dy, nlz = lz + dz;
    if ((unsigned)nlx < CHUNK_SIZE && (unsigned)nly < CHUNK_SIZE && (unsigned)nlz < CHUNK_SIZE) {
        BlockID id = (BlockID)c.blocks[Chunk::LocalIndex(nlx, nly, nlz)];
        return id != BLOCK_AIR && g_info[id].solid;
    }
    return w.Solid(wx, wy, wz);
}

static void RebuildChunkMesh(World& w, const ChunkCoord& cc, Chunk& c) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;

    int baseX = cc.x * CHUNK_SIZE, baseY = cc.y * CHUNK_SIZE, baseZ = cc.z * CHUNK_SIZE;

    for (int ly = 0; ly < CHUNK_SIZE; ly++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                BlockID id = (BlockID)c.blocks[Chunk::LocalIndex(lx, ly, lz)];
                if (id == BLOCK_AIR) continue;
                int wx = baseX + lx, wy = baseY + ly, wz = baseZ + lz;

                float u0, v0, u1, v1;
                AtlasRect(g_info[id].tex, u0, v0, u1, v1);
                float x = (float)wx, y = (float)wy, z = (float)wz;

                if (!NeighborSolid(w, c, lx, ly, lz, 1,0,0, wx + 1, wy, wz))
                    EmitFace(verts, indices, x, y, z, 1,0,0, 1,1,0, 1,1,1, 1,0,1, u0,v0,u1,v1);
                if (!NeighborSolid(w, c, lx, ly, lz, -1,0,0, wx - 1, wy, wz))
                    EmitFace(verts, indices, x, y, z, 0,0,1, 0,1,1, 0,1,0, 0,0,0, u0,v0,u1,v1);
                if (!NeighborSolid(w, c, lx, ly, lz, 0,1,0, wx, wy + 1, wz))
                    EmitFace(verts, indices, x, y, z, 0,1,0, 0,1,1, 1,1,1, 1,1,0, u0,v0,u1,v1);
                if (!NeighborSolid(w, c, lx, ly, lz, 0,-1,0, wx, wy - 1, wz))
                    EmitFace(verts, indices, x, y, z, 0,0,1, 0,0,0, 1,0,0, 1,0,1, u0,v0,u1,v1);
                if (!NeighborSolid(w, c, lx, ly, lz, 0,0,1, wx, wy, wz + 1))
                    EmitFace(verts, indices, x, y, z, 1,0,1, 1,1,1, 0,1,1, 0,0,1, u0,v0,u1,v1);
                if (!NeighborSolid(w, c, lx, ly, lz, 0,0,-1, wx, wy, wz - 1))
                    EmitFace(verts, indices, x, y, z, 0,0,0, 0,1,0, 1,1,0, 1,0,0, u0,v0,u1,v1);
            }
        }
    }

    if (c.vb) { c.vb->Release(); c.vb = nullptr; }
    if (c.ib) { c.ib->Release(); c.ib = nullptr; }
    c.indexCount = 0;

    if (!verts.empty()) {
        D3D11_BUFFER_DESC vbd = {};
        vbd.Usage = D3D11_USAGE_DEFAULT;
        vbd.ByteWidth = (UINT)(verts.size() * sizeof(Vertex));
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vinit = {};
        vinit.pSysMem = verts.data();
        g_device->CreateBuffer(&vbd, &vinit, &c.vb);

        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
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
void RebuildDirtyChunks(World& w) {
    int rebuilt = 0;
    for (auto& kv : w.chunks) {
        if (rebuilt >= MAX_CHUNK_REBUILDS_PER_FRAME) break;
        if (kv.second->dirty) { RebuildChunkMesh(w, kv.first, *kv.second); rebuilt++; }
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

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
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

    D3D11_SAMPLER_DESC uiSampDesc = {};
    uiSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
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

bool InitTextures() {
    uint8_t* atlasPixels = nullptr; int atlasW = 0, atlasH = 0;
    if (!GenerateGameTextures(TILE_SIZE, ATLAS_COLS, ATLAS_ROWS, &atlasPixels, &atlasW, &atlasH))
        return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = atlasW; td.Height = atlasH;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = atlasPixels;
    sd.SysMemPitch = atlasW * 4;
    ID3D11Texture2D* atlasTex = nullptr;
    g_device->CreateTexture2D(&td, &sd, &atlasTex);
    g_device->CreateShaderResourceView(atlasTex, nullptr, &g_atlasSRV);
    atlasTex->Release();

    FreeGeneratedPixels(atlasPixels);

    uint8_t* uiPixels = nullptr; int uiW = 0, uiH = 0;
    if (!GenerateUIAtlas(UI_CELL_W, UI_CELL_H, UI_ATLAS_COLS, UI_ATLAS_ROWS, &uiPixels, &uiW, &uiH))
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
