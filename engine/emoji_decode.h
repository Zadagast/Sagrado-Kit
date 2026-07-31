// Decode PNG bytes into SkinImage for kit emoji tiles (and other media).
#pragma once

#include "skin_image.h"

#include <cstdio>
#include <cstdint>
#include <vector>

#ifndef SAGRADO_STB_IMAGE_IMPLEMENTED
#define SAGRADO_STB_IMAGE_IMPLEMENTED
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "third_party/stb_image.h"
#endif

namespace sagrado {

inline bool decode_png_bytes(const uint8_t *data, int len, SkinImage &out) {
    if (!data || len <= 0) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char *rgba = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
    if (!rgba || w <= 0 || h <= 0) return false;
    out.w = w;
    out.h = h;
    out.px.resize(size_t(w) * size_t(h));
    for (int i = 0; i < w * h; ++i) {
        unsigned char r = rgba[i * 4 + 0];
        unsigned char g = rgba[i * 4 + 1];
        unsigned char b = rgba[i * 4 + 2];
        unsigned char a = rgba[i * 4 + 3];
        out.px[size_t(i)] = (uint32_t(a) << 24) | (uint32_t(r) << 16) |
                            (uint32_t(g) << 8) | uint32_t(b);
    }
    stbi_image_free(rgba);
    return true;
}

inline bool decode_png_file(const char *path, SkinImage &out) {
    if (!path) return false;
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> buf((size_t)n);
    if (std::fread(buf.data(), 1, (size_t)n, f) != (size_t)n) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return decode_png_bytes(buf.data(), int(buf.size()), out);
}

} // namespace sagrado
