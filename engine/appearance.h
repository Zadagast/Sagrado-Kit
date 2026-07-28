// Appearance Engine — every window speaks this.
// Resolve tokens (art → colour → stock) and paint kit surfaces into a Canvas.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>

#include "skin.h"

// --- Gel window (Sagrado/Haxial TextEdit chrome) -------------------------
// Metrics and paint order are a direct port of Sagrado native/src/chrome.h
// (measured off real Haxial TextEdit). Do not "improve" these numbers.

constexpr int kTitleH = 22;
constexpr int kBorder = 6;
constexpr int kBtnBox = 14;   // title-bar boxes are 14×14
constexpr int kBtnTop = 4;    // 4px below the window top
constexpr int kHatchW = 32;   // title-bar drag hatch
constexpr int kGrip = 21;     // grow box, flush with the frame corner
constexpr int kScrollbarW = 16;
constexpr int kHeaderH = 20;
constexpr int kRowH = 18;

// Measured off Haxial TextEdit Find & Replace (Sagrado native/):
// regular dialog buttons 24px tall; default Find button 26px outer
// (3px ring → 20px face). Tops share the same Y; default hangs 2px lower.
constexpr int kButtonH = 24;
constexpr int kDefaultButtonH = 26;
constexpr int kDefaultButtonPad = 3; // ring inset inside default outer
constexpr int kFieldH = 20;
constexpr int kFindDlgW = 442; // Sagrado Find window size
constexpr int kFindDlgH = 176;

// Default-button outer around a regular face width, same top as Find.
inline Rect default_button_rect(int x, int y, int face_w) {
    return {x, y, face_w + 2 * kDefaultButtonPad, kDefaultButtonH};
}

struct Appearance {
    Skin skin;
    ColorMap stock;

    Appearance() : skin(stock_skin()), stock(stock_colors()) {}

    void set_skin(Skin s) { skin = std::move(s); }

    bool load(const std::string &path) {
        Skin s;
        if (!skin_toml::load(path, s)) return false;
        skin = std::move(s);
        return true;
    }

    bool save(const std::string &path) const { return skin_toml::save(path, skin); }

    Color c(const char *role) const { return resolve_color(skin, stock, role); }

    void set_color(const char *role, Color col) { skin.colors[role] = col; }

    Color title_label(bool focused) const {
        // Standard colour path uses Window / Window Focus Label. Bitmap themes
        // leave those white — KDX then paints Primary Label / Disable Label.
        const char *role = focused ? "window_focus.label" : "window.label";
        Color wl = c(role);
        bool white = wl.r == 255 && wl.g == 255 && wl.b == 255;
        if (white)
            return focused ? c("primary.label") : c("primary.disable_label");
        return wl;
    }
};

struct GelLayout {
    Rect window;
    Rect client;
    Rect close_box;
    Rect hatch_box; // w == 0 when omitted (Find dialog)
    Rect max_box;
    Rect min_box;
    Rect grip;
    Rect title;
    int title_h = kTitleH;
};

enum class GelStyle {
    Main,   // TextEdit main: close + hatch + max + min + grip
    Dialog, // Find dialog: close + min only (no hatch/max/grip)
};

// Same placement as Sagrado chrome_layout Standard path (no art).
inline GelLayout gel_layout(int x, int y, int w, int h,
                            GelStyle style = GelStyle::Main) {
    GelLayout lay;
    lay.window = {x, y, w, h};
    lay.title_h = kTitleH;
    lay.client = {x + kBorder, y + kTitleH, w - 2 * kBorder,
                  h - kTitleH - kBorder};
    int by = y + kBtnTop;
    lay.close_box = {x + 5, by, kBtnBox, kBtnBox};
    lay.hatch_box = {lay.close_box.right() + 8, by, kHatchW, kBtnBox};
    lay.min_box = {x + w - 5 - kBtnBox, by, kBtnBox, kBtnBox};
    lay.max_box = {lay.min_box.x - 4 - kBtnBox, by, kBtnBox, kBtnBox};
    lay.grip = {x + w - kGrip, y + h - kGrip, kGrip, kGrip};
    lay.title = {x, y, w, kTitleH};
    if (style == GelStyle::Dialog) {
        // Match Sagrado paint_find_dialog chrome.
        lay.hatch_box = {0, 0, 0, 0};
        lay.max_box = {0, 0, 0, 0};
        lay.grip = {0, 0, 0, 0};
    }
    return lay;
}

// Title-bar box as KDX Settings draws it: red plate inset into the bar
// (dark top-left / bright bottom-right). Pressed fills deeper.
inline void gel_bevel_box(Canvas &cv, Rect r, bool pressed, Color bright,
                          Color body, Color deep, Color frame) {
    if (r.w <= 0 || r.h <= 0) return;
    cv.fill(r, pressed ? deep : body);
    cv.frame(r, frame);
    // Idle = inset into the title slab (matches real KDX Settings).
    // Pressed = even deeper / inverted highlight.
    Color tl = pressed ? bright : deep;
    Color br = pressed ? deep : bright;
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, tl);
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, tl);
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, br);
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, br);
}

