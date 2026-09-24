// essencemap.cpp -- see essencemap.h.

#include "essencemap.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

struct Col { float r, g, b, a; };
const Col kBackground = { 0.02f, 0.03f, 0.06f, 1.0f }; // fully opaque: the world isn't drawn behind the map, and a trace of it would show
const Col kGrid = { 0.5f, 0.55f, 0.7f, 0.06f };
const Col kZone = { 0.62f, 0.52f, 1.0f, 1.0f };
const Col kAttractor = { 1.0f, 0.78f, 0.35f, 1.0f };
const Col kBelt = { 0.55f, 0.48f, 0.9f, 0.2f };
const Col kRoute = { 0.72f, 0.66f, 1.0f, 0.75f };
const Col kLabel = { 0.9f, 0.88f, 1.0f, 1.0f };

void Tri(MapDrawList& o, float x0, float y0, float x1, float y1, float x2, float y2, Col c0, Col c1, Col c2) {
    const float v[18] = { x0, y0, c0.r, c0.g, c0.b, c0.a, x1, y1, c1.r, c1.g, c1.b, c1.a, x2, y2, c2.r, c2.g, c2.b, c2.a };
    o.tris.insert(o.tris.end(), v, v + 18);
}
void Rect(MapDrawList& o, float x0, float y0, float x1, float y1, Col c) {
    Tri(o, x0, y0, x1, y0, x1, y1, c, c, c);
    Tri(o, x0, y0, x1, y1, x0, y1, c, c, c);
}
// A thick segment as a quad.
void Segment(MapDrawList& o, float x0, float y0, float x1, float y1, float w, Col c) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    Tri(o, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c, c, c);
    Tri(o, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c, c, c);
}
// Filled disc; `edge` colour at the rim gives soft belts / glows.
void Disc(MapDrawList& o, float x, float y, float r, Col centre, Col edge, int sides = 20) {
    for (int i = 0; i < sides; i++) {
        float a0 = 6.2831853f * i / sides, a1 = 6.2831853f * (i + 1) / sides;
        Tri(o, x, y, x + cosf(a0) * r, y + sinf(a0) * r, x + cosf(a1) * r, y + sinf(a1) * r, centre, edge, edge);
    }
}
void Ring(MapDrawList& o, float x, float y, float r, float w, Col c, int sides = 24) {
    for (int i = 0; i < sides; i++) {
        float a0 = 6.2831853f * i / sides, a1 = 6.2831853f * (i + 1) / sides;
        Segment(o, x + cosf(a0) * r, y + sinf(a0) * r, x + cosf(a1) * r, y + sinf(a1) * r, w, c);
    }
}

// A route's path as a polyline: straight, or -- for bound (entangled)
// routes only -- a quadratic Bezier bowed to one side, so it can't be
// mistaken for a physical path (cf. DrawDottedBezier in the older
// prototypes).
std::vector<std::pair<float, float>> RoutePath(float x0, float y0, float x1, float y1, bool curved) {
    std::vector<std::pair<float, float>> p;
    if (!curved) { p.push_back({ x0, y0 }); p.push_back({ x1, y1 }); return p; }
    float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    float dx = x1 - x0, dy = y1 - y0;
    float cx = mx - dy * 0.3f, cy = my + dx * 0.3f;
    const int N = 32;
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N, u = 1 - t;
        p.push_back({ u * u * x0 + 2 * u * t * cx + t * t * x1, u * u * y0 + 2 * u * t * cy + t * t * y1 });
    }
    return p;
}

