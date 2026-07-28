// Native importer for Haxial Appearance (.hap) files.
//
// Format (reverse-engineered; all integers big-endian):
//   0x00  "%HAP" magic, u32 version (0x00010000)
//   0x2c  section table: 4 x (offset u32, length u32)
//           0: info    (engine version, checksums, metadata strings)
//           1: images  (u32 offset table, then image records)
//           2: colors  (204 x u32 0x00RRGGBB)
//           3: icons
//
// Image record:
//   u16 width, u16 height
//   u16 flags_bpp  (high byte: bit0 = transparency active; low byte bpp 1/2/4/8)
//   u8 max_palette_index, u8 transparent_palette_index
//   u32 transparent color, u8[4] caps (l,t,r,b), u8[4] positions (l,t,r,b)
//   u32[max_palette_index+1] palette
//   rows of packed indices, each row padded to a 4-byte boundary
#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

constexpr int kColorTableLen = 204;

// Semantic names for every color-table entry, verified against the real
// AppearanceEdit 1.24/1.4 under Wine: a probe .hap with color[i] = i·0x010101
// was loaded and every named swatch in the Colors panel read back its file
// index. Entries 25-28, 148-152 and 199-203 are not exposed by any
// AppearanceEdit version (reserved). See docs/hap-color-table.md.
enum HapColor : int {
    ColPrimaryLight = 1,
    ColPrimaryBackground = 2,
    ColPrimaryDark = 3,
    ColPrimaryFrame = 4,
    ColPrimaryLabel = 5,
    ColPrimaryDisableFrame = 6,
    ColPrimaryDisableLabel = 7,
    ColImportantLabel = 8,
    ColFocusBox = 9,
    ColTextBoxBackground = 10,
    ColTextBoxForeground = 11,
    ColTextHiliteBackground = 12,
    ColTextHiliteForeground = 13,
    ColTextInsertionPoint = 14,
    ColListBackground = 15,
    ColListLabel = 16,
    ColListHiliteBackground = 17,
    ColListHiliteForeground = 18,
    ColListSortColumnBackground = 19,
    ColListSeparator = 20,
    ColWorkspaceBackground1 = 21, // ..24 = Workspace Background 2..4
    ColButtonLight2 = 29,         // 29..35 = L2,L1,Button,D1,D2,Frame,Label
    ColButtonFrame = 34,
    ColButtonLabel = 35,
    ColButtonHiliteLight2 = 36, // 36..42, same layout
    ColButtonHiliteLabel = 42,
    ColButtonDisableLight2 = 43, // 43..49, same layout
    ColButtonDisableLabel = 49,
    ColDefaultButtonLight = 50, // 50..53 = Light,Button,Dark,Frame
    ColWindowLight2 = 54,       // 54..60 = L2,L1,Window,D1,D2,Frame,Label
    ColWindowLabel = 60,
    ColWindowTransition1 = 61, // ..78 = Window Transition 1..18
    ColWindowFocusLight2 = 79, // 79..85, same layout as 54..60
    ColWindowFocusLabel = 85,
    ColWindowFocusTransition1 = 86, // ..103
    ColMenuLight = 104,
    ColMenuBackground = 105,
    ColMenuDark = 106,
    ColMenuLabel = 107,
    ColMenuHiliteLight = 108,
    ColMenuHiliteBackground = 109,
    ColMenuHiliteDark = 110,
    ColMenuHiliteLabel = 111,
    ColMenuDisableLabel = 112,
    ColProgressTransition1 = 113, // ..122 = Progress Transition 1..10
    ColProgressBkgndLight = 123,
    ColProgressBkgnd = 124,
    ColProgressBkgndDark = 125,
    ColProgressFrame = 126,
    ColProgressLabel = 127,
    ColScrollBarFrame = 128,
    ColScrollBarLight = 129,
    ColScrollBar = 130,
    ColScrollBarDark = 131,
    ColScrollBarLabel = 132,
    ColScrollBarHiliteLight = 133, // 133..136 = Light,Hilite,Dark,Label
    ColScrollBarIndicatorLight = 137,
    ColScrollBarIndicator = 138,
    ColScrollBarIndicatorDark = 139,
    ColScrollBarIndicatorHiliteLight = 140, // 140..142 = Light,Hilite,Dark
    ColScrollBarBkgndLight2 = 143,          // 143..147 = L2,L1,Bkgnd,D1,D2
    ColScrollBarBkgnd = 145,
    ColScrollBarDisableLight = 153, // 153..157 = Light,Disable,Dark,Frame,Label
    ColSliderIndicatorLight = 158,  // 158..161 = Light,Indicator,Dark,Frame
    ColSliderIndicatorHiliteLight = 162, // 162..165, same layout
    ColSliderBar = 166,                  // 166..169 = Bar,Frame,Hilite,HFrame
    ColSliderDisableLight = 170,         // 170..173 = Light,Disable,Dark,Frame
    ColColumnHeaderFrame = 174,
    ColColumnHeaderLight = 175,
    ColColumnHeader = 176,
    ColColumnHeaderDark = 177,
    ColColumnHeaderLabel = 178,
    ColColumnHeaderHiliteLight = 179,
    ColColumnHeaderHilite = 180,
    ColColumnHeaderHiliteDark = 181,
    ColColumnHeaderHiliteLabel = 182,
    ColFileLabel0 = 183, // ..198 = File Label 0..15 (list-item label tints)
};