inline void gel_close_glyph(Canvas &cv, Rect r, Color c) {
    // 10×10 close glyph measured from Haxial TextEdit (box-relative 2,2).
    static const char *rows[10] = {
        "WW......WW", "WWW....WWW", ".WWW..WWW.", "..WWWWWW..",
        "...WWWW...", "...WWWW...", "..WWWWWW..", ".WWW..WWW.",
        "WWW....WWW", "WW......WW",
    };
    for (int row = 0; row < 10; ++row)
        for (int col = 0; col < 10; ++col)
            if (rows[row][col] == 'W')
                cv.put(r.x + 2 + col, r.y + 2 + row, pack(c));
}

inline void gel_diagonal_hatch(Canvas &cv, Rect r, Color c) {
    // 3px-wide '/' stripes on a 7px period — TextEdit title drag hatch.
    for (int i = 0; i < r.w + r.h + 7; i += 7)
        for (int t = 0; t < 3; ++t)
            for (int row = 0; row < r.h; ++row) {
                int col = i + t - row;
                if (col >= 0 && col < r.w) cv.put(r.x + col, r.y + row, pack(c));
            }
}

// Grow box — Sagrado paint_grip. Call after chrome (and usually after content).
inline void paint_gel_grip(Canvas &cv, const Appearance &ap, Rect g, bool focused) {
    if (g.w <= 0 || g.h <= 0) return;
    const char *grp = focused ? "window_focus" : "window";
    auto role = [&](const char *suffix) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.%s", grp, suffix);
        return ap.c(buf);
    };
    Color bright = role("light1");
    Color body = role("face");
    Color frame = role("frame");
    cv.fill({g.x, g.y, g.w - 2, g.h - 2}, body);
    cv.hline(g.x, g.right(), g.y, frame);
    cv.vline(g.x, g.y, g.bottom(), frame);
    cv.hline(g.x + 1, g.right() - 2, g.y + 1, bright);
    cv.vline(g.x + 1, g.y + 1, g.bottom() - 2, bright);
    for (int i = 0; i < 3; ++i) {
        int o = 4 + i * 4;
        for (int s = 0; s <= o; ++s) {
            cv.put(g.right() - 3 - o + s, g.bottom() - 3 - s, pack(bright));
            cv.put(g.right() - 2 - o + s, g.bottom() - 3 - s, pack(bright));
        }
    }
}

// Standard gel — Sagrado paint_chrome colour path (no art).
// solid slab, title gradient into side borders, client hole, flat title boxes.
inline void paint_gel(Canvas &cv, const Appearance &ap, Rect win,
                      const char *title, bool focused, int pressed_box = 0,
                      GelStyle style = GelStyle::Main) {
    const char *g = focused ? "window_focus" : "window";
    auto role = [&](const char *suffix) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.%s", g, suffix);
        return ap.c(buf);
    };

    // ChromeColors: bright=Light1, body=Face/Window, deep=Dark1
    Color bright = role("light1");
    Color body = role("face");
    Color deep = role("dark1");
    Color frame = role("frame");
    // Title + glyphs use Window Label on the Standard colour path (Sagrado).
    Color label = role("label");
    bool label_white = label.r == 255 && label.g == 255 && label.b == 255;
    if (label_white) label = ap.title_label(focused);

    Color grad[18];
    for (int i = 0; i < 18; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.transition.%d", g, i);
        grad[i] = ap.c(buf);
    }

    GelLayout lay = gel_layout(win.x, win.y, win.w, win.h, style);
    Rect client = lay.client;
    Rect slab{win.x + 1, win.y + 1, win.w - 2, win.h - 2};

    cv.fill(slab, body);
    for (int i = 0; i < 18; ++i)
        cv.hline(slab.x, slab.right(), win.y + 2 + i, grad[i]);
    cv.hline(slab.x, slab.right(), win.y + kTitleH - 2, deep);

    cv.hline(slab.x, slab.right(), slab.y, bright);
    cv.vline(slab.x, slab.y, slab.bottom(), bright);
    cv.vline(client.right() + 1, client.y - 1, client.bottom() + 1, bright);
    cv.hline(client.x - 1, client.right() + 2, client.bottom() + 1, bright);

    cv.vline(client.x - 2, client.y - 2, slab.bottom(), deep);
    cv.hline(client.x - 2, client.right() + 1, client.y - 2, deep);
    cv.vline(slab.right() - 1, client.y - 2, slab.bottom(), deep);
    cv.hline(slab.x + 1, slab.right(), slab.bottom() - 1, deep);

    cv.frame({client.x - 1, client.y - 1, client.w + 2, client.h + 2}, frame);
    cv.frame(win, frame);
    cv.fill(client, ap.c("primary.background"));

    int tw = cv.text_width(title);
    cv.text(win.x + (win.w - tw) / 2, win.y + (kTitleH - kFontHeight) / 2, title,
            label);

    if (lay.close_box.w > 0) {
        gel_bevel_box(cv, lay.close_box, pressed_box == 1, bright, body, deep, frame);
        gel_close_glyph(cv, lay.close_box, label);
    }
    if (lay.hatch_box.w > 0) {
        gel_bevel_box(cv, lay.hatch_box, false, bright, body, deep, frame);
        gel_diagonal_hatch(cv,
                           {lay.hatch_box.x + 2, lay.hatch_box.y + 2,
                            lay.hatch_box.w - 4, lay.hatch_box.h - 4},
                           label);
    }
    if (lay.max_box.w > 0) {
        gel_bevel_box(cv, lay.max_box, pressed_box == 3, bright, body, deep, frame);
        cv.fill({lay.max_box.x + 1, lay.max_box.y + 6, 10, 2}, label);
        cv.fill({lay.max_box.x + 5, lay.max_box.y + 2, 2, 10}, label);
    }
    if (lay.min_box.w > 0) {
        gel_bevel_box(cv, lay.min_box, pressed_box == 4, bright, body, deep, frame);
        cv.fill({lay.min_box.x + 1, lay.min_box.y + 6, 10, 2}, label);
    }
}

