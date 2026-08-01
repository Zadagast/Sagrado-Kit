// SagradoKit Editor — AppearanceEdit-style authoring app.
// Entire UI painted into a software framebuffer and blitted with
// SetDIBitsToDevice. Edits the same .sap format apps load.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

#include "../engine/appearance.h"
#include "../engine/hfnt.h"

namespace {

constexpr int kWinW = 1040;
constexpr int kWinH = 700;
constexpr int kRoleRowH = 20;
constexpr int kSwatchW = 28;
constexpr int kPanelTabH = 22;
constexpr int kPanelTabW = 88;

enum Panel : int {
    PanelColors = 0,
    PanelInfo,
    PanelImages,
    PanelIcons,
    PanelGroups,
    PanelCount,
};

enum Drag : int {
    DragNone = 0,
    DragThumbRoles,
    DragThumbPreview,
    DragSliderR,
    DragSliderG,
    DragSliderB,
    DragBtnLoad,
    DragBtnSave,
    DragBtnStock,
    DragBtnImportColors,
    DragBtnColorsOnly,
    DragBtnPaste,
    DragBtnTrans,
    DragPreviewBtn,
    DragCloseBox,
    DragMaxBox,
    DragMinBox,
    DragScrollArrowRoles,
    DragScrollArrowPreview,
    DragScrollArrowPreviewH,
    DragThumbPreviewH,
    DragSliderKit,
    DragDropdown,
    DragPanelTab,
    DragGroupBase,
    DragImgNudge,
};

struct App {
    Canvas canvas;
    Font font;             // a real Haxial %FNT face, when one was loaded
    Appearance ap;
    GelLayout gel{};
    KitPreviewLayout preview_lay{};
    bool focused = true;
    bool caret_on = true;
    bool dirty = false;
    // Focus: -1 none; 0..3 Info fields; 10 hex field (Colors/Groups/Images/Icons Text Color)
    int focus = -1;
    std::string hex_buf;
    int group_sel = 0;

    int panel = PanelColors;
    int scroll = 0;
    int selected = 0;
    int list_sel = 1;
    int preview_scroll = 0;
    KitPreviewState preview_st{};
    int asset_sel = 0; // images / icons list selection
    Color group_base{180, 180, 180};
    int trans_mode = 0; // 0=None 1=White 2=Red 3=Green 4=Blue (last applied)

    int drag = DragNone;
    int drag_btn = 0;       // active preview button id (1..3) while over it
    int drag_target = 0;    // original press target (survives move-off)
    int thumb_grab = 0;     // mouse y offset within thumb
    int arrow_dir = 0;      // -1 up / +1 down
    ScrollArrowHot arrow_hot = ScrollArrowHot::None;
    int pressed_box = 0;
    int h_thumb_grab = 0;   // mouse x offset within H thumb
    int nudge_which = 0;    // 0..7 caps, 8..15 pos; low bit unused, encode as (kind<<4)|axis

    std::string path;
    std::string status = "Stock skin — edit colours, Save to write a .sap";

    Rect role_list{};
    Rect role_sbar{};
    Rect preview{};
    Rect btn_load{}, btn_save{}, btn_stock{};
    Rect btn_import_colors{}, btn_colors_only{};
    Rect panel_tabs[PanelCount]{};
    Rect slider_r{}, slider_g{}, slider_b{};
    Rect hex_field{};
    Rect group_swatch{};
    Rect info_fields[4]{};

    // Images authoring strip
    Rect img_thumb{};
    Rect img_cap_minus[4]{}, img_cap_plus[4]{};
    Rect img_pos_minus[4]{}, img_pos_plus[4]{};
    Rect btn_paste{};
    Rect btn_trans[5]{};

    int roles_page() const {
        int body_h = role_list.h - kHeaderH;
        return std::max(1, body_h / kRoleRowH);
    }
    int roles_max_scroll() const {
        if (panel == PanelColors)
            return std::max(0, (int)all_color_roles().size() - roles_page());
        if (panel == PanelImages)
            return std::max(0, (int)all_hap_art_keys().size() - roles_page());
        if (panel == PanelIcons)
            return std::max(0, (int)all_hap_icon_keys().size() - roles_page());
        if (panel == PanelGroups)
            return std::max(0, 7 - roles_page()); // kColorGroupN
        return 0;
    }
    int preview_max_scroll() const {
        return std::max(0, preview_lay.row_count - preview_lay.page_rows);
    }
} g;

HWND g_hwnd = nullptr;

std::string exe_dir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t cut = p.find_last_of("\\/");
    return cut == std::string::npos ? "." : p.substr(0, cut);
}

