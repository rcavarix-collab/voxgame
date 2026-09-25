// render.cpp -- see render.h.

#include "render.h"
#include "shaders.h"
#include "terrain.h"
#include "props.h"
#include "sun.h"
#include "persist.h"
#include "profiler.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

int g_screenW = DEFAULT_WINDOW_W, g_screenH = DEFAULT_WINDOW_H;

// The text atlas, drawn with GDI+ at load (ui_font.cpp keeps GDI+ out of this file).
extern "C" bool GenerateTextAtlas(int atlasW, int atlasH, int whiteSize, int cols, int cellW, int cellH, float fontPx, uint8_t** outBGRA);
extern "C" void FreeTextAtlas(uint8_t* p);

namespace {
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

IDXGISwapChain* g_swap = nullptr;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ID3D11DepthStencilView* g_dsv = nullptr;

ID3D11VertexShader *g_skyVS = nullptr, *g_terrainVS = nullptr, *g_meshVS = nullptr, *g_uiVS = nullptr;
ID3D11PixelShader *g_skyPS = nullptr, *g_terrainPS = nullptr, *g_meshPS = nullptr, *g_uiPS = nullptr;
ID3D11InputLayout *g_terrainLayout = nullptr, *g_meshLayout = nullptr, *g_uiLayout = nullptr;
ID3D11Buffer* g_dynVB = nullptr;
UINT g_dynCapacity = 0; // vertices
ID3D11Buffer *g_skyCB = nullptr, *g_frameCB = nullptr, *g_uiCB = nullptr, *g_uiVB = nullptr;
ID3D11RasterizerState *g_rasterSolid = nullptr, *g_rasterNoCull = nullptr;
ID3D11DepthStencilState *g_depthOn = nullptr, *g_depthOff = nullptr;
ID3D11BlendState* g_blendAlpha = nullptr;
ID3D11SamplerState* g_pointSampler = nullptr;
ID3D11ShaderResourceView* g_atlasSRV = nullptr;
std::string g_shaderErrors;

// ---- text atlas layout: an 8 px white square top-left, then 16 x 6 glyph cells (ASCII 32..126) ----
const int ATLAS_W = 256, ATLAS_H = 128, WHITE = 8, COLS = 16, CELL_W = 10, CELL_H = 18;
const float FONT_PX = 13.0f;

// ---- terrain chunk buffers ----
struct ChunkGpu {
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    UINT indexCount = 0;
    uint32_t version = 0xFFFFFFFFu;
    bool seen = false;
};
std::unordered_map<ChunkKey, ChunkGpu, ChunkKeyHash> g_chunks;

// ---- prop tile buffers (same idea: re-created only when a tile's mesh changes) ----
struct TileGpu { ID3D11Buffer* vb = nullptr; UINT count = 0; uint32_t version = 0xFFFFFFFFu; bool seen = false; };
std::unordered_map<PropTileKey, TileGpu, PropTileKeyHash> g_propTiles;

struct TerrainVertexGpu { float x, y, z; uint8_t mat, ao, a, b; };
static_assert(sizeof(TerrainVertexGpu) == sizeof(TerrainVertex), "terrain vertex layout");

struct SkyCBData { float right[4], up[4], fwd[4], sun[4], zenith[4], horizon[4], ground[4]; };
struct FrameCBData { Mat4 viewProj; float eye[4], sun[4], sunColor[4], ambient[4], fog[4], fogParams[4]; };
struct UICBData { float screen[4]; };
struct UIVertex { float x, y, u, v, r, g, b, a; };
const UINT UI_VB_CAPACITY = 6 * 2048;
std::vector<UIVertex> g_ui;

// ---------------------------------------------------------------------
// Compiled-shader cache (from Voxistics, Part XVI there): each compile's
// bytecode is kept under a hash of everything that went into it, so a
// changed shader simply gets a new file; files nobody asked for this run
// are removed at the end of start-up. What isn't cached compiles on four
// threads at once (a fixed number: the machine is never asked what it has).
// ---------------------------------------------------------------------
struct ShaderJob { const char* src; const char* entry; const char* profile; const char* what; uint64_t key; ID3DBlob* blob; std::string errors; };
std::vector<ShaderJob> g_jobs;
std::filesystem::path g_cacheDir;
std::unordered_set<uint64_t> g_keysUsed;
int g_cached = 0, g_compiled = 0;
const uint32_t CACHE_MAGIC = 0x43434143; // "CACC"

uint64_t Fnv64(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
uint64_t ShaderKey(const char* src, const char* entry, const char* profile) {
    uint64_t h = 14695981039346656037ull;
    const char zero = 0;
    for (const char* s : { src, entry, profile }) { h = Fnv64(h, s, strlen(s)); h = Fnv64(h, &zero, 1); }
    const int version = D3D_COMPILER_VERSION;
    return Fnv64(h, &version, sizeof version);
}
std::filesystem::path CachePath(uint64_t key) {
    char name[32];
    snprintf(name, sizeof name, "%016llx.csc", (unsigned long long)key);
    return g_cacheDir / name;
}
ID3DBlob* LoadCached(uint64_t key) {
    if (g_cacheDir.empty()) return nullptr;
    std::ifstream f(CachePath(key), std::ios::binary);
    if (!f) return nullptr;
    uint32_t magic = 0, size = 0, sum = 0; uint64_t stored = 0;
    f.read((char*)&magic, 4); f.read((char*)&stored, 8); f.read((char*)&size, 4);
    if (!f || magic != CACHE_MAGIC || stored != key || size == 0 || size > (16u << 20)) return nullptr;
    std::vector<char> bytes(size);
    f.read(bytes.data(), size); f.read((char*)&sum, 4);
    if (!f || (uint32_t)Fnv64(14695981039346656037ull, bytes.data(), size) != sum) return nullptr; // damaged: recompile
    ID3DBlob* blob = nullptr;
    if (FAILED(D3DCreateBlob(size, &blob)) || !blob) return nullptr;
    memcpy(blob->GetBufferPointer(), bytes.data(), size);
    return blob;
}
void StoreCached(uint64_t key, ID3DBlob* blob) {
    if (g_cacheDir.empty() || !blob) return;
    std::ofstream f(CachePath(key), std::ios::binary | std::ios::trunc);
    if (!f) return;
    uint32_t size = (uint32_t)blob->GetBufferSize();
    uint32_t sum = (uint32_t)Fnv64(14695981039346656037ull, blob->GetBufferPointer(), size);
    f.write((const char*)&CACHE_MAGIC, 4); f.write((const char*)&key, 8); f.write((const char*)&size, 4);
    f.write((const char*)blob->GetBufferPointer(), size); f.write((const char*)&sum, 4);
}
ID3DBlob* CompileRaw(const char* src, const char* entry, const char* profile, std::string& errors) {
    ID3DBlob *blob = nullptr, *err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(hr)) {
        errors = err ? (const char*)err->GetBufferPointer() : "no compiler output\n";
        SafeRelease(blob);
    }
    SafeRelease(err);
    return blob;
}
void PrepareShaders() {
    g_cacheDir = ShaderCacheDirectory();
    ShaderJob list[] = {
        { g_skyShaderSrc, "VSMain", "vs_4_0", "sky", 0, nullptr, {} }, { g_skyShaderSrc, "PSMain", "ps_4_0", "sky", 0, nullptr, {} },
        { g_terrainShaderSrc, "VSMain", "vs_4_0", "terrain", 0, nullptr, {} }, { g_terrainShaderSrc, "PSMain", "ps_4_0", "terrain", 0, nullptr, {} },
        { g_meshShaderSrc, "VSMain", "vs_4_0", "meshes", 0, nullptr, {} }, { g_meshShaderSrc, "PSMain", "ps_4_0", "meshes", 0, nullptr, {} },
        { g_uiShaderSrc, "VSMain", "vs_4_0", "ui", 0, nullptr, {} }, { g_uiShaderSrc, "PSMain", "ps_4_0", "ui", 0, nullptr, {} },
    };
    std::vector<size_t> todo;
    for (ShaderJob& j : list) {
        j.key = ShaderKey(j.src, j.entry, j.profile);
        g_keysUsed.insert(j.key);
        j.blob = LoadCached(j.key);
        if (j.blob) g_cached++; else todo.push_back(g_jobs.size());
        g_jobs.push_back(j);
    }
    std::atomic<size_t> next{ 0 };
    auto work = [&]() {
        for (size_t i; (i = next.fetch_add(1)) < todo.size();) {
            ShaderJob& j = g_jobs[todo[i]];
            j.blob = CompileRaw(j.src, j.entry, j.profile, j.errors);
        }
    };
    std::vector<std::thread> threads;
    for (int t = 1; t < 4 && t < (int)todo.size(); t++) threads.emplace_back(work);
    work();
    for (std::thread& t : threads) t.join();
    for (size_t i : todo) { StoreCached(g_jobs[i].key, g_jobs[i].blob); g_compiled++; }
}
ID3DBlob* Shader(const char* src, const char* entry) {
    for (ShaderJob& j : g_jobs)
        if (j.src == src && strcmp(j.entry, entry) == 0) {
            if (!j.blob) g_shaderErrors += std::string(j.what) + " (" + entry + "):\n" + j.errors + "\n";
            return j.blob;
        }
    return nullptr;
}
void FinishShaders() {
    for (ShaderJob& j : g_jobs) SafeRelease(j.blob);
    g_jobs.clear();
    if (!g_cacheDir.empty()) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(g_cacheDir, ec)) {
            if (e.path().extension() != ".csc") continue;
            std::string stem = e.path().stem().string();
            char* end = nullptr;
            unsigned long long k = strtoull(stem.c_str(), &end, 16);
            if (stem.size() == 16 && end && *end == 0 && g_keysUsed.count((uint64_t)k)) continue;
            std::error_code rm;
            std::filesystem::remove(e.path(), rm);
        }
    }
    char note[96];
    snprintf(note, sizeof note, "shaders: %d from the cache, %d compiled", g_cached, g_compiled);
    ProfBootNote(note);
    if (!g_shaderErrors.empty()) WriteTextFile("shader_errors.txt", g_shaderErrors);
}