// --- Controls ------------------------------------------------------------

inline void rounded_frame(Canvas &cv, Rect r, Color frame, Color bg) {
    cv.hline(r.x + 1, r.right() - 1, r.y, frame);
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 1, frame);
    cv.vline(r.x, r.y + 1, r.bottom() - 1, frame);
    cv.vline(r.right() - 1, r.y + 1, r.bottom() - 1, frame);
    cv.put(r.x, r.y, pack(bg));
    cv.put(r.right() - 1, r.y, pack(bg));
    cv.put(r.x, r.bottom() - 1, pack(bg));
    cv.put(r.right() - 1, r.bottom() - 1, pack(bg));
}

// `r` is the outer hit/layout rect. Default buttons use a slightly taller
// outer (measured Find = 26) with a 3px ring inset to the face.
inline void paint_button(Canvas &cv, const Appearance &ap, Rect r,
                         const char *label, bool pressed, bool is_default) {
    Color workspace = ap.c("primary.background");
    if (is_default) {
        rounded_frame(cv, r, ap.c("default_button.frame"), workspace);
        cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ap.c("default_button.light"));
        cv.frame({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, ap.c("default_button.face"));
        r = {r.x + kDefaultButtonPad, r.y + kDefaultButtonPad,
             r.w - 2 * kDefaultButtonPad, r.h - 2 * kDefaultButtonPad};
    }
    Color face = ap.c("button.face");
    Color l2 = pressed ? ap.c("button.dark2") : ap.c("button.light2");
    Color l1 = pressed ? ap.c("button.dark1") : ap.c("button.light1");
    Color d1 = pressed ? ap.c("button.light1") : ap.c("button.dark1");
    Color d2 = pressed ? ap.c("button.light2") : ap.c("button.dark2");
    cv.fill(r, face);
    rounded_frame(cv, r, ap.c("button.frame"), workspace);
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, l2);
    cv.hline(r.x + 2, r.right() - 2, r.y + 2, l1);
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, l2);
    cv.vline(r.x + 2, r.y + 2, r.bottom() - 2, l1);
    cv.hline(r.x + 2, r.right() - 2, r.bottom() - 3, d1);
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, d2);
    cv.vline(r.right() - 3, r.y + 2, r.bottom() - 2, d1);
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, d2);
    // Haxial TextEdit placement: advance-box centre (matches measured Find).
    int tw = cv.text_width(label);
    int off = pressed ? 1 : 0;
    cv.text(r.x + (r.w - tw) / 2 + off, r.y + (r.h - kFontHeight) / 2 + off,
            label, ap.c("button.label"));
}

inline void paint_field(Canvas &cv, const Appearance &ap, Rect r,
                        const char *text, bool focused, bool caret_on) {
    cv.fill(r, ap.c("text.background"));
    if (focused) {
        cv.frame(r, ap.c("focus.box"));
        cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ap.c("focus.box"));
    } else {
        cv.frame(r, ap.c("primary.frame"));
    }
    int tx = r.x + 5;
    int ty = r.y + (r.h - kFontHeight) / 2;
    int end = cv.text(tx, ty, text, ap.c("text.foreground"));
    if (focused && caret_on)
        cv.vline(end + 1, r.y + 3, r.bottom() - 3, ap.c("text.insertion_point"));
}

