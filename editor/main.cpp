// SagradoKit Editor — AppearanceEdit-style authoring app.
// Entire UI painted into a software framebuffer and blitted with
// SetDIBitsToDevice. Edits the same .skin.toml format apps load.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../engine/appearance.h"

namespace {

constexpr int kWinW = 960;
constexpr int kWinH = 640;
constexpr int kRoleRowH = 20;
constexpr int kSwatchW = 28;

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
    DragPreviewBtn,
    DragCloseBox,
    DragMaxBox,
    DragMinBox,
    DragScrollArrowRoles,
    DragScrollArrowPreview,
    DragSliderKit,
    DragDropdown,
};

struct App {
    Canvas canvas;
    Appearance ap;
    GelLayout gel{};
    KitPreviewLayout preview_lay{};
    bool focused = true;
    bool caret_on = true;

    int scroll = 0;
    int selected = 0;
    int list_sel = 1;
    int preview_scroll = 0;
    KitPreviewState preview_st{};

    int drag = DragNone;
    int drag_btn = 0;       // active preview button id (1..3) while over it
    int drag_target = 0;    // original press target (survives move-off)
    int thumb_grab = 0;     // mouse y offset within thumb
    int arrow_dir = 0;      // -1 up / +1 down
    int pressed_box = 0;

    std::string path;
    std::string status = "Stock skin — edit colours, Save to write a .skin.toml";

    Rect role_list{};
    Rect role_sbar{};
    Rect preview{};
    Rect btn_load{}, btn_save{}, btn_stock{};
    Rect slider_r{}, slider_g{}, slider_b{};
    Rect hex_field{};