// Image slots (indices into the .hap image table), AppearanceEdit names.
enum Slot : int {
    SlotPushButtonNormal = 25,
    SlotPushButtonHilited = 26,
    SlotWindowFrameNormal = 220,
    SlotWindowFrameFocus = 221,
    SlotWindowCloseNormal = 223,
    SlotWindowCloseFocus = 224,
    SlotWindowCloseHilited = 225,
    SlotWindowMinimizeNormal = 228,
    SlotWindowMinimizeFocus = 229,
    SlotWindowMinimizeHilited = 230,
    SlotWindowMaximizeNormal = 233,
    SlotWindowMaximizeFocus = 234,
    SlotWindowMaximizeHilited = 235,
    SlotWindowMenuNormal = 238,
    SlotWindowMenuFocus = 239,
    SlotWindowResizeNormal = 243,
    SlotWindowResizeFocus = 244,
    SlotColumnHeaderNormal = 150,
    SlotColumnHeaderHilited = 151,
    SlotVScrollDoubleArrows = 181,
    SlotVScrollIndicatorNormal = 185,
    SlotVScrollGripsNormal = 188,
};

// A widget texture: ARGB pixels (A=0 means transparent), with the 9-slice
// caps and auxiliary positions exactly as authored in AppearanceEdit.
struct ThemeImage {
    int w = 0, h = 0;
    std::vector<uint32_t> px; // 0xFFRRGGBB opaque, 0x00000000 transparent
    uint8_t caps[4] = {0, 0, 0, 0};      // l, t, r, b
    uint8_t positions[4] = {0, 0, 0, 0}; // l, t, r, b

    uint32_t at(int x, int y) const { return px[size_t(y) * w + x]; }
};

struct Theme {
    std::string name;
    bool has_colors = false;
    uint32_t colors[kColorTableLen] = {}; // 0x00RRGGBB
    std::map<int, ThemeImage> images;

    const ThemeImage *image(int slot) const {
        auto it = images.find(slot);
        return it == images.end() ? nullptr : &it->second;
    }
    uint32_t color(int i) const {
        return (i >= 0 && i < kColorTableLen) ? colors[i] : 0;
    }
};

