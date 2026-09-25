// tools/sound_demo.cpp
//
// Offline renders and measurements of the world sound palette
// (docs/SOUND_PALETTE.md), native, no Windows needed:
//
//   tools/sound_demo.sh analyze        every sound x chord x axis corner:
//                                      level vs the -21 dB ceiling, energy
//                                      above 4.2 kHz, clicks, pitch safety
//   tools/sound_demo.sh demo OUTDIR    WAV files: each category played over
//                                      the day-cycle music, at contrasting
//                                      axis settings, plus a solo version
//
// Levels are reported on the music's own dB scale (the Part XIV layer
// tables), i.e. with the day's master level and output scale divided out.

#include "../sfx_synth.h"
#include "../music_synth.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const int SR = MUSIC_SAMPLE_RATE;

static void WriteWav(const std::string& path, const std::vector<float>& x) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("cannot write %s\n", path.c_str()); return; }
    uint32_t n = (uint32_t)x.size(), bytes = n * 2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(SR); u32(SR * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (float s : x) { float c = s > 1 ? 1 : s < -1 ? -1 : s; int16_t v = (int16_t)std::lround(c * 32767.0); fwrite(&v, 2, 1, f); }
    fclose(f);
}

// A time inside each chord (not crossfading), pulsed if possible.
static double ChordTime(int chord, double from) {
    MusicHarmony h;
    for (double t = from; t < from + 120; t += 0.25) {
        MusicHarmonyAt(t, &h);
        if (h.chord != chord || h.blending) continue;
        MusicHarmony h2; MusicHarmonyAt(t + 3.0, &h2);
        if (h2.chord == chord && !h2.blending) return t;
    }
    return from;
}

struct Measure { double peakDb, hfDb, click; bool safe; int notes; };

// In-place radix-2 FFT (re, im), n a power of two.
static void Fft(std::vector<double>& re, std::vector<double>& im) {
    size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2 * 3.14159265358979 / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; k++) {
                double wr = std::cos(ang * k), wi = std::sin(ang * k);
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
            }
    }
}
static void Spectra(const std::vector<float>& x, double& hfDb, double& clickDb) {
    const size_t N = 2048;
    std::vector<double> frameE;
    struct F { double total, hf, top; };
    std::vector<F> fr;
    for (size_t s = 0; s + N <= x.size(); s += N / 2) {
        std::vector<double> re(N), im(N, 0.0);
        for (size_t i = 0; i < N; i++) re[i] = x[s + i] * (0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / (N - 1)));
        Fft(re, im);
        F f = { 0, 0, 0 };
        for (size_t k = 1; k < N / 2; k++) {
            double e = re[k] * re[k] + im[k] * im[k], hz = (double)k * SR / N;
            f.total += e;
            if (hz > 4200) f.hf += e;
            if (hz > 10000) f.top += e;
        }
        fr.push_back(f);
    }
    double maxE = 0;
    for (auto& f : fr) maxE = std::max(maxE, f.total);
    double hf = 0, top = 0;
    for (auto& f : fr) {
        if (f.total < maxE * 0.01 || f.total <= 0) continue;
        hf = std::max(hf, f.hf / f.total);
        top = std::max(top, f.top / f.total);
    }
    hfDb = 10 * std::log10(std::max(hf, 1e-12));
    clickDb = 10 * std::log10(std::max(top, 1e-12));
}