// Find-proportioned dialog sample — Sagrado native Find size (442×176) and
// chrome (close + min only). This is the title-bar reference, not a 72px stub.
inline GelLayout paint_find_chrome_sample(Canvas &cv, const Appearance &ap,
                                          int x, int y, int max_w,
                                          bool caret_on) {
    int w = std::min(max_w, kFindDlgW);
    int h = kFindDlgH;
    Rect win{x, y, w, h};
    GelLayout lay = gel_layout(x, y, w, h, GelStyle::Dialog);
    paint_gel(cv, ap, win, "Find and Replace", true, 0, GelStyle::Dialog);

    Rect cl = lay.client;
    int lx = cl.x + 10;
    int fx = cl.x + 84;
    int fw = cl.right() - 14 - fx;
    if (fw < 40) fw = 40;
    Rect find_field{fx, cl.y + 10, fw, kFieldH};
    Rect repl_field{fx, cl.y + 40, fw, kFieldH};
    cv.text(lx, find_field.y + 6, "Find:", ap.c("primary.label"));
    cv.text(lx, repl_field.y + 6, "Replace:", ap.c("primary.label"));
    paint_field(cv, ap, find_field, "needle", true, caret_on);
    paint_field(cv, ap, repl_field, "replacement", false, false);

    int cy = cl.y + 74;
    auto paint_check = [&](int cx, int cy0, bool on, const char *lab) {
        Rect b{cx, cy0, 14, 14};
        cv.fill(b, ap.c("button.face"));
        rounded_frame(cv, b, ap.c("button.frame"), ap.c("primary.background"));
        cv.hline(b.x + 1, b.right() - 1, b.y + 1, ap.c("button.light2"));
        cv.vline(b.x + 1, b.y + 1, b.bottom() - 1, ap.c("button.light2"));
        cv.hline(b.x + 1, b.right() - 1, b.bottom() - 2, ap.c("button.dark2"));
        cv.vline(b.right() - 2, b.y + 1, b.bottom() - 1, ap.c("button.dark2"));
        if (on) {
            Color ink = ap.c("button.label");
            for (int i = 0; i < 4; ++i) {
                cv.put(b.x + 3, b.y + 6 + i, pack(ink));
                cv.put(b.x + 4, b.y + 7 + i, pack(ink));
            }
            for (int i = 0; i < 6; ++i) {
                cv.put(b.x + 5 + i, b.y + 9 - i, pack(ink));
                cv.put(b.x + 5 + i, b.y + 10 - i, pack(ink));
            }
        }
        cv.text(cx + 20, cy0 + (14 - kFontHeight) / 2, lab, ap.c("primary.label"));
    };
    paint_check(lx, cy, true, "Case Sensitive");
    if (cl.w > 280) paint_check(cl.x + 190, cy, false, "Stop at End of File");

    int by = cl.bottom() - 34;
    Rect b_all{lx, by, 96, kButtonH};
    Rect b_repl{b_all.right() + 8, by, 76, kButtonH};
    Rect b_find{cl.right() - 14 - 84, by, 84, kDefaultButtonH};
    Rect b_cancel{b_find.x - 10 - 76, by, 76, kButtonH};
    if (b_cancel.x > b_repl.right() + 4) {
        paint_button(cv, ap, b_all, "Replace All", false, false);
        paint_button(cv, ap, b_repl, "Replace", false, false);
        paint_button(cv, ap, b_cancel, "Cancel", false, false);
        paint_button(cv, ap, b_find, "Find", false, true);
    } else {
        paint_button(cv, ap, b_cancel, "Cancel", false, false);
        paint_button(cv, ap, b_find, "Find", false, true);
    }
    return lay;
}

inline void paint_column_header(Canvas &cv, const Appearance &ap, Rect r,
                                const char *label, bool hilite) {
    Color face = hilite ? ap.c("column_header.hilite") : ap.c("column_header.face");
    Color light = hilite ? ap.c("column_header.hilite_light") : ap.c("column_header.light");
    Color dark = hilite ? ap.c("column_header.hilite_dark") : ap.c("column_header.dark");
    Color ink = hilite ? ap.c("column_header.hilite_label") : ap.title_label(true);
    // Prefer primary.label when header label is stock white
    if (!hilite) {
        Color hl = ap.c("column_header.label");
        bool white = hl.r == 255 && hl.g == 255 && hl.b == 255;
        if (!(ap.skin.colors.count("column_header.label") && !white))
            ink = ap.c("primary.label");
        else
            ink = hl;
    }
    cv.fill(r, face);
    cv.frame(r, ap.c("column_header.frame"));
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, light);
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, light);
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, dark);
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, dark);
    cv.text(r.x + 6, r.y + (r.h - kFontHeight) / 2, label, ink);
}

inline void paint_list(Canvas &cv, const Appearance &ap, Rect r,
                       const char *const *rows, int n_rows, int selected,
                       const char *header) {
    // Header + body + optional v-scrollbar groove on the right
    Rect hdr{r.x, r.y, r.w - kScrollbarW, kHeaderH};
    paint_column_header(cv, ap, hdr, header, true);

    Rect body{r.x, r.y + kHeaderH, r.w - kScrollbarW, r.h - kHeaderH};
    cv.fill(body, ap.c("list.background"));
    cv.frame(body, ap.c("primary.frame"));

    int visible = body.h / kRowH;
    for (int i = 0; i < visible; ++i) {
        Rect row{body.x + 1, body.y + 1 + i * kRowH, body.w - 2, kRowH};
        bool sel = (i == selected);
        if (sel)
            cv.fill(row, ap.c("list.hilite_background"));
        else if (i % 2)
            cv.fill(row, ap.c("list.sort_column_background"));
        if (i < n_rows) {
            Color ink = sel ? ap.c("list.hilite_foreground") : ap.c("list.label");
            cv.text(row.x + 6, row.y + (kRowH - kFontHeight) / 2, rows[i], ink);
        }
        cv.hline(row.x, row.right(), row.bottom() - 1, ap.c("list.separator"));
    }
}

