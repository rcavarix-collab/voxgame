// persist.h
//
// Everything disk-facing that isn't the world's own block data: input
// bindings and gameplay/UI preferences (settings.cfg), and versioned
// world+player save/load across multiple slots (Documents\My Games\
// Voxistics\Saves\slotN.sav). Two different files on disk for two
// different reasons (preferences carry across saves; a save is one
// world's own state), bundled into one module here because both are
// "read/write something under the save directory" concerns that were
// already adjacent in the original single file.

#pragma once

#include "world.h"
#include <cstdint>
#include <string>
#include <filesystem>

// Where compiled shaders are cached between runs (render.cpp; Part XVI):
// Documents\My Games\Voxistics\ShaderCache, created if need be. Empty if
// there's no safe place for it (then nothing is cached).
std::filesystem::path ShaderCacheDirectory();

// =======================================================================
// Input bindings + gameplay/UI preferences (settings.cfg)
// =======================================================================

enum GameAction {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_JUMP,
    ACT_BREAK, ACT_PLACE, ACT_MENU, ACT_SAVE, ACT_LOAD, ACT_MAP,
    ACT_SPRINT, ACT_CROUCH, ACT_LIBRARY, // appended: saved bindings are by name, so older settings files still load
    ACT_COUNT
};
// Stable identity for the save file, same idea as g_blockNames.
extern const char* g_actionNames[ACT_COUNT];
static const int MOUSE_LEFT = -1, MOUSE_RIGHT = -2, MOUSE_MIDDLE = -3; // share the bound-input-code space with VK_* (all positive)
extern int g_keyBindings[ACT_COUNT];

extern float g_sensitivityMultX, g_sensitivityMultY;
extern bool g_invertX, g_invertY;
extern bool g_showFPS;
extern bool g_fullscreen; // borderless fullscreen on the window's monitor (Display settings / F11)
// Graphics effects (Sections 4.8, 4.10), each independently toggleable.
extern bool g_shadows, g_postEdges, g_postSSAO, g_bloom;
extern float g_masterVolume;
extern float g_musicVolume;
extern float g_worldVolume; // the world sound palette (sfx_synth.h), under Master
extern float g_fov; // degrees, vertical
// Accessibility (Section 11): press-to-toggle instead of hold-to-move for WASD.
extern bool g_toggleMovement;
extern bool g_highContrastUI; // higher-luminance-contrast menu palette
extern bool g_monoAudio;
extern int g_colourVision; // pulse_colours.h ColourVision (Accessibility)
extern bool g_vsync;          // present in step with the display (Graphics)
extern int g_frameLimit;      // frames per second cap, 30-200 (Graphics)      // world sounds centred: no stereo placement (Part XI)
extern bool g_moveToggleLatch[ACT_COUNT]; // only ACT_FORWARD/BACK/LEFT/RIGHT indices are ever used
// Music Intensity (Accessibility, Section 11): 0 = ambient bed only, no
// arp/pulse layer at all; 1 = the full designed arc -- a ceiling, not a
// ceiling-breaker (see audio module docs / DESIGN.md Part XIV.3).
extern float g_musicIntensity;
// The hotbar's ten blocks (library.h), chosen from the block library and
// saved by block name so registry changes can't scramble them.
#include "library.h"
extern BlockID g_hotbar[HOTBAR_SLOTS];

bool SaveSettings();
void LoadSettings();
void ClampSettingsToValidRanges();

// =======================================================================
// Versioned world+player save/load, multi-slot
// =======================================================================

static const int MAX_SAVE_SLOTS = 5;

std::filesystem::path GetSaveFilePath(int slot);
bool SlotExists(int slot);
// One-time migration from the pre-multi-slot single save file, called
// once at startup.
void MigrateLegacySingleSaveIfPresent();

bool SaveGame(World& w, Player& p, int slot);
bool LoadGame(World& w, Player& p, int slot);

// Writes a text file (e.g. the performance report) into the save folder,
// replacing any old one. Returns the full path written, or "" on failure.
std::string WriteTextToSaveFolder(const char* fileName, const std::string& text);
