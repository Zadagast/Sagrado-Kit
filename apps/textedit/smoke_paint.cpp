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
    paint_menu_bar(cv, ap, menu_r, titles, 5, 0);

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
