// Kit list row maths + off-clip draw culling (long lists must stay cheap).
#include "canvas.h"
#include "list_view.h"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    std::printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

} // namespace

int main() {
    using namespace sagrado;
    const Rect view{10, 20, 200, 100};
    const int row_h = 20;
    const int count = 10000;

    RowRange top = visible_rows(view, 0, row_h, count);
    check(top.first == 0, "unscrolled range starts at the first row");
    check(top.last - top.first <= view.h / row_h + 2,
          "range covers the viewport plus at most one row of slack");
    check(top.y == view.y + 2, "first visible row starts below the top pad");

    RowRange mid = visible_rows(view, 50 * row_h, row_h, count);
    check(mid.first == 49, "scrolling by N rows skips the rows above the view");
    check(mid.last <= 56, "scrolled range is still viewport-sized");
    check(mid.y <= view.y + 2 && mid.y >= view.y + 2 - row_h,
          "first visible row is drawn at most one row above the viewport");

    RowRange end = visible_rows(view, count * row_h, row_h, count);
    check(end.last == count, "range never runs past the last row");
    check(visible_rows(view, 0, row_h, 0).last == 0, "empty list yields no rows");

    check(row_at(view, 0, row_h, count, view.y + 2) == 0, "click hits the first row");
    check(row_at(view, 50 * row_h, row_h, count, view.y + 2) == 50,
          "click maps through the scroll offset");
    check(row_at(view, 0, row_h, count, view.y - 30) == -1,
          "click above the content hits nothing");

    // Culling: a row painted far outside the clip must not touch pixels.
    Canvas cv;
    cv.resize(64, 64);
    cv.clear(Color{0, 0, 0});
    cv.push_clip({0, 0, 64, 16});
    cv.text_elided(0, 4000, "off-screen row", 60, Color{255, 255, 255});
    cv.text(0, -4000, "off-screen row", Color{255, 255, 255});
    bool painted = false;
    for (int y = 0; y < 64 && !painted; ++y)
        for (int x = 0; x < 64; ++x)
            if (cv.data()[y * 64 + x] != 0) {
                painted = true;
                break;
            }
    check(!painted, "text far outside the clip paints nothing");

    cv.text(2, 2, "visible", Color{255, 255, 255});
    bool any = false;
    for (int y = 0; y < 16 && !any; ++y)
        for (int x = 0; x < 64; ++x)
            if (cv.data()[y * 64 + x] != 0) {
                any = true;
                break;
            }
    check(any, "text inside the clip still paints");

    std::printf(failures ? "list view: %d FAILED\n" : "list view: all passed\n",
                failures);
    return failures ? 1 : 0;
}
