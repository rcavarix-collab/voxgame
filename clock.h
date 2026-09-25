// clock.h
//
// The day clock (DESIGN.md §4: the one-hour day), shared by the sky
// (sky.h), the music (audio.cpp, whose day-cycle song is written against
// it) and the game. These are Voxistics' world.h "Section 13 - Day clock"
// declarations, carried as they were; Cacophony's world is the faceted
// terrain, so the clock lives in its own header instead of the block
// world's. Defined in game.cpp.

#pragma once

#include <cstdint>

// Advances only while play is actually ticking (paused = the world and
// its music wait), and wraps at DAY_LENGTH_SECONDS. 0 is sunrise (sky.h).
constexpr float DAY_LENGTH_SECONDS = 3600.0f; // one in-game day = one real hour, locked in
extern float g_dayTimeSeconds;

// Simulation ticks since the world was set up (relative only). The sky's
// clouds drift by it.
extern uint32_t g_worldTick;