std::string find_default_skin() {
    std::string dir = exe_dir();
    // Gamespot is the default appearance; Milk / stock are fallbacks.
    const char *cands[] = {
        "\\..\\research\\haps\\Gamespot-1100.hap",
        "\\..\\..\\research\\haps\\Gamespot-1100.hap",
        "\\research\\haps\\Gamespot-1100.hap",
        "\\format\\skins\\Gamespot-1100.hap",
        "\\..\\format\\skins\\Gamespot-1100.hap",
        "\\..\\research\\haps\\Milk Redux.hap",
        "\\..\\..\\research\\haps\\Milk Redux.hap",
        "\\research\\haps\\Milk Redux.hap",
        "\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\..\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\..\\..\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\format\\skins\\stock.sap",
        "\\..\\format\\skins\\stock.sap",
        "\\..\\..\\format\\skins\\stock.sap",
        "\\skins\\stock.sap"};
    for (const char *c : cands) {
        std::string p = dir + c;
        DWORD a = GetFileAttributesA(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
            return p;
    }
    return {};
}

Color selected_color() {
    const auto &roles = all_color_roles();
    if (g.selected < 0 || g.selected >= (int)roles.size()) return rgb(0, 0, 0);
    return g.ap.c(roles[size_t(g.selected)].path);
}

void set_selected_color(Color c) {
    const auto &roles = all_color_roles();
    if (g.selected < 0 || g.selected >= (int)roles.size()) return;
    g.ap.set_color(roles[size_t(g.selected)].path, c);
    g.dirty = true;
}

void set_status(const std::string &s) { g.status = s; }

void mark_dirty(const std::string &why = {}) {
    g.dirty = true;
    if (!why.empty()) set_status(why);
}

void clamp_scroll() {
    g.scroll = std::clamp(g.scroll, 0, g.roles_max_scroll());
    g.preview_scroll = std::clamp(g.preview_scroll, 0, g.preview_max_scroll());
}

bool dialog_open_path(std::string &out) {
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter =
        "Appearance (*.hap;*.sap)\0*.hap;*.sap\0"
        "Haxial Appearance (*.hap)\0*.hap\0"
        "Sagrado Appearance (*.sap)\0*.sap\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "hap";
    if (!GetOpenFileNameA(&ofn)) return false;
    out = file;
    return true;
}

bool dialog_save_path(std::string &out) {
    char file[MAX_PATH] = "untitled.sap";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Sagrado Appearance (*.sap)\0*.sap\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "sap";
    if (!GetSaveFileNameA(&ofn)) return false;
    out = file;
    return true;
}

void do_load() {
    std::string path;
    if (!dialog_open_path(path)) return;
    if (g.ap.load(path)) {
        g.path = path;
        bool hap = path.size() >= 4 &&
                   (path.compare(path.size() - 4, 4, ".hap") == 0 ||
                    path.compare(path.size() - 4, 4, ".HAP") == 0);
        // Echo the theme's own Info fields, the way AppearanceEdit titles a
        // loaded appearance, rather than just the path.
        const SkinMeta &m = g.ap.skin.meta;
        std::string s = std::string(hap ? "Loaded Hap " : "Loaded ") + m.name;
        if (!m.version.empty()) s += " " + m.version;
        if (!m.creator.empty()) s += " by " + m.creator;
        set_status(s);
    } else {
        set_status("Failed to load " + path);
    }
}

void do_save() {
    std::string path = g.path;
    auto ends_with_ci = [](const std::string &s, const char *ext) {
        size_t n = std::strlen(ext);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = s[s.size() - n + i];
            char b = ext[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };
    // .hap is import-only — Save always writes .sap (with art dumped beside it).
    if (path.empty() || ends_with_ci(path, ".hap")) {
        if (!dialog_save_path(path)) return;
    }
    if (path.empty()) return;
    if (g.ap.skin.meta.name.empty() || g.ap.skin.meta.name == "Stock")
        g.ap.skin.meta.name = "Untitled";
    if (g.ap.save(path)) {
        g.path = path;
        g.dirty = false;
        set_status("Saved " + path);
    } else {
        set_status("Failed to save " + path);
    }
}

void do_stock() {
    g.ap.set_skin(stock_skin());
    g.path.clear();
    set_status("Reset to stock");
}

// Import Colors — copy colour table from another Hap/Sap (AppearanceEdit Window Menu).
void do_import_colors() {
    std::string path;
    if (!dialog_open_path(path)) return;
    Appearance donor;
    if (!donor.load(path)) {
        set_status("Import Colors failed: " + path);
        return;
    }
    int n = 0;
    for (const auto &kv : donor.skin.colors) {
        g.ap.skin.colors[kv.first] = kv.second;
        ++n;
    }
    set_status("Imported " + std::to_string(n) + " colours from " + path);
}

// ♦ Colour Groups — derive related roles from a base (AppearanceEdit diamonds).
struct ColorGroupDef {
    const char *name;
    const char *bg_role; // role used to seed the base swatch
    enum Kind { Primary4, Bevel6, Face4, Progress4, List } kind;
};

static const ColorGroupDef kColorGroups[] = {
    {"♦ Primary Group", "primary.background", ColorGroupDef::Primary4},
    {"♦ Button Group", "button.face", ColorGroupDef::Bevel6},
    {"♦ Window Group", "window.face", ColorGroupDef::Bevel6},
    {"♦ List Group", "list.background", ColorGroupDef::List},
    {"♦ Column Header Group", "column_header.face", ColorGroupDef::Face4},
    {"♦ Progress Group", "progress.bkgnd", ColorGroupDef::Progress4},
    {"♦ Scrollbar Group", "scrollbar.face", ColorGroupDef::Face4},
};
static constexpr int kColorGroupN = sizeof(kColorGroups) / sizeof(kColorGroups[0]);

void apply_color_group(int gi, Color base) {
    if (gi < 0 || gi >= kColorGroupN) return;
    g.group_sel = gi;
    g.group_base = base;
    auto clamp8 = [](int v) -> uint8_t {
        return uint8_t(std::clamp(v, 0, 255));
    };
    auto shade = [&](int d) -> Color {
        return {clamp8(int(base.r) + d), clamp8(int(base.g) + d),
                clamp8(int(base.b) + d)};
    };
    const ColorGroupDef &gd = kColorGroups[gi];
    switch (gd.kind) {
    case ColorGroupDef::Primary4:
        g.ap.set_color("primary.light", shade(+40));
        g.ap.set_color("primary.background", base);
        g.ap.set_color("primary.dark", shade(-40));
        g.ap.set_color("primary.frame", shade(-70));
        break;
    case ColorGroupDef::Bevel6: {
        const char *p = (gi == 1) ? "button" : "window";
        char key[48];
        auto set = [&](const char *suf, Color c) {
            std::snprintf(key, sizeof(key), "%s.%s", p, suf);
            g.ap.set_color(key, c);
        };
        set("light2", shade(+55));
        set("light1", shade(+30));
        set("face", base);
        set("dark1", shade(-30));
        set("dark2", shade(-55));
        set("frame", shade(-80));
        break;
    }
    case ColorGroupDef::Face4: {
        const char *p = (gi == 4) ? "column_header" : "scrollbar";
        char key[48];
        auto set = [&](const char *suf, Color c) {
            std::snprintf(key, sizeof(key), "%s.%s", p, suf);
            g.ap.set_color(key, c);
        };
        set("light", shade(+40));
        set("face", base);
        set("dark", shade(-40));
        set("frame", shade(-70));
        break;
    }
    case ColorGroupDef::Progress4:
        g.ap.set_color("progress.bkgnd_light", shade(+40));
        g.ap.set_color("progress.bkgnd", base);
        g.ap.set_color("progress.bkgnd_dark", shade(-40));
        g.ap.set_color("progress.frame", shade(-70));
        break;
    case ColorGroupDef::List:
        g.ap.set_color("list.background", base);
        g.ap.set_color("list.sort_column_background", shade(-12));
        g.ap.set_color("list.separator", shade(-50));
        break;
    }
    mark_dirty(std::string("Group: ") + gd.name);
}

void seed_group_base_from_sel() {
    if (g.group_sel < 0 || g.group_sel >= kColorGroupN) g.group_sel = 0;
    g.group_base = g.ap.c(kColorGroups[g.group_sel].bg_role);
}

// --- Images / Icons authoring --------------------------------------------

std::vector<std::string> art_keys_list() {
    // Full Hap Images catalog — empty slots listed so Paste can fill them.
    return all_hap_art_keys();
}

std::vector<std::string> icon_keys_list() { return all_hap_icon_keys(); }

std::string selected_art_key() {
    auto keys = art_keys_list();
    if (g.asset_sel < 0 || g.asset_sel >= (int)keys.size()) return {};
    return keys[size_t(g.asset_sel)];
}

SkinImage *selected_art_mutable() {
    std::string key = selected_art_key();
    if (key.empty()) return nullptr;
    auto it = g.ap.art_cache.find(key);
    if (it == g.ap.art_cache.end() || it->second.empty()) return nullptr;
    return &it->second;
}

void sync_selected_art_ref() {
    std::string key = selected_art_key();
    SkinImage *img = selected_art_mutable();
    if (key.empty() || !img) return;
    ArtRef &ref = g.ap.skin.art[key];
    if (ref.path.empty()) {
        std::string n = key;
        for (char &c : n)
            if (c == '.') c = '_';
        ref.path = n + ".skimg";
    }
    std::memcpy(ref.caps, img->caps, 4);
    std::memcpy(ref.positions, img->positions, 4);
    ref.has_caps = true;
    ref.has_positions = true;
    ref.has_text_color = img->has_text_color;
    ref.text_color = img->text_color;
}

void clamp_caps(SkinImage &img) {
    while (img.w > 0 && int(img.caps[0]) + int(img.caps[2]) >= img.w) {
        if (img.caps[2] > 0) --img.caps[2];
        else if (img.caps[0] > 0) --img.caps[0];
        else break;
    }
    while (img.h > 0 && int(img.caps[1]) + int(img.caps[3]) >= img.h) {
        if (img.caps[3] > 0) --img.caps[3];
        else if (img.caps[1] > 0) --img.caps[1];
        else break;
    }
}

void nudge_cap(int axis, int delta) {
    SkinImage *img = selected_art_mutable();
    if (!img || axis < 0 || axis > 3) return;
    int v = int(img->caps[axis]) + delta;
    img->caps[axis] = uint8_t(std::clamp(v, 0, 255));
    clamp_caps(*img);
    sync_selected_art_ref();
    static const char *labs[] = {"L", "T", "R", "B"};
    set_status(std::string("Caps ") + labs[axis] + " = " +
               std::to_string(img->caps[axis]));
}

void nudge_pos(int axis, int delta) {
    SkinImage *img = selected_art_mutable();
    if (!img || axis < 0 || axis > 3) return;
    int v = int(img->positions[axis]) + delta;
    img->positions[axis] = uint8_t(std::clamp(v, 0, 255));
    sync_selected_art_ref();
    static const char *labs[] = {"L", "T", "R", "B"};
    set_status(std::string("Positions ") + labs[axis] + " = " +
               std::to_string(img->positions[axis]));
}

Color selected_art_text_color() {
    SkinImage *img = selected_art_mutable();
    if (!img || !img->has_text_color) return rgb(0, 0, 0);
    return plate_text_color(img);
}

void set_selected_art_text_color(Color c) {
    SkinImage *img = selected_art_mutable();
    if (!img) return;
    img->has_text_color = true;
    img->text_color = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.b);
    sync_selected_art_ref();
    mark_dirty();
}

// AppearanceEdit Transparent Color menu: None / White / 100% R/G/B.
void apply_transparent_color(int mode) {
    SkinImage *img = selected_art_mutable();
    if (!img || img->px.empty()) return;
    g.trans_mode = mode;
    for (uint32_t &p : img->px) {
        uint8_t r = uint8_t((p >> 16) & 0xff);
        uint8_t gch = uint8_t((p >> 8) & 0xff);
        uint8_t b = uint8_t(p & 0xff);
        bool match = false;
        if (mode == 0) {
            // None → all opaque
            p = 0xff000000u | (p & 0x00ffffffu);
            continue;
        } else if (mode == 1)
            match = (r == 255 && gch == 255 && b == 255);
        else if (mode == 2)
            match = (r == 255 && gch == 0 && b == 0);
        else if (mode == 3)
            match = (r == 0 && gch == 255 && b == 0);
        else if (mode == 4)
            match = (r == 0 && gch == 0 && b == 255);
        if (match)
            p = p & 0x00ffffffu; // A=0
        else
            p = 0xff000000u | (p & 0x00ffffffu);
    }
    static const char *names[] = {"None", "White", "100% Red", "100% Green",
                                  "100% Blue"};
    mark_dirty(std::string("Transparent Color: ") + names[std::clamp(mode, 0, 4)]);
}

bool paste_art_from_clipboard() {
    SkinImage *dst = selected_art_mutable();
    std::string key = selected_art_key();
    if (key.empty()) {
        set_status("Select an image slot before Paste");
        return false;
    }
    if (!OpenClipboard(g_hwnd)) {
        set_status("Clipboard unavailable");
        return false;
    }
    HANDLE h = GetClipboardData(CF_DIB);
    if (!h) {
        CloseClipboard();
        set_status("Clipboard has no DIB bitmap (copy an image first)");
        return false;
    }
    auto *bi = (BITMAPINFOHEADER *)GlobalLock(h);
    if (!bi) {
        CloseClipboard();
        return false;
    }
    int w = bi->biWidth;
    int hgt = bi->biHeight;
    bool bottom_up = hgt > 0;
    if (hgt < 0) hgt = -hgt;
    int bpp = bi->biBitCount;
    if (w <= 0 || hgt <= 0 || (bpp != 24 && bpp != 32) ||
        bi->biCompression != BI_RGB) {
        GlobalUnlock(h);
        CloseClipboard();
        set_status("Paste needs uncompressed 24/32-bit bitmap");
        return false;
    }
    size_t header = sizeof(BITMAPINFOHEADER);
    if (bpp <= 8) header += size_t(1u << bpp) * sizeof(RGBQUAD);
    const uint8_t *bits = (const uint8_t *)bi + header;
    int stride = ((w * bpp + 31) / 32) * 4;

    SkinImage out;
    out.w = w;
    out.h = hgt;
    out.px.assign(size_t(w) * size_t(hgt), 0);
    if (dst) {
        std::memcpy(out.caps, dst->caps, 4);
        std::memcpy(out.positions, dst->positions, 4);
        out.has_text_color = dst->has_text_color;
        out.text_color = dst->text_color;
    } else {
        out.caps[0] = uint8_t(std::max(1, w / 2));
        out.caps[1] = uint8_t(std::max(1, hgt / 2));
        out.caps[2] = uint8_t(std::max(0, w - 1 - out.caps[0]));
        out.caps[3] = uint8_t(std::max(0, hgt - 1 - out.caps[1]));
    }
    clamp_caps(out);

    for (int y = 0; y < hgt; ++y) {
        int src_y = bottom_up ? (hgt - 1 - y) : y;
        const uint8_t *row = bits + size_t(src_y) * size_t(stride);
        for (int x = 0; x < w; ++x) {
            uint8_t b, gch, r, a = 255;
            if (bpp == 32) {
                b = row[x * 4 + 0];
                gch = row[x * 4 + 1];
                r = row[x * 4 + 2];
                a = row[x * 4 + 3];
                if (a == 0) a = 255; // many DIBs leave A=0
            } else {
                b = row[x * 3 + 0];
                gch = row[x * 3 + 1];
                r = row[x * 3 + 2];
            }
            out.px[size_t(y) * size_t(w) + size_t(x)] =
                (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(gch) << 8) |
                uint32_t(b);
        }
    }
    GlobalUnlock(h);
    CloseClipboard();

    g.ap.art_cache[key] = std::move(out);
    sync_selected_art_ref();
    set_status("Pasted " + std::to_string(w) + "x" + std::to_string(hgt) +
               " into " + key);
    mark_dirty();
    return true;
}

std::string selected_icon_key() {
    auto keys = icon_keys_list();
    if (g.asset_sel < 0 || g.asset_sel >= (int)keys.size()) return {};
    return keys[size_t(g.asset_sel)];
}

SkinImage *selected_icon_mutable() {
    std::string key = selected_icon_key();
    if (key.empty()) return nullptr;
    auto it = g.ap.icon_cache.find(key);
    if (it == g.ap.icon_cache.end() || it->second.empty()) return nullptr;
    return &it->second;
}

void sync_selected_icon_ref() {
    std::string key = selected_icon_key();
    SkinImage *img = selected_icon_mutable();
    if (key.empty() || !img) return;
    g.ap.skin.icons[key] = key + ".skimg"; // basename rewritten on Save
}

Color selected_icon_text_color() {
    SkinImage *img = selected_icon_mutable();
    if (!img || !img->has_text_color) return rgb(0, 0, 0);
    return plate_text_color(img);
}

void set_selected_icon_text_color(Color c) {
    SkinImage *img = selected_icon_mutable();
    if (!img) return;
    img->has_text_color = true;
    img->text_color = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.b);
    sync_selected_icon_ref();
    mark_dirty();
}

