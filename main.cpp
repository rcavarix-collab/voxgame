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
#include "profiler.h"
#include "theline.h"

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

    // Per-monitor DPI aware, so Windows never bitmap-stretches (blurs) the
    // window on a scaled display. Looked up dynamically: the V2 context
    // needs Windows 10 1703+, with the Vista-era call as the fallback.
    {
        typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        SetCtxFn setCtx = user32 ? (SetCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
        if (!setCtx || !setCtx((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */)) SetProcessDPIAware();
    }

    RECT wr = { 0, 0, DEFAULT_WINDOW_W, DEFAULT_WINDOW_H };
    DWORD style = WS_OVERLAPPEDWINDOW; // resizable and maximisable; the backbuffer follows (WM_SIZE)
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
    if (g_fullscreen) ApplyFullscreen(true); // saved preference
    std::string textureProblems;
    if (!InitTextures(textureProblems)) return -1;
    if (!textureProblems.empty()) ShowToast(textureProblems, 8.0f);
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
        // The profiler closes the previous frame with its true, unclamped
        // length -- a hitch is exactly what it is there to show.
        ProfEndFrame(dt);
        ProfBeginFrame();
        if (dt > 0.25f) dt = 0.25f; // clamp huge stalls (e.g. window drag)
        accumulator += dt;

        TickAutosave(dt);
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

                int pcx = FloorDiv16((int)floor(g_player.x));
                int pcz = FloorDiv16((int)floor(g_player.z));
                {
                    ProfScope prof(PROF_TERRAIN);
                    EnsureChunksLoaded(pcx, pcz);
                    ProcessColumnGeneration(g_world);
                }
                {
                    ProfScope prof(PROF_EVICT);
                    ProcessColumnEviction(g_world);
                }

                bool fwd = IsActionDown(ACT_FORWARD), back = IsActionDown(ACT_BACK);
                bool left = IsActionDown(ACT_LEFT), right = IsActionDown(ACT_RIGHT);
                bool jump = IsActionDown(ACT_JUMP);
                {
                    ProfScope prof(PROF_PHYSICS);
                    UpdatePlayerPhysics(g_world, g_player, FIXED_DT, fwd, back, left, right, jump);
                }
                {
                    ProfScope prof(PROF_UPDATES);
                    ProcessScheduledUpdates(g_world);
                }
                UpdateLine(g_line, g_lineTuning, g_player.x, g_player.y, g_player.z, FIXED_DT);

                accumulator -= FIXED_DT;
            }
        }

        // Once per frame, not per tick: after a stall the tick loop runs
        // many catch-up ticks in one frame, and each would otherwise
        // generate another music chunk on top of the stall.
        if (g_menuScreen == MenuScreen::None) {
            ProfScope prof(PROF_MUSIC);
            RefillMusicQueueIfNeeded();
        }
        {
            ProfScope prof(PROF_MESH);
            RebuildDirtyChunks(g_world, FloorDiv16((int)floorf(g_player.x)),
                               FloorDiv16((int)floorf(g_player.y + PLAYER_EYE)), FloorDiv16((int)floorf(g_player.z)));
        }
        ProfSetCounter(PCOUNT_CHUNKS_RESIDENT, (int64_t)g_world.chunks.size());
        ProfSetCounter(PCOUNT_DIRTY_WAITING, (int64_t)g_world.dirtyChunks.size());
        ProfSetCounter(PCOUNT_COLUMNS_WAITING, (int64_t)g_pendingColumns.size());
        ProfSetCounter(PCOUNT_UPDATES_WAITING, (int64_t)ScheduledUpdateCount());
        Vec3 f, r, u;
        GetCameraVectors(g_player, f, r, u);
        Vec3 eye = { g_player.x, g_player.y + PLAYER_EYE, g_player.z };
        Mat4 view = MatLookToLH(eye, f, u);
        // g_fov (Accessibility, Section 11) is stored in degrees since
        // that's the meaningful unit for a player-facing slider.
        float fovRadians = g_fov * (3.14159265359f / 180.0f);
        Mat4 proj = MatPerspectiveFovLH(fovRadians, (float)g_screenW / g_screenH, 0.1f, 500.0f);
        RenderScene(g_world, view, proj, eye, f, u, g_dayTimeSeconds);

        {
            ProfScope prof(PROF_UI);
            RenderUIPass();
        }

        {
            ProfScope prof(PROF_PRESENT);
            g_swapChain->Present(1, 0);
        }
    }

    ShutdownAudio();
    if (comInitialized) CoUninitialize();
    return 0;
}
