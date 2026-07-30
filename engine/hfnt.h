// Haxial %FNT bitmap font reader.
//
// Verified against the four faces embedded in KDX Client 1.6 (Myoklonika,
// Tonik-klonik, Grand Mal, Panitaka) — Haxial draws every glyph itself, so a
// face is a flat table of 1bpp records, big-endian like .hap:
//
//   0x00 '%FNT'
//   0x04 u32 version (0x00020001)
//   0x08 u32 length of the whole face
//   0x18 pascal string face name  (64 byte field)
//   0x58 pascal string foundry    (64 byte field)
//   0x10c u8 line height
//   0x136 glyph table, 16 bytes per record, codepoint 1 upwards:
//         u16 codepoint, u32 bitmap offset, u8 w, u8 h, u8 planes, u8 ytop,
//         u8 advance, 5 bytes reserved
//   bitmap rows are MSB-first, (w+7)/8 bytes per row
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "font.h"

namespace hfnt {

constexpr size_t kGlyphTable = 0x136;
constexpr size_t kRecord = 16;

inline uint32_t rd32(const std::vector<uint8_t> &d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) |
           (uint32_t(d[o + 2]) << 8) | uint32_t(d[o + 3]);
}

inline uint16_t rd16(const std::vector<uint8_t> &d, size_t o) {
    return uint16_t((uint32_t(d[o]) << 8) | d[o + 1]);
}

inline std::string pstring(const std::vector<uint8_t> &d, size_t o, size_t cap) {
    if (o >= d.size()) return {};
    size_t n = d[o];
    if (n > cap - 1 || o + 1 + n > d.size()) return {};
    return std::string(reinterpret_cast<const char *>(&d[o + 1]), n);
}

// Parse a face out of an in-memory %FNT blob (a file, or the copy embedded in
// a Haxial executable). Returns false unless at least one glyph decodes.
inline bool parse(const std::vector<uint8_t> &d, Font &out) {
    if (d.size() < kGlyphTable + kRecord) return false;
    if (d[0] != '%' || d[1] != 'F' || d[2] != 'N' || d[3] != 'T') return false;
    uint32_t len = rd32(d, 8);
    size_t end = (len && len <= d.size()) ? len : d.size();

    Font f;
    f.name = pstring(d, 0x18, 64);
    int lh = d[0x10c];
    f.line_height = lh > 0 ? lh : kFontHeight;

    int max_bot = 0;
    for (size_t t = kGlyphTable; t + kRecord <= end; t += kRecord) {
        unsigned cp = rd16(d, t);
        if (cp == 0 || cp > 0xff) break;
        uint32_t off = rd32(d, t + 2);
        int w = d[t + 6], h = d[t + 7];
        int ytop = d[t + 9], adv = d[t + 10];
        if (!w || !h) { // spacing-only glyph (space, controls)
            if (adv) {
                uint8_t none = 0;
                f.add(cp, 0, 0, 0, adv, &none);
            }
            continue;
        }
        size_t stride = (size_t(w) + 7) / 8;
        size_t need = stride * size_t(h);
        if (off < kGlyphTable || off + need > end) continue;
        f.add(cp, w, h, ytop, adv, &d[off]);
        if (ytop + h > max_bot) max_bot = ytop + h;
    }
    if (f.bits.empty()) return false;
    f.ascent = max_bot < f.line_height ? max_bot : f.line_height;
    if (f.line_height < max_bot) f.line_height = max_bot;
    out = std::move(f);
    return true;
}

inline bool load(const std::string &path, Font &out) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(fp);
    return parse(d, out);
}

} // namespace hfnt