    int roles_page() const {
        int body_h = role_list.h - kHeaderH;
        return std::max(1, body_h / kRoleRowH);
    }
    int roles_max_scroll() const {
        return std::max(0, (int)all_color_roles().size() - roles_page());
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
    const char *cands[] = {"\\format\\skins\\stock.skin.toml",
                           "\\..\\format\\skins\\stock.skin.toml",
                           "\\..\\..\\format\\skins\\stock.skin.toml",
                           "\\skins\\stock.skin.toml"};
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
}

void set_status(const std::string &s) { g.status = s; }

void clamp_scroll() {
    g.scroll = std::clamp(g.scroll, 0, g.roles_max_scroll());
    g.preview_scroll = std::clamp(g.preview_scroll, 0, g.preview_max_scroll());
}

bool dialog_open_path(std::string &out) {
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "SagradoKit Skin (*.skin.toml)\0*.skin.toml\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "toml";
    if (!GetOpenFileNameA(&ofn)) return false;
    out = file;
    return true;
}

bool dialog_save_path(std::string &out) {
    char file[MAX_PATH] = "untitled.skin.toml";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "SagradoKit Skin (*.skin.toml)\0*.skin.toml\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "toml";
    if (!GetSaveFileNameA(&ofn)) return false;
    out = file;
    return true;
}

void do_load() {
    std::string path;
    if (!dialog_open_path(path)) return;
    if (g.ap.load(path)) {
        g.path = path;
        set_status("Loaded " + path);
    } else {
        set_status("Failed to load " + path);
    }
}

void do_save() {
    std::string path = g.path;
    if (path.empty() && !dialog_save_path(path)) return;
    if (path.empty()) return;
    if (g.ap.skin.meta.name.empty() || g.ap.skin.meta.name == "Stock")
        g.ap.skin.meta.name = "Untitled";
    if (g.ap.save(path)) {
        g.path = path;
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

    int split = client.x + 420;
    int content_top = by + kDefaultButtonH + 10;
    int content_h = client.bottom() - content_top - 8;

    g.role_list = {client.x + 10, content_top, 400, content_h - 90};
    int body_y = g.role_list.y + kHeaderH;
    int body_h = g.role_list.h - kHeaderH;
    g.role_sbar = {g.role_list.right() - kScrollbarW, body_y, kScrollbarW, body_h};

    int ey = g.role_list.bottom() + 8;
    g.slider_r = {client.x + 10, ey, 360, 20};
    g.slider_g = {client.x + 10, ey + 24, 360, 20};
    g.slider_b = {client.x + 10, ey + 48, 360, 20};
    g.hex_field = {client.x + 330, ey + 18, 72, kFieldH};

    g.preview = {split + 10, content_top, client.right() - split - 20, content_h};
    // Approximate preview layout metrics for hit-testing before paint.
    // paint() overwrites preview_lay with exact rects.
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
    paint_button(cv, ap, g.btn_load, "Load", load_p, false);
    paint_button(cv, ap, g.btn_save, "Save", save_p, true);
    paint_button(cv, ap, g.btn_stock, "Stock", stock_p, false);

    cv.text(g.btn_stock.right() + 16,
            g.btn_load.y + (g.btn_load.h - kFontHeight) / 2, g.status.c_str(),
            ap.c("primary.disable_label"));

    // Role list
    cv.fill(g.role_list, ap.c("list.background"));
    cv.frame(g.role_list, ap.c("primary.frame"));
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
        cv.text(sw.right() + 8, row.y + (kRoleRowH - kFontHeight) / 2,
                roles[size_t(idx)].label, ink);
    }
    bool roles_thumb_hot = g.drag == DragThumbRoles;
    paint_scrollbar(cv, ap, g.role_sbar, g.scroll, max_scroll, page, roles_thumb_hot);

    Color cur = selected_color();
    paint_slider(cv, g.slider_r, "R", cur.r, rgb(200, 40, 40));
    paint_slider(cv, g.slider_g, "G", cur.g, rgb(40, 180, 40));
    paint_slider(cv, g.slider_b, "B", cur.b, rgb(40, 80, 200));
    paint_field(cv, ap, g.hex_field, color_to_hex(cur).c_str(), true, g.caret_on);

    // Live kit preview
    cv.fill(g.preview, ap.c("workspace.background3"));
    cv.frame(g.preview, ap.c("focus.box"));
    g.preview_st.pressed_btn = (g.drag == DragPreviewBtn || g.drag == DragDropdown)
                                   ? g.drag_btn
                                   : 0;
    g.preview_st.thumb_hot = g.drag == DragThumbPreview;
    g.preview_st.slider_hot = g.drag == DragSliderKit;
    g.preview_lay = paint_kit_preview(cv, ap, g.preview, g.caret_on, g.list_sel,
                                      g.preview_scroll, g.preview_st);

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

void on_arrow_tick() {
    if (g.drag == DragScrollArrowRoles) {
        g.scroll = std::clamp(g.scroll + g.arrow_dir, 0, g.roles_max_scroll());
        redraw();
    } else if (g.drag == DragScrollArrowPreview) {
        g.preview_scroll =
            std::clamp(g.preview_scroll + g.arrow_dir, 0, g.preview_max_scroll());
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

    // Role-list scrollbar
    if (g.role_sbar.contains(mx, my)) {
        ScrollLayout sl =
            scroll_layout(g.ap, g.role_sbar, g.scroll, g.roles_max_scroll(), g.roles_page());
        if (sl.up.contains(mx, my)) {
            g.drag = DragScrollArrowRoles;
            g.arrow_dir = -1;
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 400, nullptr); // initial delay, then faster
            return;
        }
        if (sl.down.contains(mx, my)) {
            g.drag = DragScrollArrowRoles;
            g.arrow_dir = 1;
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 400, nullptr);
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

    // Role rows
    if (g.role_list.contains(mx, my) && my >= g.role_list.y + kHeaderH &&
        mx < g.role_sbar.x) {
        int row = (my - (g.role_list.y + kHeaderH)) / kRoleRowH;
        int idx = g.scroll + row;
        if (idx >= 0 && idx < (int)all_color_roles().size()) {
            g.selected = idx;
            redraw();
        }
        return;
    }

    // RGB sliders
    if (g.slider_r.contains(mx, my)) {
        g.drag = DragSliderR;
        Color c = selected_color();
        c.r = uint8_t(slider_value_at(g.slider_r, mx));
        set_selected_color(c);
        redraw();
        return;
    }
    if (g.slider_g.contains(mx, my)) {
        g.drag = DragSliderG;
        Color c = selected_color();
        c.g = uint8_t(slider_value_at(g.slider_g, mx));
        set_selected_color(c);
        redraw();
        return;
    }
    if (g.slider_b.contains(mx, my)) {
        g.drag = DragSliderB;
        Color c = selected_color();
        c.b = uint8_t(slider_value_at(g.slider_b, mx));
        set_selected_color(c);
        redraw();
        return;
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

    // Preview scrollbar
    if (g.preview_lay.sbar.contains(mx, my)) {
        int max_s = g.preview_max_scroll();
        ScrollLayout sl = scroll_layout(g.ap, g.preview_lay.sbar, g.preview_scroll, max_s,
                                        g.preview_lay.page_rows);
        if (sl.up.contains(mx, my)) {
            g.drag = DragScrollArrowPreview;
            g.arrow_dir = -1;
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 400, nullptr);
            return;
        }
        if (sl.down.contains(mx, my)) {
            g.drag = DragScrollArrowPreview;
            g.arrow_dir = 1;
            on_arrow_tick();
            SetTimer(g_hwnd, 2, 400, nullptr);
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

    // Preview list rows
    Rect list = g.preview_lay.list;
    if (list.w > 0 && mx >= list.x && mx < list.right() - kScrollbarW &&
        my >= list.y + kHeaderH && my < list.bottom()) {
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
    } else if (g.drag == DragSliderR) {
        Color c = selected_color();
        c.r = uint8_t(slider_value_at(g.slider_r, mx));
        set_selected_color(c);
        redraw();
    } else if (g.drag == DragSliderG) {
        Color c = selected_color();
        c.g = uint8_t(slider_value_at(g.slider_g, mx));
        set_selected_color(c);
        redraw();
    } else if (g.drag == DragSliderB) {
        Color c = selected_color();
        c.b = uint8_t(slider_value_at(g.slider_b, mx));
        set_selected_color(c);
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
               g.drag == DragBtnStock || g.drag == DragPreviewBtn ||
               g.drag == DragDropdown) {
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
    else if (was == DragPreviewBtn) {
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
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        if (wp == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            do_load();
            redraw();
        }
        if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            do_save();
            redraw();
        }
        if (wp == VK_UP) {
            g.selected = std::max(0, g.selected - 1);
            if (g.selected < g.scroll) g.scroll = g.selected;
            redraw();
        }
        if (wp == VK_DOWN) {
            g.selected = std::min((int)all_color_roles().size() - 1, g.selected + 1);
            if (g.selected >= g.scroll + g.roles_page())
                g.scroll = g.selected - g.roles_page() + 1;
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
        LRESULT hit = DefWindowProcA(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            layout();
            if (pt.y >= 0 && pt.y < kTitleH &&
                !g.gel.close_box.contains(pt.x, pt.y) &&
                !g.gel.max_box.contains(pt.x, pt.y) &&
                !g.gel.min_box.contains(pt.x, pt.y))
                return HTCAPTION;
            // TextEdit-style grow box in the corner
            if (g.gel.grip.contains(pt.x, pt.y)) return HTBOTTOMRIGHT;
            // Resize grips on edges
            const int grip = 4;
            int W = g.canvas.width(), H = g.canvas.height();
            bool left = pt.x < grip, right = pt.x >= W - grip;
            bool top = pt.y < grip, bottom = pt.y >= H - grip;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        return hit;
    }
    case WM_NCCALCSIZE:
        // Borderless — entire window is the client; we draw gel ourselves.
        if (wp) return 0;
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int show) {
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoKitEditor";
    RegisterClassA(&wc);

    // Exact client size — WM_NCCALCSIZE makes the window borderless, so do
    // not inflate with AdjustWindowRect (that caused hit-test mismatches).
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                  WS_SYSMENU | WS_CLIPCHILDREN;
    g_hwnd = CreateWindowExA(WS_EX_APPWINDOW, wc.lpszClassName, "SagradoKit Editor",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH,
                             nullptr, nullptr, hinst, nullptr);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
