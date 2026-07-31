// Decode CAPTCHA / media bytes into SkinImage (0xAARRGGBB).
#pragma once
#include <cstdint>
#include <vector>

#include "../../../engine/emoji_decode.h"

namespace jabber {

inline bool decode_image_bytes(const uint8_t *data, int len, SkinImage &out) {
    return sagrado::decode_png_bytes(data, len, out);
}

inline bool decode_image_vec(const std::vector<uint8_t> &bytes, SkinImage &out) {
    if (bytes.empty()) return false;
    return decode_image_bytes(bytes.data(), int(bytes.size()), out);
}

} // namespace jabber