void apply_transparent_color_icon(int mode) {
    SkinImage *img = selected_icon_mutable();
    if (!img || img->px.empty()) return;
    g.trans_mode = mode;
    for (uint32_t &p : img->px) {
        uint8_t r = uint8_t((p >> 16) & 0xff);
        uint8_t gch = uint8_t((p >> 8) & 0xff);
        uint8_t b = uint8_t(p & 0xff);
        bool match = false;
        if (mode == 0) {
            p = 0xff000000u | (p & 0x00ffffffu);
            continue;
        } else if (mode == 1)
            match = (r == 255 && gch == 255 && b == 255);
        else if (mode == 2)
            match = (r == 255 && gch == 0 && b == 0);
        else if (mode == 3)
            match = (r == 0 && gch == 255 && b == 0);
        else if (mode == 4)
            match = (r == 0 && gch == 0 && b == 255);
        if (match)
            p = p & 0x00ffffffu;
        else
            p = 0xff000000u | (p & 0x00ffffffu);
    }
    mark_dirty("Transparent Color (icon)");
}

bool paste_icon_from_clipboard() {
    std::string key = selected_icon_key();
    if (key.empty()) {
        set_status("Select an icon slot before Paste");
        return false;
    }
    // Reuse DIB decode via art paste into a temp key, then move — keep logic local.
    if (!OpenClipboard(g_hwnd)) {
        set_status("Clipboard unavailable");
        return false;
    }
    HANDLE h = GetClipboardData(CF_DIB);
    if (!h) {
        CloseClipboard();
        set_status("Clipboard has no DIB bitmap (copy an image first)");
        return false;
    }
    auto *bi = (BITMAPINFOHEADER *)GlobalLock(h);
    if (!bi) {
        CloseClipboard();
        return false;
    }
    int w = bi->biWidth;
    int hgt = bi->biHeight;
    bool bottom_up = hgt > 0;
    if (hgt < 0) hgt = -hgt;
    int bpp = bi->biBitCount;
    if (w <= 0 || hgt <= 0 || (bpp != 24 && bpp != 32) ||
        bi->biCompression != BI_RGB) {
        GlobalUnlock(h);
        CloseClipboard();
        set_status("Paste needs uncompressed 24/32-bit bitmap");
        return false;
    }
    size_t header = sizeof(BITMAPINFOHEADER);
    const uint8_t *bits = (const uint8_t *)bi + header;
    int stride = ((w * bpp + 31) / 32) * 4;
    SkinImage out;
    out.w = w;
    out.h = hgt;
    out.px.assign(size_t(w) * size_t(hgt), 0);
    std::memset(out.caps, 0, 4);
    std::memset(out.positions, 0, 4);
    for (int y = 0; y < hgt; ++y) {
        int src_y = bottom_up ? (hgt - 1 - y) : y;
        const uint8_t *row = bits + size_t(src_y) * size_t(stride);
        for (int x = 0; x < w; ++x) {
            uint8_t b, gch, r, a = 255;
            if (bpp == 32) {
                b = row[x * 4 + 0];
                gch = row[x * 4 + 1];
                r = row[x * 4 + 2];
                a = row[x * 4 + 3];
                if (a == 0) a = 255;
            } else {
                b = row[x * 3 + 0];
                gch = row[x * 3 + 1];
                r = row[x * 3 + 2];
            }
            out.px[size_t(y) * size_t(w) + size_t(x)] =
                (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(gch) << 8) |
                uint32_t(b);
        }
    }
    GlobalUnlock(h);
    CloseClipboard();
    g.ap.icon_cache[key] = std::move(out);
    sync_selected_icon_ref();
    mark_dirty("Pasted " + std::to_string(w) + "x" + std::to_string(hgt) +
               " into " + key);
    return true;
}

Color focused_edit_color() {
    if (g.panel == PanelGroups) return g.group_base;
    if (g.panel == PanelImages) return selected_art_text_color();
    if (g.panel == PanelIcons) return selected_icon_text_color();
    return selected_color();
}

void apply_focused_edit_color(Color c) {
    if (g.panel == PanelGroups) {
        apply_color_group(g.group_sel, c);
    } else if (g.panel == PanelImages) {
        set_selected_art_text_color(c);
        mark_dirty();
    } else if (g.panel == PanelIcons) {
        set_selected_icon_text_color(c);
    } else {
        set_selected_color(c);
        mark_dirty();
    }
}

void begin_hex_edit() {
    g.focus = 10;
    g.hex_buf = color_to_hex(focused_edit_color());
}

bool commit_hex_edit() {
    if (g.focus != 10) return false;
    Color c;
    if (!parse_hex_color(g.hex_buf, c)) {
        set_status("Hex must be #RRGGBB");
        g.hex_buf = color_to_hex(focused_edit_color());
        return false;
    }
    apply_focused_edit_color(c);
    g.hex_buf = color_to_hex(c);
    set_status("Hex " + g.hex_buf);
    return true;
}

std::string &info_field_ref(int i) {
    static std::string dummy;
    switch (i) {
    case 0: return g.ap.skin.meta.name;
    case 1: return g.ap.skin.meta.version;
    case 2: return g.ap.skin.meta.creator;
    case 3: return g.ap.skin.meta.description;
    default: return dummy;
    }
}


void paint_nudge(Canvas &cv, const Appearance &ap, Rect minus, Rect plus, int value,
                 bool minus_p, bool plus_p) {
    paint_button(cv, ap, minus, "-", minus_p, false);
    paint_button(cv, ap, plus, "+", plus_p, false);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", value);
    int mid = (minus.right() + plus.x) / 2;
    int tw = cv.text_width(buf);
    cv.text(mid - tw / 2, minus.y + (minus.h - kFontHeight) / 2, buf,
            ap.c("primary.label"));
}

void paint_slider(Canvas &cv, Rect r, const char *label, int value, Color fill) {
    cv.text(r.x, r.y + 2, label, g.ap.c("primary.label"));
    Rect track{r.x + 16, r.y + 4, r.w - 56, r.h - 8};
    cv.fill(track, g.ap.c("scrollbar.track"));
    cv.frame(track, g.ap.c("primary.frame"));
    int tw = std::max(1, track.w * value / 255);
    cv.fill({track.x + 1, track.y + 1, tw - 1, track.h - 2}, fill);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", value);
    cv.text(track.right() + 6, r.y + 2, buf, g.ap.c("primary.label"));
}

int slider_value_at(Rect r, int mx) {
    Rect track{r.x + 16, r.y + 4, r.w - 56, r.h - 8};
    int v = (mx - track.x) * 255 / std::max(1, track.w);
    return std::clamp(v, 0, 255);
}

