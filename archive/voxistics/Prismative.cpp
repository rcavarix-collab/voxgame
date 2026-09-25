#define NOMINMAX    // Prevent Windows min/max macros
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <string>
#include <shlobj.h>       // For SHGetKnownFolderPath
#include <knownfolders.h> // For FOLDERID_Documents

// Define min and max for GDI+ compatibility
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#include <gdiplus.h>
#undef min
#undef max

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace DirectX;

// ### Constants
constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;
constexpr int GRID_WIDTH = 64;
constexpr int GRID_HEIGHT = 64;
constexpr int GRID_DEPTH = 64;
constexpr int TEXTURE_SIZE = 32;
constexpr float MOVE_SPEED = 0.1f;
constexpr float JUMP_VELOCITY = 0.5f;
constexpr float GRAVITY = -0.015f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float PLAYER_WIDTH = 0.6f;

// ### Constant Buffer Definition
#pragma pack(push, 16)
struct ConstantBuffer {
    XMMATRIX world;
    XMMATRIX view;
    XMMATRIX proj;
};
#pragma pack(pop)

// ### ResourceManager
class ResourceManager {
public:
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;          // FullCube
    ID3D11Buffer* vertexBufferTube = nullptr;      // Tube
    ID3D11Buffer* vertexBufferRamp = nullptr;      // Ramp
    ID3D11Buffer* vertexBufferSlabBottom = nullptr;// SlabBottom
    ID3D11Buffer* vertexBufferSlabTop = nullptr;   // SlabTop
    ID3D11Buffer* vertexBufferPyramid = nullptr;    // Pyramid (full height)
    ID3D11Buffer* vertexBufferPyramidHalf = nullptr;// Pyramid (half height)
    ID3D11Buffer* vertexBufferFunnel = nullptr;     // Funnel (upside-down pyramid)
    ID3D11Buffer* vertexBufferFunnelTop = nullptr;  // FunnelTop (apex at top)
    ID3D11Buffer* indexBuffer = nullptr;           // For FullCube, Tube, Slabs
    ID3D11Buffer* indexBufferRamp = nullptr;       // For Ramp
    ID3D11Buffer* indexBufferPyramid = nullptr;     // For Pyramids
    ID3D11Buffer* constantBuffer = nullptr;
    ID3D11ShaderResourceView* blockTextures[11] = { nullptr };
    ID3D11SamplerState* sampler = nullptr;

