// Skin art images — RGBA pixels + AppearanceEdit caps/positions.
// Load path: TGA32 (uncompressed) preferred; no zlib dependency.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct SkinImage {
    int w = 0, h = 0;
    std::vector<uint32_t> px;           // 0xAARRGGBB; A=0 transparent
    uint8_t caps[4] = {0, 0, 0, 0};      // l, t, r, b
    uint8_t positions[4] = {0, 0, 0, 0}; // l, t, r, b
    // Hap AppearanceEdit Text Color on the plate (0x00RRGGBB); optional.
    bool has_text_color = false;
    uint32_t text_color = 0;

    uint32_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return px[size_t(y) * size_t(w) + size_t(x)];
    }

    bool empty() const { return w <= 0 || h <= 0 || px.empty(); }
};

struct ArtRef {
    std::string path; // relative to skin file directory
    uint8_t caps[4] = {0, 0, 0, 0};
    uint8_t positions[4] = {0, 0, 0, 0};
    bool has_caps = false;
    bool has_positions = false;
    // Hap AppearanceEdit Text Color (0x00RRGGBB); optional art meta.
    bool has_text_color = false;
    uint32_t text_color = 0;
};

// Uncompressed 32-bit TGA (top-left origin), BGRA bytes on disk.
inline bool load_tga32(const std::string &path, SkinImage &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t hdr[18];
    if (!f.read(reinterpret_cast<char *>(hdr), 18)) return false;
    int id_len = hdr[0];
    int ctype = hdr[2];
    int bpp = hdr[16];
    int desc = hdr[17];
    int w = hdr[12] | (hdr[13] << 8);
    int h = hdr[14] | (hdr[15] << 8);
    if (ctype != 2 || bpp != 32 || w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return false;
    if (id_len) f.seekg(id_len, std::ios::cur);
    bool top_left = (desc & 0x20) != 0;
    out.w = w;
    out.h = h;
    out.px.assign(size_t(w) * size_t(h), 0);
    std::vector<uint8_t> row(size_t(w) * 4);
    for (int y = 0; y < h; ++y) {
        if (!f.read(reinterpret_cast<char *>(row.data()), std::streamsize(row.size())))
            return false;
        int dy = top_left ? y : (h - 1 - y);
        for (int x = 0; x < w; ++x) {
            uint8_t b = row[size_t(x) * 4 + 0];
            uint8_t g = row[size_t(x) * 4 + 1];
            uint8_t r = row[size_t(x) * 4 + 2];
            uint8_t a = row[size_t(x) * 4 + 3];
            out.px[size_t(dy) * size_t(w) + size_t(x)] =
                (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) |
                uint32_t(b);
        }
    }
    return true;
}

// Custom binary: "SKIM" + u16le w/h + caps[4] + positions[4] + u32le AARRGGBB[].
inline bool load_skimg(const std::string &path, SkinImage &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    if (!f.read(magic, 4) || std::memcmp(magic, "SKIM", 4) != 0) return false;
    uint8_t wh[4];
    if (!f.read(reinterpret_cast<char *>(wh), 4)) return false;
    int w = wh[0] | (wh[1] << 8);
    int h = wh[2] | (wh[3] << 8);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    if (!f.read(reinterpret_cast<char *>(out.caps), 4)) return false;
    if (!f.read(reinterpret_cast<char *>(out.positions), 4)) return false;
    out.w = w;
    out.h = h;
    out.px.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < out.px.size(); ++i) {
        uint8_t b[4];
        if (!f.read(reinterpret_cast<char *>(b), 4)) return false;
        out.px[i] = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
                    (uint32_t(b[3]) << 24);
    }
    return true;
}

inline bool load_skin_image(const std::string &path, SkinImage &out) {
    out = SkinImage{};
    auto dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    for (char &c : ext)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (ext == ".skimg") return load_skimg(path, out);
    if (ext == ".tga") return load_tga32(path, out);
    // Try TGA then SKIM by sniffing
    if (load_tga32(path, out)) return true;
    return load_skimg(path, out);
}

inline bool save_skimg(const std::string &path, const SkinImage &img) {
    std::ofstream f(path, std::ios::binary);
    if (!f || img.empty()) return false;
    f.write("SKIM", 4);
    uint8_t wh[4] = {uint8_t(img.w & 0xff), uint8_t((img.w >> 8) & 0xff),
                     uint8_t(img.h & 0xff), uint8_t((img.h >> 8) & 0xff)};
    f.write(reinterpret_cast<const char *>(wh), 4);
    f.write(reinterpret_cast<const char *>(img.caps), 4);
    f.write(reinterpret_cast<const char *>(img.positions), 4);
    for (uint32_t p : img.px) {
        uint8_t b[4] = {uint8_t(p), uint8_t(p >> 8), uint8_t(p >> 16),
                        uint8_t(p >> 24)};
        f.write(reinterpret_cast<const char *>(b), 4);
    }
    return bool(f);
}

inline std::string join_path(const std::string &dir, const std::string &rel) {
    if (rel.empty()) return {};
    if (!dir.empty() && (rel[0] == '/' || (rel.size() > 1 && rel[1] == ':')))
        return rel;
    if (dir.empty()) return rel;
    char sep = '/';
    if (dir.find('\\') != std::string::npos) sep = '\\';
    if (dir.back() == '/' || dir.back() == '\\') return dir + rel;
    return dir + sep + rel;
}

inline std::string parent_dir(const std::string &path) {
    auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash);
}
