// Headless smoke test: load stock skin, resolve tokens, paint kit into a buffer.
// Build with host g++ (no Win32): make smoke
#include <cstdio>
#include <string>

#include "appearance.h"

int main(int argc, char **argv) {
    Appearance ap;
    std::string path = argc > 1 ? argv[1] : "format/skins/stock.skin.toml";
    if (!ap.load(path)) {
        std::fprintf(stderr, "load failed: %s\n", path.c_str());
        // Fall back to embedded stock
        ap.set_skin(stock_skin());
    } else {
        std::printf("loaded: %s (%s)\n", path.c_str(), ap.skin.meta.name.c_str());
    }

    Color bg = ap.c("primary.background");
    Color btn = ap.c("button.face");
    std::printf("primary.background = #%02x%02x%02x\n", bg.r, bg.g, bg.b);
    std::printf("button.face        = #%02x%02x%02x\n", btn.r, btn.g, btn.b);

    // Incomplete skin still resolves (slate omits some roles → stock)
    if (argc > 2) {
        Appearance partial;
        if (partial.load(argv[2])) {
            Color m = partial.c("menu.background"); // may fall through to stock
            std::printf("partial menu.background = #%02x%02x%02x (skin has %zu colours)\n",
                        m.r, m.g, m.b, partial.skin.colors.size());
        }
    }

    Canvas cv;
    cv.resize(640, 480);
    paint_kit_preview(cv, ap, {0, 0, 640, 480}, true, 1, 2);

    // Non-zero pixels prove we painted
    size_t lit = 0;
    for (int i = 0; i < 640 * 480; ++i)
        if (cv.data()[i]) ++lit;
    std::printf("painted %zu non-black pixels\n", lit);

    std::string out = "build/smoke-roundtrip.skin.toml";
    if (!ap.save(out)) {
        std::fprintf(stderr, "save failed\n");
        return 1;
    }
    Appearance again;
    if (!again.load(out)) {
        std::fprintf(stderr, "reload failed\n");
        return 1;
    }
    Color bg2 = again.c("primary.background");
    if (bg2.r != bg.r || bg2.g != bg.g || bg2.b != bg.b) {
        std::fprintf(stderr, "roundtrip colour mismatch\n");
        return 1;
    }
    std::printf("roundtrip ok → %s\n", out.c_str());

    // Write a PPM preview (easy to convert / view) for build verification.
    const char *ppm = "build/kit-preview.ppm";
    FILE *f = std::fopen(ppm, "wb");
    if (f) {
        std::fprintf(f, "P6\n%d %d\n255\n", cv.width(), cv.height());
        for (int i = 0; i < cv.width() * cv.height(); ++i) {
            uint32_t p = cv.data()[i];
            uint8_t rgb[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("wrote %s\n", ppm);
    }
    return lit > 1000 ? 0 : 1;
}
