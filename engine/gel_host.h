// Gel Host — shared Win32 foundation for floating Ooze Gel windows.
// Apps create tools (Find, Emoji, …) through this host so move / resize /
// close / min-size work without copying WM_NCHITTEST / DragSize per sheet.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "appearance.h"

namespace sagrado {

enum class GelHostKind {
    Dialog,   // close + hatch + min; move; no grip
    Floating, // close + hatch + min + grip; move + resize
};

struct GelHost;

using GelHostPaintFn = void (*)(GelHost &host, Canvas &cv, const Appearance &ap,
                                Rect client, void *ud);
// Return true if the message was consumed. Coords in window space (gel origin).
using GelHostInputFn = bool (*)(GelHost &host, UINT msg, WPARAM wp, LPARAM lp,
                                void *ud);
// Called when the user clicks gel close (or host hide request).
using GelHostCloseFn = void (*)(GelHost &host, void *ud);

struct GelHostDesc {
    const char *title = "Sagrado";
    const char *class_name = "SagradoGelHost";
    GelHostKind kind = GelHostKind::Floating;
    int w = 400;
    int h = 300;
    int min_w = 280;
    int min_h = 200;
    HWND owner = nullptr;
    Appearance *ap = nullptr;
    const Font *font = nullptr;
    // Close automatically when the window loses activation (picker behavior).
    bool close_on_deactivate = false;
};

struct GelHost {
    HWND hwnd = nullptr;
    Canvas canvas;
    GelLayout gel{};
    Appearance *ap = nullptr;
    const Font *font = nullptr;
    GelHostKind kind = GelHostKind::Floating;
    std::string title;
    std::string class_name;
    int min_w = 280;
    int min_h = 200;
    bool focused = true;
    bool visible = false;
    bool close_on_deactivate = false;
    int pressed_box = 0; // 1=close

    GelHostPaintFn paint_fn = nullptr;
    GelHostInputFn input_fn = nullptr;
    GelHostCloseFn close_fn = nullptr;
    void *user = nullptr;

