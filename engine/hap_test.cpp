// .hap decoder regression: builds synthetic Hap files (no fixtures needed),
// decodes them and checks pixels, palette depth, transparency, caps/positions,
// the 8-byte icon record header and the slot → role mapping.
//
// Optional corpus mode: hap_test <dir-of-haps> also decodes every .hap under
// dir and reports per-file image/icon counts (used against research/haps,
// which is not committed).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "appearance.h"
#include "hap_skin.h"

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Buf {
    std::vector<uint8_t> d;
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v >> 8)); u8(uint8_t(v)); }
    void u32(uint32_t v) { u16(uint16_t(v >> 16)); u16(uint16_t(v)); }
    void put32(size_t at, uint32_t v) {
        d[at] = uint8_t(v >> 24); d[at + 1] = uint8_t(v >> 16);
        d[at + 2] = uint8_t(v >> 8); d[at + 3] = uint8_t(v);
    }
    size_t size() const { return d.size(); }
};

// One record: bpp-packed indices, rows padded to 4 bytes. caps/positions are
// only emitted for image records (icons stop after the 8-byte core header).
void record(Buf &b, int w, int h, int bpp, const std::vector<uint32_t> &palette,
            const std::vector<uint8_t> &idx, bool transparency,
            uint8_t transparent_index, const uint8_t caps[4],
            const uint8_t pos[4], bool icon) {
    b.u16(uint16_t(w));
    b.u16(uint16_t(h));
    b.u16(uint16_t((transparency ? 0x0100 : 0) | bpp));
    b.u8(uint8_t(palette.size() - 1));
    b.u8(transparent_index);
    if (!icon) {
        b.u32(transparency ? palette[transparent_index] : 0);
        for (int i = 0; i < 4; ++i) b.u8(caps[i]);
        for (int i = 0; i < 4; ++i) b.u8(pos[i]);
    }
    for (uint32_t c : palette) b.u32(c);
    size_t stride = (size_t(w) * bpp + 31) / 32 * 4;
    for (int y = 0; y < h; ++y) {
        std::vector<uint8_t> row(stride, 0);
        for (int x = 0; x < w; ++x) {
            uint8_t v = idx[size_t(y) * w + x];
            int per = 8 / bpp;
            size_t byte = size_t(x) / per;
            int shift = 8 - bpp * (int(x % per) + 1);
            row[byte] = uint8_t(row[byte] | (v << shift));
        }
        b.d.insert(b.d.end(), row.begin(), row.end());
    }
}

