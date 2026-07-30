// Apply a loaded Hap Theme onto Appearance (Sagrado-style live Hap import).
// Maps verified Hap colour indices + image slots → named SagradoKit roles/art.
#pragma once
#include <cstdio>
#include <cstring>
#include <string>

#include "hap.h"
#include "skin.h"
#include "skin_image.h"

struct HapArtMap { int slot; const char *key; };
struct HapColorMap { int index; const char *role; };


static const HapArtMap kHapArtMap[] = {
    {17, "primary.background_pattern"},
    {25, "button.normal"},
    {26, "button.hilited"},
    {27, "button.disabled"},
    {37, "default_button.normal"},
    {38, "default_button.hilited"},
    {39, "default_button.disabled"},
    {49, "icon_button.normal"},
    {50, "icon_button.hilited"},
    {51, "icon_button.disabled"},
    {57, "tick.blank.normal"},
    {58, "tick.blank.hilited"},
    {59, "tick.blank.disabled"},
    {61, "tick.ticked.normal"},
    {62, "tick.ticked.hilited"},
    {63, "tick.ticked.disabled"},
    {65, "tick.tristate.normal"},
    {66, "tick.tristate.hilited"},
    {67, "tick.tristate.disabled"},
    {69, "mutex.blank.normal"},
    {70, "mutex.blank.hilited"},
    {71, "mutex.blank.disabled"},
    {73, "mutex.ticked.normal"},
    {74, "mutex.ticked.hilited"},
    {75, "mutex.ticked.disabled"},
    {77, "mutex.tristate.normal"},
    {78, "mutex.tristate.hilited"},
    {79, "mutex.tristate.disabled"},
    {81, "disclosure.plus.small"},
    {85, "disclosure.minus.small"},
    {89, "popup.normal"},
    {90, "popup.hilited"},
    {91, "popup.disabled"},
    {93, "popup.no_title.normal"},
    {94, "popup.no_title.hilited"},
    {95, "popup.no_title.disabled"},
    {97, "popup.symbol.normal"},
    {98, "popup.symbol.hilited"},
    {99, "popup.symbol.disabled"},
    {101, "focus_box.normal"},
    {102, "focus_box.hilited"},
    {103, "focus_box.disabled"},
    {105, "separator.h"},
    {106, "separator.v"},
    {107, "box"},
    {108, "framed_raised"},
    {111, "progress.bar"},
    {112, "progress.fill"},
    {113, "progress.non_fill"},
    {114, "progress.digit.0"},
    {115, "progress.digit.1"},
    {116, "progress.digit.2"},
    {117, "progress.digit.3"},
    {118, "progress.digit.4"},
    {119, "progress.digit.5"},
    {120, "progress.digit.6"},
    {121, "progress.digit.7"},
    {122, "progress.digit.8"},
    {123, "progress.digit.9"},
    {124, "progress.digit.full"},
    {126, "slider.h.bar.normal"},
    {127, "slider.h.bar.hilited"},
    {128, "slider.h.bar.disabled"},
    {130, "slider.h.indicator.normal"},
    {131, "slider.h.indicator.hilited"},
    {132, "slider.h.indicator.disabled"},
    {134, "slider.h.indicator_pointed.normal"},
    {135, "slider.h.indicator_pointed.hilited"},
    {136, "slider.h.indicator_pointed.disabled"},
    {138, "slider.v.bar.normal"},
    {139, "slider.v.bar.hilited"},
    {140, "slider.v.bar.disabled"},
    {142, "slider.v.indicator.normal"},
    {143, "slider.v.indicator.hilited"},
    {144, "slider.v.indicator.disabled"},
    {146, "slider.v.indicator_pointed.normal"},
    {147, "slider.v.indicator_pointed.hilited"},
    {148, "slider.v.indicator_pointed.disabled"},
    {150, "column_header.normal"},
    {151, "column_header.hilited"},
    {152, "column_header.disabled"},
    {162, "scrollbar.h.double_arrows"},
    {163, "scrollbar.h.single_arrows"},
    {164, "scrollbar.h.disabled"},
    {165, "scrollbar.h.too_small"},
    {166, "scrollbar.h.indicator.normal"},
    {167, "scrollbar.h.indicator.hilited"},
    {169, "scrollbar.h.grips.normal"},
    {170, "scrollbar.h.grips.hilited"},
    {172, "scrollbar.h.arrow_hilite.first_left"},
    {173, "scrollbar.h.arrow_hilite.first_right"},
    {174, "scrollbar.h.arrow_hilite.second_left"},
    {175, "scrollbar.h.arrow_hilite.second_right"},
    {176, "scrollbar.h.arrow_hilite.single_left"},
    {177, "scrollbar.h.arrow_hilite.single_right"},
    {181, "scrollbar.v.double_arrows"},
    {182, "scrollbar.v.single_arrows"},
    {183, "scrollbar.v.disabled"},
    {184, "scrollbar.v.too_small"},
    {185, "scrollbar.v.indicator.normal"},
    {186, "scrollbar.v.indicator.hilited"},
    {188, "scrollbar.v.grips.normal"},
    {189, "scrollbar.v.grips.hilited"},
    {191, "scrollbar.v.arrow_hilite.first_up"},
    {192, "scrollbar.v.arrow_hilite.first_down"},
    {193, "scrollbar.v.arrow_hilite.second_up"},
    {194, "scrollbar.v.arrow_hilite.second_down"},
    {195, "scrollbar.v.arrow_hilite.single_up"},
    {196, "scrollbar.v.arrow_hilite.single_down"},
    {200, "menu.background_pattern"},
    {201, "menu.background"},
    {202, "menu.item_pattern.normal"},
    {203, "menu.item_pattern.hilited"},
    {204, "menu.item_pattern.disabled"},
    {205, "menu.item.normal"},
    {206, "menu.item.hilited"},
    {207, "menu.item.disabled"},
    {208, "menu.separator"},
    {209, "menu.item.first_hilited"},
    {210, "menu.item.last_hilited"},
    {220, "window.frame.normal"},
    {221, "window.frame.focus"},
    {223, "window.close.normal"},
    {224, "window.close.focus"},
    {225, "window.close.hilited"},
    {226, "window.close.disabled"},
    {228, "window.minimize.normal"},
    {229, "window.minimize.focus"},
    {230, "window.minimize.hilited"},
    {231, "window.minimize.disabled"},
    {233, "window.maximize.normal"},
    {234, "window.maximize.focus"},
    {235, "window.maximize.hilited"},
    {236, "window.maximize.disabled"},
    {238, "window.menu.normal"},
    {239, "window.menu.focus"},
    {240, "window.menu.hilited"},
    {241, "window.menu.disabled"},
    {243, "window.resize.normal"},
    {244, "window.resize.focus"},
    {248, "popup_frame.normal"},
    {249, "popup_frame.focus"},
    {251, "wonderlight.off"},
    {252, "wonderlight.pause"},
    {253, "wonderlight.ready"},
    {254, "wonderlight.go"},
    {255, "wonderlight.finished"},
    {256, "wonderlight.flash_off"},
    {257, "wonderlight.flash_on1"},
    {258, "wonderlight.flash_on2"},
    {263, "disclosure.plus.medium"},
    {267, "disclosure.minus.medium"},
    {271, "menu_bar.pattern"},
    {272, "menu_bar.background"},
    {273, "menu_bar.title_pattern.normal"},
    {274, "menu_bar.title_pattern.hilited"},
    {275, "menu_bar.title_pattern.disabled"},
    {277, "menu_bar.title.normal"},
    {278, "menu_bar.title.hilited"},
    {279, "menu_bar.title.disabled"},
    {281, "color_chooser.normal"},
    {282, "color_chooser.hilited"},
    {283, "color_chooser.disabled"},
};
static constexpr int kHapArtMapN = int(sizeof(kHapArtMap) / sizeof(kHapArtMap[0]));

