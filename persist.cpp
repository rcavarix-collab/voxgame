// persist.cpp -- see persist.h.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include "persist.h"
#include "input.h"
#include "profiler.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

float g_sensitivity = 1.0f;
bool g_invertY = false;
float g_fov = 70.0f;
bool g_fullscreen = false;
bool g_vsync = true;
int g_frameLimit = 60;
float g_masterVolume = 1.0f, g_musicVolume = 1.0f, g_effectsVolume = 1.0f;

// Bumped whenever the default key layout changes (2: F switches weapons, E shields).
static const int BIND_VERSION = 2;

namespace fs = std::filesystem;

namespace {
// A folder we can actually write into, or empty. Refuses a path where a
// plain file sits in the way rather than failing mysteriously later.
fs::path EnsureDirectory(const fs::path& dir) {
    if (dir.empty()) return fs::path();
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_directory(dir, ec)) return fs::path();
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) return fs::path();
    return dir;
}

// Documents\My Games\Cacophony: easy for players to find and back up.
// Looked up every time (cheap), so a folder that appears later is used.
fs::path GameDirectory() {
    PWSTR docs = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
        dir = fs::path(docs) / L"My Games" / L"Cacophony";
    if (docs) CoTaskMemFree(docs);
    return EnsureDirectory(dir);
}

fs::path InGameDirectory(const wchar_t* name) {
    fs::path d = GameDirectory();
    return d.empty() ? fs::path(name) : d / name;
}

std::string Utf8(const fs::path& p) {
    // Not path::string(): that goes through the ANSI code page and can
    // throw on a user folder with characters outside it.
    std::wstring w = p.wstring();
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
}

fs::path ShaderCacheDirectory() {
    fs::path base = GameDirectory();
    return base.empty() ? base : EnsureDirectory(base / L"ShaderCache");
}

bool SaveSettings() {
    std::ostringstream ss;
    ss << "sensitivity=" << g_sensitivity << "\n";
    ss << "invertY=" << (g_invertY ? 1 : 0) << "\n";
    ss << "fov=" << g_fov << "\n";
    ss << "fullscreen=" << (g_fullscreen ? 1 : 0) << "\n";
    ss << "vsync=" << (g_vsync ? 1 : 0) << "\n";
    ss << "frameLimit=" << g_frameLimit << "\n";
    ss << "showProfiler=" << (g_showProfiler ? 1 : 0) << "\n";
    ss << "masterVolume=" << g_masterVolume << "\n";
    ss << "musicVolume=" << g_musicVolume << "\n";
    ss << "effectsVolume=" << g_effectsVolume << "\n";
    ss << "bindVersion=" << BIND_VERSION << "\n";
    for (int i = 0; i < ACT_COUNT; i++) ss << "bind." << g_actionNames[i] << "=" << g_bindings[i] << "\n";
    // Written beside, then swapped in: a crash mid-write never leaves a half file.
    fs::path path = InGameDirectory(L"settings.cfg");
    fs::path tmp = path; tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::string data = ss.str();
        out.write(data.data(), (std::streamsize)data.size());
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec); // replaces the old file
    return !ec;
}

void LoadSettings() {
    std::ifstream in(InGameDirectory(L"settings.cfg"));
    if (!in) return; // first run: defaults
    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    auto F = [&](const char* k, float d) { auto it = kv.find(k); return it == kv.end() ? d : (float)atof(it->second.c_str()); };
    auto I = [&](const char* k, int d) { auto it = kv.find(k); return it == kv.end() ? d : atoi(it->second.c_str()); };
    auto clampF = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    // A hand-edited file can hold anything: clamp everything.
    g_sensitivity = clampF(F("sensitivity", g_sensitivity), 0.25f, 3.0f);
    g_invertY = I("invertY", g_invertY) != 0;
    g_fov = clampF(F("fov", g_fov), 50.0f, 100.0f);
    g_fullscreen = I("fullscreen", g_fullscreen) != 0;
    g_vsync = I("vsync", g_vsync) != 0;
    g_frameLimit = (int)clampF((float)I("frameLimit", g_frameLimit), 30.0f, 240.0f);
    g_showProfiler = I("showProfiler", g_showProfiler) != 0;
    g_masterVolume = clampF(F("masterVolume", g_masterVolume), 0.0f, 1.0f);
    g_musicVolume = clampF(F("musicVolume", g_musicVolume), 0.0f, 1.0f);
    g_effectsVolume = clampF(F("effectsVolume", g_effectsVolume), 0.0f, 1.0f);
    // Saved keys from before the default layout last changed are the old
    // defaults, not the player's choice: drop them once, take the new ones.
    for (int i = 0; i < ACT_COUNT && I("bindVersion", 1) >= BIND_VERSION; i++) {
        std::string key = std::string("bind.") + g_actionNames[i];
        int code = I(key.c_str(), g_bindings[i]);
        if (code != 0 && code >= MOUSE_MIDDLE && code < 256) g_bindings[i] = code; // a valid key or mouse button
    }
}

std::string WriteTextFile(const char* fileName, const std::string& text) {
    fs::path d = GameDirectory();
    fs::path path = d.empty() ? fs::path(fileName) : d / fileName;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return "";
    f.write(text.data(), (std::streamsize)text.size());
    f.close();
    return f ? Utf8(fs::absolute(path)) : "";
}
