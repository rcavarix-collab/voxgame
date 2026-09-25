// game.h
//
// The glue (docs/ARCHITECTURE.md 1.3): owns the world and the mech, runs
// the fixed-step tick in order, keeps terrain resident and meshed around
// the mech within a per-frame budget, and draws the frame. Debug keys
// (F3 overlay, Ctrl+F3 report, R reset, T skip time) act only when
// pressed and only write local files.

#pragma once

#include <cstdint>

void GameInit(uint32_t seed);
void GameTick(float dt);            // one fixed step (1/60 s)
void GameFrame(float frameSeconds); // once per frame: terrain residency, meshing, uploads
void GameRender();                  // world, HUD, overlays
bool GamePaused();
void GameSetPaused(bool paused);
void GameDebugKey(int vk, bool ctrl); // non-remappable debug keys (Windows virtual-key codes)
static const int VK_F3_CODE = 0x72;   // VK_F3, without pulling windows.h into the glue
float GameDayTime();
