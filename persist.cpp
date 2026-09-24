// persist.cpp
//
// Implementations for persist.h: keybindings/settings globals,
// settings.cfg read/write, save-directory/slot resolution, and the
// versioned world+player SaveGame/LoadGame.

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath
#include "persist.h"
#include "worldfile.h"
#include "theline.h"
#include "essence.h"
#include "profiler.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar> // swprintf, for building slotN.sav filenames (Section 7.2.4)
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
    "forward", "back", "left", "right", "jump", "break", "place", "menu", "save", "load", "map", "sprint", "crouch", "library"
};
int g_keyBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_F5, VK_F9, 'M', VK_SHIFT, VK_CONTROL, 'E'
};
BlockID g_hotbar[HOTBAR_SLOTS] = { BLOCK_STONE, BLOCK_DIRT, BLOCK_WOOD, BLOCK_LOG, BLOCK_SAND,
                                   BLOCK_SANDSTONE, BLOCK_GLASS, BLOCK_MUSIC, BLOCK_TIMESTREAM, BLOCK_STONE_SLAB }; // = DefaultHotbar
float g_sensitivityMultX = 1.0f, g_sensitivityMultY = 1.0f;
bool g_invertX = false, g_invertY = false;
bool g_showFPS = false;
bool g_fullscreen = false;
bool g_shadows = true, g_postEdges = false, g_postSSAO = false, g_bloom = true;
float g_masterVolume = 1.0f;
float g_musicVolume = 1.0f;
float g_worldVolume = 1.0f;
float g_fov = 45.0f;
bool g_toggleMovement = false;
bool g_highContrastUI = false;
bool g_monoAudio = false;
bool g_vsync = true;
int g_frameLimit = 60;
bool g_moveToggleLatch[ACT_COUNT] = {};
float g_musicIntensity = 1.0f;

// =======================================================================
// Part VII - Save / load (crash-safe, versioned, name-indexed)
// =======================================================================

// The byte format itself (and SAVE_VERSION) lives in worldfile.cpp; this
// file handles the disk side and applying a decoded save to live state.

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

// Resolves (creating if needed) Documents\My Games\Voxistics -- the
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
        dir = fs::path(docsPath) / L"My Games" / L"Voxistics";
    }
    if (docsPath) CoTaskMemFree(docsPath);

    if (dir.empty()) {
        OutputDebugStringA("GetSaveDirectory: could not resolve Documents, falling back to working directory\n");
        return fs::path();
    }
    return EnsureDirectoryBulletproof(dir, "GetSaveDirectory");
}

// The multi-slot saves subfolder (Section 7.2.4), inside the same
// bulletproofed base directory as settings.cfg.
static std::filesystem::path GetSavesDirectory() {
    std::filesystem::path base = GetSaveDirectory();
    if (base.empty()) return base;
    return EnsureDirectoryBulletproof(base / L"Saves", "GetSavesDirectory");
}

// Recomputed on every save/load rather than cached once -- cheap, and
// means a save directory that only becomes available partway through a
// run (e.g. a transient permissions/antivirus hiccup clears up) is
// retried instead of being stuck with whatever the very first attempt
// happened to find.
std::filesystem::path GetSaveFilePath(int slot) {
    std::filesystem::path dir = GetSavesDirectory();
    wchar_t name[32];
    swprintf(name, 32, L"slot%d.sav", slot + 1);
    return dir.empty() ? std::filesystem::path(name) : dir / name;
}

bool SlotExists(int slot) {
    std::error_code ec;
    return std::filesystem::exists(GetSaveFilePath(slot), ec);
}

// One-time migration (same philosophy as the v2-settings migration
// above): a save from before multi-slot support existed lived directly
// at Documents\My Games\Voxistics\voxelproto.sav. If that file exists
// and slot 1 doesn't yet, move it into the new Saves\slot1.sav location
// rather than leaving it invisible to the new slot picker forever.
void MigrateLegacySingleSaveIfPresent() {
    namespace fs = std::filesystem;
    std::filesystem::path base = GetSaveDirectory();
    if (base.empty()) return;
    fs::path legacyPath = base / L"voxelproto.sav";
    std::error_code ec;
    if (!fs::exists(legacyPath, ec)) return;
    if (SlotExists(0)) return; // slot 1 already has its own save; never overwrite it
    fs::path slot1Path = GetSaveFilePath(0);
    if (slot1Path.empty()) return;
    fs::rename(legacyPath, slot1Path, ec); // same volume (same parent tree) -- a plain rename is sufficient
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
    ss << "hotbar=";
    for (int i = 0; i < HOTBAR_SLOTS; i++) ss << (i ? "," : "") << g_blocks[g_hotbar[i]].name;
    ss << "\n";
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
    if (g_loadRadius < 1) g_loadRadius = 1;
    if (g_loadRadius > 8) g_loadRadius = 8;
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
    // Hotbar by block name; an unknown or no-longer-placeable name keeps
    // that slot's default.
    {
        auto it = kv.find("hotbar");
        if (it != kv.end()) {
            std::stringstream hs(it->second); std::string name; int i = 0;
            while (i < HOTBAR_SLOTS && std::getline(hs, name, ',')) {
                for (int id = 1; id < BLOCK_COUNT; id++)
                    if (name == g_blocks[id].name && g_blocks[id].placeable) { g_hotbar[i] = (BlockID)id; break; }
                i++;
            }
        }
    }
    for (int i = 0; i < ACT_COUNT; i++) {
        std::string key = std::string("keybind.") + g_actionNames[i];
        g_keyBindings[i] = getI(key.c_str(), g_keyBindings[i]);
    }
    ClampSettingsToValidRanges();
}

