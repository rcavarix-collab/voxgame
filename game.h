// game.h
//
// Menu/UI state machine, input dispatch, the window procedure, and the
// second (orthographic UI) render pass. Everything here sits on top of
// world.h/render.h/audio.h/persist.h -- it's the layer that actually
// reacts to a click or a keypress and decides what the simulation or
// the renderer should do about it. main.cpp's message loop only reaches
// into this file for the handful of things below that it drives itself
// (registering WndProc, the per-tick input-to-action mapping, and
// kicking off the UI pass); everything else (menu layout, slider
// dragging, save-slot bookkeeping, ...) stays internal to game.cpp,
// same as it was file-static inside the original monolith.

#pragma once

#include "world.h"
#include "persist.h" // GameAction
#include <string>

// The two top-level state machines main.cpp's loop itself branches on
// (freezing the simulation accumulator while a menu is open, gating the
// mouse-look recenter). GameState (Title/InGame) stays entirely inside
// game.cpp -- nothing outside it needs to ask which one is active.
enum class MenuScreen { None, Pause, LookSettings, Graphics, Display, Audio, Keybindings, Accessibility, TitleMain, SlotPicker, OptionsHub, Map, Library };
extern MenuScreen g_menuScreen;

extern bool g_mouseCaptured;

// Per-frame timers/counters main.cpp's loop ticks down or accumulates
// directly, same as it always did as file-static state in one function.
extern float g_toastTimer;
extern int g_confirmOverwriteSlot;
extern float g_confirmOverwriteTimer;
extern int g_fpsFrameCount, g_fpsDisplay;
extern float g_fpsTimer;

// Mouse-look scaling; main.cpp's loop applies it directly to raw cursor
// delta each frame rather than going through a function call.
static const float BASE_MOUSE_SENS = 0.0025f;

// Whether an action is currently "held" per its bound input, honoring
// toggle-to-move for the four movement actions (Accessibility, Section
// 11) -- main.cpp's fixed-timestep tick calls this once per movement
// action every step.
bool IsActionDown(GameAction a);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Second (orthographic, depth-off) render pass: crosshair, hotbar, all
// menus, toasts, FPS counter. Assumes the world pass already ran this
// frame.
void RenderUIPass();

// Borderless fullscreen on the window's current monitor, or back to the
// window as it was. Doesn't touch the saved preference.
void ApplyFullscreen(bool on);

// Autosaves every few minutes of actual play; called once per frame.
void TickAutosave(float dt);
// True while a world is loaded (in play or in its menus), not at the title.
bool IsInGame();
// Once per frame: saves a finished performance capture (Ctrl+F3).
void PollPerfCapture();

// A transient centred message (save/load confirmations, startup problems).
void ShowToast(const std::string& message, float seconds);
// Debug time control (Section 13): holding ] / Page Up or [ / Page Down
// scrubs the day clock; call once per frame with the frame's real time.
void UpdateDebugTimeScrub(float frameSeconds);