// Compute all hit rects from the current canvas size (call before hit-testing).
void layout() {
    int W = g.canvas.width(), H = g.canvas.height();
    if (W <= 0 || H <= 0) {
        W = kWinW;
        H = kWinH;
    }
    g.gel = gel_layout(0, 0, W, H, GelStyle::Main, &g.ap, true);
    Rect client = g.gel.client;

    // Toolbar — Find metrics: regular 24px; default Save 26px outer, same top.
    constexpr int kToolBtnW = 72;
    constexpr int kToolGap = 8;
    int by = client.y + 10;
    g.btn_load = {client.x + 12, by, kToolBtnW, kButtonH};
    g.btn_save = default_button_rect(g.btn_load.right() + kToolGap, by, kToolBtnW);
    g.btn_stock = {g.btn_save.right() + kToolGap, by, kToolBtnW, kButtonH};
    g.btn_import_colors = {g.btn_stock.right() + kToolGap, by, 110, kButtonH};
    g.btn_colors_only = {g.btn_import_colors.right() + kToolGap, by, 100, kButtonH};

    int split = client.x + 440;
    int content_top = by + kDefaultButtonH + 10;
    int content_h = client.bottom() - content_top - 8;

    // Panel tabs above the left list.
    static const char *tab_labels[] = {"Colors", "Info", "Images", "Icons", "Groups"};
    (void)tab_labels;
    int tx = client.x + 10;
    for (int i = 0; i < PanelCount; ++i) {
        g.panel_tabs[i] = {tx, content_top, kPanelTabW, kPanelTabH};
        tx += kPanelTabW + 4;
    }

    int list_top = content_top + kPanelTabH + 6;
    int bottom_reserve = 90;
    if (g.panel == PanelImages) bottom_reserve = 210;
    if (g.panel == PanelIcons) bottom_reserve = 120;
    if (g.panel == PanelGroups) bottom_reserve = 100;
    if (g.panel == PanelInfo) bottom_reserve = 0;
    int list_h = content_h - kPanelTabH - 6 - bottom_reserve;
    g.role_list = {client.x + 10, list_top, 420, std::max(80, list_h)};
    int body_y = g.role_list.y + kHeaderH;
    int body_h = g.role_list.h - kHeaderH;
    g.role_sbar = {g.role_list.right() - kScrollbarW, body_y, kScrollbarW, body_h};

    int ey = g.role_list.bottom() + 8;
    g.slider_r = {client.x + 10, ey, 360, 20};
    g.slider_g = {client.x + 10, ey + 24, 360, 20};
    g.slider_b = {client.x + 10, ey + 48, 360, 20};
    g.hex_field = {client.x + 330, ey + 18, 72, kFieldH};
    g.group_swatch = {client.x + 380, ey, 48, 48};

    // Images authoring strip (below list)
    g.img_thumb = {client.x + 10, ey, 72, 56};
    int nx = g.img_thumb.right() + 8;
    int ny = ey;
    constexpr int kNudgeW = 18;
    constexpr int kNudgeH = 18;
    constexpr int kNudgeGap = 52;
    for (int i = 0; i < 4; ++i) {
        int x0 = nx + i * kNudgeGap;
        g.img_cap_minus[i] = {x0, ny, kNudgeW, kNudgeH};
        g.img_cap_plus[i] = {x0 + 28, ny, kNudgeW, kNudgeH};
        g.img_pos_minus[i] = {x0, ny + 22, kNudgeW, kNudgeH};
        g.img_pos_plus[i] = {x0 + 28, ny + 22, kNudgeW, kNudgeH};
    }
    g.btn_paste = {client.x + 10, ey + 64, 72, kButtonH};
    static const int kTransW = 56;
    for (int i = 0; i < 5; ++i)
        g.btn_trans[i] = {client.x + 90 + i * (kTransW + 4), ey + 64, kTransW, kButtonH};
    // Text Color RGB sliders under Paste/Transparent for Images and Icons.
    if (g.panel == PanelImages || g.panel == PanelIcons) {
        int sy = ey + 64 + kButtonH + 8;
        if (g.panel == PanelIcons) {
            // Icons: no Caps/Positions strip — Paste row at ey, sliders below.
            g.btn_paste = {client.x + 10, ey, 72, kButtonH};
            static const int kTransW = 56;
            for (int i = 0; i < 5; ++i)
                g.btn_trans[i] = {client.x + 90 + i * (kTransW + 4), ey, kTransW,
                                  kButtonH};
            sy = ey + kButtonH + 8;
        }
        g.slider_r = {client.x + 10, sy, 360, 20};
        g.slider_g = {client.x + 10, sy + 22, 360, 20};
        g.slider_b = {client.x + 10, sy + 44, 360, 20};
        g.hex_field = {client.x + 330, sy + 16, 72, kFieldH};
    }

    // Information meta field rows
    for (int i = 0; i < 4; ++i)
        g.info_fields[i] = {client.x + 120, list_top + 28 + i * 28, 280, kFieldH};

    g.preview = {split + 10, content_top, client.right() - split - 20, content_h};
    g.preview_lay.bounds = g.preview;
    g.preview_lay.page_rows = std::max(1, (g.preview.h - (kFindDlgH + 200)) / kRowH);
    g.preview_lay.row_count = 8;
    clamp_scroll();
}