    // Internal size-drag state (Floating).
    bool sizing = false;
    int size_edge = 0; // bit flags: 1=L 2=R 4=T 8=B
    int size_anchor_x = 0, size_anchor_y = 0;
    int size_orig_x = 0, size_orig_y = 0, size_orig_w = 0, size_orig_h = 0;
};

inline GelStyle gel_host_style(GelHostKind kind) {
    switch (kind) {
    case GelHostKind::Dialog:
        return GelStyle::Dialog;
    case GelHostKind::Floating:
        return GelStyle::Floating;
    }
    return GelStyle::Dialog;
}

inline constexpr int kGelHostSizeLeft = 1;
inline constexpr int kGelHostSizeRight = 2;
inline constexpr int kGelHostSizeTop = 4;
inline constexpr int kGelHostSizeBottom = 8;

inline void gel_host_blit(HWND hwnd, Canvas &cv) {
    if (!hwnd || cv.width() <= 0 || cv.height() <= 0) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cv.width();
    bi.bmiHeader.biHeight = -cv.height();
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC hdc = GetDC(hwnd);
    SetDIBitsToDevice(hdc, 0, 0, cv.width(), cv.height(), 0, 0, 0, cv.height(),
                      cv.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
}

inline void gel_host_paint(GelHost &h) {
    if (!h.hwnd || !h.ap) return;
    int W = h.canvas.width(), H = h.canvas.height();
    if (W <= 0 || H <= 0) return;
    if (h.font) h.canvas.set_font(h.font);
    else h.canvas.set_font(nullptr);
    GelStyle style = gel_host_style(h.kind);
    h.gel = gel_layout(0, 0, W, H, style, h.ap, h.focused);
    paint_gel(h.canvas, *h.ap, {0, 0, W, H}, h.title.c_str(), h.focused,
              h.pressed_box, style);
    if (h.kind == GelHostKind::Floating && h.gel.grip.w > 0)
        paint_gel_grip(h.canvas, *h.ap, h.gel.grip, h.focused);
    if (h.paint_fn)
        h.paint_fn(h, h.canvas, *h.ap, h.gel.client, h.user);
}

inline void gel_host_invalidate(GelHost &h) {
    if (!h.hwnd || !h.visible) return;
    gel_host_paint(h);
    gel_host_blit(h.hwnd, h.canvas);
}

inline void gel_host_set_title(GelHost &h, const char *title) {
    h.title = title ? title : "";
    if (h.hwnd) SetWindowTextA(h.hwnd, h.title.c_str());
    gel_host_invalidate(h);
}

inline void gel_host_set_handlers(GelHost &h, GelHostPaintFn paint,
                                  GelHostInputFn input, void *ud,
                                  GelHostCloseFn close = nullptr) {
    h.paint_fn = paint;
    h.input_fn = input;
    h.close_fn = close;
    h.user = ud;
}

inline void gel_host_begin_size(GelHost &h, int edge) {
    if (!h.hwnd || h.kind != GelHostKind::Floating || edge == 0) return;
    RECT wr{};
    GetWindowRect(h.hwnd, &wr);
    POINT pt{};
    GetCursorPos(&pt);
    h.sizing = true;
    h.size_edge = edge;
    h.size_anchor_x = pt.x;
    h.size_anchor_y = pt.y;
    h.size_orig_x = wr.left;
    h.size_orig_y = wr.top;
    h.size_orig_w = wr.right - wr.left;
    h.size_orig_h = wr.bottom - wr.top;
    SetCapture(h.hwnd);
}

inline void gel_host_apply_size(GelHost &h) {
    if (!h.sizing || !h.hwnd) return;
    POINT pt{};
    GetCursorPos(&pt);
    int dx = pt.x - h.size_anchor_x;
    int dy = pt.y - h.size_anchor_y;
    int x = h.size_orig_x, y = h.size_orig_y;
    int w = h.size_orig_w, hh = h.size_orig_h;
    if (h.size_edge & kGelHostSizeRight) w = h.size_orig_w + dx;
    if (h.size_edge & kGelHostSizeBottom) hh = h.size_orig_h + dy;
    if (h.size_edge & kGelHostSizeLeft) {
        w = h.size_orig_w - dx;
        x = h.size_orig_x + dx;
    }
    if (h.size_edge & kGelHostSizeTop) {
        hh = h.size_orig_h - dy;
        y = h.size_orig_y + dy;
    }
    if (w < h.min_w) {
        if (h.size_edge & kGelHostSizeLeft) x -= (h.min_w - w);
        w = h.min_w;
    }
    if (hh < h.min_h) {
        if (h.size_edge & kGelHostSizeTop) y -= (h.min_h - hh);
        hh = h.min_h;
    }
    SetWindowPos(h.hwnd, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
}

inline int gel_host_size_edge_at(const GelHost &h, int x, int y) {
    if (h.kind != GelHostKind::Floating) return 0;
    int W = h.canvas.width(), H = h.canvas.height();
    if (h.gel.grip.w > 0 && h.gel.grip.contains(x, y))
        return kGelHostSizeRight | kGelHostSizeBottom;
    const int edge = 4;
    int e = 0;
    if (x < edge) e |= kGelHostSizeLeft;
    if (x >= W - edge) e |= kGelHostSizeRight;
    if (y < edge) e |= kGelHostSizeTop;
    if (y >= H - edge) e |= kGelHostSizeBottom;
    // Pure top edge is title move, not size (corners still size).
    if (e == kGelHostSizeTop) return 0;
    return e;
}

inline void gel_host_request_close(GelHost &h) {
    if (h.close_fn) h.close_fn(h, h.user);
    else {
        h.visible = false;
        if (h.hwnd) ShowWindow(h.hwnd, SW_HIDE);
    }
}

inline LRESULT CALLBACK gel_host_wnd_proc(HWND hwnd, UINT msg, WPARAM wp,
                                          LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTA *>(lp);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    GelHost *hp = reinterpret_cast<GelHost *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (!hp) return DefWindowProcA(hwnd, msg, wp, lp);
    GelHost &h = *hp;

    switch (msg) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_SIZE: {
        int w = LOWORD(lp), hh = HIWORD(lp);
        if (w < 1) w = 1;
        if (hh < 1) hh = 1;
        h.canvas.resize(w, hh);
        if (h.visible) gel_host_invalidate(h);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        gel_host_paint(h);
        gel_host_blit(hwnd, h.canvas);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        h.focused = true;
        gel_host_invalidate(h);
        return 0;
    case WM_KILLFOCUS:
        h.focused = false;
        h.pressed_box = 0;
        gel_host_invalidate(h);
        return 0;
    case WM_ACTIVATE:
        // Dismiss on click-away so the picker never gets "stuck" open when
        // focus has moved to another window (Esc only reaches the focused one).
        if (LOWORD(wp) == WA_INACTIVE && h.close_on_deactivate && h.visible) {
            gel_host_request_close(h);
            return 0;
        }
        return 0;
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        int W = h.canvas.width(), H = h.canvas.height();
        if (pt.x < 0 || pt.y < 0 || pt.x >= W || pt.y >= H) return HTNOWHERE;
        h.gel = gel_layout(0, 0, W, H, gel_host_style(h.kind), h.ap, h.focused);
        if (h.gel.close_box.contains(pt.x, pt.y) ||
            h.gel.min_box.contains(pt.x, pt.y) ||
            h.gel.hatch_box.contains(pt.x, pt.y))
            return HTCLIENT;
        if (gel_host_size_edge_at(h, pt.x, pt.y) != 0) return HTCLIENT;
        if (pt.y < h.gel.title_h) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        SetCapture(hwnd);
        SetFocus(hwnd);
        h.gel = gel_layout(0, 0, h.canvas.width(), h.canvas.height(),
                           gel_host_style(h.kind), h.ap, h.focused);
        if (h.gel.close_box.contains(x, y)) {
            h.pressed_box = 1;
            gel_host_invalidate(h);
            return 0;
        }
        if (h.gel.min_box.contains(x, y)) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        int edge = gel_host_size_edge_at(h, x, y);
        if (edge) {
            gel_host_begin_size(h, edge);
            return 0;
        }
        if (h.input_fn && h.input_fn(h, msg, wp, lp, h.user)) {
            gel_host_invalidate(h);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (h.sizing) {
            h.sizing = false;
            ReleaseCapture();
            return 0;
        }
        if (h.pressed_box == 1) {
            bool on = h.gel.close_box.contains(x, y);
            h.pressed_box = 0;
            if (on) gel_host_request_close(h);
            else gel_host_invalidate(h);
            ReleaseCapture();
            return 0;
        }
        if (h.input_fn && h.input_fn(h, msg, wp, lp, h.user))
            gel_host_invalidate(h);
        ReleaseCapture();
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (h.sizing) {
            gel_host_apply_size(h);
            return 0;
        }
        if (h.input_fn && h.input_fn(h, msg, wp, lp, h.user))
            gel_host_invalidate(h);
        return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_CHAR:
    case WM_KEYDOWN:
        if (h.input_fn && h.input_fn(h, msg, wp, lp, h.user)) {
            gel_host_invalidate(h);
            return 0;
        }
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
            gel_host_request_close(h);
            return 0;
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
        mmi->ptMinTrackSize.x = h.min_w;
        mmi->ptMinTrackSize.y = h.min_h;
        return 0;
    }
    case WM_CLOSE:
        gel_host_request_close(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

inline bool gel_host_create(GelHost &h, HINSTANCE hinst, const GelHostDesc &desc) {
    if (h.hwnd) return true;
    h.ap = desc.ap;
    h.font = desc.font;
    h.kind = desc.kind;
    h.title = desc.title ? desc.title : "Sagrado";
    h.class_name = desc.class_name ? desc.class_name : "SagradoGelHost";
    h.close_on_deactivate = desc.close_on_deactivate;
    h.min_w = std::max(80, desc.min_w);
    h.min_h = std::max(60, desc.min_h);
    int w = std::max(h.min_w, desc.w);
    int hh = std::max(h.min_h, desc.h);

    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = gel_host_wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = h.class_name.c_str();
    // Ignore failure if already registered.
    RegisterClassA(&wc);

    DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN |
                  WS_MINIMIZEBOX;
    h.hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW, h.class_name.c_str(),
                             h.title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                             w, hh, desc.owner, nullptr, hinst, &h);
    if (!h.hwnd) return false;
    h.canvas.resize(w, hh);
    h.visible = false;
    return true;
}

inline void gel_host_destroy(GelHost &h) {
    if (h.hwnd) {
        DestroyWindow(h.hwnd);
        h.hwnd = nullptr;
    }
    h.visible = false;
    h.sizing = false;
}

inline void gel_host_show(GelHost &h, bool show) {
    if (!h.hwnd) return;
    h.visible = show;
    if (!show) {
        ShowWindow(h.hwnd, SW_HIDE);
        return;
    }
    // Centre over owner when possible.
    if (HWND owner = GetWindow(h.hwnd, GW_OWNER)) {
        RECT rc{}, hr{};
        if (GetWindowRect(owner, &rc) && GetWindowRect(h.hwnd, &hr)) {
            int hw = hr.right - hr.left, hh = hr.bottom - hr.top;
            int x = rc.left + ((rc.right - rc.left) - hw) / 2;
            int y = rc.top + ((rc.bottom - rc.top) - hh) / 2;
            SetWindowPos(h.hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
    ShowWindow(h.hwnd, SW_SHOW);
    SetForegroundWindow(h.hwnd);
    gel_host_invalidate(h);
}

inline bool gel_host_is_visible(const GelHost &h) {
    return h.visible && h.hwnd && IsWindowVisible(h.hwnd);
}

} // namespace sagrado
