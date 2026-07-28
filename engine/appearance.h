// Appearance Engine — every window speaks this.
// Resolve tokens (art → colour → stock) and paint kit surfaces into a Canvas.
#pragma once
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>

#include "skin.h"
#include "hap_skin.h"

// RAII clip nest — intersects Canvas clip with `r`, restores on scope exit.
struct CanvasClip {
    Canvas &cv;
    Rect prev;
    explicit CanvasClip(Canvas &c, Rect r) : cv(c), prev(c.push_clip(r)) {}
    ~CanvasClip() { cv.pop_clip(prev); }
    CanvasClip(const CanvasClip &) = delete;
    CanvasClip &operator=(const CanvasClip &) = delete;
};

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
// AppearanceEdit usual push-button height for generic kit samples (not Find).
constexpr int kKitButtonH = 20;
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
    std::string skin_dir;
    mutable std::map<std::string, SkinImage> art_cache;
    mutable std::map<std::string, SkinImage> icon_cache;

    Appearance() : skin(stock_skin()), stock(stock_colors()) {}

    void load_art_cache() {
        art_cache.clear();
        icon_cache.clear();
        auto load_into = [&](const ArtRef &ref, std::map<std::string, SkinImage> &dst,
                             const std::string &key) {
            if (ref.path.empty()) return;
            SkinImage img;
            std::string full = join_path(skin_dir, ref.path);
            if (!load_skin_image(full, img)) return;
            if (ref.has_caps) std::memcpy(img.caps, ref.caps, 4);
            if (ref.has_positions) std::memcpy(img.positions, ref.positions, 4);
            if (ref.has_text_color) {
                img.has_text_color = true;
                img.text_color = ref.text_color;
            }
            if (!ref.has_caps && img.caps[0] == 0 && img.caps[1] == 0 &&
                img.caps[2] == 0 && img.caps[3] == 0 && img.w > 1 && img.h > 1) {
                img.caps[0] = uint8_t(img.w / 2);
                img.caps[1] = uint8_t(img.h / 2);
                img.caps[2] = uint8_t(img.w - 1 - img.caps[0]);
                img.caps[3] = uint8_t(img.h - 1 - img.caps[1]);
            }
            dst[key] = std::move(img);
        };
        for (const auto &kv : skin.art) load_into(kv.second, art_cache, kv.first);
        // Icons: SlotMap path → load as SkinImage (no caps; never invent mid caps).
        for (const auto &kv : skin.icons) {
            if (kv.second.empty()) continue;
            ArtRef ref;
            ref.path = kv.second;
            if (ref.path.empty()) continue;
            SkinImage img;
            std::string full = join_path(skin_dir, ref.path);
            if (!load_skin_image(full, img)) continue;
            std::memset(img.caps, 0, 4);
            std::memset(img.positions, 0, 4);
            icon_cache[kv.first] = std::move(img);
        }
    }

    void set_skin(Skin s) {
        skin = std::move(s);
        skin_dir.clear();
        load_art_cache();
    }

    bool load(const std::string &path) {
        // Hap: Sagrado-style live import (colours + image slots → caches).
        auto lower = path;
        for (char &c : lower)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".hap") == 0) {
            Theme theme;
            if (!load_hap(path, theme)) return false;
            return apply_hap_theme(*this, theme, find_completion_pack(path));
        }
        Skin s;
        if (!skin_toml::load(path, s)) return false;
        skin = std::move(s);
        skin_dir = parent_dir(path);
        load_art_cache();
        return true;
    }

    // Write a .sap next to dumped .skimg art/icons (Hap→Sap parity round-trip).
    // Paths in the TOML are relative to the .sap directory. Existing skin.art /
    // skin.icons basenames are preferred when present.
    bool save(const std::string &path) const {
        Skin out = skin;
        std::string dir = parent_dir(path);

        auto basename_of = [](const std::string &p) -> std::string {
            auto slash = p.find_last_of("/\\");
            return slash == std::string::npos ? p : p.substr(slash + 1);
        };
        auto slot_file = [&](const std::string &key, const std::string &existing)
            -> std::string {
            if (!existing.empty()) return basename_of(existing);
            std::string n = key;
            for (char &c : n)
                if (c == '.') c = '_';
            return n + ".skimg";
        };

        for (const auto &kv : art_cache) {
            if (kv.second.empty()) continue;
            std::string existing;
            auto it = out.art.find(kv.first);
            if (it != out.art.end()) existing = it->second.path;
            std::string fname = slot_file(kv.first, existing);
            if (!save_skimg(join_path(dir, fname), kv.second)) return false;
            ArtRef &ref = out.art[kv.first];
            ref.path = fname;
            std::memcpy(ref.caps, kv.second.caps, 4);
            std::memcpy(ref.positions, kv.second.positions, 4);
            ref.has_caps = true;
            ref.has_positions = true;
            ref.has_text_color = kv.second.has_text_color;
            ref.text_color = kv.second.text_color;
        }
        for (const auto &kv : icon_cache) {
            if (kv.second.empty()) continue;
            std::string existing;
            auto it = out.icons.find(kv.first);
            if (it != out.icons.end()) existing = it->second;
            std::string fname = slot_file(kv.first, existing);
            if (!save_skimg(join_path(dir, fname), kv.second)) return false;
            out.icons[kv.first] = fname;
        }
        return skin_toml::save(path, out);
    }

    Color c(const char *role) const { return resolve_color(skin, stock, role); }

    void set_color(const char *role, Color col) { skin.colors[role] = col; }

    // Art present → image; else null (caller uses colour → stock).
    const SkinImage *art(const char *slot) const {
        auto it = art_cache.find(slot);
        if (it == art_cache.end() || it->second.empty()) return nullptr;
        return &it->second;
    }

    const SkinImage *icon(const char *slot) const {
        auto it = icon_cache.find(slot);
        if (it == icon_cache.end() || it->second.empty()) return nullptr;
        return &it->second;
    }

    Color title_label(bool focused) const {
        // Standard colour path uses Window / Window Focus Label. Bitmap themes
        // leave those near-white — KDX then paints Primary Label / Disable Label.
        const char *role = focused ? "window_focus.label" : "window.label";
        Color wl = c(role);
        if (wl.r > 200 && wl.g > 200 && wl.b > 200)
            return focused ? c("primary.label") : c("primary.disable_label");
        return wl;
    }
};

struct GelLayout {
    Rect window;
    Rect client;
    Rect close_box;
    Rect hatch_box; // Window Menu rectangle (w == 0 only if truly absent)
    Rect max_box;
    Rect min_box;
    Rect grip;
    Rect title;
    int title_h = kTitleH;
};

enum class GelStyle {
    Main,   // TextEdit main: close + Window Menu + max + min + grip
    Dialog, // Find-style: close + Window Menu + min (no max/grip)
};

// Title-button place from art Positions (Sagrado chrome_layout).
inline Rect gel_place_title_btn(const SkinImage *img, int win_x, int win_y,
                                int win_w) {
    if (!img) return {0, 0, 0, 0};
    int bx = img->positions[0] > 0 ? img->positions[0]
                                   : win_w - img->positions[2] - img->w;
    int by = img->positions[1] > 1 ? img->positions[1] : 2;
    return {win_x + bx, win_y + by, img->w, img->h};
}

// disabled_mask bits match pressed_box ids: 1=close, 2=menu, 3=max, 4=min.
// Hap contract: Disabled art optional — if absent, hide the button.
inline const SkinImage *gel_title_btn_art(const Appearance *ap, bool focused,
                                          bool disabled, bool pressed,
                                          const char *normal, const char *focus,
                                          const char *hilited,
                                          const char *disabled_slot) {
    if (!ap) return nullptr;
    if (disabled) return ap->art(disabled_slot);
    const SkinImage *img = nullptr;
    if (pressed) img = ap->art(hilited);
    if (!img) img = focused ? ap->art(focus) : ap->art(normal);
    if (!img) img = ap->art(normal);
    return img;
}

// Solid Primary Background colour, then optional tiled Primary Background art.
inline void paint_primary_background(Canvas &cv, const Appearance &ap, Rect r) {
    if (r.w <= 0 || r.h <= 0) return;
    cv.fill(r, ap.c("primary.background"));
    const SkinImage *pat = ap.art("primary.background");
    if (!pat || pat->empty() || pat->w <= 0 || pat->h <= 0) return;
    Rect prev = cv.push_clip(r);
    for (int py = r.y; py < r.bottom(); py += pat->h)
        for (int px = r.x; px < r.right(); px += pat->w)
            cv.blit_image(*pat, px, py);
    cv.pop_clip(prev);
}

// Placement: art frame Positions when present, else Standard metrics.
inline GelLayout gel_layout(int x, int y, int w, int h,
                            GelStyle style = GelStyle::Main,
                            const Appearance *ap = nullptr,
                            bool focused = true,
                            unsigned disabled_mask = 0) {
    GelLayout lay;
    lay.window = {x, y, w, h};
    lay.title = {x, y, w, kTitleH};

    const SkinImage *frame = nullptr;
    if (ap) {
        frame = focused ? ap->art("window.frame.focus") : ap->art("window.frame.normal");
        if (!frame) frame = ap->art("window.frame.normal");
    }

    if (frame) {
        int bl = frame->positions[0], bt = frame->positions[1];
        int br = frame->positions[2], bb = frame->positions[3];
        if (bt <= 0) bt = kTitleH;
        if (bl <= 0) bl = kBorder;
        if (br <= 0) br = kBorder;
        if (bb <= 0) bb = kBorder;
        lay.title_h = bt;
        lay.client = {x + bl, y + bt, w - bl - br, h - bt - bb};
        bool close_dis = (disabled_mask & (1u << 1)) != 0;
        bool menu_dis = (disabled_mask & (1u << 2)) != 0;
        bool max_dis = (disabled_mask & (1u << 3)) != 0;
        bool min_dis = (disabled_mask & (1u << 4)) != 0;
        const SkinImage *close_img =
            gel_title_btn_art(ap, focused, close_dis, false, "window.close.normal",
                              "window.close.focus", "window.close.hilited",
                              "window.close.disabled");
        const SkinImage *min_img =
            gel_title_btn_art(ap, focused, min_dis, false, "window.minimize.normal",
                              "window.minimize.focus", "window.minimize.hilited",
                              "window.minimize.disabled");
        const SkinImage *max_img =
            gel_title_btn_art(ap, focused, max_dis, false, "window.maximize.normal",
                              "window.maximize.focus", "window.maximize.hilited",
                              "window.maximize.disabled");
        const SkinImage *menu_img =
            gel_title_btn_art(ap, focused, menu_dis, false, "window.menu.normal",
                              "window.menu.focus", "window.menu.hilited",
                              "window.menu.disabled");
        // Disabled without art → hide (empty rect). Otherwise place from art.
        lay.close_box = close_dis && !close_img
                            ? Rect{0, 0, 0, 0}
                            : gel_place_title_btn(close_img, x, y, w);
        lay.min_box = min_dis && !min_img ? Rect{0, 0, 0, 0}
                                          : gel_place_title_btn(min_img, x, y, w);
        lay.max_box = max_dis && !max_img ? Rect{0, 0, 0, 0}
                                          : gel_place_title_btn(max_img, x, y, w);
        lay.hatch_box = menu_dis && !menu_img
                            ? Rect{0, 0, 0, 0}
                            : gel_place_title_btn(menu_img, x, y, w);
        // No menu art → Standard rectangle next to Close (every Haxial window),
        // unless the menu button is disabled-and-hidden.
        if (!menu_dis && lay.hatch_box.w <= 0 && lay.close_box.w > 0)
            lay.hatch_box = {lay.close_box.right() + 8, lay.close_box.y, kHatchW,
                            lay.close_box.h};
        else if (!menu_dis && lay.hatch_box.w <= 0)
            lay.hatch_box = {x + 5 + kBtnBox + 8, y + kBtnTop, kHatchW, kBtnBox};
        const SkinImage *resize = focused ? ap->art("window.resize.focus")
                                          : ap->art("window.resize.normal");
        if (!resize) resize = ap->art("window.resize.normal");
        if (resize) {
            int px = resize->positions[2] > 0 ? resize->positions[2] : 1;
            int py = resize->positions[3] > 0 ? resize->positions[3] : 1;
            lay.grip = {x + w - px - resize->w, y + h - py - resize->h, resize->w,
                        resize->h};
        } else {
            lay.grip = {x + w - kGrip, y + h - kGrip, kGrip, kGrip};
        }
    } else {
        lay.title_h = kTitleH;
        lay.client = {x + kBorder, y + kTitleH, w - 2 * kBorder,
                      h - kTitleH - kBorder};
        int by = y + kBtnTop;
        bool close_dis = (disabled_mask & (1u << 1)) != 0;
        bool menu_dis = (disabled_mask & (1u << 2)) != 0;
        bool max_dis = (disabled_mask & (1u << 3)) != 0;
        bool min_dis = (disabled_mask & (1u << 4)) != 0;
        // Colour-path gel has no Disabled art → hide when disabled.
        lay.close_box = close_dis ? Rect{0, 0, 0, 0} : Rect{x + 5, by, kBtnBox, kBtnBox};
        lay.hatch_box = menu_dis ? Rect{0, 0, 0, 0}
                                 : Rect{(lay.close_box.w > 0 ? lay.close_box.right() + 8
                                                             : x + 5 + kBtnBox + 8),
                                        by, kHatchW, kBtnBox};
        lay.min_box = min_dis ? Rect{0, 0, 0, 0}
                              : Rect{x + w - 5 - kBtnBox, by, kBtnBox, kBtnBox};
        lay.max_box = max_dis ? Rect{0, 0, 0, 0}
                              : Rect{(lay.min_box.w > 0 ? lay.min_box.x - 4 - kBtnBox
                                                        : x + w - 5 - 2 * kBtnBox - 4),
                                     by, kBtnBox, kBtnBox};
        lay.grip = {x + w - kGrip, y + h - kGrip, kGrip, kGrip};
    }

    if (style == GelStyle::Dialog) {
        // Dialogs still get the Window Menu rectangle (Haxial); no max / grip.
        lay.max_box = {0, 0, 0, 0};
        lay.grip = {0, 0, 0, 0};
    }
    return lay;
}

// Title-bar box helpers ---------------------------------------------------

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

// Minimize dash — same 2px inset / 10px span as the close glyph.
// Rows 3–4 (not 4–5): sits in the bright face of Hap spheres and optically
// centres in the 14×14 Standard box (a mid dash reads low against the X).
inline void gel_min_glyph(Canvas &cv, Rect r, Color c) {
    static const char *rows[10] = {
        "..........", "..........", "..........",
        "WWWWWWWWWW", "WWWWWWWWWW",
        "..........", "..........", "..........",
        "..........", "..........",
    };
    for (int row = 0; row < 10; ++row)
        for (int col = 0; col < 10; ++col)
            if (rows[row][col] == 'W')
                cv.put(r.x + 2 + col, r.y + 2 + row, pack(c));
}

// Maximize "+" — horizontal bar shares the min dash row; stem matches close.
inline void gel_max_glyph(Canvas &cv, Rect r, Color c) {
    static const char *rows[10] = {
        "....WW....", "....WW....", "....WW....",
        "WWWWWWWWWW", "WWWWWWWWWW",
        "....WW....", "....WW....", "....WW....",
        "....WW....", "....WW....",
    };
    for (int row = 0; row < 10; ++row)
        for (int col = 0; col < 10; ++col)
            if (rows[row][col] == 'W')
                cv.put(r.x + 2 + col, r.y + 2 + row, pack(c));
}

