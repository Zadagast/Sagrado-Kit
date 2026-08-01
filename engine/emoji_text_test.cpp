// Kit emoji-in-text test: the editor must step by drawable units (emoji
// clusters), not raw bytes, or a picked emoji paints as broken glyphs.
// Build with host g++ (no Win32): make smoke
#include <cstdio>
#include <string>

#include "appearance.h"
#include "text_field.h"
#include "text_view.h"

namespace {

const char *kGrin = "\xF0\x9F\x98\x80";      // U+1F600, 4 bytes
const char *kFamily =                        // U+1F468 ZWJ U+1F469, 11 bytes
    "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9";
const uint32_t kMark = 0xFFFF00FF;

SkinImage &stub_icon() {
    static SkinImage img;
    if (img.empty()) {
        img.w = img.h = 2;
        img.px.assign(4, kMark);
    }
    return img;
}

// Stands in for the app's emoji pack: matches the two test sequences.
bool test_probe(const char *s, int, int *out_len, const SkinImage **out_img) {
    for (const char *seq : {kFamily, kGrin}) {
        size_t n = std::string(seq).size();
        if (std::string(s, 0, n) == seq) {
            if (out_len) *out_len = int(n);
            if (out_img) *out_img = &stub_icon();
            return true;
        }
    }
    return false;
}

int failures = 0;

void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

} // namespace

int main(int argc, char **argv) {
    Appearance ap;
    std::string path = argc > 1 ? argv[1] : "format/skins/stock.sap";
    if (!ap.load(path)) ap.set_skin(stock_skin());

    Canvas cv;
    cv.resize(320, 64);
    const int em = cv.line_height();

    set_kit_emoji_probe(&test_probe);

    // 1. Units: a cluster is one step, plain ASCII is one byte.
    check(cv.unit_len(kGrin) == 4, "unit_len spans a 4-byte emoji");
    check(cv.unit_len(kFamily) == 11, "unit_len spans a ZWJ sequence");
    check(cv.unit_len("ab") == 1, "unit_len is 1 for ASCII");

    // 1b. Astral planes decode to their real codepoint — a probe that keys off
    //     the codepoint (the app's does) never matches U+FFFD.
    const char *p = kGrin;
    check(fontutil::next_cp(p) == 0x1F600 && p == kGrin + 4,
          "next_cp decodes a 4-byte codepoint");

    // 2. Measuring: an emoji is one square, and text() must agree with
    //    text_width() or the caret drifts away from the glyphs.
    std::string line = std::string("a") + kGrin + "b";
    int plain = cv.text_width("ab");
    check(cv.text_width(line.c_str()) == plain + em,
          "text_width counts an emoji as one line-height square");
    check(cv.text(0, 0, line.c_str(), Color{255, 255, 255}) ==
              cv.text_width(line.c_str()),
          "text() advances exactly as far as text_width()");

    // 3. Wrapping must not cut a cluster in half.
    auto lines = layout_lines(cv, line, plain, true);
    bool split = false;
    for (const auto &vl : lines)
        if (vl.start == 2 || vl.start == 3 || vl.len == 2 || vl.len == 3)
            split = true;
    check(!split, "layout_lines never splits an emoji cluster");

    // 4. Clicking anywhere in the emoji lands on its first byte, never inside.
    auto whole = layout_lines(cv, line, 4096, true);
    bool inside = false;
    for (int x = 0; x < plain + em + 8; ++x) {
        size_t off = offset_at_xy(cv, whole, line, 0, x);
        if (off > 1 && off < 5) inside = true;
    }
    check(!inside, "offset_at_xy never returns a mid-emoji offset");

    // 5. Editing keeps the sequence intact: one backspace clears the whole
    //    emoji instead of leaving broken continuation bytes behind.
    TextDoc doc;
    auto place_caret = [&](size_t at) {
        doc.text = line;
        doc.caret = doc.anchor = at;
    };
    place_caret(5); // just after the emoji
    doc.backspace();
    check(doc.text == "ab" && doc.caret == 1,
          "backspace removes a whole emoji, not one byte");
    place_caret(1);
    doc.del_forward();
    check(doc.text == "ab", "delete removes a whole emoji, not one byte");
    doc.text = line;
    check(doc.next_pos(1) == 5 && doc.prev_pos(5) == 1,
          "caret steps hop over the emoji");

    // 6. The editor actually paints the emoji image (the compose-box bug: it
    //    drew byte by byte, so the probe never saw a whole cluster).
    TextFieldState st;
    st.doc.text = line;
    st.doc.caret = 0;
    Rect r{0, 0, 320, 40};
    cv.clear(Color{0, 0, 0});
    text_field_relayout(cv, st, r.w - 2 * st.pad);
    TextEditorPaint ep;
    ep.r = r;
    ep.pad = st.pad;
    paint_text_editor(cv, ap, st.doc, st.lines, ep);
    bool painted = false;
    for (int y = 0; y < 40 && !painted; ++y)
        for (int x = 0; x < 320; ++x)
            if ((cv.data()[size_t(y) * 320 + x] & 0x00FFFFFFu) ==
                (kMark & 0x00FFFFFFu)) {
                painted = true;
                break;
            }
    check(painted, "paint_text_editor blits the emoji image");

    // 7. With no probe registered the Kit behaves exactly as before.
    set_kit_emoji_probe(nullptr);
    check(cv.unit_len("ab") == 1 && cv.text_width("ab") == plain,
          "ASCII text is unchanged without a probe");

    std::printf(failures ? "emoji text: %d FAILED\n" : "emoji text: all passed\n",
                failures);
    return failures ? 1 : 0;
}
