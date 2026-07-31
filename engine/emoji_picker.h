// Sagrado Emoji Picker — Ubuntu Characters–style IA, gel chrome.
// Host paints into its Canvas (overlay or HWND). Pack lives on disk.
#pragma once

#include "appearance.h"
#include "emoji_decode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace sagrado {

inline constexpr int kEmojiPickerCols = 6;
inline constexpr int kEmojiCell = 56;
inline constexpr int kEmojiGlyph48 = 48;
inline constexpr int kEmojiGlyph32 = 32;
inline constexpr int kEmojiNavW = 140;
inline constexpr int kEmojiCacheMax = 256;

struct EmojiEntry {
    std::string seq;  // "1F600" / "2764-FE0F"
    std::string cat;
    std::string name;
    std::string wire; // UTF-8
};

struct EmojiCategory {
    std::string id;
    std::string label;
    std::string icon_seq;
};

struct EmojiPickerState {
    std::string query;
    int category = 0; // 0 = Recents, 1.. = categories
    int scroll = 0;
    int hot_cell = -1;
    int pressed_cell = -1;
    int hot_nav = -1;
    bool search_focus = true;
    std::vector<std::string> recent; // wire emoji, newest first
};

enum class EmojiPickerHitKind {
    None = 0,
    Cell,
    Nav,
    Search,
    Cancel,
    Close,
    Sbar,
};

struct EmojiPickerHit {
    EmojiPickerHitKind kind = EmojiPickerHitKind::None;
    int index = -1; // cell index in filtered list, or nav index
};

struct EmojiPickerLayout {
    Rect gel{};
    GelLayout gel_lay{};
    Rect search{};
    Rect nav{};
    Rect title{};
    Rect grid{};
    Rect sbar{};
    Rect btn_cancel{};
    int grid_page = 1;
    int grid_max = 0;
    int filtered_count = 0;
    // Visible cells for hit-test (index into filtered list).
    struct VisCell {
        Rect r;
        int index = -1;
    };
    std::vector<VisCell> cells;
    std::vector<Rect> nav_rows;
};

// --- UTF-8 helpers -----------------------------------------------------------

inline void append_utf8(std::string &o, unsigned cp) {
    if (cp < 0x80) {
        o.push_back(char(cp));
    } else if (cp < 0x800) {
        o.push_back(char(0xC0 | (cp >> 6)));
        o.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        o.push_back(char(0xE0 | (cp >> 12)));
        o.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        o.push_back(char(0xF0 | (cp >> 18)));
        o.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        o.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(char(0x80 | (cp & 0x3F)));
    }
}