// Standard title-bar box: 1px outline over the title gradient (TextEdit).
// No fill / bevel — glyphs sit cleanly on the slab (Sagrado flat_box).
inline void gel_flat_box(Canvas &cv, Rect r, bool pressed, Color deep, Color frame) {
    if (r.w <= 0 || r.h <= 0) return;
    if (pressed) cv.fill(r, deep);
    cv.frame(r, frame);
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

// Grow box — art resize slot, else Sagrado paint_grip colour path.
inline void paint_gel_grip(Canvas &cv, const Appearance &ap, Rect g, bool focused) {
    if (g.w <= 0 || g.h <= 0) return;
    const SkinImage *img =
        focused ? ap.art("window.resize.focus") : ap.art("window.resize.normal");
    if (!img) img = ap.art("window.resize.normal");
    if (img) {
        cv.nine_slice(*img, g);
        return;
    }
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

// Prefer Hap plate Text Color when set; else colour-table label with
// Haxial/KDX stock-white → Primary Label practice.
inline bool label_near_white(Color c) {
    return c.r > 200 && c.g > 200 && c.b > 200;
}

inline Color plate_text_color(const SkinImage *plate) {
    if (!plate || !plate->has_text_color) return {0, 0, 0};
    return {uint8_t((plate->text_color >> 16) & 0xff),
            uint8_t((plate->text_color >> 8) & 0xff),
            uint8_t(plate->text_color & 0xff)};
}

// When candidate is stock near-white (unauthored), use Primary Label so ink
// stays readable on light bitmap chrome (Aluminum Alloy, etc.).
inline Color readable_label(const Appearance &ap, Color candidate,
                            bool disabled = false) {
    if (disabled) return ap.c("primary.disable_label");
    if (label_near_white(candidate)) return ap.c("primary.label");
    return candidate;
}

inline Color button_label_ink(const Appearance &ap, bool disabled, bool has_art,
                              const SkinImage *plate = nullptr) {
    if (disabled) return ap.c("button_disable.label");
    if (plate && plate->has_text_color) return plate_text_color(plate);
    Color ink = ap.c("button.label");
    if (has_art && label_near_white(ink)) return ap.c("primary.label");
    return ink;
}

// True when a title-button plate is near-uniform (Milk blank spheres) so
// Standard glyphs should be stamped on top. Art that already draws an X/−/+
// has enough luminance variance to skip the overlay.
inline bool skin_image_blankish(const SkinImage &img) {
    if (img.empty() || img.w < 4 || img.h < 4) return true;
    int samples = 0;
    int64_t sum = 0, sum2 = 0;
    int step_x = std::max(1, img.w / 4);
    int step_y = std::max(1, img.h / 4);
    for (int y = 0; y < img.h; y += step_y) {
        for (int x = 0; x < img.w; x += step_x) {
            uint32_t p = img.at(x, y);
            if ((p >> 24) < 128) continue; // ignore transparent
            int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            int lum = (r + g + b) / 3;
            sum += lum;
            sum2 += lum * lum;
            ++samples;
        }
    }
    if (samples < 4) return true;
    double mean = double(sum) / samples;
    double var = double(sum2) / samples - mean * mean;
    return var < 180.0; // blank spheres sit well under this; ink marks don't
}
inline void paint_gel(Canvas &cv, const Appearance &ap, Rect win,
                      const char *title, bool focused, int pressed_box = 0,
                      GelStyle style = GelStyle::Main,
                      unsigned disabled_mask = 0) {
    GelLayout lay = gel_layout(win.x, win.y, win.w, win.h, style, &ap, focused,
                               disabled_mask);

    const SkinImage *frame =
        focused ? ap.art("window.frame.focus") : ap.art("window.frame.normal");
    if (!frame) frame = ap.art("window.frame.normal");
    if (frame) {
        cv.nine_slice(*frame, win);
        Color tc = frame->has_text_color ? plate_text_color(frame)
                                         : ap.title_label(focused);
        int tw = cv.text_width(title);
        cv.text(win.x + (win.w - tw) / 2,
                win.y + (lay.title_h - kFontHeight) / 2, title, tc);
        auto paint_btn = [&](Rect r, int box_id, const char *normal,
                             const char *focus, const char *hilited,
                             const char *disabled_slot,
                             void (*glyph)(Canvas &, Rect, Color)) {
            if (r.w <= 0) return;
            bool disabled = (disabled_mask & (1u << box_id)) != 0;
            bool pressed = !disabled && pressed_box == box_id;
            const SkinImage *img =
                gel_title_btn_art(&ap, focused, disabled, pressed, normal, focus,
                                  hilited, disabled_slot);
            if (!img) return; // disabled without art → already hidden in layout
            if (img->w == r.w && img->h == r.h)
                cv.blit_image(*img, r.x, r.y);
            else
                cv.place(*img, r.x + (r.w - img->w) / 2,
                         r.y + (r.h - img->h) / 2);
            // Blank Milk-style spheres get Standard glyphs; themes that already
            // drew marks (Ashen/BeOS) skip the overlay.
            if (!disabled && glyph && skin_image_blankish(*img)) glyph(cv, r, tc);
        };
        paint_btn(lay.close_box, 1, "window.close.normal", "window.close.focus",
                  "window.close.hilited", "window.close.disabled", gel_close_glyph);
        paint_btn(lay.max_box, 3, "window.maximize.normal", "window.maximize.focus",
                  "window.maximize.hilited", "window.maximize.disabled",
                  gel_max_glyph);
        paint_btn(lay.min_box, 4, "window.minimize.normal", "window.minimize.focus",
                  "window.minimize.hilited", "window.minimize.disabled",
                  gel_min_glyph);
        // Window Menu rectangle — Hap art when present, else striped plate.
        if (lay.hatch_box.w > 0) {
            bool menu_dis = (disabled_mask & (1u << 2)) != 0;
            bool menu_pressed = !menu_dis && pressed_box == 2;
            const SkinImage *mimg =
                gel_title_btn_art(&ap, focused, menu_dis, menu_pressed,
                                  "window.menu.normal", "window.menu.focus",
                                  "window.menu.hilited", "window.menu.disabled");
            if (mimg) {
                if (mimg->w == lay.hatch_box.w && mimg->h == lay.hatch_box.h)
                    cv.blit_image(*mimg, lay.hatch_box.x, lay.hatch_box.y);
                else
                    cv.place(*mimg,
                             lay.hatch_box.x + (lay.hatch_box.w - mimg->w) / 2,
                             lay.hatch_box.y + (lay.hatch_box.h - mimg->h) / 2);
            } else if (!menu_dis) {
                gel_flat_box(cv, lay.hatch_box, menu_pressed, ap.c("window_focus.dark1"),
                             ap.c("window_focus.frame"));
                gel_diagonal_hatch(cv,
                                   {lay.hatch_box.x + 2, lay.hatch_box.y + 2,
                                    lay.hatch_box.w - 4, lay.hatch_box.h - 4},
                                   tc);
            }
        }
        paint_primary_background(cv, ap, lay.client);
        return;
    }

    const char *g = focused ? "window_focus" : "window";
    auto role = [&](const char *suffix) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.%s", g, suffix);
        return ap.c(buf);
    };

    Color bright = role("light1");
    Color body = role("face");
    Color deep = role("dark1");
    Color frame_c = role("frame");
    Color label = role("label");
    if (label_near_white(label)) label = ap.title_label(focused);

    Color grad[18];
    for (int i = 0; i < 18; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.transition.%d", g, i);
        grad[i] = ap.c(buf);
    }

    Rect client = lay.client;
    Rect slab{win.x + 1, win.y + 1, win.w - 2, win.h - 2};
    int title_bottom = win.y + lay.title_h; // first client-row / under-title

    // One continuous chrome slab (title + side + bottom rails). Do not stroke
    // full-width separators through the side rails — that chops the frame into
    // a top "piece" and left/right "pieces".
    cv.fill(slab, body);
    for (int i = 0; i < 18; ++i)
        cv.hline(slab.x, slab.right(), win.y + 2 + i, grad[i]);
    // Under-title shade only across the title span between the side rails.
    if (client.w > 0)
        cv.hline(client.x, client.right(), title_bottom - 2, deep);

    // Outer rim: continuous L highlights / shadows meeting at the corners.
    cv.hline(slab.x, slab.right(), slab.y, bright);
    cv.vline(slab.x, slab.y, slab.bottom(), bright);
    cv.hline(slab.x, slab.right(), slab.bottom() - 1, deep);
    cv.vline(slab.right() - 1, slab.y, slab.bottom(), deep);

    // Single client hole outline (no parallel deep/bright tracks around it).
    cv.frame(win, frame_c);
    cv.frame(client, frame_c);
    paint_primary_background(cv, ap, client);

    // Soft inset: one pixel inside the hole, top/left deep, bottom/right bright.
    if (client.w > 2 && client.h > 2) {
        cv.hline(client.x + 1, client.right() - 1, client.y + 1, deep);
        cv.vline(client.x + 1, client.y + 1, client.bottom() - 1, deep);
        cv.hline(client.x + 1, client.right() - 1, client.bottom() - 2, bright);
        cv.vline(client.right() - 2, client.y + 1, client.bottom() - 1, bright);
    }

    int tw = cv.text_width(title);
    cv.text(win.x + (win.w - tw) / 2,
            win.y + (lay.title_h - kFontHeight) / 2, title, label);

    if (lay.close_box.w > 0) {
        gel_flat_box(cv, lay.close_box, pressed_box == 1, deep, frame_c);
        gel_close_glyph(cv, lay.close_box, label);
    }
    if (lay.hatch_box.w > 0) {
        gel_flat_box(cv, lay.hatch_box, false, deep, frame_c);
        gel_diagonal_hatch(cv,
                           {lay.hatch_box.x + 2, lay.hatch_box.y + 2,
                            lay.hatch_box.w - 4, lay.hatch_box.h - 4},
                           label);
    }
    if (lay.max_box.w > 0) {
        gel_flat_box(cv, lay.max_box, pressed_box == 3, deep, frame_c);
        gel_max_glyph(cv, lay.max_box, label);
    }
    if (lay.min_box.w > 0) {
        gel_flat_box(cv, lay.min_box, pressed_box == 4, deep, frame_c);
        gel_min_glyph(cv, lay.min_box, label);
    }
}

// --- Controls ------------------------------------------------------------

// Simple chamfer frame for plates (button, checkbox, slider thumb).
inline void rounded_frame(Canvas &cv, Rect r, Color frame, Color bg) {
    if (r.w < 6 || r.h < 6) {
        cv.frame(r, frame);
        return;
    }
    cv.hline(r.x + 2, r.right() - 2, r.y, frame);
    cv.hline(r.x + 2, r.right() - 2, r.bottom() - 1, frame);
    cv.vline(r.x, r.y + 2, r.bottom() - 2, frame);
    cv.vline(r.right() - 1, r.y + 2, r.bottom() - 2, frame);
    cv.put(r.x + 1, r.y + 1, pack(frame));
    cv.put(r.right() - 2, r.y + 1, pack(frame));
    cv.put(r.x + 1, r.bottom() - 2, pack(frame));
    cv.put(r.right() - 2, r.bottom() - 2, pack(frame));
    auto punch = [&](int x, int y) { cv.put(x, y, pack(bg)); };
    punch(r.x, r.y); punch(r.x + 1, r.y); punch(r.x, r.y + 1);
    punch(r.right() - 1, r.y); punch(r.right() - 2, r.y); punch(r.right() - 1, r.y + 1);
    punch(r.x, r.bottom() - 1); punch(r.x + 1, r.bottom() - 1); punch(r.x, r.bottom() - 2);
    punch(r.right() - 1, r.bottom() - 1); punch(r.right() - 2, r.bottom() - 1);
    punch(r.right() - 1, r.bottom() - 2);
}

// Sagrado Standard colour-path button face (Find-faithful bevel plate).
// Pressed uses the `button_hilite.*` colour group (Haxial Hilite = press).
inline void paint_button_face_color(Canvas &cv, const Appearance &ap, Rect r,
                                    bool pressed, bool disabled = false) {
    Color workspace = ap.c("primary.background");
    const char *grp = disabled ? "button_disable"
                               : (pressed ? "button_hilite" : "button");
    auto bc = [&](const char *s) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s.%s", grp, s);
        return ap.c(buf);
    };
    Color face = bc("face");
    Color l2 = bc("light2"), l1 = bc("light1");
    Color d1 = bc("dark1"), d2 = bc("dark2");
    Color frame = bc("frame");
    cv.fill(r, face);
    rounded_frame(cv, r, frame, workspace);
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, l2);
    cv.hline(r.x + 2, r.right() - 2, r.y + 2, l1);
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, l2);
    cv.vline(r.x + 2, r.y + 2, r.bottom() - 2, l1);
    cv.hline(r.x + 2, r.right() - 2, r.bottom() - 3, d1);
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, d2);
    cv.vline(r.right() - 3, r.y + 2, r.bottom() - 2, d1);
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, d2);
}

inline void paint_button_face(Canvas &cv, const Appearance &ap, Rect r,
                              bool pressed, bool disabled = false) {
    const char *slot = disabled ? "button.disabled"
                                : (pressed ? "button.hilited" : "button.normal");
    const SkinImage *img = ap.art(slot);
    if (!img && pressed) img = ap.art("button.normal");
    if (img) {
        cv.nine_slice(*img, r);
        return;
    }
    paint_button_face_color(cv, ap, r, pressed, disabled);
}

// Default ring (colour) — Focus Box preferred when Default art absent.
inline void paint_default_ring_color(Canvas &cv, const Appearance &ap, Rect r) {
    Color workspace = ap.c("primary.background");
    rounded_frame(cv, r, ap.c("default_button.frame"), workspace);
    cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ap.c("default_button.light"));
    cv.frame({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, ap.c("default_button.face"));
}

inline void paint_button(Canvas &cv, const Appearance &ap, Rect r,
                         const char *label, bool pressed, bool is_default,
                         bool disabled = false) {
    if (disabled) pressed = false;
    bool used_art = false;
    const SkinImage *plate = nullptr;
    if (is_default) {
        const char *dslot = disabled ? "default_button.disabled"
                                     : (pressed ? "default_button.hilited"
                                                : "default_button.normal");
        const SkinImage *dimg = ap.art(dslot);
        if (!dimg && !disabled) dimg = ap.art("default_button.normal");
        if (dimg) {
            cv.nine_slice(*dimg, r);
            used_art = true;
            plate = dimg;
        } else {
            paint_default_ring_color(cv, ap, r);
            r = {r.x + kDefaultButtonPad, r.y + kDefaultButtonPad,
                 r.w - 2 * kDefaultButtonPad, r.h - 2 * kDefaultButtonPad};
            paint_button_face(cv, ap, r, pressed, disabled);
            plate = ap.art(disabled ? "button.disabled"
                                    : (pressed ? "button.hilited" : "button.normal"));
            used_art = plate != nullptr;
        }
    } else {
        const char *slot = disabled ? "button.disabled"
                                    : (pressed ? "button.hilited" : "button.normal");
        plate = ap.art(slot);
        if (!plate && pressed) plate = ap.art("button.normal");
        used_art = plate != nullptr;
        paint_button_face(cv, ap, r, pressed, disabled);
    }
    int tw = cv.text_width(label);
    int off = pressed ? 1 : 0;
    Color ink = button_label_ink(ap, disabled, used_art, plate);
    cv.text(r.x + (r.w - tw) / 2 + off, r.y + (r.h - kFontHeight) / 2 + off,
            label, ink);
}

inline void paint_field(Canvas &cv, const Appearance &ap, Rect r,
                        const char *text, bool focused, bool caret_on) {
    cv.fill(r, ap.c("text.background"));
    if (focused) {
        const SkinImage *fb = ap.art("focus_box.hilited");
        if (!fb) fb = ap.art("focus_box.normal");
        if (fb) {
            cv.nine_slice(*fb, r);
        } else {
            cv.frame(r, ap.c("focus.box"));
            cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ap.c("focus.box"));
        }
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
    GelLayout lay = gel_layout(x, y, w, h, GelStyle::Dialog, &ap, true);
    paint_gel(cv, ap, win, "Find and Replace", true, 0, GelStyle::Dialog);

    CanvasClip clip(cv, lay.client);
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
    if (by < cl.y + 4) by = cl.y + 4;
    Rect b_all{lx, by, 96, kButtonH};
    Rect b_repl{b_all.right() + 8, by, 76, kButtonH};
    Rect b_find{cl.right() - 14 - 84, by, 84, kDefaultButtonH};
    // Keep default Find bottom inside the client (outer hangs 2px vs regular).
    if (b_find.bottom() > cl.bottom() - 2)
        b_find.y = cl.bottom() - 2 - b_find.h;
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
                                const char *label, bool hilite,
                                bool disabled = false) {
    if (disabled) hilite = false;
    const char *slot = disabled ? "column_header.disabled"
                                : (hilite ? "column_header.hilited"
                                          : "column_header.normal");
    const SkinImage *img = ap.art(slot);
    if (!img && disabled) img = ap.art("column_header.normal");
    if (!img && hilite) img = ap.art("column_header.normal");

    Color ink;
    if (disabled) {
        ink = ap.c("primary.disable_label");
    } else if (img && img->has_text_color) {
        ink = plate_text_color(img);
    } else if (hilite) {
        // Hilite Label is often stock-white on light header art — same KDX
        // Primary Label remap as the normal header path.
        ink = readable_label(ap, ap.c("column_header.hilite_label"));
    } else {
        ink = readable_label(ap, ap.c("column_header.label"));
    }

    if (img) {
        cv.nine_slice(*img, r);
        cv.text(r.x + 6, r.y + (r.h - kFontHeight) / 2, label, ink);
        return;
    }

    Color face = hilite ? ap.c("column_header.hilite") : ap.c("column_header.face");
    Color light = hilite ? ap.c("column_header.hilite_light") : ap.c("column_header.light");
    Color dark = hilite ? ap.c("column_header.hilite_dark") : ap.c("column_header.dark");
    if (disabled) {
        face = ap.c("primary.dark");
        light = ap.c("primary.dark");
        dark = ap.c("primary.frame");
    }
    cv.fill(r, face);
    cv.frame(r, ap.c("column_header.frame"));
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, light);
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, light);
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, dark);
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, dark);
    cv.text(r.x + 6, r.y + (r.h - kFontHeight) / 2, label, ink);
}

inline Color file_label_color(const Appearance &ap, int index) {
    char key[24];
    std::snprintf(key, sizeof(key), "file_label.%d", std::clamp(index, 0, 15));
    Color ink = ap.c(key);
    // Stock-white file labels vanish on white list backgrounds (Aluminum).
    if (label_near_white(ink) && label_near_white(ap.c("list.background")))
        return readable_label(ap, ink);
    return ink;
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
    Rect bar{}, up{}, down{}, track{}, thumb{};
    // Double-arrows: each end is a pair (KDX / Hap). Single: only first_*.
    Rect first_start{}, first_end{};     // start cluster (top / left)
    Rect second_start{}, second_end{};   // end cluster (bottom / right)
    bool double_arrows = false;
};

// Split an end zone into the outer + inner arrow plates (Hap double caps).
inline void scroll_split_end(Rect zone, bool vertical, Rect &outer, Rect &inner) {
    if (vertical) {
        int half = zone.h / 2;
        outer = {zone.x, zone.y, zone.w, half};
        inner = {zone.x, zone.y + half, zone.w, zone.h - half};
    } else {
        int half = zone.w / 2;
        outer = {zone.x, zone.y, half, zone.h};
        inner = {zone.x + half, zone.y, zone.w - half, zone.h};
    }
}