// Walks a polyline drawing a pattern: solid, dashed (on/off) or dotted.
void Pattern(MapDrawList& o, const std::vector<std::pair<float, float>>& p, RouteStyle style, float w, Col c) {
    float on = style == RouteStyle::Intermittent ? 9.0f : 2.2f;   // dash / dot length
    float off = style == RouteStyle::Intermittent ? 6.0f : 5.0f;
    float phase = 0; // distance into the current on+off cycle
    for (size_t i = 0; i + 1 < p.size(); i++) {
        float x0 = p[i].first, y0 = p[i].second, x1 = p[i + 1].first, y1 = p[i + 1].second;
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (len < 1e-4f) continue;
        if (style == RouteStyle::Active) { Segment(o, x0, y0, x1, y1, w, c); continue; }
        float s = 0;
        while (s < len) {
            float cyc = on + off, inCyc = fmodf(phase, cyc);
            float step = inCyc < on ? on - inCyc : cyc - inCyc;
            float e = std::min(len, s + step);
            if (inCyc < on) {
                float t0 = s / len, t1 = e / len;
                float ax = x0 + (x1 - x0) * t0, ay = y0 + (y1 - y0) * t0, bx = x0 + (x1 - x0) * t1, by = y0 + (y1 - y0) * t1;
                if (style == RouteStyle::Planned) Rect(o, ax - w * 0.5f, ay - w * 0.5f, ax + w * 0.5f, ay + w * 0.5f, c);
                else Segment(o, ax, ay, bx, by, w, c);
            }
            phase += e - s;
            s = e;
        }
    }
}

float PolyLength(const std::vector<std::pair<float, float>>& p) {
    float L = 0;
    for (size_t i = 0; i + 1 < p.size(); i++)
        L += sqrtf((p[i + 1].first - p[i].first) * (p[i + 1].first - p[i].first) + (p[i + 1].second - p[i].second) * (p[i + 1].second - p[i].second));
    return L;
}
std::pair<float, float> PointAt(const std::vector<std::pair<float, float>>& p, float d) {
    for (size_t i = 0; i + 1 < p.size(); i++) {
        float x0 = p[i].first, y0 = p[i].second, x1 = p[i + 1].first, y1 = p[i + 1].second;
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (d <= len && len > 0) return { x0 + (x1 - x0) * d / len, y0 + (y1 - y0) * d / len };
        d -= len;
    }
    return p.back();
}

struct Box { float x0, y0, x1, y1; };
bool Overlaps(const Box& a, const Box& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

} // namespace

float MapNodeRadius(double magnitude, float cameraScale) {
    // One step of size per decade of essence, so a node 1000x stronger is
    // three steps bigger, not 1000x bigger.
    float r = 2.5f + 4.0f * (float)log10(1.0 + (magnitude > 0 ? magnitude : 0));
    float zoom = sqrtf(cameraScale / 0.5f);
    zoom = zoom < 0.6f ? 0.6f : (zoom > 1.6f ? 1.6f : zoom);
    return r * zoom;
}

void MapZoomAt(MapCamera& c, float factor, float sx, float sy) {
    float wx = MapWorldX(c, sx), wz = MapWorldZ(c, sy);
    c.scale = std::min(MAP_MAX_SCALE, std::max(MAP_MIN_SCALE, c.scale * factor));
    // Re-centre so (wx, wz) stays under the cursor.
    c.centerX = wx - (sx - c.screenW * 0.5f) / c.scale;
    c.centerZ = wz + (sy - c.screenH * 0.5f) / c.scale;
}

