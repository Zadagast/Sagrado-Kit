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
    SlotMap art;     // reserved — relative paths
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
    button("button", rgb(102, 102, 102), rgb(85, 85, 85), rgb(68, 68, 68),
           rgb(34, 34, 34), rgb(17, 17, 17), rgb(0, 0, 0), rgb(255, 255, 255));
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
    set_role(m, "scrollbar.track_light2", rgb(68, 68, 68));
    set_role(m, "scrollbar.track_light1", rgb(51, 51, 51));
    set_role(m, "scrollbar.track", rgb(34, 34, 34));
    set_role(m, "scrollbar.track_dark1", rgb(17, 17, 17));
    set_role(m, "scrollbar.track_dark2", rgb(0, 0, 0));
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

// Art slot reserved for later slices; colour path used when absent.
inline bool has_art(const Skin &skin, const char *slot) {
    auto it = skin.art.find(slot);
    return it != skin.art.end() && !it->second.empty();
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
        {"window_focus.light2", "Window Focus Light 2"},
        {"window_focus.light1", "Window Focus Light 1"},
        {"window_focus.face", "Window Focus Face"},
        {"window_focus.dark1", "Window Focus Dark 1"},
        {"window_focus.dark2", "Window Focus Dark 2"},
        {"window_focus.frame", "Window Focus Frame"},
        {"window_focus.label", "Window Focus Label"},
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
        {"scrollbar.frame", "ScrollBar Frame"},
        {"scrollbar.light", "ScrollBar Light"},
        {"scrollbar.face", "ScrollBar Face"},
        {"scrollbar.dark", "ScrollBar Dark"},
        {"scrollbar.label", "ScrollBar Label"},
        {"scrollbar.indicator_light", "ScrollBar Indicator Light"},
        {"scrollbar.indicator", "ScrollBar Indicator"},
        {"scrollbar.indicator_dark", "ScrollBar Indicator Dark"},
        {"scrollbar.track_light2", "ScrollBar Track Light 2"},
        {"scrollbar.track_light1", "ScrollBar Track Light 1"},
        {"scrollbar.track", "ScrollBar Track"},
        {"scrollbar.track_dark1", "ScrollBar Track Dark 1"},
        {"scrollbar.track_dark2", "ScrollBar Track Dark 2"},
        {"column_header.frame", "Column Header Frame"},
        {"column_header.light", "Column Header Light"},
        {"column_header.face", "Column Header Face"},
        {"column_header.dark", "Column Header Dark"},
        {"column_header.label", "Column Header Label"},
        {"column_header.hilite_light", "Column Header Hilite Light"},
        {"column_header.hilite", "Column Header Hilite"},
        {"column_header.hilite_dark", "Column Header Hilite Dark"},
        {"column_header.hilite_label", "Column Header Hilite Label"},
        {"workspace.background1", "Workspace Background 1"},
        {"workspace.background2", "Workspace Background 2"},
        {"workspace.background3", "Workspace Background 3"},
        {"workspace.background4", "Workspace Background 4"},
    };
    return roles;
}

// --- Minimal TOML subset reader/writer for .skin.toml --------------------

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
            ++i;
            out.push_back(s[i]);
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
    std::vector<std::string> array_vals;

    auto flush_array = [&]() {
        if (!in_array) return;
        if (array_key.find(".transition") != std::string::npos) {
            // window.transition → window.transition.0..
            std::string base = array_key;
            for (size_t i = 0; i < array_vals.size() && i < 18; ++i) {
                Color c;
                if (parse_hex_color(array_vals[i], c)) {
                    char key[64];
                    std::snprintf(key, sizeof(key), "%s.%zu", base.c_str(), i);
                    skin.colors[key] = c;
                }
            }
        }
        in_array = false;
        array_key.clear();
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
            std::string v;
            if (parse_string(line, v)) array_vals.push_back(v);
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

        if (val == "[") {
            in_array = true;
            if (section == "colors.window" || section.rfind("colors.", 0) == 0) {
                // colors.window + transition → window.transition
                std::string group = section.substr(7); // after "colors."
                array_key = group + "." + key;
            } else {
                array_key = section.empty() ? key : section + "." + key;
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
            skin.art[key] = str;
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

inline bool save(const std::string &path, const Skin &skin) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# SagradoKit skin\n";
    f << "format = \"sagrado-skin\"\n";
    f << "version = " << skin.format_version << "\n\n";
    f << "[meta]\n";
    f << "name = \"" << skin.meta.name << "\"\n";
    f << "creator = \"" << skin.meta.creator << "\"\n";
    f << "description = \"" << skin.meta.description << "\"\n";
    f << "version = \"" << skin.meta.version << "\"\n\n";

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

    f << "[art]\n";
    for (const auto &kv : skin.art)
        f << "\"" << kv.first << "\" = \"" << kv.second << "\"\n";
    f << "\n[icons]\n";
    for (const auto &kv : skin.icons)
        f << "\"" << kv.first << "\" = \"" << kv.second << "\"\n";
    return true;
}
} // namespace skin_toml