// Hap double-arrows: first_* = start cluster (top/left), second_* = end cluster.
enum class ScrollArrowHot : int {
    None = 0,
    FirstStart = 1,
    FirstEnd = 2,
    SecondStart = 3,
    SecondEnd = 4
};

// Resolve V travel insets: art Positions (T/B), else caps, else square arrow plates.
// `single` selects single-arrows art when present.
inline void scrollbar_v_insets(const Appearance &ap, int bar_w, int &top,
                               int &bot, bool single = false) {
    const SkinImage *arrows =
        single ? ap.art("scrollbar.v.single_arrows") : ap.art("scrollbar.v.double_arrows");
    if (!arrows && single) arrows = ap.art("scrollbar.v.double_arrows");
    if (arrows) {
        top = arrows->positions[1];
        bot = arrows->positions[3];
        if (top <= 0) top = arrows->caps[1];
        if (bot <= 0) bot = arrows->caps[3];
    } else {
        top = bot = 0;
    }
    if (top <= 0) top = single ? bar_w : bar_w * 2;
    if (bot <= 0) bot = single ? bar_w : bar_w * 2;
}

inline void scrollbar_h_insets(const Appearance &ap, int bar_h, int &left,
                               int &right, bool single = false) {
    const SkinImage *arrows =
        single ? ap.art("scrollbar.h.single_arrows") : ap.art("scrollbar.h.double_arrows");
    if (!arrows && single) arrows = ap.art("scrollbar.h.double_arrows");
    if (arrows) {
        left = arrows->positions[0];
        right = arrows->positions[2];
        if (left <= 0) left = arrows->caps[0];
        if (right <= 0) right = arrows->caps[2];
    } else {
        left = right = 0;
    }
    if (left <= 0) left = single ? bar_h : bar_h * 2;
    if (right <= 0) right = single ? bar_h : bar_h * 2;
}

// Min thumb length: stretch-template floor = along-axis caps + 1px mid
// (Milk V: 23+23+1 = 47). Do not use full bitmap height as a rigid brick —
// short Ashen indicators (h=5) correctly floor at bar thickness.
inline int scrollbar_min_thumb_v(const Appearance &ap, int bar_w) {
    const SkinImage *ind = ap.art("scrollbar.v.indicator.normal");
    if (ind) {
        int c = int(ind->caps[1]) + int(ind->caps[3]) + 1;
        if (c > 1) return std::max(bar_w, c);
        if (ind->h > 0) return std::max(bar_w, std::min(ind->h, bar_w * 3));
    }
    return bar_w;
}

inline int scrollbar_min_thumb_h(const Appearance &ap, int bar_h) {
    const SkinImage *ind = ap.art("scrollbar.h.indicator.normal");
    if (ind) {
        int c = int(ind->caps[0]) + int(ind->caps[2]) + 1;
        if (c > 1) return std::max(bar_h, c);
        if (ind->w > 0) return std::max(bar_h, std::min(ind->w, bar_h * 3));
    }
    return bar_h;
}

inline ScrollLayout scroll_layout(Rect bar, int value, int max_value, int page,
                                  int inset_top = -1, int inset_bot = -1,
                                  int min_thumb = -1, bool double_arrows = true) {
    ScrollLayout s;
    s.bar = bar;
    s.double_arrows = double_arrows;
    int top = inset_top >= 0 ? inset_top : (double_arrows ? bar.w * 2 : bar.w);
    int bot = inset_bot >= 0 ? inset_bot : (double_arrows ? bar.w * 2 : bar.w);
    if (top + bot > bar.h) {
        top = bar.h / 2;
        bot = bar.h - top;
    }
    s.up = {bar.x, bar.y, bar.w, top};
    s.down = {bar.x, bar.bottom() - bot, bar.w, bot};
    if (double_arrows) {
        scroll_split_end(s.up, true, s.first_start, s.first_end);
        // End cluster: inner (toward track) then outer — second_start = up, second_end = down.
        int half = bot / 2;
        s.second_start = {bar.x, s.down.y, bar.w, half};
        s.second_end = {bar.x, s.down.y + half, bar.w, bot - half};
    } else {
        s.first_start = s.up;
        s.first_end = s.down;
    }
    int track_h = s.down.y - s.up.bottom();
    if (track_h < 0) track_h = 0;
    s.track = {bar.x, s.up.bottom(), bar.w, track_h};
    if (page < 1) page = 1;
    if (max_value < 0) max_value = 0;
    int floor = min_thumb > 0 ? min_thumb : bar.w;
    int thumb_h = max_value == 0
                      ? s.track.h
                      : std::max(floor, s.track.h * page / (max_value + page));
    if (thumb_h > s.track.h) thumb_h = s.track.h;
    int travel = s.track.h - thumb_h;
    int ty = s.track.y;
    if (max_value > 0 && travel > 0)
        ty += travel * std::clamp(value, 0, max_value) / max_value;
    s.thumb = {bar.x, ty, bar.w, thumb_h};
    return s;
}

inline ScrollLayout scroll_layout(const Appearance &ap, Rect bar, int value,
                                  int max_value, int page, bool single = false) {
    int top = 0, bot = 0;
    scrollbar_v_insets(ap, bar.w, top, bot, single);
    return scroll_layout(bar, value, max_value, page, top, bot,
                         scrollbar_min_thumb_v(ap, bar.w), !single);
}

inline ScrollLayout scroll_layout_h(Rect bar, int value, int max_value, int page,
                                    int inset_left = -1, int inset_right = -1,
                                    int min_thumb = -1, bool double_arrows = true) {
    ScrollLayout s;
    s.bar = bar;
    s.double_arrows = double_arrows;
    int left = inset_left >= 0 ? inset_left : (double_arrows ? bar.h * 2 : bar.h);
    int right = inset_right >= 0 ? inset_right : (double_arrows ? bar.h * 2 : bar.h);
    if (left + right > bar.w) {
        left = bar.w / 2;
        right = bar.w - left;
    }
    s.up = {bar.x, bar.y, left, bar.h}; // left cluster
    s.down = {bar.right() - right, bar.y, right, bar.h}; // right cluster
    if (double_arrows) {
        scroll_split_end(s.up, false, s.first_start, s.first_end);
        int half = right / 2;
        s.second_start = {s.down.x, bar.y, half, bar.h};
        s.second_end = {s.down.x + half, bar.y, right - half, bar.h};
    } else {
        s.first_start = s.up;
        s.first_end = s.down;
    }
    int track_w = s.down.x - s.up.right();
    if (track_w < 0) track_w = 0;
    s.track = {s.up.right(), bar.y, track_w, bar.h};
    if (page < 1) page = 1;
    if (max_value < 0) max_value = 0;
    int floor = min_thumb > 0 ? min_thumb : bar.h;
    int thumb_w = max_value == 0
                      ? s.track.w
                      : std::max(floor, s.track.w * page / (max_value + page));
    if (thumb_w > s.track.w) thumb_w = s.track.w;
    int travel = s.track.w - thumb_w;
    int tx = s.track.x;
    if (max_value > 0 && travel > 0)
        tx += travel * std::clamp(value, 0, max_value) / max_value;
    s.thumb = {tx, bar.y, thumb_w, bar.h};
    return s;
}

inline ScrollLayout scroll_layout_h(const Appearance &ap, Rect bar, int value,
                                    int max_value, int page,
                                    bool single = false) {
    int left = 0, right = 0;
    scrollbar_h_insets(ap, bar.h, left, right, single);
    return scroll_layout_h(bar, value, max_value, page, left, right,
                           scrollbar_min_thumb_h(ap, bar.h), !single);
}

// Which arrow plate was hit (Hap first_* = start cluster, second_* = end).
inline ScrollArrowHot scroll_arrow_hit(const ScrollLayout &s, int x, int y) {
    if (s.first_start.w > 0 && s.first_start.contains(x, y))
        return ScrollArrowHot::FirstStart;
    if (s.first_end.w > 0 && s.first_end.contains(x, y))
        return ScrollArrowHot::FirstEnd;
    if (s.double_arrows && s.second_start.w > 0 && s.second_start.contains(x, y))
        return ScrollArrowHot::SecondStart;
    if (s.double_arrows && s.second_end.w > 0 && s.second_end.contains(x, y))
        return ScrollArrowHot::SecondEnd;
    return ScrollArrowHot::None;
}

inline int scroll_arrow_dir(ScrollArrowHot hot) {
    switch (hot) {
    case ScrollArrowHot::FirstStart:
    case ScrollArrowHot::SecondStart:
        return -1;
    case ScrollArrowHot::FirstEnd:
    case ScrollArrowHot::SecondEnd:
        return 1;
    default:
        return 0;
    }
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

inline void paint_arrow_h(Canvas &cv, Rect r, bool left, Color ink) {
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    if (left) {
        int L = cx - 2;
        for (int i = 0; i < 4; ++i)
            cv.vline(L + i, cy - i, cy + i + 1, ink);
    } else {
        int R = cx + 2;
        for (int i = 0; i < 4; ++i)
            cv.vline(R - i, cy - i, cy + i + 1, ink);
    }
}

inline void paint_scrollbar_grips(Canvas &cv, const SkinImage *grips, Rect thumb) {
    if (!grips || grips->empty()) return;
    // Skip donor grips that cannot fit the thumb (Milk Hap has none; Ashen is 15×6).
    if (grips->w > thumb.w || grips->h > thumb.h) return;
    int gx = thumb.x + (thumb.w - grips->w) / 2;
    int gy = thumb.y + (thumb.h - grips->h) / 2;
    cv.blit_image(*grips, gx, gy);
}

// Hap arrow-hilite stamp. Slot name picks the edge; Positions are offsets
// (0 is valid — do not treat zero as “unset”).
// second_right / second_down often store the far-edge inset in Left/Top.
inline void paint_scroll_arrow_overlay(Canvas &cv, const SkinImage *ov, Rect bar,
                                       const char *slot) {
    if (!ov || ov->empty() || !slot) return;
    const bool vert = std::strstr(slot, ".v.") != nullptr;
    int x = 0, y = 0;
    auto has = [](const char *n, const char *tok) {
        return std::strstr(n, tok) != nullptr;
    };

    if (vert) {
        x = bar.x + (bar.w - ov->w) / 2;
        if (ov->positions[0]) x = bar.x + ov->positions[0];
        else if (ov->positions[2]) x = bar.right() - ov->w - ov->positions[2];

        if (has(slot, "second_down")) {
            // Top field = inset from bottom to overlay top (Ashen/BeOS); else Bottom.
            if (ov->positions[1])
                y = bar.bottom() - ov->positions[1];
            else
                y = bar.bottom() - ov->h - ov->positions[3];
        } else if (has(slot, "second_up")) {
            y = bar.bottom() - ov->h - ov->positions[3];
        } else if (has(slot, "single_down")) {
            y = bar.bottom() - ov->h - ov->positions[3];
        } else if (has(slot, "first_down")) {
            if (ov->positions[3])
                y = bar.bottom() - ov->h - ov->positions[3];
            else if (ov->positions[1])
                y = bar.y + ov->positions[1];
            else
                y = bar.y + ov->h; // right plate of top double-arrow group
        } else {
            // first_up / single_up
            y = bar.y + ov->positions[1];
        }
    } else {
        y = bar.y + (bar.h - ov->h) / 2;
        if (ov->positions[1]) y = bar.y + ov->positions[1];
        else if (ov->positions[3]) y = bar.bottom() - ov->h - ov->positions[3];

        if (has(slot, "second_right")) {
            if (ov->positions[0])
                x = bar.right() - ov->positions[0];
            else
                x = bar.right() - ov->w - ov->positions[2];
        } else if (has(slot, "second_left")) {
            x = bar.right() - ov->w - ov->positions[2];
        } else if (has(slot, "single_right")) {
            x = bar.right() - ov->w - ov->positions[2];
        } else if (has(slot, "first_right")) {
            // Authored Right = far-edge offset; else Left; else left-cluster 2nd plate.
            if (ov->positions[2])
                x = bar.right() - ov->w - ov->positions[2];
            else if (ov->positions[0])
                x = bar.x + ov->positions[0];
            else
                x = bar.x + ov->w;
        } else {
            // first_left / single_left
            x = bar.x + ov->positions[0];
        }
    }
    cv.blit_image(*ov, x, y);
}

inline void paint_scrollbar_color_v(Canvas &cv, const Appearance &ap,
                                    const ScrollLayout &s, bool hilite_thumb) {
    cv.fill(s.track, ap.c("scrollbar.track"));
    cv.hline(s.track.x, s.track.right(), s.track.y, ap.c("scrollbar.track_light2"));
    cv.vline(s.track.x, s.track.y, s.track.bottom(), ap.c("scrollbar.track_light1"));
    cv.hline(s.track.x, s.track.right(), s.track.bottom() - 1, ap.c("scrollbar.track_dark2"));
    cv.vline(s.track.right() - 1, s.track.y, s.track.bottom(), ap.c("scrollbar.track_dark1"));

    auto paint_btn = [&](Rect r) {
        if (r.w <= 0 || r.h <= 0) return;
        cv.fill(r, ap.c("scrollbar.face"));
        cv.frame(r, ap.c("scrollbar.frame"));
        cv.hline(r.x + 1, r.right() - 1, r.y + 1, ap.c("scrollbar.light"));
        cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, ap.c("scrollbar.light"));
        cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, ap.c("scrollbar.dark"));
        cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, ap.c("scrollbar.dark"));
    };
    Color ink = ap.c("scrollbar.label");
    if (s.double_arrows) {
        paint_btn(s.first_start);
        paint_btn(s.first_end);
        paint_btn(s.second_start);
        paint_btn(s.second_end);
        paint_arrow(cv, s.first_start, true, ink);
        paint_arrow(cv, s.first_end, false, ink);
        paint_arrow(cv, s.second_start, true, ink);
        paint_arrow(cv, s.second_end, false, ink);
    } else {
        paint_btn(s.first_start);
        paint_btn(s.first_end);
        paint_arrow(cv, s.first_start, true, ink);
        paint_arrow(cv, s.first_end, false, ink);
    }

    Color il = hilite_thumb ? ap.c("scrollbar.indicator_hilite_light")
                            : ap.c("scrollbar.indicator_light");
    Color ind = hilite_thumb ? ap.c("scrollbar.indicator_hilite")
                             : ap.c("scrollbar.indicator");
    Color id = hilite_thumb ? ap.c("scrollbar.indicator_hilite_dark")
                            : ap.c("scrollbar.indicator_dark");
    cv.fill(s.thumb, ind);
    cv.frame(s.thumb, ap.c("scrollbar.frame"));
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.y + 1, il);
    cv.vline(s.thumb.x + 1, s.thumb.y + 1, s.thumb.bottom() - 1, il);
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.bottom() - 2, id);
    cv.vline(s.thumb.right() - 2, s.thumb.y + 1, s.thumb.bottom() - 1, id);
}

inline void paint_scrollbar_color_h(Canvas &cv, const Appearance &ap,
                                    const ScrollLayout &s, bool hilite_thumb) {
    cv.fill(s.track, ap.c("scrollbar.track"));
    cv.vline(s.track.x, s.track.y, s.track.bottom(), ap.c("scrollbar.track_light2"));
    cv.hline(s.track.x, s.track.right(), s.track.y, ap.c("scrollbar.track_light1"));
    cv.vline(s.track.right() - 1, s.track.y, s.track.bottom(), ap.c("scrollbar.track_dark2"));
    cv.hline(s.track.x, s.track.right(), s.track.bottom() - 1, ap.c("scrollbar.track_dark1"));

    auto paint_btn = [&](Rect r) {
        if (r.w <= 0 || r.h <= 0) return;
        cv.fill(r, ap.c("scrollbar.face"));
        cv.frame(r, ap.c("scrollbar.frame"));
        cv.hline(r.x + 1, r.right() - 1, r.y + 1, ap.c("scrollbar.light"));
        cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, ap.c("scrollbar.light"));
        cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, ap.c("scrollbar.dark"));
        cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, ap.c("scrollbar.dark"));
    };
    Color ink = ap.c("scrollbar.label");
    if (s.double_arrows) {
        paint_btn(s.first_start);
        paint_btn(s.first_end);
        paint_btn(s.second_start);
        paint_btn(s.second_end);
        paint_arrow_h(cv, s.first_start, true, ink);
        paint_arrow_h(cv, s.first_end, false, ink);
        paint_arrow_h(cv, s.second_start, true, ink);
        paint_arrow_h(cv, s.second_end, false, ink);
    } else {
        paint_btn(s.first_start);
        paint_btn(s.first_end);
        paint_arrow_h(cv, s.first_start, true, ink);
        paint_arrow_h(cv, s.first_end, false, ink);
    }

    Color il = hilite_thumb ? ap.c("scrollbar.indicator_hilite_light")
                            : ap.c("scrollbar.indicator_light");
    Color ind = hilite_thumb ? ap.c("scrollbar.indicator_hilite")
                             : ap.c("scrollbar.indicator");
    Color id = hilite_thumb ? ap.c("scrollbar.indicator_hilite_dark")
                            : ap.c("scrollbar.indicator_dark");
    cv.fill(s.thumb, ind);
    cv.frame(s.thumb, ap.c("scrollbar.frame"));
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.y + 1, il);
    cv.vline(s.thumb.x + 1, s.thumb.y + 1, s.thumb.bottom() - 1, il);
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.bottom() - 2, id);
    cv.vline(s.thumb.right() - 2, s.thumb.y + 1, s.thumb.bottom() - 1, id);
}

