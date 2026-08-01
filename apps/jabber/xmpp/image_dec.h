// Decode CAPTCHA / media bytes into SkinImage (0xAARRGGBB).
// Also prepare vCard avatars: crop/scale + PNG/JPEG encode (Gajim-shaped).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../../engine/emoji_decode.h"

#ifndef SAGRADO_STB_IMAGE_WRITE_IMPLEMENTED
#define SAGRADO_STB_IMAGE_WRITE_IMPLEMENTED
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#endif

namespace jabber {

inline bool decode_image_bytes(const uint8_t *data, int len, SkinImage &out) {
    return sagrado::decode_png_bytes(data, len, out);
}

inline bool decode_image_vec(const std::vector<uint8_t> &bytes, SkinImage &out) {
    if (bytes.empty()) return false;
    return decode_image_bytes(bytes.data(), int(bytes.size()), out);
}

// Center-crop to square, then nearest-neighbour scale to `size`×`size`.
inline SkinImage crop_square_scale(const SkinImage &src, int size) {
    SkinImage out;
    if (src.empty() || size <= 0) return out;
    int side = std::min(src.w, src.h);
    int ox = (src.w - side) / 2;
    int oy = (src.h - side) / 2;
    out.w = size;
    out.h = size;
    out.px.resize(size_t(size) * size_t(size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int sx = ox + x * side / size;
            int sy = oy + y * side / size;
            out.px[size_t(y) * size_t(size) + size_t(x)] = src.at(sx, sy);
        }
    }
    return out;
}

inline void skin_to_rgba(const SkinImage &img, std::vector<uint8_t> *rgba) {
    rgba->resize(size_t(img.w) * size_t(img.h) * 4);
    for (int i = 0; i < img.w * img.h; ++i) {
        uint32_t p = img.px[size_t(i)];
        (*rgba)[size_t(i) * 4 + 0] = uint8_t((p >> 16) & 255);
        (*rgba)[size_t(i) * 4 + 1] = uint8_t((p >> 8) & 255);
        (*rgba)[size_t(i) * 4 + 2] = uint8_t(p & 255);
        (*rgba)[size_t(i) * 4 + 3] = uint8_t((p >> 24) & 255);
    }
}

inline void stbi_write_vec(void *ctx, void *data, int size) {
    auto *out = static_cast<std::vector<uint8_t> *>(ctx);
    const auto *p = static_cast<const uint8_t *>(data);
    out->insert(out->end(), p, p + size);
}

inline bool encode_png(const SkinImage &img, std::vector<uint8_t> *out) {
    if (!out || img.empty()) return false;
    std::vector<uint8_t> rgba;
    skin_to_rgba(img, &rgba);
    out->clear();
    int ok = stbi_write_png_to_func(stbi_write_vec, out, img.w, img.h, 4, rgba.data(),
                                    img.w * 4);
    return ok != 0 && !out->empty();
}

inline bool encode_jpeg(const SkinImage &img, int quality, std::vector<uint8_t> *out) {
    if (!out || img.empty()) return false;
    std::vector<uint8_t> rgba;
    skin_to_rgba(img, &rgba);
    out->clear();
    int ok = stbi_write_jpg_to_func(stbi_write_vec, out, img.w, img.h, 4, rgba.data(),
                                    quality);
    return ok != 0 && !out->empty();
}

// XEP-0153-shaped publish: square ~96px, prefer PNG under max_bytes; else JPEG.
// Accepts large camera photos — resize/compress instead of rejecting.
inline bool prepare_vcard_avatar(const std::vector<uint8_t> &src_bytes,
                                 size_t max_bytes, SkinImage *display,
                                 std::vector<uint8_t> *pub_bytes,
                                 std::string *mime_out) {
    if (!display || !pub_bytes || !mime_out) return false;
    SkinImage src;
    if (!decode_image_vec(src_bytes, src)) return false;

    // Try publish sizes from larger (sharper) down until payload fits.
    static const int kSizes[] = {96, 64, 48, 32};
    for (int size : kSizes) {
        SkinImage tile = crop_square_scale(src, size);
        std::vector<uint8_t> png;
        if (encode_png(tile, &png) && png.size() <= max_bytes) {
            *display = std::move(tile);
            *pub_bytes = std::move(png);
            *mime_out = "image/png";
            return true;
        }
        // JPEG ladder for stubborn photos (photos often beat PNG size).
        for (int q : {85, 70, 55, 40}) {
            std::vector<uint8_t> jpg;
            if (encode_jpeg(tile, q, &jpg) && jpg.size() <= max_bytes) {
                *display = std::move(tile);
                *pub_bytes = std::move(jpg);
                *mime_out = "image/jpeg";
                return true;
            }
        }
    }
    return false;
}

} // namespace jabber
