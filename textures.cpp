// textures.cpp
//
// Procedural texture generation, used only at load time (Section 4.1 /
// 4.3 / 8.4). Block tiles are plotted pixel-exact straight into a raw
// BGRA buffer; the UI font atlas is drawn with GDI+ (it needs a real
// font rasteriser) and copied out the same way. render.cpp uploads each
// buffer once as a GPU texture -- none of this touches the frame loop.
//
// This file has no dependency on the rest of the project's types -- it
// shares a contract (GenerateGameTextures / FreeGeneratedPixels /
// GenerateUIAtlas) with render.cpp purely by convention, since the
// project intentionally has no shared header for these extern "C" entry
// points.
//
// NOTE: the tile draw order below (foundation, stone, dirt, wood,
// chest, machine) must match the atlas slot each block is assigned in
// common.h's g_info[] table (ATLAS_COLS/ATLAS_ROWS there). Changing the
// order here without changing it there will silently swap textures.

#define NOMINMAX // see main.cpp for why this precedes windows.h
#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <random>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// ---- Block tiles -------------------------------------------------------
// Drawn with exact integer-pixel primitives straight into the atlas
// buffer, each clipped to its own tile square. These used to go through
// GDI+ pens on one shared bitmap, where a 3px border pen centred on
// x+1 (plus PixelOffsetModeHalf) painted a 1px strip into whichever
// neighbouring tile had been drawn before it -- wood's right column
// picked up the chest's border, the chest's picked up the machine's.
// Clipping per tile makes that impossible by construction.
// BEGIN_BLOCK_TILES (native test harness extracts from here)
struct Rgb { uint8_t r, g, b; };

struct TileCanvas {
    uint8_t* px; int stride; int ox, oy, size;
    void Put(int x, int y, Rgb c) {
        if ((unsigned)x >= (unsigned)size || (unsigned)y >= (unsigned)size) return;
        uint8_t* p = px + (size_t)(oy + y) * stride + (size_t)(ox + x) * 4;
        p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = 255;
    }
    // 50/50 mix with what's already there (the wood grain's soft half row).
    void Blend(int x, int y, Rgb c) {
        if ((unsigned)x >= (unsigned)size || (unsigned)y >= (unsigned)size) return;
        uint8_t* p = px + (size_t)(oy + y) * stride + (size_t)(ox + x) * 4;
        p[0] = (uint8_t)((p[0] + c.b) / 2); p[1] = (uint8_t)((p[1] + c.g) / 2); p[2] = (uint8_t)((p[2] + c.r) / 2);
    }
    void Fill(int x, int y, int w, int h, Rgb c) {
        for (int yy = y; yy < y + h; yy++)
            for (int xx = x; xx < x + w; xx++) Put(xx, yy, c);
    }
    void Border(int t, Rgb c) {
        Fill(0, 0, size, t, c);
        Fill(0, size - t, size, t, c);
        Fill(0, t, t, size - 2 * t, c);
        Fill(size - t, t, t, size - 2 * t, c);
    }
    // Filled circle inscribed in the d x d cell at (x, y).
    void Disc(int x, int y, int d, Rgb c) {
        float r = d * 0.5f;
        for (int yy = 0; yy < d; yy++)
            for (int xx = 0; xx < d; xx++) {
                float dx = xx + 0.5f - r, dy = yy + 0.5f - r;
                if (dx * dx + dy * dy <= r * r) Put(x + xx, y + yy, c);
            }
    }
};

static void DrawFoundationTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 120, 120, 120 });
    Rgb line = { 70, 70, 70 };
    t.Border(2, line);
    int step = s / 4;
    for (int i = step; i < s; i += step) {
        t.Fill(i - 1, 0, 2, s, line);
        t.Fill(0, i - 1, s, 2, line);
    }
}

static void DrawStoneTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 140, 140, 145 });
    for (int i = 0; i < s * 3; i++) {
        int px = rand() % s;
        int py = rand() % s;
        int shade = 100 + rand() % 80;
        t.Fill(px, py, 2, 2, { (uint8_t)shade, (uint8_t)shade, (uint8_t)(shade + 5) });
    }
}

static void DrawDirtTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 101, 67, 33 });
    for (int i = 0; i < s * 3; i++) {
        int px = rand() % s;
        int py = rand() % s;
        int r = 80 + rand() % 50, gr = 50 + rand() % 40, b = 20 + rand() % 25;
        t.Fill(px, py, 3, 3, { (uint8_t)r, (uint8_t)gr, (uint8_t)b });
    }
}

static void DrawWoodTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 165, 120, 75 });
    Rgb grain = { 120, 85, 50 };
    for (int i = 3; i < s; i += 6) {
        int drift = rand() % 3 - 1; // -1, 0 or +1 px across the whole tile
        for (int x = 0; x < s; x++) {
            // Round-to-nearest of i + drift * x / s, done in integers.
            int y = i + (drift * (2 * x + 1) + (drift < 0 ? -s : s)) / (2 * s);
            t.Put(x, y, grain);
            t.Blend(x, y + 1, grain);
        }
    }
}

static void DrawChestTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 140, 90, 40 });
    t.Border(3, { 60, 35, 15 });
    t.Fill(0, s / 2 - s / 10, s, s / 5, { 90, 60, 30 });
    int latch = s / 6;
    t.Fill(s / 2 - latch / 2, s / 2 - latch / 2, latch, latch, { 200, 170, 60 });
}

