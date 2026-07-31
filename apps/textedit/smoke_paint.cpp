// Headless paint smoke for Sagrado TextEdit chrome (no Win32).
// Loads a skin, lays out gel + menu bar + text view metrics, paints, writes PPM.
#include <cstdio>
#include <cstring>
#include <string>

#include "appearance.h"

int main(int argc, char **argv) {
    Appearance ap;
    std::string path = argc > 1 ? argv[1] : "format/skins/milk-redux/milk-redux.sap";
    if (!ap.load(path)) {
        std::fprintf(stderr, "load failed: %s\n", path.c_str());
        ap.set_skin(stock_skin());
    } else {
        std::printf("loaded: %s (%s)\n", path.c_str(), ap.skin.meta.name.c_str());
    }

    constexpr int W = 720, H = 520;
    Canvas cv;
    cv.resize(W, H);

    GelLayout gel = gel_layout(0, 0, W, H, GelStyle::Main, &ap, true);
    paint_gel(cv, ap, {0, 0, W, H}, "Untitled - Sagrado TextEdit", true, 0,
              GelStyle::Main);

    Rect cl = gel.client;
    Rect menu_r{cl.x, cl.y, cl.w, kMenuBarH};
    static const char *titles[] = {"File", "Edit", "Find", "Appearance", "Help"};
    paint_menu_bar(cv, ap, menu_r, titles, 5, 1);

    Rect status{cl.x, cl.bottom() - 20, cl.w, 20};
    Rect text{cl.x, menu_r.bottom(), cl.w - kScrollbarW,
              status.y - menu_r.bottom()};
    cv.fill(text, ap.c("text.background"));
    {
        CanvasClip clip(cv, text);
        const char *line1 = "Sagrado TextEdit — first SagradoKit app";
        cv.text(text.x + 4, text.y + 4, line1, ap.c("text.foreground"));
        cv.text(text.x + 4, text.y + 4 + cv.line_height(),
                "Gel + menu bar + text view painted through the Appearance Engine.",
                ap.c("text.foreground"));
    }
    paint_scrollbar(cv, ap, {text.right(), text.y, kScrollbarW, text.h}, 0, 10, 20,
                    false);

    // Open Edit menu — Milk must stay on the colour path (no dark Boilerplate
    // plates from soft-complete; ignore tiny popup_frame stubs).
    static const char *edit_items[] = {
        "Undo", "-", "Cut", "Copy", "Paste", "Clear", "-", "Select All", "Sort Lines"};
    int mw = 72;
    for (int i = 0; i < 9; ++i)
        mw = std::max(mw, cv.text_width(edit_items[i]) + 28);
    MenuLayout open =
        paint_menu(cv, ap, menu_r.x + 48, menu_r.bottom(), mw, edit_items, 9, 2);
    if (path.find("Milk Redux") != std::string::npos ||
        path.find("milk-redux") != std::string::npos) {
        if (ap.art("menu.background") || ap.art("menu.item.normal")) {
            std::fprintf(stderr, "Milk menu art leaked into TextEdit paint\n");
            return 1;
        }
        Color expect = ap.c("menu.background");
        int sx = open.items_bounds.x + open.items_bounds.w / 2;
        int sy = open.items_bounds.y + 2;
        uint32_t px = cv.data()[size_t(sy) * W + sx];
        int pr = int((px >> 16) & 255), pg = int((px >> 8) & 255),
            pb = int(px & 255);
        int dr = pr - expect.r, dg = pg - expect.g, db = pb - expect.b;
        if (dr * dr + dg * dg + db * db > 40 * 40) {
            std::fprintf(stderr,
                         "Milk open menu fill #%02x%02x%02x != colour #%02x%02x%02x\n",
                         pr, pg, pb, expect.r, expect.g, expect.b);
            return 1;
        }
        // Outer ring is Focus Box (not the soft grey 3×3 stub).
        Color ring = ap.c("focus.box");
        uint32_t edge = cv.data()[size_t(open.frame.y) * W + open.frame.x];
        int er = int((edge >> 16) & 255), eg = int((edge >> 8) & 255),
            eb = int(edge & 255);
        if (er != ring.r || eg != ring.g || eb != ring.b) {
            std::fprintf(stderr,
                         "Milk menu outer ring #%02x%02x%02x != focus.box #%02x%02x%02x\n",
                         er, eg, eb, ring.r, ring.g, ring.b);
            return 1;
        }
        std::printf("Milk Edit menu colour-path OK (mw=%d, focus ring)\n", mw);
    }

    cv.fill(status, ap.c("primary.background"));
    cv.text(status.x + 8, status.y + (status.h - cv.line_height()) / 2,
            "Soft Wrap    Ln 1", ap.c("primary.label"));

    // Find dialog sample beside (clipped if needed) — paint into same canvas
    // at a fixed offset for visual check when height allows.
    if (H > kFindDlgH + 40)
        paint_find_chrome_sample(cv, ap, 40, 40, kFindDlgW, true);

    paint_gel_grip(cv, ap, gel.grip, true);

    size_t lit = 0;
    for (int i = 0; i < W * H; ++i)
        if (cv.data()[i]) ++lit;
    std::printf("painted %zu non-black pixels\n", lit);
    std::printf("client %dx%d  text %dx%d  menu_h=%d\n", gel.client.w, gel.client.h,
                text.w, text.h, kMenuBarH);

    // Clip spill check: paint gel into a short panel
    {
        Canvas panel;
        panel.resize(400, 280);
        panel.clear({0, 0, 0});
        Rect box{8, 8, 384, 264};
        {
            CanvasClip clip(panel, box);
            paint_gel(panel, ap, box, "TextEdit", true, 0, GelStyle::Main);
            paint_menu_bar(panel, ap,
                           {box.x + kBorder, box.y + kTitleH, box.w - 2 * kBorder,
                            kMenuBarH},
                           titles, 5);
        }
        size_t spill = 0;
        for (int y = 0; y < panel.height(); ++y)
            for (int x = 0; x < panel.width(); ++x) {
                if (box.contains(x, y)) continue;
                if (panel.data()[size_t(y) * panel.width() + x]) ++spill;
            }
        std::printf("clip spill outside gel panel: %zu pixels\n", spill);
        if (spill != 0) return 1;
    }

    if (lit < 1000) {
        std::fprintf(stderr, "too few pixels painted\n");
        return 1;
    }

    FILE *f = std::fopen("build/textedit-preview.ppm", "wb");
    if (f) {
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; ++i) {
            uint32_t p = cv.data()[i];
            uint8_t rgb[3] = {uint8_t((p >> 16) & 255), uint8_t((p >> 8) & 255),
                              uint8_t(p & 255)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("wrote build/textedit-preview.ppm\n");
    }
    return 0;
}