bool SaveGame(World& w, Player& p, int slot) {
    std::vector<uint8_t> buf;
    EncodeSave(p, g_dayTimeSeconds, g_worldGen, w, g_evictedChunks, SnapshotScheduledUpdates(), SnapshotLine(g_line), g_essence.Snapshot(), buf);

    // Crash-safe write sequence (Section 7.3): write to .tmp, only then
    // rotate the previous save to .bak and rename .tmp into place.
    namespace fs = std::filesystem;
    fs::path savePath = GetSaveFilePath(slot);
    fs::path tmpPath = savePath; tmpPath += L".tmp";
    fs::path bakPath = savePath; bakPath += L".bak";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write((const char*)buf.data(), (std::streamsize)buf.size());
        if (!out) return false;
    }
    std::error_code ec;
    if (fs::exists(savePath, ec)) {
        fs::remove(bakPath, ec);
        fs::rename(savePath, bakPath, ec);
    }
    fs::rename(tmpPath, savePath, ec);
    if (ec) return false;
    return true;
}

bool LoadGame(World& w, Player& p, int slot) {
    std::ifstream in(GetSaveFilePath(slot), std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize size = in.tellg();
    if (size <= 0) return false;
    in.seekg(0);
    std::vector<uint8_t> buf((size_t)size);
    in.read((char*)buf.data(), size);
    if (!in) return false;

    // Decoded into a scratch SaveData; nothing live changes unless the
    // whole file is valid.
    SaveData d;
    DecodeResult res = DecodeSave(buf.data(), buf.size(), d);
    if (res != DecodeResult::Ok) {
        char msg[160];
        snprintf(msg, sizeof(msg), "LoadGame: %s, aborting load\n", DecodeResultText(res));
        OutputDebugStringA(msg);
        return false;
    }
    for (const std::string& name : d.unknownBlockNames) {
        char msg[256];
        snprintf(msg, sizeof(msg), "LoadGame: unknown block name '%s', mapping to air\n", name.c_str());
        OutputDebugStringA(msg);
    }

    Player loaded = d.player;
    // The placeable roster can shrink between builds, so a save made with
    // a now-nonexistent hotbar slot selected must not index past the end.
    if (loaded.hotbarIndex < 0 || loaded.hotbarIndex >= g_placeableList.count) loaded.hotbarIndex = 0;
    p = loaded;
    g_dayTimeSeconds = d.dayTime;
    g_worldGen = d.gen;

    if (d.hasLegacySettings) {
        g_sensitivityMultX = d.legacySensX; g_sensitivityMultY = d.legacySensY;
        g_invertX = d.legacyInvertX; g_invertY = d.legacyInvertY;
        g_loadRadius = d.legacyRenderDist;
        ClampSettingsToValidRanges();
        g_showFPS = d.legacyShowFPS;
        g_masterVolume = d.legacyVolume;
        // Remap saved keybinding action names -> current GameAction indices,
        // the same name-indexed pattern as block names (Section 3.1).
        for (auto& kv : d.legacyBindings) {
            bool found = false;
            for (int a = 0; a < ACT_COUNT; a++) {
                if (kv.first == g_actionNames[a]) { g_keyBindings[a] = kv.second; found = true; break; }
            }
            if (!found) {
                char msg[256];
                snprintf(msg, sizeof(msg), "LoadGame: unknown action name '%s', ignoring binding\n", kv.first.c_str());
                OutputDebugStringA(msg);
            }
        }
        ApplyAudioVolumes();
        // One-time migration (Section 7.2.3): seed the new global config
        // from this legacy save's settings, but only if nothing has
        // created settings.cfg yet.
        if (!std::filesystem::exists(GetSettingsFilePath())) {
            SaveSettings();
        }
    }

    // Every saved chunk goes to the modified-chunk store; the normal
    // streaming path then generates the columns around the player and
    // overlays these onto them (Section 2.4), so loading costs only the
    // decode, however large the world.
    w.ClearChunks();
    g_evictedChunks = std::move(d.chunks);
    g_residentColumns.clear();
    ClearScheduledUpdates();
    RestoreScheduledUpdates(d.updates); // unknown kinds are dropped
    RestoreLine(g_line, g_lineTuning, d.line); // empty history for pre-v7 saves
    g_essence.Restore(d.gen.seed, d.essence);  // nothing discovered for pre-v8 saves
    g_pendingColumns.clear();
    g_pendingColumnSet.clear();
    g_pendingEvictions.clear();
    g_pendingEvictionSet.clear();
    g_lastPlayerChunkX = INT32_MIN;
    g_lastPlayerChunkZ = INT32_MIN;
    return true;
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