static const HapColorMap kHapColorMap[] = {
    {1, "primary.light"},
    {2, "primary.background"},
    {3, "primary.dark"},
    {4, "primary.frame"},
    {5, "primary.label"},
    {6, "primary.disable_frame"},
    {7, "primary.disable_label"},
    {8, "important.label"},
    {9, "focus.box"},
    {10, "text.background"},
    {11, "text.foreground"},
    {12, "text.hilite_background"},
    {13, "text.hilite_foreground"},
    {14, "text.insertion_point"},
    {15, "list.background"},
    {16, "list.label"},
    {17, "list.hilite_background"},
    {18, "list.hilite_foreground"},
    {19, "list.sort_column_background"},
    {20, "list.separator"},
    {29, "button.light2"},
    {30, "button.light1"},
    {31, "button.face"},
    {32, "button.dark1"},
    {33, "button.dark2"},
    {34, "button.frame"},
    {35, "button.label"},
    {36, "button_hilite.light2"},
    {37, "button_hilite.light1"},
    {38, "button_hilite.face"},
    {39, "button_hilite.dark1"},
    {40, "button_hilite.dark2"},
    {41, "button_hilite.frame"},
    {42, "button_hilite.label"},
    {43, "button_disable.light2"},
    {44, "button_disable.light1"},
    {45, "button_disable.face"},
    {46, "button_disable.dark1"},
    {47, "button_disable.dark2"},
    {48, "button_disable.frame"},
    {49, "button_disable.label"},
    {50, "default_button.light"},
    {51, "default_button.face"},
    {52, "default_button.dark"},
    {53, "default_button.frame"},
    {54, "window.light2"},
    {55, "window.light1"},
    {56, "window.face"},
    {57, "window.dark1"},
    {58, "window.dark2"},
    {59, "window.frame"},
    {60, "window.label"},
    {79, "window_focus.light2"},
    {80, "window_focus.light1"},
    {81, "window_focus.face"},
    {82, "window_focus.dark1"},
    {83, "window_focus.dark2"},
    {84, "window_focus.frame"},
    {85, "window_focus.label"},
    {104, "menu.light"},
    {105, "menu.background"},
    {106, "menu.dark"},
    {107, "menu.label"},
    {108, "menu.hilite_light"},
    {109, "menu.hilite_background"},
    {110, "menu.hilite_dark"},
    {111, "menu.hilite_label"},
    {112, "menu.disable_label"},
    {123, "progress.bkgnd_light"},
    {124, "progress.bkgnd"},
    {125, "progress.bkgnd_dark"},
    {126, "progress.frame"},
    {127, "progress.label"},
    {128, "scrollbar.frame"},
    {129, "scrollbar.light"},
    {130, "scrollbar.face"},
    {131, "scrollbar.dark"},
    {132, "scrollbar.label"},
    {133, "scrollbar.hilite_light"},
    {134, "scrollbar.hilite"},
    {135, "scrollbar.hilite_dark"},
    {136, "scrollbar.hilite_label"},
    {137, "scrollbar.indicator_light"},
    {138, "scrollbar.indicator"},
    {139, "scrollbar.indicator_dark"},
    {140, "scrollbar.indicator_hilite_light"},
    {141, "scrollbar.indicator_hilite"},
    {142, "scrollbar.indicator_hilite_dark"},
    {143, "scrollbar.track_light2"},
    {144, "scrollbar.track_light1"},
    {145, "scrollbar.track"},
    {146, "scrollbar.track_dark1"},
    {147, "scrollbar.track_dark2"},
    {153, "scrollbar.disable_light"},
    {154, "scrollbar.disable"},
    {155, "scrollbar.disable_dark"},
    {156, "scrollbar.disable_frame"},
    {157, "scrollbar.disable_label"},
    {174, "column_header.frame"},
    {175, "column_header.light"},
    {176, "column_header.face"},
    {177, "column_header.dark"},
    {178, "column_header.label"},
    {179, "column_header.hilite_light"},
    {180, "column_header.hilite"},
    {181, "column_header.hilite_dark"},
    {182, "column_header.hilite_label"},
    {183, "file_label.0"},
    {184, "file_label.1"},
    {185, "file_label.2"},
    {186, "file_label.3"},
    {187, "file_label.4"},
    {188, "file_label.5"},
    {189, "file_label.6"},
    {190, "file_label.7"},
    {191, "file_label.8"},
    {192, "file_label.9"},
    {193, "file_label.10"},
    {194, "file_label.11"},
    {195, "file_label.12"},
    {196, "file_label.13"},
    {197, "file_label.14"},
    {198, "file_label.15"},
};
static constexpr int kHapColorMapN =
    int(sizeof(kHapColorMap) / sizeof(kHapColorMap[0]));

