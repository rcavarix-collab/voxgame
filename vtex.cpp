// vtex.cpp -- see vtex.h and assets/textures/TEXTURE_BRIEF.md.

#include "vtex.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

std::string StripComment(const std::string& s) {
    size_t h = s.find('#');
    return h == std::string::npos ? s : s.substr(0, h);
}

// Splits on whitespace.
std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> w;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\r') j++;
        if (j > i) w.push_back(s.substr(i, j - i));
        i = j;
    }
    return w;
}

bool ValidName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string Where(const std::string& file, int line) {
    char buf[32]; snprintf(buf, sizeof(buf), ":%d", line);
    return file + buf;
}

} // namespace

void ParseVtex(const std::string& text, const std::string& fileName, VtexSet& out) {
    // Split into lines, keeping 1-based line numbers for error messages.
    std::vector<std::string> lines;
    {
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); i++)
            if (i == text.size() || text[i] == '\n') { lines.push_back(text.substr(start, i - start)); start = i + 1; }
    }

    enum class Mode { Top, TexHeader, Palette, Pixels, Block };
    Mode mode = Mode::Top;
    VtexTexture tex;
    VtexBlockFaces blk;
    uint32_t palette[128];
    bool paletteSet[128];
    bool bad = false;        // current entry has an error; skip to its `end`
    int rowsRead = 0;
    int mapKind = 0;         // 0 pixels, 1 height, 2 shine, 3 glow: which grid rows are filling
    int entryLine = 0;

    auto err = [&](int line, const std::string& msg) {
        out.errors.push_back(Where(fileName, line) + ": " + msg);
    };
    auto fail = [&](int line, const std::string& msg) {
        if (!bad) err(line, msg);
        bad = true;
    };

    for (size_t li = 0; li < lines.size(); li++) {
        int ln = (int)li + 1;
        const std::string raw = lines[li];

        if (mode == Mode::Palette) {
            // "k rrggbb" or "k rrggbbaa" (alpha, for see-through blocks; a
            // leading '#' on the hex is tolerated). The key may
            // be any printable non-space character except '#', so comment
            // stripping happens only after the colour.
            std::string t = Trim(raw);
            if (t.empty() || t[0] == '#') continue;
            if (Trim(StripComment(t)) == "pixels") { mode = Mode::Pixels; rowsRead = 0; mapKind = 0; continue; }
            if (Trim(StripComment(t)) == "end") { fail(ln, "texture '" + tex.name + "' ended before its pixels"); mode = Mode::Top; bad = false; continue; }
            char key = t[0];
            size_t p = 1;
            while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) p++;
            if (p < t.size() && t[p] == '#') p++;
            uint32_t rgb = 0; bool ok = p + 6 <= t.size() && (unsigned char)key < 128 && key > ' ';
            int digits = ok && p + 8 <= t.size() && HexDigit(t[p + 6]) >= 0 && HexDigit(t[p + 7]) >= 0 ? 8 : 6;
            for (int k = 0; ok && k < digits; k++) {
                int d = HexDigit(t[p + k]);
                if (d < 0) ok = false; else rgb = (rgb << 4) | (uint32_t)d;
            }
            if (ok && p + digits < t.size() && t[p + digits] != ' ' && t[p + digits] != '\t' && t[p + digits] != '#' && t[p + digits] != '\r') ok = false;
            if (!ok) { fail(ln, "bad palette line (expected: key character, then 6 hex digits, or 8 with alpha)"); continue; }
            if (digits == 8) rgb = (rgb >> 8) | ((255u - (rgb & 0xFFu)) << 24); // RRGGBBAA -> TTRRGGBB
            if (paletteSet[(int)key]) { fail(ln, std::string("palette key '") + key + "' defined twice"); continue; }
            palette[(int)key] = rgb; paletteSet[(int)key] = true;
            continue;
        }

        if (mode == Mode::Pixels) {
            std::string t = Trim(StripComment(raw));
            if (t.empty()) continue;
            static const char* kMapNames[4] = { "pixel", "height", "shine", "glow" };
            if (t == "end") {
                if (!bad && rowsRead != tex.size)
                    fail(ln, "texture '" + tex.name + "' has " + std::to_string(rowsRead) + " " + kMapNames[mapKind] + " rows, expected " + std::to_string(tex.size));
                if (!bad) out.textures.push_back(tex);
                mode = Mode::Top; bad = false; mapKind = 0;
                continue;
            }
            if (bad) continue;
            // After a complete grid, an optional surface map may follow:
            // `height` (0-9 then a-z, low to high), `shine` or `glow` (0-9).
            if (rowsRead == tex.size && (t == "height" || t == "shine" || t == "glow")) {
                mapKind = t == "height" ? 1 : (t == "shine" ? 2 : 3);
                std::vector<float>& m = mapKind == 1 ? tex.height : (mapKind == 2 ? tex.shine : tex.glow);
                if (!m.empty()) { fail(ln, "texture '" + tex.name + "' has two '" + t + "' maps"); continue; }
                m.assign((size_t)tex.size * tex.size, 0.0f);
                rowsRead = 0;
                continue;
            }
            if (mapKind != 0) {
                if (rowsRead >= tex.size) { fail(ln, "texture '" + tex.name + "' has more than " + std::to_string(tex.size) + " " + kMapNames[mapKind] + " rows"); continue; }
                if ((int)t.size() != tex.size) {
                    fail(ln, "texture '" + tex.name + "' " + kMapNames[mapKind] + " row " + std::to_string(rowsRead + 1) + " is " + std::to_string(t.size()) +
                             " characters, expected " + std::to_string(tex.size));
                    continue;
                }
                std::vector<float>& m = mapKind == 1 ? tex.height : (mapKind == 2 ? tex.shine : tex.glow);
                for (int x = 0; x < tex.size; x++) {
                    char c = t[x];
                    int v = c >= '0' && c <= '9' ? c - '0' : (mapKind == 1 && c >= 'a' && c <= 'z' ? 10 + c - 'a' : -1);
                    if (v < 0) {
                        fail(ln, "texture '" + tex.name + "' " + kMapNames[mapKind] + " row " + std::to_string(rowsRead + 1) + " has '" + std::string(1, c) +
                                 "' (expected " + (mapKind == 1 ? "0-9 or a-z" : "0-9") + ")");
                        break;
                    }
                    m[(size_t)rowsRead * tex.size + x] = mapKind == 1 ? v / 35.0f : v / 9.0f;
                }
                rowsRead++;
                continue;
            }
            if (rowsRead >= tex.size) { fail(ln, "texture '" + tex.name + "' has more than " + std::to_string(tex.size) + " pixel rows"); continue; }
            if ((int)t.size() != tex.size) {
                fail(ln, "texture '" + tex.name + "' row " + std::to_string(rowsRead + 1) + " is " + std::to_string(t.size()) +
                         " characters, expected " + std::to_string(tex.size));
                continue;
            }
            for (int x = 0; x < tex.size; x++) {
                unsigned char c = (unsigned char)t[x];
                if (c >= 128 || !paletteSet[c]) {
                    fail(ln, "texture '" + tex.name + "' row " + std::to_string(rowsRead + 1) + " uses '" + std::string(1, (char)c) + "', which isn't in its palette");
                    break;
                }
                tex.rgb[(size_t)rowsRead * tex.size + x] = palette[c];
            }
            rowsRead++;
            continue;
        }

        std::vector<std::string> w = Words(StripComment(raw));
        if (w.empty()) continue;

        if (mode == Mode::Top) {
            entryLine = ln;
            if (w[0] == "texture") {
                tex = VtexTexture(); bad = false;
                memset(paletteSet, 0, sizeof(paletteSet));
                mode = Mode::TexHeader;
                if (w.size() != 2 || !ValidName(w[1])) { fail(ln, "expected 'texture <name>' (lowercase a-z, 0-9, _)"); continue; }
                tex.name = w[1];
                tex.source = Where(fileName, ln);
            } else if (w[0] == "block") {
                blk = VtexBlockFaces(); bad = false;
                mode = Mode::Block;
                if (w.size() != 2 || !ValidName(w[1])) { fail(ln, "expected 'block <name>'"); continue; }
                blk.block = w[1];
                blk.source = Where(fileName, ln);
            } else {
                err(ln, "expected 'texture' or 'block', found '" + w[0] + "'");
            }
            continue;
        }

        if (mode == Mode::TexHeader) {
            if (w[0] == "end") { fail(ln, "texture '" + tex.name + "' has no palette/pixels"); mode = Mode::Top; bad = false; continue; }
            if (w[0] == "size") {
                int n = w.size() == 2 ? atoi(w[1].c_str()) : 0;
                if (n != 8 && n != 16 && n != 32 && n != 64) { fail(ln, "size must be 8, 16, 32 or 64"); continue; }
                tex.size = n;
                tex.rgb.assign((size_t)n * n, 0);
            } else if (w[0] == "palette") {
                if (tex.size == 0) fail(ln, "'size' must come before 'palette'");
                mode = Mode::Palette;
            } else {
                fail(ln, "expected 'size' or 'palette', found '" + w[0] + "'");
            }
            continue;
        }

        if (mode == Mode::Block) {
            if (w[0] == "end") {
                if (!bad) {
                    if (blk.all.empty() && blk.top.empty() && blk.bottom.empty() && blk.side.empty() && blk.front.empty())
                        err(ln, "block '" + blk.block + "' names no faces");
                    else
                        out.blocks.push_back(blk);
                }
                mode = Mode::Top; bad = false;
                continue;
            }
            if (bad) continue;
            if (w.size() != 2 || !ValidName(w[1])) { fail(ln, "expected '<face> <texture>'"); continue; }
            std::string* slot = nullptr;
            if (w[0] == "all") slot = &blk.all;
            else if (w[0] == "top") slot = &blk.top;
            else if (w[0] == "bottom") slot = &blk.bottom;
            else if (w[0] == "side") slot = &blk.side;
            else if (w[0] == "front") slot = &blk.front;
            if (!slot) { fail(ln, "unknown face '" + w[0] + "' (all, top, bottom, side, front)"); continue; }
            *slot = w[1];
            continue;
        }
    }

    if (mode != Mode::Top)
        err(entryLine, "entry starting here has no 'end'");
}
