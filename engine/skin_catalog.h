// Bundled appearances next to the exe: format/skins/*.hap|*.sap and */*.sap.
// Apps list these under Appearance so Load… is only for extras.
#pragma once

#include "hap.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace sagrado {

struct BundledSkin {
    std::string name;
    std::string path;
};

inline std::string path_stem(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string stem =
        slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos && dot > 0) stem.resize(dot);
    return stem;
}

inline std::string skin_ascii_lower(std::string s) {
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool ends_with_ci(const std::string &s, const char *ext) {
    size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = (char)std::tolower((unsigned char)s[s.size() - n + i]);
        char b = (char)std::tolower((unsigned char)ext[i]);
        if (a != b) return false;
    }
    return true;
}

// Hap Info name only — no image decode.
inline std::string peek_hap_name(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return path_stem(path);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    using namespace haputil;
    if (d.size() < 0x90 || d[0] != '%' || d[1] != 'H' || d[2] != 'A' ||
        d[3] != 'P')
        return path_stem(path);
    size_t info_off = rd32(d, 0x2c), info_len = rd32(d, 0x30);
    std::string name;
    if (info_len >= 0x34 && info_off + 0x34 <= d.size()) {
        size_t l = d[info_off + 0x22];
        size_t s = info_off + 0x34;
        if (l > 0 && s + l <= d.size())
            name.assign(reinterpret_cast<const char *>(&d[s]), l);
    }
    return name.empty() ? path_stem(path) : name;
}

// First [meta] name = "…" (or bare name =) in a .sap TOML.
inline std::string peek_sap_name(const std::string &path) {
    std::ifstream f(path);
    if (!f) return path_stem(path);
    std::string line;
    bool in_meta = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            in_meta = (line == "[meta]");
            continue;
        }
        if (!in_meta && line.find("name") != 0) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        if (key != "name") continue;
        std::string val = line.substr(eq + 1);
        size_t a = val.find('"');
        size_t b = a == std::string::npos ? std::string::npos : val.find('"', a + 1);
        if (a != std::string::npos && b != std::string::npos && b > a + 1)
            return val.substr(a + 1, b - a - 1);
    }
    return path_stem(path);
}

inline std::string skin_display_name(const std::string &path) {
    if (ends_with_ci(path, ".hap")) return peek_hap_name(path);
    if (ends_with_ci(path, ".sap")) return peek_sap_name(path);
    return path_stem(path);
}

