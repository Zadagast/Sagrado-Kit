// Kit multiline edit field — TextDoc + soft-wrap editor paint in a sunken rect.
// Single-line dialogs keep paint_field; compose / notes / Find pads use this.
// Paint / hit-test are host-agnostic; clipboard + WM_* key helpers are Win32.
#pragma once

#include "text_view.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

struct TextFieldState {
    TextDoc doc;
    std::vector<VisLine> lines;
    int scroll_y = 0; // first visible visual line
    bool focused = false;
    bool caret_on = true;
    bool dragging = false;
    int pad = 4;
};

inline void text_field_relayout(const Canvas &cv, TextFieldState &st, int wrap_w) {
    if (wrap_w < 8) wrap_w = 8;
    st.lines = layout_lines(cv, st.doc.text, wrap_w, true);
    if (st.lines.empty()) st.lines.push_back({0, 0});
}

inline int text_field_page(const TextFieldState &st, int inner_h, int lh) {
    return std::max(1, (inner_h - 2 * st.pad) / std::max(1, lh));
}

inline void text_field_clamp_scroll(TextFieldState &st, int page) {
    int max_y = std::max(0, (int)st.lines.size() - page);
    st.scroll_y = std::clamp(st.scroll_y, 0, max_y);
}

inline void text_field_ensure_caret(TextFieldState &st, int page) {
    int li = vis_index_at(st.lines, st.doc.caret);
    if (li < st.scroll_y) st.scroll_y = li;
    if (li >= st.scroll_y + page) st.scroll_y = li - page + 1;
    text_field_clamp_scroll(st, page);
}

inline size_t text_field_hit(const Canvas &cv, const TextFieldState &st, Rect inner,
                             int x, int y) {
    const int lh = cv.line_height();
    int row = (y - (inner.y + st.pad)) / std::max(1, lh) + st.scroll_y;
    int x_in = x - (inner.x + st.pad);
    return offset_at_xy(cv, st.lines, st.doc.text, row, x_in);
}

inline void text_field_move_caret(TextFieldState &st, size_t pos, bool extend,
                                  int page) {
    st.doc.caret = std::min(pos, st.doc.text.size());
    if (!extend) st.doc.anchor = st.doc.caret;
    text_field_ensure_caret(st, page);
}

inline void text_field_line_nav(TextFieldState &st, const Canvas &cv, int dir,
                                bool extend, int page) {
    int li = vis_index_at(st.lines, st.doc.caret);
    const VisLine &cur = st.lines[size_t(li)];
    int x = 0;
    for (size_t i = cur.start; i < st.doc.caret && i < cur.start + cur.len; ++i) {
        char t[2] = {st.doc.text[i], 0};
        x += cv.text_width(t);
    }
    int ni = std::clamp(li + dir, 0, (int)st.lines.size() - 1);
    text_field_move_caret(st, offset_at_xy(cv, st.lines, st.doc.text, ni, x),
                          extend, page);
}

inline std::string text_field_normalize_paste(std::string_view s) {
    std::string n;
    n.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') continue;
            n.push_back('\n');
        } else
            n.push_back(s[i]);
    }
    return n;
}