void paint() {
    layout();
    Canvas &cv = g.canvas;
    int W = cv.width(), H = cv.height();
    Appearance &ap = g.ap;

    paint_gel(cv, ap, {0, 0, W, H}, "SagradoKit Editor", g.focused, g.pressed_box);

    // Depress only while the cursor is still over the pressed button.
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    bool load_p = g.drag == DragBtnLoad && g.btn_load.contains(pt.x, pt.y);
    bool save_p = g.drag == DragBtnSave && g.btn_save.contains(pt.x, pt.y);
    bool stock_p = g.drag == DragBtnStock && g.btn_stock.contains(pt.x, pt.y);
    bool import_p =
        g.drag == DragBtnImportColors && g.btn_import_colors.contains(pt.x, pt.y);
    bool only_p =
        g.drag == DragBtnColorsOnly && g.btn_colors_only.contains(pt.x, pt.y);
    paint_button(cv, ap, g.btn_load, "Load", load_p, false);
    paint_button(cv, ap, g.btn_save, "Save", save_p, true);
    paint_button(cv, ap, g.btn_stock, "Stock", stock_p, false);
    paint_button(cv, ap, g.btn_import_colors, "Import Colors", import_p, false);
    paint_button(cv, ap, g.btn_colors_only,
                 g.preview_st.colours_only ? "Full Preview" : "Colors Preview",
                 only_p || g.preview_st.colours_only, false);

    cv.text(g.btn_colors_only.right() + 12,
            g.btn_load.y + (g.btn_load.h - cv.line_height()) / 2, g.status.c_str(),
            ap.c("primary.disable_label"));

    // Panel tabs
    static const char *tab_labels[] = {"Colors", "Info", "Images", "Icons", "Groups"};
    for (int i = 0; i < PanelCount; ++i) {
        bool on = g.panel == i;
        Rect t = g.panel_tabs[i];
        if (on)
            cv.fill(t, ap.c("list.hilite_background"));
        else
            paint_button_face(cv, ap, t, false, false);
        cv.frame(t, ap.c("primary.frame"));
        Color ink = on ? ap.c("list.hilite_foreground") : ap.c("primary.label");
        int tw = cv.text_width(tab_labels[i]);
        cv.text(t.x + (t.w - tw) / 2, t.y + (t.h - kFontHeight) / 2, tab_labels[i],
                ink);
    }

    // Left panel body
    cv.fill(g.role_list, ap.c("list.background"));
    cv.frame(g.role_list, ap.c("primary.frame"));

    if (g.panel == PanelColors) {
        paint_column_header(cv, ap,
                            {g.role_list.x, g.role_list.y, g.role_list.w, kHeaderH},
                            "Colour Roles", true);
        const auto &roles = all_color_roles();
        int body_y = g.role_list.y + kHeaderH;
        int page = g.roles_page();
        int max_scroll = g.roles_max_scroll();
        for (int i = 0; i < page; ++i) {
            int idx = g.scroll + i;
            if (idx >= (int)roles.size()) break;
            Rect row{g.role_list.x + 1, body_y + i * kRoleRowH,
                     g.role_list.w - 2 - kScrollbarW, kRoleRowH};
            bool sel = idx == g.selected;
            if (sel) cv.fill(row, ap.c("list.hilite_background"));
            Color col = ap.c(roles[size_t(idx)].path);
            Rect sw{row.x + 4, row.y + 3, kSwatchW, kRoleRowH - 6};
            cv.fill(sw, col);
            cv.frame(sw, ap.c("primary.frame"));
            Color ink = sel ? ap.c("list.hilite_foreground") : ap.c("list.label");
            cv.text(sw.right() + 8, row.y + (kRoleRowH - cv.line_height()) / 2,
                    roles[size_t(idx)].label, ink);
        }
        bool roles_thumb_hot = g.drag == DragThumbRoles;
        ScrollArrowHot roles_arrow =
            g.drag == DragScrollArrowRoles ? g.arrow_hot : ScrollArrowHot::None;
        paint_scrollbar(cv, ap, g.role_sbar, g.scroll, max_scroll, page,
                        roles_thumb_hot, false, false, roles_arrow);

        Color cur = selected_color();
        paint_slider(cv, g.slider_r, "R", cur.r, rgb(200, 40, 40));
        paint_slider(cv, g.slider_g, "G", cur.g, rgb(40, 180, 40));
        paint_slider(cv, g.slider_b, "B", cur.b, rgb(40, 80, 200));
        {
            bool hex_f = g.focus == 10;
            const char *hx =
                hex_f ? g.hex_buf.c_str() : color_to_hex(cur).c_str();
            paint_field(cv, ap, g.hex_field, hx, hex_f, g.caret_on);
        }
    } else if (g.panel == PanelInfo) {
        paint_column_header(cv, ap,
                            {g.role_list.x, g.role_list.y, g.role_list.w, kHeaderH},
                            "Information", true);
        static const char *labs[] = {"Name:", "Version:", "Creator:", "Description:"};
        for (int i = 0; i < 4; ++i) {
            cv.text(g.role_list.x + 12, g.info_fields[i].y + 4, labs[i],
                    ap.c("primary.label"));
            bool foc = g.focus == i;
            paint_field(cv, ap, g.info_fields[i], info_field_ref(i).c_str(), foc,
                        g.caret_on);
        }
        cv.text(g.role_list.x + 12, g.info_fields[3].bottom() + 16,
                "Click a field and type. Tab cycles. Save writes .sap meta.",
                ap.c("primary.disable_label"));
    } else if (g.panel == PanelImages) {
        paint_column_header(cv, ap,
                            {g.role_list.x, g.role_list.y, g.role_list.w, kHeaderH},
                            "Images", true);
        auto keys = art_keys_list();
        int body_y = g.role_list.y + kHeaderH;
        int page = g.roles_page();
        int max_scroll = g.roles_max_scroll();
        for (int i = 0; i < page; ++i) {
            int idx = g.scroll + i;
            if (idx >= (int)keys.size()) break;
            Rect row{g.role_list.x + 1, body_y + i * kRoleRowH,
                     g.role_list.w - 2 - kScrollbarW, kRoleRowH};
            bool sel = idx == g.asset_sel;
            if (sel) cv.fill(row, ap.c("list.hilite_background"));
            const SkinImage *img = ap.art(keys[size_t(idx)].c_str());
            Color ink = sel ? ap.c("list.hilite_foreground") : ap.c("list.label");
            char detail[96];
            if (img) {
                std::snprintf(detail, sizeof(detail), "%s  %dx%d  caps[%d,%d,%d,%d]",
                              keys[size_t(idx)].c_str(), img->w, img->h, img->caps[0],
                              img->caps[1], img->caps[2], img->caps[3]);
            } else {
                std::snprintf(detail, sizeof(detail), "%s  (empty)",
                              keys[size_t(idx)].c_str());
            }
            cv.text(row.x + 6, row.y + (kRoleRowH - cv.line_height()) / 2, detail, ink);
            if (img && img->has_text_color) {
                Rect sw{row.right() - 22, row.y + 3, 14, kRoleRowH - 6};
                cv.fill(sw, plate_text_color(img));
                cv.frame(sw, ap.c("primary.frame"));
            }
        }
        paint_scrollbar(cv, ap, g.role_sbar, g.scroll, max_scroll, page,
                        g.drag == DragThumbRoles, false, false,
                        g.drag == DragScrollArrowRoles ? g.arrow_hot
                                                       : ScrollArrowHot::None);

        // Authoring strip — thumbnail, Caps/Positions nudges, Paste, Transparent,
        // Text Color RGB (sliders below).
        SkinImage *sel = selected_art_mutable();
        cv.fill(g.img_thumb, ap.c("list.background"));
        cv.frame(g.img_thumb, ap.c("primary.frame"));
        if (sel) {
            int dx = g.img_thumb.x + 2 +
                     std::max(0, (g.img_thumb.w - 4 - sel->w) / 2);
            int dy = g.img_thumb.y + 2 +
                     std::max(0, (g.img_thumb.h - 4 - sel->h) / 2);
            cv.place(*sel, dx, dy);
        }
        static const char *axes[] = {"L", "T", "R", "B"};
        cv.text(g.img_cap_minus[0].x - 36, g.img_cap_minus[0].y + 2, "Caps",
                ap.c("primary.label"));
        cv.text(g.img_pos_minus[0].x - 36, g.img_pos_minus[0].y + 2, "Pos",
                ap.c("primary.label"));
        for (int i = 0; i < 4; ++i) {
            cv.text(g.img_cap_minus[i].x + 6, g.img_cap_minus[i].y - 12, axes[i],
                    ap.c("primary.disable_label"));
            int cap_v = sel ? sel->caps[i] : 0;
            int pos_v = sel ? sel->positions[i] : 0;
            bool cm = g.drag == DragImgNudge && g.nudge_which == i && g.arrow_dir < 0;
            bool cp = g.drag == DragImgNudge && g.nudge_which == i && g.arrow_dir > 0;
            bool pm =
                g.drag == DragImgNudge && g.nudge_which == (8 + i) && g.arrow_dir < 0;
            bool pp =
                g.drag == DragImgNudge && g.nudge_which == (8 + i) && g.arrow_dir > 0;
            paint_nudge(cv, ap, g.img_cap_minus[i], g.img_cap_plus[i], cap_v, cm, cp);
            paint_nudge(cv, ap, g.img_pos_minus[i], g.img_pos_plus[i], pos_v, pm, pp);
        }

        bool paste_p = g.drag == DragBtnPaste && g.btn_paste.contains(pt.x, pt.y);
        paint_button(cv, ap, g.btn_paste, "Paste", paste_p, false);
        static const char *trans_labs[] = {"None", "White", "Red", "Green", "Blue"};
        for (int i = 0; i < 5; ++i) {
            bool on = g.trans_mode == i;
            bool pressed =
                g.drag == DragBtnTrans && g.drag_target == i &&
                g.btn_trans[i].contains(pt.x, pt.y);
            paint_button(cv, ap, g.btn_trans[i], trans_labs[i], pressed || on, false);
        }

        Color tc = selected_art_text_color();
        cv.text(g.slider_r.x, g.slider_r.y - 14, "Text Color", ap.c("primary.label"));
        paint_slider(cv, g.slider_r, "R", tc.r, rgb(200, 40, 40));
        paint_slider(cv, g.slider_g, "G", tc.g, rgb(40, 180, 40));
        paint_slider(cv, g.slider_b, "B", tc.b, rgb(40, 80, 200));
        {
            bool hex_f = g.focus == 10;
            const char *hx =
                hex_f ? g.hex_buf.c_str() : color_to_hex(tc).c_str();
            paint_field(cv, ap, g.hex_field, hx, hex_f, g.caret_on);
        }
    } else if (g.panel == PanelIcons) {
        paint_column_header(cv, ap,
                            {g.role_list.x, g.role_list.y, g.role_list.w, kHeaderH},
                            "Icons", true);
        auto keys = icon_keys_list();
        int body_y = g.role_list.y + kHeaderH;
        int page = g.roles_page();
        int max_scroll = g.roles_max_scroll();
        for (int i = 0; i < page; ++i) {
            int idx = g.scroll + i;
            if (idx >= (int)keys.size()) break;
            Rect row{g.role_list.x + 1, body_y + i * kRoleRowH,
                     g.role_list.w - 2 - kScrollbarW, kRoleRowH};
            bool sel = idx == g.asset_sel;
            if (sel) cv.fill(row, ap.c("list.hilite_background"));
            const SkinImage *ic = ap.icon(keys[size_t(idx)].c_str());
            if (ic)
                paint_icon(cv, ap, row.x + 4, row.y + (kRoleRowH - 16) / 2,
                           keys[size_t(idx)].c_str(), 16);
            else {
                Rect ph{row.x + 4, row.y + (kRoleRowH - 16) / 2, 16, 16};
                cv.frame(ph, ap.c("primary.disable_frame"));
            }
            Color ink = sel ? ap.c("list.hilite_foreground") : ap.c("list.label");
            char lab[80];
            std::snprintf(lab, sizeof(lab), "%s%s", keys[size_t(idx)].c_str(),
                          ic ? "" : "  (empty)");
            cv.text(row.x + 26, row.y + (kRoleRowH - cv.line_height()) / 2, lab, ink);
        }
        paint_scrollbar(cv, ap, g.role_sbar, g.scroll, max_scroll, page,
                        g.drag == DragThumbRoles, false, false,
                        g.drag == DragScrollArrowRoles ? g.arrow_hot
                                                       : ScrollArrowHot::None);
        bool paste_p = g.drag == DragBtnPaste && g.btn_paste.contains(pt.x, pt.y);
        paint_button(cv, ap, g.btn_paste, "Paste", paste_p, false);
        static const char *trans_labs[] = {"None", "White", "Red", "Green", "Blue"};
        for (int i = 0; i < 5; ++i) {
            bool on = g.trans_mode == i;
            bool pressed = g.drag == DragBtnTrans && g.drag_target == i &&
                           g.btn_trans[i].contains(pt.x, pt.y);
            paint_button(cv, ap, g.btn_trans[i], trans_labs[i], pressed || on, false);
        }
        Color tc = selected_icon_text_color();
        cv.text(g.slider_r.x, g.slider_r.y - 14, "Text Color", ap.c("primary.label"));
        paint_slider(cv, g.slider_r, "R", tc.r, rgb(200, 40, 40));
        paint_slider(cv, g.slider_g, "G", tc.g, rgb(40, 180, 40));
        paint_slider(cv, g.slider_b, "B", tc.b, rgb(40, 80, 200));
        {
            bool hex_f = g.focus == 10;
            const char *hx =
                hex_f ? g.hex_buf.c_str() : color_to_hex(tc).c_str();
            paint_field(cv, ap, g.hex_field, hx, hex_f, g.caret_on);
        }
    } else if (g.panel == PanelGroups) {
        paint_column_header(cv, ap,
                            {g.role_list.x, g.role_list.y, g.role_list.w, kHeaderH},
                            "♦ Groups", true);
        int body_y = g.role_list.y + kHeaderH;
        int page = g.roles_page();
        for (int i = 0; i < page; ++i) {
            int idx = g.scroll + i;
            if (idx >= kColorGroupN) break;
            Rect row{g.role_list.x + 1, body_y + i * kRoleRowH,
                     g.role_list.w - 2 - kScrollbarW, kRoleRowH};
            bool sel = idx == g.group_sel;
            if (sel) cv.fill(row, ap.c("list.hilite_background"));
            Color ink = sel ? ap.c("list.hilite_foreground") : ap.c("list.label");
            cv.text(row.x + 8, row.y + (kRoleRowH - cv.line_height()) / 2,
                    kColorGroups[idx].name, ink);
        }
        paint_scrollbar(cv, ap, g.role_sbar, g.scroll, g.roles_max_scroll(), page,
                        g.drag == DragThumbRoles, false, false,
                        g.drag == DragScrollArrowRoles ? g.arrow_hot
                                                       : ScrollArrowHot::None);
        cv.fill(g.group_swatch, g.group_base);
        cv.frame(g.group_swatch, ap.c("primary.frame"));
        paint_slider(cv, g.slider_r, "R", g.group_base.r, rgb(200, 40, 40));
        paint_slider(cv, g.slider_g, "G", g.group_base.g, rgb(40, 180, 40));
        paint_slider(cv, g.slider_b, "B", g.group_base.b, rgb(40, 80, 200));
        {
            bool hex_f = g.focus == 10;
            const char *hx =
                hex_f ? g.hex_buf.c_str() : color_to_hex(g.group_base).c_str();
            paint_field(cv, ap, g.hex_field, hx, hex_f, g.caret_on);
        }
    }

    // Live kit preview — clip so tall samples cannot paint through the gel frame.
    paint_primary_background(cv, ap, g.preview);
    cv.frame(g.preview, ap.c("focus.box"));
    g.preview_st.pressed_btn = (g.drag == DragPreviewBtn || g.drag == DragDropdown)
                                   ? g.drag_btn
                                   : 0;
    g.preview_st.thumb_hot = g.drag == DragThumbPreview;
    g.preview_st.h_thumb_hot = g.drag == DragThumbPreviewH;
    g.preview_st.arrow_hot =
        g.drag == DragScrollArrowPreview ? g.arrow_hot : ScrollArrowHot::None;
    g.preview_st.h_arrow_hot =
        g.drag == DragScrollArrowPreviewH ? g.arrow_hot : ScrollArrowHot::None;
    g.preview_st.slider_hot = g.drag == DragSliderKit;
    {
        CanvasClip preview_clip(cv, g.preview);
        g.preview_lay = paint_kit_preview(cv, ap, g.preview, g.caret_on, g.list_sel,
                                          g.preview_scroll, g.preview_st);
    }

    // Grow box last — same order as Sagrado (paint_grip after content).
    paint_gel_grip(cv, ap, g.gel.grip, g.focused);
}

void blit(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g.canvas.width();
    bmi.bmiHeader.biHeight = -g.canvas.height();
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdc, 0, 0, g.canvas.width(), g.canvas.height(), 0, 0, 0,
                      g.canvas.height(), g.canvas.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
}

void redraw() {
    if (!g_hwnd) return;
    paint();
    blit(g_hwnd);
}

