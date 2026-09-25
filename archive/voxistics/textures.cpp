// textures.cpp
//
// The UI font atlas, drawn with GDI+ at load time (it needs a real font
// rasteriser) and copied out as a raw BGRA buffer for render.cpp to
// upload once. Block textures live in blocktex.cpp. None of this touches
// the frame loop.
//
// Shares its contract (FreeGeneratedPixels / GenerateUIAtlas) with
// render.cpp by extern "C" declaration rather than a header, keeping
// GDI+ out of every other file.

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX // see main.cpp for why this precedes windows.h
#endif
#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

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