struct ScrollLayout {
    Rect bar, up, down, track, thumb;
};

inline ScrollLayout scroll_layout(Rect bar, int value, int max_value, int page) {
    ScrollLayout s;
    s.bar = bar;
    s.up = {bar.x, bar.y, bar.w, bar.w};
    s.down = {bar.x, bar.bottom() - bar.w, bar.w, bar.w};
    int track_h = s.down.y - s.up.bottom();
    if (track_h < 0) track_h = 0;
    s.track = {bar.x, s.up.bottom(), bar.w, track_h};
    if (page < 1) page = 1;
    if (max_value < 0) max_value = 0;
    int thumb_h = max_value == 0
                      ? s.track.h
                      : std::max(bar.w, s.track.h * page / (max_value + page));
    if (thumb_h > s.track.h) thumb_h = s.track.h;
    int travel = s.track.h - thumb_h;
    int ty = s.track.y;
    if (max_value > 0 && travel > 0)
        ty += travel * std::clamp(value, 0, max_value) / max_value;
    s.thumb = {bar.x, ty, bar.w, thumb_h};
    return s;
}

inline void paint_arrow(Canvas &cv, Rect r, bool up, Color ink) {
    // 7×4 triangle centred in the plate
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    if (up) {
        int top = cy - 2;
        for (int i = 0; i < 4; ++i)
            cv.hline(cx - i, cx + i + 1, top + i, ink);
    } else {
        int bot = cy + 2;
        for (int i = 0; i < 4; ++i)
            cv.hline(cx - i, cx + i + 1, bot - i, ink);
    }
}

inline void paint_scrollbar(Canvas &cv, const Appearance &ap, Rect bar,
                            int value, int max_value, int page, bool hilite_thumb) {
    ScrollLayout s = scroll_layout(bar, value, max_value, page);
    // Track
    cv.fill(s.track, ap.c("scrollbar.track"));
    cv.hline(s.track.x, s.track.right(), s.track.y, ap.c("scrollbar.track_light2"));
    cv.vline(s.track.x, s.track.y, s.track.bottom(), ap.c("scrollbar.track_light1"));
    cv.hline(s.track.x, s.track.right(), s.track.bottom() - 1, ap.c("scrollbar.track_dark2"));
    cv.vline(s.track.right() - 1, s.track.y, s.track.bottom(), ap.c("scrollbar.track_dark1"));

    auto paint_btn = [&](Rect r) {
        cv.fill(r, ap.c("scrollbar.face"));
        cv.frame(r, ap.c("scrollbar.frame"));
        cv.hline(r.x + 1, r.right() - 1, r.y + 1, ap.c("scrollbar.light"));
        cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, ap.c("scrollbar.light"));
        cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, ap.c("scrollbar.dark"));
        cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, ap.c("scrollbar.dark"));
    };
    paint_btn(s.up);
    paint_btn(s.down);
    paint_arrow(cv, s.up, true, ap.c("scrollbar.label"));
    paint_arrow(cv, s.down, false, ap.c("scrollbar.label"));

    Color il = hilite_thumb ? ap.c("scrollbar.hilite_light") : ap.c("scrollbar.indicator_light");
    Color ind = hilite_thumb ? ap.c("scrollbar.hilite") : ap.c("scrollbar.indicator");
    Color id = hilite_thumb ? ap.c("scrollbar.hilite_dark") : ap.c("scrollbar.indicator_dark");
    cv.fill(s.thumb, ind);
    cv.frame(s.thumb, ap.c("scrollbar.frame"));
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.y + 1, il);
    cv.vline(s.thumb.x + 1, s.thumb.y + 1, s.thumb.bottom() - 1, il);
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.bottom() - 2, id);
    cv.vline(s.thumb.right() - 2, s.thumb.y + 1, s.thumb.bottom() - 1, id);
}

constexpr int kMenuItemH = 18;
constexpr int kSliderThumbW = 11;
constexpr int kSliderThumbH = 18;
constexpr int kDropArrowW = 20; // AppearanceEdit: No-Title popup usually 20×20

struct MenuLayout {
    Rect frame{};
    Rect items_bounds{};
    int item_h = kMenuItemH;
    int count = 0;
};

