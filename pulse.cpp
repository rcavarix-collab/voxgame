// pulse.cpp -- see pulse.h.

#include "pulse.h"
#include "shapes.h"
#include <cmath>
#include <deque>

PulseSystem g_pulse;
PulseTuning g_pulseTuning;

namespace {

const int kDir[FACE_COUNT][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

PulseCell Step(const PulseCell& c, int face) { return { c.x + kDir[face][0], c.y + kDir[face][1], c.z + kDir[face][2] }; }

int FaceBetween(const PulseCell& a, const PulseCell& b) {
    for (int f = 0; f < FACE_COUNT; f++)
        if (b.x - a.x == kDir[f][0] && b.y - a.y == kDir[f][1] && b.z - a.z == kDir[f][2]) return f;
    return -1;
}

// Somewhere pulse can be delivered (a harvester only ever gives).
bool IsStore(BlockID id) { return PulseCapacity(id) > 0 && id != BLOCK_PULSE_HARVESTER; }

uint8_t JoinedAt(World& w, const PulseCell& c) {
    BlockID nb[FACE_COUNT];
    for (int f = 0; f < FACE_COUNT; f++) { PulseCell n = Step(c, f); nb[f] = w.Get(n.x, n.y, n.z); }
    return PipeJoinMask(nb);
}

// The data record: a version byte, then the count (little-endian u32).
std::vector<uint8_t>* Record(World& w, int x, int y, int z, bool create) {
    if (y < Y_MIN || y > Y_MAX) return nullptr;
    ChunkCoord cc = World::ToChunk(x, y, z);
    Chunk* c = w.FindChunk(cc);
    if (!c) return nullptr;
    uint16_t li = (uint16_t)Chunk::LocalIndex(LocalOf(x, cc.x), LocalOf(y, cc.y), LocalOf(z, cc.z));
    if (!c->data) {
        if (!create) return nullptr;
        c->data = std::make_unique<std::unordered_map<uint16_t, std::vector<uint8_t>>>();
    }
    auto it = c->data->find(li);
    if (it == c->data->end()) {
        if (!create) return nullptr;
        c->modified = true;
        return &(*c->data)[li];
    }
    return &it->second;
}

} // namespace

int PulseCapacity(BlockID id) {
    switch (id) {
    case BLOCK_PULSE_STORE: return 256;
    case BLOCK_CHEST: return 64;
    case BLOCK_MACHINE: return 32;          // a buffer, until pulse has a use
    case BLOCK_PULSE_HARVESTER: return 8;   // what it holds when nothing will take it
    default: return 0;
    }
}

int PulseStored(World& w, int x, int y, int z) {
    std::vector<uint8_t>* r = Record(w, x, y, z, false);
    if (!r || r->size() < 5 || (*r)[0] != 1) return 0;
    return (int)((*r)[1] | ((*r)[2] << 8) | ((*r)[3] << 16) | ((uint32_t)(*r)[4] << 24));
}

void SetPulseStored(World& w, int x, int y, int z, int count) {
    std::vector<uint8_t>* r = Record(w, x, y, z, true);
    if (!r) return;
    uint32_t n = count < 0 ? 0u : (uint32_t)count;
    r->assign({ 1, (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) });
    ChunkCoord cc = World::ToChunk(x, y, z);
    if (Chunk* c = w.FindChunk(cc)) c->modified = true;
}

void PulseSystem::Reset() {
    *this = PulseSystem();
}

void PulseSystem::OnPlaced(int x, int y, int z, BlockID id) {
    if (id == BLOCK_PULSE_HARVESTER) m_harvesters.insert({ x, y, z });
}

void PulseSystem::OnChunkArrived(const ChunkCoord& cc, const Chunk& c) {
    for (int i = 0; i < CHUNK_CELLS; i++) {
        if (c.blocks[i] != BLOCK_PULSE_HARVESTER) continue;
        int lx = i % CHUNK_SIZE, lz = (i / CHUNK_SIZE) % CHUNK_SIZE, ly = i / (CHUNK_SIZE * CHUNK_SIZE);
        m_harvesters.insert({ cc.x * CHUNK_SIZE + lx, cc.y * CHUNK_SIZE + ly, cc.z * CHUNK_SIZE + lz });
    }
}

// The network a pipe belongs to, found by flood fill the first time it's
// asked for after the world last changed.
int PulseSystem::NetworkAt(World& w, const PulseTuning& t, const PulseCell& pipe) {
    if (w.edits != m_seenEdits) { m_pipeNet.clear(); m_nets.clear(); m_seenEdits = w.edits; }
    auto it = m_pipeNet.find(pipe);
    if (it != m_pipeNet.end()) return it->second;
    if (w.Get(pipe.x, pipe.y, pipe.z) != BLOCK_PULSE_PIPE) return -1;
    int id = (int)m_nets.size();
    m_nets.emplace_back();
    Network& net = m_nets.back();
    std::deque<PulseCell> open{ pipe };
    m_pipeNet[pipe] = id;
    while (!open.empty()) {
        PulseCell c = open.front(); open.pop_front();
        net.pipes.push_back(c);
        BlockID nb[FACE_COUNT];
        for (int f = 0; f < FACE_COUNT; f++) {
            PulseCell n = Step(c, f);
            nb[f] = w.Get(n.x, n.y, n.z);
            if (nb[f] == BLOCK_PULSE_PIPE) {
                if (m_pipeNet.count(n) || (int)(net.pipes.size() + open.size()) >= t.maxNetworkCells) continue;
                m_pipeNet[n] = id;
                open.push_back(n);
            } else if (IsStore(nb[f])) {
                net.outlets.push_back({ c, n, f, false });
            }
        }
        uint8_t mouths = PipeMouths(PipeJoinMask(nb), w.GetState(c.x, c.y, c.z));
        for (int f = 0; f < FACE_COUNT; f++)
            if ((mouths & (1u << f)) && !BlockSolid(nb[f])) net.outlets.push_back({ c, Step(c, f), f, true });
    }
    return id;
}

// Sends a pulse entering the network at pipe `from` (out of `source`, if
// it came from a block) to the next outlet in turn that will take it.
bool PulseSystem::Route(World& w, const PulseTuning& t, const PulseCell& from, const PulseCell* source, int skipMouthFace, Moving& out) {
    int id = NetworkAt(w, t, from);
    if (id < 0) return false;
    Network& net = m_nets[id];
    const size_t n = net.outlets.size();
    const Outlet* pick = nullptr;
    for (size_t k = 0; k < n && !pick; k++) {
        const Outlet& o = net.outlets[(net.next + k) % n];
        if (o.mouth) {
            if (o.pipe == from && o.face == skipMouthFace) continue; // not straight back out where it came in
        } else {
            BlockID b = w.Get(o.target.x, o.target.y, o.target.z);
            auto r = m_reserved.find(o.target);
            int coming = r == m_reserved.end() ? 0 : r->second;
            if (PulseStored(w, o.target.x, o.target.y, o.target.z) + coming >= PulseCapacity(b)) continue;
        }
        pick = &o;
        net.next = (net.next + k + 1) % n;
    }
    if (!pick) return false;

    // The way there, through this network's pipes.
    std::unordered_map<PulseCell, PulseCell, PulseCellHash> came;
    std::deque<PulseCell> open{ from };
    came[from] = from;
    while (!open.empty() && !came.count(pick->pipe)) {
        PulseCell c = open.front(); open.pop_front();
        for (int f = 0; f < FACE_COUNT; f++) {
            PulseCell nx = Step(c, f);
            if (came.count(nx)) continue;
            auto m = m_pipeNet.find(nx);
            if (m == m_pipeNet.end() || m->second != id) continue;
            came[nx] = c;
            open.push_back(nx);
        }
    }
    if (!came.count(pick->pipe)) return false;
    std::vector<PulseCell> pipes;
    for (PulseCell c = pick->pipe;; c = came[c]) { pipes.push_back(c); if (c == from) break; }

    out = Moving();
    if (source) out.path.push_back(*source);
    out.pipesFrom = (int)out.path.size();
    out.path.insert(out.path.end(), pipes.rbegin(), pipes.rend());
    out.pipesTo = (int)out.path.size() - 1;
    out.toMouth = pick->mouth;
    out.face = pick->face;
    out.target = pick->target;
    if (!pick->mouth) {
        out.path.push_back(pick->target);
        m_reserved[pick->target]++;
        out.reserved = true;
    }
    return true;
}

void PulseSystem::Unreserve(Moving& m) {
    if (!m.reserved) return;
    auto r = m_reserved.find(m.target);
    if (r != m_reserved.end() && --r->second <= 0) m_reserved.erase(r);
    m.reserved = false;
}

void PulseSystem::Fly(Moving& m, const PulseCell& from, int face) {
    Unreserve(m);
    m.flying = true;
    m.face = face;
    m.px = from.x + 0.5f; m.py = from.y + 0.5f; m.pz = from.z + 0.5f;
    m.cell = from;
    m.flown = 0;
    m.path.clear();
}

// Straight on, whatever the world's gravity says, until something stops it.
bool PulseSystem::StepFlying(World& w, const PulseTuning& t, Moving& m, float dt) {
    float d = t.flySpeed * dt;
    m.px += kDir[m.face][0] * d; m.py += kDir[m.face][1] * d; m.pz += kDir[m.face][2] * d;
    m.flown += d;
    if (m.flown > t.flyRange) { lost++; return false; }
    PulseCell c{ (int)floorf(m.px), (int)floorf(m.py), (int)floorf(m.pz) };
    if (c == m.cell) return true;
    m.cell = c;
    if (c.y < Y_MIN || c.y > Y_MAX) { lost++; return false; }
    BlockID b = w.Get(c.x, c.y, c.z);
    if (b == BLOCK_PULSE_PIPE) {
        // Caught only by a mouth facing it; the side of a pipe is a wall.
        int facing = m.face ^ 1; // the pipe's face the pulse arrives through
        if (PipeMouths(JoinedAt(w, c), w.GetState(c.x, c.y, c.z)) & (1u << facing)) {
            Moving next;
            if (Route(w, t, c, nullptr, facing, next)) {
                next.age = m.age;
                m = std::move(next);
                caught++;
                return true;
            }
        }
        lost++;
        return false;
    }
    if (BlockSolid(b)) { lost++; return false; } // meets a surface: gone
    return true;
}

bool PulseSystem::StepPiped(World& w, const PulseTuning& t, Moving& m, float dt) {
    float before = m.along;
    m.along += t.pipeSpeed * dt;
    int last = (int)m.path.size() - 1;
    // Each pipe it enters must still be there; if one's gone, it leaks out
    // of the break, flying on the way it was going.
    for (int k = (int)floorf(before) + 1; k <= (int)floorf(m.along) && k <= last; k++) {
        if (k < m.pipesFrom || k > m.pipesTo) continue;
        const PulseCell& c = m.path[k];
        if (w.Get(c.x, c.y, c.z) != BLOCK_PULSE_PIPE) {
            int face = FaceBetween(m.path[k - 1 < 0 ? 0 : k - 1], c);
            if (k == 0 || face < 0) { Unreserve(m); lost++; return false; }
            Fly(m, m.path[k - 1], face);
            return true;
        }
    }
    if (m.along < (float)last) return true;
    if (m.toMouth) { Fly(m, m.path[last], m.face); return true; }
    // At the store: in, if there's still room; otherwise on to wherever
    // else will have it.
    Unreserve(m);
    BlockID b = w.Get(m.target.x, m.target.y, m.target.z);
    int have = PulseStored(w, m.target.x, m.target.y, m.target.z);
    if (IsStore(b) && have < PulseCapacity(b)) {
        SetPulseStored(w, m.target.x, m.target.y, m.target.z, have + 1);
        delivered++;
        return false;
    }
    Moving next;
    if (m.pipesTo >= m.pipesFrom && Route(w, t, m.path[m.pipesTo], nullptr, -1, next)) {
        next.age = m.age;
        m = std::move(next);
        return true;
    }
    lost++;
    return false;
}

void PulseSystem::Tick(World& w, const PulseTuning& t, double beats, float dt) {
    // Whole beats since last time (the day's clock wrapping round counts as one).
    int due = 0;
    if (m_haveBeat) {
        due = beats < m_lastBeat ? 1 : (int)(floor(beats) - floor(m_lastBeat));
        if (due > t.maxPerBeat) due = t.maxPerBeat;
    }
    m_lastBeat = beats; m_haveBeat = true;

    for (int beat = 0; beat < due; beat++) {
        for (auto it = m_harvesters.begin(); it != m_harvesters.end();) {
            const PulseCell h = *it;
            BlockID here = w.Get(h.x, h.y, h.z);
            if (here != BLOCK_PULSE_HARVESTER) {
                // Broken, or its chunk has gone (it'll be found again on return).
                it = m_harvesters.erase(it);
                continue;
            }
            ++it;
            int stored = PulseStored(w, h.x, h.y, h.z);
            if (stored < PulseCapacity(BLOCK_PULSE_HARVESTER)) { stored++; gathered++; }
            // Gives up to two a beat, so a backlog drains once there's room.
            for (int give = 0; give < 2 && stored > 0 && (int)m_moving.size() < t.maxInFlight; give++) {
                bool sent = false;
                for (int k = 0; k < FACE_COUNT && !sent; k++) {
                    int f = (int)((m_turn + k) % FACE_COUNT);
                    PulseCell n = Step(h, f);
                    BlockID b = w.Get(n.x, n.y, n.z);
                    if (b == BLOCK_PULSE_PIPE) {
                        Moving m;
                        if (Route(w, t, n, &h, -1, m)) { m_moving.push_back(std::move(m)); sent = true; }
                    } else if (IsStore(b)) { // a store right beside it: straight in
                        int have = PulseStored(w, n.x, n.y, n.z);
                        if (have < PulseCapacity(b)) { SetPulseStored(w, n.x, n.y, n.z, have + 1); delivered++; sent = true; }
                    }
                }
                m_turn++;
                if (!sent) break;
                stored--;
            }
            SetPulseStored(w, h.x, h.y, h.z, stored);
        }
    }

    for (size_t i = 0; i < m_moving.size();) {
        Moving& m = m_moving[i];
        m.age += dt;
        bool alive = m.flying ? StepFlying(w, t, m, dt) : StepPiped(w, t, m, dt);
        if (alive) { i++; continue; }
        if (i + 1 != m_moving.size()) m = std::move(m_moving.back());
        m_moving.pop_back();
    }
}

void PulseSystem::Views(std::vector<PulseView>& out) const {
    out.clear();
    for (const Moving& m : m_moving) {
        float fadeIn = m.age / 0.15f; fadeIn = fadeIn > 1 ? 1 : fadeIn;
        if (m.flying) {
            float fadeOut = (g_pulseTuning.flyRange - m.flown) / 4.0f; fadeOut = fadeOut > 1 ? 1 : (fadeOut < 0 ? 0 : fadeOut);
            out.push_back({ m.px, m.py, m.pz, fadeIn * fadeOut });
            continue;
        }
        if (m.path.empty()) continue;
        int last = (int)m.path.size() - 1;
        int i = (int)floorf(m.along); if (i > last) i = last; if (i < 0) i = 0;
        int j = i < last ? i + 1 : i;
        float f = m.along - (float)i; f = f < 0 ? 0 : (f > 1 ? 1 : f);
        const PulseCell& a = m.path[i];
        const PulseCell& b = m.path[j];
        out.push_back({ a.x + 0.5f + (b.x - a.x) * f, a.y + 0.5f + (b.y - a.y) * f, a.z + 0.5f + (b.z - a.z) * f, fadeIn });
    }
}
