// input.cpp -- see input.h.

#include "input.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

const char* g_actionNames[ACT_COUNT] = {
    "forward", "back", "left", "right", "jump", "boost", "fire", "next_weapon", "lock", "shield", "pause",
};
int g_bindings[ACT_COUNT] = {
    // F switches weapons (owner's expectation; the wheel does too); E is the shield.
    'W', 'S', 'A', 'D', VK_SPACE, VK_SHIFT, MOUSE_LEFT, 'F', MOUSE_RIGHT, 'E', VK_ESCAPE,
};

namespace {
bool g_held[ACT_COUNT];
int g_presses[ACT_COUNT];
long g_mouseX = 0, g_mouseY = 0;
int g_wheel = 0;

void Code(int code, bool down) {
    for (int a = 0; a < ACT_COUNT; a++) {
        if (g_bindings[a] != code) continue;
        if (down && !g_held[a]) g_presses[a]++;
        g_held[a] = down;
    }
}
}

void InputKey(int vk, bool down, bool isRepeat) {
    if (isRepeat) return;
    // Either Shift or Control key counts as the generic one it's bound as.
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
    Code(vk, down);
}
void InputMouseButton(int code, bool down) { Code(code, down); }
void InputMouseMove(long dx, long dy) { g_mouseX += dx; g_mouseY += dy; }
void InputWheel(int notches) { g_wheel += notches; }
void InputReleaseAll() {
    for (int a = 0; a < ACT_COUNT; a++) { g_held[a] = false; g_presses[a] = 0; } // no burst of stale taps on return
    g_mouseX = g_mouseY = 0;
    g_wheel = 0;
}

bool ActionHeld(Action a) { return g_held[a]; }
bool TakePress(Action a) {
    if (g_presses[a] <= 0) return false;
    g_presses[a]--;
    return true;
}
void TakeMouse(float& dx, float& dy) { dx = (float)g_mouseX; dy = (float)g_mouseY; g_mouseX = g_mouseY = 0; }
int TakeWheel() { int w = g_wheel; g_wheel = 0; return w; }