// Popup / drop-down menu panel. `hot` is the hilited row (-1 none).
// Optional `disabled_mask` bit i disables item i.
inline MenuLayout paint_menu(Canvas &cv, const Appearance &ap, int x, int y,
                             int width, const char *const *items, int count,
                             int hot = -1, unsigned disabled_mask = 0) {
    MenuLayout lay;
    lay.count = count;
    lay.item_h = kMenuItemH;
    int h = 4 + count * kMenuItemH;
    lay.frame = {x, y, width, h};
    // Outer ring uses focus box (Haxial: Window Focus / Focus Box)
    cv.fill(lay.frame, ap.c("menu.background"));
    cv.frame(lay.frame, ap.c("focus.box"));
    cv.frame({x + 1, y + 1, width - 2, h - 2}, ap.c("menu.dark"));
    // Bevel
    cv.hline(x + 2, x + width - 2, y + 2, ap.c("menu.light"));
    cv.vline(x + 2, y + 2, y + h - 2, ap.c("menu.light"));

    lay.items_bounds = {x + 2, y + 2, width - 4, count * kMenuItemH};
    for (int i = 0; i < count; ++i) {
        Rect row{lay.items_bounds.x, lay.items_bounds.y + i * kMenuItemH,
                 lay.items_bounds.w, kMenuItemH};
        bool dis = (disabled_mask >> i) & 1;
        bool is_hot = (!dis && i == hot);
        if (is_hot) {
            cv.fill(row, ap.c("menu.hilite_background"));
            cv.hline(row.x, row.right(), row.y, ap.c("menu.hilite_light"));
            cv.vline(row.x, row.y, row.bottom(), ap.c("menu.hilite_light"));
            cv.hline(row.x, row.right(), row.bottom() - 1, ap.c("menu.hilite_dark"));
            cv.vline(row.right() - 1, row.y, row.bottom(), ap.c("menu.hilite_dark"));
        }
        Color ink = dis ? ap.c("menu.disable_label")
                        : (is_hot ? ap.c("menu.hilite_label") : ap.c("menu.label"));
        cv.text(row.x + 8, row.y + (kMenuItemH - kFontHeight) / 2, items[i], ink);
    }
    return lay;
}

inline int menu_hit_row(const MenuLayout &lay, int mx, int my) {
    if (!lay.items_bounds.contains(mx, my) || lay.item_h <= 0) return -1;
    int row = (my - lay.items_bounds.y) / lay.item_h;
    if (row < 0 || row >= lay.count) return -1;
    return row;
}

// Haxial Popup Button — a real push-button plate with a recessed arrow well.
// NOT a text field. AppearanceEdit: usually ~20px; KDX Settings matches sibling
// dialog buttons. Colour path uses Button / Button Hilite groups + Symbol.
inline void paint_dropdown(Canvas &cv, const Appearance &ap, Rect r,
                           const char *label, bool open, bool pressed,
                           bool disabled = false) {
    Color workspace = ap.c("primary.background");
    const char *grp = disabled ? "button_disable"
                     : (pressed || open) ? "button_hilite" : "button";
    auto bc = [&](const char *suffix) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s.%s", grp, suffix);
        return ap.c(buf);
    };

    Color face = bc("face");
    Color l2 = bc("light2");
    Color l1 = bc("light1");
    Color d1 = bc("dark1");
    Color d2 = bc("dark2");
    Color fr = bc("frame");
    Color ink = bc("label");

    // Whole control = button chrome (rounded frame + 2px bevel)
    cv.fill(r, face);
    rounded_frame(cv, r, fr, workspace);
    if (!pressed && !open) {
        cv.hline(r.x + 1, r.right() - 1, r.y + 1, l2);
        cv.hline(r.x + 2, r.right() - 2, r.y + 2, l1);
        cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, l2);
        cv.vline(r.x + 2, r.y + 2, r.bottom() - 2, l1);
        cv.hline(r.x + 2, r.right() - 2, r.bottom() - 3, d1);
        cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, d2);
        cv.vline(r.right() - 3, r.y + 2, r.bottom() - 2, d1);
        cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, d2);
    } else {
        // Depressed / open — invert bevel
        cv.hline(r.x + 1, r.right() - 1, r.y + 1, d2);
        cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, d2);
        cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, l2);
        cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, l2);
    }

    // Recessed arrow well on the right (KDX Settings)
    int aw = std::min(kDropArrowW, r.w / 3);
    if (aw < 14) aw = std::min(14, r.w);
    Rect well{r.right() - aw - 1, r.y + 2, aw - 1, r.h - 4};
    if (well.w > 4 && well.h > 4) {
        cv.fill(well, d1);
        // Inset bevel: dark top-left, light bottom-right
        cv.hline(well.x, well.right(), well.y, d2);
        cv.vline(well.x, well.y, well.bottom(), d2);
        cv.hline(well.x, well.right(), well.bottom() - 1, l2);
        cv.vline(well.right() - 1, well.y, well.bottom(), l2);
        paint_arrow(cv, well, false, ink);
    }

    // Title left of the well
    int tx = r.x + 8;
    int ty = r.y + (r.h - kFontHeight) / 2 + ((pressed || open) ? 1 : 0);
    int max_w = well.x - tx - 4;
    if (max_w < 8) max_w = 8;
    if (cv.text_width(label) <= max_w)
        cv.text(tx, ty, label, ink);
    else {
        std::string s(label);
        while (s.size() > 1 && cv.text_width((s + "..").c_str()) > max_w) s.pop_back();
        s += "..";
        cv.text(tx, ty, s.c_str(), ink);
    }
}

