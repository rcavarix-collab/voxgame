// ui_font.cpp
//
// The text atlas, drawn once at start-up with GDI+ from the player's
// installed system font (Consolas, or the system monospace if it's missing
// -- fonts are never shipped). Text appears only in menus and the F3 debug
// overlay (DESIGN.md §7). Layout: an opaque white square at the top-left
// (untextured shapes sample it), then a grid of ASCII 32..126, one glyph
// per cell, drawn at the size it's shown so it stays crisp.
// Kept in its own file so GDI+ touches nothing else.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <cstring>

using namespace Gdiplus;

extern "C" void FreeTextAtlas(uint8_t* p) { delete[] p; }

extern "C" bool GenerateTextAtlas(int atlasW, int atlasH, int whiteSize, int cols, int cellW, int cellH, float fontPx, uint8_t** outBGRA) {
    ULONG_PTR token = 0;
    GdiplusStartupInput in;
    if (GdiplusStartup(&token, &in, nullptr) != Ok) return false;
    bool ok = false;
    {
        Bitmap bmp(atlasW, atlasH, PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            g.Clear(Color(0, 0, 0, 0)); // a new Bitmap isn't documented to start empty
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            SolidBrush white(Color(255, 255, 255, 255));
            g.FillRectangle(&white, 0, 0, whiteSize, whiteSize);
            FontFamily consolas(L"Consolas");
            const FontFamily* fam = consolas.GetLastStatus() == Ok ? &consolas : FontFamily::GenericMonospace();
            Font font(fam, (REAL)fontPx, FontStyleBold, UnitPixel);
            StringFormat fmt(StringFormat::GenericTypographic());
            fmt.SetAlignment(StringAlignmentCenter);
            fmt.SetLineAlignment(StringAlignmentCenter);
            for (int code = 33; code <= 126; code++) {
                int i = code - 32;
                int x = (i % cols) * cellW, y = whiteSize + (i / cols) * cellH;
                if (y + cellH > atlasH) break;
                g.SetClip(Rect(x, y, cellW, cellH)); // no glyph spills into a neighbour
                wchar_t ch = (wchar_t)code;
                RectF cell((REAL)x, (REAL)y, (REAL)cellW, (REAL)cellH);
                g.DrawString(&ch, 1, &font, cell, &fmt, &white);
            }
            g.ResetClip();
        }
        Rect r(0, 0, atlasW, atlasH);
        BitmapData data;
        if (bmp.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &data) == Ok) {
            uint8_t* out = new uint8_t[(size_t)atlasW * atlasH * 4];
            for (int y = 0; y < atlasH; y++)
                memcpy(out + (size_t)y * atlasW * 4, (const uint8_t*)data.Scan0 + (size_t)y * data.Stride, (size_t)atlasW * 4);
            bmp.UnlockBits(&data);
            *outBGRA = out;
            ok = true;
        }
    }
    GdiplusShutdown(token);
    return ok;
}
