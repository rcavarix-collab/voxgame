// worldfile.h
//
// The world save format (DESIGN.md Part VII): encoding a world to bytes
// and decoding bytes back, with no file or OS access -- persist.cpp does
// the disk side (crash-safe write, slot paths) and applies the result to
// live state. Kept free of <windows.h> so it compiles and is tested
// natively (tests/).
//
// v5 stores only modified chunks (Section 7.2): everything else is
// regenerated from the world's recorded generator; v6 adds pending
// scheduled block updates, v7 The Line's state, v8 the essence map's
// discoveries. v2-v7 still load.

#pragma once

#include "world.h"
#include "theline.h"
#include "essence.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

static const uint32_t SAVE_VERSION = 8;

using ChunkMap = std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>;

struct SaveData {
    Player player;
    float dayTime = 0.0f;
    WorldGenParams gen;
    ChunkMap chunks; // every chunk that differs from the generator, all flagged modified
    std::vector<PendingUpdate> updates; // scheduled block updates still pending (v6+)
    LineSaveData line;                  // The Line's pivot history, spin and angle (v7+)
    EssenceNetwork::SaveData essence;   // discovered zones and built attractors (v8+)

    uint32_t version = 0; // of the file that was read
    // v2 only: preferences that used to live in the save (Section 7.2.3).
    bool hasLegacySettings = false;
    float legacySensX = 0, legacySensY = 0;
    bool legacyInvertX = false, legacyInvertY = false;
    int32_t legacyRenderDist = 0;
    bool legacyShowFPS = false;
    float legacyVolume = 0;
    std::vector<std::pair<std::string, int32_t>> legacyBindings;
    // Saved block names this build doesn't know (loaded as air).
    std::vector<std::string> unknownBlockNames;
};

enum class DecodeResult { Ok, Truncated, BadChecksum, BadMagic, UnsupportedVersion, UnknownGenerator, Corrupt };
const char* DecodeResultText(DecodeResult r);

// Every modified chunk from both the resident world and the eviction
// store. Unmodified chunks are skipped.
void EncodeSave(const Player& p, float dayTime, const WorldGenParams& gen,
                const World& w, const ChunkMap& evicted, const std::vector<PendingUpdate>& updates,
                const LineSaveData& line, const EssenceNetwork::SaveData& essence, std::vector<uint8_t>& out);

DecodeResult DecodeSave(const uint8_t* data, size_t size, SaveData& out);
