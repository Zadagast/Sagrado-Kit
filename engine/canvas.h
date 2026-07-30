// Software framebuffer — every pixel is ours. Host blits with one GDI call.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "font.h"
#include "skin_image.h"

struct Color {
    uint8_t r = 0, g = 0, b = 0;
};

constexpr uint32_t pack(Color c) {
    return (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.b);
}

inline Color unpack(uint32_t v) {
    return {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
}

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool contains(int px, int py) const {
        return px >= x && py >= y && px < right() && py < bottom();
    }
};

struct Canvas {
    void resize(int w, int h) {
        width_ = w;
        height_ = h;
        pixels_.assign(size_t(w) * h, 0);
        clip_ = {0, 0, w, h};
    }

    int width() const { return width_; }
    int height() const { return height_; }
    const uint32_t *data() const { return pixels_.data(); }
    uint32_t *data() { return pixels_.data(); }

    void clear(Color c) { fill({0, 0, width_, height_}, c); }

    void put(int x, int y, uint32_t p) {
        if (x < clip_.x || y < clip_.y || x >= clip_.right() || y >= clip_.bottom())
            return;
        pixels_[size_t(y) * width_ + x] = p;
    }

    void fill(Rect r, Color c) {
        uint32_t p = pack(c);
        int x0 = r.x < clip_.x ? clip_.x : r.x;
        int y0 = r.y < clip_.y ? clip_.y : r.y;
        int x1 = r.right() > clip_.right() ? clip_.right() : r.right();
        int y1 = r.bottom() > clip_.bottom() ? clip_.bottom() : r.bottom();
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                pixels_[size_t(y) * width_ + x] = p;
    }

    void hline(int x0, int x1, int y, Color c) {
        if (x0 > x1) {
            int t = x0;
            x0 = x1;
            x1 = t;
        }
        uint32_t p = pack(c);
        for (int x = x0; x < x1; ++x) put(x, y, p);
    }

    void vline(int x, int y0, int y1, Color c) {
        if (y0 > y1) {
            int t = y0;
            y0 = y1;
            y1 = t;
        }
        uint32_t p = pack(c);
        for (int y = y0; y < y1; ++y) put(x, y, p);
    }

    void frame(Rect r, Color c) {
        if (r.w <= 0 || r.h <= 0) return;
        hline(r.x, r.right(), r.y, c);
        hline(r.x, r.right(), r.bottom() - 1, c);
        vline(r.x, r.y, r.bottom(), c);
        vline(r.right() - 1, r.y, r.bottom(), c);
    }

    void rect_grad_v(Rect r, const Color *stops, int n) {
        if (n <= 0 || r.h <= 0) return;
        for (int y = 0; y < r.h; ++y) {
            int i = n == 1 ? 0 : y * (n - 1) / (r.h - 1 ? r.h - 1 : 1);
            if (i >= n) i = n - 1;
            hline(r.x, r.right(), r.y + y, stops[i]);
        }
    }

    // Faces are swappable like KDX's per-surface font settings; unset / unusable
    // means the bundled stock face. Never bind an empty Font — that paints
    // chrome with zero-width glyphs (missing labels on every control).
    void set_font(const Font *f) {
        font_ = (f && fontutil::font_usable(*f)) ? f : &stock_font();
    }
    const Font &font() const { return *font_; }
    int line_height() const { return font_->line_height; }

    int text_width(const char *s) const {
        int w = 0;
        while (*s) w += font_->advance(map_cp(s));
        return w;
    }

    // Tight ink bounds of a string (relative to the text origin). Returns false
    // if the string has no set pixels.
    bool text_ink(const char *s, int &x0, int &y0, int &x1, int &y1) const {
        bool any = false;
        int pen = 0;
        while (*s) {
            unsigned cp = map_cp(s);
            const FontGlyph &g = font_->glyphs[cp];
            for (int row = 0; row < g.h; ++row)
                for (int col = 0; col < g.w; ++col) {
                    if (!font_->pixel(g, col, row)) continue;
                    int px = pen + col, py = g.ytop + row;
                    if (!any) {
                        x0 = x1 = px;
                        y0 = y1 = py;
                        any = true;
                    } else {
                        if (px < x0) x0 = px;
                        if (px > x1) x1 = px;
                        if (py < y0) y0 = py;
                        if (py > y1) y1 = py;
                    }
                }
            pen += g.advance;
        }
        return any;
    }

    int text(int x, int y, const char *s, Color c) {
        uint32_t p = pack(c);
        while (*s) {
            const FontGlyph &g = font_->glyphs[map_cp(s)];
            for (int row = 0; row < g.h; ++row)
                for (int col = 0; col < g.w; ++col)
                    if (font_->pixel(g, col, row))
                        put(x + col, y + g.ytop + row, p);
            x += g.advance;
        }
        return x;
    }

    // Haxial clips a label that will not fit and marks it with a trailing "...";
    // returns the string to draw.
    std::string text_elide(const char *s, int max_w) const {
        if (max_w <= 0) return {};
        if (text_width(s) <= max_w) return s;
        std::string out = s;
        while (!out.empty()) {
            out.pop_back();
            // Never cut a UTF-8 sequence in half.
            while (!out.empty() && (uint8_t(out.back()) & 0xc0) == 0x80)
                out.pop_back();
            if (text_width((out + "...").c_str()) <= max_w) return out + "...";
        }
        return {};
    }