// ---------------------------------------------------------------------
// GPU timing: a ring of three frames of timestamp queries, read back only
// when ready (DONOTFLUSH), so measuring never makes the CPU wait.
// ---------------------------------------------------------------------
const int GPU_RING = 3;
struct GpuFrame { ID3D11Query *disjoint = nullptr, *t0 = nullptr, *t1 = nullptr, *t2 = nullptr; bool issued = false; };
GpuFrame g_gpu[GPU_RING];
int g_gpuIndex = 0;
bool g_gpuOk = false;

void InitGpuTiming() {
    D3D11_QUERY_DESC dq = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }, tq = { D3D11_QUERY_TIMESTAMP, 0 };
    g_gpuOk = true;
    for (GpuFrame& f : g_gpu)
        if (FAILED(g_dev->CreateQuery(&dq, &f.disjoint)) || FAILED(g_dev->CreateQuery(&tq, &f.t0)) ||
            FAILED(g_dev->CreateQuery(&tq, &f.t1)) || FAILED(g_dev->CreateQuery(&tq, &f.t2)))
            g_gpuOk = false;
}

// ---------------------------------------------------------------------
// Size-dependent targets
// ---------------------------------------------------------------------
void CreateTargets() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    // An sRGB view of the plain back buffer: shading happens in linear light
    // and the hardware converts on write.
    D3D11_RENDER_TARGET_VIEW_DESC rd = {};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(g_dev->CreateRenderTargetView(back, &rd, &g_rtv))) g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = g_screenW; dd.Height = g_screenH; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depth = nullptr;
    g_dev->CreateTexture2D(&dd, nullptr, &depth);
    if (depth) { g_dev->CreateDepthStencilView(depth, nullptr, &g_dsv); depth->Release(); }
}

