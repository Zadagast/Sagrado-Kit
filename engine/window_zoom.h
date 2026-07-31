// App-owned Zoom for borderless Ooze Gel (WS_POPUP).
// Wine / WS_POPUP often rejects ShowWindow(SW_MAXIMIZE) — the window flashes
// to the work area then snaps back. Drive Zoom with SetWindowPos + saved rect
// instead (same spirit as app-owned gel resize).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sagrado {

struct WindowZoomState {
    bool zoomed = false;
    RECT restore{};
};

inline void window_zoom_clear(WindowZoomState &st) { st.zoomed = false; }

// Expand to the monitor work area, or restore the pre-zoom rect.
inline void window_zoom_toggle(HWND hwnd, WindowZoomState &st) {
    if (!hwnd) return;
    if (st.zoomed) {
        int w = st.restore.right - st.restore.left;
        int h = st.restore.bottom - st.restore.top;
        if (w < 80) w = 80;
        if (h < 60) h = 60;
        SetWindowPos(hwnd, nullptr, st.restore.left, st.restore.top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        st.zoomed = false;
        return;
    }
    GetWindowRect(hwnd, &st.restore);
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT wa;
    if (mon && GetMonitorInfoA(mon, &mi))
        wa = mi.rcWork;
    else
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(hwnd, nullptr, wa.left, wa.top, wa.right - wa.left,
                 wa.bottom - wa.top, SWP_NOZORDER | SWP_NOACTIVATE);
    st.zoomed = true;
}

} // namespace sagrado
