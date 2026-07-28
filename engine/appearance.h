// Appearance Engine — every window speaks this.
// Resolve tokens (art → colour → stock) and paint kit surfaces into a Canvas.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>

#include "skin.h"

constexpr int kTitleH = 22;
constexpr int kBorder = 6;
constexpr int kBtnBox = 14;
constexpr int kScrollbarW = 16;
constexpr int kHeaderH = 20;
constexpr int kRowH = 18;

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
        // Prefer primary.label — art themes often leave window labels white.
        Color pl = c("primary.label");
        const char *role = focused ? "window_focus.label" : "window.label";
        Color wl = c(role);
        bool stock_white = wl.r == 255 && wl.g == 255 && wl.b == 255;
        bool skin_authored = skin.colors.count(role) != 0;
        if (skin_authored && !stock_white) return wl;
        return pl;
    }
};

// --- Gel window (framed chrome + client) ---------------------------------

struct GelLayout {
    Rect window;
    Rect client;
    Rect close_box;
    Rect max_box;
    Rect min_box;
    Rect title;
};

inline GelLayout gel_layout(int x, int y, int w, int h) {
    GelLayout lay;
    lay.window = {x, y, w, h};
    lay.client = {x + kBorder, y + kTitleH, w - 2 * kBorder, h - kTitleH - kBorder};
    int bx = x + w - kBorder - kBtnBox;
    int by = y + 4;
    lay.close_box = {bx, by, kBtnBox, kBtnBox};
    lay.max_box = {bx - kBtnBox - 2, by, kBtnBox, kBtnBox};
    lay.min_box = {bx - 2 * (kBtnBox + 2), by, kBtnBox, kBtnBox};
    lay.title = {x + kBorder + 2, y + 3, lay.min_box.x - x - kBorder - 6, kFontHeight};
    return lay;
}

inline void paint_gel(Canvas &cv, const Appearance &ap, Rect win,
                      const char *title, bool focused, int pressed_box = 0) {
    const char *g = focused ? "window_focus" : "window";
    auto role = [&](const char *suffix) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.%s", g, suffix);
        return ap.c(buf);
    };

    Color face = role("face");
    Color light2 = role("light2");
    Color light1 = role("light1");
    Color dark1 = role("dark1");
    Color dark2 = role("dark2");
    Color frame = role("frame");

    // Outer slab
    cv.fill(win, face);
    cv.frame(win, frame);
    // Bevel
    cv.hline(win.x + 1, win.right() - 1, win.y + 1, light2);
    cv.vline(win.x + 1, win.y + 1, win.bottom() - 1, light2);
    cv.hline(win.x + 2, win.right() - 2, win.y + 2, light1);
    cv.vline(win.x + 2, win.y + 2, win.bottom() - 2, light1);
    cv.hline(win.x + 2, win.right() - 2, win.bottom() - 3, dark1);
    cv.vline(win.right() - 3, win.y + 2, win.bottom() - 2, dark1);
    cv.hline(win.x + 1, win.right() - 1, win.bottom() - 2, dark2);
    cv.vline(win.right() - 2, win.y + 1, win.bottom() - 1, dark2);

    // Title gradient
    Color grad[18];
    for (int i = 0; i < 18; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.transition.%d", g, i);
        grad[i] = ap.c(buf);
    }
    Rect title_bar{win.x + kBorder, win.y + 2, win.w - 2 * kBorder, kTitleH - 2};
    cv.rect_grad_v(title_bar, grad, 18);

    GelLayout lay = gel_layout(win.x, win.y, win.w, win.h);

    // Client cut-out
    cv.fill(lay.client, ap.c("primary.background"));
    cv.frame(lay.client, ap.c("primary.dark"));
    cv.hline(lay.client.x, lay.client.right(), lay.client.y, ap.c("primary.dark"));
    cv.vline(lay.client.x, lay.client.y, lay.client.bottom(), ap.c("primary.dark"));
    cv.hline(lay.client.x, lay.client.right(), lay.client.bottom() - 1, ap.c("primary.light"));
    cv.vline(lay.client.right() - 1, lay.client.y, lay.client.bottom(), ap.c("primary.light"));

    // Title text — prefer primary.label
    Color ink = ap.title_label(focused);
    cv.text(lay.title.x, lay.title.y, title, ink);

    // Traffic-light boxes
    auto paint_box = [&](Rect b, int id, char glyph) {
        bool pressed = pressed_box == id;
        Color bf = pressed ? dark1 : light1;
        Color bd = pressed ? light1 : dark1;
        cv.fill(b, face);
        cv.frame(b, frame);
        cv.hline(b.x + 1, b.right() - 1, b.y + 1, bf);
        cv.vline(b.x + 1, b.y + 1, b.bottom() - 1, bf);
        cv.hline(b.x + 1, b.right() - 1, b.bottom() - 2, bd);
        cv.vline(b.right() - 2, b.y + 1, b.bottom() - 1, bd);
        char s[2] = {glyph, 0};
        int tw = cv.text_width(s);
        cv.text(b.x + (b.w - tw) / 2, b.y + (b.h - kFontHeight) / 2 + 1, s, ink);
    };
    paint_box(lay.min_box, 4, '-');
    paint_box(lay.max_box, 3, '+');
    paint_box(lay.close_box, 1, 'x');
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