    HRESULT Init(ID3D11Device* device, ID3D11DeviceContext* context) {
        const char* vsSrc =
            "cbuffer CB : register(b0) { float4x4 world, view, proj; };"
            "struct VS_IN { float3 pos : POSITION; float2 tex : TEXCOORD0; };"
            "struct PS_IN { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };"
            "PS_IN main(VS_IN input) {"
            "    PS_IN output;"
            "    float4 worldPos = mul(world, float4(input.pos, 1.0f));"
            "    float4 viewPos = mul(view, worldPos);"
            "    output.pos = mul(proj, viewPos);"
            "    output.tex = input.tex;"
            "    return output;"
            "}";
        const char* psSrc =
            "Texture2D tex : register(t0);"
            "SamplerState sam : register(s0);"
            "struct PS_IN { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };"
            "float4 main(PS_IN input) : SV_TARGET { return tex.Sample(sam, input.tex); }";

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        HRESULT hr = D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr,
            "main", "vs_4_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return hr;
        }
        hr = D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr,
            "main", "ps_4_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            vsBlob->Release();
            return hr;
        }
        if (errorBlob) errorBlob->Release();

        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            nullptr, &vertexShader);
        if (FAILED(hr)) return hr;
        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
            nullptr, &pixelShader);
        if (FAILED(hr)) return hr;

        D3D11_INPUT_ELEMENT_DESC ied[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        hr = device->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), &inputLayout);
        if (FAILED(hr)) return hr;

        struct Vertex { XMFLOAT3 pos; XMFLOAT2 tex; };

        // FullCube Vertices
        Vertex vertices[] = {
            // Front face
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 0: Bottom-left
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) }, // 1: Top-left
            { XMFLOAT3(0.5f,   0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) }, // 2: Top-right
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 3: Bottom-right
            // Back face
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) }, // 4: Bottom-left
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 5: Top-left
            { XMFLOAT3(0.5f,   0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 6: Top-right
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) }, // 7: Bottom-right
            // Left face
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 8: Bottom-front
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) }, // 9: Top-front
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 10: Top-back
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) }, // 11: Bottom-back
            // Right face
            { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 12: Bottom-front
            { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) }, // 13: Top-front
            { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 14: Top-back
            { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) }, // 15: Bottom-back
            // Top face
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 16: Front-left
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 17: Back-left
            { XMFLOAT3(0.5f,   0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 18: Back-right
            { XMFLOAT3(0.5f,   0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 19: Front-right
            // Bottom face
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) }, // 20: Front-left
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) }, // 21: Back-left
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) }, // 22: Back-right
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) }  // 23: Front-right
        };

        // Tube Vertices (thin along X-axis)
        Vertex verticesTube[] = {
            // Front face
            { XMFLOAT3(-0.5f, -0.1f, -0.1f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.1f, -0.1f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.1f, -0.1f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f,  -0.1f, -0.1f), XMFLOAT2(1.0f, 1.0f) },
            // Back face
            { XMFLOAT3(-0.5f, -0.1f,  0.1f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.1f,  0.1f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.1f,  0.1f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f,  -0.1f,  0.1f), XMFLOAT2(1.0f, 1.0f) },
            // Left face
            { XMFLOAT3(-0.5f, -0.1f, -0.1f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.1f, -0.1f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-0.5f,  0.1f,  0.1f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-0.5f, -0.1f,  0.1f), XMFLOAT2(1.0f, 1.0f) },
            // Right face
            { XMFLOAT3(0.5f, -0.1f, -0.1f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.1f, -0.1f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,  0.1f,  0.1f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f, -0.1f,  0.1f), XMFLOAT2(1.0f, 1.0f) },
            // Top face
            { XMFLOAT3(-0.5f,  0.1f, -0.1f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.1f,  0.1f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.1f,  0.1f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.1f, -0.1f), XMFLOAT2(1.0f, 1.0f) },
            // Bottom face
            { XMFLOAT3(-0.5f, -0.1f, -0.1f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-0.5f, -0.1f,  0.1f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  -0.1f,  0.1f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3(0.5f,  -0.1f, -0.1f), XMFLOAT2(1.0f, 0.0f) }
        };

        // Ramp Vertices (sloping up along positive X)
        Vertex verticesRamp[] = {
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 0: Bottom-left-front
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 1: Bottom-left-back
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 2: Bottom-right-front
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 3: Bottom-right-back
            { XMFLOAT3(0.5f,   0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 4: Top-right-front
            { XMFLOAT3(0.5f,   0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }  // 5: Top-right-back
        };

        // SlabBottom Vertices (bottom half, y from -0.5 to 0.0)
        Vertex verticesSlabBottom[] = {
            // Front face
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.0f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.0f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Back face
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.0f,  0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.0f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Left face
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.0f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-0.5f,  0.0f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Right face
            { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.0f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,  0.0f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Top face
            { XMFLOAT3(-0.5f,  0.0f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.0f,  0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.0f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f,   0.0f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Bottom face
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) }
        };

        // SlabTop Vertices (top half, y from 0.0 to 0.5)
        Vertex verticesSlabTop[] = {
            // Front face
            { XMFLOAT3(-0.5f, 0.0f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f, 0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.0f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Back face
            { XMFLOAT3(-0.5f, 0.0f, 0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f, 0.5f, 0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.5f, 0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.0f, 0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Left face
            { XMFLOAT3(-0.5f, 0.0f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f, 0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-0.5f, 0.5f, 0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-0.5f, 0.0f, 0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Right face
            { XMFLOAT3(0.5f, 0.0f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(0.5f, 0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.5f, 0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.0f, 0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Top face
            { XMFLOAT3(-0.5f, 0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-0.5f, 0.5f, 0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.5f, 0.5f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(0.5f, 0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
            // Bottom face
            { XMFLOAT3(-0.5f, 0.0f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(-0.5f, 0.0f, 0.5f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(0.5f, 0.0f, 0.5f), XMFLOAT2(1.0f, 1.0f) },
            { XMFLOAT3(0.5f, 0.0f, -0.5f), XMFLOAT2(1.0f, 0.0f) }
        };

        // Pyramid Vertices (1 block tall)
        Vertex verticesPyramid[] = {
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 0: base front left
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 1: base back left
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 2: base back right
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 3: base front right
            { XMFLOAT3(0.0f,   0.5f,  0.0f), XMFLOAT2(0.5f, 0.5f) }  // 4: apex
        };

        // PyramidHalf Vertices (0.5 block tall)
        Vertex verticesPyramidHalf[] = {
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 0
            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 1
            { XMFLOAT3(0.5f,  -0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 2
            { XMFLOAT3(0.5f,  -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 3
            { XMFLOAT3(0.0f,   0.0f,  0.0f), XMFLOAT2(0.5f, 0.5f) }  // 4: apex at y=0
        };

        // Funnel Vertices (upside-down pyramid)
        Vertex verticesFunnel[] = {
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 0: base front left
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 1: base back left
            { XMFLOAT3(0.5f,   0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 2: base back right
            { XMFLOAT3(0.5f,   0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 3: base front right
            { XMFLOAT3(0.0f,  -0.5f,  0.0f), XMFLOAT2(0.5f, 0.5f) }  // 4: apex
        };

        // FunnelTop Vertices
        Vertex verticesFunnelTop[] = {
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) }, // 0: base front left
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) }, // 1: base back left
            { XMFLOAT3(0.5f,   0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) }, // 2: base back right
            { XMFLOAT3(0.5f,   0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) }, // 3: base front right
            { XMFLOAT3(0.0f,   0.0f,  0.0f), XMFLOAT2(0.5f, 0.5f) }  // 4: apex at y=0.0
        };

        // Indices for FullCube, Tube, SlabBottom, SlabTop (6 faces, 36 indices)
        UINT indices[] = {
            0,1,2, 0,2,3,       // Front face
            4,6,5, 4,7,6,       // Back face
            10,9,8, 11,10,8,    // Left face (reversed for clockwise winding)
            12,13,14, 12,14,15, // Right face
            16,17,18, 16,18,19, // Top face
            22,21,20, 23,22,20  // Bottom face (reversed for clockwise winding)
        };

        // Indices for Ramp (5 faces, 24 indices)
        UINT indicesRamp[] = {
            0,3,1, 0,2,3,       // Bottom face (reversed)
            0,1,5, 0,5,4,       // Sloped top face (unchanged)
            0,4,2,              // Left face (reversed)
            1,3,5,              // Right face (unchanged)
            5,3,2, 4,5,2        // Front face (reversed)
        };

        // Indices for Pyramids (4 faces, 12 indices)
        UINT indicesPyramid[] = {
            0, 4, 3, // Front face (clockwise from outside)
            3, 4, 2, // Right face
            2, 4, 1, // Back face
            1, 4, 0  // Left face
        };

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA sd = {};

        // FullCube Buffer
        bd.ByteWidth = sizeof(vertices);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        sd.pSysMem = vertices;
        hr = device->CreateBuffer(&bd, &sd, &vertexBuffer);
        if (FAILED(hr)) return hr;

        // Tube Buffer
        bd.ByteWidth = sizeof(verticesTube);
        sd.pSysMem = verticesTube;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferTube);
        if (FAILED(hr)) return hr;

        // Ramp Buffer
        bd.ByteWidth = sizeof(verticesRamp);
        sd.pSysMem = verticesRamp;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferRamp);
        if (FAILED(hr)) return hr;

        // SlabBottom Buffer
        bd.ByteWidth = sizeof(verticesSlabBottom);
        sd.pSysMem = verticesSlabBottom;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferSlabBottom);
        if (FAILED(hr)) return hr;

        // SlabTop Buffer
        bd.ByteWidth = sizeof(verticesSlabTop);
        sd.pSysMem = verticesSlabTop;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferSlabTop);
        if (FAILED(hr)) return hr;

        // Pyramid Buffer
        bd.ByteWidth = sizeof(verticesPyramid);
        sd.pSysMem = verticesPyramid;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferPyramid);
        if (FAILED(hr)) return hr;

        // PyramidHalf Buffer
        bd.ByteWidth = sizeof(verticesPyramidHalf);
        sd.pSysMem = verticesPyramidHalf;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferPyramidHalf);
        if (FAILED(hr)) return hr;

        // Funnel Buffer
        bd.ByteWidth = sizeof(verticesFunnel);
        sd.pSysMem = verticesFunnel;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferFunnel);
        if (FAILED(hr)) return hr;

        // FunnelTop Buffer
        bd.ByteWidth = sizeof(verticesFunnelTop);
        sd.pSysMem = verticesFunnelTop;
        hr = device->CreateBuffer(&bd, &sd, &vertexBufferFunnelTop);
        if (FAILED(hr)) return hr;

        // Index Buffer for FullCube, Tube, Slabs
        bd.ByteWidth = sizeof(indices);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        sd.pSysMem = indices;
        hr = device->CreateBuffer(&bd, &sd, &indexBuffer);
        if (FAILED(hr)) return hr;

        // Index Buffer for Ramp
        bd.ByteWidth = sizeof(indicesRamp);
        sd.pSysMem = indicesRamp;
        hr = device->CreateBuffer(&bd, &sd, &indexBufferRamp);
        if (FAILED(hr)) return hr;

        // Index Buffer for Pyramids
        bd.ByteWidth = sizeof(indicesPyramid);
        sd.pSysMem = indicesPyramid;
        hr = device->CreateBuffer(&bd, &sd, &indexBufferPyramid);
        if (FAILED(hr)) return hr;

        // Constant Buffer
        bd.ByteWidth = sizeof(ConstantBuffer);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&bd, nullptr, &constantBuffer);
        if (FAILED(hr)) return hr;

        // Load textures from the primative_sprites folder in Documents
        PWSTR documentsPathRaw = nullptr;
        hr = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPathRaw);
        if (FAILED(hr)) {
            return hr;
        }
        std::wstring documentsPath = documentsPathRaw;
        CoTaskMemFree(documentsPathRaw);

        for (int i = 0; i < 11; ++i) {
            // Construct file path (e.g., Documents\primative_sprites\block1.png)
            std::wstring filename = documentsPath + L"\\primative_sprites\\block" + std::to_wstring(i + 1) + L".png";
            Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(filename.c_str());
            if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
                if (bitmap) delete bitmap;
                return E_FAIL; // Fail if any texture cannot be loaded
            }

            // Verify texture size
            if (bitmap->GetWidth() != TEXTURE_SIZE || bitmap->GetHeight() != TEXTURE_SIZE) {
                delete bitmap;
                return E_FAIL; // Ensure textures are 32x32
            }

            Gdiplus::BitmapData bitmapData;
            Gdiplus::Rect rect(0, 0, TEXTURE_SIZE, TEXTURE_SIZE);
            hr = bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData);
            if (hr != Gdiplus::Ok) {
                delete bitmap;
                return E_FAIL;
            }

            D3D11_TEXTURE2D_DESC texDesc = {};
            texDesc.Width = TEXTURE_SIZE;
            texDesc.Height = TEXTURE_SIZE;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            if (bitmapData.Stride < 0) {
                bitmap->UnlockBits(&bitmapData);
                delete bitmap;
                return E_FAIL; // Negative stride is invalid
            }

            D3D11_SUBRESOURCE_DATA initData = { bitmapData.Scan0, static_cast<UINT>(bitmapData.Stride), 0 };
            ID3D11Texture2D* texture = nullptr;
            hr = device->CreateTexture2D(&texDesc, &initData, &texture);
            bitmap->UnlockBits(&bitmapData);
            if (FAILED(hr)) {
                delete bitmap;
                return hr;
            }

            hr = device->CreateShaderResourceView(texture, nullptr, &blockTextures[i]);
            texture->Release();
            delete bitmap;
            if (FAILED(hr)) return hr;
        }

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // Use point filtering for sharp textures
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;  // Clamp to avoid wrapping artifacts
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = 0; // Lock to mipmap level 0 since MipLevels = 1
        hr = device->CreateSamplerState(&sampDesc, &sampler);
        if (FAILED(hr)) return hr;

        vsBlob->Release();
        psBlob->Release();

        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constantBuffer);
        context->IASetInputLayout(inputLayout);
        context->PSSetSamplers(0, 1, &sampler);
        return S_OK;
    }

    void Cleanup() {
        if (sampler) sampler->Release();
        for (int i = 0; i < 11; ++i) if (blockTextures[i]) blockTextures[i]->Release();
        if (constantBuffer) constantBuffer->Release();
        if (indexBufferPyramid) indexBufferPyramid->Release();
        if (indexBufferRamp) indexBufferRamp->Release();
        if (indexBuffer) indexBuffer->Release();
        if (vertexBufferFunnelTop) vertexBufferFunnelTop->Release();
        if (vertexBufferFunnel) vertexBufferFunnel->Release();
        if (vertexBufferPyramidHalf) vertexBufferPyramidHalf->Release();
        if (vertexBufferPyramid) vertexBufferPyramid->Release();
        if (vertexBufferSlabTop) vertexBufferSlabTop->Release();
        if (vertexBufferSlabBottom) vertexBufferSlabBottom->Release();
        if (vertexBufferRamp) vertexBufferRamp->Release();
        if (vertexBufferTube) vertexBufferTube->Release();
        if (vertexBuffer) vertexBuffer->Release();
        if (inputLayout) inputLayout->Release();
        if (pixelShader) pixelShader->Release();
        if (vertexShader) vertexShader->Release();
    }
};

// ### InputManager
class InputManager {
public:
    bool IsKeyDown(int key) {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    POINT GetMouseDelta(HWND hwnd) {
        POINT current;
        GetCursorPos(&current);
        ScreenToClient(hwnd, &current);
        POINT delta;
        delta.x = current.x - WINDOW_WIDTH / 2;
        delta.y = current.y - WINDOW_HEIGHT / 2;
        POINT center = { WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 };
        ClientToScreen(hwnd, &center);
        SetCursorPos(center.x, center.y);
        return delta;
    }
};

// ### AudioManager and UIManager
class AudioManager {
public:
    void PlaySound(const std::string& soundName) {}
};

class UIManager {
public:
    void RenderUI(ID3D11DeviceContext* context) {}
};

// ### GameStateManager
enum class GameState { MainMenu, InGame, Paused, GameOver };
class GameStateManager {
public:
    GameState currentState = GameState::InGame;
    void SetState(GameState state) { currentState = state; }
    GameState GetState() const { return currentState; }
};

// ### Entity Classes
class Entity {
public:
    virtual void Update(HWND hwnd, InputManager* input) {}
    virtual void Render(ID3D11DeviceContext* context, ResourceManager* res,
        const XMMATRIX& view, const XMMATRIX& proj) {
    }
    virtual ~Entity() {}
};

class EntityManager {
public:
    std::vector<Entity*> entities;
    ~EntityManager() {
        for (Entity* e : entities) delete e;
        entities.clear();
    }
    void AddEntity(Entity* entity) { entities.push_back(entity); }
    void UpdateEntities(HWND hwnd, InputManager* input) {
        for (Entity* e : entities) e->Update(hwnd, input);
    }
    void RenderEntities(ID3D11DeviceContext* context, ResourceManager* res,
        const XMMATRIX& view, const XMMATRIX& proj) {
        for (Entity* e : entities) e->Render(context, res, view, proj);
    }
};

// ### Grid
class Grid : public Entity {
    std::vector<int> grid;
    int currentType = 1;
    int GetIndex(int x, int y, int z) const {
        return x + y * GRID_WIDTH + z * GRID_WIDTH * GRID_HEIGHT;
    }

    enum class ShapeType { FullCube, Tube, Ramp, SlabBottom, SlabTop, Pyramid, PyramidHalf, Funnel, FunnelTop };

    ShapeType GetShapeFromType(int type) const {
        switch (type) {
        case 2: return ShapeType::Tube;
        case 3: return ShapeType::Ramp;
        case 4: return ShapeType::SlabBottom;
        case 5: return ShapeType::SlabTop;
        case 6: return ShapeType::Pyramid;
        case 7: return ShapeType::PyramidHalf;
        case 8: return ShapeType::Funnel;
        case 9: return ShapeType::Pyramid; // Reuse Pyramid for variety
        case 10: return ShapeType::FunnelTop; // Reuse Funnel for variety
        default: return ShapeType::FullCube; // Type 1
        }
    }

public:
    Grid() : grid(GRID_WIDTH* GRID_HEIGHT* GRID_DEPTH, 0) {
        for (int x = 0; x < GRID_WIDTH; ++x) {
            for (int z = 0; z < GRID_DEPTH; ++z) {
                // Ground layer: keep it simple, all type 1
                grid[GetIndex(x, 0, z)] = 1;
                // Upper layer: blocks every 2 units, cycle types 1 to 10
                if (x % 2 == 0 && z % 2 == 0) {
                    grid[GetIndex(x, 1, z)] = (x / 2 + z / 2) % 10 + 1;
                }
            }
        }
    }

    bool IsSolid(int x, int y, int z) const {
        return (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT ||
            z < 0 || z >= GRID_DEPTH) || (grid[GetIndex(x, y, z)] != 0);
    }

    void PlaceBlock(int x, int y, int z) {
        if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT &&
            z >= 0 && z < GRID_DEPTH)
            grid[GetIndex(x, y, z)] = currentType;
    }

    void RemoveBlock(int x, int y, int z) {
        if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT &&
            z >= 0 && z < GRID_DEPTH)
            grid[GetIndex(x, y, z)] = 0;
    }

    void SetCurrentType(int type) {
        currentType = std::max(1, std::min(10, type));
    }
    int GetCurrentType() const { return currentType; }

    bool Raycast(XMFLOAT3 start, XMVECTOR dir, float maxDist,
        XMFLOAT3& hitPos, XMFLOAT3& normal) const {
        XMFLOAT3 rayDir;
        XMStoreFloat3(&rayDir, XMVector3Normalize(dir));
        float t = 0.0f;
        const float step = 0.1f;
        while (t < maxDist) {
            XMFLOAT3 pos = { start.x + rayDir.x * t,
                             start.y + rayDir.y * t,
                             start.z + rayDir.z * t };
            int x = (int)(pos.x + GRID_WIDTH / 2.0f);
            int y = (int)pos.y;
            int z = (int)(pos.z + GRID_DEPTH / 2.0f);
            if (IsSolid(x, y, z)) {
                hitPos = pos;
                XMFLOAT3 prevPos = { start.x + rayDir.x * (t - step),
                                     start.y + rayDir.y * (t - step),
                                     start.z + rayDir.z * (t - step) };
                int px = (int)(prevPos.x + GRID_WIDTH / 2.0f);
                int py = (int)prevPos.y;
                int pz = (int)(prevPos.z + GRID_DEPTH / 2.0f);
                normal = { (px != x) ? (px > x ? 1.0f : -1.0f) : 0.0f,
                           (py != y) ? (py > y ? 1.0f : -1.0f) : 0.0f,
                           (pz != z) ? (pz > z ? 1.0f : -1.0f) : 0.0f };
                return true;
            }
            t += step;
        }
        return false;
    }

    virtual void Render(ID3D11DeviceContext* context, ResourceManager* res,
        const XMMATRIX& view, const XMMATRIX& proj) override {
        ConstantBuffer cb;
        cb.view = view;
        cb.proj = proj;
        UINT stride = static_cast<UINT>(sizeof(XMFLOAT3) + sizeof(XMFLOAT2));
        UINT offset = 0;
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (int x = 0; x < GRID_WIDTH; ++x) {
            for (int y = 0; y < GRID_HEIGHT; ++y) {
                for (int z = 0; z < GRID_DEPTH; ++z) {
                    int type = grid[GetIndex(x, y, z)];
                    if (type >= 1 && type <= 10) {
                        ShapeType shape = GetShapeFromType(type);
                        ID3D11Buffer* vb = nullptr;
                        ID3D11Buffer* ib = nullptr;
                        UINT indexCount = 0;

                        switch (shape) {
                        case ShapeType::FullCube:
                            vb = res->vertexBuffer;
                            ib = res->indexBuffer;
                            indexCount = 36;
                            break;
                        case ShapeType::Tube:
                            vb = res->vertexBufferTube;
                            ib = res->indexBuffer;
                            indexCount = 36;
                            break;
                        case ShapeType::Ramp:
                            vb = res->vertexBufferRamp;
                            ib = res->indexBufferRamp;
                            indexCount = 24;
                            break;
                        case ShapeType::SlabBottom:
                            vb = res->vertexBufferSlabBottom;
                            ib = res->indexBuffer;
                            indexCount = 36;
                            break;
                        case ShapeType::SlabTop:
                            vb = res->vertexBufferSlabTop;
                            ib = res->indexBuffer;
                            indexCount = 36;
                            break;
                        case ShapeType::Pyramid:
                            vb = res->vertexBufferPyramid;
                            ib = res->indexBufferPyramid;
                            indexCount = 12;
                            break;
                        case ShapeType::PyramidHalf:
                            vb = res->vertexBufferPyramidHalf;
                            ib = res->indexBufferPyramid;
                            indexCount = 12;
                            break;
                        case ShapeType::Funnel:
                            vb = res->vertexBufferFunnel;
                            ib = res->indexBufferPyramid;
                            indexCount = 12;
                            break;
                        case ShapeType::FunnelTop:
                            vb = res->vertexBufferFunnelTop;
                            ib = res->indexBufferPyramid;
                            indexCount = 12;
                            break;
                        }

                        cb.world = XMMatrixTranslation(
                            (float)x - GRID_WIDTH / 2.0f,
                            (float)y,
                            (float)z - GRID_DEPTH / 2.0f);
                        context->UpdateSubresource(res->constantBuffer, 0, nullptr, &cb, 0, 0);
                        context->PSSetShaderResources(0, 1, &res->blockTextures[type - 1]);
                        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                        context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
                        context->DrawIndexed(indexCount, 0, 0);
                    }
                }
            }
        }
    }
};

// ### Player
class Player : public Entity {
    XMFLOAT3 position = { 0.0f, 5.0f, -10.0f };
    float yaw = 0.0f, pitch = 0.0f, verticalVelocity = 0.0f;
    Grid* grid;
public:
    Player(Grid* g) : grid(g) {}

    bool IsOnGround() const {
        int x = (int)(position.x + GRID_WIDTH / 2.0f);
        int y = (int)(position.y - 0.1f);
        int z = (int)(position.z + GRID_DEPTH / 2.0f);
        return grid->IsSolid(x, y, z);
    }

    bool CheckCollision(const XMFLOAT3& pos) const {
        for (float h : { 0.0f, PLAYER_HEIGHT }) {
            int gridX = (int)(pos.x + GRID_WIDTH / 2.0f);
            int gridY = (int)(pos.y + h);
            int gridZ = (int)(pos.z + GRID_DEPTH / 2.0f);
            if (grid->IsSolid(gridX, gridY, gridZ))
                return true;
        }
        return false;
    }

    virtual void Update(HWND hwnd, InputManager* input) override {
        POINT delta = input->GetMouseDelta(hwnd);
        yaw += delta.x * 0.005f;
        pitch -= delta.y * 0.005f;
        pitch = std::max(-XM_PI / 2.0f + 0.01f, std::min(XM_PI / 2.0f - 0.01f, pitch));

        XMVECTOR forward = XMVectorSet(sinf(yaw), 0.0f, cosf(yaw), 0.0f);
        XMVECTOR rightVec = XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward);
        XMVECTOR vel = XMVectorZero();
        if (input->IsKeyDown('W'))
            vel = XMVectorAdd(vel, XMVectorScale(forward, MOVE_SPEED));
        if (input->IsKeyDown('S'))
            vel = XMVectorSubtract(vel, XMVectorScale(forward, MOVE_SPEED));
        if (input->IsKeyDown('A'))
            vel = XMVectorSubtract(vel, XMVectorScale(rightVec, MOVE_SPEED));
        if (input->IsKeyDown('D'))
            vel = XMVectorAdd(vel, XMVectorScale(rightVec, MOVE_SPEED));

        XMFLOAT3 newPos = position;
        {
            XMFLOAT3 temp = newPos;
            temp.x += XMVectorGetX(vel);
            if (!CheckCollision(temp))
                newPos.x = temp.x;
        }
        {
            XMFLOAT3 temp = newPos;
            temp.z += XMVectorGetZ(vel);
            if (!CheckCollision(temp))
                newPos.z = temp.z;
        }

        if (IsOnGround()) {
            if (input->IsKeyDown(VK_SPACE))
                verticalVelocity = JUMP_VELOCITY;
            else
                verticalVelocity = 0;
        }
        else {
            verticalVelocity += GRAVITY;
        }
        XMFLOAT3 temp = newPos;
        temp.y += verticalVelocity;
        int gridX = (int)(position.x + GRID_WIDTH / 2.0f);
        int gridZ = (int)(position.z + GRID_DEPTH / 2.0f);
        if (verticalVelocity < 0) {
            int gridYFeet = (int)(temp.y);
            if (grid->IsSolid(gridX, gridYFeet, gridZ)) {
                newPos.y = (float)(gridYFeet + 1);
                verticalVelocity = 0;
            }
            else {
                newPos.y = temp.y;
            }
        }
        else if (verticalVelocity > 0) {
            int gridYHead = (int)(temp.y + PLAYER_HEIGHT);
            if (grid->IsSolid(gridX, gridYHead, gridZ)) {
                newPos.y = (float)gridYHead - PLAYER_HEIGHT - 0.001f;
                verticalVelocity = 0;
            }
            else {
                newPos.y = temp.y;
            }
        }
        else {
            newPos.y = temp.y;
        }
        position = newPos;

        bool leftClick = input->IsKeyDown(VK_LBUTTON);
        bool rightClick = input->IsKeyDown(VK_RBUTTON);
        if (leftClick || rightClick) {
            XMFLOAT3 hit, normal;
            XMVECTOR forwardVec = XMVector3Normalize(
                XMVectorSet(sinf(yaw) * cosf(pitch), sinf(pitch),
                    cosf(yaw) * cosf(pitch), 0.0f)
            );
            if (grid->Raycast(position, forwardVec, 5.0f, hit, normal)) {
                int x = (int)(hit.x + GRID_WIDTH / 2.0f);
                int y = (int)hit.y;
                int z = (int)(hit.z + GRID_DEPTH / 2.0f);
                if (rightClick)
                    grid->RemoveBlock(x, y, z);
                else if (leftClick && !CheckCollision({ hit.x + normal.x, hit.y + normal.y, hit.z + normal.z }))
                    grid->PlaceBlock(x + (int)normal.x, y + (int)normal.y, z + (int)normal.z);
            }
        }
    }

    virtual void Render(ID3D11DeviceContext*, ResourceManager*, const XMMATRIX&, const XMMATRIX&) override {}

    XMFLOAT3 GetPosition() const { return position; }
    float GetYaw() const { return yaw; }
    float GetPitch() const { return pitch; }
    XMVECTOR GetForwardVector() const {
        return XMVector3Normalize(
            XMVectorSet(sinf(yaw) * cosf(pitch), sinf(pitch),
                cosf(yaw) * cosf(pitch), 0.0f)
        );
    }
};

// TOOL/WEAPON
class Tool : public Entity {
    Player* player; // store pointer to the player so we can query its state
public:
    Tool(Player* p) : player(p) {}

    virtual void Render(ID3D11DeviceContext* context, ResourceManager* res,
        const XMMATRIX& view, const XMMATRIX& proj) override {
        XMFLOAT3 pPos = player->GetPosition();
        float yaw = player->GetYaw();
        float pitch = player->GetPitch();

        XMVECTOR forward = player->GetForwardVector();
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        XMVECTOR right = XMVector3Cross(forward, up);

        XMVECTOR offset = XMVectorAdd(XMVectorScale(right, 0.5f),
            XMVectorScale(up, -0.5f));
        offset = XMVectorAdd(offset, XMVectorScale(forward, 1.0f));

        XMVECTOR toolPos = XMVectorAdd(XMLoadFloat3(&pPos), offset);

        XMMATRIX toolRotation = XMMatrixRotationRollPitchYaw(pitch, yaw, 0);
        XMMATRIX toolTranslation = XMMatrixTranslationFromVector(toolPos);
        XMMATRIX stickRotation = XMMatrixRotationY(-XM_PI / 2.0f);
        XMMATRIX world = stickRotation * toolRotation * toolTranslation;

        ConstantBuffer cb;
        cb.world = world;
        cb.view = view;
        cb.proj = proj;
        context->UpdateSubresource(res->constantBuffer, 0, nullptr, &cb, 0, 0);

        UINT stride = static_cast<UINT>(sizeof(XMFLOAT3) + sizeof(XMFLOAT2));
        UINT offsetVal = 0;
        ID3D11Buffer* vb = res->vertexBufferTube;
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offsetVal);

        context->IASetIndexBuffer(res->indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        context->PSSetShaderResources(0, 1, &res->blockTextures[10]);
        context->DrawIndexed(36, 0, 0);
    }
};

// ### Camera
class Camera {
public:
    XMFLOAT3 position;
    float yaw, pitch;
    Camera() : position(0.0f, 5.0f, -10.0f), yaw(0.0f), pitch(0.0f) {}
    XMMATRIX GetViewMatrix() const {
        XMVECTOR pos = XMLoadFloat3(&position);
        XMVECTOR forward = XMVector3Normalize(
            XMVectorSet(sinf(yaw) * cosf(pitch), sinf(pitch),
                cosf(yaw) * cosf(pitch), 0.0f)
        );
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        return XMMatrixLookAtLH(pos, XMVectorAdd(pos, forward), up);
    }
    XMMATRIX GetProjMatrix() const {
        return XMMatrixPerspectiveFovLH(XM_PI / 4.0f,
            (float)WINDOW_WIDTH / WINDOW_HEIGHT,
            0.1f, 1000.0f);
    }
    void UpdateFromPlayer(const Player& player) {
        position = player.GetPosition();
        yaw = player.GetYaw();
        pitch = player.GetPitch();
    }
};

// ### Game
class Game {
public:
    HWND hwnd;
    Game() : hwnd(nullptr) {}
    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;
    ID3D11DepthStencilView* depthStencil = nullptr;

    ResourceManager resourceManager;
    InputManager inputManager;
    AudioManager audioManager;
    UIManager uiManager;
    GameStateManager stateManager;
    EntityManager entityManager;
    Camera camera;

    Grid* grid = nullptr;
    Player* player = nullptr;

    HRESULT Init(HWND hwnd) {
        this->hwnd = hwnd;
        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferDesc.Width = WINDOW_WIDTH;
        scd.BufferDesc.Height = WINDOW_HEIGHT;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 0;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 1;
        scd.OutputWindow = hwnd;
        scd.SampleDesc.Count = 1;
        scd.SampleDesc.Quality = 0;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
            nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &scd,
            &swapChain, &device, nullptr, &context);
        if (FAILED(hr)) return hr;

        ID3D11Texture2D* backBuffer = nullptr;
        hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) return hr;
        hr = device->CreateRenderTargetView(backBuffer, nullptr, &renderTarget);
        backBuffer->Release();
        if (FAILED(hr)) return hr;

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = WINDOW_WIDTH;
        depthDesc.Height = WINDOW_HEIGHT;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        ID3D11Texture2D* depthStencilTex = nullptr;
        hr = device->CreateTexture2D(&depthDesc, nullptr, &depthStencilTex);
        if (FAILED(hr)) return hr;
        hr = device->CreateDepthStencilView(depthStencilTex, nullptr, &depthStencil);
        depthStencilTex->Release();
        if (FAILED(hr)) return hr;

        context->OMSetRenderTargets(1, &renderTarget, depthStencil);

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        vp.Width = (FLOAT)WINDOW_WIDTH;
        vp.Height = (FLOAT)WINDOW_HEIGHT;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context->RSSetViewports(1, &vp);

        hr = resourceManager.Init(device, context);
        if (FAILED(hr)) return hr;

        grid = new Grid();
        player = new Player(grid);
        entityManager.AddEntity(grid);
        entityManager.AddEntity(player);
        Tool* tool = new Tool(player);
        entityManager.AddEntity(tool);

        return S_OK;
    }

    void Update() {
        if (stateManager.GetState() == GameState::InGame) {
            entityManager.UpdateEntities(hwnd, &inputManager);
            camera.UpdateFromPlayer(*player);
        }
    }

    void Render() {
        float clearColor[] = { 0.5f, 0.7f, 1.0f, 1.0f };
        context->ClearRenderTargetView(renderTarget, clearColor);
        context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH, 1.0f, 0);
        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjMatrix();
        entityManager.RenderEntities(context, &resourceManager, view, proj);
        uiManager.RenderUI(context);
        swapChain->Present(1, 0);
    }

    void Cleanup() {
        resourceManager.Cleanup();
        if (depthStencil) depthStencil->Release();
        if (renderTarget) renderTarget->Release();
        if (context) context->Release();
        if (device) device->Release();
        if (swapChain) swapChain->Release();
    }
};

// ### Window Procedure and Main
Game* g_game = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_game = new Game();
        if (FAILED(g_game->Init(hwnd)))
            return -1;
        SetTimer(hwnd, 1, 16, nullptr);
        ShowCursor(FALSE);
        return 0;
    case WM_DESTROY:
        ShowCursor(TRUE);
        KillTimer(hwnd, 1);
        if (g_game) {
            g_game->Cleanup();
            delete g_game;
            g_game = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (g_game->stateManager.GetState() == GameState::InGame) {
                g_game->stateManager.SetState(GameState::Paused);
                ShowCursor(TRUE);
                ClipCursor(NULL);
            }
            else if (g_game->stateManager.GetState() == GameState::Paused) {
                g_game->stateManager.SetState(GameState::InGame);
                ShowCursor(FALSE);
                RECT rect;
                GetClientRect(hwnd, &rect);
                POINT ul = { rect.left, rect.top };
                POINT lr = { rect.right, rect.bottom };
                ClientToScreen(hwnd, &ul);
                ClientToScreen(hwnd, &lr);
                rect.left = ul.x; rect.top = ul.y;
                rect.right = lr.x; rect.bottom = lr.y;
                ClipCursor(&rect);
            }
        }
        if (g_game && g_game->stateManager.GetState() == GameState::InGame) {
            if ((int)wParam >= '1' && (int)wParam <= '9')
                g_game->grid->SetCurrentType((int)wParam - '0');
            else if ((int)wParam == '0')
                g_game->grid->SetCurrentType(10);
        }
        return 0;
    case WM_TIMER:
        if (g_game) {
            g_game->Update();
            g_game->Render();
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VoxelGame";
    RegisterClassEx(&wc);

    RECT wr = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindow(
        L"VoxelGame", L"Voxel Game",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}