// Hap: scrollbar cross-axis is exactly 16. If art is thinner (Milk 15), centre
// the strip in the layout trough — do not stretch cross-axis.
inline Rect scrollbar_art_strip(Rect bar, bool vertical, int art_thickness) {
    if (art_thickness <= 0) return bar;
    if (vertical) {
        if (art_thickness >= bar.w) return bar;
        return {bar.x + (bar.w - art_thickness) / 2, bar.y, art_thickness, bar.h};
    }
    if (art_thickness >= bar.h) return bar;
    return {bar.x, bar.y + (bar.h - art_thickness) / 2, bar.w, art_thickness};
}

inline Rect scrollbar_thumb_on_strip(Rect thumb, Rect strip, bool vertical) {
    if (vertical)
        return {strip.x, thumb.y, strip.w, thumb.h};
    return {thumb.x, strip.y, thumb.w, strip.h};
}

// Centre the indicator in the arrow strip cross-axis (Milk 14-wide art in a
// 15-wide strip) — never stretch cross-axis via nine_slice.
inline Rect scrollbar_indicator_dest(Rect thumb_on_strip, bool vertical,
                                     int art_thickness) {
    if (art_thickness <= 0) return thumb_on_strip;
    if (vertical) {
        if (art_thickness >= thumb_on_strip.w) return thumb_on_strip;
        return {thumb_on_strip.x + (thumb_on_strip.w - art_thickness) / 2,
                thumb_on_strip.y, art_thickness, thumb_on_strip.h};
    }
    if (art_thickness >= thumb_on_strip.h) return thumb_on_strip;
    return {thumb_on_strip.x,
            thumb_on_strip.y + (thumb_on_strip.h - art_thickness) / 2,
            thumb_on_strip.w, art_thickness};
}

// Art-first vertical scrollbar (double/single arrows + indicator + grips + overlays).
// `arrow_hot` stamps pressed-arrow hilite overlays (Haxial Positions = offsets).
inline void paint_scrollbar(Canvas &cv, const Appearance &ap, Rect bar,
                            int value, int max_value, int page, bool hilite_thumb,
                            bool single = false, bool disabled = false,
                            ScrollArrowHot arrow_hot = ScrollArrowHot::None) {
    // Too-small: shorter than single-arrows caps → stamp too_small art.
    const SkinImage *single_art = ap.art("scrollbar.v.single_arrows");
    int min_h = single_art ? (single_art->caps[1] + single_art->caps[3])
                           : (2 * bar.w);
    if (min_h < 2) min_h = 2 * bar.w;
    if (bar.h < min_h) {
        const SkinImage *tiny = ap.art("scrollbar.v.too_small");
        if (tiny) {
            Rect strip = scrollbar_art_strip(bar, true, tiny->w);
            cv.nine_slice(*tiny, strip);
            return;
        }
    }

    if (disabled) {
        const SkinImage *dis = ap.art("scrollbar.v.disabled");
        if (dis) {
            Rect strip = scrollbar_art_strip(bar, true, dis->w);
            cv.nine_slice(*dis, strip);
            return;
        }
    }

    ScrollLayout s = scroll_layout(ap, bar, value, max_value, page, single);

    const char *arrow_slot =
        single ? "scrollbar.v.single_arrows" : "scrollbar.v.double_arrows";
    const SkinImage *arrows = ap.art(arrow_slot);
    if (!arrows && single) arrows = ap.art("scrollbar.v.double_arrows");
    if (arrows) {
        Rect strip = scrollbar_art_strip(bar, true, arrows->w);
        cv.nine_slice(*arrows, strip);
        if (!disabled) {
            Rect th = scrollbar_thumb_on_strip(s.thumb, strip, true);
            const char *islot = hilite_thumb ? "scrollbar.v.indicator.hilited"
                                             : "scrollbar.v.indicator.normal";
            const SkinImage *ind = ap.art(islot);
            if (!ind) ind = ap.art("scrollbar.v.indicator.normal");
            if (ind && th.h > 0) {
                Rect idest = scrollbar_indicator_dest(th, true, ind->w);
                cv.nine_slice(*ind, idest);
                th = idest; // grips centre on the painted indicator
            } else if (th.h > 0) {
                Color face = hilite_thumb ? ap.c("scrollbar.indicator_hilite")
                                          : ap.c("scrollbar.indicator");
                cv.fill(th, face);
            }
            const char *gslot = hilite_thumb ? "scrollbar.v.grips.hilited"
                                             : "scrollbar.v.grips.normal";
            const SkinImage *grips = ap.art(gslot);
            if (!grips) grips = ap.art("scrollbar.v.grips.normal");
            paint_scrollbar_grips(cv, grips, th);

            const char *ov_slot = nullptr;
            if (single) {
                if (arrow_hot == ScrollArrowHot::FirstStart)
                    ov_slot = "scrollbar.v.arrow_hilite.single_up";
                else if (arrow_hot == ScrollArrowHot::FirstEnd)
                    ov_slot = "scrollbar.v.arrow_hilite.single_down";
            } else {
                switch (arrow_hot) {
                case ScrollArrowHot::FirstStart:
                    ov_slot = "scrollbar.v.arrow_hilite.first_up";
                    break;
                case ScrollArrowHot::FirstEnd:
                    ov_slot = "scrollbar.v.arrow_hilite.first_down";
                    break;
                case ScrollArrowHot::SecondStart:
                    ov_slot = "scrollbar.v.arrow_hilite.second_up";
                    break;
                case ScrollArrowHot::SecondEnd:
                    ov_slot = "scrollbar.v.arrow_hilite.second_down";
                    break;
                default:
                    break;
                }
            }
            if (ov_slot) paint_scroll_arrow_overlay(cv, ap.art(ov_slot), strip, ov_slot);
        }
        return;
    }

    paint_scrollbar_color_v(cv, ap, s, hilite_thumb);
}

// Art-first horizontal scrollbar.
inline void paint_scrollbar_h(Canvas &cv, const Appearance &ap, Rect bar,
                              int value, int max_value, int page,
                              bool hilite_thumb, bool single = false,
                              bool disabled = false,
                              ScrollArrowHot arrow_hot = ScrollArrowHot::None) {
    const SkinImage *single_art = ap.art("scrollbar.h.single_arrows");
    int min_w = single_art ? (single_art->caps[0] + single_art->caps[2])
                           : (2 * bar.h);
    if (min_w < 2) min_w = 2 * bar.h;
    if (bar.w < min_w) {
        const SkinImage *tiny = ap.art("scrollbar.h.too_small");
        if (tiny) {
            Rect strip = scrollbar_art_strip(bar, false, tiny->h);
            cv.nine_slice(*tiny, strip);
            return;
        }
    }

    if (disabled) {
        const SkinImage *dis = ap.art("scrollbar.h.disabled");
        if (dis) {
            Rect strip = scrollbar_art_strip(bar, false, dis->h);
            cv.nine_slice(*dis, strip);
            return;
        }
    }

    ScrollLayout s = scroll_layout_h(ap, bar, value, max_value, page, single);

    const char *arrow_slot =
        single ? "scrollbar.h.single_arrows" : "scrollbar.h.double_arrows";
    const SkinImage *arrows = ap.art(arrow_slot);
    if (!arrows && single) arrows = ap.art("scrollbar.h.double_arrows");
    if (arrows) {
        Rect strip = scrollbar_art_strip(bar, false, arrows->h);
        cv.nine_slice(*arrows, strip);
        if (!disabled) {
            Rect th = scrollbar_thumb_on_strip(s.thumb, strip, false);
            const char *islot = hilite_thumb ? "scrollbar.h.indicator.hilited"
                                             : "scrollbar.h.indicator.normal";
            const SkinImage *ind = ap.art(islot);
            if (!ind) ind = ap.art("scrollbar.h.indicator.normal");
            if (ind && th.w > 0) {
                Rect idest = scrollbar_indicator_dest(th, false, ind->h);
                cv.nine_slice(*ind, idest);
                th = idest;
            } else if (th.w > 0) {
                Color face = hilite_thumb ? ap.c("scrollbar.indicator_hilite")
                                          : ap.c("scrollbar.indicator");
                cv.fill(th, face);
            }
            const char *gslot = hilite_thumb ? "scrollbar.h.grips.hilited"
                                             : "scrollbar.h.grips.normal";
            const SkinImage *grips = ap.art(gslot);
            if (!grips) grips = ap.art("scrollbar.h.grips.normal");
            paint_scrollbar_grips(cv, grips, th);

            const char *ov_slot = nullptr;
            if (single) {
                if (arrow_hot == ScrollArrowHot::FirstStart)
                    ov_slot = "scrollbar.h.arrow_hilite.single_left";
                else if (arrow_hot == ScrollArrowHot::FirstEnd)
                    ov_slot = "scrollbar.h.arrow_hilite.single_right";
            } else {
                switch (arrow_hot) {
                case ScrollArrowHot::FirstStart:
                    ov_slot = "scrollbar.h.arrow_hilite.first_left";
                    break;
                case ScrollArrowHot::FirstEnd:
                    ov_slot = "scrollbar.h.arrow_hilite.first_right";
                    break;
                case ScrollArrowHot::SecondStart:
                    ov_slot = "scrollbar.h.arrow_hilite.second_left";
                    break;
                case ScrollArrowHot::SecondEnd:
                    ov_slot = "scrollbar.h.arrow_hilite.second_right";
                    break;
                default:
                    break;
                }
            }
            if (ov_slot) paint_scroll_arrow_overlay(cv, ap.art(ov_slot), strip, ov_slot);
        }
        return;
    }

    paint_scrollbar_color_h(cv, ap, s, hilite_thumb);
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
// Art path: menu.background(_pattern) + menu.item.* + menu.separator + popup_frame.*.
inline MenuLayout paint_menu(Canvas &cv, const Appearance &ap, int x, int y,
                             int width, const char *const *items, int count,
                             int hot = -1, unsigned disabled_mask = 0) {
    MenuLayout lay;
    lay.count = count;
    lay.item_h = kMenuItemH;

    int pad_l = 2, pad_t = 2, pad_r = 2, pad_b = 2;
    const SkinImage *frame = ap.art("popup_frame.focus");
    if (!frame) frame = ap.art("popup_frame.normal");
    if (frame) {
        // Positions = frame thickness (even). If all zero, derive from caps
        // (even, ≥2) so Milk's 3×3 stub still pads sanely.
        auto even_thick = [](int v) {
            if (v <= 0) return 0;
            if (v < 2) return 2;
            return v & ~1;
        };
        pad_l = even_thick(frame->positions[0]);
        pad_t = even_thick(frame->positions[1]);
        pad_r = even_thick(frame->positions[2]);
        pad_b = even_thick(frame->positions[3]);
        if (pad_l + pad_t + pad_r + pad_b == 0) {
            pad_l = pad_t = pad_r = pad_b =
                even_thick(std::max({int(frame->caps[0]), int(frame->caps[1]),
                                     int(frame->caps[2]), int(frame->caps[3]), 2}));
        }
        if (pad_l <= 0) pad_l = 2;
        if (pad_t <= 0) pad_t = 2;
        if (pad_r <= 0) pad_r = 2;
        if (pad_b <= 0) pad_b = 2;
    }

    int inner_h = count * kMenuItemH;
    int h = pad_t + pad_b + inner_h;
    lay.frame = {x, y, width, h};
    lay.items_bounds = {x + pad_l, y + pad_t, width - pad_l - pad_r, inner_h};

    // Background colour / optional pattern under art.
    cv.fill(lay.frame, ap.c("menu.background"));
    const SkinImage *pat = ap.art("menu.background_pattern");
    if (pat && !pat->empty() && lay.items_bounds.w > 0 && lay.items_bounds.h > 0) {
        for (int py = lay.items_bounds.y; py < lay.items_bounds.bottom(); py += pat->h)
            for (int px = lay.items_bounds.x; px < lay.items_bounds.right(); px += pat->w)
                cv.blit_image(*pat, px, py);
    }
    const SkinImage *mbg = ap.art("menu.background");
    if (mbg)
        cv.nine_slice(*mbg, lay.items_bounds);

    if (frame) {
        cv.nine_slice(*frame, lay.frame);
    } else {
        cv.frame(lay.frame, ap.c("focus.box"));
        cv.frame({x + 1, y + 1, width - 2, h - 2}, ap.c("menu.dark"));
        cv.hline(x + 2, x + width - 2, y + 2, ap.c("menu.light"));
        cv.vline(x + 2, y + 2, y + h - 2, ap.c("menu.light"));
    }

    for (int i = 0; i < count; ++i) {
        Rect row{lay.items_bounds.x, lay.items_bounds.y + i * kMenuItemH,
                 lay.items_bounds.w, kMenuItemH};
        bool dis = (disabled_mask >> i) & 1;
        bool is_hot = (!dis && i == hot);
        bool is_sep = items[i] && (std::strcmp(items[i], "-") == 0 ||
                                   std::strcmp(items[i], "—") == 0);

        if (is_sep) {
            // Hap docs: separator ≤4 px. Milk's 28×36 plate is not a rule —
            // treat as missing and draw the colour path.
            const SkinImage *sep = ap.art("menu.separator");
            if (sep && sep->h > 0 && sep->h <= 4) {
                int sh = std::min(sep->h, row.h);
                Rect dest{row.x, row.y + (row.h - sh) / 2, row.w, sh};
                cv.nine_slice(*sep, dest);
            } else {
                int mid = row.y + row.h / 2;
                cv.hline(row.x + 4, row.right() - 4, mid, ap.c("menu.dark"));
                cv.hline(row.x + 4, row.right() - 4, mid + 1, ap.c("menu.light"));
            }
            continue;
        }

        const char *pslot = dis ? "menu.item.pattern.disabled"
                                : (is_hot ? "menu.item.pattern.hilited"
                                          : "menu.item.pattern.normal");
        const SkinImage *ipat = ap.art(pslot);
        if (!ipat && is_hot) ipat = ap.art("menu.item.pattern.normal");
        if (ipat && !ipat->empty() && ipat->w > 0 && ipat->h > 0) {
            Rect prev = cv.push_clip(row);
            for (int py = row.y; py < row.bottom(); py += ipat->h)
                for (int px = row.x; px < row.right(); px += ipat->w)
                    cv.blit_image(*ipat, px, py);
            cv.pop_clip(prev);
        }

        const char *islot = dis ? "menu.item.disabled"
                                : (is_hot ? "menu.item.hilited" : "menu.item.normal");
        const SkinImage *item = ap.art(islot);
        if (!item && is_hot) item = ap.art("menu.item.normal");
        if (item) {
            cv.nine_slice(*item, row);
        } else if (is_hot) {
            cv.fill(row, ap.c("menu.hilite_background"));
            cv.hline(row.x, row.right(), row.y, ap.c("menu.hilite_light"));
            cv.vline(row.x, row.y, row.bottom(), ap.c("menu.hilite_light"));
            cv.hline(row.x, row.right(), row.bottom() - 1, ap.c("menu.hilite_dark"));
            cv.vline(row.right() - 1, row.y, row.bottom(), ap.c("menu.hilite_dark"));
        }

        Color ink = dis ? ap.c("menu.disable_label")
                        : (is_hot ? ap.c("menu.hilite_label") : ap.c("menu.label"));
        if (!dis && item && item->has_text_color)
            ink = plate_text_color(item);
        else if (!dis)
            ink = readable_label(ap, ink);
        cv.text(row.x + 8, row.y + (kMenuItemH - kFontHeight) / 2, items[i], ink);
    }
    return lay;
}

constexpr int kMenuBarH = 20;

struct MenuBarLayout {
    Rect bounds{};
    int count = 0;
    Rect item_rects[12]{};
};

// Menu bar strip — distinct from open popup menus.
// Hap Menu Bar art is rarely authored (slots empty across probed themes);
// colour path uses menu.* roles; optional art keys: menu_bar.pattern,
// menu_bar.background, menu_bar.title(.hilited|.disabled).
inline MenuBarLayout paint_menu_bar(Canvas &cv, const Appearance &ap, Rect r,
                                    const char *const *titles, int count,
                                    int hot = -1, unsigned disabled_mask = 0) {
    MenuBarLayout lay;
    lay.bounds = r;
    lay.count = std::clamp(count, 0, 12);

    cv.fill(r, ap.c("menu.background"));
    const SkinImage *pat = ap.art("menu_bar.pattern");
    if (pat && !pat->empty()) {
        for (int py = r.y; py < r.bottom(); py += pat->h)
            for (int px = r.x; px < r.right(); px += pat->w)
                cv.blit_image(*pat, px, py);
    }
    const SkinImage *bar = ap.art("menu_bar.background");
    if (bar) cv.nine_slice(*bar, r);
    else {
        cv.hline(r.x, r.right(), r.y, ap.c("menu.light"));
        cv.hline(r.x, r.right(), r.bottom() - 1, ap.c("menu.dark"));
    }

    int x = r.x + 6;
    for (int i = 0; i < lay.count; ++i) {
        const char *t = titles[i] ? titles[i] : "";
        int tw = cv.text_width(t);
        int iw = tw + 16;
        Rect item{x, r.y, iw, r.h};
        lay.item_rects[i] = item;
        bool dis = (disabled_mask >> i) & 1;
        bool is_hot = (!dis && i == hot);

        const char *tslot = dis ? "menu_bar.title.disabled"
                                : (is_hot ? "menu_bar.title.hilited"
                                          : "menu_bar.title.normal");
        const SkinImage *title_art = ap.art(tslot);
        if (!title_art && is_hot) title_art = ap.art("menu_bar.title.normal");
        const SkinImage *tpat = ap.art(dis ? "menu_bar.title_pattern.disabled"
                                           : (is_hot ? "menu_bar.title_pattern.hilited"
                                                     : "menu_bar.title_pattern.normal"));
        if (tpat && !tpat->empty()) {
            for (int py = item.y; py < item.bottom(); py += tpat->h)
                for (int px = item.x; px < item.right(); px += tpat->w)
                    cv.blit_image(*tpat, px, py);
        } else if (is_hot) {
            cv.fill(item, ap.c("menu.hilite_background"));
        }
        if (title_art) cv.nine_slice(*title_art, item);

        Color ink = dis ? ap.c("menu.disable_label")
                        : (is_hot ? ap.c("menu.hilite_label") : ap.c("menu.label"));
        if (!dis && title_art && title_art->has_text_color)
            ink = plate_text_color(title_art);
        else if (!dis)
            ink = readable_label(ap, ink);
        cv.text(item.x + 8, item.y + (item.h - kFontHeight) / 2, t, ink);
        x += iw;
    }
    return lay;
}

inline int menu_hit_row(const MenuLayout &lay, int mx, int my) {
    if (!lay.items_bounds.contains(mx, my) || lay.item_h <= 0) return -1;
    int row = (my - lay.items_bounds.y) / lay.item_h;
    if (row < 0 || row >= lay.count) return -1;
    return row;
}

// Haxial Popup Button — art plate + symbol place; else raised colour plate.
// `no_title` uses popup.no_title.* (well next to a field; symbol centred).
inline void paint_dropdown(Canvas &cv, const Appearance &ap, Rect r,
                           const char *label, bool open, bool pressed,
                           bool disabled = false, bool no_title = false) {
    bool down = pressed || open;
    const char *pslot;
    if (no_title) {
        pslot = disabled ? "popup.no_title.disabled"
                         : (down ? "popup.no_title.hilited" : "popup.no_title.normal");
    } else {
        pslot = disabled ? "popup.disabled"
                         : (down ? "popup.hilited" : "popup.normal");
    }
    const SkinImage *plate = ap.art(pslot);
    if (!plate && down)
        plate = ap.art(no_title ? "popup.no_title.normal" : "popup.normal");
    if (!plate && no_title) {
        pslot = disabled ? "popup.disabled"
                         : (down ? "popup.hilited" : "popup.normal");
        plate = ap.art(pslot);
        if (!plate && down) plate = ap.art("popup.normal");
    }
    const char *sslot = disabled ? "popup.symbol.disabled"
                                 : (down ? "popup.symbol.hilited" : "popup.symbol.normal");
    const SkinImage *sym = ap.art(sslot);
    if (!sym) sym = ap.art("popup.symbol.normal");

    if (plate) {
        cv.nine_slice(*plate, r);
        if (sym) {
            int sx, sy;
            if (no_title) {
                sx = r.x + (r.w - sym->w) / 2;
                sy = r.y + (r.h - sym->h) / 2;
            } else {
                if (sym->positions[0] > 0)
                    sx = r.x + sym->positions[0];
                else if (sym->positions[2] > 0)
                    sx = r.right() - sym->positions[2] - sym->w;
                else
                    sx = r.right() - sym->w - 4;
                if (sym->positions[1] > 0)
                    sy = r.y + sym->positions[1];
                else if (sym->positions[3] > 0)
                    sy = r.bottom() - sym->positions[3] - sym->h;
                else
                    sy = r.y + (r.h - sym->h) / 2;
            }
            cv.place(*sym, sx, sy);
        }
        if (no_title || !label || !*label) return;
        Color ink = button_label_ink(ap, disabled, true, plate);
        int tx = r.x + 8 + (down ? 1 : 0);
        int ty = r.y + (r.h - kFontHeight) / 2 + (down ? 1 : 0);
        int max_w = (sym ? (sym->positions[2] > 0 ? r.w - sym->positions[2] - sym->w
                                                  : r.w - sym->w - 8)
                         : r.w - 20) -
                    8;
        if (max_w < 8) max_w = 8;
        if (cv.text_width(label) <= max_w)
            cv.text(tx, ty, label, ink);
        else {
            std::string s(label);
            while (s.size() > 1 && cv.text_width((s + "..").c_str()) > max_w)
                s.pop_back();
            s += "..";
            cv.text(tx, ty, s.c_str(), ink);
        }
        return;
    }

    paint_button_face(cv, ap, r, down, disabled);

    const char *grp = disabled ? "button_disable" : "button";
    auto bc = [&](const char *s) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s.%s", grp, s);
        return ap.c(buf);
    };
    Color ink = bc("label");
    Color seam_d = down ? bc("light2") : bc("dark2");
    Color seam_l = down ? bc("dark2") : bc("light2");

    int aw = std::min(kDropArrowW, r.w / 3);
    if (aw < 14) aw = std::min(14, r.w);
    int div_x = r.right() - aw;
    cv.vline(div_x, r.y + 4, r.bottom() - 4, seam_d);
    cv.vline(div_x + 1, r.y + 4, r.bottom() - 4, seam_l);

    Rect well{div_x + 1, r.y, r.right() - (div_x + 1), r.h};
    paint_arrow(cv, well, false, ink);

    if (no_title || !label || !*label) return;
    int tx = r.x + 8 + (down ? 1 : 0);
    int ty = r.y + (r.h - kFontHeight) / 2 + (down ? 1 : 0);
    int max_w = div_x - tx - 4;
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
    int thumb_w = kSliderThumbW;
    int thumb_h = kSliderThumbH;
    int travel_a = 0; // H: left inset; V: top inset (Hap Positions)
    int travel_b = 0; // H: right inset; V: bottom inset
};