// Assemble a whole file: header, info, images, colors, icons.
std::vector<uint8_t> build_hap(const std::string &name) {
    Buf b;
    for (int i = 0; i < 0x90; ++i) b.u8(0);
    b.d[0] = '%'; b.d[1] = 'H'; b.d[2] = 'A'; b.d[3] = 'P';
    b.put32(4, 0x00010000);

    // Info section: 4 string lengths at +0x22, strings from +0x34.
    size_t info_off = b.size();
    Buf info;
    for (int i = 0; i < 0x34; ++i) info.u8(0);
    info.d[0x22] = uint8_t(name.size());
    for (char c : name) info.u8(uint8_t(c));
    b.d.insert(b.d.end(), info.d.begin(), info.d.end());
    b.put32(0x2c, uint32_t(info_off));
    b.put32(0x30, uint32_t(b.size() - info_off));

    // Images: offset table wide enough for the late Haxial slots (menu bar,
    // colour chooser) that AppearanceEdit 1.4 added, then the records.
    size_t img_off = b.size();
    const int table = 284;
    Buf img;
    for (int i = 0; i < table; ++i) img.u32(0);
    const uint8_t caps[4] = {3, 4, 5, 6};
    const uint8_t pos[4] = {7, 8, 9, 10};
    const uint8_t zero[4] = {0, 0, 0, 0};

    // slot 25 (button.normal): 4x2 @ 8bpp, palette index 1 transparent.
    img.put32(25 * 4, uint32_t(img.size()));
    record(img, 4, 2, 8, {0x102030, 0x405060, 0x708090},
           {0, 1, 2, 0, 2, 1, 0, 1}, true, 1, caps, pos, false);
    // slot 17 (primary.background_pattern): 8x1 @ 1bpp, opaque.
    img.put32(17 * 4, uint32_t(img.size()));
    record(img, 8, 1, 1, {0x000000, 0xffffff}, {0, 1, 0, 1, 1, 0, 1, 0}, false,
           0, zero, zero, false);
    // slot 26 (button.hilited): 4x1 @ 4bpp, opaque.
    img.put32(26 * 4, uint32_t(img.size()));
    record(img, 4, 1, 4, {0x111111, 0x222222, 0x333333, 0x444444},
           {3, 2, 1, 0}, false, 0, zero, zero, false);
    // The slots AppearanceEdit's row checkmarks pinned down: progress non-fill
    // and bitmap digits, menu first/last hilite, the menu bar block and the
    // colour chooser. Each is a flat 2x2 so only the mapping is under test.
    for (int slot : {113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124,
                     209, 210, 271, 272, 273, 274, 275, 277, 278, 279,
                     281, 282, 283}) {
        img.put32(slot * 4, uint32_t(img.size()));
        record(img, 2, 2, 1, {0x0a0b0c, 0x0a0b0c}, {0, 0, 0, 0}, false, 0, zero,
               zero, false);
    }
    b.d.insert(b.d.end(), img.d.begin(), img.d.end());
    b.put32(0x34, uint32_t(img_off));
    b.put32(0x38, uint32_t(b.size() - img_off));

    // Colors: 204 entries, colors[i] = i * 0x010101.
    size_t col_off = b.size();
    for (int i = 0; i < kColorTableLen; ++i)
        b.u32(uint32_t(i) * 0x010101u);
    b.put32(0x3c, uint32_t(col_off));
    b.put32(0x40, uint32_t(b.size() - col_off));

    // Icons: 8-byte record header, no caps/positions. Record 4 = 2x2 @ 2bpp.
    // Icon n uses record 4n (16 px) / 4n+1 (32 px), so record 4 = icon 1 = Stop.
    // Records 68/76/160 are the file icons data / image/ / folder/, each a solid
    // colour so the taxonomy walk can be told apart pixel by pixel.
    size_t ico_off = b.size();
    Buf ico;
    const int ico_table = 300;
    for (int i = 0; i < ico_table; ++i) ico.u32(0);
    ico.put32(4 * 4, uint32_t(ico.size()));
    record(ico, 2, 2, 2, {0xaabbcc, 0xddeeff, 0x010203},
           {0, 1, 2, 1}, true, 2, zero, zero, true);
    struct Solid { int rec; uint32_t rgb; };
    for (const Solid &s : {Solid{68, 0x111100}, Solid{76, 0x222200},
                           Solid{160, 0x333300}, Solid{292, 0x444400}}) {
        ico.put32(s.rec * 4, uint32_t(ico.size()));
        record(ico, 16, 16, 1, {s.rgb, s.rgb},
               std::vector<uint8_t>(16 * 16, 0), false, 0, zero, zero, true);
    }
    b.d.insert(b.d.end(), ico.d.begin(), ico.d.end());
    b.put32(0x44, uint32_t(ico_off));
    b.put32(0x48, uint32_t(b.size() - ico_off));
    return b.d;
}

bool write_file(const std::string &path, const std::vector<uint8_t> &d) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    return true;
}

