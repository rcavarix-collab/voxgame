// textures.cpp
//
// Procedural texture generation, used only at load time (Section 4.1 /
// 4.3 / 8.4). GDI+ draws each block's pattern into an in-memory bitmap,
// which is copied out as a raw BGRA pixel buffer for render.cpp to
// upload once as a GPU texture. GDI+ never touches the frame loop.
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

static void DrawFoundationTile(Graphics& g, int x, int y, int size) {
    SolidBrush base(Color(255, 120, 120, 120));
    g.FillRectangle(&base, x, y, size, size);
    Pen pen(Color(255, 70, 70, 70), 2.0f);
    g.DrawRectangle(&pen, x + 1, y + 1, size - 2, size - 2);
    int step = size / 4;
    for (int i = step; i < size; i += step) {
        g.DrawLine(&pen, x + i, y, x + i, y + size);
        g.DrawLine(&pen, x, y + i, x + size, y + i);
    }
}

static void DrawStoneTile(Graphics& g, int x, int y, int size) {
    SolidBrush base(Color(255, 140, 140, 145));
    g.FillRectangle(&base, x, y, size, size);
    for (int i = 0; i < size * 3; i++) {
        int px = x + rand() % size;
        int py = y + rand() % size;
        int shade = 100 + rand() % 80;
        SolidBrush dot(Color(255, shade, shade, shade + 5));
        g.FillRectangle(&dot, px, py, 2, 2);
    }
}

static void DrawDirtTile(Graphics& g, int x, int y, int size) {
    SolidBrush base(Color(255, 101, 67, 33));
    g.FillRectangle(&base, x, y, size, size);
    for (int i = 0; i < size * 3; i++) {
        int px = x + rand() % size;
        int py = y + rand() % size;
        int r = 80 + rand() % 50, gr = 50 + rand() % 40, b = 20 + rand() % 25;
        SolidBrush dot(Color(255, r, gr, b));
        g.FillRectangle(&dot, px, py, 3, 3);
    }
}

static void DrawWoodTile(Graphics& g, int x, int y, int size) {
    SolidBrush base(Color(255, 165, 120, 75));
    g.FillRectangle(&base, x, y, size, size);
    Pen grain(Color(255, 120, 85, 50), 1.5f);
    for (int i = 3; i < size; i += 6) {
        g.DrawLine(&grain, x, y + i, x + size, y + i + (rand() % 3 - 1));
    }
}

static void DrawChestTile(Graphics& g, int x, int y, int size) {
    SolidBrush base(Color(255, 140, 90, 40));
    g.FillRectangle(&base, x, y, size, size);
    Pen border(Color(255, 60, 35, 15), 3.0f);
    g.DrawRectangle(&border, x + 1, y + 1, size - 2, size - 2);
    SolidBrush band(Color(255, 90, 60, 30));
    g.FillRectangle(&band, x, y + size / 2 - size / 10, size, size / 5);
    SolidBrush latch(Color(255, 200, 170, 60));
    int latchSize = size / 6;
    g.FillRectangle(&latch, x + size / 2 - latchSize / 2, y + size / 2 - latchSize / 2, latchSize, latchSize);
}

static void DrawMachineTile(Graphics& g, int x, int y, int size) {
    SolidBrush base(Color(255, 65, 68, 78));
    g.FillRectangle(&base, x, y, size, size);
    Pen border(Color(255, 25, 27, 32), 3.0f);
    g.DrawRectangle(&border, x + 1, y + 1, size - 2, size - 2);
    SolidBrush bolt(Color(255, 200, 200, 60));
    int b = size / 10;
    int margin = size / 8;
    g.FillEllipse(&bolt, x + margin, y + margin, b, b);
    g.FillEllipse(&bolt, x + size - margin - b, y + margin, b, b);
    g.FillEllipse(&bolt, x + margin, y + size - margin - b, b, b);
    g.FillEllipse(&bolt, x + size - margin - b, y + size - margin - b, b, b);
    SolidBrush panel(Color(255, 90, 140, 150));
    int pm = size / 4;
    g.FillRectangle(&panel, x + pm, y + pm, size - 2 * pm, size - 2 * pm);
}

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