// Hap: bar spans the full control; Positions only limit indicator travel inside it.
inline SliderLayout slider_layout(Rect r, int value, int max_value,
                                  int thumb_w = kSliderThumbW,
                                  int thumb_h = kSliderThumbH, int travel_l = 0,
                                  int travel_r = 0, int bar_h = 6,
                                  int thumb_above = 0) {
    SliderLayout s;
    s.bounds = r;
    s.max_value = std::max(1, max_value);
    s.value = std::clamp(value, 0, s.max_value);
    s.thumb_w = thumb_w;
    s.thumb_h = thumb_h;
    s.travel_a = std::max(0, travel_l);
    s.travel_b = std::max(0, travel_r);
    if (bar_h < 2) bar_h = 2;
    s.bar = {r.x, r.y + (r.h - bar_h) / 2, r.w, bar_h};
    int track_x = s.bar.x + s.travel_a;
    int track_w = s.bar.w - s.travel_a - s.travel_b;
    if (track_w < thumb_w) track_w = std::max(thumb_w, s.bar.w);
    int travel = std::max(0, track_w - thumb_w);
    int tx = track_x + (travel * s.value) / s.max_value;
    int ty = s.bar.y - thumb_above;
    if (thumb_above <= 0)
        ty = r.y + (r.h - thumb_h) / 2;
    s.thumb = {tx, ty, thumb_w, thumb_h};
    return s;
}

inline SliderLayout slider_layout(const Appearance &ap, Rect r, int value,
                                  int max_value, bool hilite = false,
                                  bool pointed = false) {
    const char *bslot = hilite ? "slider.h.bar.hilited" : "slider.h.bar.normal";
    const SkinImage *bar = ap.art(bslot);
    if (!bar) bar = ap.art("slider.h.bar.normal");
    const char *islot =
        pointed ? (hilite ? "slider.h.indicator_pointed.hilited"
                          : "slider.h.indicator_pointed.normal")
                : (hilite ? "slider.h.indicator.hilited"
                          : "slider.h.indicator.normal");
    const SkinImage *ind = ap.art(islot);
    if (!ind && pointed)
        ind = ap.art(hilite ? "slider.h.indicator.hilited"
                            : "slider.h.indicator.normal");
    if (!ind) ind = ap.art("slider.h.indicator.normal");

    int thumb_w = kSliderThumbW, thumb_h = kSliderThumbH;
    int travel_l = 0, travel_r = 0, bar_h = 6, thumb_above = 0;
    if (bar) {
        bar_h = bar->h;
        travel_l = bar->positions[0];
        travel_r = bar->positions[2];
    }
    if (ind) {
        thumb_w = ind->w;
        thumb_h = ind->h;
        thumb_above = ind->positions[1];
    }
    // AppearanceEdit: total height including indicator ≤ 30.
    int band = bar_h;
    if (thumb_above > 0)
        band = bar_h + thumb_above + std::max(0, thumb_h - bar_h);
    else
        band = std::max(bar_h, thumb_h);
    if (band > 30) {
        if (thumb_above > 0) thumb_above = std::max(0, 30 - bar_h - 1);
        band = (thumb_above > 0) ? bar_h + thumb_above + std::max(0, thumb_h - bar_h)
                                 : std::max(bar_h, thumb_h);
        if (band > 30 && thumb_h > bar_h) thumb_h = std::max(bar_h, 30 - std::max(0, thumb_above));
    }
    if (band > r.h && r.h > 0) {
        if (thumb_above > 0) thumb_above = std::max(0, r.h - bar_h - 1);
    }
    return slider_layout(r, value, max_value, thumb_w, thumb_h, travel_l, travel_r,
                         bar_h, thumb_above);
}

inline int slider_value_at_x(const SliderLayout &s, int mx) {
    int track_x = s.bar.x + s.travel_a;
    int track_w = s.bar.w - s.travel_a - s.travel_b;
    int travel = std::max(1, track_w - s.thumb_w);
    int rel = mx - track_x - s.thumb_w / 2;
    return std::clamp(rel * s.max_value / travel, 0, s.max_value);
}

inline SliderLayout slider_layout_v(const Appearance &ap, Rect r, int value,
                                    int max_value, bool hilite = false,
                                    bool pointed = false) {
    SliderLayout s;
    s.bounds = r;
    s.max_value = std::max(1, max_value);
    s.value = std::clamp(value, 0, s.max_value);

    const char *bslot = hilite ? "slider.v.bar.hilited" : "slider.v.bar.normal";
    const SkinImage *bar = ap.art(bslot);
    if (!bar) bar = ap.art("slider.v.bar.normal");
    const char *islot =
        pointed ? (hilite ? "slider.v.indicator_pointed.hilited"
                          : "slider.v.indicator_pointed.normal")
                : (hilite ? "slider.v.indicator.hilited"
                          : "slider.v.indicator.normal");
    const SkinImage *ind = ap.art(islot);
    if (!ind && pointed)
        ind = ap.art(hilite ? "slider.v.indicator.hilited"
                            : "slider.v.indicator.normal");
    if (!ind) ind = ap.art("slider.v.indicator.normal");

    int bar_w = 6, travel_t = 0, travel_b = 0;
    int thumb_w = kSliderThumbH, thumb_h = kSliderThumbW; // swap defaults
    int thumb_left = 0;
    if (bar) {
        bar_w = bar->w;
        travel_t = bar->positions[1];
        travel_b = bar->positions[3];
    }
    if (ind) {
        thumb_w = ind->w;
        thumb_h = ind->h;
        thumb_left = ind->positions[0];
    }
    // AppearanceEdit: total width including indicator ≤ 30.
    int band = bar_w;
    if (thumb_left > 0)
        band = bar_w + thumb_left + std::max(0, thumb_w - bar_w);
    else
        band = std::max(bar_w, thumb_w);
    if (band > 30) {
        if (thumb_left > 0) thumb_left = std::max(0, 30 - bar_w - 1);
        if (std::max(bar_w, thumb_w) > 30 && thumb_w > bar_w)
            thumb_w = std::max(bar_w, 30 - std::max(0, thumb_left));
    }
    if (band > r.w && r.w > 0) {
        if (thumb_left > 0) thumb_left = std::max(0, r.w - bar_w - 1);
    }

    s.thumb_w = thumb_w;
    s.thumb_h = thumb_h;
    s.travel_a = std::max(0, travel_t);
    s.travel_b = std::max(0, travel_b);
    // Bar spans full height; Positions only limit thumb travel.
    s.bar = {r.x + (r.w - bar_w) / 2, r.y, bar_w, r.h};
    int track_y = s.bar.y + s.travel_a;
    int track_h = s.bar.h - s.travel_a - s.travel_b;
    if (track_h < thumb_h) track_h = std::max(thumb_h, s.bar.h);
    int travel = std::max(0, track_h - thumb_h);
    // Value 0 at top (like scroll).
    int ty = track_y + (travel * s.value) / s.max_value;
    int tx = s.bar.x - thumb_left;
    if (thumb_left <= 0) tx = r.x + (r.w - thumb_w) / 2;
    s.thumb = {tx, ty, thumb_w, thumb_h};
    return s;
}

inline int slider_value_at_y(const SliderLayout &s, int my) {
    int track_y = s.bar.y + s.travel_a;
    int track_h = s.bar.h - s.travel_a - s.travel_b;
    int travel = std::max(1, track_h - s.thumb_h);
    int rel = my - track_y - s.thumb_h / 2;
    return std::clamp(rel * s.max_value / travel, 0, s.max_value);
}

inline SliderLayout paint_slider(Canvas &cv, const Appearance &ap, Rect r,
                                 int value, int max_value, bool hilite,
                                 bool pointed = false, bool disabled = false) {
    // Disabled art when present; layout still uses normal metrics if disabled
    // plates are missing (Hap often authors only Normal/Hilited).
    const char *bslot = disabled ? "slider.h.bar.disabled"
                                 : (hilite ? "slider.h.bar.hilited"
                                           : "slider.h.bar.normal");
    const SkinImage *bar = ap.art(bslot);
    if (!bar && disabled) bar = ap.art("slider.h.bar.normal");
    if (!bar) bar = ap.art("slider.h.bar.normal");
    const char *islot =
        disabled
            ? (pointed ? "slider.h.indicator_pointed.disabled"
                       : "slider.h.indicator.disabled")
            : (pointed ? (hilite ? "slider.h.indicator_pointed.hilited"
                                 : "slider.h.indicator_pointed.normal")
                       : (hilite ? "slider.h.indicator.hilited"
                                 : "slider.h.indicator.normal"));
    SliderLayout s = slider_layout(ap, r, value, max_value, hilite && !disabled,
                                   pointed);
    const SkinImage *ind = ap.art(islot);
    if (!ind && pointed && !disabled)
        ind = ap.art(hilite ? "slider.h.indicator.hilited"
                            : "slider.h.indicator.normal");
    if (!ind && disabled) {
        ind = ap.art(pointed ? "slider.h.indicator_pointed.normal"
                             : "slider.h.indicator.normal");
    }
    if (!ind) ind = ap.art("slider.h.indicator.normal");

    if (bar) {
        cv.nine_slice(*bar, s.bar);
    } else if (disabled) {
        // Colour-path disabled: flat disable plate, no value-fill trough.
        cv.fill(s.bar, ap.c("slider.disable"));
        cv.frame(s.bar, ap.c("slider.disable_frame"));
    } else {
        // Hap travel model: full trough in Bar colour; indicator alone shows
        // value. Do not value-fill the trough (old kit mistake).
        cv.fill(s.bar, ap.c("slider.bar"));
        cv.frame(s.bar, ap.c("slider.bar_frame"));
    }

    if (ind) {
        if (ind->caps[0] || ind->caps[1] || ind->caps[2] || ind->caps[3])
            cv.nine_slice(*ind, s.thumb);
        else
            cv.blit_image(*ind, s.thumb.x, s.thumb.y);
        return s;
    }

    if (disabled) {
        cv.fill(s.thumb, ap.c("slider.disable"));
        rounded_frame(cv, s.thumb, ap.c("slider.disable_frame"),
                      ap.c("primary.background"));
        return s;
    }
    Color il = hilite ? ap.c("slider.indicator_hilite_light")
                      : ap.c("slider.indicator_light");
    Color face = hilite ? ap.c("slider.indicator_hilite") : ap.c("slider.indicator");
    Color id = hilite ? ap.c("slider.indicator_hilite_dark")
                      : ap.c("slider.indicator_dark");
    Color fr = hilite ? ap.c("slider.indicator_hilite_frame")
                      : ap.c("slider.indicator_frame");
    cv.fill(s.thumb, face);
    rounded_frame(cv, s.thumb, fr, ap.c("primary.background"));
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.y + 1, il);
    cv.vline(s.thumb.x + 1, s.thumb.y + 1, s.thumb.bottom() - 1, il);
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.bottom() - 2, id);
    cv.vline(s.thumb.right() - 2, s.thumb.y + 1, s.thumb.bottom() - 1, id);
    int mid = s.thumb.y + s.thumb.h / 2;
    for (int i = -2; i <= 2; i += 2)
        cv.hline(s.thumb.x + 2, s.thumb.right() - 2, mid + i, il);
    return s;
}