void BuildMapDrawList(const EssenceNetwork& net, const MapCamera& cam, const MapTuning& t, float time,
                      float playerX, float playerZ, float playerYaw, float pivotX, float pivotZ,
                      float (*textWidth)(const std::string&, float), float textHeight,
                      MapDrawList& out) {
    out = MapDrawList();
    const float W = (float)cam.screenW, H = (float)cam.screenH;
    Rect(out, 0, 0, W, H, kBackground);

    // Faint grid: spacing a power of two blocks, at least ~48 px apart.
    float spacing = 16.0f;
    while (spacing * cam.scale < 48.0f) spacing *= 2.0f;
    for (float gx = floorf(MapWorldX(cam, 0) / spacing) * spacing; MapScreenX(cam, gx) <= W; gx += spacing)
        Segment(out, MapScreenX(cam, gx), 0, MapScreenX(cam, gx), H, 1.0f, kGrid);
    for (float gz = floorf(MapWorldZ(cam, H) / spacing) * spacing; MapScreenY(cam, gz) >= 0; gz += spacing)
        Segment(out, 0, MapScreenY(cam, gz), W, MapScreenY(cam, gz), 1.0f, kGrid);

    const std::vector<EssenceNode>& nodes = net.Nodes();
    const std::vector<EssenceRoute>& routes = net.Routes();
    auto sx = [&](const EssenceNode& n) { return MapScreenX(cam, n.x); };
    auto sy = [&](const EssenceNode& n) { return MapScreenY(cam, n.z); };
    auto minor = [&](const EssenceNode& n) { return n.kind == NodeKind::Convergence && EssenceBand(n.magnitude) <= net.tuning.minorBand; };
    auto onScreen = [&](float x, float y, float pad) { return x > -pad && x < W + pad && y > -pad && y < H + pad; };

    // Belts: minor sources merge into one soft region per group -- the node
    // they roll up into (hierarchy as grouping), or their placement region.
    struct Belt { double sx = 0, sz = 0; int count = 0; float maxR = 0; std::vector<int> members; };
    std::unordered_map<uint64_t, Belt> belts;
    for (int i = 0; i < (int)nodes.size(); i++) {
        if (!minor(nodes[i])) continue;
        // Grouped by the node it rolls up into, else by placement region
        // (bit 63 keeps the two kinds of key apart; region coordinates go
        // in as unsigned 31-bit fields -- shifting a negative value is UB).
        uint64_t key;
        if (nodes[i].parent >= 0) {
            key = (uint64_t)nodes[i].parent;
        } else {
            uint32_t rx = (uint32_t)(int32_t)floorf(nodes[i].x / net.tuning.regionSize) & 0x7FFFFFFFu;
            uint32_t rz = (uint32_t)(int32_t)floorf(nodes[i].z / net.tuning.regionSize) & 0x7FFFFFFFu;
            key = (1ull << 63) | ((uint64_t)rx << 31) | rz;
        }
        Belt& b = belts[key];
        b.sx += nodes[i].x; b.sz += nodes[i].z; b.count++; b.members.push_back(i);
    }
    for (auto& kv : belts) {
        Belt& b = kv.second;
        float cx = (float)(b.sx / b.count), cz = (float)(b.sz / b.count);
        for (int i : b.members) b.maxR = std::max(b.maxR, sqrtf((nodes[i].x - cx) * (nodes[i].x - cx) + (nodes[i].z - cz) * (nodes[i].z - cz)));
        float r = std::max(20.0f, b.maxR + 14.0f) * cam.scale;
        float px = MapScreenX(cam, cx), py = MapScreenY(cam, cz);
        if (!onScreen(px, py, r)) continue;
        Col edge = kBelt; edge.a = 0.0f;
        Disc(out, px, py, r, kBelt, edge, 28);
        out.belts++;
        if (cam.scale >= t.minorDotScale)
            for (int i : b.members) Disc(out, sx(nodes[i]), sy(nodes[i]), 2.0f, kZone, kZone, 8);
    }

    // Routes: style from the network (solid active, dashed intermittent,
    // dotted planned), width and pulse speed from the flow's band, curved
    // only when bound.
    struct Ranked { double key; int index; float x, y; };
    std::vector<Ranked> routeRank;
    for (int ri = 0; ri < (int)routes.size(); ri++) {
        const EssenceRoute& r = routes[ri];
        const EssenceNode& a = nodes[r.a];
        const EssenceNode& b = nodes[r.b];
        float x0 = sx(a), y0 = sy(a), x1 = sx(b), y1 = sy(b);
        if (std::max(x0, x1) < 0 || std::min(x0, x1) > W || std::max(y0, y1) < 0 || std::min(y0, y1) > H) continue;
        std::vector<std::pair<float, float>> path = RoutePath(x0, y0, x1, y1, r.bound);
        float w = 1.2f + 0.9f * r.band;
        Col c = kRoute;
        if (r.style == RouteStyle::Planned) c.a = 0.45f;
        Pattern(out, path, r.style, w, c);
        // Pulses travel along flowing routes, faster for higher bands.
        if (r.style != RouteStyle::Planned) {
            float L = PolyLength(path);
            float speed = 20.0f + 25.0f * r.band;
            float gap = 60.0f;
            for (float d = fmodf(time * speed, gap); d < L; d += gap) {
                std::pair<float, float> p = PointAt(path, d);
                Col glow = { 1, 1, 1, 0.8f }, rim = { 1, 1, 1, 0.0f };
                Disc(out, p.first, p.second, w + 1.5f, glow, rim, 10);
            }
        }
        out.routesDrawn++;
        std::pair<float, float> mid = PointAt(path, PolyLength(path) * 0.5f);
        if (r.style != RouteStyle::Planned) routeRank.push_back({ (double)r.band, ri, mid.first, mid.second });
    }

    // Nodes (minor ones live in their belts): size from magnitude by decade.
    std::vector<Ranked> nodeRank;
    for (int i = 0; i < (int)nodes.size(); i++) {
        const EssenceNode& n = nodes[i];
        if (minor(n)) continue;
        float x = sx(n), y = sy(n);
        float r = MapNodeRadius(n.magnitude, cam.scale);
        if (!onScreen(x, y, r)) continue;
        Col c = n.kind == NodeKind::Attractor ? kAttractor : kZone;
        Col halo = c; halo.a = 0.22f;
        Col clear = c; clear.a = 0.0f;
        Disc(out, x, y, r * 2.2f, halo, clear, 20);
        if (n.kind == NodeKind::Attractor) {
            // Built things read as squares; natural ones as rounds.
            Rect(out, x - r * 0.8f, y - r * 0.8f, x + r * 0.8f, y + r * 0.8f, c);
        } else {
            Disc(out, x, y, r, c, c, 20);
        }
        out.nodesDrawn++;
        nodeRank.push_back({ n.magnitude, i, x, y + r + 3.0f });
    }

    // Labels: the most significant first; a label that would overlap one
    // already placed is dropped rather than stacked.
    std::vector<Box> placed;
    auto tryLabel = [&](const std::string& text, float cx, float y, float scale, Col c) {
        float w = textWidth(text, scale), h = textHeight * (scale / 0.65f);
        Box b = { cx - w * 0.5f - 2, y - 1, cx + w * 0.5f + 2, y + h + 1 };
        if (b.x0 < 0 || b.x1 > W || b.y0 < 0 || b.y1 > H) return false;
        for (const Box& p : placed) if (Overlaps(p, b)) return false;
        placed.push_back(b);
        out.labels.push_back({ text, cx - w * 0.5f, y, scale, c.r, c.g, c.b, c.a });
        return true;
    };
    std::sort(nodeRank.begin(), nodeRank.end(), [](const Ranked& a, const Ranked& b) { return a.key > b.key; });
    int budget = cam.scale >= t.labelAllScale ? (int)nodeRank.size() : t.labelTopNodes;
    for (const Ranked& r : nodeRank) {
        if (out.nodesLabeled >= budget) break;
        const EssenceNode& n = nodes[r.index];
        std::string text = n.kind == NodeKind::Attractor
            ? std::string(EssenceBandWord(EssenceBand(n.magnitude))) + " ATTRACTOR"
            : std::string(EssenceBandWord(EssenceBand(n.magnitude))) + " CONVERGENCE";
        if (tryLabel(text, r.x, r.y, 0.65f, kLabel)) out.nodesLabeled++;
    }
    std::sort(routeRank.begin(), routeRank.end(), [](const Ranked& a, const Ranked& b) { return a.key > b.key; });
    for (const Ranked& r : routeRank) {
        if (out.routesLabeled >= t.labelTopRoutes) break;
        std::string text = std::string(EssenceBandWord(routes[r.index].band)) + " FLOW";
        Col c = kRoute; c.a = 0.9f;
        if (tryLabel(text, r.x, r.y - textHeight * 0.5f, 0.55f, c)) out.routesLabeled++;
    }

    // The player (an arrow along their facing) and the pivot the map opens
    // on -- The Line's centre (a faint ring; the Line itself stays unseen).
    float px = MapScreenX(cam, playerX), py = MapScreenY(cam, playerZ);
    float fx = sinf(playerYaw), fz = cosf(playerYaw); // forward in world (x, z)
    float ax = fx, ay = -fz;                           // on screen (+Z is up)
    Col white = { 1, 1, 1, 0.95f };
    Tri(out, px + ax * 9, py + ay * 9, px - ax * 5 - ay * 5, py - ay * 5 + ax * 5, px - ax * 5 + ay * 5, py - ay * 5 - ax * 5, white, white, white);
    Ring(out, MapScreenX(cam, pivotX), MapScreenY(cam, pivotZ), 6.0f, 1.5f, { 0.8f, 0.85f, 1.0f, 0.45f });
}