// Renders one sound alone and measures it.
static Measure MeasureSound(SoundId id, double t0, const SoundAxes& ax, SoundMaterial mat) {
    SoundPalette p;
    p.SetAxes(ax);
    AmbientScene sc; sc.machines = 0.5f; sc.musicBlockCount = 1; sc.musicBlockKey[0] = 5; sc.musicBlockY[0] = 1;
    p.SetScene(sc);
    std::vector<float> out(SR * 7);
    const int blk = 512;
    // Warm up one block so the palette knows the harmony.
    p.Render(out.data(), blk, t0, true);
    SoundCue c; c.id = id; c.material = mat; c.slot = 4; c.strength = 0.8f; c.key = 3; c.height = 1;
    p.Play(c);
    if (id == SND_SLIDE) { /* released below */ }
    for (int i = blk; i + blk <= (int)out.size(); i += blk) {
        if (id == SND_SLIDE && i >= SR) p.Release(SND_SLIDE);
        p.Render(out.data() + i, blk, t0 + (double)i / SR, true);
    }
    MusicHarmony h; MusicHarmonyAt(t0, &h);
    double scale = h.masterGain * 0.78;
    Measure m = { -120, -120, -120, true, 0 };
    double peak = 0;
    for (float x : out) peak = std::max(peak, std::fabs(x / scale));
    m.peakDb = 20 * std::log10(std::max(peak, 1e-9));
    // Spectral share above 4.2 kHz (brightness) and above 10 kHz (clicks
    // are broadband), worst frame among the frames that carry the sound.
    Spectra(out, m.hfDb, m.click);
    SoundPalette::NoteLog log[64];
    int n = p.RecentNotes(log, 64);
    m.notes = n;
    for (int i = 0; i < n; i++) {
        int midi = (int)std::lround(69 + 12 * std::log2(log[i].hz / 440.0));
        int pc = ((midi % 12) + 12) % 12;
        // Safe sets (docs/SOUND_PALETTE.md 1.2), pitch classes C=0.
        static const int sets[5][7] = {
            { 2, 4, 5, 7, 9, 0, -1 }, { 7, 9, 0, 2, 5, -1, -1 }, { 4, 7, 9, 11, 2, -1, -1 },
            { 9, 0, 2, 4, 7, -1, -1 }, { 2, 4, 5, 7, 9, 0, -1 } };
        bool ok = false;
        if (log[i].anchor) ok = pc == 2 || pc == 7 || pc == 9;
        else for (int k = 0; k < 7; k++) if (sets[log[i].chord][k] == pc) ok = true;
        if (!ok) { m.safe = false; printf("    unsafe: %s %.1f Hz (pc %d) under chord %d\n", SoundName(log[i].id), log[i].hz, pc, log[i].chord); }
    }
    return m;
}

static int Analyze() {
    const double times[4] = { ChordTime(MUSIC_DM9, 1200), ChordTime(MUSIC_G7SUS4, 1200), ChordTime(MUSIC_EM7, 1200), ChordTime(MUSIC_A7SUS4, 1200) };
    SoundAxes corners[8];
    for (int i = 0; i < 8; i++) { corners[i].positive = (i & 1) ? 1.0f : -1.0f; corners[i].activity = (i & 2) ? 1.0f : 0.0f; corners[i].mechanical = (i & 4) ? 1.0f : 0.0f; }
    int bad = 0;
    printf("%-14s %8s %8s %8s %8s\n", "sound", "mean dB", "max dB", ">4.2k", ">10k");
    for (int id = 0; id < SND_COUNT; id++) {
        double worstPeak = -120, worstHf = -120, worstClick = -120, sumPeak = 0; int cnt = 0;
        bool safe = true;
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < 8; k++) {
                Measure m = MeasureSound((SoundId)id, times[c], corners[k], id == SND_SET || id == SND_TAKE ? MAT_STONE : MAT_NONE);
                if (m.peakDb < -100) continue;
                worstPeak = std::max(worstPeak, m.peakDb);
                worstHf = std::max(worstHf, m.hfDb);
                worstClick = std::max(worstClick, m.click);
                sumPeak += m.peakDb; cnt++;
                safe = safe && m.safe;
            }
        bool loud = worstPeak > -21.0 + 0.5;
        bool bright = worstHf > -20.0, clicky = worstClick > -40.0;
        printf("%-14s %8.1f %8.1f %8.1f %8.1f%s%s%s%s\n", SoundName((SoundId)id), cnt ? sumPeak / cnt : -120.0, worstPeak, worstHf, worstClick,
               loud ? "  LOUD" : "", safe ? "" : "  UNSAFE", bright ? "  BRIGHT" : "", clicky ? "  CLICK" : "");
        if (loud || !safe || bright || clicky) bad++;
    }
    printf("%d problem sound(s)\n", bad);
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------------
// Demos: the palette over the music.
// ---------------------------------------------------------------------