#ifdef _WIN32
inline bool skin_file_exists(const std::string &p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline void append_glob(const std::string &dir, const char *pattern,
                        std::vector<std::string> *out) {
    if (!out) return;
    WIN32_FIND_DATAA fd{};
    std::string glob = dir + "\\" + pattern;
    HANDLE h = FindFirstFileA(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        out->push_back(dir + "\\" + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

inline void append_subdir_saps(const std::string &root,
                               std::vector<std::string> *out) {
    if (!out) return;
    WIN32_FIND_DATAA fd{};
    std::string glob = root + "\\*";
    HANDLE h = FindFirstFileA(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        append_glob(root + "\\" + fd.cFileName, "*.sap", out);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#else
inline bool skin_file_exists(const std::string &p) {
    struct ::stat st {};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
inline bool match_ext(const char *name, const char *pattern) {
    // pattern like "*.hap" / "*.sap"
    if (!pattern || pattern[0] != '*' || pattern[1] != '.') return false;
    return ends_with_ci(name, pattern + 1);
}
inline void append_glob(const std::string &dir, const char *pattern,
                        std::vector<std::string> *out) {
    if (!out) return;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!match_ext(e->d_name, pattern)) continue;
        std::string p = dir + "/" + e->d_name;
        if (skin_file_exists(p)) out->push_back(p);
    }
    closedir(d);
}
inline void append_subdir_saps(const std::string &root,
                               std::vector<std::string> *out) {
    if (!out) return;
    DIR *d = opendir(root.c_str());
    if (!d) return;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string sub = root + "/" + e->d_name;
        struct ::stat st {};
        if (::stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        append_glob(sub, "*.sap", out);
    }
    closedir(d);
}
#endif

// Prefer Gamespot → Milk → Ooze → stock under format/skins/ (shipped by make skins).
inline char skin_path_sep() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

inline std::string join_skin_path(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    char sep = skin_path_sep();
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}

inline std::string find_default_bundled_skin(const std::string &exe_dir) {
    const char *cands[] = {
        "format/skins/Gamespot-1100.hap",
        "../format/skins/Gamespot-1100.hap",
        "format/skins/Milk Redux.hap",
        "../format/skins/Milk Redux.hap",
        "format/skins/milk-redux/milk-redux.sap",
        "../format/skins/milk-redux/milk-redux.sap",
        "format/skins/ooze/ooze.sap",
        "../format/skins/ooze/ooze.sap",
        "format/skins/stock.sap",
        "../format/skins/stock.sap",
        // Dev tree before `make skins` (optional).
        "../research/haps/Gamespot-1100.hap",
        "../../research/haps/Gamespot-1100.hap",
        "research/haps/Gamespot-1100.hap",
        "../research/haps/Milk Redux.hap",
        "../../research/haps/Milk Redux.hap",
        "research/haps/Milk Redux.hap",
    };
    for (const char *rel : cands) {
        std::string p = join_skin_path(exe_dir, rel);
        // Also try backslash form for Win32 module dirs.
        std::string pw = p;
        for (char &c : pw)
            if (c == '/') c = '\\';
        if (skin_file_exists(p)) return p;
        if (pw != p && skin_file_exists(pw)) return pw;
    }
    return {};
}

inline std::string bundled_skins_root(const std::string &exe_dir) {
    std::string a = join_skin_path(exe_dir, "format/skins");
    if (skin_file_exists(join_skin_path(a, "Gamespot-1100.hap")) ||
        skin_file_exists(join_skin_path(a, "stock.sap")) ||
        skin_file_exists(join_skin_path(a, "Milk Redux.hap")))
        return a;
    std::string b = join_skin_path(exe_dir, "../format/skins");
    if (skin_file_exists(join_skin_path(b, "Gamespot-1100.hap")) ||
        skin_file_exists(join_skin_path(b, "stock.sap")) ||
        skin_file_exists(join_skin_path(b, "Milk Redux.hap")))
        return b;
    return a;
}

// Installed themes for Appearance menus. Dedupes by display name (prefer .hap).
inline std::vector<BundledSkin> list_bundled_skins(const std::string &exe_dir) {
    std::string root = bundled_skins_root(exe_dir);
    std::vector<std::string> paths;
    append_glob(root, "*.hap", &paths);
    append_glob(root, "*.sap", &paths);
    append_subdir_saps(root, &paths);

    std::vector<BundledSkin> out;
    out.reserve(paths.size());
    for (const std::string &p : paths) {
        // stock.sap is the colour dump; menu keeps a dedicated Stock command.
        if (skin_ascii_lower(path_stem(p)) == "stock") continue;
        BundledSkin s;
        s.path = p;
        s.name = skin_display_name(p);
        if (s.name.empty()) s.name = path_stem(p);
        out.push_back(std::move(s));
    }

    std::sort(out.begin(), out.end(), [](const BundledSkin &a, const BundledSkin &b) {
        std::string na = skin_ascii_lower(a.name), nb = skin_ascii_lower(b.name);
        if (na != nb) return na < nb;
        bool ah = ends_with_ci(a.path, ".hap"), bh = ends_with_ci(b.path, ".hap");
        if (ah != bh) return ah; // .hap first
        return a.path < b.path;
    });

    std::vector<BundledSkin> uniq;
    uniq.reserve(out.size());
    for (auto &s : out) {
        if (!uniq.empty() &&
            skin_ascii_lower(uniq.back().name) == skin_ascii_lower(s.name))
            continue;
        uniq.push_back(std::move(s));
    }
    return uniq;
}

} // namespace sagrado
