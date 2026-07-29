// Corpus probe: run every .hap in a directory through the engine's own
// decoder (engine/hap.h) + Hap→skin mapping (engine/hap_skin.h) and report
//   * decode failures
//   * per-slot occupancy across the corpus, with geometry ranges
//   * occupied slots the kit does not map yet (fidelity gaps)
//   * mapped keys no probed theme authors
//   * colour-table anomalies
// Build: g++ -std=c++17 -Iengine research/hap_corpus.cpp -o build/hap_corpus
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hap_skin.h" // pulls engine/hap.h — do not include "hap.h" here
                      // (research/hap.h would shadow it)

namespace {

struct SlotStat {
    std::set<std::string> themes;
    int min_w = 1 << 30, max_w = 0, min_h = 1 << 30, max_h = 0;
    int caps_min[4] = {255, 255, 255, 255};
    int caps_max[4] = {0, 0, 0, 0};
    int pos_min[4] = {255, 255, 255, 255};
    int pos_max[4] = {0, 0, 0, 0};

    void add(const std::string &theme, const ThemeImage &img) {
        themes.insert(theme);
        min_w = std::min(min_w, img.w);
        max_w = std::max(max_w, img.w);
        min_h = std::min(min_h, img.h);
        max_h = std::max(max_h, img.h);
        for (int i = 0; i < 4; ++i) {
            caps_min[i] = std::min(caps_min[i], int(img.caps[i]));
            caps_max[i] = std::max(caps_max[i], int(img.caps[i]));
            pos_min[i] = std::min(pos_min[i], int(img.positions[i]));
            pos_max[i] = std::max(pos_max[i], int(img.positions[i]));
        }
    }
    bool any_positions() const {
        for (int i = 0; i < 4; ++i)
            if (pos_max[i]) return true;
        return false;
    }
};

std::string art_key(int slot) {
    for (int i = 0; i < kHapArtMapN; ++i)
        if (kHapArtMap[i].slot == slot) return kHapArtMap[i].key;
    return {};
}

std::string icon_key(int slot) {
    for (int i = 0; i < kHapIconMapN; ++i)
        if (kHapIconMap[i].slot == slot) return kHapIconMap[i].key;
    return {};
}

void print_table(const char *title, const std::map<int, SlotStat> &stats,
                 int total, bool icons) {
    std::printf("\n=== %s ===\n", title);
    std::printf("%-5s %-38s %-6s %-11s %-22s %s\n", "slot", "kit key", "themes",
                "size", "caps l,t,r,b", "positions l,t,r,b");
    for (const auto &[slot, st] : stats) {
        std::string key = icons ? icon_key(slot) : art_key(slot);
        char size[32], caps[32], pos[32];
        if (st.min_w == st.max_w && st.min_h == st.max_h)
            std::snprintf(size, sizeof(size), "%dx%d", st.min_w, st.min_h);
        else
            std::snprintf(size, sizeof(size), "%d-%dx%d-%d", st.min_w, st.max_w,
                          st.min_h, st.max_h);
        auto rng = [](char *dst, size_t n, const int *lo, const int *hi) {
            int off = 0;
            for (int i = 0; i < 4; ++i) {
                if (lo[i] == hi[i])
                    off += std::snprintf(dst + off, n - off, "%d", lo[i]);
                else
                    off += std::snprintf(dst + off, n - off, "%d-%d", lo[i], hi[i]);
                if (i < 3) off += std::snprintf(dst + off, n - off, ",");
            }
        };
        rng(caps, sizeof(caps), st.caps_min, st.caps_max);
        rng(pos, sizeof(pos), st.pos_min, st.pos_max);
        std::printf("%-5d %-38s %3zu/%-3d %-11s %-22s %s\n", slot,
                    key.empty() ? "-- UNMAPPED --" : key.c_str(),
                    st.themes.size(), total, size, caps, pos);
    }
}

} // namespace

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "research/haps";
    DIR *dp = opendir(dir.c_str());
    if (!dp) {
        std::fprintf(stderr, "cannot open %s\n", dir.c_str());
        return 1;
    }
    std::vector<std::string> files;
    while (dirent *e = readdir(dp)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".hap") == 0)
            files.push_back(dir + "/" + n);
    }
    closedir(dp);
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "no .hap files in %s\n", dir.c_str());
        return 1;
    }

    std::map<int, SlotStat> art, icons;
    std::vector<std::string> failed, no_name, short_table;
    int ok = 0, with_art = 0, with_icons = 0;

    for (const std::string &path : files) {
        Theme t;
        if (!load_hap(path, t)) {
            failed.push_back(path);
            continue;
        }
        ++ok;
        std::string name = t.name.empty() ? path : t.name;
        if (t.name.empty()) no_name.push_back(path);
        if (!t.images.empty()) ++with_art;
        if (!t.icons.empty()) ++with_icons;
        for (const auto &[slot, img] : t.images) art[slot].add(name, img);
        for (const auto &[slot, img] : t.icons) icons[slot].add(name, img);
    }

    std::printf("corpus: %zu files, decoded %d, failed %zu\n", files.size(), ok,
                failed.size());
    std::printf("themes with art: %d   with icons: %d\n", with_art, with_icons);
    for (const std::string &f : failed) std::printf("  DECODE FAIL: %s\n", f.c_str());
    for (const std::string &f : no_name) std::printf("  no metadata name: %s\n", f.c_str());

    print_table("image slots", art, ok, false);
    print_table("icon slots", icons, ok, true);

    // Unmapped occupancy, most common first — the fidelity backlog.
    std::vector<std::pair<size_t, int>> unmapped;
    for (const auto &[slot, st] : art)
        if (art_key(slot).empty()) unmapped.push_back({st.themes.size(), slot});
    std::sort(unmapped.rbegin(), unmapped.rend());
    std::printf("\n=== unmapped image slots by popularity (%zu slots) ===\n",
                unmapped.size());
    for (auto [n, slot] : unmapped)
        std::printf("  slot %-4d in %zu themes  %dx%d..%dx%d\n", slot, n,
                    art.at(slot).min_w, art.at(slot).min_h, art.at(slot).max_w,
                    art.at(slot).max_h);

    std::vector<std::pair<size_t, int>> uicons;
    for (const auto &[slot, st] : icons)
        if (icon_key(slot).empty()) uicons.push_back({st.themes.size(), slot});
    std::sort(uicons.rbegin(), uicons.rend());
    std::printf("\n=== unmapped icon slots by popularity (%zu slots) ===\n",
                uicons.size());
    for (auto [n, slot] : uicons)
        std::printf("  icon %-4d in %zu themes  %dx%d\n", slot, n,
                    icons.at(slot).min_w, icons.at(slot).min_h);

    // Mapped keys nobody in the corpus authors.
    std::printf("\n=== mapped art keys absent from the whole corpus ===\n");
    for (int i = 0; i < kHapArtMapN; ++i)
        if (!art.count(kHapArtMap[i].slot))
            std::printf("  slot %-4d %s\n", kHapArtMap[i].slot, kHapArtMap[i].key);
    for (int i = 0; i < kHapIconMapN; ++i)
        if (!icons.count(kHapIconMap[i].slot))
            std::printf("  icon %-4d %s\n", kHapIconMap[i].slot, kHapIconMap[i].key);

    // Slots that carry positions anywhere in the corpus (must be honoured).
    std::printf("\n=== slots with non-zero positions somewhere ===\n");
    for (const auto &[slot, st] : art) {
        if (!st.any_positions()) continue;
        std::string key = art_key(slot);
        std::printf("  slot %-4d %-38s pos %d-%d,%d-%d,%d-%d,%d-%d\n", slot,
                    key.empty() ? "-- UNMAPPED --" : key.c_str(), st.pos_min[0],
                    st.pos_max[0], st.pos_min[1], st.pos_max[1], st.pos_min[2],
                    st.pos_max[2], st.pos_min[3], st.pos_max[3]);
    }
    return failed.empty() ? 0 : 2;
}