// Paint a sunken multiline field (field/list frame colours + editor body).
inline void paint_text_field(Canvas &cv, const Appearance &ap, Rect r,
                             TextFieldState &st, bool focused) {
    if (r.w <= 0 || r.h <= 0) return;
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

    Rect inner{r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    const int wrap = std::max(8, inner.w - 2 * st.pad);
    text_field_relayout(cv, st, wrap);
    const int lh = cv.line_height();
    const int page = text_field_page(st, inner.h, lh);
    text_field_clamp_scroll(st, page);

    TextEditorPaint ep;
    ep.r = inner;
    ep.pad = st.pad;
    ep.scroll_y = st.scroll_y;
    ep.focused = focused;
    ep.caret_on = st.caret_on;
    ep.show_caret = focused;
    paint_text_editor(cv, ap, st.doc, st.lines, ep);
}

inline bool text_field_mouse_down(TextFieldState &st, const Canvas &cv, Rect r,
                                  int x, int y, bool extend) {
    if (!r.contains(x, y)) return false;
    Rect inner{r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    const int wrap = std::max(8, inner.w - 2 * st.pad);
    text_field_relayout(cv, st, wrap);
    const int page = text_field_page(st, inner.h, cv.line_height());
    size_t off = text_field_hit(cv, st, inner, x, y);
    text_field_move_caret(st, off, extend, page);
    st.dragging = true;
    st.focused = true;
    return true;
}

inline bool text_field_mouse_move(TextFieldState &st, const Canvas &cv, Rect r,
                                  int x, int y) {
    if (!st.dragging) return false;
    Rect inner{r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    const int wrap = std::max(8, inner.w - 2 * st.pad);
    text_field_relayout(cv, st, wrap);
    const int page = text_field_page(st, inner.h, cv.line_height());
    size_t off = text_field_hit(cv, st, inner, x, y);
    text_field_move_caret(st, off, true, page);
    return true;
}

inline bool text_field_mouse_up(TextFieldState &st) {
    if (!st.dragging) return false;
    st.dragging = false;
    return true;
}

inline std::string text_field_trimmed(const TextFieldState &st) {
    const std::string &t = st.doc.text;
    size_t a = 0, b = t.size();
    while (a < b && (t[a] == ' ' || t[a] == '\t' || t[a] == '\n' || t[a] == '\r'))
        ++a;
    while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t' || t[b - 1] == '\n' ||
                     t[b - 1] == '\r'))
        --b;
    return t.substr(a, b - a);
}

#ifdef _WIN32
#include "clipboard.h"

// Clipboard helpers for the field (kit CF_TEXT path).
inline void text_field_copy(HWND hwnd, const TextFieldState &st) {
    if (!st.doc.has_sel()) return;
    sagrado::clipboard_set(hwnd, st.doc.selected());
}

inline void text_field_cut(HWND hwnd, TextFieldState &st) {
    if (!st.doc.has_sel()) return;
    text_field_copy(hwnd, st);
    st.doc.replace_range(st.doc.sel_lo(), st.doc.sel_hi(), "");
}

inline void text_field_paste(HWND hwnd, TextFieldState &st) {
    std::string n = text_field_normalize_paste(sagrado::clipboard_get(hwnd));
    if (!n.empty()) st.doc.insert(n);
}

// WM_KEYDOWN handler. When `enter_sends` is true, plain Enter is left to the
// app (return false); Shift+Enter inserts a newline. Otherwise Enter inserts
// a newline. Returns true if the key was consumed.
inline bool text_field_keydown(TextFieldState &st, Canvas &cv, HWND hwnd,
                               WPARAM vk, bool shift, bool ctrl, Rect r,
                               bool enter_sends) {
    if (!st.focused) return false;
    Rect inner{r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    const int wrap = std::max(8, inner.w - 2 * st.pad);
    text_field_relayout(cv, st, wrap);
    const int page = text_field_page(st, inner.h, cv.line_height());

    if (ctrl) {
        if (vk == 'A') {
            st.doc.select_all();
            return true;
        }
        if (vk == 'C') {
            text_field_copy(hwnd, st);
            return true;
        }
        if (vk == 'X') {
            text_field_cut(hwnd, st);
            text_field_ensure_caret(st, page);
            return true;
        }
        if (vk == 'V') {
            text_field_paste(hwnd, st);
            text_field_relayout(cv, st, wrap);
            text_field_ensure_caret(st, page);
            return true;
        }
        if (vk == 'Z') {
            st.doc.undo();
            text_field_relayout(cv, st, wrap);
            text_field_ensure_caret(st, page);
            return true;
        }
    }

    if (vk == VK_LEFT) {
        if (st.doc.caret > 0)
            text_field_move_caret(st, st.doc.caret - 1, shift, page);
        return true;
    }
    if (vk == VK_RIGHT) {
        if (st.doc.caret < st.doc.text.size())
            text_field_move_caret(st, st.doc.caret + 1, shift, page);
        return true;
    }
    if (vk == VK_UP) {
        text_field_line_nav(st, cv, -1, shift, page);
        return true;
    }
    if (vk == VK_DOWN) {
        text_field_line_nav(st, cv, 1, shift, page);
        return true;
    }
    if (vk == VK_HOME) {
        int li = vis_index_at(st.lines, st.doc.caret);
        text_field_move_caret(st, st.lines[size_t(li)].start, shift, page);
        return true;
    }
    if (vk == VK_END) {
        int li = vis_index_at(st.lines, st.doc.caret);
        const VisLine &vl = st.lines[size_t(li)];
        text_field_move_caret(st, vl.start + vl.len, shift, page);
        return true;
    }
    if (vk == VK_DELETE) {
        st.doc.del_forward();
        text_field_relayout(cv, st, wrap);
        text_field_ensure_caret(st, page);
        return true;
    }
    if (vk == VK_RETURN || vk == VK_SEPARATOR) {
        if (enter_sends && !shift) return false; // app Send
        st.doc.insert("\n");
        text_field_relayout(cv, st, wrap);
        text_field_ensure_caret(st, page);
        return true;
    }
    if (vk == VK_BACK) {
        st.doc.backspace();
        text_field_relayout(cv, st, wrap);
        text_field_ensure_caret(st, page);
        return true;
    }
    return false;
}

// WM_CHAR for printable. Backspace / Delete / Enter / shortcuts belong to
// text_field_keydown so they are not applied twice.
inline bool text_field_char(TextFieldState &st, Canvas &cv, Rect r, WPARAM ch,
                            bool enter_sends) {
    if (!st.focused) return false;
    if (ch == '\r' || ch == '\n' || ch == 8) {
        (void)enter_sends;
        return false; // keydown owns these
    }
    if (ch < 32 || ch > 126) return false;
    Rect inner{r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    const int wrap = std::max(8, inner.w - 2 * st.pad);
    char t[2] = {char(ch), 0};
    st.doc.insert(t);
    text_field_relayout(cv, st, wrap);
    text_field_ensure_caret(st, text_field_page(st, inner.h, cv.line_height()));
    return true;
}
#endif // _WIN32