int scroll_from_thumb_y(const ScrollLayout &sl, int my, int max_scroll) {
    if (max_scroll <= 0 || sl.track.h <= sl.thumb.h) return 0;
    int travel = sl.track.h - sl.thumb.h;
    int ty = my - g.thumb_grab - sl.track.y;
    ty = std::clamp(ty, 0, travel);
    return travel ? ty * max_scroll / travel : 0;
}

int scroll_from_thumb_x(const ScrollLayout &sl, int mx, int max_scroll) {
    if (max_scroll <= 0 || sl.track.w <= sl.thumb.w) return 0;
    int travel = sl.track.w - sl.thumb.w;
    int tx = mx - g.h_thumb_grab - sl.track.x;
    tx = std::clamp(tx, 0, travel);
    return travel ? tx * max_scroll / travel : 0;
}

void on_arrow_tick() {
    if (g.drag == DragScrollArrowRoles) {
        g.scroll = std::clamp(g.scroll + g.arrow_dir, 0, g.roles_max_scroll());
        redraw();
    } else if (g.drag == DragScrollArrowPreview) {
        g.preview_scroll =
            std::clamp(g.preview_scroll + g.arrow_dir, 0, g.preview_max_scroll());
        redraw();
    } else if (g.drag == DragScrollArrowPreviewH) {
        g.preview_st.h_scroll = std::clamp(g.preview_st.h_scroll + g.arrow_dir, 0, 8);
        redraw();
    }
}

void mouse_down(int mx, int my) {
    layout();
    // Ensure preview hit rects match last paint; paint once if empty.
    if (g.preview_lay.btn_ok.w == 0) {
        paint();
    }

    SetCapture(g_hwnd);

    if (g.gel.close_box.contains(mx, my)) {
        g.drag = DragCloseBox;
        g.pressed_box = 1;
        redraw();
        return;
    }
    if (g.gel.max_box.contains(mx, my)) {
        g.drag = DragMaxBox;
        g.pressed_box = 3;
        redraw();
        return;
    }
    if (g.gel.min_box.contains(mx, my)) {
        g.drag = DragMinBox;
        g.pressed_box = 4;
        redraw();
        return;
    }

    // Toolbar — press now, act on release if still over the button
    if (g.btn_load.contains(mx, my)) {
        g.drag = DragBtnLoad;
        redraw();
        return;
    }
    if (g.btn_save.contains(mx, my)) {
        g.drag = DragBtnSave;
        redraw();
        return;
    }
    if (g.btn_stock.contains(mx, my)) {
        g.drag = DragBtnStock;
        redraw();
        return;
    }
    if (g.btn_import_colors.contains(mx, my)) {
        g.drag = DragBtnImportColors;
        redraw();
        return;
    }
    if (g.btn_colors_only.contains(mx, my)) {
        g.drag = DragBtnColorsOnly;
        redraw();
        return;
    }

    // Panel tabs
    for (int i = 0; i < PanelCount; ++i) {
        if (g.panel_tabs[i].contains(mx, my)) {
            g.panel = i;
            g.scroll = 0;
            g.asset_sel = 0;
            if (i == PanelGroups) seed_group_base_from_sel();
            if (i == PanelInfo) g.focus = 0;
            else g.focus = -1;
            set_status(std::string("Panel: ") +
                       (i == 0   ? "Colors"
                        : i == 1 ? "Information"
                        : i == 2 ? "Images"
                        : i == 3 ? "Icons"
                                 : "Groups"));
            redraw();
            return;
        }
    }

    // Role-list scrollbar
    if (g.role_sbar.contains(mx, my) &&
        (g.panel == PanelColors || g.panel == PanelImages || g.panel == PanelIcons ||
         g.panel == PanelGroups)) {
        ScrollLayout sl =
            scroll_layout(g.ap, g.role_sbar, g.scroll, g.roles_max_scroll(), g.roles_page());
        ScrollArrowHot hot = scroll_arrow_hit(sl, mx, my);
        if (hot != ScrollArrowHot::None) {
            g.drag = DragScrollArrowRoles;
            g.arrow_hot = hot;
            g.arrow_dir = scroll_arrow_dir(hot);
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 200, nullptr); // hold-repeat initial delay
            return;
        }
        if (sl.thumb.contains(mx, my)) {
            g.drag = DragThumbRoles;
            g.thumb_grab = my - sl.thumb.y;
            redraw();
            return;
        }
        // Page track click
        if (sl.track.contains(mx, my)) {
            if (my < sl.thumb.y) g.scroll -= g.roles_page();
            else g.scroll += g.roles_page();
            clamp_scroll();
            redraw();
            return;
        }
    }

    // Role / asset / group rows
    if (g.role_list.contains(mx, my) && my >= g.role_list.y + kHeaderH &&
        mx < g.role_sbar.x &&
        (g.panel == PanelColors || g.panel == PanelImages || g.panel == PanelIcons ||
         g.panel == PanelGroups)) {
        int row = (my - (g.role_list.y + kHeaderH)) / kRoleRowH;
        int idx = g.scroll + row;
        if (g.panel == PanelColors) {
            if (idx >= 0 && idx < (int)all_color_roles().size()) {
                g.selected = idx;
                g.focus = -1;
                redraw();
            }
        } else if (g.panel == PanelImages) {
            if (idx >= 0 && idx < (int)art_keys_list().size()) {
                g.asset_sel = idx;
                g.focus = -1;
                redraw();
            }
        } else if (g.panel == PanelIcons) {
            if (idx >= 0 && idx < (int)icon_keys_list().size()) {
                g.asset_sel = idx;
                g.focus = -1;
                redraw();
            }
        } else if (g.panel == PanelGroups) {
            if (idx >= 0 && idx < kColorGroupN) {
                g.group_sel = idx;
                seed_group_base_from_sel();
                g.focus = -1;
                redraw();
            }
        }
        return;
    }

    // Info fields + hex field focus
    if (g.panel == PanelInfo) {
        for (int i = 0; i < 4; ++i) {
            if (g.info_fields[i].contains(mx, my)) {
                g.focus = i;
                redraw();
                return;
            }
        }
    }
    if ((g.panel == PanelColors || g.panel == PanelGroups || g.panel == PanelImages ||
         g.panel == PanelIcons) &&
        g.hex_field.contains(mx, my)) {
        begin_hex_edit();
        redraw();
        return;
    }

    // RGB sliders (Colors + Groups + Images/Icons Text Color)
    auto apply_slider_channel = [&](char ch, int mxv) {
        int v = slider_value_at(ch == 'r'   ? g.slider_r
                                : ch == 'g' ? g.slider_g
                                            : g.slider_b,
                                mxv);
        if (g.panel == PanelGroups) {
            if (ch == 'r') g.group_base.r = uint8_t(v);
            else if (ch == 'g') g.group_base.g = uint8_t(v);
            else g.group_base.b = uint8_t(v);
            apply_color_group(g.group_sel, g.group_base);
        } else if (g.panel == PanelImages) {
            Color c = selected_art_text_color();
            if (ch == 'r') c.r = uint8_t(v);
            else if (ch == 'g') c.g = uint8_t(v);
            else c.b = uint8_t(v);
            set_selected_art_text_color(c);
        } else if (g.panel == PanelIcons) {
            Color c = selected_icon_text_color();
            if (ch == 'r') c.r = uint8_t(v);
            else if (ch == 'g') c.g = uint8_t(v);
            else c.b = uint8_t(v);
            set_selected_icon_text_color(c);
        } else {
            Color c = selected_color();
            if (ch == 'r') c.r = uint8_t(v);
            else if (ch == 'g') c.g = uint8_t(v);
            else c.b = uint8_t(v);
            set_selected_color(c);
        }
    };
    if ((g.panel == PanelColors || g.panel == PanelGroups || g.panel == PanelImages ||
         g.panel == PanelIcons) &&
        g.slider_r.contains(mx, my)) {
        g.drag = DragSliderR;
        apply_slider_channel('r', mx);
        redraw();
        return;
    }
    if ((g.panel == PanelColors || g.panel == PanelGroups || g.panel == PanelImages ||
         g.panel == PanelIcons) &&
        g.slider_g.contains(mx, my)) {
        g.drag = DragSliderG;
        apply_slider_channel('g', mx);
        redraw();
        return;
    }
    if ((g.panel == PanelColors || g.panel == PanelGroups || g.panel == PanelImages ||
         g.panel == PanelIcons) &&
        g.slider_b.contains(mx, my)) {
        g.drag = DragSliderB;
        apply_slider_channel('b', mx);
        redraw();
        return;
    }

    // Images/Icons authoring — Caps/Positions nudges, Paste, Transparent Color
    if (g.panel == PanelImages) {
        for (int i = 0; i < 4; ++i) {
            if (g.img_cap_minus[i].contains(mx, my)) {
                g.drag = DragImgNudge;
                g.nudge_which = i;
                g.arrow_dir = -1;
                nudge_cap(i, -1);
                redraw();
                return;
            }
            if (g.img_cap_plus[i].contains(mx, my)) {
                g.drag = DragImgNudge;
                g.nudge_which = i;
                g.arrow_dir = 1;
                nudge_cap(i, 1);
                redraw();
                return;
            }
            if (g.img_pos_minus[i].contains(mx, my)) {
                g.drag = DragImgNudge;
                g.nudge_which = 8 + i;
                g.arrow_dir = -1;
                nudge_pos(i, -1);
                redraw();
                return;
            }
            if (g.img_pos_plus[i].contains(mx, my)) {
                g.drag = DragImgNudge;
                g.nudge_which = 8 + i;
                g.arrow_dir = 1;
                nudge_pos(i, 1);
                redraw();
                return;
            }
        }
        if (g.btn_paste.contains(mx, my)) {
            g.drag = DragBtnPaste;
            redraw();
            return;
        }
        for (int i = 0; i < 5; ++i) {
            if (g.btn_trans[i].contains(mx, my)) {
                g.drag = DragBtnTrans;
                g.drag_target = i;
                redraw();
                return;
            }
        }
    }
    if (g.panel == PanelIcons) {
        if (g.btn_paste.contains(mx, my)) {
            g.drag = DragBtnPaste;
            redraw();
            return;
        }
        for (int i = 0; i < 5; ++i) {
            if (g.btn_trans[i].contains(mx, my)) {
                g.drag = DragBtnTrans;
                g.drag_target = i;
                redraw();
                return;
            }
        }
    }

    // Open dropdown menu takes clicks first (stacked above list)
    if (g.preview_st.dropdown_open && g.preview_lay.menu.contains(mx, my)) {
        int row = menu_hit_row(g.preview_lay.menu_lay, mx, my);
        if (row >= 0 && row != 3) { // 3 = Disabled
            g.preview_st.menu_sel = row;
            g.preview_st.menu_hot = row;
            g.preview_st.dropdown_open = false;
            static const char *names[] = {"Standard", "Slate", "Custom...", "Disabled"};
            set_status(std::string("Menu: ") + names[row]);
            redraw();
        }
        return;
    }
    if (g.preview_st.dropdown_open && !g.preview_lay.dropdown.contains(mx, my) &&
        !g.preview_lay.menu.contains(mx, my)) {
        // Click outside closes the menu (and consumes the click)
        g.preview_st.dropdown_open = false;
        g.preview_st.menu_hot = -1;
        redraw();
        return;
    }

    // Preview buttons
    if (g.preview_lay.btn_ok.contains(mx, my)) {
        g.drag = DragPreviewBtn;
        g.drag_target = g.drag_btn = 1;
        set_status("Preview: OK");
        redraw();
        return;
    }
    if (g.preview_lay.btn_cancel.contains(mx, my)) {
        g.drag = DragPreviewBtn;
        g.drag_target = g.drag_btn = 2;
        set_status("Preview: Cancel");
        redraw();
        return;
    }
    if (g.preview_lay.btn_press.contains(mx, my)) {
        g.drag = DragPreviewBtn;
        g.drag_target = g.drag_btn = 3;
        set_status("Preview: Pressed");
        redraw();
        return;
    }

    // Dropdown
    if (g.preview_lay.dropdown.contains(mx, my)) {
        g.drag = DragDropdown;
        g.drag_btn = 4;
        g.drag_target = 4;
        g.preview_st.dropdown_open = !g.preview_st.dropdown_open;
        g.preview_st.menu_hot = g.preview_st.menu_sel;
        set_status(g.preview_st.dropdown_open ? "Menu open" : "Menu closed");
        redraw();
        return;
    }

    // Kit slider
    if (g.preview_lay.slider.contains(mx, my)) {
        g.drag = DragSliderKit;
        g.preview_st.slider_value =
            slider_value_at_x(g.preview_lay.slider_lay, mx);
        set_status("Slider: " + std::to_string(g.preview_st.slider_value));
        redraw();
        return;
    }

    // Preview vertical scrollbar
    if (g.preview_lay.sbar.contains(mx, my)) {
        int max_s = g.preview_max_scroll();
        ScrollLayout sl = scroll_layout(g.ap, g.preview_lay.sbar, g.preview_scroll, max_s,
                                        g.preview_lay.page_rows);
        ScrollArrowHot hot = scroll_arrow_hit(sl, mx, my);
        if (hot != ScrollArrowHot::None) {
            g.drag = DragScrollArrowPreview;
            g.arrow_hot = hot;
            g.arrow_dir = scroll_arrow_dir(hot);
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 200, nullptr);
            return;
        }
        if (sl.thumb.contains(mx, my)) {
            g.drag = DragThumbPreview;
            g.thumb_grab = my - sl.thumb.y;
            redraw();
            return;
        }
        if (sl.track.contains(mx, my)) {
            if (my < sl.thumb.y) g.preview_scroll -= g.preview_lay.page_rows;
            else g.preview_scroll += g.preview_lay.page_rows;
            clamp_scroll();
            redraw();
            return;
        }
    }

    // Preview horizontal scrollbar
    if (g.preview_lay.hsbar.contains(mx, my)) {
        constexpr int kHMax = 8, kHPage = 4;
        ScrollLayout sl = scroll_layout_h(g.ap, g.preview_lay.hsbar, g.preview_st.h_scroll,
                                          kHMax, kHPage);
        ScrollArrowHot hot = scroll_arrow_hit(sl, mx, my);
        if (hot != ScrollArrowHot::None) {
            g.drag = DragScrollArrowPreviewH;
            g.arrow_hot = hot;
            g.arrow_dir = scroll_arrow_dir(hot);
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 200, nullptr);
            return;
        }
        if (sl.thumb.contains(mx, my)) {
            g.drag = DragThumbPreviewH;
            g.h_thumb_grab = mx - sl.thumb.x;
            redraw();
            return;
        }
        if (sl.track.contains(mx, my)) {
            if (mx < sl.thumb.x) g.preview_st.h_scroll -= kHPage;
            else g.preview_st.h_scroll += kHPage;
            g.preview_st.h_scroll = std::clamp(g.preview_st.h_scroll, 0, kHMax);
            redraw();
            return;
        }
    }

    // Preview list rows (exclude V and H bars)
    Rect list = g.preview_lay.list;
    if (list.w > 0 && mx >= list.x && mx < list.right() - kScrollbarW &&
        my >= list.y + kHeaderH && my < list.bottom() - kScrollbarW) {
        int row = (my - (list.y + kHeaderH)) / kRowH;
        int idx = g.preview_scroll + row;
        if (idx >= 0 && idx < g.preview_lay.row_count) {
            g.list_sel = idx;
            set_status(std::string("Preview selected row ") + std::to_string(idx + 1));
            redraw();
        }
    }
}

