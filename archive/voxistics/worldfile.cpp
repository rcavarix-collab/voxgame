// worldfile.cpp -- see worldfile.h. Layout of a v5 file (little-endian):
//
//   u32 magic "VXLG", u32 version
//   player: f32 x, y, z, yaw, pitch; i32 hotbarIndex; f32 dayTime
//   generator: str name, u32 version, u64 seed
//   u32 nameCount, str name[nameCount]        (block identity, Section 3.1)
//   u32 chunkCount, then per chunk:
//     i32 cx, cy, cz; u8 flags (1 = has state, 2 = has data)
//     blocks: runs of (u16 length, u16 nameIndex) covering all 4096 cells
//     state (flag 1): runs of (u16 length, u8 value) covering 4096 cells
//     data (flag 2): u16 count, then (u16 cell, u32 length, bytes) each
//   u32 updateCount, then per update (v6+):
//     i32 x, y, z; u8 kind; u32 delay (ticks from now)
//   The Line (v7+): u32 cellCount, then (i64 cell, f32 seconds) each;
//     f64 angular momentum; f32 angle
//   Essence (v8+): u32 zoneCount, u64 zone ids; u32 attractorCount, (i32 x, y, z) each
//   u32 FNV-1a checksum of everything before it
//
// Cells run in Chunk::LocalIndex order (x fastest, then z, then y), so
// the horizontal layers typical of terrain and buildings compress into a
// handful of runs.

#include "worldfile.h"
#include <cstring>

