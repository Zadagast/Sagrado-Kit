// Decode CAPTCHA / media bytes into SkinImage (0xAARRGGBB).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../../../engine/skin_image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "../third_party/stb_image.h"

namespace jabber {

inline bool decode_image_bytes(const uint8_t *data, int len, SkinImage &out) {
    int w = 0, h = 0, comp = 0;
    unsigned char *rgba =
        stbi_load_from_memory(data, len, &w, &h, &comp, 4);
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

inline bool decode_image_vec(const std::vector<uint8_t> &bytes, SkinImage &out) {
    if (bytes.empty()) return false;
    return decode_image_bytes(bytes.data(), int(bytes.size()), out);
}

} // namespace jabber