// Icons. Each icon n occupies record 4n (16 px) and 4n+1 (32 px) of the icon
// section; AppearanceEdit's panel row order does not follow the numbering.
//
// The names below are ground truth, not inference: AppearanceEdit 1.4 saves a
// version-2 .hap whose icon sections are row-indexed tables of named %IKN
// records (file icons carry the literal type string, e.g. "folder/uploads").
// Re-saving a v1 theme and matching the decoded artwork of every v2 record back
// to the v1 record it came from yields the record number for each panel row.
// Cross-checked over Blue Print / Ashen / WinXP, whose authored sets differ, and
// confirmed independently by clearing single icons before saving. Rows whose art
// is unauthored in all 111 corpus themes (Haxial, Help, Download, Upload)
// stay unmapped rather than guessed. See docs/haxial-surface-map.md.
static const HapArtMap kHapIconMap[] = {
    {4, "alert.stop.16"},
    {5, "alert.stop.32"},
    {8, "alert.note.16"},
    {9, "alert.note.32"},
    {12, "alert.caution.16"},
    {13, "alert.caution.32"},
    {16, "alert.question.16"},
    {17, "alert.question.32"},
    {288, "settings.16"},
    {289, "settings.32"},
    {292, "tools.16"},
    {293, "tools.32"},
    {296, "exit.16"},
    {297, "exit.32"},
    {300, "about.16"},
    {301, "about.32"},
    {308, "information.16"},
    {309, "information.32"},
    {312, "address_book.16"},
    {313, "address_book.32"},
    {324, "launch.16"},
    {325, "launch.32"},
    {328, "create_folder.16"},
    {329, "create_folder.32"},
    {336, "connect.16"},
    {337, "connect.32"},
    {340, "disconnect.16"},
    {341, "disconnect.32"},
    {344, "data_transfer.16"},
    {345, "data_transfer.32"},
    {348, "news.16"},
    {349, "news.32"},
    {352, "chat.16"},
    {353, "chat.32"},
    {356, "message.16"},
    {357, "message.32"},
    {360, "users.16"},
    {361, "users.32"},
    {368, "server.16"},
    {369, "server.32"},
    {372, "files.16"},
    {373, "files.32"},
    {376, "document_saved.16"},
    {377, "document_saved.32"},
    {380, "document_unsaved.16"},
    {381, "document_unsaved.32"},
};
static constexpr int kHapIconMapN =
    int(sizeof(kHapIconMap) / sizeof(kHapIconMap[0]));

