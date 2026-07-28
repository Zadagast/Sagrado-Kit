// SagradoKit Editor — AppearanceEdit-style authoring app.
// Entire UI painted into a software framebuffer and blitted with
// SetDIBitsToDevice. Edits the same .skin.toml format apps load.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../engine/appearance.h"

namespace {

constexpr int kWinW = 960;
constexpr int kWinH = 640;
constexpr int kRoleRowH = 20;
constexpr int kSwatchW = 28;

struct App {
    Canvas canvas;
    Appearance ap;
    GelLayout gel{};
    bool focused = true;
    bool caret_on = true;
    int pressed_box = 0;

    // Colour list
    int scroll = 0;
    int selected = 0;
    int list_sel = 1;
    int preview_scroll = 2;

    // RGB edit for selected role
    int edit_channel = 0; // 0=R 1=G 2=B
    std::string path;
    std::string status = "Stock skin — edit colours, Save As to write a .skin.toml";

    Rect role_list{};
    Rect preview{};
    Rect btn_load{}, btn_save{}, btn_stock{};
    Rect slider_r{}, slider_g{}, slider_b{};
    Rect hex_field{};
} g;

HWND g_hwnd = nullptr;
HINSTANCE g_hinst = nullptr;

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
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void do_save() {
    std::string path = g.path;
    if (path.empty() && !dialog_save_path(path)) return;
    if (path.empty()) return;
    // Ensure authored skin has meta name
    if (g.ap.skin.meta.name.empty() || g.ap.skin.meta.name == "Stock")
        g.ap.skin.meta.name = "Untitled";
    if (g.ap.save(path)) {
        g.path = path;
        set_status("Saved " + path);
    } else {
        set_status("Failed to save " + path);
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void do_stock() {
    g.ap.set_skin(stock_skin());
    g.path.clear();
    set_status("Reset to stock");
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void paint_slider(Canvas &cv, Rect r, const char *label, int value, Color fill) {
    cv.text(r.x, r.y + 2, label, g.ap.c("primary.label"));
    Rect track{r.x + 16, r.y + 4, r.w - 16, r.h - 8};
    cv.fill(track, g.ap.c("scrollbar.track"));
    cv.frame(track, g.ap.c("primary.frame"));
    int tw = std::max(1, track.w * value / 255);
    cv.fill({track.x + 1, track.y + 1, tw - 1, track.h - 2}, fill);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", value);
    cv.text(track.right() + 6, r.y + 2, buf, g.ap.c("primary.label"));
}

int slider_value_at(Rect r, int mx) {
    Rect track{r.x + 16, r.y + 4, r.w - 16, r.h - 8};
    int v = (mx - track.x) * 255 / std::max(1, track.w);
    return std::clamp(v, 0, 255);
}

void paint() {
    Canvas &cv = g.canvas;
    int W = cv.width(), H = cv.height();
    Appearance &ap = g.ap;

    // Editor gel frame
    paint_gel(cv, ap, {0, 0, W, H}, "SagradoKit Editor", g.focused, g.pressed_box);
    g.gel = gel_layout(0, 0, W, H);
    Rect client = g.gel.client;

    // Toolbar buttons
    int ty = client.y + 8;
    g.btn_load = {client.x + 10, ty, 80, 24};
    g.btn_save = {client.x + 100, ty, 80, 24};
    g.btn_stock = {client.x + 190, ty, 80, 24};
    paint_button(cv, ap, g.btn_load, "Load", false, false);
    paint_button(cv, ap, g.btn_save, "Save", false, true);
    paint_button(cv, ap, g.btn_stock, "Stock", false, false);

    // Status
    cv.text(client.x + 290, ty + 4, g.status.c_str(), ap.c("primary.disable_label"));

    // Split: colour roles left, preview right
    int split = client.x + 420;
    int content_top = ty + 34;
    int content_h = client.bottom() - content_top - 8;

    // Role list panel
    g.role_list = {client.x + 10, content_top, 400, content_h - 90};
    cv.fill(g.role_list, ap.c("list.background"));
    cv.frame(g.role_list, ap.c("primary.frame"));
    paint_column_header(cv, ap,
                        {g.role_list.x, g.role_list.y, g.role_list.w, kHeaderH},
                        "Colour Roles", true);

    const auto &roles = all_color_roles();
    int body_y = g.role_list.y + kHeaderH;
    int body_h = g.role_list.h - kHeaderH;
    int visible = body_h / kRoleRowH;
    int max_scroll = std::max(0, (int)roles.size() - visible);
    if (g.scroll > max_scroll) g.scroll = max_scroll;

    for (int i = 0; i < visible; ++i) {
        int idx = g.scroll + i;
        if (idx >= (int)roles.size()) break;
        Rect row{g.role_list.x + 1, body_y + i * kRoleRowH, g.role_list.w - 2 - kScrollbarW,
                 kRoleRowH};
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
    Rect sbar{g.role_list.right() - kScrollbarW, body_y, kScrollbarW, body_h};
    paint_scrollbar(cv, ap, sbar, g.scroll, max_scroll, visible, false);

    // RGB editors under the list
    Color cur = selected_color();
    int ey = g.role_list.bottom() + 8;
    g.slider_r = {client.x + 10, ey, 300, 20};
    g.slider_g = {client.x + 10, ey + 24, 300, 20};
    g.slider_b = {client.x + 10, ey + 48, 300, 20};
    paint_slider(cv, g.slider_r, "R", cur.r, rgb(200, 40, 40));
    paint_slider(cv, g.slider_g, "G", cur.g, rgb(40, 180, 40));
    paint_slider(cv, g.slider_b, "B", cur.b, rgb(40, 80, 200));

    g.hex_field = {client.x + 330, ey + 20, 80, 24};
    paint_field(cv, ap, g.hex_field, color_to_hex(cur).c_str(), true, g.caret_on);

    // Live kit preview
    g.preview = {split + 10, content_top, client.right() - split - 20, content_h};
    cv.fill(g.preview, ap.c("workspace.background3"));
    cv.frame(g.preview, ap.c("focus.box"));
    paint_kit_preview(cv, ap, g.preview, g.caret_on, g.list_sel, g.preview_scroll);
}

void blit(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g.canvas.width();
    bmi.bmiHeader.biHeight = -g.canvas.height(); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdc, 0, 0, g.canvas.width(), g.canvas.height(), 0, 0, 0,
                      g.canvas.height(), g.canvas.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
}

void hit_role_list(int mx, int my) {
    if (!g.role_list.contains(mx, my)) return;
    int body_y = g.role_list.y + kHeaderH;
    if (my < body_y) return;
    // scrollbar
    Rect sbar{g.role_list.right() - kScrollbarW, body_y, kScrollbarW,
              g.role_list.h - kHeaderH};
    if (sbar.contains(mx, my)) {
        const auto &roles = all_color_roles();
        int visible = sbar.h / kRoleRowH;
        int max_scroll = std::max(0, (int)roles.size() - visible);
        ScrollLayout sl = scroll_layout(sbar, g.scroll, max_scroll, visible);
        if (sl.up.contains(mx, my)) g.scroll = std::max(0, g.scroll - 1);
        else if (sl.down.contains(mx, my)) g.scroll = std::min(max_scroll, g.scroll + 1);
        else if (sl.track.contains(mx, my)) {
            int rel = my - sl.track.y;
            g.scroll = max_scroll ? rel * max_scroll / sl.track.h : 0;
        }
        return;
    }
    int row = (my - body_y) / kRoleRowH;
    int idx = g.scroll + row;
    if (idx >= 0 && idx < (int)all_color_roles().size()) g.selected = idx;
}

void hit_sliders(int mx, int my) {
    Color c = selected_color();
    if (g.slider_r.contains(mx, my)) {
        c.r = uint8_t(slider_value_at(g.slider_r, mx));
        set_selected_color(c);
    } else if (g.slider_g.contains(mx, my)) {
        c.g = uint8_t(slider_value_at(g.slider_g, mx));
        set_selected_color(c);
    } else if (g.slider_b.contains(mx, my)) {
        c.b = uint8_t(slider_value_at(g.slider_b, mx));
        set_selected_color(c);
    }
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
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;
    case WM_TIMER:
        g.caret_on = !g.caret_on;
        InvalidateRect(hwnd, nullptr, FALSE);
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
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SETFOCUS:
        g.focused = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        g.focused = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        SetCapture(hwnd);
        if (g.gel.close_box.contains(mx, my)) {
            g.pressed_box = 1;
        } else if (g.btn_load.contains(mx, my)) {
            do_load();
            return 0;
        } else if (g.btn_save.contains(mx, my)) {
            do_save();
            return 0;
        } else if (g.btn_stock.contains(mx, my)) {
            do_stock();
            return 0;
        } else {
            hit_role_list(mx, my);
            hit_sliders(mx, my);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!(wp & MK_LBUTTON)) return 0;
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        hit_sliders(mx, my);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g.pressed_box == 1) PostQuitMessage(0);
        g.pressed_box = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const auto &roles = all_color_roles();
        int visible = (g.role_list.h - kHeaderH) / kRoleRowH;
        int max_scroll = std::max(0, (int)roles.size() - visible);
        if (delta > 0) g.scroll = std::max(0, g.scroll - 3);
        else g.scroll = std::min(max_scroll, g.scroll + 3);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        if (wp == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) do_load();
        if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) do_save();
        if (wp == VK_UP) {
            g.selected = std::max(0, g.selected - 1);
            if (g.selected < g.scroll) g.scroll = g.selected;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (wp == VK_DOWN) {
            g.selected = std::min((int)all_color_roles().size() - 1, g.selected + 1);
            int visible = (g.role_list.h - kHeaderH) / kRoleRowH;
            if (g.selected >= g.scroll + visible) g.scroll = g.selected - visible + 1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_NCHITTEST: {
        // Custom gel: allow dragging by title bar
        LRESULT hit = DefWindowProcA(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y >= 0 && pt.y < kTitleH &&
                !g.gel.close_box.contains(pt.x, pt.y) &&
                !g.gel.max_box.contains(pt.x, pt.y) &&
                !g.gel.min_box.contains(pt.x, pt.y))
                return HTCAPTION;
        }
        return hit;
    }
    case WM_NCCALCSIZE:
        // Borderless — we draw our own gel frame
        if (wp) return 0;
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int show) {
    g_hinst = hinst;
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoKitEditor";
    RegisterClassA(&wc);

    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                  WS_SYSMENU | WS_VISIBLE;
    RECT r{0, 0, kWinW, kWinH};
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowA(
        wc.lpszClassName, "SagradoKit Editor", style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hinst, nullptr);
    ShowWindow(g_hwnd, show);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