struct Ev { double t; SoundCue c; };

static void Mix(const std::string& path, double t0, double seconds, const SoundAxes& ax, const AmbientScene& scene,
                const std::vector<Ev>& evs, bool withMusic, bool ambient, double releaseSlideAt = -1) {
    SoundPalette p;
    p.SetAxes(ax); p.SetScene(scene); p.SetAmbientEnabled(ambient);
    MusicState ms; ResetMusicState(&ms);
    MusicColour col; col.positive = ax.positive; col.activity = ax.activity; col.mechanical = ax.mechanical;
    int n = (int)(seconds * SR);
    std::vector<float> out(n, 0.0f);
    const int blk = 512;
    std::vector<int16_t> pcm(blk);
    size_t e = 0;
    for (int i = 0; i + blk <= n; i += blk) {
        double t = t0 + (double)i / SR;
        while (e < evs.size() && evs[e].t <= (double)i / SR) { p.Play(evs[e].c); e++; }
        if (releaseSlideAt >= 0 && (double)i / SR >= releaseSlideAt && (double)(i - blk) / SR < releaseSlideAt) p.Release(SND_SLIDE);
        p.Render(out.data() + i, blk, t, true);
        if (withMusic) {
            GenerateMusicChunk(t, blk, 1.0, &ms, pcm.data(), &col);
            for (int j = 0; j < blk; j++) out[i + j] += pcm[j] / 32767.0f;
        }
    }
    WriteWav(path, out);
    printf("  %s\n", path.c_str());
}

static SoundCue Cue(SoundId id, SoundMaterial m = MAT_NONE, int slot = 0, float strength = 1.0f) {
    SoundCue c; c.id = id; c.material = m; c.slot = slot; c.strength = strength; return c;
}

