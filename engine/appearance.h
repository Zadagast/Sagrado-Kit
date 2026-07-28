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

    // Title text — prefer primary.label; vertically centre on ink in the bar
    Color ink = ap.title_label(focused);
    cv.text_centered(lay.title, title, ink, 0);

    // Traffic-light boxes — drawn icons centred in the plate (not font glyphs;
    // the 16px face is taller than the 14px box and sits optically low).
    auto paint_box = [&](Rect b, int id, int kind) {
        bool pressed = pressed_box == id;
        Color bf = pressed ? dark1 : light1;
        Color bd = pressed ? light1 : dark1;
        cv.fill(b, face);
        cv.frame(b, frame);
        cv.hline(b.x + 1, b.right() - 1, b.y + 1, bf);
        cv.vline(b.x + 1, b.y + 1, b.bottom() - 1, bf);
        cv.hline(b.x + 1, b.right() - 1, b.bottom() - 2, bd);
        cv.vline(b.right() - 2, b.y + 1, b.bottom() - 1, bd);
        int cx = b.x + b.w / 2 + (pressed ? 1 : 0);
        int cy = b.y + b.h / 2 + (pressed ? 1 : 0);
        uint32_t p = pack(ink);
        if (kind == 0) { // minimize —
            for (int dx = -3; dx <= 3; ++dx) cv.put(cx + dx, cy, p);
        } else if (kind == 1) { // maximize □
            for (int dx = -3; dx <= 3; ++dx) {
                cv.put(cx + dx, cy - 3, p);
                cv.put(cx + dx, cy + 3, p);
            }
            for (int dy = -2; dy <= 2; ++dy) {
                cv.put(cx - 3, cy + dy, p);
                cv.put(cx + 3, cy + dy, p);
            }
        } else { // close ×
            for (int d = -3; d <= 3; ++d) {
                cv.put(cx + d, cy + d, p);
                cv.put(cx + d, cy - d, p);
            }
        }
    };
    paint_box(lay.min_box, 4, 0);
    paint_box(lay.max_box, 3, 1);
    paint_box(lay.close_box, 1, 2);
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
    // Inner face for label (keep clear of the 2px bevel)
    Rect label_r{r.x + 3, r.y + 3, r.w - 6, r.h - 6};
    cv.text_centered(label_r, label, ap.c("button.label"), pressed ? 1 : 0);
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
constexpr int kSliderThumbW = 10;
constexpr int kSliderThumbH = 18;
constexpr int kDropArrowW = 18;

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

// Closed drop-down (popup button): field face + label + arrow well.
inline void paint_dropdown(Canvas &cv, const Appearance &ap, Rect r,
                           const char *label, bool open, bool pressed) {
    Color bg = ap.c("text.background");
    Color workspace = ap.c("primary.background");
    cv.fill(r, bg);
    if (open)
        cv.frame(r, ap.c("focus.box"));
    else
        rounded_frame(cv, r, ap.c("primary.frame"), workspace);

    Rect arrow{r.right() - kDropArrowW, r.y, kDropArrowW, r.h};
    // Arrow well like a mini button
    Color face = pressed || open ? ap.c("button.dark1") : ap.c("button.face");
    cv.fill(arrow, face);
    cv.vline(arrow.x, arrow.y, arrow.bottom(), ap.c("button.frame"));
    paint_arrow(cv, arrow, false, ap.c("button.label"));

    int tx = r.x + 6;
    int ty = r.y + (r.h - kFontHeight) / 2;
    int max_w = arrow.x - tx - 4;
    // Clip label visually by not drawing past the arrow (simple truncation)
    if (cv.text_width(label) <= max_w)
        cv.text(tx, ty, label, ap.c("text.foreground"));
    else {
        std::string s(label);
        while (s.size() > 1 && cv.text_width((s + "..").c_str()) > max_w) s.pop_back();
        s += "..";
        cv.text(tx, ty, s.c_str(), ap.c("text.foreground"));
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

    // Nested gel sample (compact)
    Rect gel{x, y, std::min(w, 280), 72};
    paint_gel(cv, ap, gel, "Preview Window", true, 0);
    GelLayout gl = gel_layout(gel.x, gel.y, gel.w, gel.h);
    cv.text(gl.client.x + 8, gl.client.y + 4, "Client area", ap.c("primary.label"));
    y = gel.bottom() + 10;

    // Buttons
    lay.btn_ok = {x, y, 90, 24};
    lay.btn_cancel = {x + 100, y, 90, 24};
    lay.btn_press = {x + 200, y, 90, 24};
    paint_button(cv, ap, lay.btn_ok, "OK", st.pressed_btn == 1, true);
    paint_button(cv, ap, lay.btn_cancel, "Cancel", st.pressed_btn == 2, false);
    paint_button(cv, ap, lay.btn_press, "Pressed", true, false);
    y += 30;

    // Field + dropdown on one row when wide enough
    int field_w = std::min(w, 290);
    lay.field = {x, y, field_w, 24};
    paint_field(cv, ap, lay.field, "Edit colour roles...", true, caret_on);
    y += 30;

    static const char *menu_items[] = {"Standard", "Slate", "Custom...", "Disabled"};
    static const unsigned kMenuDisabled = 1u << 3;
    const char *drop_label = menu_items[std::clamp(st.menu_sel, 0, 3)];
    lay.dropdown = {x, y, std::min(w, 200), 24};
    paint_dropdown(cv, ap, lay.dropdown, drop_label, st.dropdown_open,
                   st.pressed_btn == 4);
    y += 30;

    // Slider
    cv.text(x, y, "Slider", ap.c("primary.label"));
    lay.slider = {x + 56, y, std::min(w - 56, 220), 22};
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