void synthetic_test() {
    const std::string path = "build/hap_test.hap";
    check(write_file(path, build_hap("Probe Theme")), "write synthetic .hap");

    Theme t;
    check(load_hap(path, t), "load synthetic .hap");
    check(t.name == "Probe Theme", "info metadata name");
    check(t.has_colors, "colors present");
    check(t.colors[2] == 0x020202u, "colour table index 2");
    check(t.colors[kColorTableLen - 1] == uint32_t(kColorTableLen - 1) * 0x010101u,
          "colour table last entry");

    const ThemeImage *btn = t.image(25);
    check(btn != nullptr, "image slot 25 present");
    if (btn) {
        check(btn->w == 4 && btn->h == 2, "slot 25 geometry");
        check(btn->caps[0] == 3 && btn->caps[1] == 4 && btn->caps[2] == 5 &&
                  btn->caps[3] == 6,
              "slot 25 caps");
        check(btn->positions[0] == 7 && btn->positions[3] == 10,
              "slot 25 positions");
        check(btn->at(0, 0) == 0xff102030u, "8bpp pixel");
        check(btn->at(1, 0) == 0x00000000u, "transparent index cleared");
        check(btn->at(2, 0) == 0xff708090u, "8bpp palette lookup");
    }
    const ThemeImage *pat = t.image(17);
    check(pat != nullptr, "image slot 17 present");
    if (pat) {
        check(pat->w == 8 && pat->h == 1, "1bpp geometry");
        check(pat->at(0, 0) == 0xff000000u && pat->at(1, 0) == 0xffffffffu,
              "1bpp pixels");
        check(pat->at(7, 0) == 0xff000000u, "1bpp last pixel");
    }
    const ThemeImage *hil = t.image(26);
    check(hil != nullptr, "image slot 26 present");
    if (hil)
        check(hil->at(0, 0) == 0xff444444u && hil->at(3, 0) == 0xff111111u,
              "4bpp pixels");

    // Icon records use the 8-byte header; the 20-byte image header would put
    // the palette 12 bytes late and either fail or decode noise.
    const ThemeImage *icon = t.icon(4);
    check(icon != nullptr, "icon slot 4 present");
    if (icon) {
        check(icon->w == 2 && icon->h == 2, "icon geometry");
        check(icon->at(0, 0) == 0xffaabbccu, "2bpp icon pixel 0");
        check(icon->at(1, 0) == 0xffddeeffu, "2bpp icon pixel 1");
        check(icon->at(0, 1) == 0x00000000u, "icon transparent index cleared");
        check(icon->caps[0] == 0 && icon->positions[0] == 0,
              "icon records carry no caps/positions");
    }

    // Mapping: art/icon caches keyed by verified names, colours applied.
    Appearance ap;
    ap.set_skin(stock_skin());
    check(apply_hap_theme(ap, t), "apply_hap_theme");
    check(ap.skin.meta.name == "Probe Theme", "skin name from .hap");
    check(ap.art("button.normal") != nullptr, "button.normal art mapped");
    check(ap.art("primary.background_pattern") != nullptr,
          "primary.background_pattern art mapped");
    check(ap.icon("alert.stop.16") != nullptr, "icon record 4 mapped");
    // Icon names are the AppearanceEdit rows verified against v2 saves; the
    // pre-#21 map read record 292 as help and 160/240 as folder/server.
    check(ap.icon("tools.16") != nullptr, "icon record 292 is Tools");
    check(ap.icon("help.16") == nullptr, "record 292 is not Help");
    check(ap.icon("file_icon.data.16") != nullptr, "record 68 is file type data");
    check(ap.icon("file.generic.16") != nullptr, "file.generic alias kept");
    check(ap.icon("file_icon.image/.16") != nullptr, "record 76 is file type image/");
    check(ap.icon("file_icon.folder/.16") != nullptr, "record 160 is file type folder/");
    check(ap.icon("folder.16") != nullptr, "folder alias kept");

    // Taxonomy walk: exact type → family → generic data.
    Canvas cv;
    cv.resize(32, 32);
    struct Walk { const char *type; uint32_t rgb; };
    for (const Walk &w : {Walk{"image/jpeg", 0x222200}, Walk{"image/", 0x222200},
                          Walk{"folder/uploads", 0x333300},
                          Walk{"text/plain", 0x111100}, Walk{"", 0x111100}}) {
        cv.clear({0, 0, 0});
        paint_file_icon(cv, ap, 0, 0, w.type, 16);
        check((cv.data()[0] & 0xffffffu) == w.rgb,
              w.type[0] ? w.type : "(no file type)");
    }
    // Slots read off AppearanceEdit 1.4's row checkmarks (see
    // docs/haxial-surface-map.md); before this they were decoded but unnamed.
    for (const char *role : {"progress.non_fill", "progress.digit.0",
                             "progress.digit.9", "progress.digit.full",
                             "menu.item.first_hilited", "menu.item.last_hilited",
                             "menu_bar.pattern", "menu_bar.background",
                             "menu_bar.title_pattern.normal",
                             "menu_bar.title_pattern.disabled",
                             "menu_bar.title.normal", "menu_bar.title.hilited",
                             "menu_bar.title.disabled", "color_chooser.normal",
                             "color_chooser.hilited", "color_chooser.disabled"})
        check(ap.art(role) != nullptr, role);
    // Slot 276 is the hole between the title pattern and title triples.
    check(ap.art("menu_bar.title_pattern.hilited") != nullptr, "slot 274 mapped");

    // Digits stamp the percentage: 100% prefers the single Digit 100% record.
    cv.clear({0, 0, 0});
    paint_progress(cv, ap, {0, 0, 32, 16}, 100);
    int stamped = 0;
    for (int i = 0; i < 32 * 16; ++i)
        if ((cv.data()[i] & 0xffffffu) == 0x0a0b0cu) ++stamped;
    check(stamped > 0, "progress digits paint from art");

    Color pb = ap.c("primary.background");
    check(pb.r == 2 && pb.g == 2 && pb.b == 2, "colour index 2 → primary.background");

    // Truncated file must be rejected, not read out of bounds.
    std::vector<uint8_t> full = build_hap("Truncated");
    for (size_t cut : {full.size() / 2, full.size() - 4}) {
        std::vector<uint8_t> part(full.begin(), full.begin() + cut);
        check(write_file("build/hap_test_trunc.hap", part), "write truncated");
        Theme bad;
        load_hap("build/hap_test_trunc.hap", bad); // must not crash
    }

    // No Info metadata → name falls back to the file stem (12 real themes).
    std::vector<uint8_t> anon = build_hap("");
    check(write_file("build/classic-probe.hap", anon), "write unnamed .hap");
    Theme anon_t;
    check(load_hap("build/classic-probe.hap", anon_t), "load unnamed .hap");
    check(anon_t.name == "classic-probe", "name falls back to file stem");
}

} // namespace

