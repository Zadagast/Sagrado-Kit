// Kit text view / editor paint — read-only wrap and editable selection+caret.
// Include after Appearance / CanvasClip are defined (via appearance.h).
#pragma once

#include "text_doc.h"
#include "text_layout.h"

#include <algorithm>
#include <string>
#include <vector>

// Read-only wrapped text inside `r`. `scroll_y` is in pixels from content top.
// Returns content height in pixels (including pad).
inline int paint_text_view(Canvas &cv, const Appearance &ap, Rect r,
                           const std::string &text, int wrap_w, int scroll_y,
                           Color ink, int pad = 4, bool fill_bg = true) {
    if (r.w <= 0 || r.h <= 0) return 0;
    if (fill_bg) cv.fill(r, ap.c("text.background"));
    int inner_w = wrap_w > 0 ? wrap_w : std::max(8, r.w - 2 * pad);
    auto lines = layout_lines(cv, text, inner_w, true);
    const int lh = cv.line_height();
    int content_h = text_content_height(lines, lh);
    CanvasClip clip(cv, r);
    int y = r.y + pad - scroll_y;
    for (const auto &vl : lines) {
        if (y + lh > r.y && y < r.bottom()) {
            std::string line = text.substr(vl.start, vl.len);
            cv.text(r.x + pad, y, line.c_str(), ink);
        }
        y += lh;
    }
    return content_h + 2 * pad;
}

inline int paint_text_view(Canvas &cv, const Appearance &ap, Rect r,
                           const std::string &text, int wrap_w, int scroll_y,
                           int pad = 4, bool fill_bg = true) {
    return paint_text_view(cv, ap, r, text, wrap_w, scroll_y,
                           ap.c("text.foreground"), pad, fill_bg);
}

// Editable document paint (selection hilite + caret). `scroll_y` is in lines.
struct TextEditorPaint {
    Rect r;
    int pad = 4;
    int scroll_y = 0; // first visible visual line
    int scroll_x = 0; // pixel offset when hard-wrapped
    bool focused = true;
    bool caret_on = true;
    bool show_caret = true;
};

inline void paint_text_editor(Canvas &cv, const Appearance &ap, const TextDoc &doc,
                              const std::vector<VisLine> &lines,
                              const TextEditorPaint &p) {
    if (p.r.w <= 0 || p.r.h <= 0) return;
    cv.fill(p.r, ap.c("text.background"));
    CanvasClip clip(cv, p.r);

    const int lh = cv.line_height();
    const int page = std::max(1, (p.r.h - 2 * p.pad) / lh);
    const size_t lo = doc.sel_lo();
    const size_t hi = doc.sel_hi();
    Color fg = ap.c("text.foreground");
    Color hi_bg = ap.c("text.hilite_background");
    Color hi_fg = ap.c("text.hilite_foreground");
    Color caret_c = ap.c("text.insertion_point");

    for (int row = 0; row < page; ++row) {
        int li = p.scroll_y + row;
        if (li < 0 || li >= (int)lines.size()) break;
        const VisLine &vl = lines[size_t(li)];
        int y = p.r.y + p.pad + row * lh;
        int x = p.r.x + p.pad - p.scroll_x;

        size_t a = vl.start;
        size_t b = vl.start + vl.len;
        size_t s0 = std::max(a, lo);
        size_t s1 = std::min(b, hi);
        // Step by drawable units (emoji cluster / codepoint), not bytes.
        auto unit_at = [&](size_t i, size_t end) {
            int n = cv.unit_len(doc.text.c_str() + i);
            if (n <= 0) n = 1;
            if (i + size_t(n) > end) n = int(end - i);
            return doc.text.substr(i, size_t(n));
        };

        if (s0 < s1) {
            int x0 = x;
            for (size_t i = a; i < s0;) {
                std::string u = unit_at(i, s0);
                x0 += cv.text_width(u.c_str());
                i += u.size();
            }
            int x1 = x0;
            for (size_t i = s0; i < s1;) {
                std::string u = unit_at(i, s1);
                x1 += cv.text_width(u.c_str());
                i += u.size();
            }
            if (x1 == x0) x1 = x0 + 4;
            cv.fill({x0, y, x1 - x0, lh}, hi_bg);
        }

        int pen = x;
        for (size_t i = a; i < b;) {
            std::string u = unit_at(i, b);
            Color ink = (i >= lo && i < hi) ? hi_fg : fg;
            cv.text(pen, y, u.c_str(), ink);
            pen += cv.text_width(u.c_str());
            i += u.size();
        }

        if (p.show_caret && p.focused && p.caret_on && doc.caret >= a &&
            doc.caret <= b) {
            bool at_wrap_end =
                (doc.caret == b && vl.len > 0 && li + 1 < (int)lines.size() &&
                 lines[size_t(li + 1)].start == b);
            if (!at_wrap_end) {
                int cx = x;
                for (size_t i = a; i < doc.caret;) {
                    std::string u = unit_at(i, doc.caret);
                    cx += cv.text_width(u.c_str());
                    i += u.size();
                }
                cv.vline(cx, y, y + lh, caret_c);
            }
        }
    }
}
