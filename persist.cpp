// persist.cpp
//
// Implementations for persist.h: keybindings/settings globals,
// settings.cfg read/write, save-directory/slot resolution, and the
// versioned world+player SaveGame/LoadGame.

#define NOMINMAX
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath
#include "persist.h"
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
    "forward", "back", "left", "right", "jump", "break", "place", "menu", "save", "load"
};
int g_keyBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_F5, VK_F9
};
float g_sensitivityMultX = 1.0f, g_sensitivityMultY = 1.0f;
bool g_invertX = false, g_invertY = false;
bool g_showFPS = false;
float g_masterVolume = 1.0f;
float g_musicVolume = 1.0f;
float g_fov = 45.0f;
bool g_toggleMovement = false;
bool g_highContrastUI = false;
bool g_moveToggleLatch[ACT_COUNT] = {};
float g_musicIntensity = 1.0f;

// =======================================================================
// Part VII - Save / load (crash-safe, versioned, name-indexed)
// =======================================================================

static const uint32_t SAVE_VERSION = 4; // v3 dropped the embedded settings block (Section 7.2.3); v4 adds the day-clock field (Section 13). Both v2 and v3 files remain loadable -- see LoadGame's version handling.

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
    ss << "masterVolume=" << g_masterVolume << "\n";
    ss << "musicVolume=" << g_musicVolume << "\n";
    ss << "fov=" << g_fov << "\n";
    ss << "toggleMovement=" << (g_toggleMovement ? 1 : 0) << "\n";
    ss << "highContrastUI=" << (g_highContrastUI ? 1 : 0) << "\n";
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
    if (g_loadRadius < 1) g_loadRadius = 1;
    if (g_loadRadius > 8) g_loadRadius = 8;
    g_masterVolume = ClampF(g_masterVolume, 0.0f, 1.0f);
    g_musicVolume = ClampF(g_musicVolume, 0.0f, 1.0f);
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
    g_masterVolume = getF("masterVolume", g_masterVolume);
    g_musicVolume = getF("musicVolume", g_musicVolume);
    g_fov = getF("fov", g_fov);
    g_toggleMovement = getB("toggleMovement", g_toggleMovement);
    g_highContrastUI = getB("highContrastUI", g_highContrastUI);
    g_musicIntensity = getF("musicIntensity", g_musicIntensity);
    for (int i = 0; i < ACT_COUNT; i++) {
        std::string key = std::string("keybind.") + g_actionNames[i];
        g_keyBindings[i] = getI(key.c_str(), g_keyBindings[i]);
    }
    ClampSettingsToValidRanges();
}

static uint32_t Fnv1a(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

static void AppendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
static void AppendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v >> 8) & 0xFF));
}
static void AppendU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}
static void AppendI32(std::vector<uint8_t>& b, int32_t v) { AppendU32(b, (uint32_t)v); }
static void AppendF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits; memcpy(&bits, &v, 4); AppendU32(b, bits);
}
static void AppendStr(std::vector<uint8_t>& b, const char* s) {
    uint16_t len = (uint16_t)strlen(s);
    AppendU16(b, len);
    for (uint16_t i = 0; i < len; i++) b.push_back((uint8_t)s[i]);
}

struct Reader {
    const uint8_t* data; size_t size; size_t pos = 0;
    bool ok = true;
    bool need(size_t n) { if (pos + n > size) { ok = false; return false; } return true; }
    uint8_t ReadU8() { if (!need(1)) return 0; return data[pos++]; }
    uint16_t ReadU16() { if (!need(2)) return 0; uint16_t v = data[pos] | (data[pos+1] << 8); pos += 2; return v; }
    uint32_t ReadU32() { if (!need(4)) return 0; uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)data[pos+i] << (8*i); pos += 4; return v; }
    int32_t ReadI32() { return (int32_t)ReadU32(); }
    float ReadF32() { uint32_t bits = ReadU32(); float f; memcpy(&f, &bits, 4); return f; }
    std::string ReadStr() {
        uint16_t len = ReadU16();
        if (!need(len)) return "";
        std::string s((const char*)&data[pos], len);
        pos += len;
        return s;
    }
};

