// Anti-aliased system text for the Kit's software framebuffer.
//
// The bundled faces are 1bpp bitmaps — faithful to KDX, but they are the single
// loudest "this app is from 1999" signal at modern pixel densities. This file
// rasterises the host's UI font into 8-bit coverage glyphs with GDI and hands
// them to Canvas through the AAFace hook, so smooth text is a Kit-wide facility
// rather than one app's private trick. Chrome that must stay pixel-exact (skin
// art, stamps) is unaffected — this only changes glyph rasterisation.
#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "canvas.h"

namespace sagrado {

struct NativeFace {
    HFONT font = nullptr;
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    uint32_t *bits = nullptr;
    int cell = 0; // square rasterisation cell, px
    int line_height = 0;
    int ascent = 0;
    std::unordered_map<unsigned, AAGlyph> glyphs;
    std::vector<std::vector<uint8_t>> pixels; // stable storage for AAGlyph::cov
};

inline NativeFace &native_face() {
    static NativeFace f;
    return f;
}

// Rasterise one codepoint. GDI has no 8bpp text target, so draw white on black
// into a 32bpp DIB and read a channel back as coverage.
inline const AAGlyph *native_glyph(unsigned cp) {
    NativeFace &f = native_face();
    if (!f.dc || cp < 32 || cp == 0x7f) return nullptr;
    auto it = f.glyphs.find(cp);
    if (it != f.glyphs.end())
        return it->second.advance ? &it->second : nullptr;

    wchar_t buf[2];
    int n = 0;
    if (cp < 0x10000) {
        buf[n++] = wchar_t(cp);
    } else {
        unsigned v = cp - 0x10000;
        buf[n++] = wchar_t(0xd800 + (v >> 10));
        buf[n++] = wchar_t(0xdc00 + (v & 0x3ff));
    }
    ABC abc{};
    SIZE ext{};
    if (!GetTextExtentPoint32W(f.dc, buf, n, &ext)) {
        f.glyphs[cp] = AAGlyph{};
        return nullptr;
    }
    int adv = ext.cx;
    (void)abc;
    const int pad = f.cell / 4 + 2;
    memset(f.bits, 0, size_t(f.cell) * f.cell * 4);
    ExtTextOutW(f.dc, pad, 0, 0, nullptr, buf, n, nullptr);

    // Trim to inked bounds so blending touches only real pixels.
    int x0 = f.cell, y0 = f.cell, x1 = -1, y1 = -1;
    for (int y = 0; y < f.cell; ++y)
        for (int x = 0; x < f.cell; ++x) {
            if (!(f.bits[size_t(y) * f.cell + x] & 0xff)) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    AAGlyph g;
    g.advance = adv;
    if (x1 >= x0 && y1 >= y0) {
        g.w = x1 - x0 + 1;
        g.h = y1 - y0 + 1;
        g.left = x0 - pad;
        g.top = f.ascent - y0; // rows below the baseline give a negative top
        std::vector<uint8_t> cov(size_t(g.w) * g.h);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
                cov[size_t(y) * g.w + x] =
                    uint8_t(f.bits[size_t(y0 + y) * f.cell + (x0 + x)] & 0xff);
        f.pixels.push_back(std::move(cov));
        g.cov = f.pixels.back().data();
    }
    auto ins = f.glyphs.emplace(cp, g).first;
    return ins->second.advance ? &ins->second : nullptr;
}

// Install the host UI font at `px` pixels as the Kit's text face. Returns false
// (leaving the bitmap face in place) when the system gives us nothing usable.
inline bool use_native_text(int px, const wchar_t *family = L"Segoe UI") {
    NativeFace &f = native_face();
    if (f.dc) return true;
    HDC screen = GetDC(nullptr);
    f.dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!f.dc) return false;
    f.font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
    if (!f.font) {
        DeleteDC(f.dc);
        f.dc = nullptr;
        return false;
    }
    SelectObject(f.dc, f.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(f.dc, &tm);
    f.ascent = tm.tmAscent;
    f.line_height = tm.tmHeight + tm.tmExternalLeading;
    f.cell = f.line_height * 2 + 8;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = f.cell;
    bi.bmiHeader.biHeight = -f.cell; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    f.bmp = CreateDIBSection(f.dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!f.bmp) {
        DeleteObject(f.font);
        DeleteDC(f.dc);
        f.dc = nullptr;
        f.font = nullptr;
        return false;
    }
    f.bits = static_cast<uint32_t *>(bits);
    SelectObject(f.dc, f.bmp);
    SetBkMode(f.dc, OPAQUE);
    SetBkColor(f.dc, RGB(0, 0, 0));
    SetTextColor(f.dc, RGB(255, 255, 255));

    // A face that rasterises nothing is worse than the bitmap one.
    if (!native_glyph('M')) {
        DeleteObject(f.bmp);
        DeleteObject(f.font);
        DeleteDC(f.dc);
        f = NativeFace{};
        return false;
    }
    set_kit_aa_face(&native_glyph, f.line_height, f.ascent);
    return true;
}

} // namespace sagrado

#endif // _WIN32
