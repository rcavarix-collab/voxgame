#pragma once
#include <vector>
#include <cstdio>
#include <cstdint>
#include <algorithm>
static void WritePNG(const char* path, int w, int h, const std::vector<uint8_t>& rgb) {
    auto crc = [](const uint8_t* b, size_t n, uint32_t c) {
        static uint32_t tab[256]; static bool init = false;
        if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; tab[i] = k; } init = true; }
        c = ~c; for (size_t i = 0; i < n; i++) c = tab[(c ^ b[i]) & 255] ^ (c >> 8); return ~c;
    };
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3); }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (size_t i = 0; i < raw.size(); i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n == raw.size()); z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
    }
    uint32_t ad = (b << 16) | a; z.push_back(ad >> 24); z.push_back(ad >> 16); z.push_back(ad >> 8); z.push_back(ad);
    FILE* fp = fopen(path, "wb");
    const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, fp);
    auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
        uint8_t len[4] = { (uint8_t)(d.size() >> 24), (uint8_t)(d.size() >> 16), (uint8_t)(d.size() >> 8), (uint8_t)d.size() };
        fwrite(len, 1, 4, fp);
        std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), d.begin(), d.end());
        fwrite(td.data(), 1, td.size(), fp);
        uint32_t c = crc(td.data(), td.size(), 0);
        uint8_t cb[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c }; fwrite(cb, 1, 4, fp);
    };
    std::vector<uint8_t> ihdr = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w, (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0 };
    chunk("IHDR", ihdr); chunk("IDAT", z); chunk("IEND", {});
    fclose(fp);
}
