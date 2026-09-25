// persist.cpp
//
// Implementations for persist.h: keybindings/settings globals,
// settings.cfg read/write and the game's folder. (Voxistics' save slots
// and SaveGame/LoadGame are left out: Cacophony has no saves yet.)

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath
#include "persist.h"
#include "play.h" // g_loadRadius
#include "profiler.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib") // provides the FOLDERID_* GUID data (declared, not defined, in knownfolders.h)

// =======================================================================
// Input bindings + gameplay/UI preferences
// =======================================================================

const char* g_actionNames[ACT_COUNT] = {
    "forward", "back", "left", "right", "jump", "fire", "lock", "menu", "boost", "shield", "weapon"
};
// F switches weapons (owner), E raises the shield, Shift boosts.
int g_keyBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_SHIFT, 'E', 'F'
};
float g_sensitivityMultX = 1.0f, g_sensitivityMultY = 1.0f;
bool g_invertX = false, g_invertY = false;
bool g_showFPS = false;
bool g_fullscreen = false;
bool g_shadows = true, g_postEdges = false, g_postSSAO = false, g_bloom = true;
float g_masterVolume = 1.0f;
float g_musicVolume = 1.0f;
float g_worldVolume = 1.0f;
float g_fov = 70.0f; // Cacophony: a wider view from the mech's high seat (Voxistics: 45)
bool g_toggleMovement = false;
bool g_highContrastUI = false;
bool g_monoAudio = false;
bool g_vsync = true;
int g_frameLimit = 60;
bool g_moveToggleLatch[ACT_COUNT] = {};
float g_musicIntensity = 1.0f;

// Shared by GetSaveDirectory and GetSavesDirectory below: checks
// exists()&&!is_directory() before create_directories() specifically to
// catch a plain file already occupying part of the intended path,
// rather than letting a failed directory creation surface as a
// mysterious save failure. Empty return means "use the fallback"
// (the current working directory) rather than this path.
static std::filesystem::path EnsureDirectoryBulletproof(std::filesystem::path dir, const char* what) {
    namespace fs = std::filesystem;
    if (dir.empty()) return fs::path();
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_directory(dir, ec)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: a file already occupies the intended directory path, falling back\n", what);
        OutputDebugStringA(msg);
        return fs::path();
    }
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: could not create the directory, falling back\n", what);
        OutputDebugStringA(msg);
        return fs::path();
    }
    return dir;
}

// Resolves (creating if needed) Documents\My Games\Cacophony -- the
// conventional PC-game save location: visible and easy for players to
// find, back up, or copy between machines, unlike a hidden AppData
// folder. Falls back to the current working directory (this prototype's
// original behavior) if the known-folder lookup fails for any reason,
// or if something unexpected already occupies part of the intended
// path -- e.g. a plain file sitting where a folder needs to be. A save
// attempt should always have somewhere safe to go rather than failing
// forever because the "nice" location didn't pan out.
static std::filesystem::path GetSaveDirectory() {
    namespace fs = std::filesystem;
    PWSTR docsPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docsPath);
    fs::path dir;
    if (SUCCEEDED(hr) && docsPath) {
        dir = fs::path(docsPath) / L"My Games" / L"Cacophony";
    }
    if (docsPath) CoTaskMemFree(docsPath);

    if (dir.empty()) {
        OutputDebugStringA("GetSaveDirectory: could not resolve Documents, falling back to working directory\n");
        return fs::path();
    }
    return EnsureDirectoryBulletproof(dir, "GetSaveDirectory");
}

std::filesystem::path ShaderCacheDirectory() {
    std::filesystem::path base = GetSaveDirectory();
    if (base.empty()) return base;
    return EnsureDirectoryBulletproof(base / L"ShaderCache", "ShaderCacheDirectory");
}

// =======================================================================
// Section 7.2.3 - Global settings file
// =======================================================================
//
// Gameplay/UI preferences (sensitivity, inversion, render distance, the
// FPS toggle, volumes, keybindings) live in their own small text file,
// separate from any world save, so they're available before any save is
// loaded (e.g. a title screen's Options) and carry over between saves
// rather than being tied to one. Plain "key=value" lines rather than the
// versioned binary format saves use: it's a handful of scalars a player
// might reasonably want to hand-edit or inspect, and forward/backward
// compatibility just falls out of "unknown keys are ignored, missing
// keys keep their compiled-in default" with no version field needed.
static std::filesystem::path GetSettingsFilePath() {
    std::filesystem::path dir = GetSaveDirectory(); // same bulletproofed directory as the save file
    std::filesystem::path filename = L"settings.cfg";
    return dir.empty() ? filename : dir / filename;
}

