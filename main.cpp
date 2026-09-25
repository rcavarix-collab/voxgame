// main.cpp
//
// Cacophony: entry point only (docs/ARCHITECTURE.md 1.3). The window
// (per-monitor DPI aware, resizable, borderless fullscreen on F11), mouse
// capture with raw input while playing, and the loop: a fixed 60-tick
// simulation (at most five ticks a frame, so a stall never feeds itself),
// then one frame of meshing, drawing and presenting, capped to the chosen
// frame rate. Everything the loop drives lives in its own file.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h> // timeBeginPeriod
#include "common.h"
#include "game.h"
#include "input.h"
#include "persist.h"
#include "profiler.h"
#include "render.h"

namespace {
HWND g_hwnd = nullptr;
bool g_captured = false;
WINDOWPLACEMENT g_windowedPlacement = {}; // .length set before use

// Mouse capture: hidden cursor held inside the window, movement from raw
// input. Released whenever the game pauses or loses focus, so the cursor is
// never stolen from other programs (Prismative.cpp's flaw, not repeated).
void Capture(bool on) {
    if (on == g_captured) return;
    g_captured = on;
    if (on) {
        RECT rc; GetClientRect(g_hwnd, &rc);
        POINT tl = { rc.left, rc.top }, br = { rc.right, rc.bottom };
        ClientToScreen(g_hwnd, &tl); ClientToScreen(g_hwnd, &br);
        RECT clip = { tl.x, tl.y, br.x, br.y };
        ClipCursor(&clip);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

void SetPaused(bool paused) {
    GameSetPaused(paused);
    Capture(!paused && GetForegroundWindow() == g_hwnd);
}

void ApplyFullscreen(bool on) {
    DWORD style = (DWORD)GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    if (on) {
        g_windowedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(g_hwnd, &g_windowedPlacement);
        MONITORINFO mi = {};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &g_windowedPlacement);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    if (g_captured) { Capture(false); Capture(true); } // re-clip to the new client area
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        ResizeRender(LOWORD(lParam), HIWORD(lParam));
        if (g_captured) { Capture(false); Capture(true); }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) { Capture(false); InputReleaseAll(); if (!GamePaused()) SetPaused(true); }
        return 0;
    case WM_KILLFOCUS:
        Capture(false);
        InputReleaseAll();
        return 0;
    case WM_INPUT: {
        if (!g_captured) break;
        RAWINPUT ri;
        UINT size = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
            InputMouseMove(ri.data.mouse.lLastX, ri.data.mouse.lLastY);
        break; // DefWindowProc must still see WM_INPUT
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        bool repeat = (lParam & (1 << 30)) != 0;
        int vk = (int)wParam;
        if (vk == VK_F11 && !repeat) { g_fullscreen = !g_fullscreen; ApplyFullscreen(g_fullscreen); SaveSettings(); return 0; }
        if (vk == VK_F4 && msg == WM_SYSKEYDOWN) break; // Alt+F4 closes as usual
        if (!repeat && vk == g_bindings[ACT_PAUSE]) { SetPaused(!GamePaused()); return 0; }
        if (!repeat) GameDebugKey(vk, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        if (!GamePaused()) InputKey(vk, true, repeat);
        return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP:
        InputKey((int)wParam, false, false);
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
        if (GamePaused()) { SetPaused(false); return 0; } // a click returns to the game (and isn't also a shot)
        int code = msg == WM_LBUTTONDOWN ? MOUSE_LEFT : msg == WM_RBUTTONDOWN ? MOUSE_RIGHT : MOUSE_MIDDLE;
        InputMouseButton(code, true);
        return 0;
    }
    case WM_LBUTTONUP: InputMouseButton(MOUSE_LEFT, false); return 0;
    case WM_RBUTTONUP: InputMouseButton(MOUSE_RIGHT, false); return 0;
    case WM_MBUTTONUP: InputMouseButton(MOUSE_MIDDLE, false); return 0;
    case WM_MOUSEWHEEL:
        if (!GamePaused()) InputWheel(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;
    case WM_DESTROY:
        Capture(false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    ProfBootMark("LAUNCH");
    {
        // Per-monitor DPI aware, so Windows never blurs the window on a
        // scaled display (V2 context where available, the old call otherwise).
        typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        SetCtxFn setCtx = user32 ? (SetCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
        if (!setCtx || !setCtx((HANDLE)-4)) SetProcessDPIAware();
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CacophonyWindow";
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    RECT wr = { 0, 0, DEFAULT_WINDOW_W, DEFAULT_WINDOW_H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowW(L"CacophonyWindow", L"Cacophony", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;
    RECT rc; GetClientRect(g_hwnd, &rc);
    ResizeRender(rc.right - rc.left, rc.bottom - rc.top);
    ShowWindow(g_hwnd, nCmdShow);
    ProfBootMark("WINDOW");
    LoadSettings();
    ProfBootMark("SETTINGS");

    if (!InitRender(g_hwnd)) {
        MessageBoxW(g_hwnd, L"Cacophony couldn't start its graphics.\nshader_errors.txt in Documents\\My Games\\Cacophony may say why.",
                    L"Cacophony", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (g_fullscreen) ApplyFullscreen(true);
    RAWINPUTDEVICE rid = { 0x01, 0x02, 0, g_hwnd }; // generic desktop mouse
    RegisterRawInputDevices(&rid, 1, sizeof rid);
    GameInit(20260925u);
    ProfBootMark("WORLD");
    Capture(GetForegroundWindow() == g_hwnd);

    timeBeginPeriod(1); // 1 ms sleeps, so the frame cap is precise
    LARGE_INTEGER freq, last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    const float TICK = 1.0f / 60.0f;
    float accumulator = 0;
    bool first = true, running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        last = now;
        ProfEndFrame(dt); // the true frame length: a hitch is what it's there to show
        ProfBeginFrame();
        if (dt > 0.25f) dt = 0.25f;
        if (GamePaused()) accumulator = 0; // paused: the world waits, and no catch-up burst on return
        else {
            accumulator += dt;
            int ticks = 0;
            while (accumulator >= TICK && ticks++ < 5) { GameTick(TICK); accumulator -= TICK; }
            if (accumulator >= TICK) accumulator = fmodf(accumulator, TICK);
        }
        GameFrame(dt);
        GameRender();
        {
            ProfScope prof(PROF_PRESENT);
            PresentFrame(g_vsync);
            if (first) { first = false; ProfBootMark("FIRST FRAME"); }
            // With vsync on, the display paces the frames; the cap applies without it
            // (a cap that isn't a divisor of the refresh rate would make frames judder).
            if (!g_vsync) {
                double target = 1.0 / (double)g_frameLimit;
                for (;;) {
                    LARGE_INTEGER t; QueryPerformanceCounter(&t);
                    double left = target - (double)(t.QuadPart - now.QuadPart) / (double)freq.QuadPart;
                    if (left <= 0) break;
                    if (left > 0.002) Sleep((DWORD)((left - 0.0015) * 1000.0));
                }
            }
        }
    }
    timeEndPeriod(1);
    ShutdownRender();
    return 0;
}