inline SliderLayout paint_slider_v(Canvas &cv, const Appearance &ap, Rect r,
                                   int value, int max_value, bool hilite,
                                   bool pointed = false, bool disabled = false) {
    const char *bslot = disabled ? "slider.v.bar.disabled"
                                 : (hilite ? "slider.v.bar.hilited"
                                           : "slider.v.bar.normal");
    const SkinImage *bar = ap.art(bslot);
    if (!bar && disabled) bar = ap.art("slider.v.bar.normal");
    if (!bar) bar = ap.art("slider.v.bar.normal");
    const char *islot =
        disabled
            ? (pointed ? "slider.v.indicator_pointed.disabled"
                       : "slider.v.indicator.disabled")
            : (pointed ? (hilite ? "slider.v.indicator_pointed.hilited"
                                 : "slider.v.indicator_pointed.normal")
                       : (hilite ? "slider.v.indicator.hilited"
                                 : "slider.v.indicator.normal"));
    SliderLayout s =
        slider_layout_v(ap, r, value, max_value, hilite && !disabled, pointed);
    const SkinImage *ind = ap.art(islot);
    if (!ind && pointed && !disabled)
        ind = ap.art(hilite ? "slider.v.indicator.hilited"
                            : "slider.v.indicator.normal");
    if (!ind && disabled) {
        ind = ap.art(pointed ? "slider.v.indicator_pointed.normal"
                             : "slider.v.indicator.normal");
    }
    if (!ind) ind = ap.art("slider.v.indicator.normal");

    if (bar) {
        cv.nine_slice(*bar, s.bar);
    } else if (disabled) {
        cv.fill(s.bar, ap.c("slider.disable"));
        cv.frame(s.bar, ap.c("slider.disable_frame"));
    } else {
        cv.fill(s.bar, ap.c("slider.bar"));
        cv.frame(s.bar, ap.c("slider.bar_frame"));
    }

    if (ind) {
        if (ind->caps[0] || ind->caps[1] || ind->caps[2] || ind->caps[3])
            cv.nine_slice(*ind, s.thumb);
        else
            cv.blit_image(*ind, s.thumb.x, s.thumb.y);
        return s;
    }

    if (disabled) {
        cv.fill(s.thumb, ap.c("slider.disable"));
        rounded_frame(cv, s.thumb, ap.c("slider.disable_frame"),
                      ap.c("primary.background"));
        return s;
    }
    Color il = hilite ? ap.c("slider.indicator_hilite_light")
                      : ap.c("slider.indicator_light");
    Color face = hilite ? ap.c("slider.indicator_hilite") : ap.c("slider.indicator");
    Color id = hilite ? ap.c("slider.indicator_hilite_dark")
                      : ap.c("slider.indicator_dark");
    Color fr = hilite ? ap.c("slider.indicator_hilite_frame")
                      : ap.c("slider.indicator_frame");
    cv.fill(s.thumb, face);
    rounded_frame(cv, s.thumb, fr, ap.c("primary.background"));
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.y + 1, il);
    cv.vline(s.thumb.x + 1, s.thumb.y + 1, s.thumb.bottom() - 1, il);
    cv.hline(s.thumb.x + 1, s.thumb.right() - 1, s.thumb.bottom() - 2, id);
    cv.vline(s.thumb.right() - 2, s.thumb.y + 1, s.thumb.bottom() - 1, id);
    return s;
}

// --- Tick (checkbox) / Mutex (radio)

// --- Tick (checkbox) / Mutex (radio) ------------------------------------
// AppearanceEdit: Tick/Mutex Blank|Ticked|Tristate × Normal/Hilited/Disabled.
// Title is drawn outside the box. Height ≤ 18.

constexpr int kTickBox = 14;

enum class TickMark { Blank, Ticked, Tristate };

inline const char *tick_art_slot(TickMark mark, bool pressed, bool disabled) {
    const char *m = mark == TickMark::Ticked     ? "ticked"
                    : mark == TickMark::Tristate ? "tristate"
                                                 : "blank";
    const char *st = disabled ? "disabled" : (pressed ? "hilited" : "normal");
    static char buf[48];
    std::snprintf(buf, sizeof(buf), "tick.%s.%s", m, st);
    return buf;
}

inline const char *mutex_art_slot(TickMark mark, bool pressed, bool disabled) {
    const char *m = mark == TickMark::Ticked     ? "ticked"
                    : mark == TickMark::Tristate ? "tristate"
                                                 : "blank";
    const char *st = disabled ? "disabled" : (pressed ? "hilited" : "normal");
    static char buf[48];
    std::snprintf(buf, sizeof(buf), "mutex.%s.%s", m, st);
    return buf;
}

inline void paint_tick_glyph(Canvas &cv, Rect b, Color ink) {
    for (int i = 0; i < 4; ++i) {
        cv.put(b.x + 3, b.y + 6 + i, pack(ink));
        cv.put(b.x + 4, b.y + 7 + i, pack(ink));
    }
    for (int i = 0; i < 6; ++i) {
        cv.put(b.x + 5 + i, b.y + 9 - i, pack(ink));
        cv.put(b.x + 5 + i, b.y + 10 - i, pack(ink));
    }
}

inline void paint_tristate_glyph(Canvas &cv, Rect b, Color ink) {
    cv.fill({b.x + 3, b.y + 3, b.w - 6, b.h - 6}, ink);
}

inline void paint_mutex_dot(Canvas &cv, Rect b, Color ink) {
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            if (dx * dx + dy * dy <= 5) cv.put(cx + dx, cy + dy, pack(ink));
}

// Place Tick/Mutex art: Hap marks are ≤18 and must not smear via nine_slice
// into kTickBox. Centre-blit natural size when close to the dest.
inline void place_tick_art(Canvas &cv, const SkinImage &img, Rect &b) {
    auto adiff = [](int a, int b) { return a > b ? a - b : b - a; };
    if (img.w <= 18 && img.h <= 18 && adiff(img.w, b.w) <= 6 &&
        adiff(img.h, b.h) <= 6) {
        b.w = img.w;
        b.h = img.h;
        cv.blit_image(img, b.x, b.y);
        return;
    }
    if (img.w == b.w && img.h == b.h)
        cv.blit_image(img, b.x, b.y);
    else
        cv.nine_slice(img, b);
}

// Checkbox. Returns the hit box (glyph only).
inline Rect paint_tick(Canvas &cv, const Appearance &ap, int x, int y,
                       TickMark mark, const char *label, bool pressed = false,
                       bool disabled = false) {
    Rect b{x, y, kTickBox, kTickBox};
    const SkinImage *img = ap.art(tick_art_slot(mark, pressed, disabled));
    if (!img && pressed) img = ap.art(tick_art_slot(mark, false, disabled));
    if (!img && mark != TickMark::Blank)
        img = ap.art(tick_art_slot(TickMark::Blank, pressed, disabled));
    if (img) {
        place_tick_art(cv, *img, b);
    } else {
        const char *grp = disabled ? "button_disable" : "button";
        auto bc = [&](const char *s) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s.%s", grp, s);
            return ap.c(buf);
        };
        cv.fill(b, pressed ? bc("dark1") : bc("face"));
        rounded_frame(cv, b, bc("frame"), ap.c("primary.background"));
        cv.hline(b.x + 1, b.right() - 1, b.y + 1, pressed ? bc("dark2") : bc("light2"));
        cv.vline(b.x + 1, b.y + 1, b.bottom() - 1, pressed ? bc("dark2") : bc("light2"));
        cv.hline(b.x + 1, b.right() - 1, b.bottom() - 2, pressed ? bc("light2") : bc("dark2"));
        cv.vline(b.right() - 2, b.y + 1, b.bottom() - 1, pressed ? bc("light2") : bc("dark2"));
        Color ink = bc("label");
        if (mark == TickMark::Ticked) paint_tick_glyph(cv, b, ink);
        else if (mark == TickMark::Tristate) paint_tristate_glyph(cv, b, ink);
    }
    if (label && *label) {
        Color ink = disabled ? ap.c("primary.disable_label") : ap.c("primary.label");
        cv.text(b.right() + 6, b.y + (b.h - kFontHeight) / 2, label, ink);
    }
    return b;
}

// Radio (mutex). Same layout as Tick; glyph is a filled disc when ticked.
inline Rect paint_mutex(Canvas &cv, const Appearance &ap, int x, int y,
                        TickMark mark, const char *label, bool pressed = false,
                        bool disabled = false) {
    Rect b{x, y, kTickBox, kTickBox};
    const SkinImage *img = ap.art(mutex_art_slot(mark, pressed, disabled));
    if (!img && pressed) img = ap.art(mutex_art_slot(mark, false, disabled));
    if (!img && mark != TickMark::Blank)
        img = ap.art(mutex_art_slot(TickMark::Blank, pressed, disabled));
    if (img) {
        place_tick_art(cv, *img, b);
    } else {
        const char *grp = disabled ? "button_disable" : "button";
        auto bc = [&](const char *s) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s.%s", grp, s);
            return ap.c(buf);
        };
        // Circular-ish colour plate: chamfered square reads as radio well.
        cv.fill(b, pressed ? bc("dark1") : bc("face"));
        rounded_frame(cv, b, bc("frame"), ap.c("primary.background"));
        cv.hline(b.x + 1, b.right() - 1, b.y + 1, pressed ? bc("dark2") : bc("light2"));
        cv.vline(b.x + 1, b.y + 1, b.bottom() - 1, pressed ? bc("dark2") : bc("light2"));
        cv.hline(b.x + 1, b.right() - 1, b.bottom() - 2, pressed ? bc("light2") : bc("dark2"));
        cv.vline(b.right() - 2, b.y + 1, b.bottom() - 1, pressed ? bc("light2") : bc("dark2"));
        Color ink = bc("label");
        if (mark == TickMark::Ticked) paint_mutex_dot(cv, b, ink);
        else if (mark == TickMark::Tristate) paint_tristate_glyph(cv, b, ink);
    }
    if (label && *label) {
        Color ink = disabled ? ap.c("primary.disable_label") : ap.c("primary.label");
        cv.text(b.right() + 6, b.y + (b.h - kFontHeight) / 2, label, ink);
    }
    return b;
}

// --- Progress / separators / box / disclosure / focus art ---------------

constexpr int kProgressH = 15; // AppearanceEdit: ≤ 16; Milk Hap bar/fill are 15
constexpr int kWonderLight = 16;

enum class ProgressStyle {
    Continuous, // AppearanceEdit: stretch Hap fill (or colour gradient)
    Segmented,  // KDX File Transfers: tiled LED columns in the trough
};

inline void paint_progress_trough_colour(Canvas &cv, const Appearance &ap, Rect r) {
    // Recessed well — dark top/left, light bottom/right (matches Hap trough).
    cv.fill(r, ap.c("progress.bkgnd"));
    cv.frame(r, ap.c("progress.frame"));
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, ap.c("progress.bkgnd_dark"));
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, ap.c("progress.bkgnd_dark"));
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, ap.c("progress.bkgnd_light"));
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, ap.c("progress.bkgnd_light"));
}

inline Color progress_led_shade(Color body, int row, int h) {
    // Glossy LED column: bright near top-third, mid body, darker lower band.
    if (h <= 1) return body;
    int t = (row * 10) / std::max(1, h - 1); // 0..10
    auto clamp8 = [](int v) -> uint8_t {
        return uint8_t(std::clamp(v, 0, 255));
    };
    if (t <= 2) {
        int k = 90 - t * 20;
        return {clamp8(int(body.r) + k), clamp8(int(body.g) + k),
                clamp8(int(body.b) + k)};
    }
    if (t >= 7) {
        int k = 55 + (t - 7) * 15;
        return {clamp8(int(body.r) * (100 - k) / 100),
                clamp8(int(body.g) * (100 - k) / 100),
                clamp8(int(body.b) * (100 - k) / 100)};
    }
    return body;
}

inline void paint_progress_led_colour(Canvas &cv, Rect seg, Color body) {
    for (int y = 0; y < seg.h; ++y) {
        Color c = progress_led_shade(body, y, seg.h);
        cv.hline(seg.x, seg.right(), seg.y + y, c);
    }
}

inline int progress_art_height(const SkinImage *bar, const SkinImage *fill) {
    int h = kProgressH;
    if (bar && bar->h > 0) h = bar->h;
    else if (fill && fill->h > 0) h = fill->h;
    return std::clamp(h, 1, 16);
}

// led_tint: colour-path LED hue for File Transfers (WonderLight state). Ignored
// when Hap progress.fill art is present (theme owns the LED look).
inline void paint_progress(Canvas &cv, const Appearance &ap, Rect r, int value,
                           int max_value = 100,
                           ProgressStyle style = ProgressStyle::Continuous,
                           const Color *led_tint = nullptr) {
    if (r.w <= 0 || r.h <= 0) return;
    int vmax = std::max(1, max_value);
    int v = std::clamp(value, 0, vmax);
    const SkinImage *bar = ap.art("progress.bar");
    const SkinImage *fill = ap.art("progress.fill");

    // AppearanceEdit: progress is vertically centered; height ≤ 16.
    int ph = progress_art_height(bar, fill);
    Rect track = r;
    if (r.h != ph) {
        track.h = ph;
        track.y = r.y + (r.h - ph) / 2;
    }

    auto paint_trough = [&]() {
        if (bar)
            cv.nine_slice(*bar, track);
        else
            paint_progress_trough_colour(cv, ap, track);
    };

    if (style == ProgressStyle::Segmented) {
        paint_trough();
        constexpr int kGap = 1, kPad = 1;
        int seg_w = fill ? fill->w : 4;
        if (seg_w < 2) seg_w = 2;
        int inner = track.w - 2 * kPad;
        if (inner < seg_w) return;
        int nseg = std::max(1, (inner + kGap) / (seg_w + kGap));
        // Round up so small % still lights at least one LED when v > 0.
        int lit = v <= 0 ? 0 : std::max(1, (nseg * v + vmax - 1) / vmax);
        if (lit > nseg) lit = nseg;

        Color body = led_tint ? *led_tint : ap.c("progress.transition.5");
        for (int i = 0; i < lit; ++i) {
            int sx = track.x + kPad + i * (seg_w + kGap);
            if (sx + seg_w > track.right() - kPad) break;
            if (fill) {
                // Art-first: tile Hap fill column; gap shows trough through.
                // Theme owns LED colour — led_tint is colour-path only.
                Rect seg{sx, track.y, seg_w, track.h};
                if (fill->w == seg.w && fill->h == seg.h)
                    cv.blit_image(*fill, seg.x, seg.y);
                else
                    cv.nine_slice(*fill, seg);
            } else {
                // Inset 1px so recessed bevel stays visible.
                Rect seg{sx, track.y + 1, seg_w, track.h - 2};
                if (seg.h > 0) paint_progress_led_colour(cv, seg, body);
            }
        }
        return;
    }

    // Continuous — AppearanceEdit Progress Bar + Fill.
    paint_trough();
    // Hap Positions (including authored 0). Colour path defaults to 2px inset.
    int inset_l = 2, inset_r = 2, inset_t = 2, inset_b = 2;
    if (fill) {
        inset_l = fill->positions[0];
        inset_r = fill->positions[2];
        inset_t = fill->positions[1];
        inset_b = fill->positions[3];
    }
    int inner_w = track.w - inset_l - inset_r;
    int fill_w = (inner_w * v) / vmax;
    if (fill_w <= 0) return;
    Rect fr{track.x + inset_l, track.y + inset_t, fill_w,
            track.h - inset_t - inset_b};
    if (fr.h <= 0 || fr.w <= 0) return;
    if (fill) {
        cv.nine_slice(*fill, fr);
    } else {
        Color stops[10];
        if (led_tint) {
            for (int i = 0; i < 10; ++i)
                stops[i] = progress_led_shade(*led_tint, i, 10);
        } else {
            for (int i = 0; i < 10; ++i) {
                char key[40];
                std::snprintf(key, sizeof(key), "progress.transition.%d", i);
                stops[i] = ap.c(key);
            }
        }
        cv.rect_grad_v(fr, stops, 10);
    }
}

// WonderLight — 16×16 activity lamp (KDX file-transfer status spheres).
enum class WonderLightState {
    Off,
    Pause,
    Ready,
    Go,
    Finished,
    FlashOff,
    FlashOn1,
    FlashOn2,
};

inline const char *wonderlight_art_slot(WonderLightState st) {
    switch (st) {
    case WonderLightState::Off: return "wonderlight.off";
    case WonderLightState::Pause: return "wonderlight.pause";
    case WonderLightState::Ready: return "wonderlight.ready";
    case WonderLightState::Go: return "wonderlight.go";
    case WonderLightState::Finished: return "wonderlight.finished";
    case WonderLightState::FlashOff: return "wonderlight.flash_off";
    case WonderLightState::FlashOn1: return "wonderlight.flash_on1";
    case WonderLightState::FlashOn2: return "wonderlight.flash_on2";
    }
    return "wonderlight.off";
}

inline Color wonderlight_color(WonderLightState st) {
    switch (st) {
    case WonderLightState::Go:
    case WonderLightState::Ready:
        return rgb(0, 200, 40);
    case WonderLightState::Finished:
        return rgb(40, 120, 255);
    case WonderLightState::Pause:
        return rgb(220, 180, 0);
    case WonderLightState::FlashOn1:
    case WonderLightState::FlashOn2:
        return rgb(255, 60, 60);
    case WonderLightState::FlashOff:
    case WonderLightState::Off:
    default:
        return rgb(90, 90, 90);
    }
}