inline std::string seq_to_utf8(const std::string &seq) {
    std::string o;
    size_t i = 0;
    while (i < seq.size()) {
        while (i < seq.size() && (seq[i] == '-' || seq[i] == ' ')) ++i;
        if (i >= seq.size()) break;
        unsigned cp = 0;
        while (i < seq.size() && seq[i] != '-' && seq[i] != ' ') {
            char c = seq[i++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= unsigned(c - '0');
            else if (c >= 'A' && c <= 'F') cp |= unsigned(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') cp |= unsigned(c - 'a' + 10);
        }
        if (cp) append_utf8(o, cp);
    }
    return o;
}

inline std::string utf8_to_seq(const std::string &wire) {
    std::string out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(wire.data());
    const unsigned char *e = p + wire.size();
    while (p < e) {
        unsigned cp = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0 && p + 1 < e) {
            cp = (unsigned(*p & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p + 2 < e) {
            cp = (unsigned(*p & 0x0F) << 12) | (unsigned(p[1] & 0x3F) << 6) |
                 (p[2] & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p + 3 < e) {
            cp = (unsigned(*p & 0x07) << 18) | (unsigned(p[1] & 0x3F) << 12) |
                 (unsigned(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            p += 4;
        } else {
            ++p;
            continue;
        }
        if (!out.empty()) out.push_back('-');
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%X", cp);
        out += buf;
    }
    return out;
}

// --- Pack catalog ------------------------------------------------------------

struct EmojiPack {
    std::string root;
    std::vector<EmojiCategory> cats;
    std::vector<EmojiEntry> entries;
    std::unordered_map<std::string, int> wire_to_index;
    bool loaded = false;
};

inline EmojiPack &emoji_pack() {
    static EmojiPack p;
    return p;
}

inline bool emoji_pack_set_root(const std::string &dir) {
    EmojiPack &p = emoji_pack();
    p = EmojiPack{};
    p.root = dir;
    while (!p.root.empty() && (p.root.back() == '/' || p.root.back() == '\\'))
        p.root.pop_back();
    std::string cat_path = p.root + "/catalog.txt";
    std::ifstream in(cat_path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("CAT ", 0) == 0) {
            // CAT id Label icon_seq  (label is single token)
            char id[64] = {}, label[64] = {}, icon[64] = {};
            if (std::sscanf(line.c_str(), "CAT %63s %63s %63s", id, label, icon) >=
                2) {
                EmojiCategory c;
                c.id = id;
                c.label = label;
                c.icon_seq = icon;
                p.cats.push_back(std::move(c));
            }
        } else if (line.rfind("EMOJI ", 0) == 0) {
            // EMOJI seq cat name with spaces...
            size_t a = 6;
            size_t sp1 = line.find(' ', a);
            if (sp1 == std::string::npos) continue;
            size_t sp2 = line.find(' ', sp1 + 1);
            if (sp2 == std::string::npos) continue;
            EmojiEntry e;
            e.seq = line.substr(a, sp1 - a);
            e.cat = line.substr(sp1 + 1, sp2 - sp1 - 1);
            e.name = line.substr(sp2 + 1);
            e.wire = seq_to_utf8(e.seq);
            if (e.wire.empty()) continue;
            p.wire_to_index[e.wire] = int(p.entries.size());
            p.entries.push_back(std::move(e));
        }
    }
    p.loaded = !p.entries.empty();
    return p.loaded;
}

inline bool emoji_pack_ready() { return emoji_pack().loaded; }

// --- Icon cache --------------------------------------------------------------

struct EmojiCache {
    std::unordered_map<std::string, SkinImage> images;
    std::list<std::string> order;
};

inline EmojiCache &emoji_cache() {
    static EmojiCache c;
    return c;
}

inline const SkinImage *emoji_icon_by_seq(const std::string &seq, int px) {
    if (seq.empty() || !emoji_pack().loaded) return nullptr;
    if (px != kEmojiGlyph32 && px != kEmojiGlyph48) px = kEmojiGlyph48;
    std::string key = std::to_string(px) + ":" + seq;
    EmojiCache &c = emoji_cache();
    auto it = c.images.find(key);
    if (it != c.images.end()) {
        c.order.remove(key);
        c.order.push_front(key);
        return &it->second;
    }
    std::string folder = px == kEmojiGlyph32 ? "/png32/" : "/png48/";
    std::string path = emoji_pack().root + folder + seq + ".png";
    SkinImage img;
    if (!decode_png_file(path.c_str(), img) || img.empty()) return nullptr;
    while (int(c.images.size()) >= kEmojiCacheMax && !c.order.empty()) {
        c.images.erase(c.order.back());
        c.order.pop_back();
    }
    c.order.push_front(key);
    auto &slot = c.images[key];
    slot = std::move(img);
    return &slot;
}

inline const SkinImage *emoji_icon(const std::string &wire, int px) {
    if (wire.empty()) return nullptr;
    auto it = emoji_pack().wire_to_index.find(wire);
    if (it == emoji_pack().wire_to_index.end()) {
        // Try seq form
        return emoji_icon_by_seq(utf8_to_seq(wire), px);
    }
    return emoji_icon_by_seq(emoji_pack().entries[size_t(it->second)].seq, px);
}

inline void emoji_recent_push(EmojiPickerState &st, const std::string &wire) {
    if (wire.empty()) return;
    st.recent.erase(std::remove(st.recent.begin(), st.recent.end(), wire),
                    st.recent.end());
    st.recent.insert(st.recent.begin(), wire);
    if (st.recent.size() > 48) st.recent.resize(48);
}

// --- Filter ------------------------------------------------------------------

inline std::string ascii_lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

inline std::vector<int> emoji_filtered_indices(const EmojiPickerState &st) {
    std::vector<int> out;
    const EmojiPack &p = emoji_pack();
    if (!p.loaded) return out;
    std::string q = ascii_lower(st.query);
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back() == ' ') q.pop_back();

    if (!q.empty()) {
        for (int i = 0; i < int(p.entries.size()); ++i) {
            if (p.entries[size_t(i)].name.find(q) != std::string::npos)
                out.push_back(i);
        }
        return out;
    }

    if (st.category <= 0) {
        // Recents — map wire → entry index when known
        for (const auto &w : st.recent) {
            auto it = p.wire_to_index.find(w);
            if (it != p.wire_to_index.end()) out.push_back(it->second);
        }
        return out;
    }

    int ci = st.category - 1;
    if (ci < 0 || ci >= int(p.cats.size())) return out;
    const std::string &cid = p.cats[size_t(ci)].id;
    for (int i = 0; i < int(p.entries.size()); ++i)
        if (p.entries[size_t(i)].cat == cid) out.push_back(i);
    return out;
}

inline void emoji_picker_size(int *dw, int *dh) {
    if (dw) *dw = 520;
    if (dh) *dh = 400;
}

inline const char *emoji_nav_label(int nav) {
    if (nav == 0) return "Recents";
    const EmojiPack &p = emoji_pack();
    int ci = nav - 1;
    if (ci >= 0 && ci < int(p.cats.size())) return p.cats[size_t(ci)].label.c_str();
    return "";
}

inline int emoji_nav_count() { return 1 + int(emoji_pack().cats.size()); }

// --- Paint -------------------------------------------------------------------

inline EmojiPickerLayout paint_emoji_picker(
    Canvas &cv, const Appearance &ap, Rect box, EmojiPickerState &st,
    bool focused, bool sbar_thumb_hot = false,
    ScrollArrowHot sbar_arrow_hot = ScrollArrowHot::None) {
    EmojiPickerLayout lay;
    lay.gel = box;
    lay.gel_lay =
        gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &ap, focused);
    paint_gel(cv, ap, box, "Emoji", focused, 0, GelStyle::Dialog);

    Rect cl = lay.gel_lay.client;
    const int pad = 10;
    const int lh = cv.line_height();
    int y = cl.y + pad;

    // Search
    lay.search = {cl.x + pad, y, cl.w - 2 * pad, 24};
    paint_field(cv, ap, lay.search, st.query.c_str(), st.search_focus, true);
    y = lay.search.bottom() + 8;

    // Cancel row reserved
    int footer_h = 36;
    int body_bottom = cl.bottom() - footer_h;
    lay.btn_cancel = {cl.right() - pad - 70, cl.bottom() - 32, 70, 26};

    // Nav rail — row height fits all categories without overlap; icons scale down.
    lay.nav = {cl.x + pad, y, kEmojiNavW, body_bottom - y};
    cv.fill(lay.nav, ap.c("list.background"));
    cv.frame(lay.nav, ap.c("primary.frame"));

    int nav_n = emoji_nav_count();
    int avail_h = std::max(0, lay.nav.h - 4);
    int row_h = lh + 8;
    if (nav_n > 0)
        row_h = std::min(row_h, std::max(lh + 2, avail_h / nav_n));
    if (row_h < lh + 2) row_h = lh + 2;
    int icon_px = std::min(20, std::max(12, row_h - 4));
    lay.nav_rows.clear();
    {
        CanvasClip clip(cv, lay.nav);
        for (int i = 0; i < nav_n; ++i) {
            Rect row{lay.nav.x + 2, lay.nav.y + 2 + i * row_h, lay.nav.w - 4,
                     row_h};
            lay.nav_rows.push_back(row);
            bool sel = (st.query.empty() && st.category == i);
            bool hot = (st.hot_nav == i);
            if (sel)
                cv.fill(row, ap.c("list.hilite_background"));
            else if (hot)
                cv.fill(row, ap.c("primary.light"));
            Color ink = sel ? ap.c("list.hilite_foreground")
                            : label_ink(ap, ap.c("list.label"));
            int tx = row.x + 6;
            if (i > 0) {
                int ci = i - 1;
                if (ci >= 0 && ci < int(emoji_pack().cats.size())) {
                    const SkinImage *ic = emoji_icon_by_seq(
                        emoji_pack().cats[size_t(ci)].icon_seq, kEmojiGlyph32);
                    if (ic && !ic->empty()) {
                        int iy = row.y + (row.h - icon_px) / 2;
                        cv.blit_image_scaled(*ic, tx, iy, icon_px, icon_px);
                        tx += icon_px + 6;
                    }
                }
            }
            const char *lab = emoji_nav_label(i);
            cv.text_elided(tx, label_y_centered(cv, row, lab), lab,
                           row.right() - tx - 4, ink);
        }
    }

    // Main grid area
    int gx = lay.nav.right() + 8;
    lay.title = {gx, y, cl.right() - pad - gx, lh + 2};
    const char *title = "Search";
    if (st.query.empty()) {
        if (st.category == 0) title = "Recently Used";
        else title = emoji_nav_label(st.category);
    }
    cv.text(lay.title.x, lay.title.y, title, label_ink(ap, ap.c("primary.label")));

    lay.grid = {gx, lay.title.bottom() + 4, cl.right() - pad - gx,
                body_bottom - (lay.title.bottom() + 4)};
    cv.fill(lay.grid, ap.c("list.background"));
    cv.frame(lay.grid, ap.c("primary.frame"));

    auto filtered = emoji_filtered_indices(st);
    lay.filtered_count = int(filtered.size());

    int cols = kEmojiPickerCols;
    int cell = kEmojiCell;
    int inner_pad = 4;
    int content_h = 0;
    if (!filtered.empty()) {
        int rows = (int(filtered.size()) + cols - 1) / cols;
        content_h = rows * cell + 2 * inner_pad;
    }
    lay.grid_page = std::max(1, lay.grid.h - 2);
    lay.grid_max = std::max(0, content_h - lay.grid_page);
    st.scroll = std::clamp(st.scroll, 0, lay.grid_max);

    lay.sbar = {};
    Rect grid_body = lay.grid;
    if (lay.grid_max > 0 && lay.grid.w > kScrollbarW + 40) {
        lay.sbar = {lay.grid.right() - kScrollbarW, lay.grid.y, kScrollbarW,
                    lay.grid.h};
        grid_body.w -= kScrollbarW;
    }

    lay.cells.clear();
    {
        CanvasClip clip(cv, grid_body);
        int usable_w = grid_body.w - 2 * inner_pad;
        int gap = 0;
        if (cols > 1)
            gap = std::max(0, (usable_w - cols * cell) / (cols - 1));
        int x0 = grid_body.x + inner_pad;
        int y0 = grid_body.y + inner_pad - st.scroll;
        for (int i = 0; i < int(filtered.size()); ++i) {
            int col = i % cols;
            int row = i / cols;
            Rect cell_r{x0 + col * (cell + gap), y0 + row * cell, cell, cell};
            if (cell_r.bottom() < grid_body.y || cell_r.y > grid_body.bottom())
                continue;
            lay.cells.push_back({cell_r, i});
            bool hot = (st.hot_cell == i);
            bool pressed = (st.pressed_cell == i);
            if (pressed)
                cv.fill(cell_r, ap.c("list.hilite_background"));
            else if (hot)
                cv.fill(cell_r, ap.c("primary.light"));
            int ei = filtered[size_t(i)];
            const SkinImage *ic =
                emoji_icon_by_seq(emoji_pack().entries[size_t(ei)].seq,
                                  kEmojiGlyph48);
            if (ic && !ic->empty()) {
                int ix = cell_r.x + (cell_r.w - ic->w) / 2;
                int iy = cell_r.y + (cell_r.h - ic->h) / 2;
                cv.blit_image(*ic, ix, iy);
            }
        }
    }

    if (lay.sbar.w > 0)
        paint_scrollbar(cv, ap, lay.sbar, st.scroll, lay.grid_max, lay.grid_page,
                        sbar_thumb_hot, false, false, sbar_arrow_hot);

    if (filtered.empty()) {
        const char *empty = st.category == 0 && st.query.empty()
                                ? "No recent emoji yet"
                                : "No matches";
        cv.text(grid_body.x + 12, grid_body.y + 12, empty,
                ap.c("menu.disable_label"));
    }

    paint_button(cv, ap, lay.btn_cancel, "Cancel", false, false);
    return lay;
}

inline EmojiPickerHit emoji_picker_hit(const EmojiPickerLayout &lay, int x,
                                       int y) {
    EmojiPickerHit h;
    if (lay.gel_lay.close_box.w > 0 && lay.gel_lay.close_box.contains(x, y)) {
        h.kind = EmojiPickerHitKind::Close;
        return h;
    }
    if (lay.btn_cancel.contains(x, y)) {
        h.kind = EmojiPickerHitKind::Cancel;
        return h;
    }
    if (lay.search.contains(x, y)) {
        h.kind = EmojiPickerHitKind::Search;
        return h;
    }
    if (lay.sbar.w > 0 && lay.sbar.contains(x, y)) {
        h.kind = EmojiPickerHitKind::Sbar;
        return h;
    }
    for (int i = 0; i < int(lay.nav_rows.size()); ++i) {
        if (lay.nav_rows[size_t(i)].contains(x, y)) {
            h.kind = EmojiPickerHitKind::Nav;
            h.index = i;
            return h;
        }
    }
    for (const auto &c : lay.cells) {
        if (c.r.contains(x, y)) {
            h.kind = EmojiPickerHitKind::Cell;
            h.index = c.index;
            return h;
        }
    }
    return h;
}

// Wire emoji for filtered cell index (or empty).
inline std::string emoji_picker_wire_at(const EmojiPickerState &st, int filt_i) {
    auto filtered = emoji_filtered_indices(st);
    if (filt_i < 0 || filt_i >= int(filtered.size())) return {};
    int ei = filtered[size_t(filt_i)];
    return emoji_pack().entries[size_t(ei)].wire;
}

// Transcript reaction marks — returns pixels advanced in y.
inline int paint_emoji_marks(Canvas &cv, int x, int y, int row_h,
                             const std::vector<std::pair<std::string, int>> &marks,
                             Color count_ink, const std::vector<bool> &mine_flags) {
    if (marks.empty()) return 0;
    const int lh = cv.line_height();
    int px = x;
    for (size_t i = 0; i < marks.size(); ++i) {
        const SkinImage *ic = emoji_icon(marks[i].first, kEmojiGlyph32);
        if (ic && !ic->empty()) {
            int iy = y + (row_h - ic->h) / 2;
            cv.blit_image(*ic, px, iy);
            px += ic->w + 2;
        }
        if (marks[i].second > 1 || (i < mine_flags.size() && mine_flags[i])) {
            std::string tail;
            if (marks[i].second > 1) tail += std::to_string(marks[i].second);
            if (i < mine_flags.size() && mine_flags[i]) tail += "*";
            cv.text(px, y + (row_h - lh) / 2, tail.c_str(), count_ink);
            px += cv.text_width(tail.c_str()) + 8;
        } else {
            px += 8;
        }
    }
    return row_h;
}

} // namespace sagrado