namespace {

const uint32_t MAGIC = ('G' << 24) | ('L' << 16) | ('X' << 8) | 'V';

uint32_t Fnv1a(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

struct Writer {
    std::vector<uint8_t>& b;
    void U8(uint8_t v) { b.push_back(v); }
    void U16(uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
    void U32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
    void U64(uint64_t v) { for (int i = 0; i < 8; i++) b.push_back((uint8_t)(v >> (8 * i))); }
    void F64(double v) { uint64_t bits; memcpy(&bits, &v, 8); U64(bits); }
    void I32(int32_t v) { U32((uint32_t)v); }
    void F32(float v) { uint32_t bits; memcpy(&bits, &v, 4); U32(bits); }
    void Str(const char* s) {
        size_t n = strlen(s); if (n > 0xFFFF) n = 0xFFFF;
        U16((uint16_t)n); b.insert(b.end(), (const uint8_t*)s, (const uint8_t*)s + n);
    }
};

struct Reader {
    const uint8_t* data; size_t size; size_t pos = 0;
    bool ok = true;
    bool need(size_t n) { if (!ok || size - pos < n) { ok = false; return false; } return true; }
    uint8_t U8() { if (!need(1)) return 0; return data[pos++]; }
    uint16_t U16() { if (!need(2)) return 0; uint16_t v = (uint16_t)(data[pos] | (data[pos + 1] << 8)); pos += 2; return v; }
    uint32_t U32() { if (!need(4)) return 0; uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)data[pos + i] << (8 * i); pos += 4; return v; }
    uint64_t U64() { if (!need(8)) return 0; uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)data[pos + i] << (8 * i); pos += 8; return v; }
    int32_t I32() { return (int32_t)U32(); }
    float F32() { uint32_t bits = U32(); float f; memcpy(&f, &bits, 4); return f; }
    double F64() { uint64_t bits = U64(); double d; memcpy(&d, &bits, 8); return d; }
    std::string Str() {
        uint16_t n = U16();
        if (!need(n)) return "";
        std::string s((const char*)data + pos, n); pos += n;
        return s;
    }
};

void EncodeChunk(Writer& w, const ChunkCoord& cc, const Chunk& c) {
    w.I32(cc.x); w.I32(cc.y); w.I32(cc.z);
    bool hasState = false;
    for (int i = 0; i < CHUNK_CELLS && !hasState; i++) hasState = c.state[i] != 0;
    bool hasData = c.data && !c.data->empty();
    w.U8((uint8_t)((hasState ? 1 : 0) | (hasData ? 2 : 0)));

    // Block IDs are written as-is: the name table written with them is in
    // registry order, so ID == name index on the way out.
    for (int i = 0; i < CHUNK_CELLS;) {
        int j = i + 1;
        while (j < CHUNK_CELLS && c.blocks[j] == c.blocks[i]) j++;
        w.U16((uint16_t)(j - i)); w.U16(c.blocks[i]);
        i = j;
    }
    if (hasState) {
        for (int i = 0; i < CHUNK_CELLS;) {
            int j = i + 1;
            while (j < CHUNK_CELLS && c.state[j] == c.state[i]) j++;
            w.U16((uint16_t)(j - i)); w.U8(c.state[i]);
            i = j;
        }
    }
    if (hasData) {
        w.U16((uint16_t)c.data->size());
        for (const auto& kv : *c.data) {
            w.U16(kv.first);
            w.U32((uint32_t)kv.second.size());
            w.b.insert(w.b.end(), kv.second.begin(), kv.second.end());
        }
    }
}

bool DecodeChunk(Reader& r, const std::vector<BlockID>& remap, ChunkMap& out) {
    ChunkCoord cc;
    cc.x = r.I32(); cc.y = r.I32(); cc.z = r.I32();
    uint8_t flags = r.U8();
    if (!r.ok || cc.y < 0 || cc.y > Y_MAX / CHUNK_SIZE) return false;
    auto c = std::make_unique<Chunk>();
    for (int i = 0; i < CHUNK_CELLS;) {
        uint16_t run = r.U16(), idx = r.U16();
        if (!r.ok || run == 0 || run > CHUNK_CELLS - i) return false;
        BlockID id = idx < remap.size() ? remap[idx] : BLOCK_AIR;
        memset(c->blocks + i, id, run);
        i += run;
    }
    if (flags & 1) {
        for (int i = 0; i < CHUNK_CELLS;) {
            uint16_t run = r.U16(); uint8_t v = r.U8();
            if (!r.ok || run == 0 || run > CHUNK_CELLS - i) return false;
            memset(c->state + i, v, run);
            i += run;
        }
    }
    if (flags & 2) {
        uint16_t n = r.U16();
        if (n > 0) c->data = std::make_unique<std::unordered_map<uint16_t, std::vector<uint8_t>>>();
        for (uint16_t k = 0; k < n; k++) {
            uint16_t cell = r.U16(); uint32_t len = r.U32();
            if (!r.need(len) || cell >= CHUNK_CELLS) return false;
            (*c->data)[cell].assign(r.data + r.pos, r.data + r.pos + len);
            r.pos += len;
        }
    }
    c->modified = true;
    out[cc] = std::move(c);
    return true;
}

// v2-v4: a flat list of (x, y, z, nameIndex) for every non-air block,
// made with the original hills terrain.
bool DecodeLegacyBlocks(Reader& r, const std::vector<BlockID>& remap, SaveData& out) {
    uint32_t blockCount = r.U32();
    if (!r.ok || blockCount > (r.size - r.pos) / 13) return false;
    for (uint32_t i = 0; i < blockCount; i++) {
        int32_t x = r.I32(), y = r.I32(), z = r.I32();
        uint8_t idx = r.U8();
        if (!r.ok) return false;
        if (y < Y_MIN || y > Y_MAX) continue;
        BlockID id = idx < remap.size() ? remap[idx] : BLOCK_AIR;
        ChunkCoord cc = World::ToChunk(x, y, z);
        std::unique_ptr<Chunk>& c = out.chunks[cc];
        if (!c) { c = std::make_unique<Chunk>(); c->modified = true; }
        c->blocks[Chunk::LocalIndex(LocalOf(x, cc.x), LocalOf(y, cc.y), LocalOf(z, cc.z))] = (uint8_t)id;
    }
    out.gen.type = GEN_HILLS;
    out.gen.version = 1;
    out.gen.seed = 0;
    // A legacy save holds every non-air block, so a hills chunk the
    // player dug out completely isn't in it at all -- without an explicit
    // empty chunk, regeneration would quietly refill it. Hills v1 never
    // reaches above y = 60 (chunk row 3).
    std::vector<std::pair<int, int>> columns;
    for (const auto& kv : out.chunks) columns.push_back({ kv.first.x, kv.first.z });
    for (const auto& col : columns)
        for (int cy = 0; cy <= 60 / CHUNK_SIZE; cy++) {
            std::unique_ptr<Chunk>& c = out.chunks[{ col.first, cy, col.second }];
            if (!c) { c = std::make_unique<Chunk>(); c->modified = true; }
        }
    return true;
}

} // namespace