static int Demo(const std::string& dir) {
    // Interactions: a build streak, some mining, a scroll along the hotbar,
    // a refused placement, a landing and a slide -- warm/healthy vs worn/
    // mechanical, over Midday.
    std::vector<Ev> build;
    double t = 1.0;
    for (int i = 0; i < 7; i++) { build.push_back({ t, Cue(SND_SET, i < 4 ? MAT_STONE : MAT_WOOD) }); t += 0.32; }
    t += 1.2;
    for (int i = 0; i < 5; i++) { build.push_back({ t, Cue(SND_TAKE, MAT_EARTH) }); t += 0.38; }
    t += 1.0;
    for (int s = 0; s < 10; s++) { build.push_back({ t, Cue(SND_SLOT, MAT_NONE, s) }); t += 0.12; }
    t += 0.8;
    build.push_back({ t, Cue(SND_CANT) }); t += 1.0;
    build.push_back({ t, Cue(SND_LAND, MAT_STONE, 0, 0.9f) }); t += 1.2;
    build.push_back({ t, Cue(SND_SLIDE) });
    double slideEnd = t + 1.1;
    t += 2.0;
    build.push_back({ t, Cue(SND_LIBRARY_OPEN) }); t += 0.9;
    build.push_back({ t, Cue(SND_PICK) }); t += 0.5;
    build.push_back({ t, Cue(SND_DROP, MAT_NONE, 3, 1.0f) }); t += 0.9;
    build.push_back({ t, Cue(SND_LIBRARY_CLOSE) }); t += 1.2;
    build.push_back({ t, Cue(SND_SEALED) });
    double tMid = ChordTime(MUSIC_DM9, 1300);
    SoundAxes warm; warm.positive = 0.8f; warm.activity = 0.6f; warm.mechanical = 0.1f;
    SoundAxes worn; worn.positive = -0.7f; worn.activity = 0.6f; worn.mechanical = 0.9f;
    AmbientScene none;
    Mix(dir + "/1_interactions_warm_organic.wav", tMid, t + 3, warm, none, build, true, false, slideEnd);
    Mix(dir + "/2_interactions_worn_mechanical.wav", tMid, t + 3, worn, none, build, true, false, slideEnd);
    Mix(dir + "/3_interactions_solo.wav", tMid, t + 3, warm, none, build, false, false, slideEnd);

    // Discovery and progress, spaced across the Afternoon.
    std::vector<Ev> disc = {
        { 1.0, Cue(SND_UNVEIL) }, { 6.0, Cue(SND_GLINT) }, { 12.0, Cue(SND_VEIN) }, { 18.0, Cue(SND_HORIZON) },
        { 26.0, Cue(SND_TIMESLIP) }, { 32.0, Cue(SND_CADENCE) }, { 37.0, Cue(SND_ONLINE) }, { 42.0, Cue(SND_RISE) },
        { 48.0, Cue(SND_MENDING) }, { 48.5, Cue(SND_MENDING) }, { 49.0, Cue(SND_MENDING) }, { 49.5, Cue(SND_MENDING) },
        { 50.0, Cue(SND_MENDING) }, { 50.5, Cue(SND_MENDING) }, { 51.0, Cue(SND_MENDING) }, { 51.5, Cue(SND_MENDING) },
        { 56.0, Cue(SND_RIFT_CLOSED) },
    };
    SoundAxes bright; bright.positive = 0.7f; bright.activity = 0.4f; bright.mechanical = 0.3f;
    Mix(dir + "/4_discovery_progress.wav", 2150, 64, bright, none, disc, true, false);
    Mix(dir + "/5_discovery_progress_solo.wav", 2150, 64, bright, none, disc, false, false);

    // The dark side: an omen, a sigh, worn ground and a heartbeat at Dusk.
    std::vector<Ev> dark = { { 1.0, Cue(SND_OMEN) }, { 10.0, Cue(SND_SIGH) }, { 16.0, Cue(SND_WORN) }, { 18.0, Cue(SND_HEARTBEAT) },
                             { 26.0, Cue(SND_HEARTBEAT) } };
    SoundAxes neg; neg.positive = -0.8f; neg.activity = 0.3f; neg.mechanical = 0.2f;
    AmbientScene darkScene; darkScene.dark = 1.0f;
    Mix(dir + "/6_neglect.wav", 2850, 36, neg, darkScene, dark, true, false);

    // Ambience, scheduled by the palette itself: a meadow in the Morning
    // (calm, then busy), a works yard at Midday, a cave, and the night.
    AmbientScene meadow; meadow.plants = 1.0f; meadow.water = 0.3f;
    SoundAxes calm; calm.positive = 0.6f; calm.activity = 0.1f; calm.mechanical = 0.05f;
    SoundAxes busy = calm; busy.activity = 0.9f;
    std::vector<Ev> nothing;
    Mix(dir + "/7_meadow_calm.wav", 700, 50, calm, meadow, nothing, true, true);
    Mix(dir + "/8_meadow_busy.wav", 700, 50, busy, meadow, nothing, true, true);
    AmbientScene yard; yard.machines = 1.0f; yard.ember = 0.6f; yard.musicBlockCount = 2;
    yard.musicBlockKey[0] = 3; yard.musicBlockKey[1] = 6; yard.musicBlockY[0] = 0; yard.musicBlockY[1] = 2;
    SoundAxes works; works.positive = 0.3f; works.activity = 0.7f; works.mechanical = 0.95f;
    Mix(dir + "/9_works_yard.wav", 1500, 50, works, yard, nothing, true, true);
    AmbientScene cave; cave.enclosed = true; cave.water = 1.0f; cave.glow = 0.8f; cave.deep = true;
    SoundAxes still; still.positive = 0.3f; still.activity = 0.15f; still.mechanical = 0.3f;
    Mix(dir + "/10_cave.wav", 2500, 50, still, cave, nothing, true, true);
    AmbientScene nightScene = meadow; nightScene.lookingUp = true; nightScene.stillSeconds = 20;
    Mix(dir + "/11_night.wav", 3330, 60, calm, nightScene, nothing, true, true);
    return 0;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "analyze";
    if (mode == "analyze") return Analyze();
    if (mode == "demo") return Demo(argc > 2 ? argv[2] : ".");
    printf("usage: sound_demo analyze | demo OUTDIR\n");
    return 2;
}