int main(int argc, char **argv) {
    synthetic_test();

    if (argc > 1) {
        // Corpus mode over a directory of real themes (not committed).
        std::string dir = argv[1];
        std::string cmd = "ls -1 \"" + dir + "\"";
        FILE *p = popen(cmd.c_str(), "r");
        int files = 0, ok = 0, with_art = 0, with_icons = 0, unnamed = 0;
        char line[1024];
        while (p && std::fgets(line, sizeof(line), p)) {
            std::string f(line);
            while (!f.empty() && (f.back() == '\n' || f.back() == '\r')) f.pop_back();
            if (f.size() < 5 || f.substr(f.size() - 4) != ".hap") continue;
            ++files;
            Theme t;
            if (!load_hap(dir + "/" + f, t)) continue;
            ++ok;
            if (!t.images.empty()) ++with_art;
            if (!t.icons.empty()) ++with_icons;
            if (t.name.empty()) ++unnamed;
        }
        if (p) pclose(p);
        std::printf("corpus: %d files, decoded %d, art %d, icons %d, unnamed %d\n",
                    files, ok, with_art, with_icons, unnamed);
        check(files == 0 || ok == files, "every corpus theme decodes");
        check(unnamed == 0, "every corpus theme resolves a name");
    }

    if (failures) {
        std::fprintf(stderr, "%d hap check(s) failed\n", failures);
        return 1;
    }
    std::printf("hap decoder checks passed\n");
    return 0;
}
