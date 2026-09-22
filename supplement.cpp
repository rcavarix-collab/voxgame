// supplement.cpp
//
// Procedural texture generation, used only at load time (Section 4.1 /
// 4.3 / 8.4). GDI+ draws each block's pattern into an in-memory bitmap,
// which is copied out as a raw BGRA pixel buffer for main.cpp to upload
// once as a GPU texture. GDI+ never touches the frame loop.
//
// This file has no dependency on main.cpp's types -- the two files
// share a contract (GenerateGameTextures / FreeGeneratedPixels) purely
// by convention, since the project intentionally has no shared header.
//
// NOTE: the tile draw order below (foundation, stone, dirt, wood,
// chest, machine) must match the atlas slot each block is assigned in
// main.cpp's g_info[] table (ATLAS_COLS/ATLAS_ROWS there). Changing the
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

static void DrawPipeTexture(Graphics& g, int size) {
    SolidBrush base(Color(255, 150, 150, 160));
    g.FillRectangle(&base, 0, 0, size, size);
    Pen band(Color(255, 90, 90, 100), 2.0f);
    int step = size / 5;
    for (int i = step; i < size; i += step) {
        g.DrawLine(&band, 0, i, size, i);
    }
    Pen border(Color(255, 60, 60, 70), 2.0f);
    g.DrawRectangle(&border, 0, 0, size - 1, size - 1);
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
    uint8_t** outAtlasPixelsBGRA, int* outAtlasW, int* outAtlasH,
    uint8_t** outPipePixelsBGRA, int* outPipeSize)
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
            // Slot order must match g_info[].tex in main.cpp:
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

    if (ok) {
        int pipeSize = tileSize;
        Bitmap pipeBmp(pipeSize, pipeSize, PixelFormat32bppARGB);
        {
            Graphics g(&pipeBmp);
            g.SetSmoothingMode(SmoothingModeNone);
            DrawPipeTexture(g, pipeSize);
        }
        uint8_t* pixels = CopyBitmapBGRA(pipeBmp, pipeSize, pipeSize);
        if (!pixels) ok = false;
        *outPipePixelsBGRA = pixels;
        *outPipeSize = pipeSize;
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

// ---------------------------------------------------------------------
// Procedural ambient audio (Section 10). One deterministic, seamlessly-
// looping background track, synthesized entirely in code -- same "we
// generate our own assets" approach as the textures above, so there is
// no external audio asset and nothing to license. It draws on a wider
// palette of experiments (drones, harmonic pads, filtered-noise "air"
// beds), but deliberately without any of that palette's randomized
// bursts/whistles: per an explicit accessibility goal, nothing in this
// track ever produces a sudden or unpredictable loud event.
//
// Loop-seam handling: the tonal pad (root drone + a fifth an octave up
// + a gently vibratoed shimmer, under a slow amplitude swell) is built
// by phase accumulation with every oscillation/modulation rate chosen
// so it completes an exact integer number of cycles across the loop
// length. That makes it mathematically periodic -- it loops with no
// seam and needs no crossfade. The noise "air" bed has no such natural
// periodicity (it's filtered noise, not an oscillator), so its own tail
// is blended into its own head with a short equal-power crossfade
// before the two layers are mixed together.
// ---------------------------------------------------------------------

static const int kAudioSampleRate = 44100;
static const double kAudioPI = 3.14159265358979323846;
static const double kLoopSeconds = 20.0;
static const double kCrossfadeSeconds = 2.0;

struct OnePoleLowpass {
    double a0 = 1.0, b1 = 0.0, z1 = 0.0;
    void SetCutoff(double hz) {
        double x = std::exp(-2.0 * kAudioPI * hz / kAudioSampleRate);
        b1 = x;
        a0 = 1.0 - x;
    }
    double Process(double in) {
        double out = a0 * in + b1 * z1;
        z1 = out;
        return out;
    }
};

// Filtered white noise, its own tail crossfaded into its own head so it
// loops seamlessly across `n` samples despite having no natural period.
static std::vector<double> BuildNoiseBed(int n, double cutoffHz, unsigned seed) {
    std::vector<double> raw(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    OnePoleLowpass lp;
    lp.SetCutoff(cutoffHz);
    for (int i = 0; i < n; i++) raw[i] = lp.Process(dist(rng));

    int xfade = (int)(kCrossfadeSeconds * kAudioSampleRate);
    if (xfade > 0 && xfade * 2 < n) {
        for (int k = 0; k < xfade; k++) {
            double t = (double)k / (double)(xfade - 1);
            double wIn = std::sin(t * kAudioPI * 0.5);  wIn *= wIn;   // head fades in
            double wOut = std::cos(t * kAudioPI * 0.5); wOut *= wOut; // tail fades out
            int i = n - xfade + k;
            raw[i] = raw[i] * wOut + raw[k] * wIn;
        }
    }
    return raw;
}

// A calm tonal pad: root drone + a fifth an octave up + a gently
// vibratoed high shimmer, under a slow overall amplitude swell. Every
// frequency and modulation rate is chosen so (rate * kLoopSeconds) is
// an exact integer -- the whole layer is therefore mathematically
// periodic over the loop and needs no crossfade to avoid a seam.
static std::vector<double> BuildTonalPad(int n) {
    std::vector<double> out(n, 0.0);

    const double root = 44.0;          // 44 * 20 = 880 exact cycles
    const double fifth = 132.0;        // an octave + a perfect fifth above the root
    const double shimmerBase = 308.0;
    const double shimmerDepth = 4.0;
    const double shimmerLfoHz = 0.10;  // 0.10 * 20 = 2 exact cycles
    const double swellHz = 0.05;       // 0.05 * 20 = 1 exact cycle

    double rootPhase = 0.0, fifthPhase = 0.0, shimmerPhase = 0.0, lfoPhase = 0.0, swellPhase = 0.0;
    for (int i = 0; i < n; i++) {
        rootPhase += 2.0 * kAudioPI * root / kAudioSampleRate;
        fifthPhase += 2.0 * kAudioPI * fifth / kAudioSampleRate;
        lfoPhase += 2.0 * kAudioPI * shimmerLfoHz / kAudioSampleRate;
        double instFreq = shimmerBase + shimmerDepth * std::sin(lfoPhase);
        shimmerPhase += 2.0 * kAudioPI * instFreq / kAudioSampleRate;
        swellPhase += 2.0 * kAudioPI * swellHz / kAudioSampleRate;

        double swell = 0.85 + 0.15 * std::sin(swellPhase);
        double tone = std::sin(rootPhase) * 0.50
                    + std::sin(fifthPhase) * 0.28
                    + std::sin(shimmerPhase) * 0.12;
        out[i] = tone * swell;
    }
    return out;
}

extern "C" bool GenerateAmbientTrack(int16_t** outPCM, uint32_t* outSampleCount, uint32_t* outSampleRate) {
    int n = (int)(kLoopSeconds * kAudioSampleRate);

    std::vector<double> tonal = BuildTonalPad(n);
    std::vector<double> air = BuildNoiseBed(n, 900.0, 90210u);

    std::vector<double> mix(n);
    double sumSq = 0.0;
    for (int i = 0; i < n; i++) {
        double v = tonal[i] * 0.85 + air[i] * 0.30;
        mix[i] = v;
        sumSq += v * v;
    }

    // RMS-based normalization (rather than peak-based) so loudness stays
    // consistent if more tracks are added later, with generous headroom
    // below clipping since Master/Music sliders only ever attenuate.
    double rms = std::sqrt(sumSq / (double)n);
    const double targetRms = 0.20;
    double scale = (rms > 1e-9) ? (targetRms / rms) : 1.0;

    int16_t* pcm = new int16_t[n];
    for (int i = 0; i < n; i++) {
        double v = mix[i] * scale;
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        pcm[i] = (int16_t)(v * 32767.0);
    }

    *outPCM = pcm;
    *outSampleCount = (uint32_t)n;
    *outSampleRate = (uint32_t)kAudioSampleRate;
    return true;
}

extern "C" void FreeGeneratedAudio(int16_t* p) {
    delete[] p;
}