void mouse_move(int mx, int my) {
    if (g.drag == DragThumbRoles) {
        ScrollLayout sl =
            scroll_layout(g.ap, g.role_sbar, g.scroll, g.roles_max_scroll(), g.roles_page());
        g.scroll = scroll_from_thumb_y(sl, my, g.roles_max_scroll());
        redraw();
    } else if (g.drag == DragThumbPreview) {
        ScrollLayout sl =
            scroll_layout(g.ap, g.preview_lay.sbar, g.preview_scroll, g.preview_max_scroll(),
                          g.preview_lay.page_rows);
        g.preview_scroll = scroll_from_thumb_y(sl, my, g.preview_max_scroll());
        redraw();
    } else if (g.drag == DragThumbPreviewH) {
        ScrollLayout sl = scroll_layout_h(g.ap, g.preview_lay.hsbar, g.preview_st.h_scroll, 8, 4);
        g.preview_st.h_scroll = scroll_from_thumb_x(sl, mx, 8);
        redraw();
    } else if (g.drag == DragSliderR || g.drag == DragSliderG || g.drag == DragSliderB) {
        char ch = g.drag == DragSliderR ? 'r' : (g.drag == DragSliderG ? 'g' : 'b');
        Rect sr = ch == 'r' ? g.slider_r : (ch == 'g' ? g.slider_g : g.slider_b);
        int v = slider_value_at(sr, mx);
        if (g.panel == PanelGroups) {
            if (ch == 'r') g.group_base.r = uint8_t(v);
            else if (ch == 'g') g.group_base.g = uint8_t(v);
            else g.group_base.b = uint8_t(v);
            apply_color_group(g.group_sel, g.group_base);
        } else if (g.panel == PanelImages) {
            Color c = selected_art_text_color();
            if (ch == 'r') c.r = uint8_t(v);
            else if (ch == 'g') c.g = uint8_t(v);
            else c.b = uint8_t(v);
            set_selected_art_text_color(c);
        } else if (g.panel == PanelIcons) {
            Color c = selected_icon_text_color();
            if (ch == 'r') c.r = uint8_t(v);
            else if (ch == 'g') c.g = uint8_t(v);
            else c.b = uint8_t(v);
            set_selected_icon_text_color(c);
        } else {
            Color c = selected_color();
            if (ch == 'r') c.r = uint8_t(v);
            else if (ch == 'g') c.g = uint8_t(v);
            else c.b = uint8_t(v);
            set_selected_color(c);
        }
        redraw();
    } else if (g.drag == DragSliderKit) {
        g.preview_st.slider_value =
            slider_value_at_x(slider_layout(g.ap, g.preview_lay.slider,
                                            g.preview_st.slider_value, 100,
                                            g.preview_st.slider_hot),
                              mx);
        set_status("Slider: " + std::to_string(g.preview_st.slider_value));
        redraw();
    } else if (g.drag == DragCloseBox) {
        g.pressed_box = g.gel.close_box.contains(mx, my) ? 1 : 0;
        redraw();
    } else if (g.drag == DragMaxBox) {
        g.pressed_box = g.gel.max_box.contains(mx, my) ? 3 : 0;
        redraw();
    } else if (g.drag == DragMinBox) {
        g.pressed_box = g.gel.min_box.contains(mx, my) ? 4 : 0;
        redraw();
    } else if (g.drag == DragBtnLoad || g.drag == DragBtnSave ||
               g.drag == DragBtnStock || g.drag == DragBtnImportColors ||
               g.drag == DragBtnColorsOnly || g.drag == DragBtnPaste ||
               g.drag == DragBtnTrans || g.drag == DragImgNudge ||
               g.drag == DragPreviewBtn || g.drag == DragDropdown) {
        if (g.drag == DragPreviewBtn) {
            bool over =
                (g.drag_target == 1 && g.preview_lay.btn_ok.contains(mx, my)) ||
                (g.drag_target == 2 && g.preview_lay.btn_cancel.contains(mx, my)) ||
                (g.drag_target == 3 && g.preview_lay.btn_press.contains(mx, my));
            g.drag_btn = over ? g.drag_target : 0;
        }
        redraw();
    } else if (g.preview_st.dropdown_open && g.preview_lay.menu.w > 0) {
        // Hover-hilite menu rows while open (no button held)
        int row = menu_hit_row(g.preview_lay.menu_lay, mx, my);
        if (row != g.preview_st.menu_hot) {
            g.preview_st.menu_hot = row;
            redraw();
        }
    }
}

