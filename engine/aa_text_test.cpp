// Kit smooth-text path: a host-installed anti-aliased face must drive metrics,
// blending and ink bounds, and the bitmap face must come back when it is gone.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "canvas.h"

static void check(bool ok, const char *what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
}

// A fake face: 'A' is a 2x2 half-coverage block advancing 6px, nothing else.
static uint8_t kCov[4] = {128, 128, 128, 128};

static const AAGlyph *fake_glyph(unsigned cp) {
    static AAGlyph g;
    if (cp != 'A') return nullptr;
    g = AAGlyph{kCov, 2, 2, 1, 2, 6};
    return &g;
}

int main() {
    Canvas cv;
    cv.resize(32, 32);

    const int bitmap_lh = cv.line_height();
    const int bitmap_w = cv.text_width("AAA");

    set_kit_aa_face(&fake_glyph, 20, 16);
    check(cv.line_height() == 20, "installed face drives line height");
    check(cv.text_width("AAA") == 18, "installed face drives advances");
    // Unknown codepoints must still fall through to the bitmap face.
    check(cv.text_width("B") > 0, "codepoints the face lacks still measure");

    cv.clear({0, 0, 0});
    cv.text(4, 0, "A", {255, 255, 255});
    // left=1, top=2 with ascent 16 → rows 14..15, cols 5..6, at 50% coverage.
    uint32_t px = cv.data()[14 * 32 + 5];
    check(px != 0, "coverage blends ink into the framebuffer");
    // Blended in linear light, so half coverage of white on black lands well
    // above sRGB 128 — that is what keeps light-on-dark text from going thin.
    check((px & 0xff) > 170 && (px & 0xff) < 205, "coverage blends in linear light");
    check(cv.data()[13 * 32 + 5] == 0, "glyph does not paint above its ink");

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    check(cv.text_ink("A", x0, y0, x1, y1), "smooth glyphs report ink");
    check(x0 == 1 && x1 == 2 && y0 == 14 && y1 == 15, "ink bounds follow the face");

    // Eliding still works against the installed face's advances.
    check(cv.text_elide("AAAA", 100) == std::string("AAAA"), "short labels pass through");
    check(cv.text_elide("AAAA", 1).empty(), "no room for the ellipsis elides away");

    clear_kit_aa_face();
    check(cv.line_height() == bitmap_lh, "removing the face restores the bitmap one");
    check(cv.text_width("AAA") == bitmap_w, "bitmap metrics come back unchanged");

    std::printf("aa text: all passed\n");
    return 0;
}