const char* DecodeResultText(DecodeResult r) {
    switch (r) {
    case DecodeResult::Ok: return "ok";
    case DecodeResult::Truncated: return "file truncated";
    case DecodeResult::BadChecksum: return "checksum mismatch";
    case DecodeResult::BadMagic: return "not a save file";
    case DecodeResult::UnsupportedVersion: return "unsupported save version";
    case DecodeResult::UnknownGenerator: return "made with a world generator this build doesn't have";
    case DecodeResult::Corrupt: return "corrupt data";
    }
    return "?";
}

void EncodeSave(const Player& p, float dayTime, const WorldGenParams& gen,
                const World& world, const ChunkMap& evicted, const std::vector<PendingUpdate>& updates,
                const LineSaveData& line, const EssenceNetwork::SaveData& essence, std::vector<uint8_t>& out) {
    out.clear();
    Writer w{ out };
    w.U32(MAGIC);
    w.U32(SAVE_VERSION);
    w.F32(p.x); w.F32(p.y); w.F32(p.z); w.F32(p.yaw); w.F32(p.pitch);
    w.I32(p.hotbarIndex);
    w.F32(dayTime);
    w.Str(WorldGenName(gen.type)); w.U32(gen.version); w.U64(gen.seed);
    w.U32(BLOCK_COUNT);
    for (int i = 0; i < BLOCK_COUNT; i++) w.Str(g_blocks[i].name);

    uint32_t chunkCount = 0;
    for (const auto& kv : world.chunks) if (kv.second->modified) chunkCount++;
    for (const auto& kv : evicted) if (kv.second->modified) chunkCount++;
    w.U32(chunkCount);
    for (const auto& kv : world.chunks) if (kv.second->modified) EncodeChunk(w, kv.first, *kv.second);
    for (const auto& kv : evicted) if (kv.second->modified) EncodeChunk(w, kv.first, *kv.second);

    // Pending updates, so a save taken mid-collapse finishes collapsing.
    w.U32((uint32_t)updates.size());
    for (const PendingUpdate& u : updates) { w.I32(u.x); w.I32(u.y); w.I32(u.z); w.U8(u.kind); w.U32(u.delay); }

    // The Line (Part XVIII): only its history -- everything else re-derives.
    w.U32((uint32_t)line.cells.size());
    for (const LineCellSave& c : line.cells) { w.F32(c.x); w.F32(c.z); w.F32(c.seconds); }
    w.F64(line.angMom);
    w.F32(line.theta);

    // Essence network (Part XIX): what's been discovered, and what was built.
    w.U32((uint32_t)essence.discoveredZones.size());
    for (uint64_t id : essence.discoveredZones) w.U64(id);
    w.U32((uint32_t)(essence.attractors.size() / 3));
    for (size_t i = 0; i + 2 < essence.attractors.size(); i += 3) { w.I32(essence.attractors[i]); w.I32(essence.attractors[i + 1]); w.I32(essence.attractors[i + 2]); }

    w.U32(Fnv1a(out.data(), out.size()));
}