ID3D11Buffer* MakeBuffer(UINT bytes, UINT bind, D3D11_USAGE usage, const void* data) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = bytes; bd.BindFlags = bind; bd.Usage = usage;
    if (usage == D3D11_USAGE_DYNAMIC) bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA sd = { data, 0, 0 };
    ID3D11Buffer* b = nullptr;
    g_dev->CreateBuffer(&bd, data ? &sd : nullptr, &b);
    return b;
}
template <class T> void Upload(ID3D11Buffer* b, const T& data) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &data, sizeof(T)); g_ctx->Unmap(b, 0); }
}

// ---- view frustum (Gribb/Hartmann planes from the combined matrix) ----
struct Plane { float a, b, c, d; };
void Frustum(const Mat4& M, Plane p[6]) {
    auto set = [](Plane& q, float a, float b, float c, float d) { q = { a, b, c, d }; };
    set(p[0], M.m[0][3] + M.m[0][0], M.m[1][3] + M.m[1][0], M.m[2][3] + M.m[2][0], M.m[3][3] + M.m[3][0]);
    set(p[1], M.m[0][3] - M.m[0][0], M.m[1][3] - M.m[1][0], M.m[2][3] - M.m[2][0], M.m[3][3] - M.m[3][0]);
    set(p[2], M.m[0][3] + M.m[0][1], M.m[1][3] + M.m[1][1], M.m[2][3] + M.m[2][1], M.m[3][3] + M.m[3][1]);
    set(p[3], M.m[0][3] - M.m[0][1], M.m[1][3] - M.m[1][1], M.m[2][3] - M.m[2][1], M.m[3][3] - M.m[3][1]);
    set(p[4], M.m[0][2], M.m[1][2], M.m[2][2], M.m[3][2]);
    set(p[5], M.m[0][3] - M.m[0][2], M.m[1][3] - M.m[1][2], M.m[2][3] - M.m[2][2], M.m[3][3] - M.m[3][2]);
}
bool BoxVisible(const Plane p[6], Vec3 mn, Vec3 mx) {
    for (int i = 0; i < 6; i++) {
        float x = p[i].a >= 0 ? mx.x : mn.x, y = p[i].b >= 0 ? mx.y : mn.y, z = p[i].c >= 0 ? mx.z : mn.z;
        if (p[i].a * x + p[i].b * y + p[i].c * z + p[i].d < 0) return false;
    }
    return true;
}
} // namespace

