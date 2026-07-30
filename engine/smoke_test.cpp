// Headless smoke test: load stock skin, resolve tokens, paint kit into a buffer.
// Build with host g++ (no Win32): make smoke
#include <cstdio>
#include <cstring>
#include <string>

#include "appearance.h"
#include "hfnt.h"

int main(int argc, char **argv) {
    Appearance ap;
    std::string path = argc > 1 ? argv[1] : "format/skins/stock.sap";
    if (!ap.load(path)) {
        std::fprintf(stderr, "load failed: %s\n", path.c_str());
        // Fall back to embedded stock
        ap.set_skin(stock_skin());
    } else {
        std::printf("loaded: %s (%s)\n", path.c_str(), ap.skin.meta.name.c_str());
        std::printf("art slots authored: %zu  loaded: %zu\n", ap.skin.art.size(),
                    ap.art_cache.size());
        std::printf("icon slots authored: %zu  loaded: %zu\n", ap.skin.icons.size(),
                    ap.icon_cache.size());
        if (ap.art("button.normal"))
            std::printf("button.normal art %dx%d caps=[%d,%d,%d,%d]\n",
                        ap.art("button.normal")->w, ap.art("button.normal")->h,
                        ap.art("button.normal")->caps[0],
                        ap.art("button.normal")->caps[1],
                        ap.art("button.normal")->caps[2],
                        ap.art("button.normal")->caps[3]);
        if (ap.art("wonderlight.go"))
            std::printf("wonderlight.go art %dx%d\n", ap.art("wonderlight.go")->w,
                        ap.art("wonderlight.go")->h);
        if (ap.icon("file.generic.16"))
            std::printf("file.generic.16 icon %dx%d\n", ap.icon("file.generic.16")->w,
                        ap.icon("file.generic.16")->h);
        if (ap.icon("folder.16"))
            std::printf("folder.16 icon %dx%d\n", ap.icon("folder.16")->w,
                        ap.icon("folder.16")->h);
        if (ap.icon("user.16"))
            std::printf("user.16 icon %dx%d\n", ap.icon("user.16")->w,
                        ap.icon("user.16")->h);
        if (ap.art("hap.image.271"))
            std::printf("hap.image.271 preserved %dx%d\n", ap.art("hap.image.271")->w,
                        ap.art("hap.image.271")->h);
        if (ap.art("menu.item.hilited"))
            std::printf("menu.item.hilited art %dx%d\n", ap.art("menu.item.hilited")->w,
                        ap.art("menu.item.hilited")->h);
        if (ap.art("slider.h.bar.disabled"))
            std::printf("slider.h.bar.disabled art %dx%d\n",
                        ap.art("slider.h.bar.disabled")->w,
                        ap.art("slider.h.bar.disabled")->h);
        // Milk leaves open-menu plates empty (colour path). Soft-complete must
        // not invent dark Boilerplate menu.background / menu.item.* — that
        // reads as two themes mixed together. (Hap may still carry a junk
        // menu.separator plate; paint ignores separators taller than 4px.)
        if (path.find("Milk Redux") != std::string::npos ||
            path.find("milk-redux") != std::string::npos) {
            if (ap.art("menu.background") || ap.art("menu.background_pattern") ||
                ap.art("menu.item.normal") || ap.art("menu.item.hilited") ||
                ap.art("menu.item.pattern.normal") ||
                ap.art("menu.item.pattern.hilited")) {
                std::fprintf(stderr,
                             "Milk open-menu art was soft-completed (mixed theme)\n");
                return 1;
            }
            std::printf("Milk open menus: colour path (no invented menu art)\n");
        }
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

    // --- Text: stock face metrics, Latin-1 folding, elision, %FNT faces.
    {
        Canvas t;
        t.resize(200, 40);
        if (t.line_height() != kFontHeight) {
            std::fprintf(stderr, "stock line height %d\n", t.line_height());
            return 1;
        }
        // Accented input must still measure and paint (folded onto ASCII art).
        if (t.text_width("caf\xc3\xa9") != t.text_width("cafe")) {
            std::fprintf(stderr, "latin-1 fold changed advance\n");
            return 1;
        }
        if (t.text_width("caf\xe9") != t.text_width("cafe")) {
            std::fprintf(stderr, "raw latin-1 byte not folded\n");
            return 1;
        }
        // Em-dash / smart quotes fold onto ASCII (stock face has no U+2014).
        if (t.text_width("Untitled \xe2\x80\x94 App") !=
            t.text_width("Untitled - App")) {
            std::fprintf(stderr, "em-dash not folded to hyphen\n");
            return 1;
        }
        if (t.text_width("\xe2\x80\x9chello\xe2\x80\x9d") !=
            t.text_width("\"hello\"")) {
            std::fprintf(stderr, "smart quotes not folded\n");
            return 1;
        }
        // Empty Font must not stick — Canvas keeps stock (avoids blank labels).
        {
            Font empty;
            t.set_font(&empty);
            if (t.font().name != "Stock" || t.text_width("Find") == 0) {
                std::fprintf(stderr, "empty font was not rejected\n");
                return 1;
            }
            t.set_font(nullptr);
        }
        std::string cut = t.text_elide("Menu Bar Pattern", 60);
        if (cut.size() < 4 || cut.compare(cut.size() - 3, 3, "...") != 0 ||
            t.text_width(cut.c_str()) > 60) {
            std::fprintf(stderr, "elision wrong: %s\n", cut.c_str());
            return 1;
        }
        if (t.text_elide("Menu", 400) != "Menu") {
            std::fprintf(stderr, "short label was elided\n");
            return 1;
        }

        // A Haxial face: 12px line, one 'A' 5x7 two rows down, advance 7.
        std::vector<uint8_t> f(0x136 + 3 * hfnt::kRecord, 0);
        const char *magic = "%FNT";
        for (int i = 0; i < 4; ++i) f[i] = uint8_t(magic[i]);
        f[7] = 1;
        f[6] = 2; // version 0x00020001
        f[0x18] = 4;
        std::memcpy(&f[0x19], "Test", 4);
        f[0x10c] = 12;
        size_t rec = 0x136;
        auto put16 = [&](size_t o, unsigned v) {
            f[o] = uint8_t(v >> 8);
            f[o + 1] = uint8_t(v);
        };
        auto put32 = [&](size_t o, uint32_t v) {
            for (int i = 0; i < 4; ++i) f[o + i] = uint8_t(v >> (24 - 8 * i));
        };
        size_t bits = f.size();
        put16(rec, ' ');
        f[rec + 10] = 4; // space: advance only
        rec += hfnt::kRecord;
        put16(rec, 'A');
        put32(rec + 2, uint32_t(bits));
        f[rec + 6] = 5;  // w
        f[rec + 7] = 7;  // h
        f[rec + 8] = 1;  // planes
        f[rec + 9] = 2;  // ytop
        f[rec + 10] = 7; // advance
        for (int i = 0; i < 7; ++i) f.push_back(0xf8);

        Font face;
        if (!hfnt::parse(f, face)) {
            std::fprintf(stderr, "%%FNT parse failed\n");
            return 1;
        }
        if (face.name != "Test" || face.line_height != 12 ||
            face.glyphs['A'].advance != 7 || face.glyphs['A'].ytop != 2 ||
            !face.pixel(face.glyphs['A'], 4, 6) ||
            face.pixel(face.glyphs['A'], 5, 0)) {
            std::fprintf(stderr, "%%FNT face decoded wrong (%s lh=%d)\n",
                         face.name.c_str(), face.line_height);
            return 1;
        }
        t.set_font(&face);
        if (t.line_height() != 12 || t.text_width("A A") != 18) {
            std::fprintf(stderr, "loaded face metrics wrong: %d/%d\n",
                         t.line_height(), t.text_width("A A"));
            return 1;
        }
        t.clear(rgb(0, 0, 0));
        t.text(0, 0, "A", rgb(255, 255, 255));
        if (!t.data()[size_t(2) * t.width()] || t.data()[0]) {
            std::fprintf(stderr, "loaded face painted at wrong ytop\n");
            return 1;
        }
        t.set_font(nullptr);
        if (t.line_height() != kFontHeight) {
            std::fprintf(stderr, "font reset failed\n");
            return 1;
        }
        std::printf("text: stock lh=%d, %%FNT '%s' lh=%d ok\n", kFontHeight,
                    face.name.c_str(), face.line_height);
    }

    Canvas cv;
    cv.resize(640, 960);
    paint_kit_preview(cv, ap, {0, 0, 640, 960}, true, 1, 2);

    // Non-zero pixels prove we painted
    size_t lit = 0;
    for (int i = 0; i < 640 * 960; ++i)
        if (cv.data()[i]) ++lit;
    std::printf("painted %zu non-black pixels\n", lit);

    std::string out = "build/smoke-roundtrip.sap";
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
    if (!ap.art_cache.empty()) {
        if (again.art_cache.size() != ap.art_cache.size()) {
            std::fprintf(stderr, "roundtrip art count mismatch %zu → %zu\n",
                         ap.art_cache.size(), again.art_cache.size());
            return 1;
        }
        if (ap.art("button.normal") && again.art("button.normal")) {
            const SkinImage *a = ap.art("button.normal");
            const SkinImage *b = again.art("button.normal");
            if (a->w != b->w || a->h != b->h || a->px != b->px) {
                std::fprintf(stderr, "roundtrip button.normal pixels mismatch\n");
                return 1;
            }
        }
        std::printf("art roundtrip ok (%zu slots)\n", again.art_cache.size());
    }
    std::printf("roundtrip ok → %s\n", out.c_str());

    // Clip regression: tall kit content must not paint outside a short panel.
    {
        Canvas panel;
        panel.resize(400, 280);
        panel.clear(rgb(0, 0, 0));
        Rect box{8, 8, 384, 264};
        panel.fill(box, ap.c("workspace.background3"));
        {
            CanvasClip clip(panel, box);
            paint_kit_preview(panel, ap, box, true, 1, 0);
        }
        size_t spill = 0;
        for (int y = 0; y < panel.height(); ++y)
            for (int x = 0; x < panel.width(); ++x) {
                if (box.contains(x, y)) continue;
                if (panel.data()[size_t(y) * panel.width() + x]) ++spill;
            }
        std::printf("clip spill outside preview panel: %zu pixels\n", spill);
        if (spill != 0) {
            std::fprintf(stderr, "kit preview spilled past clip\n");
            return 1;
        }
    }

    // Write a PPM preview (easy to convert / view) for build verification.
    const char *ppm = ap.art("button.normal") ? "build/kit-preview-art.ppm"
                                              : "build/kit-preview.ppm";
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