DecodeResult DecodeSave(const uint8_t* data, size_t size, SaveData& out) {
    if (size < 12) return DecodeResult::Truncated;
    uint32_t stored;
    memcpy(&stored, data + size - 4, 4); // little-endian host (x86/x64)
    if (stored != Fnv1a(data, size - 4)) return DecodeResult::BadChecksum;

    Reader r{ data, size - 4 };
    if (r.U32() != MAGIC) return DecodeResult::BadMagic;
    out.version = r.U32();
    // v2 embedded preferences, v3 moved them out, v4 added the day clock,
    // v5 added the generator and per-chunk storage, v6 pending updates,
    // v7 The Line, v8 the essence map's discoveries, v9 where in each of
    // The Line's cells the time was spent (Section 7.2).
    if (out.version < 2 || out.version > SAVE_VERSION) return DecodeResult::UnsupportedVersion;

    Player& p = out.player;
    p.x = r.F32(); p.y = r.F32(); p.z = r.F32(); p.yaw = r.F32(); p.pitch = r.F32();
    p.hotbarIndex = r.I32();
    out.dayTime = out.version >= 4 ? r.F32() : 0.0f; // pre-clock saves resume at dawn

    if (out.version == 2) {
        out.hasLegacySettings = true;
        out.legacySensX = r.F32(); out.legacySensY = r.F32();
        out.legacyInvertX = r.U8() != 0; out.legacyInvertY = r.U8() != 0;
        out.legacyRenderDist = r.I32();
        out.legacyShowFPS = r.U8() != 0;
        out.legacyVolume = r.F32();
        uint32_t bindCount = r.U32();
        if (!r.ok || bindCount > 256) return DecodeResult::Corrupt;
        out.legacyBindings.resize(bindCount);
        for (uint32_t i = 0; i < bindCount; i++) {
            out.legacyBindings[i].first = r.Str();
            out.legacyBindings[i].second = r.I32();
        }
    }

    if (out.version >= 5) {
        std::string genName = r.Str();
        out.gen.version = r.U32();
        out.gen.seed = r.U64();
        if (!r.ok) return DecodeResult::Truncated;
        if (!WorldGenFromName(genName.c_str(), out.gen.type) || out.gen.version == 0 ||
            out.gen.version > WorldGenLatestVersion(out.gen.type))
            return DecodeResult::UnknownGenerator;
    }

    // Saved name index -> current BlockID. A name this build doesn't know
    // becomes air (Section 7.4) rather than whatever ID has that slot now.
    uint32_t nameCount = r.U32();
    if (!r.ok || nameCount > 65536) return DecodeResult::Corrupt;
    std::vector<BlockID> remap(nameCount, BLOCK_AIR);
    for (uint32_t i = 0; i < nameCount; i++) {
        std::string name = r.Str();
        bool found = false;
        for (int b = 0; b < BLOCK_COUNT; b++)
            if (name == g_blocks[b].name) { remap[i] = (BlockID)b; found = true; break; }
        if (!found && r.ok) out.unknownBlockNames.push_back(name);
    }
    if (!r.ok) return DecodeResult::Truncated;

    if (out.version >= 5) {
        uint32_t chunkCount = r.U32();
        if (!r.ok) return DecodeResult::Truncated;
        for (uint32_t i = 0; i < chunkCount; i++)
            if (!DecodeChunk(r, remap, out.chunks)) return r.ok ? DecodeResult::Corrupt : DecodeResult::Truncated;
        if (out.version >= 6) {
            uint32_t n = r.U32();
            if (!r.ok || n > (r.size - r.pos) / 17) return DecodeResult::Corrupt;
            out.updates.resize(n);
            for (PendingUpdate& u : out.updates) {
                u.x = r.I32(); u.y = r.I32(); u.z = r.I32();
                u.kind = (UpdateKind)r.U8(); u.delay = r.U32();
            }
            if (!r.ok) return DecodeResult::Truncated;
        }
        if (out.version >= 7) {
            uint32_t n = r.U32();
            if (!r.ok || n > (r.size - r.pos) / 12) return DecodeResult::Corrupt;
            out.line.cells.resize(n);
            for (LineCellSave& c : out.line.cells) {
                if (out.version >= 9) { c.x = r.F32(); c.z = r.F32(); c.seconds = r.F32(); continue; }
                // v7/v8: a 32-block cell key and its seconds; the time is
                // taken to have been spent at the cell's centre.
                uint64_t key = r.U64();
                int cx = (int)(uint32_t)(key >> 32), cz = (int)(uint32_t)(key & 0xFFFFFFFFu);
                c.x = (cx + 0.5f) * 32.0f; c.z = (cz + 0.5f) * 32.0f;
                c.seconds = r.F32();
            }
            out.line.angMom = r.F64();
            out.line.theta = r.F32();
            if (!r.ok) return DecodeResult::Truncated;
        }
        if (out.version >= 8) {
            uint32_t nz = r.U32();
            if (!r.ok || nz > (r.size - r.pos) / 8) return DecodeResult::Corrupt;
            out.essence.discoveredZones.resize(nz);
            for (uint64_t& id : out.essence.discoveredZones) id = r.U64();
            uint32_t na = r.U32();
            if (!r.ok || na > (r.size - r.pos) / 12) return DecodeResult::Corrupt;
            out.essence.attractors.resize((size_t)na * 3);
            for (int32_t& v : out.essence.attractors) v = r.I32();
            if (!r.ok) return DecodeResult::Truncated;
        }
    } else {
        if (!DecodeLegacyBlocks(r, remap, out)) return r.ok ? DecodeResult::Corrupt : DecodeResult::Truncated;
    }
    return DecodeResult::Ok;
}