struct SliderLayout {
    Rect bounds{};
    Rect bar{};
    Rect thumb{};
    int value = 0;
    int max_value = 100;
};

inline SliderLayout slider_layout(Rect r, int value, int max_value) {
    SliderLayout s;
    s.bounds = r;
    s.max_value = std::max(1, max_value);
    s.value = std::clamp(value, 0, s.max_value);
    int bar_h = 6;
    s.bar = {r.x + 2, r.y + (r.h - bar_h) / 2, r.w - 4, bar_h};
    int travel = std::max(0, s.bar.w - kSliderThumbW);
    int tx = s.bar.x + (travel * s.value) / s.max_value;
    s.thumb = {tx, r.y + (r.h - kSliderThumbH) / 2, kSliderThumbW, kSliderThumbH};
    return s;
}

inline int slider_value_at_x(const SliderLayout &s, int mx) {
    int travel = std::max(1, s.bar.w - kSliderThumbW);
    int rel = mx - s.bar.x - kSliderThumbW / 2;
    return std::clamp(rel * s.max_value / travel, 0, s.max_value);
}

inline SliderLayout paint_slider(Canvas &cv, const Appearance &ap, Rect r,
                                 int value, int max_value, bool hilite) {
    SliderLayout s = slider_layout(r, value, max_value);
    // Bar track
    cv.fill(s.bar, ap.c("slider.bar"));
    cv.frame(s.bar, ap.c("slider.bar_frame"));
    // Filled portion
    if (s.value > 0) {
        int fill_w = (s.thumb.x + kSliderThumbW / 2) - s.bar.x;
        fill_w = std::clamp(fill_w, 0, s.bar.w);
        Rect fill{s.bar.x, s.bar.y, fill_w, s.bar.h};
        cv.fill(fill, ap.c("slider.bar_hilite"));
        cv.frame(fill, ap.c("slider.bar_hilite_frame"));
    }
    // Thumb
    Color il = hilite ? ap.c("slider.indicator_hilite_light") : ap.c("slider.indicator_light");
    Color ind = hilite ? ap.c("slider.indicator_hilite") : ap.c("slider.indicator");
    Color id = hilite ? ap.c("slider.indicator_hilite_dark") : ap.c("slider.indicator_dark");
    Color fr = hilite ? ap.c("slider.indicator_hilite_frame") : ap.c("slider.indicator_frame");
    cv.fill(s.thumb, ind);
    rounded_frame(cv, s.thumb, fr, ap.c("primary.background"));
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.y + 1, il);
    cv.vline(s.thumb.x + 1, s.thumb.y + 1, s.thumb.bottom() - 1, il);
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.bottom() - 2, id);
    cv.vline(s.thumb.right() - 2, s.thumb.y + 1, s.thumb.bottom() - 1, id);
    // Grip ridges — three horizontal lines like KDX Settings volume thumb
    int mid = s.thumb.y + s.thumb.h / 2;
    for (int i = -2; i <= 2; i += 2)
        cv.hline(s.thumb.x + 2, s.thumb.right() - 2, mid + i, il);
    return s;
}

struct KitPreviewLayout {
    Rect bounds{};
    Rect btn_ok{}, btn_cancel{}, btn_press{};
    Rect field{};
    Rect dropdown{};
    Rect menu{};
    MenuLayout menu_lay{};
    Rect slider{};
    SliderLayout slider_lay{};
    Rect list{};
    Rect sbar{};
    int page_rows = 1;
    int row_count = 8;
};

struct KitPreviewState {
    int pressed_btn = 0; // 0 none, 1 OK, 2 Cancel, 3 Pressed
    bool thumb_hot = false;
    bool dropdown_open = false;
    int menu_hot = -1;
    int menu_sel = 0;
    int slider_value = 40;
    bool slider_hot = false;
};