bool SaveSettings() {
    std::ostringstream ss;
    ss << "sensitivityX=" << g_sensitivityMultX << "\n";
    ss << "sensitivityY=" << g_sensitivityMultY << "\n";
    ss << "invertX=" << (g_invertX ? 1 : 0) << "\n";
    ss << "invertY=" << (g_invertY ? 1 : 0) << "\n";
    ss << "renderDistance=" << g_loadRadius << "\n";
    ss << "showFPS=" << (g_showFPS ? 1 : 0) << "\n";
    ss << "showProfiler=" << (g_showProfiler ? 1 : 0) << "\n";
    ss << "fullscreen=" << (g_fullscreen ? 1 : 0) << "\n";
    ss << "sun_shadows=" << (g_shadows ? 1 : 0) << "\n";
    ss << "outlines=" << (g_postEdges ? 1 : 0) << "\n";
    ss << "ssao=" << (g_postSSAO ? 1 : 0) << "\n";
    ss << "bloom=" << (g_bloom ? 1 : 0) << "\n";
    ss << "masterVolume=" << g_masterVolume << "\n";
    ss << "musicVolume=" << g_musicVolume << "\n";
    ss << "worldVolume=" << g_worldVolume << "\n";
    ss << "fov=" << g_fov << "\n";
    ss << "toggleMovement=" << (g_toggleMovement ? 1 : 0) << "\n";
    ss << "highContrastUI=" << (g_highContrastUI ? 1 : 0) << "\n";
    ss << "monoAudio=" << (g_monoAudio ? 1 : 0) << "\n";
    ss << "vsync=" << (g_vsync ? 1 : 0) << "\n";
    ss << "frameLimit=" << g_frameLimit << "\n";
    ss << "musicIntensity=" << g_musicIntensity << "\n";
    for (int i = 0; i < ACT_COUNT; i++) {
        ss << "keybind." << g_actionNames[i] << "=" << g_keyBindings[i] << "\n"; // name-indexed, same reasoning as g_blockNames
    }

    namespace fs = std::filesystem;
    fs::path path = GetSettingsFilePath();
    fs::path tmpPath = path; tmpPath += L".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::string data = ss.str();
        out.write(data.data(), (std::streamsize)data.size());
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    return !ec;
}

// settings.cfg is plain text a player may hand-edit, so values read from
// it (or from a legacy v2 save) are clamped to the same ranges the menu
// sliders allow -- an out-of-range render distance alone would have
// EnsureChunksLoaded enqueue millions of columns.
static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
void ClampSettingsToValidRanges() {
    g_sensitivityMultX = ClampF(g_sensitivityMultX, 0.25f, 3.0f);
    g_sensitivityMultY = ClampF(g_sensitivityMultY, 0.25f, 3.0f);
    if (g_loadRadius < VIEW_CHUNKS_MIN) g_loadRadius = VIEW_CHUNKS_MIN; // Cacophony seam: its own chunk range (play.h)
    if (g_loadRadius > VIEW_CHUNKS_MAX) g_loadRadius = VIEW_CHUNKS_MAX;
    g_masterVolume = ClampF(g_masterVolume, 0.0f, 1.0f);
    g_musicVolume = ClampF(g_musicVolume, 0.0f, 1.0f);
    g_worldVolume = ClampF(g_worldVolume, 0.0f, 1.0f);
    g_fov = ClampF(g_fov, 45.0f, 100.0f);
    g_musicIntensity = ClampF(g_musicIntensity, 0.0f, 1.0f);
}

// Missing file (first run) or missing/unrecognized individual keys
// (an older settings.cfg from before some setting existed) both just
// keep whatever the caller's compiled-in default already was -- loading
// settings can only ever refine current state, never fail outright.
void LoadSettings() {
    std::ifstream in(GetSettingsFilePath());
    if (!in) return;

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }

    auto getF = [&](const char* k, float def) { auto it = kv.find(k); return it == kv.end() ? def : (float)atof(it->second.c_str()); };
    auto getI = [&](const char* k, int def) { auto it = kv.find(k); return it == kv.end() ? def : atoi(it->second.c_str()); };
    auto getB = [&](const char* k, bool def) { auto it = kv.find(k); return it == kv.end() ? def : (atoi(it->second.c_str()) != 0); };

    g_sensitivityMultX = getF("sensitivityX", g_sensitivityMultX);
    g_sensitivityMultY = getF("sensitivityY", g_sensitivityMultY);
    g_invertX = getB("invertX", g_invertX);
    g_invertY = getB("invertY", g_invertY);
    g_loadRadius = getI("renderDistance", g_loadRadius);
    g_showFPS = getB("showFPS", g_showFPS);
    g_showProfiler = getB("showProfiler", g_showProfiler);
    g_fullscreen = getB("fullscreen", g_fullscreen);
    // "sun_shadows", not the old "shadows": shadows now carry the lighting
    // (4.9) and default on, so an old file's default-off doesn't stick.
    g_shadows = getB("sun_shadows", g_shadows);
    g_postEdges = getB("outlines", g_postEdges);
    g_postSSAO = getB("ssao", g_postSSAO);
    g_bloom = getB("bloom", g_bloom);
    g_masterVolume = getF("masterVolume", g_masterVolume);
    g_musicVolume = getF("musicVolume", g_musicVolume);
    g_worldVolume = getF("worldVolume", g_worldVolume);
    g_fov = getF("fov", g_fov);
    g_toggleMovement = getB("toggleMovement", g_toggleMovement);
    g_highContrastUI = getB("highContrastUI", g_highContrastUI);
    g_monoAudio = getB("monoAudio", g_monoAudio);
    g_vsync = getB("vsync", g_vsync);
    g_frameLimit = (int)getF("frameLimit", (float)g_frameLimit);
    if (g_frameLimit < 30) g_frameLimit = 30;
    if (g_frameLimit > 200) g_frameLimit = 200;
    g_musicIntensity = getF("musicIntensity", g_musicIntensity);
    for (int i = 0; i < ACT_COUNT; i++) {
        std::string key = std::string("keybind.") + g_actionNames[i];
        g_keyBindings[i] = getI(key.c_str(), g_keyBindings[i]);
    }
    ClampSettingsToValidRanges();
}

std::string WriteTextToSaveFolder(const char* fileName, const std::string& text) {
    std::filesystem::path dir = GetSaveDirectory();
    std::filesystem::path path = dir.empty() ? std::filesystem::path(fileName) : dir / fileName;
    std::ofstream f(path, std::ios::binary | std::ios::trunc); // wide paths, and no deprecated CRT calls (MSVC SDL checks)
    if (!f) return "";
    f.write(text.data(), (std::streamsize)text.size());
    f.close();
    return f ? path.string() : "";
}