inline void paint_wonderlight_sphere(Canvas &cv, Rect b, Color body) {
    // Glossy sphere: radial-ish shade + top-left highlight (colour fallback).
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    int rad = std::min(b.w, b.h) / 2 - 1;
    Color hi{uint8_t(std::min(255, int(body.r) + 90)),
             uint8_t(std::min(255, int(body.g) + 90)),
             uint8_t(std::min(255, int(body.b) + 90))};
    Color lo{uint8_t(body.r * 2 / 5), uint8_t(body.g * 2 / 5), uint8_t(body.b * 2 / 5)};
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            int dx = b.x + x - cx, dy = b.y + y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 > rad * rad) continue;
            // Highlight bias toward top-left
            int hx = dx + rad / 3, hy = dy + rad / 3;
            int t = hx * hx + hy * hy;
            Color c = t < (rad * rad) / 4 ? hi : (d2 > (rad * rad * 3) / 4 ? lo : body);
            if (d2 > (rad - 1) * (rad - 1)) c = rgb(0, 0, 0);
            cv.put(b.x + x, b.y + y, pack(c));
        }
}

inline Rect paint_wonderlight(Canvas &cv, const Appearance &ap, int x, int y,
                              WonderLightState st) {
    Rect b{x, y, kWonderLight, kWonderLight};
    const SkinImage *img = ap.art(wonderlight_art_slot(st));
    if (img) {
        if (img->w == b.w && img->h == b.h)
            cv.blit_image(*img, b.x, b.y);
        else {
            // Centre if size differs (Ashen ships 12×12).
            int dx = b.x + (b.w - img->w) / 2;
            int dy = b.y + (b.h - img->h) / 2;
            cv.blit_image(*img, dx, dy);
        }
        return b;
    }
    paint_wonderlight_sphere(cv, b, wonderlight_color(st));
    return b;
}

// File / folder icon — skin [icons] slot. No synthetic document fallback:
// incomplete Haps leave the chrome empty until soft-complete / authoring.
inline Rect paint_icon(Canvas &cv, const Appearance &ap, int x, int y,
                       const char *slot = "file.generic.16", int size = 16) {
    Rect b{x, y, size, size};
    const SkinImage *img = ap.icon(slot);
    if (!img && size >= 32) {
        if (std::strcmp(slot, "file.generic.32") == 0 ||
            std::strcmp(slot, "document.32") == 0)
            img = ap.icon("document.32");
        if (!img) img = ap.icon("file.generic.32");
        if (!img) img = ap.icon("document.32");
    }
    if (!img && size < 32) {
        if (std::strcmp(slot, "file.generic.16") == 0 ||
            std::strcmp(slot, "document.16") == 0)
            img = ap.icon("document.16");
        if (!img) img = ap.icon("file.generic.16");
        if (!img) img = ap.icon("document.16");
    }
    if (!img) return b;
    if (img->w == size && img->h == size)
        cv.blit_image(*img, b.x, b.y);
    else
        cv.place(*img, b.x + (b.w - img->w) / 2, b.y + (b.h - img->h) / 2);
    return b;
}

// True when the requested icon (or a size fallback) is in the cache.
inline bool has_icon(const Appearance &ap, const char *slot, int size = 16) {
    if (ap.icon(slot)) return true;
    if (std::strcmp(slot, "file.generic.16") == 0 ||
        std::strcmp(slot, "document.16") == 0)
        return ap.icon("document.16") || ap.icon("file.generic.16");
    if (std::strcmp(slot, "file.generic.32") == 0 ||
        std::strcmp(slot, "document.32") == 0)
        return ap.icon("document.32") || ap.icon("file.generic.32");
    if (std::strcmp(slot, "user.16") == 0)
        return ap.icon("users.16") || ap.icon("user.16");
    if (size >= 32) return ap.icon("file.generic.32") != nullptr;
    return ap.icon("file.generic.16") != nullptr;
}

// Icon Button — Hap 49/50/51. Optional title drawn beside a centred icon.
inline void paint_icon_button(Canvas &cv, const Appearance &ap, Rect r,
                              const char *icon_slot, const char *title,
                              bool pressed, bool disabled = false) {
    if (disabled) pressed = false;
    const char *slot = disabled ? "icon_button.disabled"
                                : (pressed ? "icon_button.hilited"
                                           : "icon_button.normal");
    const SkinImage *img = ap.art(slot);
    if (!img && pressed) img = ap.art("icon_button.normal");
    if (img)
        cv.nine_slice(*img, r);
    else
        paint_button_face(cv, ap, r, pressed, disabled);

    int pad = 3;
    int icon_sz = std::min(16, std::min(r.h, r.w) - 2 * pad);
    if (icon_sz < 8) icon_sz = std::max(8, std::min(r.h, r.w) - 2);
    int off = pressed ? 1 : 0;
    bool has_title = title && *title;
    const char *islot = icon_slot ? icon_slot : "file.generic.16";
    bool draw_icon = has_icon(ap, islot, icon_sz);
    if (has_title) {
        int tw = cv.text_width(title);
        int gap = draw_icon ? 4 : 0;
        int icon_part = draw_icon ? icon_sz : 0;
        int total = icon_part + gap + tw;
        int ix = r.x + std::max(pad, (r.w - total) / 2) + off;
        int iy = r.y + (r.h - icon_sz) / 2 + off;
        if (draw_icon) paint_icon(cv, ap, ix, iy, islot, icon_sz);
        Color ink = button_label_ink(ap, disabled, img != nullptr, img);
        cv.text(ix + icon_part + gap, r.y + (r.h - kFontHeight) / 2 + off, title,
                ink);
    } else if (draw_icon) {
        int ix = r.x + (r.w - icon_sz) / 2 + off;
        int iy = r.y + (r.h - icon_sz) / 2 + off;
        paint_icon(cv, ap, ix, iy, islot, icon_sz);
    }
}

struct TransferRow {
    Rect bounds{};
    Rect light{};
    Rect progress{};
};

struct TransferCol {
    const char *title;
    int w;
};

// Ellipsize label so it never paints past the cell (KDX list columns).
inline void paint_cell_text(Canvas &cv, int x, int y, int max_w, const char *text,
                            Color ink) {
    if (!text || max_w <= 0) return;
    if (cv.text_width(text) <= max_w) {
        cv.text(x, y, text, ink);
        return;
    }
    std::string s(text);
    while (s.size() > 1 && cv.text_width((s + "..").c_str()) > max_w) s.pop_back();
    s += "..";
    cv.text(x, y, s.c_str(), ink);
}

// KDX File Transfers list row — single line, columns, LED meter in Progress.
inline TransferRow paint_transfer_list_row(Canvas &cv, const Appearance &ap,
                                           Rect row, const TransferCol *cols,
                                           int ncols, const char *name,
                                           const char *size, const char *status,
                                           const char *rate, WonderLightState light,
                                           int progress_pct, bool selected,
                                           const char *icon_slot = "file.generic.16") {
    TransferRow tr;
    tr.bounds = row;
    if (selected)
        cv.fill(row, ap.c("list.hilite_background"));
    Color ink = selected ? ap.c("list.hilite_foreground") : ap.c("list.label");
    int ty = row.y + (row.h - kFontHeight) / 2;
    int x = row.x;
    for (int i = 0; i < ncols; ++i) {
        Rect cell{x, row.y, cols[i].w, row.h};
        const char *key = cols[i].title ? cols[i].title : "";
        int text_pad = 4;
        int max_w = cell.w - text_pad - 2;
        if (max_w < 4) max_w = 4;
        if (std::strcmp(key, "") == 0 || std::strcmp(key, " ") == 0) {
            // Lamp column — WonderLight only.
            int ly = row.y + (row.h - kWonderLight) / 2;
            tr.light = paint_wonderlight(cv, ap, cell.x + (cell.w - kWonderLight) / 2,
                                         ly, light);
        } else if (std::strcmp(key, "Name") == 0) {
            int ix = cell.x + 2;
            int iy = row.y + (row.h - 16) / 2;
            paint_icon(cv, ap, ix, iy, icon_slot ? icon_slot : "file.generic.16", 16);
            int text_x = ix + 18;
            int name_w = cell.right() - text_x - 2;
            if (name_w < 4) name_w = 4;
            paint_cell_text(cv, text_x, ty, name_w, name, ink);
        } else if (std::strcmp(key, "Size") == 0) {
            paint_cell_text(cv, cell.x + text_pad, ty, max_w, size, ink);
        } else if (std::strcmp(key, "Progress") == 0) {
            int ph = progress_art_height(ap.art("progress.bar"), ap.art("progress.fill"));
            int pw = cell.w - 6;
            if (pw < 20) pw = std::max(8, cell.w - 2);
            tr.progress = {cell.x + 3, row.y + (row.h - ph) / 2, pw, ph};
            // Hap path: tile progress.fill art. Colour path: progress.transition.*
            // (no WonderLight rainbow tint — that is not a Hap progress colour).
            paint_progress(cv, ap, tr.progress, progress_pct, 100,
                           ProgressStyle::Segmented, nullptr);
        } else if (std::strcmp(key, "Status") == 0) {
            paint_cell_text(cv, cell.x + text_pad, ty, max_w, status, ink);
        } else if (std::strcmp(key, "Rate") == 0) {
            paint_cell_text(cv, cell.x + text_pad, ty, max_w, rate, ink);
        }
        x += cols[i].w;
    }
    cv.hline(row.x, row.right(), row.bottom() - 1, ap.c("list.separator"));
    return tr;
}

struct FileTransfersLayout {
    GelLayout gel{};
    Rect list{};
    Rect btn_close{}, btn_stop{}, btn_clear{};
};

// Official-style KDX File Transfers window sample (gel + columns + LEDs + footer).
inline FileTransfersLayout paint_file_transfers_window(Canvas &cv,
                                                       const Appearance &ap,
                                                       Rect win) {
    FileTransfersLayout ft;
    ft.gel = gel_layout(win.x, win.y, win.w, win.h, GelStyle::Dialog, &ap, true);
    paint_gel(cv, ap, win, "File Transfers", true, 0, GelStyle::Dialog);

    CanvasClip clip(cv, ft.gel.client);
    Rect cl = ft.gel.client;
    constexpr int kPad = 6;
    constexpr int kFootH = kButtonH + 8;
    int list_h = cl.h - 2 * kPad - kFootH;
    if (list_h < 0) list_h = 0;
    // Never force the list taller than the gel client — that spills past the frame.
    if (list_h < kHeaderH + kRowH && cl.h >= kHeaderH + kRowH + 2 * kPad + kFootH)
        list_h = kHeaderH + kRowH;
    ft.list = {cl.x + kPad, cl.y + kPad, std::max(0, cl.w - 2 * kPad), list_h};

    // Column widths: Status/Rate wide enough for sample strings; Name absorbs rest.
    // "Receiving" ~9 glyphs, "573/sec" ~7 — size for our bitmap font + pad.
    TransferCol cols[6] = {
        {"", 22},
        {"Name", 118},
        {"Size", 40},
        {"Progress", 100},
        {"Status", 76},
        {"Rate", 62},
    };
    int min_total = 0;
    for (int i = 0; i < 6; ++i) min_total += cols[i].w;
    if (min_total > ft.list.w) {
        // Steal from Name, then Progress, keep Status/Rate readable.
        int need = min_total - ft.list.w;
        int take = std::min(need, std::max(0, cols[1].w - 56));
        cols[1].w -= take;
        need -= take;
        if (need > 0) {
            take = std::min(need, std::max(0, cols[3].w - 64));
            cols[3].w -= take;
            need -= take;
        }
        if (need > 0) {
            take = std::min(need, std::max(0, cols[4].w - 56));
            cols[4].w -= take;
            need -= take;
        }
        if (need > 0) cols[5].w = std::max(40, cols[5].w - need);
    } else if (min_total < ft.list.w) {
        cols[1].w += ft.list.w - min_total; // leftover → Name
    }

    // Header plates per column (KDX list).
    int hx = ft.list.x;
    for (int i = 0; i < 6; ++i) {
        Rect hdr{hx, ft.list.y, cols[i].w, kHeaderH};
        const char *lab = cols[i].title;
        if (!lab || !*lab) lab = " ";
        paint_column_header(cv, ap, hdr, lab, i == 3); // Progress sorted
        hx += cols[i].w;
    }

    Rect body{ft.list.x, ft.list.y + kHeaderH, ft.list.w, ft.list.h - kHeaderH};
    cv.fill(body, ap.c("list.background"));
    cv.frame(body, ap.c("primary.frame"));

    struct Sample {
        const char *name;
        const char *size;
        const char *status;
        const char *rate;
        WonderLightState light;
        int pct;
        bool sel;
        const char *icon;
    };
    static const Sample samples[] = {
        {"server_info.txt", "1.1K", "Finished", "573/sec", WonderLightState::Finished,
         100, false, "file.generic.16"},
        {"Uploads", "—", "Receiving", "12K/sec", WonderLightState::Go, 42, true,
         "folder.uploads.16"},
        {"alice", "—", "Awaiting", "—", WonderLightState::Pause, 10, false, "user.16"},
    };
    constexpr int kSamples = 3;
    int visible = body.h / kRowH;
    if (visible > kSamples) visible = kSamples;
    for (int i = 0; i < visible; ++i) {
        Rect row{body.x + 1, body.y + 1 + i * kRowH, body.w - 2, kRowH};
        if (!samples[i].sel && i % 2)
            cv.fill(row, ap.c("list.sort_column_background"));
        paint_transfer_list_row(cv, ap, row, cols, 6, samples[i].name, samples[i].size,
                                samples[i].status, samples[i].rate, samples[i].light,
                                samples[i].pct, samples[i].sel, samples[i].icon);
    }

    // Footer buttons — Close / Stop All / Clear Finished
    int by = cl.bottom() - kPad - kButtonH;
    if (by < ft.list.bottom() + 2) by = ft.list.bottom() + 2;
    if (by + kButtonH > cl.bottom() - 2) by = cl.bottom() - 2 - kButtonH;
    if (by < cl.y) by = cl.y;
    int bw = 88;
    int gap = 8;
    int bx = cl.x + kPad;
    ft.btn_close = {bx, by, bw, kButtonH};
    ft.btn_stop = {bx + bw + gap, by, bw, kButtonH};
    ft.btn_clear = {bx + 2 * (bw + gap), by, 110, kButtonH};
    if (ft.btn_clear.right() > cl.right() - kPad) {
        int avail = cl.w - 2 * kPad - 2 * gap;
        bw = avail / 3;
        ft.btn_close = {bx, by, bw, kButtonH};
        ft.btn_stop = {bx + bw + gap, by, bw, kButtonH};
        ft.btn_clear = {bx + 2 * (bw + gap), by, bw, kButtonH};
    }
    paint_button(cv, ap, ft.btn_close, "Close", false, false);
    paint_button(cv, ap, ft.btn_stop, "Stop All", false, false);
    paint_button(cv, ap, ft.btn_clear, "Clear Finished", false, false);
    return ft;
}

// Legacy stacked transfer card — thin wrapper kept for call sites / smoke.
constexpr int kTransferRowH = 56;
inline TransferRow paint_transfer_row(Canvas &cv, const Appearance &ap, Rect r,
                                      const char *name, const char *status,
                                      WonderLightState light, int progress_pct,
                                      bool /*finished*/) {
    TransferCol cols[6] = {{"", 22}, {"Name", 120}, {"Size", 1}, {"Progress", 140},
                           {"Status", 1}, {"Rate", 1}};
    cols[3].w = std::max(80, r.w - 22 - 120 - 3);
    cv.fill(r, ap.c("list.background"));
    Rect row{r.x, r.y + (r.h - kRowH) / 2, r.w, kRowH};
    return paint_transfer_list_row(cv, ap, row, cols, 6, name, "", status, "", light,
                                   progress_pct, false);
}

inline void paint_separator_h(Canvas &cv, const Appearance &ap, Rect r) {
    const SkinImage *img = ap.art("separator.h");
    if (img) {
        int h = std::min(img->h, r.h > 0 ? r.h : img->h);
        if (h > 4) h = 4;
        Rect dest{r.x, r.y + (r.h - h) / 2, r.w, h};
        cv.nine_slice(*img, dest);
        return;
    }
    int y = r.y + r.h / 2;
    cv.hline(r.x, r.right(), y, ap.c("primary.dark"));
    cv.hline(r.x, r.right(), y + 1, ap.c("primary.light"));
}

inline void paint_separator_v(Canvas &cv, const Appearance &ap, Rect r) {
    const SkinImage *img = ap.art("separator.v");
    if (img) {
        int w = std::min(img->w, r.w > 0 ? r.w : img->w);
        if (w > 4) w = 4;
        Rect dest{r.x + (r.w - w) / 2, r.y, w, r.h};
        cv.nine_slice(*img, dest);
        return;
    }
    int x = r.x + r.w / 2;
    cv.vline(x, r.y, r.bottom(), ap.c("primary.dark"));
    cv.vline(x + 1, r.y, r.bottom(), ap.c("primary.light"));
}

// Grouping box — AppearanceEdit "Box". Optional title on the top edge.
inline void paint_box(Canvas &cv, const Appearance &ap, Rect r, const char *title = nullptr) {
    const SkinImage *img = ap.art("box");
    if (img) {
        cv.nine_slice(*img, r);
    } else {
        cv.frame(r, ap.c("primary.frame"));
        cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ap.c("primary.dark"));
    }
    if (title && *title) {
        int tw = cv.text_width(title);
        int tx = r.x + 8;
        cv.fill({tx - 2, r.y, tw + 4, kFontHeight}, ap.c("primary.background"));
        cv.text(tx, r.y - kFontHeight / 2 + 1, title, ap.c("primary.label"));
    }
}