// File icons: same 4n/4n+1 record pairing, keyed by Haxial's file-type strings
// (the taxonomy is embedded in every Haxial app, e.g. Calculator's FTI table).
// Names come from the v2 records, which store the type string verbatim.
// "file_icon.data" is the generic file; "file.generic.NN" and "folder.NN" stay
// as aliases because the painters already ask for them.
static const HapArtMap kHapFileIconMap[] = {
    {40, "file_icon.executable/program.16"},
    {41, "file_icon.executable/program.32"},
    {44, "file_icon.executable/plugin.16"},
    {45, "file_icon.executable/plugin.32"},
    {48, "file_icon.executable/library.16"},
    {49, "file_icon.executable/library.32"},
    {64, "file_icon.alias/unattached.16"},
    {65, "file_icon.alias/unattached.32"},
    {68, "file_icon.data.16"},
    {69, "file_icon.data.32"},
    {68, "file.generic.16"},
    {69, "file.generic.32"},
    {72, "file_icon.text/.16"},
    {73, "file_icon.text/.32"},
    {76, "file_icon.image/.16"},
    {77, "file_icon.image/.32"},
    {80, "file_icon.audio/.16"},
    {81, "file_icon.audio/.32"},
    {84, "file_icon.video/.16"},
    {85, "file_icon.video/.32"},
    {88, "file_icon.font/.16"},
    {89, "file_icon.font/.32"},
    {92, "file_icon.archive/.16"},
    {93, "file_icon.archive/.32"},
    {96, "file_icon.data/truncated.16"},
    {97, "file_icon.data/truncated.32"},
    {160, "file_icon.folder/.16"},
    {161, "file_icon.folder/.32"},
    {160, "folder.16"},
    {161, "folder.32"},
    {172, "file_icon.folder/uploads.16"},
    {173, "file_icon.folder/uploads.32"},
    {176, "file_icon.folder/dropbox.16"},
    {177, "file_icon.folder/dropbox.32"},
    {192, "file_icon.folder/programs.16"},
    {193, "file_icon.folder/programs.32"},
    {196, "file_icon.folder/programming.16"},
    {197, "file_icon.folder/programming.32"},
    {200, "file_icon.folder/games.16"},
    {201, "file_icon.folder/games.32"},
    {204, "file_icon.folder/internet.16"},
    {205, "file_icon.folder/internet.32"},
    {208, "file_icon.folder/images.16"},
    {209, "file_icon.folder/images.32"},
    {212, "file_icon.folder/audio.16"},
    {213, "file_icon.folder/audio.32"},
    {240, "file_icon.volume/ram.16"},
    {241, "file_icon.volume/ram.32"},
    {244, "file_icon.volume/hd.16"},
    {245, "file_icon.volume/hd.32"},
    {248, "file_icon.volume/net.16"},
    {249, "file_icon.volume/net.32"},
    {252, "file_icon.volume/cd.16"},
    {253, "file_icon.volume/cd.32"},
    {256, "file_icon.volume/dvd.16"},
    {257, "file_icon.volume/dvd.32"},
    {260, "file_icon.volume/cart.16"},
    {261, "file_icon.volume/cart.32"},
};
static constexpr int kHapFileIconMapN =
    int(sizeof(kHapFileIconMap) / sizeof(kHapFileIconMap[0]));