    // Draw label centred on the ink bounds inside r (not the advance box).
    void text_centered(Rect r, const char *s, Color c, int press_off = 0) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (!text_ink(s, x0, y0, x1, y1)) return;
        int iw = x1 - x0 + 1;
        int ih = y1 - y0 + 1;
        int tx = r.x + (r.w - iw) / 2 - x0 + press_off;
        int ty = r.y + (r.h - ih) / 2 - y0 + press_off;
        text(tx, ty, s, c);
    }

    // Blit art 1:1, honouring per-pixel alpha (A=0 skips).
    void blit_image(const SkinImage &img, int dx, int dy) {
        if (img.empty()) return;
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                uint32_t p = img.at(x, y);
                if (p >> 24) put(dx + x, dy + y, p & 0x00ffffffu);
            }
    }

    // Place a symbol (popup arrow, etc.) — same as blit_image.
    void place(const SkinImage &img, int dx, int dy) { blit_image(img, dx, dy); }

    // Draw a label into r's width, eliding it if it does not fit.
    int text_elided(int x, int y, const char *s, int max_w, Color c) {
        std::string t = text_elide(s, max_w);
        return text(x, y, t.c_str(), c);
    }

    Rect clip_rect() const { return clip_; }

    // Replace clip (caller should intersect with prior clip when nesting).
    void set_clip(Rect r) {
        if (r.w < 0) r.w = 0;
        if (r.h < 0) r.h = 0;
        clip_ = r;
    }

    // Intersect current clip with r; returns previous clip for restore.
    Rect push_clip(Rect r) {
        Rect prev = clip_;
        int x0 = r.x > clip_.x ? r.x : clip_.x;
        int y0 = r.y > clip_.y ? r.y : clip_.y;
        int x1 = r.right() < clip_.right() ? r.right() : clip_.right();
        int y1 = r.bottom() < clip_.bottom() ? r.bottom() : clip_.bottom();
        clip_ = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
        return prev;
    }

    void pop_clip(Rect prev) { clip_ = prev; }

    // 9-slice using AppearanceEdit caps: corners 1:1, edges/centre stretched.
    // Corners are stamped last so stretched edges cannot own corner pixels
    // (avoids the frame reading as four disconnected border sticks).
    void nine_slice(const SkinImage &img, Rect r) {
        if (img.empty() || r.w <= 0 || r.h <= 0) return;
        int cl = img.caps[0], ct = img.caps[1];
        int cr = img.caps[2], cb = img.caps[3];
        if (cl + cr >= img.w) {
            cl = img.w / 2;
            cr = img.w - 1 - cl;
        }
        if (ct + cb >= img.h) {
            ct = img.h / 2;
            cb = img.h - 1 - ct;
        }
        if (cl + cr >= r.w) {
            cl = r.w / 2;
            cr = r.w - cl;
        }
        if (ct + cb >= r.h) {
            ct = r.h / 2;
            cb = r.h - ct;
        }
        int mid_sw = img.w - cl - cr, mid_sh = img.h - ct - cb;
        int mid_dw = r.w - cl - cr, mid_dh = r.h - ct - cb;

        auto sample = [&](int x, int y) {
            int sy = y < ct          ? y
                     : y >= r.h - cb ? img.h - (r.h - y)
                     : mid_sh <= 0   ? ct
                                     : ct + (y - ct) * mid_sh / mid_dh;
            int sx = x < cl          ? x
                     : x >= r.w - cr ? img.w - (r.w - x)
                     : mid_sw <= 0   ? cl
                                     : cl + (x - cl) * mid_sw / mid_dw;
            if (sy < 0 || sy >= img.h || sx < 0 || sx >= img.w) return;
            uint32_t p = img.at(sx, sy);
            if (p >> 24) put(r.x + x, r.y + y, p & 0x00ffffffu);
        };

        // Edges + centre (exclude destination corners).
        for (int y = 0; y < r.h; ++y) {
            bool y_corner = (y < ct) || (y >= r.h - cb);
            for (int x = 0; x < r.w; ++x) {
                bool x_corner = (x < cl) || (x >= r.w - cr);
                if (x_corner && y_corner) continue;
                sample(x, y);
            }
        }
        // Corners last.
        for (int y = 0; y < ct; ++y)
            for (int x = 0; x < cl; ++x) sample(x, y);
        for (int y = 0; y < ct; ++y)
            for (int x = r.w - cr; x < r.w; ++x) sample(x, y);
        for (int y = r.h - cb; y < r.h; ++y)
            for (int x = 0; x < cl; ++x) sample(x, y);
        for (int y = r.h - cb; y < r.h; ++y)
            for (int x = r.w - cr; x < r.w; ++x) sample(x, y);
    }

  private:
    // Fold one input codepoint onto a glyph the face actually has.
    unsigned map_cp(const char *&s) const {
        unsigned cp = fontutil::next_cp(s);
        if (font_->has(cp)) return cp;
        unsigned char punct = fontutil::fold_punct(cp);
        if (punct && font_->has(punct)) return punct;
        unsigned char base = fontutil::fold_latin1(cp);
        if (base && font_->has(base)) return base;
        // Ellipsis → three dots when the face has '.'.
        if (cp == 0x2026 && font_->has('.')) return '.';
        return font_->has('?') ? '?' : ' ';
    }

    const Font *font_ = &stock_font();
    int width_ = 0, height_ = 0;
    Rect clip_{0, 0, 0, 0};
    std::vector<uint32_t> pixels_;
};