static void DrawMachineTile(TileCanvas& t) {
    int s = t.size;
    t.Fill(0, 0, s, s, { 65, 68, 78 });
    t.Border(3, { 25, 27, 32 });
    Rgb bolt = { 200, 200, 60 };
    int b = s / 10;
    int margin = s / 8;
    t.Disc(margin, margin, b, bolt);
    t.Disc(s - margin - b, margin, b, bolt);
    t.Disc(margin, s - margin - b, b, bolt);
    t.Disc(s - margin - b, s - margin - b, b, bolt);
    int pm = s / 4;
    t.Fill(pm, pm, s - 2 * pm, s - 2 * pm, { 90, 140, 150 });
}

extern "C" bool GenerateGameTextures(
    int tileSize, int atlasCols, int atlasRows,
    uint8_t** outAtlasPixelsBGRA, int* outAtlasW, int* outAtlasH)
{
    int atlasW = tileSize * atlasCols;
    int atlasH = tileSize * atlasRows;
    uint8_t* pixels = new uint8_t[(size_t)atlasW * atlasH * 4]();

    srand(1234); // deterministic patterns across runs

    // Slot order must match g_info[].tex in common.h:
    // 0 foundation, 1 stone, 2 dirt, 3 wood, 4 chest, 5 machine.
    void (*const draw[])(TileCanvas&) = {
        DrawFoundationTile, DrawStoneTile, DrawDirtTile,
        DrawWoodTile, DrawChestTile, DrawMachineTile,
    };
    int slots = (int)(sizeof(draw) / sizeof(draw[0]));
    for (int i = 0; i < slots && i < atlasCols * atlasRows; i++) {
        TileCanvas t = { pixels, atlasW * 4, (i % atlasCols) * tileSize, (i / atlasCols) * tileSize, tileSize };
        draw[i](t);
    }

    *outAtlasPixelsBGRA = pixels;
    *outAtlasW = atlasW;
    *outAtlasH = atlasH;
    return true;
}
// END_BLOCK_TILES

static uint8_t* CopyBitmapBGRA(Bitmap& bmp, int w, int h) {
    Rect rect(0, 0, w, h);
    BitmapData data;
    if (bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok)
        return nullptr;
    uint8_t* out = new uint8_t[(size_t)w * h * 4];
    const uint8_t* src = (const uint8_t*)data.Scan0;
    for (int y = 0; y < h; y++) {
        memcpy(out + (size_t)y * w * 4, src + (size_t)y * data.Stride, (size_t)w * 4);
    }
    bmp.UnlockBits(&data);
    return out;
}

extern "C" void FreeGeneratedPixels(uint8_t* p) {
    delete[] p;
}

// UI font-glyph atlas: the top whiteH rows are opaque white (for
// untextured tinted rectangles, Section 4.6), then one band per text
// size, each a cols-wide grid whose cell (code-32) holds ASCII `code`
// (32..126) centred in the cell, baked at that band's pixel size so it
// is drawn 1:1. Generated once at load time, same as the block atlas --
// GDI+ never touches the frame loop.
extern "C" bool GenerateUIAtlas(
    int atlasW, int atlasH, int whiteH, int cols, int bandCount,
    const int* cellW, const int* cellH, const int* bandY, const float* fontPx,
    uint8_t** outPixelsBGRA)
{
    ULONG_PTR token;
    GdiplusStartupInput startupInput;
    if (GdiplusStartup(&token, &startupInput, nullptr) != Ok) return false;

    bool ok = true;
    uint8_t* pixels = nullptr;
    {
        Bitmap bmp(atlasW, atlasH, PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            // A freshly constructed Bitmap isn't documented to start
            // zero-filled -- clear it explicitly rather than relying on
            // that. Everything not covered by a glyph needs to end up
            // fully transparent, since white glyphs carry their shape
            // in the alpha channel alone (Section 4.6).
            g.Clear(Color(0, 0, 0, 0));
            // Hinted: stems snap to whole pixels, which is what makes 1:1
            // text read crisp rather than smeared across two columns.
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            SolidBrush white(Color(255, 255, 255, 255));
            g.FillRectangle(&white, 0, 0, atlasW, whiteH);

            FontFamily consolas(L"Consolas");
            FontFamily* fam = &consolas;
            if (consolas.GetLastStatus() != Ok) {
                fam = const_cast<FontFamily*>(FontFamily::GenericMonospace());
            }
            // Typographic format: no GDI+ side padding, so centring in the
            // cell centres the glyph's own advance box.
            StringFormat fmt(StringFormat::GenericTypographic());
            fmt.SetAlignment(StringAlignmentCenter);
            fmt.SetLineAlignment(StringAlignmentCenter);

            for (int band = 0; band < bandCount; band++) {
                Font font(fam, (Gdiplus::REAL)fontPx[band], FontStyleBold, UnitPixel);
                for (int code = 32; code <= 126; code++) {
                    int i = code - 32;
                    int x = (i % cols) * cellW[band], y = bandY[band] + (i / cols) * cellH[band];
                    // Clip to the cell so no glyph can spill into a neighbour.
                    g.SetClip(Rect(x, y, cellW[band], cellH[band]));
                    wchar_t ch = (wchar_t)code;
                    RectF cellRect((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)cellW[band], (Gdiplus::REAL)cellH[band]);
                    g.DrawString(&ch, 1, &font, cellRect, &fmt, &white);
                }
            }
            g.ResetClip();
        }
        pixels = CopyBitmapBGRA(bmp, atlasW, atlasH);
        if (!pixels) ok = false;
    }

    GdiplusShutdown(token);
    if (!ok) return false;
    *outPixelsBGRA = pixels;
    return true;
}
