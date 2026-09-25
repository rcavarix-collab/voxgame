// input.h
//
// Actions and their bindings (docs/ARCHITECTURE.md 1.2). Keys and mouse
// buttons map to actions through one remappable table (saved by action
// name in settings.cfg), never through scattered key checks. The window
// procedure feeds raw events in; the fixed-step tick reads held state and
// takes presses. Presses are counted as they arrive, so a quick tap that
// starts and ends between two ticks still counts; key auto-repeat is
// ignored (the key is already held -- drillder.cpp's flaw, not repeated).
// Mouse movement comes from raw input, so aim isn't limited by the cursor
// or the screen edge. No allocation, no per-frame cost worth measuring.

#pragma once

enum Action {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT,
    ACT_JUMP, ACT_BOOST,
    ACT_FIRE, ACT_NEXT_WEAPON, ACT_LOCK, ACT_SHIELD,
    ACT_PAUSE,
    ACT_COUNT
};
extern const char* g_actionNames[ACT_COUNT];    // stable names for settings.cfg
static const int MOUSE_LEFT = -1, MOUSE_RIGHT = -2, MOUSE_MIDDLE = -3; // alongside VK_* codes (all positive)
extern int g_bindings[ACT_COUNT];

// From the window procedure.
void InputKey(int vk, bool down, bool isRepeat);
void InputMouseButton(int code, bool down);
void InputMouseMove(long dx, long dy);  // raw, unaccelerated counts
void InputWheel(int notches);           // +1 away from the player
void InputReleaseAll();                 // focus lost or paused: nothing stays held, pending taps dropped

// For the tick.
bool ActionHeld(Action a);
bool TakePress(Action a);               // true once per press
void TakeMouse(float& dx, float& dy);   // everything moved since the last call
int TakeWheel();
