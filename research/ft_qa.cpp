// Headless File Transfers visual QA — Milk / Ashen / stock.
// Build: g++ -std=c++17 -O2 -Iengine research/bin/ft_qa.cpp -o build/ft_qa
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "appearance.h"

static void write_ppm(const char *path, const Canvas &cv) {
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", cv.width(), cv.height());
    for (int i = 0; i < cv.width() * cv.height(); ++i) {
        uint32_t p = cv.data()[i];
        uint8_t rgb[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("wrote %s\n", path);
}

static void dump_art(const Appearance &ap, const char *slot) {
    const SkinImage *im = ap.art(slot);
    if (!im) {
        std::printf("  %-28s MISSING\n", slot);
        return;
    }
    std::printf("  %-28s %dx%d pos=[%d,%d,%d,%d] caps=[%d,%d,%d,%d]\n", slot, im->w,
                im->h, im->positions[0], im->positions[1], im->positions[2],
                im->positions[3], im->caps[0], im->caps[1], im->caps[2],
                im->caps[3]);
}

static void analyze_segmented_leds(const Canvas &cv, Rect track, int fill_w,
                                   const char *label) {
    // Scan one mid-row of the progress trough for lit LED column widths + gaps.
    if (track.w <= 0 || track.h <= 0) return;
    int y = track.y + track.h / 2;
    if (y < 0 || y >= cv.height()) return;
    auto px = [&](int x) -> uint32_t {
        if (x < 0 || x >= cv.width()) return 0;
        return cv.data()[size_t(y) * cv.width() + x];
    };
    // Sample leftmost pad pixel as trough reference.
    uint32_t trough = px(track.x + 1);
    int run = 0;
    int gap = 0;
    int segs = 0;
    int gaps = 0;
    int sum_seg = 0, sum_gap = 0;
    bool in_seg = false;
    for (int x = track.x; x < track.x + track.w; ++x) {
        uint32_t p = px(x);
        // Treat near-trough as gap (allow ±8 per channel noise).
        int dr = int((p >> 16) & 255) - int((trough >> 16) & 255);
        int dg = int((p >> 8) & 255) - int((trough >> 8) & 255);
        int db = int(p & 255) - int(trough & 255);
        bool is_trough = (dr * dr + dg * dg + db * db) < 48 * 48;
        if (!is_trough) {
            if (!in_seg) {
                if (gap > 0 && segs > 0) {
                    sum_gap += gap;
                    ++gaps;
                }
                gap = 0;
                in_seg = true;
                run = 1;
            } else {
                ++run;
            }
        } else {
            if (in_seg) {
                sum_seg += run;
                ++segs;
                in_seg = false;
                run = 0;
            }
            ++gap;
        }
    }
    if (in_seg) {
        sum_seg += run;
        ++segs;
    }
    double avg_seg = segs ? double(sum_seg) / segs : 0;
    double avg_gap = gaps ? double(sum_gap) / gaps : 0;
    std::printf("  LED scan %s: segs=%d avg_w=%.2f gaps=%d avg_gap=%.2f "
                "(fill_art_w=%d track=%dx%d)\n",
                label, segs, avg_seg, gaps, avg_gap, fill_w, track.w, track.h);
}

static void dump_theme(const char *label, Appearance &ap, const char *out_dir) {
    std::printf("\n=== %s (%s) ===\n", label, ap.skin.meta.name.c_str());
    dump_art(ap, "progress.bar");
    dump_art(ap, "progress.fill");
    const char *wl[] = {"wonderlight.off",     "wonderlight.pause",
                        "wonderlight.ready",   "wonderlight.go",
                        "wonderlight.finished","wonderlight.flash_off",
                        "wonderlight.flash_on1","wonderlight.flash_on2"};
    for (auto s : wl) dump_art(ap, s);

    char path[512];

    Canvas preview;
    preview.resize(640, 480);
    preview.clear(rgb(0, 0, 0));
    paint_kit_preview(preview, ap, {0, 0, 640, 480}, true, 1, 0);
    std::snprintf(path, sizeof(path), "%s/kit-preview-ft-%s.ppm", out_dir, label);
    write_ppm(path, preview);

    Canvas ft;
    ft.resize(460, 200);
    ft.clear(rgb(48, 48, 48));
    paint_file_transfers_window(ft, ap, {0, 0, 460, 200});
    std::snprintf(path, sizeof(path), "%s/file-transfers-%s.ppm", out_dir, label);
    write_ppm(path, ft);

    // Continuous vs Segmented compare strip
    Canvas strip;
    strip.resize(420, 90);
    strip.clear(rgb(60, 60, 60));
    strip.text(8, 6, "Continuous 65%", rgb(255, 255, 255));
    Rect cont{8, 22, 400, 16};
    paint_progress(strip, ap, cont, 65, 100, ProgressStyle::Continuous, nullptr);
    strip.text(8, 46, "Segmented 42%", rgb(255, 255, 255));
    Rect seg{8, 62, 400, 16};
    paint_progress(strip, ap, seg, 42, 100, ProgressStyle::Segmented, nullptr);
    std::snprintf(path, sizeof(path), "%s/progress-compare-%s.ppm", out_dir, label);
    write_ppm(path, strip);

    const SkinImage *fill = ap.art("progress.fill");
    int fill_w = fill ? fill->w : 4;
    int ph = progress_art_height(ap.art("progress.bar"), fill);
    Rect seg_track{8, 62 + (16 - ph) / 2, 400, ph};
    analyze_segmented_leds(strip, seg_track, fill_w, label);

    // WonderLight state strip (Off Pause Ready Go Finished FlashOff On1 On2)
    Canvas lamps;
    lamps.resize(280, 40);
    lamps.clear(rgb(50, 50, 50));
    WonderLightState states[] = {
        WonderLightState::Off,       WonderLightState::Pause,
        WonderLightState::Ready,     WonderLightState::Go,
        WonderLightState::Finished,  WonderLightState::FlashOff,
        WonderLightState::FlashOn1,  WonderLightState::FlashOn2};
    for (int i = 0; i < 8; ++i)
        paint_wonderlight(lamps, ap, 8 + i * 34, 12, states[i]);
    std::snprintf(path, sizeof(path), "%s/wonderlights-%s.ppm", out_dir, label);
    write_ppm(path, lamps);

    // Colour-path tint demo (stock LEDs only meaningful; Hap ignores tint)
    Canvas tint;
    tint.resize(420, 70);
    tint.clear(rgb(40, 40, 40));
    Color green = rgb(0, 200, 40);
    Color blue = rgb(40, 120, 255);
    Color amber = rgb(220, 180, 0);
    tint.text(8, 4, "Segmented + WonderLight tint (colour path)", rgb(220, 220, 220));
    paint_progress(tint, ap, {8, 20, 130, 16}, 70, 100, ProgressStyle::Segmented,
                   &green);
    paint_progress(tint, ap, {148, 20, 130, 16}, 100, 100, ProgressStyle::Segmented,
                   &blue);
    paint_progress(tint, ap, {288, 20, 130, 16}, 20, 100, ProgressStyle::Segmented,
                   &amber);
    tint.text(8, 44, "Go tint", rgb(180, 180, 180));
    tint.text(148, 44, "Finished tint", rgb(180, 180, 180));
    tint.text(288, 44, "Pause tint", rgb(180, 180, 180));
    std::snprintf(path, sizeof(path), "%s/led-tint-%s.ppm", out_dir, label);
    write_ppm(path, tint);
}

int main() {
    const char *outdir = "/opt/cursor/artifacts/kdx-ft-qa";
    mkdir(outdir, 0755);
    mkdir("build/kdx-ft-qa", 0755);

    struct Case {
        const char *label;
        const char *path;
    };
    Case cases[] = {
        {"stock", "format/skins/stock.sap"},
        {"milk", "research/haps/Milk Redux.hap"},
        {"ashen", "research/haps/Ashen.hap"},
    };
    for (auto &c : cases) {
        Appearance ap;
        if (!ap.load(c.path)) {
            std::fprintf(stderr, "load failed: %s\n", c.path);
            continue;
        }
        dump_theme(c.label, ap, outdir);
        // Mirror into repo build/ for commit-able reference copies of reports
        // (PPMs stay in artifacts; report is text-only in research/).
    }

    // Also dump milk/ashen into build for local inspection
    {
        Appearance milk;
        if (milk.load("research/haps/Milk Redux.hap")) {
            Canvas ft;
            ft.resize(460, 200);
            ft.clear(rgb(48, 48, 48));
            paint_file_transfers_window(ft, milk, {0, 0, 460, 200});
            write_ppm("build/kdx-ft-qa/file-transfers-milk.ppm", ft);
        }
        Appearance ashen;
        if (ashen.load("research/haps/Ashen.hap")) {
            Canvas ft;
            ft.resize(460, 200);
            ft.clear(rgb(48, 48, 48));
            paint_file_transfers_window(ft, ashen, {0, 0, 460, 200});
            write_ppm("build/kdx-ft-qa/file-transfers-ashen.ppm", ft);
        }
    }
    return 0;
}
