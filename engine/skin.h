// SagradoKit skin format — named colour roles + reserved art/icon slots.
// Incomplete skins OK. Resolution: art → colour → stock.
#pragma once
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "canvas.h"

inline Color rgb(uint8_t r, uint8_t g, uint8_t b) { return {r, g, b}; }

inline bool parse_hex_color(const std::string &s, Color &out) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[8];
    for (size_t i = 1; i < s.size(); ++i) {
        int h = hex(s[i]);
        if (h < 0) return false;
        v[i - 1] = h;
    }
    out.r = uint8_t((v[0] << 4) | v[1]);
    out.g = uint8_t((v[2] << 4) | v[3]);
    out.b = uint8_t((v[4] << 4) | v[5]);
    return true;
}

inline std::string color_to_hex(Color c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

// Flat role path: "primary.background", "button.label", "window.transition.3"
using ColorMap = std::map<std::string, Color>;
using SlotMap = std::map<std::string, std::string>;
using ArtMap = std::map<std::string, ArtRef>;

struct SkinMeta {
    std::string name = "Untitled";
    std::string creator;
    std::string description;
    std::string version;
};

struct Skin {
    int format_version = 1;
    SkinMeta meta;
    ColorMap colors; // only roles authored in this skin
    ArtMap art;      // slot → file + caps/positions
    SlotMap icons;   // reserved — relative paths
};

// --- Stock defaults (last-resort plates) ---------------------------------

inline void set_role(ColorMap &m, const char *k, Color c) { m[k] = c; }

inline ColorMap stock_colors() {
    ColorMap m;
    // Primary
    set_role(m, "primary.light", rgb(255, 255, 255));
    set_role(m, "primary.background", rgb(51, 51, 51));
    set_role(m, "primary.dark", rgb(17, 17, 17));
    set_role(m, "primary.frame", rgb(0, 0, 0));
    set_role(m, "primary.label", rgb(255, 255, 255));
    set_role(m, "primary.disable_frame", rgb(85, 85, 85));
    set_role(m, "primary.disable_label", rgb(136, 136, 136));
    set_role(m, "important.label", rgb(255, 255, 255));
    set_role(m, "focus.box", rgb(136, 0, 0));
    // Text
    set_role(m, "text.background", rgb(0, 0, 0));
    set_role(m, "text.foreground", rgb(0, 204, 0));
    set_role(m, "text.hilite_background", rgb(136, 0, 0));
    set_role(m, "text.hilite_foreground", rgb(255, 255, 255));
    set_role(m, "text.insertion_point", rgb(204, 0, 0));
    // List
    set_role(m, "list.background", rgb(34, 34, 34));
    set_role(m, "list.label", rgb(255, 255, 255));
    set_role(m, "list.hilite_background", rgb(136, 0, 0));
    set_role(m, "list.hilite_foreground", rgb(255, 255, 255));
    set_role(m, "list.sort_column_background", rgb(42, 42, 42));
    set_role(m, "list.separator", rgb(17, 17, 17));
    // Button
    auto button = [&](const char *g, Color l2, Color l1, Color face, Color d1,
                      Color d2, Color frame, Color label) {
        set_role(m, (std::string(g) + ".light2").c_str(), l2);
        set_role(m, (std::string(g) + ".light1").c_str(), l1);
        set_role(m, (std::string(g) + ".face").c_str(), face);
        set_role(m, (std::string(g) + ".dark1").c_str(), d1);
        set_role(m, (std::string(g) + ".dark2").c_str(), d2);
        set_role(m, (std::string(g) + ".frame").c_str(), frame);
        set_role(m, (std::string(g) + ".label").c_str(), label);
    };
    button("button", rgb(170, 170, 170), rgb(119, 119, 119), rgb(85, 85, 85),
           rgb(34, 34, 34), rgb(0, 0, 0), rgb(0, 0, 0), rgb(255, 255, 255));
    button("button_hilite", rgb(136, 136, 136), rgb(102, 102, 102),
           rgb(68, 68, 68), rgb(34, 34, 34), rgb(17, 17, 17), rgb(0, 0, 0),
           rgb(255, 255, 255));
    button("button_disable", rgb(85, 85, 85), rgb(68, 68, 68), rgb(58, 58, 58),
           rgb(42, 42, 42), rgb(26, 26, 26), rgb(0, 0, 0), rgb(136, 136, 136));
    set_role(m, "default_button.light", rgb(204, 0, 0));
    set_role(m, "default_button.face", rgb(136, 0, 0));
    set_role(m, "default_button.dark", rgb(68, 0, 0));
    set_role(m, "default_button.frame", rgb(0, 0, 0));
    // Window / window_focus — Standard: unfocused is greyscale, focused red.
    // Gradients measured off real Haxial TextEdit (rows 2..19).
    static const uint8_t kGradRed[18] = {50,  61,  72,  82,  93,  104, 114, 125,
                                         136, 139, 146, 153, 160, 167, 174, 181,
                                         188, 195};
    static const uint8_t kGradGrey[18] = {12, 15, 18, 20, 23,  26, 28,  31,
                                          34, 39, 52, 65, 78,  91, 104, 117,
                                          130, 143};
    auto window = [&](const char *g, Color l2, Color l1, Color face, Color d1,
                      Color d2, Color frame, Color label, const uint8_t *grad,
                      bool grey_grad) {
        set_role(m, (std::string(g) + ".light2").c_str(), l2);
        set_role(m, (std::string(g) + ".light1").c_str(), l1);
        set_role(m, (std::string(g) + ".face").c_str(), face);
        set_role(m, (std::string(g) + ".dark1").c_str(), d1);
        set_role(m, (std::string(g) + ".dark2").c_str(), d2);
        set_role(m, (std::string(g) + ".frame").c_str(), frame);
        set_role(m, (std::string(g) + ".label").c_str(), label);
        for (int i = 0; i < 18; ++i) {
            char key[48];
            std::snprintf(key, sizeof(key), "%s.transition.%d", g, i);
            uint8_t v = grad[i];
            set_role(m, key, grey_grad ? rgb(v, v, v) : rgb(v, 0, 0));
        }
    };
    window("window", rgb(85, 85, 85), rgb(85, 85, 85), rgb(34, 34, 34),
           rgb(17, 17, 17), rgb(17, 17, 17), rgb(0, 0, 0), rgb(136, 136, 136),
           kGradGrey, true);
    window("window_focus", rgb(85, 0, 0), rgb(204, 0, 0), rgb(136, 0, 0),
           rgb(68, 0, 0), rgb(34, 0, 0), rgb(0, 0, 0), rgb(255, 255, 255),
           kGradRed, false);
    // Menu
    set_role(m, "menu.light", rgb(102, 102, 102));
    set_role(m, "menu.background", rgb(51, 51, 51));
    set_role(m, "menu.dark", rgb(17, 17, 17));
    set_role(m, "menu.label", rgb(255, 255, 255));
    set_role(m, "menu.hilite_light", rgb(170, 68, 68));
    set_role(m, "menu.hilite_background", rgb(136, 0, 0));
    set_role(m, "menu.hilite_dark", rgb(68, 0, 0));
    set_role(m, "menu.hilite_label", rgb(255, 255, 255));
    set_role(m, "menu.disable_label", rgb(136, 136, 136));
    // Slider (Haxial Slider Bar / Indicator groups)
    set_role(m, "slider.bar", rgb(34, 34, 34));
    set_role(m, "slider.bar_frame", rgb(0, 0, 0));
    set_role(m, "slider.bar_hilite", rgb(136, 0, 0));
    set_role(m, "slider.bar_hilite_frame", rgb(0, 0, 0));
    set_role(m, "slider.indicator_light", rgb(102, 102, 102));
    set_role(m, "slider.indicator", rgb(68, 68, 68));
    set_role(m, "slider.indicator_dark", rgb(17, 17, 17));
    set_role(m, "slider.indicator_frame", rgb(0, 0, 0));
    set_role(m, "slider.indicator_hilite_light", rgb(170, 68, 68));
    set_role(m, "slider.indicator_hilite", rgb(136, 0, 0));
    set_role(m, "slider.indicator_hilite_dark", rgb(68, 0, 0));
    set_role(m, "slider.indicator_hilite_frame", rgb(0, 0, 0));
    set_role(m, "slider.disable_light", rgb(85, 85, 85));
    set_role(m, "slider.disable", rgb(58, 58, 58));
    set_role(m, "slider.disable_dark", rgb(34, 34, 34));
    set_role(m, "slider.disable_frame", rgb(0, 0, 0));
    // Scrollbar
    set_role(m, "scrollbar.frame", rgb(0, 0, 0));
    set_role(m, "scrollbar.light", rgb(102, 102, 102));
    set_role(m, "scrollbar.face", rgb(51, 51, 51));
    set_role(m, "scrollbar.dark", rgb(17, 17, 17));
    set_role(m, "scrollbar.label", rgb(136, 136, 136));
    set_role(m, "scrollbar.hilite_light", rgb(136, 136, 136));
    set_role(m, "scrollbar.hilite", rgb(68, 68, 68));
    set_role(m, "scrollbar.hilite_dark", rgb(34, 34, 34));
    set_role(m, "scrollbar.hilite_label", rgb(255, 255, 255));
    set_role(m, "scrollbar.indicator_light", rgb(102, 102, 102));
    set_role(m, "scrollbar.indicator", rgb(51, 51, 51));
    set_role(m, "scrollbar.indicator_dark", rgb(17, 17, 17));
    set_role(m, "scrollbar.indicator_hilite_light", rgb(170, 68, 68));
    set_role(m, "scrollbar.indicator_hilite", rgb(136, 0, 0));
    set_role(m, "scrollbar.indicator_hilite_dark", rgb(68, 0, 0));
    set_role(m, "scrollbar.track_light2", rgb(68, 68, 68));
    set_role(m, "scrollbar.track_light1", rgb(51, 51, 51));
    set_role(m, "scrollbar.track", rgb(34, 34, 34));
    set_role(m, "scrollbar.track_dark1", rgb(17, 17, 17));
    set_role(m, "scrollbar.track_dark2", rgb(0, 0, 0));
    set_role(m, "scrollbar.disable_light", rgb(85, 85, 85));
    set_role(m, "scrollbar.disable", rgb(58, 58, 58));
    set_role(m, "scrollbar.disable_dark", rgb(34, 34, 34));
    set_role(m, "scrollbar.disable_frame", rgb(0, 0, 0));
    set_role(m, "scrollbar.disable_label", rgb(136, 136, 136));
    // Column header
    set_role(m, "column_header.frame", rgb(0, 0, 0));
    set_role(m, "column_header.light", rgb(102, 102, 102));
    set_role(m, "column_header.face", rgb(68, 68, 68));
    set_role(m, "column_header.dark", rgb(34, 34, 34));
    set_role(m, "column_header.label", rgb(255, 255, 255));
    set_role(m, "column_header.hilite_light", rgb(170, 68, 68));
    set_role(m, "column_header.hilite", rgb(136, 0, 0));
    set_role(m, "column_header.hilite_dark", rgb(68, 0, 0));
    set_role(m, "column_header.hilite_label", rgb(255, 255, 255));
    // File Label 0–15 (list-item label tints; Hap 183–198)
    static const Color kFileLabels[16] = {
        rgb(255, 255, 255), rgb(255, 0, 0),     rgb(255, 127, 0),
        rgb(0, 255, 0),     rgb(0, 255, 255),   rgb(0, 0, 255),
        rgb(136, 0, 255),   rgb(136, 0, 0),     rgb(85, 34, 0),
        rgb(255, 255, 0),   rgb(0, 127, 0),     rgb(0, 127, 127),
        rgb(0, 34, 102),    rgb(255, 0, 255),   rgb(127, 0, 127),
        rgb(102, 102, 102),
    };
    for (int i = 0; i < 16; ++i) {
        char key[24];
        std::snprintf(key, sizeof(key), "file_label.%d", i);
        set_role(m, key, kFileLabels[i]);
    }
    // Progress (Haxial Progress Bar / Fill)
    set_role(m, "progress.bkgnd_light", rgb(68, 68, 68));
    set_role(m, "progress.bkgnd", rgb(34, 34, 34));
    set_role(m, "progress.bkgnd_dark", rgb(17, 17, 17));
    set_role(m, "progress.frame", rgb(0, 0, 0));
    set_role(m, "progress.label", rgb(255, 255, 255));
    for (int i = 0; i < 10; ++i) {
        char key[40];
        std::snprintf(key, sizeof(key), "progress.transition.%d", i);
        uint8_t v = uint8_t(80 + i * 16);
        set_role(m, key, rgb(v, 0, 0));
    }
    // Workspace
    set_role(m, "workspace.background1", rgb(51, 51, 51));
    set_role(m, "workspace.background2", rgb(42, 42, 42));
    set_role(m, "workspace.background3", rgb(34, 34, 34));
    set_role(m, "workspace.background4", rgb(26, 26, 26));
    return m;
}

inline Skin stock_skin() {
    Skin s;
    s.meta.name = "Stock";
    s.meta.creator = "SagradoKit";
    s.meta.description = "Built-in stock appearance";
    s.meta.version = "1.0";
    s.colors = stock_colors();
    return s;
}

// Resolve colour: skin override → stock.
inline Color resolve_color(const Skin &skin, const ColorMap &stock,
                           const char *role) {
    auto it = skin.colors.find(role);
    if (it != skin.colors.end()) return it->second;
    auto st = stock.find(role);
    if (st != stock.end()) return st->second;
    return rgb(255, 0, 255); // missing role marker (should not happen)
}

// Art slot present when a non-empty path is authored.
inline bool has_art(const Skin &skin, const char *slot) {
    auto it = skin.art.find(slot);
    return it != skin.art.end() && !it->second.path.empty();
}

// --- Ordered role list for the editor ------------------------------------

struct ColorRole {
    const char *path;  // "primary.background"
    const char *label; // "Primary Background"
};

inline const std::vector<ColorRole> &all_color_roles() {
    static const std::vector<ColorRole> roles = {
        {"primary.light", "Primary Light"},
        {"primary.background", "Primary Background"},
        {"primary.dark", "Primary Dark"},
        {"primary.frame", "Primary Frame"},
        {"primary.label", "Primary Label"},
        {"primary.disable_frame", "Primary Disable Frame"},
        {"primary.disable_label", "Primary Disable Label"},
        {"important.label", "Important Label"},
        {"focus.box", "Focus Box"},
        {"text.background", "Text Background"},
        {"text.foreground", "Text Foreground"},
        {"text.hilite_background", "Text Hilite Background"},
        {"text.hilite_foreground", "Text Hilite Foreground"},
        {"text.insertion_point", "Text Insertion Point"},
        {"list.background", "List Background"},
        {"list.label", "List Label"},
        {"list.hilite_background", "List Hilite Background"},
        {"list.hilite_foreground", "List Hilite Foreground"},
        {"list.sort_column_background", "List Sort Column Background"},
        {"list.separator", "List Separator"},
        {"button.light2", "Button Light 2"},
        {"button.light1", "Button Light 1"},
        {"button.face", "Button Face"},
        {"button.dark1", "Button Dark 1"},
        {"button.dark2", "Button Dark 2"},
        {"button.frame", "Button Frame"},
        {"button.label", "Button Label"},
        {"button_hilite.light2", "Button Hilite Light 2"},
        {"button_hilite.light1", "Button Hilite Light 1"},
        {"button_hilite.face", "Button Hilite Face"},
        {"button_hilite.dark1", "Button Hilite Dark 1"},
        {"button_hilite.dark2", "Button Hilite Dark 2"},
        {"button_hilite.frame", "Button Hilite Frame"},
        {"button_hilite.label", "Button Hilite Label"},
        {"button_disable.light2", "Button Disable Light 2"},
        {"button_disable.light1", "Button Disable Light 1"},
        {"button_disable.face", "Button Disable Face"},
        {"button_disable.dark1", "Button Disable Dark 1"},
        {"button_disable.dark2", "Button Disable Dark 2"},
        {"button_disable.frame", "Button Disable Frame"},
        {"button_disable.label", "Button Disable Label"},
        {"default_button.light", "Default Button Light"},
        {"default_button.face", "Default Button Face"},
        {"default_button.dark", "Default Button Dark"},
        {"default_button.frame", "Default Button Frame"},
        {"window.light2", "Window Light 2"},
        {"window.light1", "Window Light 1"},
        {"window.face", "Window Face"},
        {"window.dark1", "Window Dark 1"},
        {"window.dark2", "Window Dark 2"},
        {"window.frame", "Window Frame"},
        {"window.label", "Window Label"},
        {"window.transition.0", "Window Transition 1"},
        {"window.transition.1", "Window Transition 2"},
        {"window.transition.2", "Window Transition 3"},
        {"window.transition.3", "Window Transition 4"},
        {"window.transition.4", "Window Transition 5"},
        {"window.transition.5", "Window Transition 6"},
        {"window.transition.6", "Window Transition 7"},
        {"window.transition.7", "Window Transition 8"},
        {"window.transition.8", "Window Transition 9"},
        {"window.transition.9", "Window Transition 10"},
        {"window.transition.10", "Window Transition 11"},
        {"window.transition.11", "Window Transition 12"},
        {"window.transition.12", "Window Transition 13"},
        {"window.transition.13", "Window Transition 14"},
        {"window.transition.14", "Window Transition 15"},
        {"window.transition.15", "Window Transition 16"},
        {"window.transition.16", "Window Transition 17"},
        {"window.transition.17", "Window Transition 18"},
        {"window_focus.light2", "Window Focus Light 2"},
        {"window_focus.light1", "Window Focus Light 1"},
        {"window_focus.face", "Window Focus Face"},
        {"window_focus.dark1", "Window Focus Dark 1"},
        {"window_focus.dark2", "Window Focus Dark 2"},
        {"window_focus.frame", "Window Focus Frame"},
        {"window_focus.label", "Window Focus Label"},
        {"window_focus.transition.0", "Window Focus Transition 1"},
        {"window_focus.transition.1", "Window Focus Transition 2"},
        {"window_focus.transition.2", "Window Focus Transition 3"},
        {"window_focus.transition.3", "Window Focus Transition 4"},
        {"window_focus.transition.4", "Window Focus Transition 5"},
        {"window_focus.transition.5", "Window Focus Transition 6"},
        {"window_focus.transition.6", "Window Focus Transition 7"},
        {"window_focus.transition.7", "Window Focus Transition 8"},
        {"window_focus.transition.8", "Window Focus Transition 9"},
        {"window_focus.transition.9", "Window Focus Transition 10"},
        {"window_focus.transition.10", "Window Focus Transition 11"},
        {"window_focus.transition.11", "Window Focus Transition 12"},
        {"window_focus.transition.12", "Window Focus Transition 13"},
        {"window_focus.transition.13", "Window Focus Transition 14"},
        {"window_focus.transition.14", "Window Focus Transition 15"},
        {"window_focus.transition.15", "Window Focus Transition 16"},
        {"window_focus.transition.16", "Window Focus Transition 17"},
        {"window_focus.transition.17", "Window Focus Transition 18"},
        {"menu.light", "Menu Light"},
        {"menu.background", "Menu Background"},
        {"menu.dark", "Menu Dark"},
        {"menu.label", "Menu Label"},
        {"menu.hilite_light", "Menu Hilite Light"},
        {"menu.hilite_background", "Menu Hilite Background"},
        {"menu.hilite_dark", "Menu Hilite Dark"},
        {"menu.hilite_label", "Menu Hilite Label"},
        {"menu.disable_label", "Menu Disable Label"},
        {"slider.bar", "Slider Bar"},
        {"slider.bar_frame", "Slider Bar Frame"},
        {"slider.bar_hilite", "Slider Bar Hilite"},
        {"slider.bar_hilite_frame", "Slider Bar Hilite Frame"},
        {"slider.indicator_light", "Slider Indicator Light"},
        {"slider.indicator", "Slider Indicator"},
        {"slider.indicator_dark", "Slider Indicator Dark"},
        {"slider.indicator_frame", "Slider Indicator Frame"},
        {"slider.indicator_hilite_light", "Slider Indicator Hilite Light"},
        {"slider.indicator_hilite", "Slider Indicator Hilite"},
        {"slider.indicator_hilite_dark", "Slider Indicator Hilite Dark"},
        {"slider.indicator_hilite_frame", "Slider Indicator Hilite Frame"},
        {"slider.disable_light", "Slider Disable Light"},
        {"slider.disable", "Slider Disable"},
        {"slider.disable_dark", "Slider Disable Dark"},
        {"slider.disable_frame", "Slider Disable Frame"},
        {"scrollbar.frame", "ScrollBar Frame"},
        {"scrollbar.light", "ScrollBar Light"},
        {"scrollbar.face", "ScrollBar Face"},
        {"scrollbar.dark", "ScrollBar Dark"},
        {"scrollbar.label", "ScrollBar Label"},
        {"scrollbar.hilite_light", "ScrollBar Hilite Light"},
        {"scrollbar.hilite", "ScrollBar Hilite"},
        {"scrollbar.hilite_dark", "ScrollBar Hilite Dark"},
        {"scrollbar.hilite_label", "ScrollBar Hilite Label"},
        {"scrollbar.indicator_light", "ScrollBar Indicator Light"},
        {"scrollbar.indicator", "ScrollBar Indicator"},
        {"scrollbar.indicator_dark", "ScrollBar Indicator Dark"},
        {"scrollbar.indicator_hilite_light", "ScrollBar Indicator Hilite Light"},
        {"scrollbar.indicator_hilite", "ScrollBar Indicator Hilite"},
        {"scrollbar.indicator_hilite_dark", "ScrollBar Indicator Hilite Dark"},
        {"scrollbar.track_light2", "ScrollBar Track Light 2"},
        {"scrollbar.track_light1", "ScrollBar Track Light 1"},
        {"scrollbar.track", "ScrollBar Track"},
        {"scrollbar.track_dark1", "ScrollBar Track Dark 1"},
        {"scrollbar.track_dark2", "ScrollBar Track Dark 2"},
        {"scrollbar.disable_light", "ScrollBar Disable Light"},
        {"scrollbar.disable", "ScrollBar Disable"},
        {"scrollbar.disable_dark", "ScrollBar Disable Dark"},
        {"scrollbar.disable_frame", "ScrollBar Disable Frame"},
        {"scrollbar.disable_label", "ScrollBar Disable Label"},
        {"column_header.frame", "Column Header Frame"},
        {"column_header.light", "Column Header Light"},
        {"column_header.face", "Column Header Face"},
        {"column_header.dark", "Column Header Dark"},
        {"column_header.label", "Column Header Label"},
        {"column_header.hilite_light", "Column Header Hilite Light"},
        {"column_header.hilite", "Column Header Hilite"},
        {"column_header.hilite_dark", "Column Header Hilite Dark"},
        {"column_header.hilite_label", "Column Header Hilite Label"},
        {"file_label.0", "File Label 0"},
        {"file_label.1", "File Label 1"},
        {"file_label.2", "File Label 2"},
        {"file_label.3", "File Label 3"},
        {"file_label.4", "File Label 4"},
        {"file_label.5", "File Label 5"},
        {"file_label.6", "File Label 6"},
        {"file_label.7", "File Label 7"},
        {"file_label.8", "File Label 8"},
        {"file_label.9", "File Label 9"},
        {"file_label.10", "File Label 10"},
        {"file_label.11", "File Label 11"},
        {"file_label.12", "File Label 12"},
        {"file_label.13", "File Label 13"},
        {"file_label.14", "File Label 14"},
        {"file_label.15", "File Label 15"},
        {"progress.bkgnd_light", "Progress Background Light"},
        {"progress.bkgnd", "Progress Background"},
        {"progress.bkgnd_dark", "Progress Background Dark"},
        {"progress.frame", "Progress Frame"},
        {"progress.label", "Progress Label"},
        {"progress.transition.0", "Progress Transition 1"},
        {"progress.transition.1", "Progress Transition 2"},
        {"progress.transition.2", "Progress Transition 3"},
        {"progress.transition.3", "Progress Transition 4"},
        {"progress.transition.4", "Progress Transition 5"},
        {"progress.transition.5", "Progress Transition 6"},
        {"progress.transition.6", "Progress Transition 7"},
        {"progress.transition.7", "Progress Transition 8"},
        {"progress.transition.8", "Progress Transition 9"},
        {"progress.transition.9", "Progress Transition 10"},
        {"workspace.background1", "Workspace Background 1"},
        {"workspace.background2", "Workspace Background 2"},
        {"workspace.background3", "Workspace Background 3"},
        {"workspace.background4", "Workspace Background 4"},
    };
    return roles;
}

// --- Minimal TOML subset reader/writer for .sap (Sagrado Appearance) -----

namespace skin_toml {
inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline bool parse_string(const std::string &raw, std::string &out) {
    std::string s = trim(raw);
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() - 1) {
            switch (s[++i]) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(s[i]); break;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return true;
}

inline bool load(const std::string &path, Skin &skin) {
    std::ifstream f(path);
    if (!f) return false;
    skin = Skin{};
    std::string section;
    std::string line;
    bool in_array = false;
    std::string array_key;
    std::string array_section;
    std::vector<std::string> array_vals;

    auto parse_section_slot = [](const std::string &sec, const char *prefix,
                                 std::string &slot_out) -> bool {
        // art."button.normal"  or  art.button.normal
        size_t plen = std::strlen(prefix);
        if (sec.rfind(prefix, 0) != 0) return false;
        if (sec.size() <= plen) return false;
        std::string rest = sec.substr(plen);
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
            rest = rest.substr(1, rest.size() - 2);
        if (rest.empty()) return false;
        slot_out = rest;
        return true;
    };

    auto flush_array = [&]() {
        if (!in_array) return;
        if (array_key.find(".transition") != std::string::npos) {
            std::string base = array_key;
            for (size_t i = 0; i < array_vals.size() && i < 18; ++i) {
                Color c;
                if (parse_hex_color(array_vals[i], c)) {
                    char key[64];
                    std::snprintf(key, sizeof(key), "%s.%zu", base.c_str(), i);
                    skin.colors[key] = c;
                }
            }
        } else {
            std::string slot;
            if (parse_section_slot(array_section, "art.", slot) ||
                parse_section_slot(array_section, "art_meta.", slot)) {
                ArtRef &ref = skin.art[slot];
                uint8_t vals[4] = {0, 0, 0, 0};
                for (size_t i = 0; i < array_vals.size() && i < 4; ++i)
                    vals[i] = uint8_t(std::atoi(array_vals[i].c_str()));
                if (array_key == "caps") {
                    for (int i = 0; i < 4; ++i) ref.caps[i] = vals[i];
                    ref.has_caps = true;
                } else if (array_key == "positions") {
                    for (int i = 0; i < 4; ++i) ref.positions[i] = vals[i];
                    ref.has_positions = true;
                }
            }
        }
        in_array = false;
        array_key.clear();
        array_section.clear();
        array_vals.clear();
    };

    while (std::getline(f, line)) {
        // strip comments
        bool in_str = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') in_str = !in_str;
            if (!in_str && line[i] == '#') {
                line = line.substr(0, i);
                break;
            }
        }
        line = trim(line);
        if (line.empty()) continue;

        if (in_array) {
            if (line[0] == ']') {
                flush_array();
                continue;
            }
            if (line.back() == ',') line.pop_back();
            line = trim(line);
            std::string v;
            if (parse_string(line, v))
                array_vals.push_back(v);
            else
                array_vals.push_back(line); // bare ints for caps/positions
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            flush_array();
            section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        // Unquote dotted keys: "button.normal"
        if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
            key = key.substr(1, key.size() - 2);

        if (val == "[" || (val.size() >= 2 && val.front() == '[' && val.back() == ']')) {
            in_array = true;
            array_section = section;
            if (section == "colors.window" || section.rfind("colors.", 0) == 0) {
                std::string group = section.substr(7);
                array_key = group + "." + key;
            } else {
                array_key = key;
            }
            // Inline array: key = [a, b, c]
            if (val.size() >= 2 && val.front() == '[' && val.back() == ']') {
                std::string inner = trim(val.substr(1, val.size() - 2));
                size_t start = 0;
                while (start < inner.size()) {
                    size_t comma = inner.find(',', start);
                    std::string item = trim(inner.substr(
                        start, comma == std::string::npos ? std::string::npos
                                                          : comma - start));
                    if (!item.empty()) {
                        std::string v;
                        if (parse_string(item, v))
                            array_vals.push_back(v);
                        else
                            array_vals.push_back(item);
                    }
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                flush_array();
            }
            continue;
        }

        if (key == "format") continue;
        if (key == "version" && section.empty()) {
            skin.format_version = std::atoi(val.c_str());
            continue;
        }

        std::string str;
        bool is_str = parse_string(val, str);

        if (section == "meta") {
            if (key == "name" && is_str) skin.meta.name = str;
            else if (key == "creator" && is_str) skin.meta.creator = str;
            else if (key == "description" && is_str) skin.meta.description = str;
            else if (key == "version" && is_str) skin.meta.version = str;
            continue;
        }
        if (section == "art" && is_str) {
            skin.art[key].path = str;
            continue;
        }
        std::string art_slot;
        if (parse_section_slot(section, "art.", art_slot) ||
            parse_section_slot(section, "art_meta.", art_slot)) {
            ArtRef &ref = skin.art[art_slot];
            if ((key == "file" || key == "path") && is_str) ref.path = str;
            else if (key == "text_color" && is_str) {
                Color c;
                if (parse_hex_color(str, c)) {
                    ref.has_text_color = true;
                    ref.text_color = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) |
                                    uint32_t(c.b);
                }
            }
            continue;
        }
        if (section == "icons" && is_str) {
            skin.icons[key] = str;
            continue;
        }
        if (section.rfind("colors.", 0) == 0 && is_str) {
            Color c;
            if (parse_hex_color(str, c)) {
                std::string group = section.substr(7);
                skin.colors[group + "." + key] = c;
            }
            continue;
        }
    }
    flush_array();
    return true;
}

// Metadata imported from a .hap carries quotes and newlines (theme authors put
// multi-line credits in the Info panel), so basic strings must be escaped.
inline std::string toml_escape(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

inline bool save(const std::string &path, const Skin &skin) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# SagradoKit skin\n";
    f << "format = \"sap\"\n";
    f << "version = " << skin.format_version << "\n\n";
    f << "[meta]\n";
    f << "name = \"" << toml_escape(skin.meta.name) << "\"\n";
    f << "creator = \"" << toml_escape(skin.meta.creator) << "\"\n";
    f << "description = \"" << toml_escape(skin.meta.description) << "\"\n";
    f << "version = \"" << toml_escape(skin.meta.version) << "\"\n\n";

    // Group colors by prefix before the last role segment... actually by
    // first segment (and second for dotted groups already flattened).
    // Roles look like "primary.background" or "window.transition.0".
    std::map<std::string, std::map<std::string, Color>> groups;
    std::map<std::string, std::vector<Color>> transitions;
    for (const auto &kv : skin.colors) {
        const std::string &path = kv.first;
        // window.transition.N
        auto tpos = path.find(".transition.");
        if (tpos != std::string::npos) {
            std::string group = path.substr(0, tpos);
            int idx = std::atoi(path.c_str() + tpos + 12);
            auto &vec = transitions[group];
            if ((int)vec.size() <= idx) vec.resize(size_t(idx + 1));
            vec[size_t(idx)] = kv.second;
            continue;
        }
        size_t dot = path.find('.');
        if (dot == std::string::npos) continue;
        std::string group = path.substr(0, dot);
        std::string role = path.substr(dot + 1);
        groups[group][role] = kv.second;
    }

    for (const auto &g : groups) {
        f << "[colors." << g.first << "]\n";
        for (const auto &r : g.second)
            f << r.first << " = \"" << color_to_hex(r.second) << "\"\n";
        auto it = transitions.find(g.first);
        if (it != transitions.end()) {
            f << "transition = [\n";
            for (size_t i = 0; i < it->second.size(); ++i) {
                f << "  \"" << color_to_hex(it->second[i]) << "\"";
                if (i + 1 < it->second.size()) f << ",";
                if (i % 4 == 3) f << "\n";
                else f << " ";
            }
            if (!it->second.empty() && (it->second.size() % 4) != 0) f << "\n";
            f << "]\n";
        }
        f << "\n";
    }
    // Orphan transitions (group with only transition)
    for (const auto &t : transitions) {
        if (groups.count(t.first)) continue;
        f << "[colors." << t.first << "]\n";
        f << "transition = [\n";
        for (size_t i = 0; i < t.second.size(); ++i) {
            f << "  \"" << color_to_hex(t.second[i]) << "\"";
            if (i + 1 < t.second.size()) f << ",";
            f << (i % 4 == 3 ? "\n" : " ");
        }
        f << "]\n\n";
    }

    // Art: one table per slot so caps/positions travel with the path.
    for (const auto &kv : skin.art) {
        const ArtRef &ref = kv.second;
        if (ref.path.empty() && !ref.has_caps && !ref.has_positions) continue;
        f << "[art.\"" << kv.first << "\"]\n";
        if (!ref.path.empty()) f << "file = \"" << ref.path << "\"\n";
        if (ref.has_caps || ref.path.size()) {
            // Always persist caps when known; zeros are valid AppearanceEdit values.
            f << "caps = [" << int(ref.caps[0]) << ", " << int(ref.caps[1])
              << ", " << int(ref.caps[2]) << ", " << int(ref.caps[3]) << "]\n";
        }
        if (ref.has_positions) {
            f << "positions = [" << int(ref.positions[0]) << ", "
              << int(ref.positions[1]) << ", " << int(ref.positions[2]) << ", "
              << int(ref.positions[3]) << "]\n";
        }
        if (ref.has_text_color && ref.text_color != 0) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "#%06x", ref.text_color & 0xffffffu);
            f << "text_color = \"" << hex << "\"\n";
        }
        f << "\n";
    }
    f << "[icons]\n";
    for (const auto &kv : skin.icons)
        f << "\"" << kv.first << "\" = \"" << kv.second << "\"\n";
    return true;
}
} // namespace skin_toml
