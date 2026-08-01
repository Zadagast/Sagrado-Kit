// Kit plain-text document — caret, selection, one-level undo.
// Win32-free; apps own file path / Find / Sort Lines.
#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

struct TextDoc {
    std::string text;
    size_t caret = 0;
    size_t anchor = 0; // selection other end; equal → no selection
    bool dirty = false;
    std::string undo_text;
    size_t undo_caret = 0;

    size_t sel_lo() const { return std::min(caret, anchor); }
    size_t sel_hi() const { return std::max(caret, anchor); }
    bool has_sel() const { return caret != anchor; }

    void push_undo() {
        undo_text = text;
        undo_caret = caret;
    }

    void undo() {
        if (undo_text == text && undo_caret == caret) return;
        std::string cur = text;
        size_t cc = caret;
        text = undo_text;
        caret = anchor = undo_caret;
        undo_text = cur;
        undo_caret = cc;
        dirty = true;
    }

    void replace_range(size_t lo, size_t hi, const std::string &ins) {
        if (lo > text.size()) lo = text.size();
        if (hi > text.size()) hi = text.size();
        if (lo > hi) std::swap(lo, hi);
        push_undo();
        text.replace(lo, hi - lo, ins);
        caret = anchor = lo + ins.size();
        dirty = true;
    }

    void insert(const std::string &s) {
        if (has_sel()) replace_range(sel_lo(), sel_hi(), s);
        else replace_range(caret, caret, s);
    }

    // Caret steps land on UTF-8 boundaries so editing never splits a
    // multi-byte character (emoji included) into broken bytes.
    size_t prev_pos(size_t i) const {
        if (i == 0) return 0;
        --i;
        while (i > 0 && ((unsigned char)text[i] & 0xc0) == 0x80) --i;
        return i;
    }

    size_t next_pos(size_t i) const {
        if (i >= text.size()) return text.size();
        ++i;
        while (i < text.size() && ((unsigned char)text[i] & 0xc0) == 0x80) ++i;
        return i;
    }

    void backspace() {
        if (has_sel()) {
            replace_range(sel_lo(), sel_hi(), "");
            return;
        }
        if (caret == 0) return;
        replace_range(prev_pos(caret), caret, "");
    }

    void del_forward() {
        if (has_sel()) {
            replace_range(sel_lo(), sel_hi(), "");
            return;
        }
        if (caret >= text.size()) return;
        replace_range(caret, next_pos(caret), "");
    }

    void select_all() {
        anchor = 0;
        caret = text.size();
    }

    std::string selected() const {
        if (!has_sel()) return {};
        return text.substr(sel_lo(), sel_hi() - sel_lo());
    }
};
