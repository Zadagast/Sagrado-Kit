// Shared row maths for scrolling lists (Kit first: apps call this rather
// than looping every row). Canvas clips off-screen drawing, but a long list
// still pays per-row layout cost unless the loop itself is bounded.
#pragma once

#include "canvas.h"

namespace sagrado {

// Half-open [first, last) range of fixed-height rows that intersect `view`
// at the given pixel scroll offset. `count` is the total number of rows.
struct RowRange {
    int first = 0;
    int last = 0;
    int y = 0; // top of `first` in canvas coordinates
};

inline RowRange visible_rows(Rect view, int scroll, int row_h, int count,
                             int top_pad = 2) {
    RowRange r;
    if (row_h <= 0 || count <= 0 || view.h <= 0) return r;
    int origin = view.y + top_pad - scroll;
    int first = (view.y - origin) / row_h;
    if (first < 0) first = 0;
    if (first > count) first = count;
    int last = (view.bottom() - origin) / row_h + 1;
    if (last < first) last = first;
    if (last > count) last = count;
    r.first = first;
    r.last = last;
    r.y = origin + first * row_h;
    return r;
}

// Row index under a click, or -1 when the point misses the list content.
inline int row_at(Rect view, int scroll, int row_h, int count, int y,
                  int top_pad = 2) {
    if (row_h <= 0 || count <= 0) return -1;
    int idx = (y - (view.y + top_pad - scroll)) / row_h;
    if (y < view.y + top_pad - scroll) return -1;
    return (idx >= 0 && idx < count) ? idx : -1;
}

} // namespace sagrado