extern "C" bool GenerateGameTextures(
    int tileSize, int atlasCols, int atlasRows,
    uint8_t** outAtlasPixelsBGRA, int* outAtlasW, int* outAtlasH)
{
    ULONG_PTR token;
    GdiplusStartupInput startupInput;
    if (GdiplusStartup(&token, &startupInput, nullptr) != Ok) return false;

    srand(1234); // deterministic patterns across runs

    bool ok = true;
    int atlasW = tileSize * atlasCols;
    int atlasH = tileSize * atlasRows;
    {
        Bitmap atlasBmp(atlasW, atlasH, PixelFormat32bppARGB);
        {
            Graphics g(&atlasBmp);
            g.SetSmoothingMode(SmoothingModeNone);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            // Slot order must match g_info[].tex in common.h:
            // 0 foundation, 1 stone, 2 dirt, 3 wood, 4 chest, 5 machine.
            DrawFoundationTile(g, 0 * tileSize, 0 * tileSize, tileSize);
            DrawStoneTile(g, 1 * tileSize, 0 * tileSize, tileSize);
            DrawDirtTile(g, 2 * tileSize, 0 * tileSize, tileSize);
            DrawWoodTile(g, 0 * tileSize, 1 * tileSize, tileSize);
            DrawChestTile(g, 1 * tileSize, 1 * tileSize, tileSize);
            DrawMachineTile(g, 2 * tileSize, 1 * tileSize, tileSize);
        }
        uint8_t* pixels = CopyBitmapBGRA(atlasBmp, atlasW, atlasH);
        if (!pixels) { ok = false; }
        *outAtlasPixelsBGRA = pixels;
        *outAtlasW = atlasW;
        *outAtlasH = atlasH;
    }

    GdiplusShutdown(token);
    return ok;
}

extern "C" void FreeGeneratedPixels(uint8_t* p) {
    delete[] p;
}

// UI font-glyph atlas: a cols x rows grid, cell (code-32) holds the
// glyph for ASCII code `code` (32..126), and the very last cell is left
// as an opaque solid-white square for drawing untextured tinted
// rectangles through the same texture/pipeline as text (Section 4.6).
// Generated once at load time, same as the block atlas -- GDI+ never
// touches the frame loop.
extern "C" bool GenerateUIAtlas(
    int cellW, int cellH, int cols, int rows,
    uint8_t** outPixelsBGRA, int* outW, int* outH)
{
    ULONG_PTR token;
    GdiplusStartupInput startupInput;
    if (GdiplusStartup(&token, &startupInput, nullptr) != Ok) return false;

    int w = cellW * cols;
    int h = cellH * rows;
    bool ok = true;
    uint8_t* pixels = nullptr;
    {
        Bitmap bmp(w, h, PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            // A freshly constructed Bitmap isn't documented to start
            // zero-filled -- clear it explicitly rather than relying on
            // that. Everything not covered by a glyph needs to end up
            // fully transparent, since white glyphs carry their shape
            // in the alpha channel alone (Section 4.6).
            g.Clear(Color(0, 0, 0, 0));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAlias);

            FontFamily consolas(L"Consolas");
            FontFamily* fam = &consolas;
            if (consolas.GetLastStatus() != Ok) {
                fam = const_cast<FontFamily*>(FontFamily::GenericMonospace());
            }
            Font font(fam, (Gdiplus::REAL)(cellH * 0.62f), FontStyleBold, UnitPixel);
            SolidBrush white(Color(255, 255, 255, 255));
            StringFormat fmt;
            fmt.SetAlignment(StringAlignmentCenter);
            fmt.SetLineAlignment(StringAlignmentCenter);

            int totalCells = cols * rows;
            int whiteCell = totalCells - 1;
            for (int i = 0; i < totalCells; i++) {
                int col = i % cols, row = i / cols;
                float cx = (float)(col * cellW), cy = (float)(row * cellH);
                if (i == whiteCell) {
                    g.FillRectangle(&white, cx + 1, cy + 1, (float)cellW - 2, (float)cellH - 2);
                    continue;
                }
                int code = i + 32; // ASCII 32..126
                if (code > 126) continue;
                wchar_t ch = (wchar_t)code;
                RectF cellRect(cx, cy, (Gdiplus::REAL)cellW, (Gdiplus::REAL)cellH);
                g.DrawString(&ch, 1, &font, cellRect, &fmt, &white);
            }
        }
        pixels = CopyBitmapBGRA(bmp, w, h);
        if (!pixels) ok = false;
    }

    GdiplusShutdown(token);
    if (!ok) return false;
    *outPixelsBGRA = pixels;
    *outW = w;
    *outH = h;
    return true;
}