bool SaveGame(World& w, Player& p, int slot) {
    std::vector<uint8_t> buf;
    AppendU32(buf, ('G' << 24) | ('L' << 16) | ('X' << 8) | 'V'); // magic "VXLG" (little-endian on disk)
    AppendU32(buf, SAVE_VERSION);

    AppendF32(buf, p.x); AppendF32(buf, p.y); AppendF32(buf, p.z);
    AppendF32(buf, p.yaw); AppendF32(buf, p.pitch);
    AppendI32(buf, p.hotbarIndex);
    AppendF32(buf, g_dayTimeSeconds); // Section 13 -- world state, not a settings.cfg preference

    // No settings block as of v3 -- gameplay/UI preferences live in the
    // separate global settings.cfg (Section 7.2.3) now, not here.

    AppendU32(buf, BLOCK_COUNT);
    for (int i = 0; i < BLOCK_COUNT; i++) AppendStr(buf, g_blockNames[i]);

    // Count non-air blocks first. Walks both World::chunks (currently
    // resident) and g_evictedChunks (out-of-radius but real, Section
    // 2.4-perf) -- a chunk is in exactly one of the two at any time, and
    // skipping the second would silently drop whatever the player built
    // in any area they've since walked away from.
    uint32_t blockCount = 0;
    auto countBlocks = [&](const uint8_t* blocks) {
        for (int i = 0; i < CHUNK_CELLS; i++) if (blocks[i] != BLOCK_AIR) blockCount++;
    };
    for (auto& kv : w.chunks) countBlocks(kv.second->blocks);
    for (auto& kv : g_evictedChunks) countBlocks(kv.second->blocks);
    AppendU32(buf, blockCount);

    auto writeBlocks = [&](const ChunkCoord& cc, const uint8_t* blocks) {
        int baseX = cc.x * CHUNK_SIZE, baseY = cc.y * CHUNK_SIZE, baseZ = cc.z * CHUNK_SIZE;
        for (int ly = 0; ly < CHUNK_SIZE; ly++)
            for (int lz = 0; lz < CHUNK_SIZE; lz++)
                for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                    uint8_t id = blocks[Chunk::LocalIndex(lx, ly, lz)];
                    if (id == BLOCK_AIR) continue;
                    AppendI32(buf, baseX + lx);
                    AppendI32(buf, baseY + ly);
                    AppendI32(buf, baseZ + lz);
                    AppendU8(buf, id);
                }
    };
    for (auto& kv : w.chunks) writeBlocks(kv.first, kv.second->blocks);
    for (auto& kv : g_evictedChunks) writeBlocks(kv.first, kv.second->blocks);

    uint32_t checksum = Fnv1a(buf.data(), buf.size());
    AppendU32(buf, checksum);

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
    if (size < 12) return false;
    in.seekg(0);
    std::vector<uint8_t> buf((size_t)size);
    in.read((char*)buf.data(), size);
    if (!in) return false;

    if (buf.size() < 4) return false;
    uint32_t storedChecksum;
    memcpy(&storedChecksum, buf.data() + buf.size() - 4, 4);
    uint32_t computed = Fnv1a(buf.data(), buf.size() - 4);
    if (storedChecksum != computed) {
        OutputDebugStringA("LoadGame: checksum mismatch, aborting load\n");
        return false;
    }

    Reader r{ buf.data(), buf.size() - 4 };
    uint32_t magic = r.ReadU32();
    uint32_t expectedMagic = ('G' << 24) | ('L' << 16) | ('X' << 8) | 'V';
    if (magic != expectedMagic) {
        OutputDebugStringA("LoadGame: bad magic, aborting load\n");
        return false;
    }
    uint32_t version = r.ReadU32();
    // v2 (settings embedded in the save) and v3 (settings moved out, no
    // day clock yet) are both still loadable, not just the current v4
    // (Section 7.2.3/13) -- each older version's world/player data is a
    // strict prefix of the newer format, just missing fields added
    // since. Loading an older save fills those in with sensible
    // defaults (below) rather than refusing an otherwise-fine world.
    if (version != 2 && version != 3 && version != SAVE_VERSION) {
        OutputDebugStringA("LoadGame: unsupported version, aborting load\n");
        return false;
    }
    bool hasLegacySettings = (version == 2);
    bool hasDayTime = (version >= 4);

    Player loaded;
    loaded.x = r.ReadF32(); loaded.y = r.ReadF32(); loaded.z = r.ReadF32();
    loaded.yaw = r.ReadF32(); loaded.pitch = r.ReadF32();
    loaded.hotbarIndex = r.ReadI32();

    // Day clock (Section 13): absent on v2/v3 saves made before it
    // existed -- those resume at dawn (0.0) rather than needing a
    // meaningless stored value.
    float loadedDayTime = 0.0f;
    if (hasDayTime) loadedDayTime = r.ReadF32();

    // Legacy (v2-only) settings block: read into locals first, same as
    // the rest of this function -- nothing gets applied to live state
    // until the whole load is known to be valid. Absent entirely on v3.
    float loadedSensX = 0, loadedSensY = 0;
    bool loadedInvertX = false, loadedInvertY = false;
    int32_t loadedRenderDist = 0;
    bool loadedShowFPS = false;
    float loadedVolume = 0;
    std::vector<std::pair<std::string, int32_t>> loadedBindings;
    if (hasLegacySettings) {
        loadedSensX = r.ReadF32(); loadedSensY = r.ReadF32();
        loadedInvertX = r.ReadU8() != 0; loadedInvertY = r.ReadU8() != 0;
        loadedRenderDist = r.ReadI32();
        loadedShowFPS = r.ReadU8() != 0;
        loadedVolume = r.ReadF32();
        uint32_t bindCount = r.ReadU32();
        loadedBindings.resize(bindCount);
        for (uint32_t i = 0; i < bindCount; i++) {
            loadedBindings[i].first = r.ReadStr();
            loadedBindings[i].second = r.ReadI32();
        }
        if (!r.ok) return false;
    }

    uint32_t nameCount = r.ReadU32();
    std::vector<std::string> savedNames(nameCount);
    for (uint32_t i = 0; i < nameCount; i++) savedNames[i] = r.ReadStr();
    if (!r.ok) return false;

    // Remap saved name index -> current BlockID. Anything no longer
    // present maps to AIR with a logged warning (Section 7.4) rather
    // than silently reinterpreting whatever ID occupies that slot today.
    std::vector<BlockID> remap(nameCount, BLOCK_AIR);
    for (uint32_t i = 0; i < nameCount; i++) {
        bool found = false;
        for (int b = 0; b < BLOCK_COUNT; b++) {
            if (savedNames[i] == g_blockNames[b]) { remap[i] = (BlockID)b; found = true; break; }
        }
        if (!found) {
            char msg[256];
            snprintf(msg, sizeof(msg), "LoadGame: unknown block name '%s', mapping to air\n", savedNames[i].c_str());
            OutputDebugStringA(msg);
        }
    }

    uint32_t blockCount = r.ReadU32();
    if (!r.ok) return false;

    World fresh; // build into a scratch world; only swap in if fully valid
    for (uint32_t i = 0; i < blockCount; i++) {
        int32_t x = r.ReadI32(), y = r.ReadI32(), z = r.ReadI32();
        uint8_t nameIdx = r.ReadU8();
        if (!r.ok) return false;
        BlockID id = (nameIdx < remap.size()) ? remap[nameIdx] : BLOCK_AIR;
        fresh.SetRaw(x, y, z, id); // bulk load path, no gravity (Section 5.2)
    }

    // The placeable roster can shrink between builds (pipes were removed),
    // so a save made with a now-nonexistent hotbar slot selected must not
    // index past the end of g_placeable.
    if (loaded.hotbarIndex < 0 || loaded.hotbarIndex >= g_placeableCount) loaded.hotbarIndex = 0;
    p = loaded;
    g_dayTimeSeconds = loadedDayTime;

    if (hasLegacySettings) {
        g_sensitivityMultX = loadedSensX; g_sensitivityMultY = loadedSensY;
        g_invertX = loadedInvertX; g_invertY = loadedInvertY;
        g_loadRadius = loadedRenderDist;
        ClampSettingsToValidRanges();
        g_showFPS = loadedShowFPS;
        g_masterVolume = loadedVolume;
        // Remap saved keybinding action names -> current GameAction indices,
        // the same name-indexed pattern as the block remap above (Section
        // 3.1): an unrecognized action name is skipped with a warning
        // instead of corrupting some other action's binding, and any action
        // absent from the save simply keeps its pre-load value.
        for (auto& kv : loadedBindings) {
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
        // created settings.cfg yet -- once it exists, it's the source of
        // truth and this block never overwrites it again.
        if (!std::filesystem::exists(GetSettingsFilePath())) {
            SaveSettings();
        }
    }

    // Every column in the save is marked generated, so terrain-gen never
    // re-runs over it and stomps edits. Only columns near the loaded
    // player become resident; the rest go straight to the eviction store
    // rather than all being meshed on the first frames and then evicted
    // (a large save would otherwise stall the view around the player
    // behind thousands of far-away rebuilds). Placed after the legacy
    // settings block since that can change g_loadRadius.
    w.ClearChunks();
    g_evictedChunks.clear();
    g_generatedColumns.clear();
    g_residentColumns.clear();
    int pcx = FloorDiv16((int)floorf(p.x)), pcz = FloorDiv16((int)floorf(p.z));
    for (auto& kv : fresh.chunks) {
        long long key = ColumnKey(kv.first.x, kv.first.z);
        g_generatedColumns.insert(key);
        if (ColumnDistance(kv.first.x, kv.first.z, pcx, pcz) <= g_loadRadius + CHUNK_EVICT_MARGIN) {
            g_residentColumns.insert(key);
            w.AdoptChunk(kv.first, std::move(kv.second));
        } else {
            g_evictedChunks.emplace(kv.first, std::move(kv.second));
        }
    }
    ClearFallQueue();
    g_pendingColumns.clear();
    g_pendingColumnSet.clear();
    g_pendingEvictions.clear();
    g_pendingEvictionSet.clear();
    g_lastPlayerChunkX = INT32_MIN;
    g_lastPlayerChunkZ = INT32_MIN;
    return true;
}
