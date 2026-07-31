// Kit context menu — paint_menu + menu_place; apps own items and actions.
// Window-space coordinates. Never TrackPopupMenu for in-window chrome.
#pragma once

#include "appearance.h"

#include <cstring>
#include <vector>

struct ContextMenuItem {
    const char *label = "";
    bool enabled = true;
    bool sep = false;
};

struct ContextMenuState {
    bool open = false;
    int x = 0, y = 0; // click point in window space
    int hot = -1;
    std::vector<ContextMenuItem> items;
    MenuLayout lay{};
    // Labels fed to paint_menu ("-" for separators).
    std::vector<const char *> paint_labels;
    unsigned disabled_mask = 0;
};

inline void context_menu_close(ContextMenuState &st) {
    st.open = false;
    st.hot = -1;
    st.lay = {};
    st.paint_labels.clear();
    st.disabled_mask = 0;
}

inline void context_menu_open(ContextMenuState &st, int x, int y,
                              const ContextMenuItem *items, int count) {
    st.open = true;
    st.x = x;
    st.y = y;
    st.hot = -1;
    st.items.assign(items, items + count);
    st.paint_labels.clear();
    st.paint_labels.reserve(size_t(count));
    st.disabled_mask = 0;
    for (int i = 0; i < count; ++i) {
        if (st.items[size_t(i)].sep ||
            (st.items[size_t(i)].label &&
             (std::strcmp(st.items[size_t(i)].label, "-") == 0))) {
            st.paint_labels.push_back("-");
        } else {
            st.paint_labels.push_back(st.items[size_t(i)].label
                                          ? st.items[size_t(i)].label
                                          : "");
            if (!st.items[size_t(i)].enabled && i < 32)
                st.disabled_mask |= (1u << i);
        }
    }
    st.lay = {};
}

inline void context_menu_open(ContextMenuState &st, int x, int y,
                              const std::vector<ContextMenuItem> &items) {
    if (items.empty()) {
        context_menu_close(st);
        return;
    }
    context_menu_open(st, x, y, items.data(), (int)items.size());
}

// Place + paint. Updates st.lay for hit-testing. Returns the menu layout.
inline MenuLayout paint_context_menu(Canvas &cv, const Appearance &ap,
                                     Rect window_bounds, ContextMenuState &st) {
    if (!st.open || st.paint_labels.empty()) {
        st.lay = {};
        return st.lay;
    }
    const int count = (int)st.paint_labels.size();
    int mw = 72;
    for (int i = 0; i < count; ++i)
        mw = std::max(mw, cv.text_width(st.paint_labels[size_t(i)]) + 28);
    int mh = menu_estimate_h(count);
    // Anchor is a 1×1 at the click so menu_place can flip/clamp inside gel.
    Rect anchor{st.x, st.y, 1, 1};
    int mx = 0, my = 0;
    menu_place(window_bounds, anchor, mw, mh, &mx, &my);
    st.lay = paint_menu(cv, ap, mx, my, mw, st.paint_labels.data(), count,
                        st.hot, st.disabled_mask);
    return st.lay;
}

inline int context_menu_hit(const ContextMenuState &st, int mx, int my) {
    if (!st.open) return -1;
    return menu_hit_row(st.lay, mx, my);
}

// True when (mx,my) is on the open menu frame (including padding).
inline bool context_menu_contains(const ContextMenuState &st, int mx, int my) {
    if (!st.open) return false;
    return st.lay.frame.contains(mx, my);
}