inline void paint_button(Canvas &cv, const Appearance &ap, Rect r,
                         const char *label, bool pressed, bool is_default) {
    Color workspace = ap.c("primary.background");
    if (is_default) {
        rounded_frame(cv, r, ap.c("default_button.frame"), workspace);
        cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ap.c("default_button.light"));
        cv.frame({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, ap.c("default_button.face"));
        r = {r.x + 3, r.y + 3, r.w - 6, r.h - 6};
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
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    if (up) {
        for (int i = 0; i < 4; ++i)
            cv.hline(cx - i, cx + i + 1, cy - 2 + i, ink);
    } else {
        for (int i = 0; i < 4; ++i)
            cv.hline(cx - i, cx + i + 1, cy + 2 - i, ink);
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

struct KitPreviewLayout {
    Rect bounds{};
    Rect btn_ok{}, btn_cancel{}, btn_press{};
    Rect field{};
    Rect list{};
    Rect sbar{};
    int page_rows = 1;
    int row_count = 8;
};

// Full kit preview panel inside a gel client rect.
// `pressed_btn`: 0 none, 1 OK, 2 Cancel, 3 Pressed demo.
inline KitPreviewLayout paint_kit_preview(Canvas &cv, const Appearance &ap,
                                          Rect client, bool caret_on,
                                          int list_sel, int scroll_val,
                                          int pressed_btn = 0,
                                          bool thumb_hot = false) {
    KitPreviewLayout lay;
    lay.bounds = client;
    int pad = 10;
    int x = client.x + pad;
    int y = client.y + pad;
    int w = client.w - 2 * pad;

    cv.text(x, y, "Kit Preview", ap.c("primary.label"));
    y += kFontHeight + 8;

    // Nested gel sample
    Rect gel{x, y, std::min(w, 280), 120};
    paint_gel(cv, ap, gel, "Preview Window", true, 0);
    GelLayout gl = gel_layout(gel.x, gel.y, gel.w, gel.h);
    cv.text(gl.client.x + 8, gl.client.y + 8, "Client area", ap.c("primary.label"));
    y = gel.bottom() + 12;

    // Buttons + field
    lay.btn_ok = {x, y, 90, 24};
    lay.btn_cancel = {x + 100, y, 90, 24};
    lay.btn_press = {x + 200, y, 90, 24};
    paint_button(cv, ap, lay.btn_ok, "OK", pressed_btn == 1, true);
    paint_button(cv, ap, lay.btn_cancel, "Cancel", pressed_btn == 2, false);
    // Sample always reads as depressed; holding it keeps the same look.
    paint_button(cv, ap, lay.btn_press, "Pressed", true, false);
    y += 34;
    lay.field = {x, y, std::min(w, 290), 24};
    paint_field(cv, ap, lay.field, "Edit colour roles...", true, caret_on);
    y += 34;

    // List + header + scrollbar (scroll_val is first visible row)
    static const char *rows[] = {"Row One", "Row Two", "Row Three", "Row Four",
                                 "Row Five", "Row Six", "Row Seven", "Row Eight"};
    lay.row_count = 8;
    int list_h = client.bottom() - y - pad;
    if (list_h < 80) list_h = 80;
    lay.list = {x, y, std::min(w, 320), list_h};
    lay.page_rows = std::max(1, (lay.list.h - kHeaderH) / kRowH);
    int max_scroll = std::max(0, lay.row_count - lay.page_rows);
    if (scroll_val < 0) scroll_val = 0;
    if (scroll_val > max_scroll) scroll_val = max_scroll;

    // Paint header + scrolled body
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
    paint_scrollbar(cv, ap, lay.sbar, scroll_val, max_scroll, lay.page_rows, thumb_hot);
    return lay;
}