// Placard / framed raised plate (top/bottom of full-bleed lists).
inline void paint_framed_raised(Canvas &cv, const Appearance &ap, Rect r) {
    const SkinImage *img = ap.art("framed_raised");
    if (img) {
        cv.nine_slice(*img, r);
        return;
    }
    cv.fill(r, ap.c("button.face"));
    rounded_frame(cv, r, ap.c("button.frame"), ap.c("primary.background"));
    cv.hline(r.x + 1, r.right() - 1, r.y + 1, ap.c("button.light2"));
    cv.vline(r.x + 1, r.y + 1, r.bottom() - 1, ap.c("button.light2"));
    cv.hline(r.x + 1, r.right() - 1, r.bottom() - 2, ap.c("button.dark2"));
    cv.vline(r.right() - 2, r.y + 1, r.bottom() - 1, ap.c("button.dark2"));
}

enum class DisclosureKind { PlusSmall, MinusSmall, PlusMedium, MinusMedium };

inline const char *disclosure_art_slot(DisclosureKind k) {
    switch (k) {
    case DisclosureKind::PlusSmall: return "disclosure.plus.small";
    case DisclosureKind::MinusSmall: return "disclosure.minus.small";
    case DisclosureKind::PlusMedium: return "disclosure.plus.medium";
    case DisclosureKind::MinusMedium: return "disclosure.minus.medium";
    }
    return "disclosure.plus.small";
}

inline Rect paint_disclosure(Canvas &cv, const Appearance &ap, int x, int y,
                             DisclosureKind kind, bool pressed = false) {
    bool medium = kind == DisclosureKind::PlusMedium ||
                  kind == DisclosureKind::MinusMedium;
    int s = medium ? 16 : 12;
    Rect b{x, y, s, s};
    const SkinImage *img = ap.art(disclosure_art_slot(kind));
    if (img) {
        if (img->w == b.w && img->h == b.h)
            cv.blit_image(*img, b.x, b.y);
        else if (img->w * 2 <= b.w || img->h * 2 <= b.h)
            // Much smaller than dest (Milk medium 6×8 in 16×16) — centre, no smear.
            cv.place(*img, b.x + (b.w - img->w) / 2, b.y + (b.h - img->h) / 2);
        else
            cv.nine_slice(*img, b);
        return b;
    }
    auto bc = [&](const char *suf) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "button.%s", suf);
        return ap.c(buf);
    };
    cv.fill(b, pressed ? bc("dark1") : bc("face"));
    rounded_frame(cv, b, bc("frame"), ap.c("primary.background"));
    Color ink = bc("label");
    bool plus = kind == DisclosureKind::PlusSmall || kind == DisclosureKind::PlusMedium;
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    int arm = medium ? 4 : 3;
    cv.hline(cx - arm, cx + arm + 1, cy, ink);
    cv.hline(cx - arm, cx + arm + 1, cy - 1, ink);
    if (plus) {
        cv.vline(cx, cy - arm, cy + arm + 1, ink);
        cv.vline(cx - 1, cy - arm, cy + arm + 1, ink);
    }
    return b;
}

// Focus box art around a field/list (3px thick, transparent centre). Colour
// path already draws focus.box frames in paint_field; this overlays art.
inline void paint_focus_box(Canvas &cv, const Appearance &ap, Rect r,
                            bool hilited = false, bool disabled = false) {
    const char *slot = disabled ? "focus_box.disabled"
                                : (hilited ? "focus_box.hilited" : "focus_box.normal");
    const SkinImage *img = ap.art(slot);
    if (!img) img = ap.art("focus_box.normal");
    if (img) {
        cv.nine_slice(*img, r);
        return;
    }
    Color ring = disabled ? ap.c("primary.disable_frame") : ap.c("focus.box");
    cv.frame(r, ring);
    cv.frame({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ring);
    cv.frame({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, ring);
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
    Rect hsbar{};
    int page_rows = 1;
    int row_count = 8;
};

struct KitPreviewState {
    int pressed_btn = 0; // 0 none, 1 OK, 2 Cancel, 3 Pressed
    bool thumb_hot = false;
    bool h_thumb_hot = false;
    ScrollArrowHot arrow_hot = ScrollArrowHot::None;
    ScrollArrowHot h_arrow_hot = ScrollArrowHot::None;
    bool dropdown_open = false;
    int menu_hot = -1;
    int menu_sel = 0;
    int slider_value = 40;
    bool slider_hot = false;
    int h_scroll = 2;
    bool colours_only = false; // AppearanceEdit Colors Preview (hide images/icons)
};

// Full kit preview panel inside a gel client rect.
// All ink is clipped to `client` so nested gels / wide rows cannot spill the
// host window frame. Sections that no longer fit vertically are skipped.
inline KitPreviewLayout paint_kit_preview(Canvas &cv, const Appearance &ap_in,
                                          Rect client, bool caret_on,
                                          int list_sel, int scroll_val,
                                          const KitPreviewState &st = {}) {
    KitPreviewLayout lay;
    lay.bounds = client;
    CanvasClip panel_clip(cv, client);

    // Colours-only Preview (AppearanceEdit Colors Preview): hide images/icons.
    Appearance ap_colours;
    const Appearance *ap_ptr = &ap_in;
    if (st.colours_only) {
        ap_colours.skin = ap_in.skin;
        ap_colours.stock = ap_in.stock;
        // Leave art/icon caches empty → colour-path paint.
        ap_ptr = &ap_colours;
    }
    const Appearance &ap = *ap_ptr;

    int pad = 10;
    int x = client.x + pad;
    int y = client.y + pad;
    int w = client.w - 2 * pad;
    if (w < 40) w = 40;
    const int right = client.right() - pad;
    auto fits = [&](int need_h) { return y + need_h <= client.bottom() - pad; };

    cv.text(x, y, st.colours_only ? "Kit Preview (colours)" : "Kit Preview",
            ap.c("primary.label"));
    y += kFontHeight + 8;

    // Icon buttons + catalog strip + menu bar (one band).
    {
        const SkinImage *ib_hi = ap.art("icon_button.hilited");
        const SkinImage *ib_n = ap.art("icon_button.normal");
        int ib_w = 32;
        int ib_h = std::max(kKitButtonH, ib_n ? ib_n->h : kKitButtonH);
        if (ib_hi) {
            ib_w = std::max(ib_w, ib_hi->caps[0] + ib_hi->caps[2] + 2);
            ib_h = std::max(ib_h, ib_hi->h);
        } else if (ib_n) {
            ib_w = std::max(ib_w, ib_n->w);
        }
        int need = ib_h + 20 + kMenuBarH + 16;
        if (fits(need)) {
            Rect ib0{x, y, ib_w, ib_h};
            Rect ib1{ib0.right() + 6, y, ib_w, ib_h};
            Rect ib2{ib1.right() + 6, y, std::max(72, ib_w + 44), ib_h};
            if (ib0.right() <= right)
                paint_icon_button(cv, ap, ib0, "folder.16", nullptr, false, false);
            if (ib1.right() <= right)
                paint_icon_button(cv, ap, ib1, "file.generic.16", nullptr, true, false);
            if (ib2.right() <= right)
                paint_icon_button(cv, ap, ib2, "folder.16", "Open", false, false);
            Rect ibd{ib2.right() + 6, y, ib_w, ib_h};
            if (ibd.right() <= right)
                paint_icon_button(cv, ap, ibd, "user.16", nullptr, false, false);
            y += ib_h + 6;
            // High-occupancy catalog strip used in lists/menus/FT.
            static const char *strip[] = {
                "file.generic.16", "folder.16", "folder.uploads.16", "user.16",
                "document.text.16", "stop.16", "caution.16", "message.16"};
            int ix = x;
            for (const char *slot : strip) {
                if (ix + 18 > right) break;
                paint_icon(cv, ap, ix, y, slot, 16);
                ix += 20;
            }
            y += 20;
            static const char *bar_titles[] = {"File", "Edit", "View", "Help"};
            paint_menu_bar(cv, ap, {x, y, std::min(w, 320), kMenuBarH}, bar_titles, 4,
                           1, 0);
            y += kMenuBarH + 10;
        }
    }

    // Find dialog — TextEdit-measured gel + 24px buttons (not kKitButtonH).
    if (fits(kFindDlgH)) {
        paint_find_chrome_sample(cv, ap, x, y, w, caret_on);
        y += kFindDlgH + 10;
    }

    // Push buttons — AppearanceEdit usual 20px; default OK is +3px ring (26 outer).
    if (fits(kDefaultButtonH)) {
        constexpr int kBtnW = 72;
        constexpr int kBtnGap = 10;
        lay.btn_ok = default_button_rect(x, y, kBtnW);
        lay.btn_cancel = {lay.btn_ok.right() + kBtnGap, y, kBtnW, kKitButtonH};
        lay.btn_press = {lay.btn_cancel.right() + kBtnGap, y, kBtnW, kKitButtonH};
        Rect btn_dis{lay.btn_press.right() + kBtnGap, y, kBtnW, kKitButtonH};
        paint_button(cv, ap, lay.btn_ok, "OK", st.pressed_btn == 1, true);
        if (lay.btn_cancel.right() <= right)
            paint_button(cv, ap, lay.btn_cancel, "Cancel", st.pressed_btn == 2, false);
        if (lay.btn_press.right() <= right)
            paint_button(cv, ap, lay.btn_press, "Pressed", true, false);
        if (btn_dis.right() <= right)
            paint_button(cv, ap, btn_dis, "Disabled", false, false, true);
        y += kDefaultButtonH + 8;
    }

    // Tick / mutex / disclosure
    if (fits(kTickBox)) {
        paint_tick(cv, ap, x, y, TickMark::Ticked, "Tick on");
        if (x + 110 + 80 <= right)
            paint_tick(cv, ap, x + 110, y, TickMark::Blank, "Tick off");
        if (x + 220 + 40 <= right)
            paint_tick(cv, ap, x + 220, y, TickMark::Tristate, "Tri", false, false);
        if (x + 300 + 70 <= right)
            paint_mutex(cv, ap, x + 300, y, TickMark::Ticked, "Mutex A");
        if (x + 400 + 70 <= right)
            paint_mutex(cv, ap, x + 400, y, TickMark::Blank, "Mutex B");
        if (x + 528 + 12 <= right) {
            paint_disclosure(cv, ap, x + 510, y, DisclosureKind::PlusSmall);
            paint_disclosure(cv, ap, x + 528, y, DisclosureKind::MinusSmall);
        }
        y += kTickBox + 10;
    }

    // Progress + box/framed (one band)
    if (fits(std::max(kProgressH, 36) + 8)) {
        int row_h = std::max(kProgressH, 36);
        paint_progress(cv, ap, {x, y + (row_h - kProgressH) / 2,
                                std::min(w / 2, 180), kProgressH}, 65, 100);
        int bx = x + std::min(w / 2, 180) + 12;
        if (bx + 100 <= right) {
            paint_box(cv, ap, {bx, y, 100, 36}, "Box");
            if (bx + 210 <= right) {
                paint_framed_raised(cv, ap, {bx + 110, y, 100, 36});
                cv.text(bx + 120, y + 12, "Framed", ap.c("primary.label"));
            }
        }
        y += row_h + 10;
    }

    // KDX File Transfers — the gel + list + progress LED sample.
    int ftw = std::min(w, 450);
    int fth = 158;
    if (fits(fth)) {
        paint_file_transfers_window(cv, ap, {x, y, ftw, fth});
        y += fth + 8;
    }

    // Field + popup (AppearanceEdit: usually 20px)
    if (fits(kFieldH)) {
        int field_w = std::min(w, 280);
        if (field_w + kDropArrowW + 8 > w) field_w = std::max(80, w - kDropArrowW - 8);
        lay.field = {x, y, field_w, kFieldH};
        paint_field(cv, ap, lay.field, "Edit colour roles...", true, caret_on);
        Rect no_title{lay.field.right() + 6, y, kDropArrowW + 2, kFieldH};
        if (no_title.right() <= right)
            paint_dropdown(cv, ap, no_title, "", false, false, false, true);
        y += kFieldH + 8;
    }

    static const char *menu_items[] = {"Standard", "Slate", "-", "Custom...", "Disabled"};
    static const unsigned kMenuDisabled = 1u << 4;
    const char *drop_label = menu_items[std::clamp(st.menu_sel, 0, 4)];
    if (st.menu_sel == 2) drop_label = "Standard";
    if (fits(kKitButtonH)) {
        lay.dropdown = {x, y, std::min(w, 200), kKitButtonH};
        paint_dropdown(cv, ap, lay.dropdown, drop_label, st.dropdown_open,
                       st.pressed_btn == 4, false);
        Rect drop_dis{lay.dropdown.right() + 10, y,
                      std::min(120, right - (lay.dropdown.right() + 10)), kKitButtonH};
        if (drop_dis.w > 60)
            paint_dropdown(cv, ap, drop_dis, "None", false, false, true);
        y += kKitButtonH + 8;
    }

    // One H slider + disabled + one pointed V — exercise fill/travel/disabled.
    if (fits(48)) {
        int slide_w = std::min(std::max(80, w - 120), 160);
        lay.slider = {x, y, slide_w, 22};
        if (lay.slider.right() > right) lay.slider.w = std::max(40, right - lay.slider.x);
        lay.slider_lay =
            paint_slider(cv, ap, lay.slider, st.slider_value, 100, st.slider_hot);
        char sval[16];
        std::snprintf(sval, sizeof(sval), "%d", st.slider_value);
        if (lay.slider.right() + 28 <= right)
            cv.text(lay.slider.right() + 8, y + 3, sval, ap.c("primary.label"));
        Rect dis_slide{lay.slider.right() + 40, y, std::min(80, right - (lay.slider.right() + 40)),
                       22};
        if (dis_slide.w >= 40)
            paint_slider(cv, ap, dis_slide, 30, 100, false, false, true);
        Rect vslide{dis_slide.right() + 12, y - 2, 22, 48};
        if (vslide.right() <= right && vslide.bottom() <= client.bottom() - pad)
            paint_slider_v(cv, ap, vslide, st.slider_value, 100, true, true);
        y += 52;
    }

    // List + header + V/H scrollbars (KDX list chrome: H inside list bounds).
    static const char *rows[] = {"Row One", "Row Two", "Row Three", "Row Four",
                                 "Row Five", "Row Six", "Row Seven", "Row Eight"};
    static const char *row_icons[] = {
        "file.generic.16", "folder.16", "user.16", "document.text.16",
        "folder.uploads.16", "message.16", "hard_disk.16", "files.16"};
    lay.row_count = 8;
    int list_h = client.bottom() - y - pad;
    if (list_h >= kHeaderH + kRowH + kScrollbarW) {
        lay.list = {x, y, std::min(w, 320), list_h};
        int body_h = lay.list.h - kHeaderH - kScrollbarW;
        lay.page_rows = std::max(1, body_h / kRowH);
        int max_scroll = std::max(0, lay.row_count - lay.page_rows);
        if (scroll_val < 0) scroll_val = 0;
        if (scroll_val > max_scroll) scroll_val = max_scroll;

        Rect hdr{lay.list.x, lay.list.y, lay.list.w - kScrollbarW, kHeaderH};
        int split = hdr.w * 2 / 3;
        paint_column_header(cv, ap, {hdr.x, hdr.y, split, hdr.h}, "Name", true);
        paint_column_header(cv, ap,
                            {hdr.x + split, hdr.y, hdr.w - split, hdr.h}, "Off",
                            false, true);
        Rect body{lay.list.x, lay.list.y + kHeaderH, lay.list.w - kScrollbarW, body_h};
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
                Color ink = sel ? ap.c("list.hilite_foreground")
                                : file_label_color(ap, idx % 16);
                int iy = row.y + (kRowH - 16) / 2;
                paint_icon(cv, ap, row.x + 4, iy, row_icons[idx], 16);
                cv.text(row.x + 24, row.y + (kRowH - kFontHeight) / 2, rows[idx], ink);
            }
            cv.hline(row.x, row.right(), row.bottom() - 1, ap.c("list.separator"));
        }
        // V bar spans header+body (stops above H bar); H sits under body.
        lay.sbar = {lay.list.right() - kScrollbarW, lay.list.y, kScrollbarW,
                    lay.list.h - kScrollbarW};
        paint_scrollbar(cv, ap, lay.sbar, scroll_val, max_scroll, lay.page_rows,
                        st.thumb_hot, false, false, st.arrow_hot);

        lay.hsbar = {lay.list.x, lay.list.bottom() - kScrollbarW,
                     lay.list.w - kScrollbarW, kScrollbarW};
        int h_max = 8;
        int h_page = 4;
        int h_val = std::clamp(st.h_scroll, 0, h_max);
        paint_scrollbar_h(cv, ap, lay.hsbar, h_val, h_max, h_page, st.h_thumb_hot,
                          false, false, st.h_arrow_hot);

        // Corner between V and H (KDX list join).
        Rect corner{lay.hsbar.right(), lay.hsbar.y, kScrollbarW, kScrollbarW};
        cv.fill(corner, ap.c("scrollbar.face"));
        cv.frame(corner, ap.c("scrollbar.frame"));
    }

    // Open dropdown menu painted last so it stacks above the list (still clipped).
    if (st.dropdown_open && lay.dropdown.w > 0) {
        lay.menu_lay = paint_menu(cv, ap, lay.dropdown.x, lay.dropdown.bottom(),
                                  lay.dropdown.w, menu_items, 5, st.menu_hot,
                                  kMenuDisabled);
        lay.menu = lay.menu_lay.frame;
    }
    return lay;
}

