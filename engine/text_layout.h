// Kit text layout — soft/hard wrap shared by TextEdit, Jabber, alerts.
// Apps must not ship a private wrap loop; call these helpers.
#pragma once

#include "canvas.h"

#include <algorithm>
#include <string>
#include <vector>

struct VisLine {
    size_t start = 0;
    size_t len = 0;
};

// Soft-wrap (or hard) visual lines for a given pixel width.
inline std::vector<VisLine> layout_lines(const Canvas &cv, const std::string &text,
                                         int wrap_w, bool soft_wrap) {
    std::vector<VisLine> out;
    if (wrap_w < 8) wrap_w = 8;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n || (i == n && out.empty())) {
        if (i >= n) {
            out.push_back({i, 0});
            break;
        }
        size_t line_end = text.find('\n', i);
        if (line_end == std::string::npos) line_end = n;
        size_t para = line_end; // exclusive content end (before \n)
        size_t content_end = para;
        if (content_end > i && text[content_end - 1] == '\r') --content_end;

        if (!soft_wrap || wrap_w <= 0) {
            out.push_back({i, content_end - i});
            i = (para < n) ? para + 1 : n;
            if (i >= n && para < n) out.push_back({n, 0});
            continue;
        }

        size_t pos = i;
        while (pos < content_end) {
            size_t start = pos;
            int w = 0;
            size_t last_break = start;
            size_t p = start;
            // Advance by drawable units so multi-byte runs (emoji) are never
            // split across visual lines or measured a byte at a time.
            auto unit_at = [&](size_t i) {
                int n = cv.unit_len(text.c_str() + i);
                if (n <= 0) n = 1;
                if (i + size_t(n) > content_end) n = int(content_end - i);
                return size_t(n);
            };
            while (p < content_end) {
                char ch = text[p];
                size_t ul = unit_at(p);
                int aw = cv.text_width(text.substr(p, ul).c_str());
                if (w + aw > wrap_w && p > start) break;
                w += aw;
                p += ul;
                if (ch == ' ' || ch == '\t') last_break = p;
            }
            if (p < content_end && last_break > start) p = last_break;
            if (p == start) p = start + unit_at(start);
            out.push_back({start, p - start});
            pos = p;
            while (pos < content_end && text[pos] == ' ') ++pos;
        }
        if (pos == i) out.push_back({i, 0});
        i = (para < n) ? para + 1 : n;
        if (i >= n && para < n) out.push_back({n, 0});
    }
    if (out.empty()) out.push_back({0, 0});
    return out;
}

inline int vis_index_at(const std::vector<VisLine> &lines, size_t offset) {
    for (int i = 0; i < (int)lines.size(); ++i) {
        size_t end = lines[i].start + lines[i].len;
        if (offset < end || (offset == end && i + 1 == (int)lines.size()))
            return i;
        if (offset == end && i + 1 < (int)lines.size() && lines[i + 1].start > end)
            return i;
        if (offset <= lines[i].start) return i;
    }
    return (int)lines.size() - 1;
}

inline size_t offset_at_xy(const Canvas &cv, const std::vector<VisLine> &lines,
                           const std::string &text, int line, int x_in_line) {
    if (lines.empty()) return 0;
    line = std::clamp(line, 0, (int)lines.size() - 1);
    const VisLine &vl = lines[size_t(line)];
    int x = 0;
    for (size_t i = 0; i < vl.len;) {
        int n = cv.unit_len(text.c_str() + vl.start + i);
        if (n <= 0) n = 1;
        if (i + size_t(n) > vl.len) n = int(vl.len - i);
        int aw = cv.text_width(text.substr(vl.start + i, size_t(n)).c_str());
        if (x + aw / 2 >= x_in_line) return vl.start + i;
        x += aw;
        i += size_t(n);
    }
    return vl.start + vl.len;
}

inline int text_content_height(const std::vector<VisLine> &lines, int line_height) {
    return (int)lines.size() * std::max(1, line_height);
}
