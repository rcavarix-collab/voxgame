// main.cpp
//
// Voxistics - Milestone 1 prototype. Entry point only: window creation,
// startup sequencing, and the fixed-timestep game loop. Everything the
// loop drives lives in its own module now (world.h/world.cpp,
// render.h/render.cpp, audio.h/audio.cpp, persist.h/persist.cpp,
// game.h/game.cpp) -- see DESIGN.md for the full design and Part XV for
// the build.

// MSVC's windows.h defines min/max function-like macros unless this is
// set first -- without it, any bare std::min/std::max call anywhere in
// this project would silently break at the token that happens to be
// followed by '('.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cmath>

#include "common.h"
#include "world.h"
#include "render.h"
#include "audio.h"
#include "persist.h"
#include "game.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Wide (W-suffixed) throughout, deliberately -- mixing an ANSI-
    // registered window (RegisterClassA/CreateWindowA) with the wide
    // DefWindowProcW that the project's Unicode character-set setting
    // makes the unsuffixed DefWindowProc macro expand to is a known
    // Win32 mismatch that corrupts non-client text (the title bar):
    // it's what produced the garbled CJK-looking title before this.
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VoxisticsWindowClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT wr = { 0, 0, SCREEN_W, SCREEN_H };
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    AdjustWindowRect(&wr, style, FALSE);
    g_hwnd = CreateWindowW(L"VoxisticsWindowClass", L"Voxistics",
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            wr.right - wr.left, wr.bottom - wr.top,
                            nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return -1;
    ShowWindow(g_hwnd, nCmdShow);

    LoadSettings(); // before anything reads g_sensitivityMultX/g_loadRadius/g_masterVolume/etc.
    MigrateLegacySingleSaveIfPresent(); // before the title screen's slot picker can show slot 1

    // XAudio2Create requires COM initialized on the calling thread.
    // Nothing else in this file has needed that so far (SHGetKnownFolderPath
    // manages its own COM state internally), so this is the first call
    // that actually needs it.
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(comHr);

    if (!InitD3D(g_hwnd)) return -1;
    if (!InitTextures()) return -1;
    InitAudio(); // a machine with no usable audio device still gets a silent but playable game (Section 10)
    BuildSkyMesh();

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);
    const float FIXED_DT = 1.0f / 60.0f;
    float accumulator = 0.0f;

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - lastTime.QuadPart) / (float)freq.QuadPart;
        lastTime = now;
        if (dt > 0.25f) dt = 0.25f; // clamp huge stalls (e.g. window drag)
        accumulator += dt;

        if (g_toastTimer > 0.0f) {
            g_toastTimer -= dt;
            if (g_toastTimer < 0.0f) g_toastTimer = 0.0f;
        }
        if (g_confirmOverwriteSlot != -1) {
            g_confirmOverwriteTimer -= dt;
            if (g_confirmOverwriteTimer <= 0.0f) g_confirmOverwriteSlot = -1; // armed confirm expired; next click re-arms instead of overwriting
        }

        g_fpsFrameCount++;
        g_fpsTimer += dt;
        if (g_fpsTimer >= 1.0f) {
            g_fpsDisplay = g_fpsFrameCount;
            g_fpsFrameCount = 0;
            g_fpsTimer -= 1.0f;
        }

        // The foreground check is defense-in-depth alongside the
        // WM_KILLFOCUS handler above: without it, a focus change this
        // same frame that WM_KILLFOCUS hasn't been dispatched for yet
        // would still let this recenter the real cursor into the
        // window while some other application is what's actually
        // focused.
        if (g_mouseCaptured && GetForegroundWindow() == g_hwnd) {
            POINT cursor; GetCursorPos(&cursor);
            RECT rc; GetClientRect(g_hwnd, &rc);
            POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
            ClientToScreen(g_hwnd, &center);
            int dx = cursor.x - center.x, dy = cursor.y - center.y;
            float sensX = BASE_MOUSE_SENS * g_sensitivityMultX;
            float sensY = BASE_MOUSE_SENS * g_sensitivityMultY;
            g_player.yaw += (g_invertX ? -dx : dx) * sensX;
            g_player.pitch += (g_invertY ? dy : -dy) * sensY;
            if (g_player.pitch > 1.55f) g_player.pitch = 1.55f;
            if (g_player.pitch < -1.55f) g_player.pitch = -1.55f;
            SetCursorPos(center.x, center.y);
        }

        // Fixed-timestep simulation, decoupled from render/present rate
        // (Section 5.3's recommended accumulator approach). While the
        // pause menu is open the world is frozen and the accumulator is
        // dropped rather than left to build up, so resuming doesn't
        // trigger a burst of catch-up ticks for however long it was paused.
        if (g_menuScreen != MenuScreen::None) {
            accumulator = 0.0f;
        } else {
            while (accumulator >= FIXED_DT) {
                // Day clock (Section 13): advances only here, gated
                // identically to every other simulation system -- the
                // single authoritative source of "what time is it,"
                // which the music (Part XIV) reads directly rather than
                // tracking its own independent notion of time.
                g_dayTimeSeconds = fmodf(g_dayTimeSeconds + FIXED_DT, DAY_LENGTH_SECONDS);
                RefillMusicQueueIfNeeded();

                int pcx = FloorDiv16((int)floor(g_player.x));
                int pcz = FloorDiv16((int)floor(g_player.z));
                EnsureChunksLoaded(pcx, pcz);
                ProcessColumnGeneration(g_world);
                ProcessColumnEviction(g_world);

                bool fwd = IsActionDown(ACT_FORWARD), back = IsActionDown(ACT_BACK);
                bool left = IsActionDown(ACT_LEFT), right = IsActionDown(ACT_RIGHT);
                bool jump = IsActionDown(ACT_JUMP);
                UpdatePlayerPhysics(g_world, g_player, FIXED_DT, fwd, back, left, right, jump);
                ProcessFalls(g_world);

                accumulator -= FIXED_DT;
            }
        }

        RebuildDirtyChunks(g_world);

        float clearColor[4] = { 0.4f, 0.6f, 0.9f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, g_dsv);
        g_context->ClearRenderTargetView(g_rtv, clearColor);
        g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        g_context->RSSetState(g_rasterState);
        g_context->OMSetDepthStencilState(g_depthState, 0);

        Vec3 f, r, u;
        GetCameraVectors(g_player, f, r, u);
        Vec3 eye = { g_player.x, g_player.y + PLAYER_EYE, g_player.z };
        Mat4 view = MatLookToLH(eye, f, u);
        // g_fov (Accessibility, Section 11) is stored in degrees since
        // that's the meaningful unit for a player-facing slider; 45 deg
        // is this constant's old fixed value, unchanged until the
        // slider is touched.
        float fovRadians = g_fov * (3.14159265359f / 180.0f);
        Mat4 proj = MatPerspectiveFovLH(fovRadians, (float)SCREEN_W / SCREEN_H, 0.1f, 500.0f);
        Mat4 viewProj = MatMul(view, proj);
        Frustum frustum = ExtractFrustum(viewProj);

        // Sky pass: depth off (reusing the UI pass's depth-disabled
        // state), drawn before the opaque world pass so normal depth-
        // tested geometry always overdraws it regardless of the sky
        // box's actual size. Its view matrix drops the eye position
        // (rotation only) so the sky rotates with the camera but never
        // translates with it, same as any conventional skybox.
        {
            Mat4 skyView = MatLookToLH({ 0, 0, 0 }, f, u);
            Mat4 skyViewProj = MatMul(skyView, proj);
            g_context->OMSetDepthStencilState(g_uiDepthState, 0);
            g_context->VSSetShader(g_skyVS, nullptr, 0);
            g_context->PSSetShader(g_skyPS, nullptr, 0);
            g_context->IASetInputLayout(g_skyLayout);
            g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_context->VSSetConstantBuffers(0, 1, &g_skyCBuffer);
            D3D11_MAPPED_SUBRESOURCE mapped;
            g_context->Map(g_skyCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            *(Mat4*)mapped.pData = skyViewProj;
            g_context->Unmap(g_skyCBuffer, 0);
            UINT skyStride = sizeof(SkyVertex), skyOffset = 0;
            g_context->IASetVertexBuffers(0, 1, &g_skyVB, &skyStride, &skyOffset);
            g_context->IASetIndexBuffer(g_skyIB, DXGI_FORMAT_R32_UINT, 0);
            g_context->DrawIndexed(g_skyIndexCount, 0, 0);
            g_context->OMSetDepthStencilState(g_depthState, 0);
        }

        g_context->VSSetShader(g_vs, nullptr, 0);
        g_context->PSSetShader(g_ps, nullptr, 0);
        g_context->IASetInputLayout(g_layout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetConstantBuffers(0, 1, &g_cbuffer);
        g_context->PSSetSamplers(0, 1, &g_sampler);

        UpdateCBuffer(viewProj);
        g_context->PSSetShaderResources(0, 1, &g_atlasSRV);
        UINT stride = sizeof(Vertex), offset = 0;
        for (auto& kv : g_world.chunks) {
            Chunk& c = *kv.second;
            if (c.indexCount == 0) continue;
            const ChunkCoord& cc = kv.first;
            Vec3 minB = { (float)(cc.x * CHUNK_SIZE), (float)(cc.y * CHUNK_SIZE), (float)(cc.z * CHUNK_SIZE) };
            Vec3 maxB = { minB.x + CHUNK_SIZE, minB.y + CHUNK_SIZE, minB.z + CHUNK_SIZE };
            if (!FrustumIntersectsAABB(frustum, minB, maxB)) continue;
            g_context->IASetVertexBuffers(0, 1, &c.vb, &stride, &offset);
            g_context->IASetIndexBuffer(c.ib, DXGI_FORMAT_R32_UINT, 0);
            g_context->DrawIndexed(c.indexCount, 0, 0);
        }

        RenderUIPass();

        g_swapChain->Present(1, 0);
    }

    ShutdownAudio();
    if (comInitialized) CoUninitialize();
    return 0;
}
