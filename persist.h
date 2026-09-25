// persist.h
//
// Everything that touches the disk: where the game keeps its files
// (Documents\My Games\Cacophony, falling back to the working folder), the
// settings file (plain key=value lines, unknown keys ignored, missing keys
// keep their defaults, every value clamped to a sane range), the shader
// cache folder, and one-off text files such as the Ctrl+F3 report. Local
// files only; nothing is sent anywhere. (Carried from Voxistics' persist.)

#pragma once

#include <filesystem>
#include <string>

extern float g_sensitivity;      // mouse, 0.25..3 (1 = default)
extern bool g_invertY;
extern float g_fov;              // vertical degrees, 50..100
extern bool g_fullscreen;        // borderless (F11)
extern bool g_vsync;
extern int g_frameLimit;         // frames per second cap, 30..240
extern float g_masterVolume, g_musicVolume, g_effectsVolume;

// Documents\My Games\Cacophony\ShaderCache, created if need be; empty if
// there's nowhere safe (then nothing is cached).
std::filesystem::path ShaderCacheDirectory();

void LoadSettings();
bool SaveSettings();

// Writes `text` to a file in the game's folder, replacing any old one.
// Returns the full path (UTF-8, for showing), or "" on failure.
std::string WriteTextFile(const char* fileName, const std::string& text);