inline SkinImage theme_image_to_skin(const ThemeImage &t) {
    SkinImage s;
    s.w = t.w;
    s.h = t.h;
    s.px = t.px;
    std::memcpy(s.caps, t.caps, 4);
    std::memcpy(s.positions, t.positions, 4);
    return s;
}

inline Color hap_u32_to_color(uint32_t v) {
    return {uint8_t((v >> 16) & 0xff), uint8_t((v >> 8) & 0xff),
            uint8_t(v & 0xff)};
}

// Fill Skin colours + art/icon caches from a Hap Theme (incomplete OK).
template <typename AppearanceT>
inline bool apply_hap_theme(AppearanceT &ap, Theme &theme) {
    Skin skin = stock_skin();
    skin.meta.name = theme.name.empty() ? "Hap Theme" : theme.name;
    skin.meta.creator = "imported from .hap";
    skin.meta.description = "Live Hap import (Sagrado-style)";

    if (theme.has_colors) {
        for (int i = 0; i < kHapColorMapN; ++i) {
            int idx = kHapColorMap[i].index;
            if (idx >= 0 && idx < kColorTableLen)
                skin.colors[kHapColorMap[i].role] = hap_u32_to_color(theme.colors[idx]);
        }
        // Window / window_focus / progress transitions
        for (int i = 0; i < 18; ++i) {
            char key[40];
            std::snprintf(key, sizeof(key), "window.transition.%d", i);
            skin.colors[key] = hap_u32_to_color(theme.colors[61 + i]);
            std::snprintf(key, sizeof(key), "window_focus.transition.%d", i);
            skin.colors[key] = hap_u32_to_color(theme.colors[86 + i]);
        }
        for (int i = 0; i < 10; ++i) {
            char key[40];
            std::snprintf(key, sizeof(key), "progress.transition.%d", i);
            skin.colors[key] = hap_u32_to_color(theme.colors[113 + i]);
        }
    }

    ap.skin = std::move(skin);
    ap.skin_dir.clear();
    ap.art_cache.clear();
    ap.icon_cache.clear();

    for (int i = 0; i < kHapArtMapN; ++i) {
        const ThemeImage *img = theme.image(kHapArtMap[i].slot);
        if (!img || img->w <= 0) continue;
        ap.art_cache[kHapArtMap[i].key] = theme_image_to_skin(*img);
    }
    for (int i = 0; i < kHapIconMapN; ++i) {
        const ThemeImage *img = theme.icon(kHapIconMap[i].slot);
        if (!img || img->w <= 0) continue;
        ap.icon_cache[kHapIconMap[i].key] = theme_image_to_skin(*img);
    }
    for (int i = 0; i < kHapFileIconMapN; ++i) {
        const ThemeImage *img = theme.icon(kHapFileIconMap[i].slot);
        if (!img || img->w <= 0) continue;
        ap.icon_cache[kHapFileIconMap[i].key] = theme_image_to_skin(*img);
    }
    return true;
}