void mouse_up(int mx, int my) {
    KillTimer(g_hwnd, 2);
    int was = g.drag;
    int target = g.drag_target;
    g.drag = DragNone;
    g.drag_btn = 0;
    g.drag_target = 0;
    g.pressed_box = 0;
    g.arrow_dir = 0;
    g.arrow_hot = ScrollArrowHot::None;
    ReleaseCapture();

    if (was == DragCloseBox && g.gel.close_box.contains(mx, my)) {
        PostQuitMessage(0);
        return;
    }
    if (was == DragMaxBox && g.gel.max_box.contains(mx, my)) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        GetWindowPlacement(g_hwnd, &wp);
        ShowWindow(g_hwnd, wp.showCmd == SW_SHOWMAXIMIZED ? SW_RESTORE
                                                          : SW_SHOWMAXIMIZED);
        return;
    }
    if (was == DragMinBox && g.gel.min_box.contains(mx, my)) {
        ShowWindow(g_hwnd, SW_MINIMIZE);
        return;
    }
    if (was == DragBtnLoad && g.btn_load.contains(mx, my)) do_load();
    else if (was == DragBtnSave && g.btn_save.contains(mx, my)) do_save();
    else if (was == DragBtnStock && g.btn_stock.contains(mx, my)) do_stock();
    else if (was == DragBtnImportColors && g.btn_import_colors.contains(mx, my))
        do_import_colors();
    else if (was == DragBtnColorsOnly && g.btn_colors_only.contains(mx, my)) {
        g.preview_st.colours_only = !g.preview_st.colours_only;
        set_status(g.preview_st.colours_only ? "Colors Preview (no images/icons)"
                                             : "Full Kit Preview");
    } else if (was == DragBtnPaste && g.btn_paste.contains(mx, my)) {
        if (g.panel == PanelIcons) paste_icon_from_clipboard();
        else paste_art_from_clipboard();
    } else if (was == DragBtnTrans && target >= 0 && target < 5 &&
               g.btn_trans[target].contains(mx, my)) {
        if (g.panel == PanelIcons) apply_transparent_color_icon(target);
        else apply_transparent_color(target);
    } else if (was == DragPreviewBtn) {
        if (target == 1 && g.preview_lay.btn_ok.contains(mx, my))
            set_status("Preview: OK clicked");
        else if (target == 2 && g.preview_lay.btn_cancel.contains(mx, my))
            set_status("Preview: Cancel clicked");
        else if (target == 3 && g.preview_lay.btn_press.contains(mx, my))
            set_status("Preview: Pressed clicked");
    }
    redraw();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.canvas.resize(kWinW, kWinH);
        {
            std::string p = find_default_skin();
            if (!p.empty() && g.ap.load(p)) {
                g.path = p;
                set_status("Loaded " + p);
            } else {
                g.ap.set_skin(stock_skin());
            }
        }
        SetTimer(hwnd, 1, 500, nullptr); // caret blink
        return 0;
    case WM_TIMER:
        if (wp == 1) {
            g.caret_on = !g.caret_on;
            redraw();
        } else if (wp == 2) {
            // Arrow hold-repeat: after first delay, tick faster
            KillTimer(hwnd, 2);
            SetTimer(hwnd, 2, 50, nullptr);
            on_arrow_tick();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint();
        blit(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0) {
            g.canvas.resize(w, h);
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SETFOCUS:
        g.focused = true;
        redraw();
        return 0;
    case WM_KILLFOCUS:
        g.focused = false;
        redraw();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        // Treat double-clicks as presses — otherwise rapid clicks are eaten
        // (Win32 sends DBLCLK instead of the second DOWN).
        mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (wp & MK_LBUTTON) {
            mouse_move(mx, my);
        } else if (g.preview_st.dropdown_open) {
            layout();
            int row = menu_hit_row(g.preview_lay.menu_lay, mx, my);
            if (row != g.preview_st.menu_hot) {
                g.preview_st.menu_hot = row;
                redraw();
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        layout();
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int step = delta > 0 ? -3 : 3;
        if (g.preview.contains(pt.x, pt.y)) {
            g.preview_scroll =
                std::clamp(g.preview_scroll + step, 0, g.preview_max_scroll());
        } else {
            g.scroll = std::clamp(g.scroll + step, 0, g.roles_max_scroll());
        }
        redraw();
        return 0;
    }
    case WM_CHAR: {
        // Info meta typing + hex #RRGGBB
        if (g.focus >= 0 && g.focus <= 3 && g.panel == PanelInfo) {
            std::string &s = info_field_ref(g.focus);
            if (wp == 8) { // Backspace
                if (!s.empty()) s.pop_back();
                mark_dirty();
            } else if (wp >= 32 && wp < 127 && s.size() < 120) {
                s.push_back(char(wp));
                mark_dirty();
            }
            redraw();
            return 0;
        }
        if (g.focus == 10) {
            if (wp == 8) {
                // Keep leading '#'
                if (g.hex_buf.size() > 1) g.hex_buf.pop_back();
            } else if (wp == '\r' || wp == '\n') {
                commit_hex_edit();
            } else if (std::isxdigit(int(wp)) && g.hex_buf.size() < 7) {
                if (g.hex_buf.empty()) g.hex_buf = "#";
                g.hex_buf.push_back(char(wp));
                if (g.hex_buf.size() == 7) commit_hex_edit();
            }
            redraw();
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g.focus >= 0) {
                if (g.focus == 10) g.hex_buf = color_to_hex(focused_edit_color());
                g.focus = -1;
                redraw();
                return 0;
            }
            PostQuitMessage(0);
        }
        if (wp == VK_TAB && g.panel == PanelInfo) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (g.focus < 0 || g.focus > 3) g.focus = 0;
            else g.focus = shift ? (g.focus + 3) % 4 : (g.focus + 1) % 4;
            redraw();
            return 0;
        }
        if (wp == VK_RETURN && g.focus == 10) {
            commit_hex_edit();
            redraw();
            return 0;
        }
        if (wp == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            do_load();
            redraw();
        }
        if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            do_save();
            redraw();
        }
        if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (g.panel == PanelImages) paste_art_from_clipboard();
            else if (g.panel == PanelIcons) paste_icon_from_clipboard();
            redraw();
        }
        if (wp == VK_UP) {
            if (g.panel == PanelImages || g.panel == PanelIcons) {
                g.asset_sel = std::max(0, g.asset_sel - 1);
                if (g.asset_sel < g.scroll) g.scroll = g.asset_sel;
            } else if (g.panel == PanelGroups) {
                g.group_sel = std::max(0, g.group_sel - 1);
                seed_group_base_from_sel();
                if (g.group_sel < g.scroll) g.scroll = g.group_sel;
            } else {
                g.selected = std::max(0, g.selected - 1);
                if (g.selected < g.scroll) g.scroll = g.selected;
            }
            redraw();
        }
        if (wp == VK_DOWN) {
            if (g.panel == PanelImages) {
                int n = (int)art_keys_list().size();
                g.asset_sel = std::min(n - 1, g.asset_sel + 1);
                if (g.asset_sel >= g.scroll + g.roles_page())
                    g.scroll = g.asset_sel - g.roles_page() + 1;
            } else if (g.panel == PanelIcons) {
                int n = (int)icon_keys_list().size();
                g.asset_sel = std::min(n - 1, g.asset_sel + 1);
                if (g.asset_sel >= g.scroll + g.roles_page())
                    g.scroll = g.asset_sel - g.roles_page() + 1;
            } else if (g.panel == PanelGroups) {
                g.group_sel = std::min(kColorGroupN - 1, g.group_sel + 1);
                seed_group_base_from_sel();
                if (g.group_sel >= g.scroll + g.roles_page())
                    g.scroll = g.group_sel - g.roles_page() + 1;
            } else {
                g.selected =
                    std::min((int)all_color_roles().size() - 1, g.selected + 1);
                if (g.selected >= g.scroll + g.roles_page())
                    g.scroll = g.selected - g.roles_page() + 1;
            }
            redraw();
        }
        if (wp == VK_PRIOR) {
            g.scroll = std::max(0, g.scroll - g.roles_page());
            redraw();
        }
        if (wp == VK_NEXT) {
            g.scroll = std::min(g.roles_max_scroll(), g.scroll + g.roles_page());
            redraw();
        }
        return 0;
    case WM_NCHITTEST: {
        // Borderless gel: always hit-test ourselves. DefWindowProc often returns
        // HTBORDER after NCCALCSIZE=0 and would skip this block entirely.
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        int W = g.canvas.width(), H = g.canvas.height();
        if (pt.x < 0 || pt.y < 0 || pt.x >= W || pt.y >= H) return HTNOWHERE;
        layout();
        if (g.gel.close_box.contains(pt.x, pt.y) ||
            g.gel.max_box.contains(pt.x, pt.y) ||
            g.gel.min_box.contains(pt.x, pt.y) ||
            g.gel.hatch_box.contains(pt.x, pt.y))
            return HTCLIENT;
        if (pt.y < kTitleH) return HTCAPTION;
        if (g.gel.grip.contains(pt.x, pt.y)) return HTBOTTOMRIGHT;
        const int edge = 4;
        bool left = pt.x < edge, right = pt.x >= W - edge;
        bool top = pt.y < edge, bottom = pt.y >= H - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_NCCALCSIZE:
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR cmd, int show) {
    // "--font <face.fnt>" swaps the stock face for a real Haxial one, the way
    // KDX lets a user pick a face per surface.
    for (int i = 1; i < __argc; ++i) {
        if (std::strcmp(__argv[i], "--font") || i + 1 >= __argc) continue;
        if (hfnt::load(__argv[++i], g.font)) {
            g.canvas.set_font(&g.font);
            g.status = "Font: " + g.font.name;
        } else {
            g.status = std::string("Could not load font: ") + __argv[i];
        }
    }
    (void)cmd;

    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoKitEditor";
    RegisterClassA(&wc);

    // Exact client size — WM_NCCALCSIZE makes the window borderless, so do
    // not inflate with AdjustWindowRect (that caused hit-test mismatches).
    // Gel owns the title bar: omit WS_CAPTION / WS_SYSMENU / min / max styles
    // so Wine does not stack a host WM title bar on Linux.
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
    g_hwnd = CreateWindowExA(WS_EX_APPWINDOW, wc.lpszClassName, "SagradoKit Editor",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH,
                             nullptr, nullptr, hinst, nullptr);
    ShowWindow(g_hwnd, show);
    {
        RECT wr{};
        GetWindowRect(g_hwnd, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        SetWindowPos(g_hwnd, nullptr, wr.left, wr.top, w + 1, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(g_hwnd, nullptr, wr.left, wr.top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