const std::string& ShaderErrors() { return g_shaderErrors; }

bool InitRender(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = g_screenW; scd.BufferDesc.Height = g_screenH;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL chosen;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, ARRAYSIZE(levels),
                                             D3D11_SDK_VERSION, &scd, &g_swap, &g_dev, &chosen, &g_ctx)))
        return false;
    // Alt+Enter would flip DXGI into exclusive fullscreen behind our back; F11 is borderless.
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_swap->GetParent(__uuidof(IDXGIFactory), (void**)&factory)) && factory) {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    CreateTargets();
    ProfBootMark("GRAPHICS DEVICE");

    PrepareShaders();
    ID3DBlob *b;
    if ((b = Shader(g_skyShaderSrc, "VSMain"))) g_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_skyVS);
    if ((b = Shader(g_skyShaderSrc, "PSMain"))) g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_skyPS);
    if ((b = Shader(g_terrainShaderSrc, "VSMain"))) {
        g_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_terrainVS);
        D3D11_INPUT_ELEMENT_DESC ie[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_dev->CreateInputLayout(ie, 2, b->GetBufferPointer(), b->GetBufferSize(), &g_terrainLayout);
    }
    if ((b = Shader(g_terrainShaderSrc, "PSMain"))) g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_terrainPS);
    if ((b = Shader(g_meshShaderSrc, "VSMain"))) {
        g_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_meshVS);
        D3D11_INPUT_ELEMENT_DESC ie[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_dev->CreateInputLayout(ie, 2, b->GetBufferPointer(), b->GetBufferSize(), &g_meshLayout);
    }
    if ((b = Shader(g_meshShaderSrc, "PSMain"))) g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_meshPS);
    if ((b = Shader(g_uiShaderSrc, "VSMain"))) {
        g_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_uiVS);
        D3D11_INPUT_ELEMENT_DESC ie[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_dev->CreateInputLayout(ie, 3, b->GetBufferPointer(), b->GetBufferSize(), &g_uiLayout);
    }
    if ((b = Shader(g_uiShaderSrc, "PSMain"))) g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_uiPS);
    FinishShaders();
    if (!g_terrainVS || !g_terrainPS || !g_terrainLayout) return false; // nothing to see without these
    ProfBootMark("SHADERS");

    g_skyCB = MakeBuffer(sizeof(SkyCBData), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, nullptr);
    g_frameCB = MakeBuffer(sizeof(FrameCBData), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, nullptr);
    g_uiCB = MakeBuffer(sizeof(UICBData), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, nullptr);
    g_uiVB = MakeBuffer(UI_VB_CAPACITY * sizeof(UIVertex), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, nullptr);

    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_BACK; rs.DepthClipEnable = TRUE; // clockwise = front
    g_dev->CreateRasterizerState(&rs, &g_rasterSolid);
    rs.CullMode = D3D11_CULL_NONE; // 2D shapes come in either winding
    g_dev->CreateRasterizerState(&rs, &g_rasterNoCull);
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS;
    g_dev->CreateDepthStencilState(&ds, &g_depthOn);
    ds.DepthEnable = FALSE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    g_dev->CreateDepthStencilState(&ds, &g_depthOff);
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_dev->CreateBlendState(&bd, &g_blendAlpha);
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev->CreateSamplerState(&sd, &g_pointSampler);

    // Text atlas: the player's installed system font, rendered once here (never shipped).
    uint8_t* pixels = nullptr;
    if (GenerateTextAtlas(ATLAS_W, ATLAS_H, WHITE, COLS, CELL_W, CELL_H, FONT_PX, &pixels) && pixels) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = ATLAS_W; td.Height = ATLAS_H; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = { pixels, ATLAS_W * 4, 0 };
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_dev->CreateTexture2D(&td, &init, &tex)) && tex) { g_dev->CreateShaderResourceView(tex, nullptr, &g_atlasSRV); tex->Release(); }
        FreeTextAtlas(pixels);
    }
    InitGpuTiming();
    g_ui.reserve(UI_VB_CAPACITY);
    ProfBootMark("RENDER SETUP");
    return true;
}