namespace haputil {

inline uint16_t rd16(const std::vector<uint8_t> &d, size_t o) {
    if (o + 2 > d.size()) return 0;
    return uint16_t(d[o]) << 8 | d[o + 1];
}
inline uint32_t rd32(const std::vector<uint8_t> &d, size_t o) {
    if (o + 4 > d.size()) return 0;
    return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 |
           uint32_t(d[o + 2]) << 8 | d[o + 3];
}

inline bool parse_image(const std::vector<uint8_t> &d, size_t o,
                        ThemeImage &out) {
    int w = rd16(d, o), h = rd16(d, o + 2);
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;
    uint16_t flags_bpp = rd16(d, o + 4);
    bool transparent_active = (flags_bpp & 0x0100) != 0;
    int bpp = flags_bpp & 0xff;
    if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) return false;
    if (o + 20 > d.size()) return false;
    int palette_len = d[o + 6] + 1;
    int transparent_index = d[o + 7];
    for (int i = 0; i < 4; ++i) {
        out.caps[i] = d[o + 12 + i];
        out.positions[i] = d[o + 16 + i];
    }
    std::vector<uint32_t> palette(palette_len);
    for (int i = 0; i < palette_len; ++i)
        palette[i] = rd32(d, o + 20 + 4 * i) & 0x00ffffff;
    size_t pixels_off = o + 20 + 4 * palette_len;
    size_t stride = (size_t(w) * bpp + 31) / 32 * 4;
    if (pixels_off + stride * h > d.size()) return false;

    out.w = w;
    out.h = h;
    out.px.assign(size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t *row = d.data() + pixels_off + y * stride;
        for (int x = 0; x < w; ++x) {
            int idx;
            switch (bpp) {
                case 8: idx = row[x]; break;
                case 4: idx = (row[x / 2] >> (4 * (1 - x % 2))) & 0xf; break;
                case 2: idx = (row[x / 4] >> (6 - 2 * (x % 4))) & 0x3; break;
                default: idx = (row[x / 8] >> (7 - x % 8)) & 0x1; break;
            }
            if (transparent_active && idx == transparent_index) continue;
            uint32_t rgb = idx < palette_len ? palette[idx] : 0;
            out.px[size_t(y) * w + x] = 0xff000000u | rgb;
        }
    }
    return true;
}

} // namespace haputil

// Load a .hap file. Returns false on any structural error.
inline bool load_hap(const std::string &path, Theme &theme) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    using namespace haputil;
    if (d.size() < 0x90 || d[0] != '%' || d[1] != 'H' || d[2] != 'A' ||
        d[3] != 'P')
        return false;
    if (rd32(d, 4) != 0x00010000) return false;

    size_t info_off = rd32(d, 0x2c), info_len = rd32(d, 0x30);
    size_t img_off = rd32(d, 0x34), img_len = rd32(d, 0x38);
    size_t col_off = rd32(d, 0x3c), col_len = rd32(d, 0x40);

    // Metadata: 4 string lengths at +0x22, strings at +0x34.
    if (info_len >= 0x34 && info_off + 0x34 <= d.size()) {
        size_t s = info_off + 0x34;
        size_t l = d[info_off + 0x22];
        if (s + l <= d.size() && l > 0)
            theme.name.assign(reinterpret_cast<const char *>(&d[s]), l);
    }

    size_t n = col_len / 4;
    if (n > kColorTableLen) n = kColorTableLen;
    for (size_t i = 0; i < n; ++i)
        theme.colors[i] = rd32(d, col_off + 4 * i) & 0x00ffffff;
    theme.has_colors = n > 0;

    if (img_len > 0) {
        // Offset table (relative to section start) runs to the first record.
        size_t first_record = SIZE_MAX;
        std::vector<size_t> offsets;
        for (size_t i = 0; 4 * i < first_record; ++i) {
            if (first_record == SIZE_MAX && i > 4096) return false;
            size_t v = rd32(d, img_off + 4 * i);
            if (v != 0 && v < first_record) first_record = v;
            offsets.push_back(v);
        }
        for (size_t slot = 0; slot < offsets.size(); ++slot) {
            if (offsets[slot] == 0) continue;
            ThemeImage img;
            if (parse_image(d, img_off + offsets[slot], img))
                theme.images[int(slot)] = std::move(img);
        }
    }
    return true;
}