// Full kit preview panel inside a gel client rect.
inline KitPreviewLayout paint_kit_preview(Canvas &cv, const Appearance &ap,
                                          Rect client, bool caret_on,
                                          int list_sel, int scroll_val,
                                          const KitPreviewState &st = {}) {
    KitPreviewLayout lay;
    lay.bounds = client;
    int pad = 10;
    int x = client.x + pad;
    int y = client.y + pad;
    int w = client.w - 2 * pad;

    cv.text(x, y, "Kit Preview", ap.c("primary.label"));
    y += kFontHeight + 8;

    // Find-sized dialog chrome — title bar / icons at real TextEdit proportions
    // (Sagrado Find is 442×176). Not a stubby 72px nested gel.
    paint_find_chrome_sample(cv, ap, x, y, w, caret_on);
    y += kFindDlgH + 10;

    // Buttons — Find metrics: regular 24px; default OK 26px outer, same top.
    constexpr int kBtnW = 72;
    constexpr int kBtnGap = 10;
    lay.btn_ok = default_button_rect(x, y, kBtnW);
    lay.btn_cancel = {lay.btn_ok.right() + kBtnGap, y, kBtnW, kButtonH};
    lay.btn_press = {lay.btn_cancel.right() + kBtnGap, y, kBtnW, kButtonH};
    paint_button(cv, ap, lay.btn_ok, "OK", st.pressed_btn == 1, true);
    paint_button(cv, ap, lay.btn_cancel, "Cancel", st.pressed_btn == 2, false);
    paint_button(cv, ap, lay.btn_press, "Pressed", true, false);
    y += kDefaultButtonH + 8;

    // Field + dropdown — AppearanceEdit: usually 20px tall
    int field_w = std::min(w, 280);
    lay.field = {x, y, field_w, kFieldH};
    paint_field(cv, ap, lay.field, "Edit colour roles...", true, caret_on);
    y += kFieldH + 8;

    // Popup buttons — real bevelled plates (KDX Settings), not text fields.
    // AppearanceEdit default ~20px; match sibling buttons when in a dialog row.
    static const char *menu_items[] = {"Standard", "Slate", "Custom...", "Disabled"};
    static const unsigned kMenuDisabled = 1u << 3;
    const char *drop_label = menu_items[std::clamp(st.menu_sel, 0, 3)];
    int pop_h = kButtonH; // KDX Settings: popup matches sibling dialog buttons
    lay.dropdown = {x, y, std::min(w, 200), pop_h};
    paint_dropdown(cv, ap, lay.dropdown, drop_label, st.dropdown_open,
                   st.pressed_btn == 4, false);
    // Disabled companion (KDX Settings "Sound List" / None)
    Rect drop_dis{lay.dropdown.right() + 10, y, std::min(120, w - lay.dropdown.w - 10),
                  pop_h};
    if (drop_dis.w > 60)
        paint_dropdown(cv, ap, drop_dis, "None", false, false, true);
    y += pop_h + 8;

    // Slider (bar centred in a ≤30px total height band per AppearanceEdit)
    cv.text(x, y + 2, "Slider", ap.c("primary.label"));
    lay.slider = {x + 56, y, std::min(w - 56, 200), 22};
    lay.slider_lay =
        paint_slider(cv, ap, lay.slider, st.slider_value, 100, st.slider_hot);
    char sval[16];
    std::snprintf(sval, sizeof(sval), "%d", st.slider_value);
    cv.text(lay.slider.right() + 8, y + 3, sval, ap.c("primary.label"));
    y += 28;

    // List + header + scrollbar
    static const char *rows[] = {"Row One", "Row Two", "Row Three", "Row Four",
                                 "Row Five", "Row Six", "Row Seven", "Row Eight"};
    lay.row_count = 8;
    int list_h = client.bottom() - y - pad;
    if (list_h < 72) list_h = 72;
    lay.list = {x, y, std::min(w, 320), list_h};
    lay.page_rows = std::max(1, (lay.list.h - kHeaderH) / kRowH);
    int max_scroll = std::max(0, lay.row_count - lay.page_rows);
    if (scroll_val < 0) scroll_val = 0;
    if (scroll_val > max_scroll) scroll_val = max_scroll;

    Rect hdr{lay.list.x, lay.list.y, lay.list.w - kScrollbarW, kHeaderH};
    paint_column_header(cv, ap, hdr, "Name", true);
    Rect body{lay.list.x, lay.list.y + kHeaderH, lay.list.w - kScrollbarW,
              lay.list.h - kHeaderH};
    cv.fill(body, ap.c("list.background"));
    cv.frame(body, ap.c("primary.frame"));
    for (int i = 0; i < lay.page_rows; ++i) {
        int idx = scroll_val + i;
        Rect row{body.x + 1, body.y + 1 + i * kRowH, body.w - 2, kRowH};
        bool sel = (idx == list_sel);
        if (sel)
            cv.fill(row, ap.c("list.hilite_background"));
        else if (i % 2)
            cv.fill(row, ap.c("list.sort_column_background"));
        if (idx >= 0 && idx < lay.row_count) {
            Color ink = sel ? ap.c("list.hilite_foreground") : ap.c("list.label");
            cv.text(row.x + 6, row.y + (kRowH - kFontHeight) / 2, rows[idx], ink);
        }
        cv.hline(row.x, row.right(), row.bottom() - 1, ap.c("list.separator"));
    }
    lay.sbar = {lay.list.right() - kScrollbarW, lay.list.y + kHeaderH, kScrollbarW,
                lay.list.h - kHeaderH};
    paint_scrollbar(cv, ap, lay.sbar, scroll_val, max_scroll, lay.page_rows, st.thumb_hot);

    // Open dropdown menu painted last so it stacks above the list
    if (st.dropdown_open) {
        lay.menu_lay = paint_menu(cv, ap, lay.dropdown.x, lay.dropdown.bottom(),
                                  lay.dropdown.w, menu_items, 4, st.menu_hot,
                                  kMenuDisabled);
        lay.menu = lay.menu_lay.frame;
    }
    return lay;
}