void ShutdownRender() {
    for (auto& kv : g_chunks) { SafeRelease(kv.second.vb); SafeRelease(kv.second.ib); }
    g_chunks.clear();
    for (auto& kv : g_propTiles) SafeRelease(kv.second.vb);
    g_propTiles.clear();
    SafeRelease(g_dynVB);
    SafeRelease(g_meshLayout); SafeRelease(g_meshPS); SafeRelease(g_meshVS);
    for (GpuFrame& f : g_gpu) { SafeRelease(f.disjoint); SafeRelease(f.t0); SafeRelease(f.t1); SafeRelease(f.t2); }
    SafeRelease(g_atlasSRV); SafeRelease(g_pointSampler); SafeRelease(g_blendAlpha);
    SafeRelease(g_depthOn); SafeRelease(g_depthOff); SafeRelease(g_rasterSolid); SafeRelease(g_rasterNoCull);
    SafeRelease(g_uiVB); SafeRelease(g_uiCB); SafeRelease(g_frameCB); SafeRelease(g_skyCB);
    SafeRelease(g_uiLayout); SafeRelease(g_terrainLayout);
    SafeRelease(g_uiPS); SafeRelease(g_uiVS); SafeRelease(g_terrainPS); SafeRelease(g_terrainVS); SafeRelease(g_skyPS); SafeRelease(g_skyVS);
    SafeRelease(g_dsv); SafeRelease(g_rtv);
    if (g_ctx) g_ctx->ClearState();
    SafeRelease(g_ctx); SafeRelease(g_swap); SafeRelease(g_dev);
}

void ResizeRender(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!g_swap) { g_screenW = w; g_screenH = h; return; }
    if (w == g_screenW && h == g_screenH) return;
    g_screenW = w; g_screenH = h;
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    SafeRelease(g_rtv); SafeRelease(g_dsv);
    g_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    CreateTargets();
}

int SyncTerrain(const Terrain& t) {
    int uploaded = 0;
    for (auto& kv : g_chunks) kv.second.seen = false;
    for (const auto& kv : t.Chunks()) {
        const TerrainChunk& c = kv.second;
        if (!c.meshed || c.indices.empty()) continue;
        ChunkGpu& g = g_chunks[kv.first];
        g.seen = true;
        if (g.version == c.version) continue;
        SafeRelease(g.vb); SafeRelease(g.ib);
        g.vb = MakeBuffer((UINT)(c.verts.size() * sizeof(TerrainVertex)), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, c.verts.data());
        g.ib = MakeBuffer((UINT)(c.indices.size() * sizeof(uint16_t)), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, c.indices.data());
        g.indexCount = (UINT)c.indices.size();
        g.version = c.version;
        uploaded++;
    }
    // Chunks gone from the world (or emptied) give their buffers back.
    for (auto it = g_chunks.begin(); it != g_chunks.end();) {
        if (!it->second.seen) { SafeRelease(it->second.vb); SafeRelease(it->second.ib); it = g_chunks.erase(it); }
        else ++it;
    }
    return uploaded;
}

void GpuFrameBegin() {
    if (!g_gpuOk) return;
    GpuFrame& f = g_gpu[g_gpuIndex];
    // The oldest frame in the ring: read it only if it's ready (never wait).
    if (f.issued) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        UINT64 a, b, c;
        if (g_ctx->GetData(f.disjoint, &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            g_ctx->GetData(f.t0, &a, sizeof a, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            g_ctx->GetData(f.t1, &b, sizeof b, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            g_ctx->GetData(f.t2, &c, sizeof c, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint && dj.Frequency) {
            double k = 1000.0 / (double)dj.Frequency;
            ProfAddMs(PROF_GPU_WORLD, (double)(b - a) * k);
            ProfAddMs(PROF_GPU_UI, (double)(c - b) * k);
        }
        f.issued = false;
    }
    g_ctx->Begin(f.disjoint);
    g_ctx->End(f.t0);
}
void GpuMarkWorldDone() { if (g_gpuOk) g_ctx->End(g_gpu[g_gpuIndex].t1); }
void GpuFrameEnd() {
    if (!g_gpuOk) return;
    GpuFrame& f = g_gpu[g_gpuIndex];
    g_ctx->End(f.t2);
    g_ctx->End(f.disjoint);
    f.issued = true;
    g_gpuIndex = (g_gpuIndex + 1) % GPU_RING;
}

void RenderWorld(const Terrain& t, const FrameView& v) {
    ProfScope prof(PROF_WORLD);
    (void)t; // chunk buffers are already synced (SyncTerrain); props and shadows will read it
    D3D11_VIEWPORT vp = { 0, 0, (float)g_screenW, (float)g_screenH, 0, 1 };
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->OMSetRenderTargets(1, &g_rtv, g_dsv);
    g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    g_ctx->RSSetState(g_rasterSolid);
    SkyLight sky = SkyAt(v.dayTime);
    Vec3 sun = SunDirection(v.dayTime);
    float strength = SunStrength(v.dayTime);

    // Sky first, depth off: everything else draws over it.
    if (g_skyVS && g_skyPS) {
        SkyCBData s = {};
        auto put = [](float* d, Vec3 a, float w) { d[0] = a.x; d[1] = a.y; d[2] = a.z; d[3] = w; };
        put(s.right, v.right, v.tanHalfFovX); put(s.up, v.up, v.tanHalfFovY); put(s.fwd, v.forward, 0);
        put(s.sun, sun, strength); put(s.zenith, sky.zenith, 1); put(s.horizon, sky.horizon, 1); put(s.ground, sky.ground, 1);
        Upload(g_skyCB, s);
        g_ctx->OMSetDepthStencilState(g_depthOff, 0);
        g_ctx->IASetInputLayout(nullptr);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->VSSetShader(g_skyVS, nullptr, 0);
        g_ctx->PSSetShader(g_skyPS, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_skyCB);
        g_ctx->Draw(3, 0);
    } else {
        float clear[4] = { sky.horizon.x, sky.horizon.y, sky.horizon.z, 1 };
        g_ctx->ClearRenderTargetView(g_rtv, clear);
    }

    // Terrain.
    Mat4 vpm = MatMul(v.view, v.proj);
    FrameCBData f = {};
    f.viewProj = vpm;
    auto put = [](float* d, Vec3 a, float w) { d[0] = a.x; d[1] = a.y; d[2] = a.z; d[3] = w; };
    put(f.eye, v.eye, 0); put(f.sun, sun, strength); put(f.sunColor, sky.sun, 1); put(f.ambient, sky.ambient, 1);
    put(f.fog, sky.horizon, 1);
    f.fogParams[0] = 120.0f; f.fogParams[1] = 1.0f / 520.0f; f.fogParams[2] = 0.85f;
    Upload(g_frameCB, f);
    g_ctx->OMSetDepthStencilState(g_depthOn, 0);
    g_ctx->IASetInputLayout(g_terrainLayout);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_terrainVS, nullptr, 0);
    g_ctx->PSSetShader(g_terrainPS, nullptr, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_frameCB);
    g_ctx->PSSetConstantBuffers(0, 1, &g_frameCB);
    Plane planes[6];
    Frustum(vpm, planes);
    const float span = CHUNK * CELL;
    int drawn = 0; long long tris = 0;
    UINT stride = sizeof(TerrainVertex), offset = 0;
    for (auto& kv : g_chunks) {
        Vec3 mn = Terrain::ChunkMin(kv.first) - Vec3{ CELL, CELL, CELL };  // facets reach a cell outside their chunk
        Vec3 mx = mn + Vec3{ span + 2 * CELL, span + 2 * CELL, span + 2 * CELL };
        if (!BoxVisible(planes, mn, mx)) continue;
        g_ctx->IASetVertexBuffers(0, 1, &kv.second.vb, &stride, &offset);
        g_ctx->IASetIndexBuffer(kv.second.ib, DXGI_FORMAT_R16_UINT, 0);
        g_ctx->DrawIndexed(kv.second.indexCount, 0, 0);
        drawn++;
        tris += kv.second.indexCount / 3;
    }
    ProfAddCounter(PCOUNT_CHUNKS_DRAWN, drawn);
    ProfAddCounter(PCOUNT_TRIANGLES_DRAWN, tris);
}

int SyncProps(const Props& p) {
    int uploaded = 0;
    for (auto& kv : g_propTiles) kv.second.seen = false;
    for (const auto& kv : p.Tiles()) {
        const PropTile& t = kv.second;
        if (!t.meshed || t.mesh.empty()) continue;
        TileGpu& g = g_propTiles[kv.first];
        g.seen = true;
        if (g.version == t.version) continue;
        SafeRelease(g.vb);
        g.vb = MakeBuffer((UINT)(t.mesh.size() * sizeof(MeshVertex)), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, t.mesh.data());
        g.count = (UINT)t.mesh.size();
        g.version = t.version;
        uploaded++;
    }
    for (auto it = g_propTiles.begin(); it != g_propTiles.end();) {
        if (!it->second.seen) { SafeRelease(it->second.vb); it = g_propTiles.erase(it); }
        else ++it;
    }
    return uploaded;
}

void RenderMeshes(const FrameView& v, const std::vector<MeshVertex>& dynamic) {
    ProfScope prof(PROF_WORLD);
    if (!g_meshVS || !g_meshPS || !g_meshLayout) return;
    g_ctx->OMSetDepthStencilState(g_depthOn, 0);
    g_ctx->RSSetState(g_rasterSolid);
    g_ctx->IASetInputLayout(g_meshLayout);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_meshVS, nullptr, 0);
    g_ctx->PSSetShader(g_meshPS, nullptr, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_frameCB); // RenderWorld filled it this frame
    g_ctx->PSSetConstantBuffers(0, 1, &g_frameCB);
    Plane planes[6];
    Frustum(MatMul(v.view, v.proj), planes);
    UINT stride = sizeof(MeshVertex), offset = 0;
    long long tris = 0;
    for (auto& kv : g_propTiles) {
        Vec3 mn = { kv.first.x * Props::TILE - 12, -20, kv.first.z * Props::TILE - 12 };  // fallen trees reach past their tile
        Vec3 mx = { mn.x + Props::TILE + 24, 30, mn.z + Props::TILE + 24 };
        if (!BoxVisible(planes, mn, mx)) continue;
        g_ctx->IASetVertexBuffers(0, 1, &kv.second.vb, &stride, &offset);
        g_ctx->Draw(kv.second.count, 0);
        tris += kv.second.count / 3;
    }
    if (!dynamic.empty()) {
        UINT n = (UINT)dynamic.size();
        const UINT MAX_DYNAMIC = 300000; // governed: a wild frame can't ask for more
        if (n > MAX_DYNAMIC) n = MAX_DYNAMIC - MAX_DYNAMIC % 3;
        if (n > g_dynCapacity) {
            SafeRelease(g_dynVB);
            g_dynCapacity = n + n / 2 + 3000;
            if (g_dynCapacity > MAX_DYNAMIC) g_dynCapacity = MAX_DYNAMIC;
            g_dynVB = MakeBuffer(g_dynCapacity * sizeof(MeshVertex), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, nullptr);
        }
        D3D11_MAPPED_SUBRESOURCE m;
        if (g_dynVB && SUCCEEDED(g_ctx->Map(g_dynVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, dynamic.data(), n * sizeof(MeshVertex));
            g_ctx->Unmap(g_dynVB, 0);
            g_ctx->IASetVertexBuffers(0, 1, &g_dynVB, &stride, &offset);
            g_ctx->Draw(n, 0);
            tris += n / 3;
        }
    }
    ProfAddCounter(PCOUNT_TRIANGLES_DRAWN, tris);
}

bool ProjectToScreen(const FrameView& v, Vec3 p, float& sx, float& sy) {
    Vec3 d = p - v.eye;
    float z = Dot(d, v.forward);
    if (z < 0.5f) return false;
    sx = g_screenW * 0.5f * (1.0f + Dot(d, v.right) / (z * v.tanHalfFovX));
    sy = g_screenH * 0.5f * (1.0f - Dot(d, v.up) / (z * v.tanHalfFovY));
    return true;
}

void PresentFrame(bool vsync) { g_swap->Present(vsync ? 1 : 0, 0); }

// ---------------------------------------------------------------------
// 2D layer
// ---------------------------------------------------------------------
namespace {
void Color4(uint32_t rgba, float out[4]) {
    out[0] = ((rgba >> 24) & 255) / 255.0f; out[1] = ((rgba >> 16) & 255) / 255.0f;
    out[2] = ((rgba >> 8) & 255) / 255.0f; out[3] = (rgba & 255) / 255.0f;
}
void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t rgba) {
    float c[4]; Color4(rgba, c);
    UIVertex a = { x0, y0, u0, v0, c[0], c[1], c[2], c[3] }, b = { x1, y0, u1, v0, c[0], c[1], c[2], c[3] };
    UIVertex d = { x0, y1, u0, v1, c[0], c[1], c[2], c[3] }, e = { x1, y1, u1, v1, c[0], c[1], c[2], c[3] };
    g_ui.insert(g_ui.end(), { a, b, d, b, e, d });
}
void Tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba) {
    float c[4]; Color4(rgba, c);
    float u = 0.5f * WHITE / ATLAS_W, vv = 0.5f * WHITE / ATLAS_H;
    g_ui.push_back({ x0, y0, u, vv, c[0], c[1], c[2], c[3] });
    g_ui.push_back({ x1, y1, u, vv, c[0], c[1], c[2], c[3] });
    g_ui.push_back({ x2, y2, u, vv, c[0], c[1], c[2], c[3] });
}
void FlushUI() {
    if (g_ui.empty() || !g_uiVS || !g_uiPS || !g_atlasSRV) { g_ui.clear(); return; }
    UICBData cb = { { 2.0f / g_screenW, 2.0f / g_screenH, 0, 0 } };
    Upload(g_uiCB, cb);
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->OMSetDepthStencilState(g_depthOff, 0);
    float blend[4] = { 0, 0, 0, 0 };
    g_ctx->OMSetBlendState(g_blendAlpha, blend, 0xFFFFFFFF);
    g_ctx->RSSetState(g_rasterNoCull);
    g_ctx->IASetInputLayout(g_uiLayout);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_uiVS, nullptr, 0);
    g_ctx->PSSetShader(g_uiPS, nullptr, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_uiCB);
    g_ctx->PSSetShaderResources(0, 1, &g_atlasSRV);
    g_ctx->PSSetSamplers(0, 1, &g_pointSampler);
    UINT stride = sizeof(UIVertex), offset = 0;
    g_ctx->IASetVertexBuffers(0, 1, &g_uiVB, &stride, &offset);
    for (size_t start = 0; start < g_ui.size(); start += UI_VB_CAPACITY) {
        size_t n = g_ui.size() - start;
        if (n > UI_VB_CAPACITY) n = UI_VB_CAPACITY;
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(g_ctx->Map(g_uiVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) break;
        memcpy(m.pData, g_ui.data() + start, n * sizeof(UIVertex));
        g_ctx->Unmap(g_uiVB, 0);
        g_ctx->Draw((UINT)n, 0);
    }
    g_ctx->OMSetBlendState(nullptr, blend, 0xFFFFFFFF);
    g_ui.clear();
}
}

void UIBegin() { g_ui.clear(); }
void UIEnd() { ProfScope prof(PROF_UI); FlushUI(); }
void UIRect(float x, float y, float w, float h, uint32_t rgba) {
    float u = 0.5f * WHITE / ATLAS_W, vv = 0.5f * WHITE / ATLAS_H;
    Quad(x, y, x + w, y + h, u, vv, u, vv, rgba);
}
void UIRing(float cx, float cy, float r, float th, uint32_t rgba, int segments) {
    for (int i = 0; i < segments; i++) {
        float a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
        float ox0 = cx + cosf(a0) * (r + th * 0.5f), oy0 = cy + sinf(a0) * (r + th * 0.5f);
        float ox1 = cx + cosf(a1) * (r + th * 0.5f), oy1 = cy + sinf(a1) * (r + th * 0.5f);
        float ix0 = cx + cosf(a0) * (r - th * 0.5f), iy0 = cy + sinf(a0) * (r - th * 0.5f);
        float ix1 = cx + cosf(a1) * (r - th * 0.5f), iy1 = cy + sinf(a1) * (r - th * 0.5f);
        Tri(ox0, oy0, ox1, oy1, ix0, iy0, rgba);
        Tri(ox1, oy1, ix1, iy1, ix0, iy0, rgba);
    }
}
float UILineHeight() { return (float)CELL_H; }
float UITextWidth(const char* text) {
    float w = 0, best = 0;
    for (const char* p = text; *p; p++) { if (*p == '\n') { best = w > best ? w : best; w = 0; } else w += CELL_W - 2; }
    return w > best ? w : best;
}
void UIText(float x, float y, const char* text, uint32_t rgba) {
    float cx = floorf(x), cy = floorf(y); // whole pixels: glyphs are drawn 1:1 and stay crisp
    for (const char* p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') { cx = floorf(x); cy += CELL_H; continue; }
        if (ch < 32 || ch > 126) ch = '?';
        int i = ch - 32;
        float gx = (float)((i % COLS) * CELL_W), gy = (float)(WHITE + (i / COLS) * CELL_H);
        if (ch != ' ')
            Quad(cx, cy, cx + CELL_W, cy + CELL_H, gx / ATLAS_W, gy / ATLAS_H, (gx + CELL_W) / ATLAS_W, (gy + CELL_H) / ATLAS_H, rgba);
        cx += CELL_W - 2; // monospace advance, cells overlap by their padding
    }
}
